#include "OutputHandler.h"

#include "OutputFrame.h"
#include "../plugin/LichtensteinPluginHandler.h"

#include <glog/logging.h>
#include <INIReader.h>

/**
 * Attempts to load the output plugin specified in the configuration file, then
 * sets up the plugin for outputting.
 */
OutputHandler::OutputHandler(INIReader *_config, LichtensteinPluginHandler *_pluginHandler) :
 	config(_config), pluginHandler(_pluginHandler) {
	// TODO: actually read the option ROM
	this->loadPlugin(nullptr, 0);
}

/**
 * De-allocates the output plugin.
 */
OutputHandler::~OutputHandler() {
	delete this->plugin;
}



/**
 * Loads the plugin and instantiates it.
 */
void OutputHandler::loadPlugin(void *rom, size_t romLen) {
	// get the factory function
	std::string uuid = this->config->Get("output", "module", "");

	try {
		this->plugin = this->pluginHandler->initOutputPluginByUUID(uuid, rom, romLen);
	} catch(std::out_of_range e) {
		LOG(FATAL) << "Couldn't load output plugin with UUID '" << uuid << "'";
	}
}



/**
 * Queues frames for output.
 */
int OutputHandler::queueOutputFrame(OutputFrame *frame) {
	return this->plugin->queueFrame(frame);
}

/**
 * Actually outputs the given channels.
 */
int OutputHandler::outputChannels(std::bitset<32> &channels) {
	return this->plugin->outputChannels(channels);
}
