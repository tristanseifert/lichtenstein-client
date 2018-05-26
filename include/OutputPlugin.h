/**
 * Defines an abstract class that all output plugins should subclass.
 */
#ifndef OUTPUTPLUGIN_H
#define OUTPUTPLUGIN_H

#include <cstddef>

#include <string>
#include <bitset>

class OutputFrame;

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

		virtual int queueFrame(OutputFrame *frame) = 0;
		virtual int outputChannels(std::bitset<32> &channels) = 0;

	// shared variables
	protected:
		void *romData = nullptr;
		size_t romDataLength = 0;
};

#endif
