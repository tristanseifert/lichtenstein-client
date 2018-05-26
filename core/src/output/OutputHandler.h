#ifndef OUTPUTHANDLER_H
#define OUTPUTHANDLER_H

#include <bitset>
#include <cstddef>

class INIReader;
class OutputPlugin;
class LichtensteinPluginHandler;

class OutputFrame;

class OutputHandler {
	public:
		OutputHandler(INIReader *config, LichtensteinPluginHandler *pluginHandler);
		~OutputHandler();

	public:
		int queueOutputFrame(OutputFrame *frame);
		int outputChannels(std::bitset<32> &channels);

	private:
		void loadPlugin(void *rom, size_t romLen);

	private:
		INIReader *config = nullptr;

		LichtensteinPluginHandler *pluginHandler = nullptr;
		OutputPlugin *plugin = nullptr;
};

#endif
