#include "LichtensteinPluginHandler.h"

#include <glog/logging.h>

#include <dirent.h>
#include <sys/stat.h>
#include <dlfcn.h>

#include <uuid/uuid.h>

/**
 * Sets up the plugin handler and loads the plugins.
 */
LichtensteinPluginHandler::LichtensteinPluginHandler(INIReader *_cfg) : config(_cfg) {
	// load plugins
	this->loadInputPlugins();
	this->loadOutputPlugins();

	// init plugins
	this->callPluginConstructors();
}

/**
 * Unloads all plugins cleanly.
 */
LichtensteinPluginHandler::~LichtensteinPluginHandler() {
	// call plugin destructors
	this->callPluginDestructors();
}



/**
 * Add an output plugin with the given UUID to the registry.
 */

int LichtensteinPluginHandler::registerOutputPlugin(const uuid_t &uuid, output_plugin_factory_t factory) {
	// get UUID as string
	char uuidStr[36];
	uuid_unparse_upper(uuid, (char *) &uuidStr);

	LOG(INFO) << "Registering plugin factory method for UUID " << uuidStr;

	this->outFactories[std::string(uuidStr)] = factory;
	return 0;
}

/**
 * Adds an input plugin with the given UUID to the registry.
 */
int LichtensteinPluginHandler::registerInputPlugin(const uuid_t &uuid, input_plugin_factory_t factory) {
	// get UUID as string
	char uuidStr[36];
	uuid_unparse_upper(uuid, (char *) &uuidStr);

	LOG(INFO) << "Registering plugin factory method for UUID " << uuidStr;

	this->inFactories[std::string(uuidStr)] = factory;
	return 0;
}



/**
 * Loads input plugins.
 */
void LichtensteinPluginHandler::loadInputPlugins(void) {
	// read from the INI
	std::string path = this->config->Get("input", "module_dir", "unknown");
	CHECK(path != "unknown") << "input module_dir was not set in config, aborting";

	this->loadPluginsInDirectory(path);
}

/**
 * Loads output plugins.
 */
void LichtensteinPluginHandler::loadOutputPlugins(void) {
	// read from the INI
	std::string path = this->config->Get("output", "module_dir", "unknown");
	CHECK(path != "unknown") << "output module_dir was not set in config, aborting";

	this->loadPluginsInDirectory(path);
}

/**
 * Loads all plugins from the given directory.
 */
void LichtensteinPluginHandler::loadPluginsInDirectory(std::string &directory) {
	LOG(INFO) << "Loading plugins from " << directory;

	// set up for listing the directory
	DIR *dir = nullptr;
	struct dirent *it = nullptr;
	struct stat statbuf;
	int err = 0;

	// attempt to open the directory
	dir = opendir(directory.c_str());

	if(dir != nullptr) {
		// iterate over all entries
		while((it = readdir(dir)) != nullptr) {
			// make sure it's not a hidden file
			if(it->d_name[0] != '.') {
				std::string fullPath = directory +  '/' + std::string(it->d_name);

				// get info about it
				err = lstat(fullPath.c_str(), &statbuf);

				if(err != 0) {
					PLOG(ERROR) << "Couldn't stat " << fullPath << ": ";
				} else {
					if(!S_ISDIR(statbuf.st_mode)) {
						// load the plugin
						err = this->loadPlugin(fullPath);

						LOG_IF(WARNING, err != 0) << "Couldn't load plugin "
						 	<< fullPath << ": " << err;
					}
				}
			}
		}

		// close the directory
		closedir(dir);
	} else {
		PLOG(ERROR) << "Couldn't list plugin files in directory " << directory;
	}
}

/**
 * Loads the plugin from the specified path.
 */
int LichtensteinPluginHandler::loadPlugin(std::string &path) {
	void *lib = nullptr;
	const char *cPath = path.c_str();

	int err = 0;

	// attempt to dlopen it
	lib = dlopen(cPath, RTLD_LAZY);
	CHECK(lib != nullptr) << "Couldn't open library!";

	// validate compatibility
	err = this->isPluginCompatible(lib);
	if(err) goto done;

	// plugin is good; keep the handle for later
	this->pluginHandles.push_back(std::make_tuple(path, lib));

	// close the library file again if there were any errors
done: ;
	if(err != 0) {
		dlclose(lib);
	}

	return err;
}

/**
 * Verifies that the plugin is compatible with this client version. Returns 0 if
 * it is valid, an error code otherwise.
 */
int LichtensteinPluginHandler::isPluginCompatible(void *handle) {
	// try to locate the info struct
	void *infoAddr = dlsym(handle, "plugin_info");

	if(infoAddr == nullptr) {
		return PLUGIN_MISSING_INFO;
	}

	// cast it and validate the magic and ABI version
	lichtenstein_plugin_t *info = static_cast<lichtenstein_plugin_t *>(infoAddr);

	if(info->magic != PLUGIN_MAGIC) {
		return PLUGIN_INVALID_MAGIC;
	} else if(info->clientVersion != PLUGIN_CLIENT_VERSION) {
		return PLUGIN_ABI_MISMATCH;
	}

	// otherwise, the plugin is probably good.
	return 0;
}



/**
 * Calls the initializer functions for all loaded plugins.
 */
void LichtensteinPluginHandler::callPluginConstructors(void) {
	void *handle;
	std::string path;

	// iterate over all plugins we loaded before
	for(auto tuple : this->pluginHandles) {
		std::tie(path, handle) = tuple;

		// locate the info symbol
		void *infoAddr = dlsym(handle, "plugin_info");

		if(infoAddr == nullptr) {
			// we shouldn't ever get here
			LOG(FATAL) << "Plugin with handle 0x" << std::hex << handle
				<< " suddenly lost its info struct...";

			continue;
		}

		// get the info and call the function
		lichtenstein_plugin_t *info = static_cast<lichtenstein_plugin_t *>(infoAddr);

		LOG(INFO) << "Initializing plugin \"" << info->name << "\" from " << path;
		info->init(this);
	}
}
/**
 * Calls the de-initializer functions for all loaded plugins.
 */
void LichtensteinPluginHandler::callPluginDestructors(void) {
	void *handle;
	std::string path;
	int err = 0;

	// iterate over all plugins we loaded before
	for(auto tuple : this->pluginHandles) {
		std::tie(path, handle) = tuple;

		// locate the info symbol
		void *infoAddr = dlsym(handle, "plugin_info");

		if(infoAddr == nullptr) {
			// we shouldn't ever get here
			LOG(FATAL) << "Plugin with handle 0x" << std::hex << handle
				<< " suddenly lost its info struct...";

			continue;
		}

		// get the info and call the function
		lichtenstein_plugin_t *info = static_cast<lichtenstein_plugin_t *>(infoAddr);

		LOG(INFO) << "De-initializing plugin \"" << info->name << "\" from " << path;
		info->deinit(this);

		// unload the object
		err = dlclose(handle);

		if(err != 0) {
			LOG(WARNING) << "Couldn't unload " << path << ": " << dlerror();
		}
	}
}
