#include "LEDChainOutputPlugin.h"

#include <glog/logging.h>
#include <INIReader.h>

#include <mutex>
#include <thread>
#include <atomic>
#include <stdexcept>
#include <string>
#include <vector>
#include <sstream>

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/ioctl.h>

#include <OutputFrame.h>

// SPI stuff
#ifdef __linux__
	#include <linux/types.h>
	#include <linux/spi/spidev.h>

	#include <libkmod.h>
#endif

// static initializers
const std::string LEDChainOutputPlugin::deviceFiles[] = {
	"/dev/ledchain0",
	"/dev/ledchain1"
};

static int parseCsvList(std::string &in, std::vector<std::string> &out);



/**
 * Trampoline to get into the worker thread
 */
void LEDChainThreadEntry(void *ctx) {
	(static_cast<LEDChainOutputPlugin *>(ctx))->workerEntry();
}

/**
 * libkmod logging function
 */
void kmod_log(void *log_data, int priority, const char *file, int line, const char *fn, const char *format, va_list args) {

}



/**
 * Invokes the constructor for the plugin and returns it.
 */
OutputPlugin *LEDChainOutputPlugin::create(PluginHandler *handler, void *romData, size_t length) {
	return new LEDChainOutputPlugin(handler, romData, length);
}

/**
 * Initializes the output plugin, allocating memory and setting up the hardware.
 */
LEDChainOutputPlugin::LEDChainOutputPlugin(PluginHandler *_handler, void *romData, size_t length) : handler(_handler), OutputPlugin(romData, length) {
	// get config information
	this->readConfig();

	// load kernel module
	this->loadModule();

	// reset hardware
	this->reset();

	// now, set up the worker thread
	this->setUpThread();
}



/**
 * Resets the hardware and de-allocates memory.
 */
LEDChainOutputPlugin::~LEDChainOutputPlugin() {
	int err;

	// clean up the thread
	this->shutDownThread();

	// unload module
	this->unloadModule();
}



/**
 * Sets up the worker thread, as well as a pipe with which we communicate with
 * that thread.
 */
void LEDChainOutputPlugin::setUpThread(void) {
	int err = 0;
	int fd[2];

	// make sure there's no existing thread
	CHECK(this->worker == nullptr) << "Trying to start thread when it's already running";

	// create the thread communicating pipe
	err = pipe(fd);
	PLOG_IF(FATAL, err != 0) << "Couldn't create pipe";

	this->workerPipeRead = fd[0];
	this->workerPipeWrite = fd[1];

	// enable non-blocking IO on the read end of the pipe
	int flags = fcntl(this->workerPipeRead, F_GETFL);
	PCHECK(flags != -1) << "Couldn't get flags";

	flags |= O_NONBLOCK;

	err = fcntl(this->workerPipeRead, F_SETFL, flags);
	PCHECK(err != -1) << "Couldn't set flags";


	// set run flag and create thread
	this->run = true;
	this->worker = new std::thread(LEDChainThreadEntry, this);
}

/**
 * Sends the thread the shutdown command, then waits for it to quit.
 */
void LEDChainOutputPlugin::shutDownThread(void) {
	int err = 0;

	// clear the running flag
	this->run = false;

	// write to the pipe to unblock the thread
	int blah = kWorkerShutdown;
	err = write(this->workerPipeWrite, &blah, sizeof(blah));

	if(err < 0) {
		PLOG(ERROR) << "Couldn't write to pipe, shit's fucked";

		// if we can't write to the pipe we're fucked, just kill the thread
		delete this->worker;
	} else {
		// wait for thread to terminate
		this->worker->join();

		delete this->worker;
	}

	// clear the pointer
	this->worker = nullptr;
}

/**
 * Entry point for the worker thread.
 */
