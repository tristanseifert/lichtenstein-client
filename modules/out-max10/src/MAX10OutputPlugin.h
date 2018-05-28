#ifndef MAX10OUTPUTPLUGIN_H
#define MAX10OUTPUTPLUGIN_H

#include <lichtenstein_plugin.h>

#include <cstddef>
#include <cstdint>

#include <mutex>
#include <tuple>
#include <thread>
#include <atomic>
#include <string>
#include <queue>
#include <bitset>

class OutputFrame;

class MAX10OutputPlugin : public OutputPlugin {
	friend void MAX10ThreadEntry(void *);

	public:
		MAX10OutputPlugin(PluginHandler *handler, void *romData, size_t length);
		~MAX10OutputPlugin();

		static OutputPlugin *create(PluginHandler *handler, void *romData, size_t length);

	public:
		virtual const std::string name(void);

		virtual const unsigned int maxChannels(void);
		virtual int setEnabledChannels(unsigned int channels);

		virtual int queueFrame(OutputFrame *frame);
		virtual int outputChannels(std::bitset<32> &channels);

	private:
		void setUpThread(void);
		void shutDownThread(void);

		void workerEntry(void);

		void configureSPI(void);
		void allocateFramebuffer(void);

		void sendFrameToFramebuffer(OutputFrame *);
		void outputChannelsWithData(void);

		void releaseUnusedFramebufferMem(void);

		int findBlockOfSize(size_t);
		void setBlockState(uint32_t, size_t, bool);
		int getFbBytesFree(void);

		void reset(void);
		int doSpiTransaction(void *, void *, size_t);

		int sendSpiCommand(uint8_t command, void *read, void *write, size_t length) {
			return this->sendSpiCommand(command, nullptr, 0, read, write, length);
		}
		int sendSpiCommand(uint8_t, void *, size_t, void *, void *, size_t);

		int readStatusReg(std::bitset<16> &status);

		int writePeriphMem(uint32_t, void *, size_t);
		int writePeriphReg(unsigned int, uint32_t, uint16_t);

	private:
		// commands written to SPI device
		enum {
			kCommandReadStatus	= 0x00,
			kCommandWriteMem	= 0x01,
			kCommandWriteReg	= 0x02,
		};

		// commands written on pipe
		enum {
			kWorkerNOP,
			kWorkerShutdown,
			kWorkerCheckQueue,
			kWorkerOutputAllChannels,
		};

	private:
		PluginHandler *handler = nullptr;

		// worker thread
		std::thread *worker = nullptr;
		std::atomic_bool run;

		// pipe for communicating with worker
		int workerPipeRead = -1;
		int workerPipeWrite = -1;

		// queue of output frames
		std::queue<OutputFrame *> outFrames;
		// lock protecting the queue
		std::mutex outFramesMutex;

		// list of (channel, address, length) to output
		std::vector<std::tuple<unsigned int, uint32_t, uint16_t>> channelOutputMap;
		// list of (channel, address, length) currently outputting
		std::vector<std::tuple<unsigned int, uint32_t, uint16_t>> activeChannels;

		// SPI baud rate, device file, etc
		unsigned int i2cEeepromAddr = 0;

		unsigned int spiBaud = 0;
		std::string spiDeviceFile;

		// SPI device handle
		unsigned int spiDevice = -1;

		// framebuffer memory
		// uint8_t *framebuffer = nullptr;
		size_t framebufferLen = 0;

		// channels to output
		std::bitset<32> channelsToOutput;

		/**
		 * Bitmap of used memory in the framebuffer: each bit represents a span
		 * of N bytes. When the bit is set (true), that span of N bytes is
		 * in use. Index 0 refers to addres 0 in the framebuffer, 1 to N,
		 * and so forth.
		 *
		 * N is defined as kFBMapBlockSize in the .cpp file. Smaller values save
		 * host memory, but force a more coarse allocation of framebuffer memory.
		 */
		std::vector<bool> fbUsedMap;

	private:
		size_t framesDroppedDueToInsufficientMem = 0;
};

#endif
