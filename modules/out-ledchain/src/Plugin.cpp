/**
 * Entry file for input/output plugins. This includes the appropriate headers
 * for types, and defines the structures/functions required.
 *
 * Use the template in the root of the repository to create a file customized
 * for your plugin.
 */
#include "LEDChainOutputPlugin.h"

#include <lichtenstein_plugin.h>

#include <uuid/uuid.h>

const unsigned char rev01_uuid[16] = {
	0x84, 0x5D, 0x25, 0xC1, 0xC3, 0xAB, 0x4D, 0x95,
	0x97, 0xA4, 0xE3, 0x3F, 0xF2, 0x35, 0xC1, 0x73
};

/**
 * Initialization function: attach an output plugin to the plugin handler.
 */
PLUGIN_PRIVATE void ledchain_init(PluginHandler *handler) {
	// register the plugin
	handler->registerOutputPlugin(rev01_uuid, LEDChainOutputPlugin::create);
}

/**
 * Destructor function: detach an output plugin to the plugin handler.
 */
PLUGIN_PRIVATE void ledchain_deinit(PluginHandler *handler) {

}

// export the struct
PLUGIN_EXPORT lichtenstein_plugin_t plugin_info = {
	.magic = PLUGIN_MAGIC,
	.clientVersion = PLUGIN_CLIENT_VERSION,
	.type = kPluginTypeOutput,
	.version = 0x00010000,

	.name = "Omega2 PWM Output",
	.author = "Tristan Seifert",
	.license ="BSD 3 Clause",
	.url = "https://github.com/tristanseifert/lichtenstein_client/tree/master/modules/out-ledchain",
	.build = GIT_HASH "/" GIT_BRANCH,
	.compiledOn = COMPILE_TIME,

	.init = ledchain_init,
	.deinit = ledchain_deinit
};
