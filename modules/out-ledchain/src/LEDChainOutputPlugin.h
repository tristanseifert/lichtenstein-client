#ifndef LEDCHAINOUTPUTPLUGIN_H
#define LEDCHAINOUTPUTPLUGIN_H

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

#ifdef __linux__
#include <libkmod/libkmod.h>
#endif

class OutputFrame;

class LEDChainOutputPlugin : public OutputPlugin {
	friend void LEDChainThreadEntry(void *);

	public:
		LEDChainOutputPlugin(PluginHandler *handler, void *romData, size_t length);
		~LEDChainOutputPlugin();

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

		void readConfig(void);

		void loadModule(void);
		void unloadModule(void);

		void reset(void);

		void doOutputTest(void);

	private:
		// commands written on pipe
		enum {
			kWorkerNOP,
			kWorkerShutdown,
			kWorkerCheckQueue,
			kWorkerOutputChannels,
		};

	private:
		// PWM channels supported, starting with 0
		static const int numChannels = 2;

		// maximum number of LEDs per channel
		static const int maxLedsPerChannel = 300;

	private:
#ifdef __linux__
		// libkmod context
		struct kmod_ctx *kmodCtx;
#endif

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


		// channels to output
		std::bitset<32> channelsToOutput;


		// configuration for output channels
		int numLeds[LEDChainOutputPlugin::numChannels];
		int ledType[LEDChainOutputPlugin::numChannels];
};

#endif
