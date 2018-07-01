#include "MAX10OutputPlugin.h"

#include <glog/logging.h>
#include <INIReader.h>

#include <mutex>
#include <thread>
#include <atomic>
#include <stdexcept>

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
#endif 

// size of each "block" in the framebuffer allocation map
static const size_t kFBMapBlockSize = 16;



/**
 * Trampoline to get into the worker thread
 */
void MAX10ThreadEntry(void *ctx) {
	(static_cast<MAX10OutputPlugin *>(ctx))->workerEntry();
}



/**
 * Invokes the constructor for the plugin and returns it.
 */
OutputPlugin *MAX10OutputPlugin::create(PluginHandler *handler, void *romData, size_t length) {
	return new MAX10OutputPlugin(handler, romData, length);
}

/**
 * Initializes the output plugin, allocating memory and setting up the hardware.
 */
MAX10OutputPlugin::MAX10OutputPlugin(PluginHandler *_handler, void *romData, size_t length) : handler(_handler), OutputPlugin(romData, length) {
	// get SPI settings
	this->configureHardware();
	// allocate framebuffer
	this->allocateFramebuffer();

	// reset the output chip
	this->reset();

	// set up the worker thread
	this->setUpThread();
}

/**
 * Allocates a shadow copy of the framebuffer on the board: received frames are
 * "fit" into this buffer and will later be transferred to the chip.
 */
void MAX10OutputPlugin::allocateFramebuffer(void) {
	INIReader *config = this->handler->getConfig();

	// TODO: read from EEPROM data
	this->framebufferLen = config->GetInteger("output_max10", "fbsize", 131072);

	if((this->framebufferLen % kFBMapBlockSize) != 0) {
		LOG(FATAL) << "Framebuffer length must be a multiple of "
			<< kFBMapBlockSize << "; config specified " << this->framebufferLen;
	}

	// this->framebuffer = static_cast<uint8_t *>(malloc(this->framebufferLen));
	// CHECK(this->framebuffer != nullptr) << "Couldn't allocate framebuffer!";

	// allocate the framebuffer use
	size_t blocks = this->framebufferLen / kFBMapBlockSize;

	this->fbUsedMap.clear();
	this->fbUsedMap.reserve(blocks);

	for(size_t i = 0; i < blocks; i++) {
		this->fbUsedMap.push_back(false);
	}
}



/**
 * Resets the hardware and de-allocates memory.
 */
MAX10OutputPlugin::~MAX10OutputPlugin() {
	int err;

	// clean up the thread
	this->shutDownThread();

	// de-allocate framebuffer
	// if(this->framebuffer) {
		// free(this->framebuffer);
	// }

	// clean up hardware
	this->cleanUpHardware();
}



/**
 * Sets up the worker thread, as well as a pipe with which we communicate with
 * that thread.
 */
void MAX10OutputPlugin::setUpThread(void) {
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
	this->worker = new std::thread(MAX10ThreadEntry, this);
}

/**
 * Sends the thread the shutdown command, then waits for it to quit.
 */
