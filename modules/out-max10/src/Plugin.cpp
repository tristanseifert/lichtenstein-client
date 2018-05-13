/**
 * Entry file for input/output plugins. This includes the appropriate headers
 * for types, and defines the structures/functions required.
 *
 * Use the template in the root of the repository to create a file customized
 * for your plugin.
 */
#include "MAX10OutputPlugin.h"

#include <lichtenstein_plugin.h>

#include <uuid/uuid.h>

const unsigned char rev01_uuid[16] = {
	0xCF, 0xBF, 0x67, 0x17, 0x4C, 0x7B, 0x43, 0x53,
	0x90, 0xC6, 0xD1, 0x4E, 0x13, 0x55, 0x5E, 0x59
};

/**
 * Initialization function: attach an output plugin to the plugin handler.
 */
PLUGIN_PRIVATE void max10_init(PluginHandler *handler) {
	// register the plugin
	handler->registerOutputPlugin(rev01_uuid, MAX10OutputPlugin::create);
}

/**
 * Destructor function: attach an output plugin to the plugin handler.
 */
PLUGIN_PRIVATE void max10_deinit(PluginHandler *handler) {

}

// export the struct
PLUGIN_EXPORT lichtenstein_plugin_t plugin_info = {
	.magic = PLUGIN_MAGIC,
	.clientVersion = PLUGIN_CLIENT_VERSION,
	.type = kPluginTypeOutput,
	.version = 0x00010000,

	.name = "MAX-10 FPGA Output Board",
	.author = "Tristan Seifert",
	.license ="BSD 3 Clause",
	.url = "https://github.com/tristanseifert/lichtenstein_client/tree/master/modules/out-max10",
	.build = GIT_HASH "/" GIT_BRANCH,
	.compiledOn = COMPILE_TIME,

	.init = max10_init,
	.deinit = max10_deinit
};
