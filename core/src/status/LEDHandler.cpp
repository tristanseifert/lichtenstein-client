#include "LEDHandler.h"

#include <glog/logging.h>
#include <INIReader.h>
#include <cpptime.h>

#include <string>
#include <chrono>

#include <cstdio>

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
}



/**
 * Reads per-LED configuration from the config file.
 */
void LEDHandler::configureLeds(void) {
	// configure error LED
	this->errorLedPath = this->config->Get("statusled", "errorled", "none");

	if(this->errorLedPath != "none") {
		this->errorLedConfigured = true;
	}

	// configure output active LED
	this->outputActiveLedPath = this->config->Get("statusled", "outputled", "none");

	if(this->outputActiveLedPath != "none") {
		this->outputActiveLedConfigured = true;
	}

	// configure error LED
	this->adoptedLedPath = this->config->Get("statusled", "adoptionled", "none");

	if(this->adoptedLedPath != "none") {
		this->adoptedLedConfigured = true;
	}

	// configure error LED
	this->heartbeatLedPath = this->config->Get("statusled", "heartbeatled", "none");

	if(this->heartbeatLedPath != "none") {
		this->heartbeatLedConfigured = true;
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
