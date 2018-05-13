#ifndef MAX10OUTPUTPLUGIN_H
#define MAX10OUTPUTPLUGIN_H

#include <lichtenstein_plugin.h>

#include <cstddef>

class MAX10OutputPlugin : public OutputPlugin {
	public:
		MAX10OutputPlugin(void *romData, size_t length);
		~MAX10OutputPlugin();

		static OutputPlugin *create(void *romData, size_t length);

	public:
		virtual const std::string name();

		virtual const unsigned int maxChannels();
		virtual int setEnabledChannels(unsigned int channels);

		virtual bool isChannelBusy(unsigned int channel);
		virtual int sendChannelData(unsigned int channel, void *data, size_t length);

	private:
		PluginHandler *handler = nullptr;
};

#endif
