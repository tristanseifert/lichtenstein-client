#include "MAX10OutputPlugin.h"

#include <OutputFrame.h>

MAX10OutputPlugin::MAX10OutputPlugin(void *romData, size_t length) : OutputPlugin(romData, length) {

}

MAX10OutputPlugin::~MAX10OutputPlugin() {

}

/**
 * Invokes the constructor for the plugin and returns it.
 */
OutputPlugin *MAX10OutputPlugin::create(PluginHandler *handler, void *romData, size_t length) {
	return new MAX10OutputPlugin(romData, length);
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
 * Queues a frame for output: this could convert the frame to the output
 * representation, for example.
 */
int MAX10OutputPlugin::queueFrame(OutputFrame *frame) {
	// TODO: implement
	return -1;
}

/**
 * Outputs all channels whose bits are set. This uses absolute channel
 * numbering.
 */
int MAX10OutputPlugin::outputChannels(std::bitset<32> &channels) {
	// TODO: implement
	return -1;
}
