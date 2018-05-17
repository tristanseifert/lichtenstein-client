#include "MAX10OutputPlugin.h"

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

bool MAX10OutputPlugin::isChannelBusy(unsigned int channel) {
	return false;
}

int MAX10OutputPlugin::sendChannelData(unsigned int channel, void *data, size_t length) {
	return -1;
}
