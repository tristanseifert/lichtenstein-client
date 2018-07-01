/**
 * Implements the central plugin loader and registry.
 */
#ifndef LICHTENSTEIN_PLUGINHANDLER_H
#define LICHTENSTEIN_PLUGINHANDLER_H

#include <lichtenstein_plugin.h>

#include <string>
#include <vector>
#include <tuple>
#include <map>
#include <queue>
#include <bitset>

#include <uuid/uuid.h>

#include <INIReader.h>

class ProtocolHandler;
class OutputFrame;

class GPIOHelper;

class LichtensteinPluginHandler : public PluginHandler {
	friend class InputHandler;
	friend class OutputHandler;

	friend int main(int, const char *[]);

	public:
		LichtensteinPluginHandler(INIReader *config);
		~LichtensteinPluginHandler();

	// plugin API
	public:
		virtual INIReader *getConfig(void) {
			return this->config;
		}

		virtual GPIOHelper *getGPIOHelper(void) {
			return this->gpioHelper;
		}

		virtual int registerOutputPlugin(const uuid_t &uuid, output_plugin_factory_t factory);
		virtual int registerInputPlugin(const uuid_t &uuid, input_plugin_factory_t factory);

		virtual void acknowledgeFrame(OutputFrame *frame, bool nack = false);

	// API used by the rest of the server
	protected:
		output_plugin_factory_t getOutputFactoryByUUID(std::string uuid) const {
			return this->outFactories.at(uuid);
		}
		OutputPlugin *initOutputPluginByUUID(std::string uuid, void *rom, size_t romLen) {
			output_plugin_factory_t factory = this->getOutputFactoryByUUID(uuid);

			return factory(this, rom, romLen);
		}

		input_plugin_factory_t getInputFactoryByUUID(std::string uuid) const {
			return this->inFactories.at(uuid);
		}
		InputPlugin *initInputPluginByUUID(std::string uuid) {
			input_plugin_factory_t factory = this->getInputFactoryByUUID(uuid);

			return factory(this);
		}

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

		// factory methods for input/output plugins
		std::map<std::string, output_plugin_factory_t> outFactories;
		std::map<std::string, input_plugin_factory_t> inFactories;

		ProtocolHandler *protocolHandler = nullptr;

		INIReader *config = nullptr;
		GPIOHelper *gpioHelper = nullptr;
};

#endif
