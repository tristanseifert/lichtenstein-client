/**
 * Implements the central plugin loader and registry.
 */
#ifndef LICHTENSTEIN_PLUGINHANDLER_H
#define LICHTENSTEIN_PLUGINHANDLER_H

#include <lichtenstein_plugin.h>

#include <string>
#include <vector>
#include <tuple>

#include <uuid/uuid.h>

#include <INIReader.h>

class LichtensteinPluginHandler : public PluginHandler {
	public:
		LichtensteinPluginHandler(INIReader *config);
		~LichtensteinPluginHandler();

	// plugin API
	public:
		virtual int registerOutputPlugin(const uuid_t &uuid, output_plugin_factory_t factory);
		virtual int registerInputPlugin(const uuid_t &uuid, input_plugin_factory_t factory);

	private:
		enum {
			PLUGIN_LOADED			= 0,
			PLUGIN_MISSING_INFO		= 1,
			PLUGIN_INVALID_MAGIC,
			PLUGIN_ABI_MISMATCH
		};

	private:
		void loadInputPlugins(void);
		void loadOutputPlugins(void);

		void loadPluginsInDirectory(std::string &directory);

		int loadPlugin(std::string &path);
		int isPluginCompatible(void *handle);

		void callPluginConstructors(void);
		void callPluginDestructors(void);

	private:
		// handles returned by dlopen for these plugins
		std::vector<std::tuple<std::string, void *>> pluginHandles;

		INIReader *config = nullptr;
};

#endif