void LEDChainOutputPlugin::workerEntry(void) {
	int err = 0, rsz;
	fd_set readfds;

	// set up hardware
	this->reset();

	// perform power-on test
	this->doOutputTest();

	// main loop
	while(this->run) {
		// set up the read descriptors we wait on
		int max = this->workerPipeRead;

		FD_ZERO(&readfds);
		FD_SET(this->workerPipeRead, &readfds);

		// block on the file descriptors
		err = select((max + 1), &readfds, nullptr, nullptr, nullptr);

		if(err < 0) {
			PLOG(INFO) << "select failed";
			continue;
		}

		// did the worker pipe become ready?
		if(FD_ISSET(this->workerPipeRead, &readfds)) {
			// read an int from the pipe
			int command = kWorkerNOP;
			rsz = read(this->workerPipeRead, &command, sizeof(command));

			if(rsz <= 0) {
				PLOG(WARNING) << "Couldn't read from worker pipe:";
			} else {
				// process the command
				switch(command) {
					// No-op, do nothing
					case kWorkerNOP: {
						break;
					}

					// Shut down the thread
					case kWorkerShutdown: {
						LOG(INFO) << "Shutting down worker thread";
						break;
					}

					// Pop a frame off the queue and process it
					case kWorkerCheckQueue: {
						// loop while there is stuff in the queue
						bool haveMore = true;

						while(haveMore) {
							OutputFrame *frame = nullptr;

							// pop the frame
							try {
								// get a lock on the frame queue
								std::lock_guard<std::mutex> lck(this->outFramesMutex);

								// get the leading element
								frame = this->outFrames.front();
								this->outFrames.pop();

								// are there more?
								haveMore = !this->outFrames.empty();
							} catch (std::logic_error &ex) {
								LOG(FATAL) << "Couldn't get lock: " << ex.what();

								// force exit the loop
								haveMore = false;
							}

							// process the frame
							if(frame != nullptr) {
								// TODO: implement
							}
						}

						break;
					}

					// outputs all channels for which we have data
					case kWorkerOutputChannels: {
						// TODO: implement
						break;
					}

					// shouldn't get here
					default: {
						LOG(WARNING) << "Unknown command " << command;
						break;
					}
				}
			}
		}
	}

	// clean up
	this->reset();
}



/**
 * Reads the configuration for the plugin.
 */
void LEDChainOutputPlugin::readConfig(void) {
	INIReader *config = this->handler->getConfig();

	// clear config structs
	memset(this->numLeds, 0, sizeof(this->numLeds));
	memset(this->ledType, 0, sizeof(this->ledType));

	// first, read the number of leds
	std::string numList = config->Get("output_ledchain", "leds", "");

	if(numList != "") {
		std::vector<std::string> strings;

		// parse the list
		int channels = parseCsvList(numList, strings);
		CHECK(channels <= LEDChainOutputPlugin::numChannels && channels > 0) << "Invalid number of channels for LED count: " << channels;

		// copy the number to the LED number array
		int i = 0;

		for(std::string str : strings) {
			// convert and validate
			int leds = std::stoi(str);

			CHECK(leds >= 0) << "LEDs must be a positive number for channel " << i;
			CHECK(leds <= LEDChainOutputPlugin::maxLedsPerChannel) << "Can't support " << leds << " for channel " << i << "; max is " << LEDChainOutputPlugin::maxLedsPerChannel;

			// if it's valid, store it for later
			this->numLeds[i++] = leds;
		}
	} else {
		LOG(WARNING) << "LED count was omitted, check output_ledchain.leds";
	}

	// now, read the type of LED
	std::string typeList = config->Get("output_ledchain", "types", "");

	if(typeList != "") {
		std::vector<std::string> strings;

		// parse the list
		int channels = parseCsvList(typeList, strings);
		CHECK(channels <= LEDChainOutputPlugin::numChannels && channels > 0) << "Invalid number of channels for types: " << channels;

		// copy the number to the LED type array
		int i = 0;

		for(std::string str : strings) {
			// convert and validate
			int type = std::stoi(str);

			CHECK(type >= 0 && type <= 4) << "Invalid LED type " << type << " for channel " << i;

			// if it's valid, store it for later
			this->ledType[i++] = type;
		}
	} else {
		LOG(WARNING) << "LED type was omitted, defaulting to WS2811; check output_ledchain.types";
	}
}



/**
 * Loads the ledchain kernel module.
 */
void LEDChainOutputPlugin::loadModule(void) {
	int err;

	// get path to module
	INIReader *config = this->handler->getConfig();

	std::string path = config->Get("output_ledchain", "module_path", "");

	size_t kmodPathLen = strlen(path.c_str()) + 8;

	char *kmodPath = static_cast<char *>(malloc(kmodPathLen));
	memset(kmodPath, 0, kmodPathLen);

	strncpy(kmodPath, path.c_str(), kmodPathLen);

	LOG(INFO) << "Loading ledchain kernel module from " << kmodPath;

	// build the parameter string
	std::stringstream paramStream;

	for(int i = 0; i < LEDChainOutputPlugin::numChannels; i++) {
		// is this channel active?
		if(this->numLeds[i] > 0) {
			// channel marker
			paramStream << "ledchain" << i << "=";

			// inverted (always zero), count, type
			paramStream << "0," << this->numLeds[i] << "," << this->ledType[i];

			// add a space for the next channel
			paramStream << " ";
		}
	}

	// get the param C string and load the module
	size_t paramsLen = strlen(paramStream.str().c_str()) + 8;

	char *params = static_cast<char *>(malloc(paramsLen));
	memset(params, 0, paramsLen);

	strncpy(params, paramStream.str().c_str(), paramsLen);

#ifdef __linux__
	// initialize libkmod
	this->kmodCtx = kmod_new(nullptr, nullptr);
	CHECK(this->kmodCtx != nullptr) << "Couldn't initialize libkmod";

	// kmod_set_log_fn(this->kmodCtx, kmod_log, nullptr);

	// get a reference to the module
	err = kmod_module_new_from_path(this->kmodCtx, kmodPath, &this->kmod);
	CHECK(err == 0) << "Couldn't get reference to ledchain module: " << err;

	// now, attempt to load it
	LOG(INFO) << "Loading ledchain with params: " << params;

	err = kmod_module_insert_module(this->kmod, 0, params);
	CHECK(err == 0) << "Couldn't insert module: " << err;

	// if we get here, the module is loaded :D
#endif

	// clean up
	free(kmodPath);
	free(params);
}

