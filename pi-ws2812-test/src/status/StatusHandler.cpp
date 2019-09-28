#include "StatusHandler.h"

#include "LEDHandler.h"

#include <glog/logging.h>
#include <INIReader.h>

// static instance (singleton)
static StatusHandler *sharedHandler = nullptr;



/**
 * Initializes the LED handler: this loads the paths for the LED output files
 * from the config, and disables output functionality for that LED if they were
 * omitted from the config.
 */
StatusHandler::StatusHandler(INIReader *_config) : config(_config) {
	// allocate LED handler
	this->led = new LEDHandler(this->config);
	CHECK(this->led != nullptr) << "Couldn't allocate LED handler";
}

/**
 * Resets the state of all output mechanisms.
 */
StatusHandler::~StatusHandler() {
	// delete LEDs
	if(this->led) {
		delete this->led;
	}
}



/**
 * Initializes the status handler, passing the given config reader to it. This
 * should be called early on from the main routine.
 */
void StatusHandler::initSingleton(INIReader *config) {
	sharedHandler = new StatusHandler(config);
}

/**
 * Deallocates the status handler.
 */
void StatusHandler::deallocSingleton(void) {
	CHECK(sharedHandler != nullptr) << "Call to deallocSingleton but singleton "
		<< "is null? what the fuck";

	delete sharedHandler;

	// set it to null so later invocations fail (with a crash but fuck it)
	sharedHandler = nullptr;
}

/**
 * Returns the previously allocated status handler.
 */
StatusHandler *StatusHandler::sharedInstance(void) {
	return sharedHandler;
}



/**
 * Asserts the error indicator.
 */
void StatusHandler::assertErrorState() {
	if(this->led) {
		this->led->setErrorState(true);
	}
}

/**
 * Sets the state of the output indicator.
 */
void StatusHandler::setOutputState(bool isActive) {
	if(this->led) {
		this->led->setOutputState(isActive);
	}
}

/**
 * Sets the state of the adoption indicator.
 */
void StatusHandler::setAdoptionState(bool isAdopted) {
	if(this->led) {
		this->led->setAdoptionState(isAdopted);
	}
}
