#include "GPIOInputPlugin.h"

#include <glog/logging.h>
#include <INIReader.h>

#include <time.h>

#include <atomic>
#include <thread>

#include <cstdio>

#include <sstream>

static bool replace(std::string& str, const std::string& from, const std::string& to) {
    size_t start_pos = str.find(from);

	if(start_pos == std::string::npos) {
        return false;
	}

    str.replace(start_pos, from.length(), to);
    return true;
}
/**
 * Entry point for the worker thread.
 */
void GPIOInputPluginWorkerEntry(void *ctx) {
	static_cast<GPIOInputPlugin *>(ctx)->workerEntry();
}



/**
 * Initializer: this will export and configure all GPIOs.
 */
GPIOInputPlugin::GPIOInputPlugin(PluginHandler *_handler) : handler(_handler) {
	this->exportGPIOs();
	this->initGPIOs();

	// set up worker thread
	this->run = true;
	this->worker = new std::thread(GPIOInputPluginWorkerEntry, this);
}

/**
 * Destructor: un-exports all GPIOs.
 */
GPIOInputPlugin::~GPIOInputPlugin() {
	// terminate the worker thread
	this->run = false;
	this->worker->join();

	delete this->worker;

	// clean up GPIOs
	this->cleanUpGPIOs();
}

/**
 * Invokes the constructor for the plugin and returns it.
 */
InputPlugin *GPIOInputPlugin::create(PluginHandler *handler) {
	return new GPIOInputPlugin(handler);
}



/**
 * Plugin name
 */
const std::string GPIOInputPlugin::name(void) {
	return "sysfs-based GPIO inputs";
}

/**
 * This plugin polls in a background thread.
 */
const bool GPIOInputPlugin::supportsInterrupts(void) {
	return false;
}



/**
 * Gets the state of all inputs; they're pushed onto the output vector with the
 * input whose GPIO number was specified first.
 */
int GPIOInputPlugin::getInputState(std::vector<bool> &outState) {
	return -1;
}



/**
 * Gets the state of all test inputs; they're pushed onto the output vector
 * with the input whose GPIO number was specified first.
 */
int GPIOInputPlugin::getTestState(std::vector<bool> &outState) {
	return -1;
}



/**
 * Entry point for the worker thread.
 */
void GPIOInputPlugin::workerEntry(void) {
	int err;
	INIReader *config = this->handler->getConfig();

	// get the interval to wait
	int msecsToWait = config->GetInteger("input_gpio", "interval", 100);

	while(this->run) {
		// read states
		std::vector<bool> testInputs;
		std::vector<bool> standardInputs;

		// read all test inputs
		for(int pin : this->testInputPins) {
			err = this->readGPIO(pin);

			if(err >= 0) {
				testInputs.push_back((err == 1) ? true : false);
			} else {
				LOG(WARNING) << "Couldn't read pin " << pin << ": " << err;
			}
		}

		// read all standard inputs
		for(int pin : this->inputPins) {
			err = this->readGPIO(pin);

			if(err >= 0) {
				standardInputs.push_back((err == 1) ? true : false);
			} else {
				LOG(WARNING) << "Couldn't read pin " << pin << ": " << err;
			}
		}

		// XXX: debug
		std::stringstream testInputsStr;
		std::stringstream normalInputsStr;

		for(bool state : testInputs) {
			testInputsStr << ((state) ? "1 " : "0 ");
		}

		VLOG(1) << "Test inputs: " << testInputsStr.str();

		for(bool state : standardInputs) {
			normalInputsStr << ((state) ? "1 " : "0 ");
		}

		VLOG(1) << "Normal inputs: " << normalInputsStr.str();

		// sleep for the specified interval
		struct timespec sleepTime;
		memset(&sleepTime, 0, sizeof(sleepTime));

		sleepTime.tv_sec = msecsToWait / 1000;
		sleepTime.tv_nsec = (msecsToWait % 1000) * 1000000;

		err = nanosleep(&sleepTime, nullptr);
	}

	// clean up
	LOG(INFO) << "Terminating worker thread";
}


