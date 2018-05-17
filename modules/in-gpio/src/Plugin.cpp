/**
 * Entry file for input/output plugins. This includes the appropriate headers
 * for types, and defines the structures/functions required.
 *
 * Use the template in the root of the repository to create a file customized
 * for your plugin.
 */
#include "GPIOInputPlugin.h"

#include <lichtenstein_plugin.h>

#include <uuid/uuid.h>

const static unsigned char input_uuid[16] = {
	0x30, 0xCD, 0x58, 0x04, 0x28, 0xBE, 0x46, 0x79,
	0x9B, 0x54, 0x78, 0x77, 0xA5, 0x4D, 0x6D, 0xA7
};

/**
 * Initialization function: attach an input plugin to the plugin handler.
 */
PLUGIN_PRIVATE void gpio_init(PluginHandler *handler) {
	// register the plugin
	handler->registerInputPlugin(input_uuid, GPIOInputPlugin::create);
}

/**
 * Destructor function: clean up a previously initialized plugin.
 */
PLUGIN_PRIVATE void gpio_deinit(PluginHandler *handler) {

}

// export the struct
PLUGIN_EXPORT lichtenstein_plugin_t plugin_info = {
	.magic = PLUGIN_MAGIC,
	.clientVersion = PLUGIN_CLIENT_VERSION,
	.type = kPluginTypeInput,
	.version = 0x00010000,

	.name = "sysfs-based GPIO inputs",
	.author = "Tristan Seifert",
	.license ="BSD 3 Clause",
	.url = "https://github.com/tristanseifert/lichtenstein_client/tree/master/modules/out-max10",
	.build = GIT_HASH "/" GIT_BRANCH,
	.compiledOn = COMPILE_TIME,

	.init = gpio_init,
	.deinit = gpio_deinit
};
