/**
 * Entry file for input/output plugins. This includes the appropriate headers
 * for types, and defines the structures/functions required.
 *
 * Use the template in the root of the repository to create a file customized
 * for your plugin.
 */
#include <lichtenstein_plugin.h>

/**
 * Initialization function: attach an output plugin to the plugin handler.
 */
PLUGIN_PRIVATE void max10_init(PluginHandler *handler) {

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

	.init = max10_init
};
