/**
 * Shared LED handler: this contains the needed logic for dealing with status
 * reporting via LEDs.
 *
 * This class shouldn't be accessed directly: instead, use the StatusHandler
 * singleton.
 */
#ifndef LEDHANDLER_H
#define LEDHANDLER_H

#include <string>

class INIReader;

namespace CppTime {
	class Timer;
};

class LEDHandler {
	public:
		LEDHandler(INIReader *config);
		~LEDHandler();

		void setErrorState(bool state) {
			if(this->errorLedConfigured) {
				this->setLedState(this->errorLedPath, state);
			}
		}
		void setOutputState(bool state) {
			if(this->outputActiveLedConfigured) {
				this->setLedState(this->outputActiveLedPath, state);
			}
		}
		void setAdoptionState(bool state) {
			if(this->adoptedLedConfigured) {
				this->setLedState(this->adoptedLedPath, state);
			}
		}

	private:
		void configureLeds(void);

		int setLedState(std::string &, bool);

	private:
		INIReader *config = nullptr;

		CppTime::Timer *heartbeatTimer;

		bool errorLedConfigured = false;
		std::string errorLedPath;

		bool outputActiveLedConfigured = false;
		std::string outputActiveLedPath;

		bool adoptedLedConfigured = false;
		std::string adoptedLedPath;

		bool heartbeatLedConfigured = false;
		std::string heartbeatLedPath;
};

#endif
