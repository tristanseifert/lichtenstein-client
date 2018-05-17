/**
 * Defines an abstract class that all output plugins should subclass.
 */
#ifndef OUTPUTPLUGIN_H
#define OUTPUTPLUGIN_H

#include <cstddef>

#include <string>

class OutputPlugin {
	public:
		OutputPlugin(void *_romData, size_t _romDataLength) : romData(_romData),
		 	romDataLength(_romDataLength) {

		}
		virtual ~OutputPlugin() {

		}

	// generic information API
	public:
		virtual const std::string name(void) = 0;

		virtual const unsigned int maxChannels(void) = 0;
		virtual int setEnabledChannels(unsigned int channels) = 0;

		virtual bool isChannelBusy(unsigned int channel) = 0;
		virtual int sendChannelData(unsigned int channel, void *data, size_t length) = 0;

	// shared variables
	protected:
		void *romData = nullptr;
		size_t romDataLength = 0;
};

#endif
