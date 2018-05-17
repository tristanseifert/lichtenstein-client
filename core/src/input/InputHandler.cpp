#include "InputHandler.h"

#include "LichtensteinPluginHandler.h"

#include <glog/logging.h>
#include <INIReader.h>

/**
 * Attempts to load the input plugin specified in the configuration file, then
 * sets up the input event handler.
 */
InputHandler::InputHandler(INIReader *_config, LichtensteinPluginHandler *_pluginHandler) :
 	config(_config), pluginHandler(_pluginHandler) {
	this->loadPlugin();
}

/**
 * De-allocates the input plugin.
 */
InputHandler::~InputHandler() {
	delete this->plugin;
}



/**
 * Loads the plugin and instantiates it.
 */
void InputHandler::loadPlugin(void) {
	// get the factory function
	std::string uuid = this->config->Get("input", "module", "");

	try {
		this->plugin = this->pluginHandler->initInputPluginByUUID(uuid);
	} catch(std::out_of_range e) {
		LOG(FATAL) << "Couldn't load input plugin with UUID '" << uuid << "'";
	}
}