void MAX10OutputPlugin::shutDownThread(void) {
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
void MAX10OutputPlugin::workerEntry(void) {
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
								this->sendFrameToFramebuffer(frame);
							}
						}

						break;
					}

					// outputs all channels for which we have data
					case kWorkerOutputAllChannels: {
						// check to see which parts of memory we can release
						// TODO: does this release memory we are outputting with?
						this->releaseUnusedFramebufferMem();

						// attempt to output data for all channels
						this->outputChannelsWithData();
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
 * Attempts to send the frame to the framebuffer. This will try to find a place
 * in the framebuffer that's available, then sends the data and acknowledges
 * receipt of the frame.
 */
void MAX10OutputPlugin::sendFrameToFramebuffer(OutputFrame *frame) {
	int err, addr;

	// try to find a spot in the framebuffer
	addr = this->findBlockOfSize(frame->getDataLen());

	if(addr == -1) {
		// increment counter and log a message
		this->framesDroppedDueToInsufficientMem++;

		LOG(WARNING) << "Couldn't find memory for frame " << frame;
		LOG_EVERY_N(WARNING, 10) << "Dropped "
			<< this->framesDroppedDueToInsufficientMem << "frames due to "
			<< "insufficient memory";

		// nack the frame
		this->handler->acknowledgeFrame(frame, true);
		return;
	}

	// copy it into the framebuffer
	// uint8_t *ptr = this->framebuffer + addr;
	// memcpy(ptr, frame->getData(), frame->getDataLen());

	// mark that region of data as used in the framebuffer
	VLOG(2) << "Allocating " << frame->getDataLen() << " bytes for channel "
		<< frame->getChannel() << " at 0x" << std::hex << addr;

	this->setBlockState(addr, frame->getDataLen(), true);

	// insert it in the vector
	this->channelOutputMap.push_back(std::make_tuple(frame->getChannel(), addr,
		frame->getDataLen()));

	// send the frame from the framebuffer
	err = this->writePeriphMem(addr, frame->getData(), frame->getDataLen());

	if(err < 0) {
		LOG(ERROR) << "Couldn't write peripheral memory: " << err;

		// nack the frame
		this->handler->acknowledgeFrame(frame, true);

		// reverse the memory reservation
		this->setBlockState(addr, frame->getDataLen(), false);

		// also remove the entry from the output map
		for(int i = 0; i < this->channelOutputMap.size(); i++) {
			auto tuple = this->channelOutputMap[i];

			// does the channel match?
			if(std::get<0>(tuple) == frame->getChannel()) {
				// if so, remove it
				this->channelOutputMap.erase(this->channelOutputMap.begin() + i);
			}
		}

		return;
	}

	// if we get down here, nothing went wrong
	this->handler->acknowledgeFrame(frame);
}



/**
 * Finds a contiguous block in framebuffer memory big enough to fit `size` bytes
 * then returns its address, or -1 if not possible.
 */
int MAX10OutputPlugin::findBlockOfSize(size_t size) {
	int addr = 0;
	int freeBytesFound = 0;

	VLOG(3) << "Requested allocation of " << size << " bytes";

	// make sure it's not larger than the framebuffer
	if(size > this->framebufferLen) {
		LOG(ERROR) << "Requested allocation of " << size << " bytes, larger "
			<< "than framebuffer of " << this->framebufferLen << " bytes";
		return -1;
	}

	// iterate over every block in the framebuffer
	int totalBlocks = (this->framebufferLen / kFBMapBlockSize);

	for(int i = 0; i < totalBlocks; i++) {
		// is this block empty?
		if(this->fbUsedMap[i] == false) {
			// if size counter is zero, store address
			if(freeBytesFound == 0) {
				addr = (i * kFBMapBlockSize);
			}

			// if so, increment the size couner
			freeBytesFound += kFBMapBlockSize;

			// is the block size bigger or equal to the requested?
			if(freeBytesFound >= size) {
				return addr;
			}
		}
		// otherwise, reset the counter
		else {
			freeBytesFound = 0;
		}
	}

	// if we get down here, no free block could be found
	LOG(ERROR) << "Couldn't satisfy framebuffer allocation of " << size
	 	<< " bytes; have " << this->getFbBytesFree() << " bytes free";
	return -1;
}

/**
 * Sets the state of a particular block in memory.
 */
void MAX10OutputPlugin::setBlockState(uint32_t addr, size_t size, bool state) {
	VLOG(3) << "Setting state of " << size << " bytes at 0x" << std::hex << addr
		<< " to " << state;

	// convert address and length into blocks
	unsigned int start = (addr / kFBMapBlockSize);
	int count = (size / kFBMapBlockSize) + (size % kFBMapBlockSize != 0);

	// set all of them to the same value
	for(int i = 0; i < count; i++) {
		this->fbUsedMap[start++] = state;
	}
}

/**
 * Calculates the number of free bytes of framebuffer memory, aka all elements
 * in the framebuffer useage map that are false.
 */
int MAX10OutputPlugin::getFbBytesFree(void) {
	int blocks = 0;

	// iterate over the ENTIRE framebuffer
	for(int i = 0; i < this->fbUsedMap.size(); i++) {
		// is this block unset?
		if(this->fbUsedMap[i] == false) {
			// if so, increment block counter
			blocks++;
		}
	}

	// convert blocks to bytes
	return (blocks * kFBMapBlockSize);
}



/**
 * Outputs channels that have data.
 */
void MAX10OutputPlugin::outputChannelsWithData(void) {
	int err;

	// for each requested channel, do we have an output mapping?
	for(int i = 0; i < this->channelsToOutput.size(); i++) {
		// is the channel set?
		if(this->channelsToOutput[i]) {
			// do we have a mapping for that channel?
			for(int j = 0; j < this->channelOutputMap.size(); j++) {
				auto tuple = this->channelOutputMap[j];

				unsigned int channel = std::get<0>(tuple);

				// we do
				if(channel == i) {
					uint32_t addr = std::get<1>(tuple);
					uint16_t length = std::get<2>(tuple);

					// write output regs
					err = this->writePeriphReg(channel, addr, length);
					PLOG_IF(ERROR, err < 0) << "Couldn't write registers for "
						<< "channel " << channel << ": " << err;

					// move it to the list of outputting channels
					this->activeChannels.push_back(tuple);

					// remove the tuple
					this->channelOutputMap.erase(this->channelOutputMap.begin() + j);
				}
			}
		}
	}
}

/**
 * Releases memory in the framebuffer that was previously allocated to an active
 * channel, but is no longer used: this is done by checking whether a channel
 * is outputting data (via a status register read,) and if not, finding all
 * entries in the `activeChannels` vector for that channel, and marking the
 * blocks of memory specified by those entries as free.
 *
 * This should be run immediately before channels are set to output data again,
 * or, in other words, after as long of a wait as possible after commanding
 * channels to output data.
 */
void MAX10OutputPlugin::releaseUnusedFramebufferMem(void) {
	std::bitset<16> status;
	int err;

	// read status register
	err = this->readStatusReg(status);

	if(err != 0) {
		LOG(ERROR) << "Couldn't read status register: " << err;
		return;
	}

	// iterate over all active channels
	for(int i = 0; i < status.size(); i++) {
		// is this channel no longer active?
		if(status[i] == false) {
again: ;
			// try to find an entry for it
			for(int j = 0; j < this->activeChannels.size(); j++) {
				auto tuple = this->activeChannels[j];

				// does the channel number match?
				if(std::get<0>(tuple) == i) {
					unsigned int addr = std::get<1>(tuple);
					size_t size = std::get<2>(tuple);

					VLOG(2) << "Freeing unused memory: channel " << i << ": "
						<< size << ", bytes at 0x" << std::hex << addr;

					// if so, free the memory from this channel
					this->setBlockState(addr, size, false);

					// remove it!
					this->activeChannels.erase(this->activeChannels.begin() + j);

					// check again whether there's any more for this channel
					// TODO: this can probably be done without goto's
					goto again;
				}
			}
		}
	}
}



/**
 * Queues a frame for output: this just pushes the frame into an internal queue
 * that the background worker thread processes.
 */
int MAX10OutputPlugin::queueFrame(OutputFrame *frame) {
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
int MAX10OutputPlugin::outputChannels(std::bitset<32> &channels) {
	int err;

	// copy channels to output
	this->channelsToOutput = channels;

	// notify worker thread
	int cmd = kWorkerOutputAllChannels;
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
const std::string MAX10OutputPlugin::name(void) {
	return "MAX10 FPGA Output Board";
}

/**
 * Returns the maximum number of supported output channels: this can vary among
 * different versions of the hardware, but for now, it's hard-coded at 16.
 *
 * TODO: Read this out of EEPROM data.
 */
const unsigned int MAX10OutputPlugin::maxChannels(void) {
	return 16;
}

/**
 * Sets which channels are enabled for output. This doesn't do anything here, as
 * we need to explicitly load each channel's registers.
 */
int MAX10OutputPlugin::setEnabledChannels(unsigned int channels) {
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
void MAX10OutputPlugin::doOutputTest(void) {
	int err = 0;

	// allocate a buffer
	const size_t bufferElements = 300;
	uint32_t *buffer = new uint32_t[bufferElements];

	CHECK(buffer != nullptr) << "Couldn't allocate buffer, system is super fucked";

	// Upload a red buffer to memory
	for(int i = 0; i < bufferElements; i++) {
		buffer[i] = 0xFF000000;
	}

	err = this->writePeriphMem(0x0000, buffer, 4 * bufferElements);
	CHECK(err > 0) << "Couldn't write red buffer to memory";

	// Upload a green buffer to memory
	for(int i = 0; i < bufferElements; i++) {
		buffer[i] = 0x00FF0000;
	}

	err = this->writePeriphMem(0x1000, buffer, 4 * bufferElements);
	CHECK(err > 0) << "Couldn't write green buffer to memory";

	// Upload a blue buffer to memory
	for(int i = 0; i < bufferElements; i++) {
		buffer[i] = 0x0000FF00;
	}

	err = this->writePeriphMem(0x2000, buffer, 4 * bufferElements);
	CHECK(err > 0) << "Couldn't write green buffer to memory";

	// Upload a white buffer to memory
	for(int i = 0; i < bufferElements; i++) {
		buffer[i] = 0x000000FF;
	}

	err = this->writePeriphMem(0x3000, buffer, 4 * bufferElements);
	CHECK(err > 0) << "Couldn't write white buffer to memory";

	// Upload a zeroed buffer to memory
	for(int i = 0; i < bufferElements; i++) {
		buffer[i] = 0x00000000;
	}

	err = this->writePeriphMem(0x4000, buffer, 4 * bufferElements);
	CHECK(err > 0) << "Couldn't write zeroed buffer to memory";



	// Now, iterate through each channel
	for(int i = 0; i < 16; i++) {
		// exit if run is false
		if(this->run == false) {
			LOG(WARNING) << "Aborting output POST!";
			return;
		}

		// output red
		err = this->writePeriphReg(i, 0x0000, (4 * bufferElements));
		CHECK(err == 0) << "Couldn't output red buffer for channel " << i;

		usleep(MAX10OutputPlugin::kPOSTChannelWait);

		// output green
		err = this->writePeriphReg(i, 0x1000, (4 * bufferElements));
		CHECK(err == 0) << "Couldn't output green buffer for channel " << i;

		usleep(MAX10OutputPlugin::kPOSTChannelWait);

		// output blue
		err = this->writePeriphReg(i, 0x2000, (4 * bufferElements));
		CHECK(err == 0) << "Couldn't output blue buffer for channel " << i;

		usleep(MAX10OutputPlugin::kPOSTChannelWait);

		// output white
		err = this->writePeriphReg(i, 0x3000, (4 * bufferElements));
		CHECK(err == 0) << "Couldn't output white buffer for channel " << i;

		usleep(MAX10OutputPlugin::kPOSTChannelWait);

		// reset the channel
		err = this->writePeriphReg(i, 0x4000, (4 * bufferElements));
		CHECK(err == 0) << "Couldn't reset channel " << i;
	}

	// log
	LOG(INFO) << "Finished output POST";


	// Clean up
	delete[] buffer;
}
