#ifndef MAX10OUTPUTPLUGIN_H
#define MAX10OUTPUTPLUGIN_H

#include <lichtenstein_plugin.h>

#include <cstddef>
#include <string>
#include <vector>
#include <queue>
#include <bitset>

class OutputFrame;

class MAX10OutputPlugin : public OutputPlugin {
	public:
		MAX10OutputPlugin(void *romData, size_t length);
		~MAX10OutputPlugin();

		static OutputPlugin *create(PluginHandler *handler, void *romData, size_t length);

	public:
		virtual const std::string name(void);

		virtual const unsigned int maxChannels(void);
		virtual int setEnabledChannels(unsigned int channels);

		virtual int queueFrame(OutputFrame *frame);
		virtual int outputChannels(std::bitset<32> &channels);

	private:
		PluginHandler *handler = nullptr;

		// queue of output frames
		std::queue<OutputFrame *> outFrames;
};

#endif