/**
 * Gets the GPIO numbers of all configured GPIOs.
 */
void GPIOInputPlugin::getAllGPIOs(std::vector<int> &out) {
	INIReader *config = this->handler->getConfig();

	// first, parse the regular inputs, if they exist
	std::string inputList = config->Get("input_gpio", "gpios", "");

	if(inputList != "") {
		std::vector<std::string> strings;

		// parse the list
		this->inputChannels = this->parseCsvList(inputList, strings);

		// clear input pins vector
		this->inputPins.clear();

		// insert the GPIO number
		for(std::string str : strings) {
			out.push_back(std::stoi(str));
			this->inputPins.push_back(std::stoi(str));
		}
	} else {
		LOG(WARNING) << "No inputs were configured, check input_gpio.gpios";
	}

	// first, parse the test inputs, if they exist
	std::string testInputList = config->Get("input_gpio", "test_gpios", "");

	if(testInputList != "") {
		std::vector<std::string> strings;

		// parse the list
		this->testChannels = this->parseCsvList(testInputList, strings);

		// clear input pins vector
		this->testInputPins.clear();

		// insert the GPIO number
		for(std::string str : strings) {
			out.push_back(std::stoi(str));
			this->testInputPins.push_back(std::stoi(str));
		}
	} else {
		LOG(WARNING) << "No test inputs were configured, check input_gpio.test_gpios";
	}
}

/**
 * Attempts to export all GPIOs specified.
 */
void GPIOInputPlugin::exportGPIOs(void) {
	int err;

	// get all GPIO pins
	std::vector<int> gpios;
	this->getAllGPIOs(gpios);

	// log info
	LOG(INFO) << "in_gpio: " << this->inputChannels << " input channels, "
		<< this->testChannels << " test channels";

	// for each GPIO, export it
	for(int pin : gpios) {
		err = this->exportGPIO(pin);
		LOG_IF(FATAL, err != 0) << "Couldn't export pin " << pin << ": " << err;
	}
}

/**
 * Initializes all GPIOs by configuring them as inputs.
 */
void GPIOInputPlugin::initGPIOs(void) {
	int err;

	// get all GPIO pins
	std::vector<int> gpios;
	this->getAllGPIOs(gpios);

	// for each GPIO, initialize it
	for(int pin : gpios) {
		err = this->configureGPIO(pin);
		LOG_IF(FATAL, err != 0) << "Couldn't configure pin " << pin << ": " << err;
	}
}

/**
 * Un-exports all GPIOs.
 */
void GPIOInputPlugin::cleanUpGPIOs(void) {
	int err;

	// get all GPIO pins
	std::vector<int> gpios;
	this->getAllGPIOs(gpios);

	// for each GPIO, unexport it
	for(int pin : gpios) {
		err = this->unExportGPIO(pin);
		LOG_IF(FATAL, err != 0) << "Couldn't unexport pin " << pin << ": " << err;
	}
}



/**
 * Exports the GPIO on the specified pin.
 *
 * @returns 0 if successful, error code otherwise.
 */
int GPIOInputPlugin::exportGPIO(int pin) {
	int err;

	// get the location of the file
	std::string path = this->gpioExport;
	replace(path, "$PIN", std::to_string(pin));

	// open the file for writing
	FILE *f = fopen(path.c_str(), "wb");

	if(f == nullptr) {
		PLOG(WARNING) << "Couldn't open " << path;
		return errno;
	}

	// write the string
	std::string pinStr = std::to_string(pin);

	const char *str = pinStr.c_str();
	size_t len = strlen(str);

	err = fwrite(str, 1, len, f);
	PLOG_IF(WARNING, err != len) << "Couldn't write to " << path
		<< " (wrote " << err << " bytes)";

	// close the file again
	err = fclose(f);
	PLOG_IF(WARNING, err != 0) << "Couldn't close " << path;

	return err;
}

/**
 * Un-exports the GPIO on the specified pin.
 *
 * @returns 0 if successful, error code otherwise.
 */
