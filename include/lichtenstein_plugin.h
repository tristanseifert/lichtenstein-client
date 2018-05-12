#ifndef LICHTENSTEIN_PLUGIN_H
#define LICHTENSTEIN_PLUGIN_H

#include "PluginHandler.h"

#include <cstdint>

/**
 * Magic value for plugins.
 */
#define PLUGIN_MAGIC				'AGES'

/**
 * Current client ABI version. Plugins whose ABI number doesn't match this value
 * will not be loaded. This should _only_ be changed in case the binary API to
 * the client is broken.
 */
#define PLUGIN_CLIENT_VERSION		0x00001000

/**
 * Plugin type
 */
typedef enum {
	kPluginTypeUnknown				= 0,
	kPluginTypeOutput				= 1,
	kPluginTypeInput				= 2,
} lichtenstein_plugin_type_t;

/**
 * Structure defining information about a plugin. Each plugin must export this
 * structure (any additional exports are optional) so that it can be loaded
 * properly.
 */
typedef struct {
	// Magic value
	uint32_t magic;
	// Client ABI version
	uint32_t clientVersion;

	// Type of plugin
	lichtenstein_plugin_type_t type;

	// Plugin version
	uint32_t version;

	// Plugin name
	const char *name;
	// Author name
	const char *author;
	// License
	const char *licence;
	// Website for further information about plugin
	const char *url;

	// Function used to instantiate the plugin
	void* (*create)(PluginHandler *);
} lichtenstein_plugin_t;

#endif
