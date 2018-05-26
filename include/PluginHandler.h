#ifndef PLUGINHANDLER_H
#define PLUGINHANDLER_H

#include <cstddef>
#include <uuid/uuid.h>

#include <INIReader.h>

#include "OutputFrame.h"

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

		/**
		 * Are frames for outputting available?
		 */
		virtual bool areFramesAvailable(void) = 0;
		/**
		 * Attempt to dequeue a frame; regardless of whether the frame is even
		 * output.
		 */
		virtual int dequeueFrame(OutputFrame **out) = 0;
		/**
		 * Acknowledges a frame as having been processed. This will notify the
		 * server that the frame is ready.
		 */
		virtual void acknowledgeFrame(OutputFrame *frame) = 0;
};

#endif
