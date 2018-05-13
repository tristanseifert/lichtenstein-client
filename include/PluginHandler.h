#ifndef PLUGINHANDLER_H
#define PLUGINHANDLER_H

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
		virtual int registerOutputPlugin(void *uuid, void *pluginClass) = 0;
		virtual int registerInputPlugin(void *uuid, void *pluginClass) = 0;
};

#endif
