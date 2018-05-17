#ifndef PLUGINHANDLER_H
#define PLUGINHANDLER_H

#include <cstddef>

#include <INIReader.h>

#include <uuid/uuid.h>

class OutputPlugin;
class InputPlugin;

class PluginHandler;

// output plugin factory method: OutputPlugin *factory(void *romData, size_t romDataLength);
typedef OutputPlugin* (*output_plugin_factory_t)(PluginHandler *, void *, size_t);
// input plugin factory method: OutputPlugin *factory();
typedef InputPlugin* (*input_plugin_factory_t)(PluginHandler *);

/**
 * Plugin handler class exported to each plugin.
 */
class PluginHandler {
	// you shouldn't ever call this from a plugin
	public:
		PluginHandler();
		~PluginHandler();

	// functions plugins can call
	public:
		virtual INIReader *getConfig(void) = 0;

		virtual int registerOutputPlugin(const uuid_t &uuid, output_plugin_factory_t factory) = 0;
		virtual int registerInputPlugin(const uuid_t &uuid, input_plugin_factory_t factory) = 0;
};

#endif
