#include "LEDHandler.h"

#include <glog/logging.h>
#include <INIReader.h>
#include <cpptime.h>

#include <string>
#include <chrono>

#include <cstdio>

#include <sys/stat.h>

#define kHeartbeatRate	std::chrono::milliseconds(500)

/**
 * Initializes the LED handler: this loads the paths for the LED output files
 * from the config, and disables output functionality for that LED if they were
 * omitted from the config.
 */
LEDHandler::LEDHandler(INIReader *_config) : config(_config) {
	// read LED configuration
	this->configureLeds();

	// clear the error LED
	this->setErrorState(false);

	// if the heartbeat LED is set, configure the timer
	if(this->heartbeatLedConfigured) {
		// allocate timer
		this->heartbeatTimer = new CppTime::Timer();

		// now, allocate the event
		auto id = this->heartbeatTimer->add(kHeartbeatRate,
		[this](CppTime::timer_id) {
			static bool state = true;

			// toggle the heartbeat LED
			if(this->heartbeatLedConfigured) {
				// set LED state
				this->setLedState(this->heartbeatLedPath, state);

				// inverse it
				state = !state;
			}
		}, kHeartbeatRate);

	}
}

/**
 * Resets the state of all LEDs.
 */
LEDHandler::~LEDHandler() {
	// reset all LEDs except the error indicator
	this->setOutputState(false);
	this->setAdoptionState(false);

	// clean up heartbeat timer
	if(this->heartbeatLedConfigured && this->heartbeatTimer) {
		// delete the heartbeat timer
		delete this->heartbeatTimer;

		// extinguish the heartbeat LED
		this->setLedState(this->heartbeatLedPath, false);
	}
}



/**
 * Reads per-LED configuration from the config file.
 */
void LEDHandler::configureLeds(void) {
	// configure error LED
	this->errorLedPath = this->config->Get("statusled", "errorled", "none");

	if(this->errorLedPath != "none") {
		this->errorLedConfigured = this->checkIfFileExists(this->errorLedPath);
	}

	// configure output active LED
	this->outputActiveLedPath = this->config->Get("statusled", "outputled", "none");

	if(this->outputActiveLedPath != "none") {
		this->outputActiveLedConfigured = this->checkIfFileExists(this->outputActiveLedPath);
	}

	// configure error LED
	this->adoptedLedPath = this->config->Get("statusled", "adoptionled", "none");

	if(this->adoptedLedPath != "none") {
		this->adoptedLedConfigured = this->checkIfFileExists(this->adoptedLedPath);
	}

	// configure error LED
	this->heartbeatLedPath = this->config->Get("statusled", "heartbeatled", "none");

	if(this->heartbeatLedPath != "none") {
		this->heartbeatLedConfigured = this->checkIfFileExists(this->heartbeatLedPath);
	}
}



/**
 * Sets the state of the LED with the given path.
 */
int LEDHandler::setLedState(std::string &path, bool state) {
	int err = 0;

	// Attempt to open the file
	FILE *f = fopen(path.c_str(), "wb");

	if(f == nullptr) {
		PLOG(WARNING) << "Couldn't open " << path;
		return errno;
	}

	// write to the file
	char value = state ? '1' : '0';
	const size_t valueLen = sizeof(value);

	err = fwrite(&value, 1, valueLen, f);
	PLOG_IF(WARNING, err != valueLen) << "Couldn't write to " << path
		<< " (wrote " << err << " bytes)";

	// close the file again
	err = fclose(f);
	PLOG_IF(WARNING, err != 0) << "Couldn't close " << path;

	// done!
	return err;
}



/**
 * Checks whether the given file exists.
 */
bool LEDHandler::checkIfFileExists(std::string &path) {
	int err;
  struct stat buffer;

	// get info about the file
  err = stat(path.c_str(), &buffer);

	// file exists if status is 0
	return (err == 0);
}