/**
 * Unloads the ledchain kernel module.
 */
void LEDChainOutputPlugin::unloadModule(void) {
	int err;

#ifdef __linux__
	// attempt to remove it
	err = kmod_module_remove_module(this->kmod, KMOD_REMOVE_FORCE);
	CHECK(err == 0) << "Couldn't remove module: " << err;

	// Lastly, clean up libkmod context
	kmod_unref(this->kmodCtx);
#endif
}

/**
 * Resets the output hardware.
 *
 * This essentially just forces a reset pulse on the outputs and
 * makes sure that the level shifters aren't driving the outputs.
 */
void LEDChainOutputPlugin::reset(void) {

}



/**
 * Queues a frame for output: this just pushes the frame into an internal queue
 * that the background worker thread processes.
 */
int LEDChainOutputPlugin::queueFrame(OutputFrame *frame) {
	int err;

	// sanity checks
	CHECK(frame != nullptr) << "What the fuck, frame is nullptr?";

	// try to push it on the queue
	try {
		// get a lock on the frame queue
		std::lock_guard<std::mutex> lck(this->outFramesMutex);

		// just push it into the queue
		this->outFrames.push(frame);
	} catch (std::logic_error &ex) {
		LOG(ERROR) << "Couldn't get lock: " << ex.what();
		return -1;
	}

	// notify the worker thread
	int cmd = kWorkerCheckQueue;
	err = write(this->workerPipeWrite, &cmd, sizeof(cmd));
	PLOG_IF(ERROR, err < 0) << "Couldn't write to pipe, shit's fucked: " << err;

	// done
	return 0;
}

/**
 * Outputs all channels whose bits are set. This uses channel numbering relative
 * to the first output channel on the output chip.
 */
int LEDChainOutputPlugin::outputChannels(std::bitset<32> &channels) {
	int err;

	// copy channels to output
	this->channelsToOutput = channels;

	// notify worker thread
	int cmd = kWorkerOutputChannels;
	err = write(this->workerPipeWrite, &cmd, sizeof(cmd));

	// was there an error writing to the pipe?
	if(err < 0) {
		PLOG(ERROR) << "Couldn't write to pipe, shit's fucked: " << err;
		return err;
	}

	// if we get down here, we presumeably output everything
	return 0;
}



/**
 * Returns the run-time name of this output plugin.
 */
const std::string LEDChainOutputPlugin::name(void) {
	return "Omega2 PWM Output";
}

/**
 * Returns the maximum number of supported output channels: the Omega2 only has
 * two PWM channels we can access externally.
 */
const unsigned int LEDChainOutputPlugin::maxChannels(void) {
	return 2;
}

/**
 * Sets which channels are enabled for output.
 */
int LEDChainOutputPlugin::setEnabledChannels(unsigned int channels) {
	return 0;
}



/**
 * Power-on test: this will cycle through all 16 output channels, testing each
 * of the channels by illuminating the red, green, blue, and white LEDs.
 *
 * This handles each channel one after another so that we don't overload the
 * power supply.
 *
 * TODO: handle non-RGBW (aka just plain RGB) LED strips also.
 */
void LEDChainOutputPlugin::doOutputTest(void) {
	// TODO: implement
}



/**
 * Parses a comma-separated list of objects, inserting each of them into the
 * output vector.
 */
static int parseCsvList(std::string &in, std::vector<std::string> &out) {
	int numItems = 0;

	// create a stream of input data
	std::stringstream ss(in);

	// iterate while there is more data in the stream
	while(ss.good()) {
		// read a line separated by a comma
		std::string substr;
		getline(ss, substr, ',');

		// then push it on the output vector
		out.push_back(substr);
		numItems++;
	}

	return numItems;
}
