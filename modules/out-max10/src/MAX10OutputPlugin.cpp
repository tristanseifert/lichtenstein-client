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

#include <OutputFrame.h>

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
	this->configureSPI();
	// allocate framebuffer
	this->allocateFramebuffer();

	// reset the output chip
	this->reset();

	// set up the worker thread
	this->setUpThread();
}

/**
 * Configures the SPI bus.
 */
void MAX10OutputPlugin::configureSPI(void) {
	INIReader *config = this->handler->getConfig();

	// TODO: read from EEPROM data
	this->spiBaud = config->GetInteger("output_max10", "baud", 2500000);

	this->spiCsLine = config->GetInteger("output_max10", "cs", -1);
	CHECK(this->spiCsLine >= 0) << "Invalid chip select line " << this->spiCsLine;
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

	this->framebuffer = static_cast<uint8_t *>(malloc(this->framebufferLen));
	CHECK(this->framebuffer != nullptr) << "Couldn't allocate framebuffer!";

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
	// clean up the thread
	this->shutDownThread();

	// de-allocate framebuffer
	if(this->framebuffer) {
		free(this->framebuffer);
	}
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
		PLOG(ERROR) << "Couldn't write to pipe";

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

		// nack the frame
		this->handler->acknowledgeFrame(frame, true);
		return;
	}

	// copy it into the framebuffer
	uint8_t *ptr = this->framebuffer + addr;
	memcpy(ptr, frame->getData(), frame->getDataLen());

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
		LOG(ERROR) << "Couldn't write to peripheral: " << err;

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
 * channel, but is no longer used.
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
				}
			}
		}
	}
}



const std::string MAX10OutputPlugin::name(void) {
	return "MAX10 FPGA Output Board";
}
const unsigned int MAX10OutputPlugin::maxChannels(void) {
	return 16;
}

int MAX10OutputPlugin::setEnabledChannels(unsigned int channels) {
	return -1;
}



/**
 * Resets the output chip.
 */
void MAX10OutputPlugin::reset(void) {
	// TODO: implement
}

/**
 * Reads the status register to determine which channels are actively sending
 * data.
 */
int MAX10OutputPlugin::readStatusReg(std::bitset<16> &status) {
	// clear the output status bitset
	status.reset();

	// TODO: implement
	return 0;
}

/**
 * Writes the given data into the peripheral's memory.
 *
 * @return The number of bytes written if successful, a negative error code
 * otherwise.
 */
int MAX10OutputPlugin::writePeriphMem(uint32_t addr, void *data, size_t length) {
	// TODO: implement
	return -1;
}

/**
 * Writes the address and length into the given channel's output registers; this
 * will immediately start outputting data for that channel.
 *
 * @return 0 if successful, negative error code otherwise.
 */
int MAX10OutputPlugin::writePeriphReg(unsigned int channel, uint32_t addr, uint16_t length) {
	// TODO: implement
	return -1;
}



/**
 * Queues a frame for output: this could convert the frame to the output
 * representation, for example.
 */
int MAX10OutputPlugin::queueFrame(OutputFrame *frame) {
	int err;

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
	int blah = kWorkerCheckQueue;
	err = write(this->workerPipeWrite, &blah, sizeof(blah));
	PLOG_IF(ERROR, err < 0) << "Couldn't write to pipe: " << err;

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
	int blah = kWorkerOutputAllChannels;
	err = write(this->workerPipeWrite, &blah, sizeof(blah));

	// was there an error writing to the pipe?
	if(err < 0) {
		PLOG(ERROR) << "Couldn't write to pipe: " << err;
		return err;
	}

	// if we get down here, we presumeably output everything
	return 0;
}
