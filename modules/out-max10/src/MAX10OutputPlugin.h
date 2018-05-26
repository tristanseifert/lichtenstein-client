#ifndef MAX10OUTPUTPLUGIN_H
#define MAX10OUTPUTPLUGIN_H

#include <lichtenstein_plugin.h>

#include <cstddef>
#include <cstdint>

#include <string>
#include <queue>
#include <bitset>

class OutputFrame;

class MAX10OutputPlugin : public OutputPlugin {
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
		void configureSPI(void);
		void allocateFramebuffer(void);

		void reset(void);

	private:
		PluginHandler *handler = nullptr;

		// queue of output frames
		std::queue<OutputFrame *> outFrames;

		// SPI baud rate and chip select
		unsigned int spiBaud = 0;
		int spiCsLine = -1;

		// framebuffer memory
		uint8_t *framebuffer = nullptr;
		size_t framebufferLen = 0;

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
};

#endif