int GPIOInputPlugin::unExportGPIO(int pin) {
	int err;

	// get the location of the file
	std::string path = this->gpioUnExport;
	replace(path, "$PIN", std::to_string(pin));

	// open the file for writing
	FILE *f = fopen(path.c_str(), "wb");

	if(f == nullptr) {
		PLOG(WARNING) << "Couldn't open " << path;
		return errno;
	}

	// write the string
	std::string pinStr = std::to_string(pin);

	const char *str = pinStr.c_str();
	size_t len = strlen(str);

	err = fwrite(str, 1, len, f);
	PLOG_IF(WARNING, err != len) << "Couldn't write to " << path
		<< " (wrote " << err << " bytes)";

	// close the file again
	err = fclose(f);
	PLOG_IF(WARNING, err != 0) << "Couldn't close " << path;

	return err;
}

/**
 * Configures the pin as an input, with a weak pull-up if possible.
 *
 * @returns 0 if successful, error code otherwise.
 */
int GPIOInputPlugin::configureGPIO(int pin) {
	int err;

	// configure it as an input
	err = this->configureGPIO(pin, "direction", "in");

	if(err != 0) {
		LOG(WARNING) << "Couldn't configure pin " << pin << " as input: " << err;
		return err;
	}

	// success
	return 0;
}

/**
 * Writes the given value to the given GPIO configuration file.
 *
 * @return 0 if successful, an error code otherwise.
 */
int GPIOInputPlugin::configureGPIO(int pin, std::string attribute, std::string value) {
	int err;

	// get the location of the file
	std::string path = this->gpioAttribute;
	replace(path, "$PIN", std::to_string(pin));
	replace(path, "$ATTRIBUTE", attribute);

	// open the file for writing
	FILE *f = fopen(path.c_str(), "wb");

	if(f == nullptr) {
		PLOG(WARNING) << "Couldn't open " << path;
		return errno;
	}

	// write the string
	const char *str = value.c_str();
	size_t len = strlen(str);

	err = fwrite(str, 1, len, f);
	PLOG_IF(WARNING, err != len) << "Couldn't write to " << path
		<< " (wrote " << err << " bytes)";

	// close the file again
	err = fclose(f);
	PLOG_IF(WARNING, err != 0) << "Couldn't close " << path;

	return err;
}

/**
 * Reads the specified GPIO pin.
 *
 * @return positive pin state if successful, negative error code otherwise.
 */
int GPIOInputPlugin::readGPIO(int pin) {
	int err;
	bool hadError = false;

	// get the location of the file
	std::string path = this->gpioAttribute;
	replace(path, "$PIN", std::to_string(pin));
	replace(path, "$ATTRIBUTE", "value");

	// open the file for writing
	FILE *f = fopen(path.c_str(), "rb");

	if(f == nullptr) {
		PLOG(WARNING) << "Couldn't open " << path;
		return -errno;
	}

	// read a single byte
	char buffer;

	err = fread(&buffer, 1, sizeof(char), f);

	if(err != 1) {
		PLOG(WARNING) << "Couldn't read from " << path
			<< " (read " << err << " bytes)";
		hadError = true;
	}


	// close the file again
	err = fclose(f);

	if(err != 0) {
		PLOG(WARNING) << "Couldn't close " << path;
		hadError = true;
	}

	// interpret result
	if(!hadError) {
		// is the input low?
		if(buffer == '0') {
			return 0;
		}
		// is the input high?
		else if(buffer == '1') {
			return 1;
		}
		// unknown value
		else {
			LOG(WARNING) << "Read unknown value '" << buffer << "' from " << path;
		}
	}

	// couldn't read
	return -1;
}


/**
 * Parses a comma-separated list of objects, inserting each of them into the
 * output vector.
 */
int GPIOInputPlugin::parseCsvList(std::string &in, std::vector<std::string> &out) {
	int numItems = 0;

	// create a stream of input data
	std::stringstream ss(in);

	// iterate while there is more data in the stream
	while(ss.good()) {
		// read a line separated by a comma
		std::string substr;
		getline(ss, substr, ',');

		// then push it on the output vector
		out.push_back(substr);
		numItems++;
	}

	return numItems;
}
