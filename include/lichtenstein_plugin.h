#ifndef LICHTENSTEIN_PLUGIN_H
#define LICHTENSTEIN_PLUGIN_H

#include "PluginHandler.h"

#include "OutputPlugin.h"
#include "InputPlugin.h"

#include <cstdint>

/**
 * Magic value for plugins.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmultichar"

#define PLUGIN_MAGIC				'AGES'

#pragma GCC diagnostic pop

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

	// (Required) Plugin name
	const char *name;
	// (Required) Author name
	const char *author;
	// (Required) License
	const char *license;
	// (Required) Website for further information about plugin
	const char *url;
	// Build number of the plugin
	const char *build;
	// date/time string when this plugin was compiled
	const char *compiledOn;

	// Function used to initialize the plugin
	void (*init)(PluginHandler *);
	// Function used to shut down the plugin
	void (*deinit)(PluginHandler *);
} lichtenstein_plugin_t;

/**
 * Prefix symbols that you would like to be exported outside of the plugin with
 * this macro.
*/
#if defined _WIN32 || defined __CYGWIN__
	#ifdef BUILDING_DLL
		#ifdef __GNUC__
			#define PLUGIN_EXPORT __attribute__ ((dllexport))
		#else
			#define PLUGIN_EXPORT __declspec(dllexport)
		#endif
	#else
		#ifdef __GNUC__
			#define PLUGIN_EXPORT __attribute__ ((dllimport))
		#else
			#define PLUGIN_EXPORT __declspec(dllimport)
		#endif
	#endif
	#define PLUGIN_PRIVATE
#else
	#define PLUGIN_EXPORT __attribute__ ((visibility ("default")))
	#define PLUGIN_PRIVATE  __attribute__ ((visibility ("hidden")))
#endif

#endif
