#ifndef MAX10OUTPUTPLUGIN_H
#define MAX10OUTPUTPLUGIN_H

#include <lichtenstein_plugin.h>

#include <cstddef>
#include <string>
#include <vector>

class MAX10OutputPlugin : public OutputPlugin {
	public:
		MAX10OutputPlugin(void *romData, size_t length);
		~MAX10OutputPlugin();

		static OutputPlugin *create(PluginHandler *handler, void *romData, size_t length);

	public:
		virtual const std::string name(void);

		virtual const unsigned int maxChannels(void);
		virtual int setEnabledChannels(unsigned int channels);

		virtual bool isChannelBusy(unsigned int channel);
		virtual int sendChannelData(unsigned int channel, void *data, size_t length);

	private:
		PluginHandler *handler = nullptr;
};

#endif
