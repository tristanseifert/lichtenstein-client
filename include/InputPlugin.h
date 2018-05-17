/**
 * Defines an abstract class that all input plugins should subclass.
 */
#ifndef INPUTPLUGIN_H
#define INPUTPLUGIN_H

#include <string>
#include <vector>

class InputPlugin {
	public:
		InputPlugin() {

		}
		virtual ~InputPlugin() {

		}

	// generic information API
	public:
		virtual const std::string name(void) = 0;

		virtual const bool supportsInterrupts(void) = 0;

		virtual const unsigned int numInputChannels() = 0;
		virtual int getInputState(std::vector<bool> &outState) = 0;

		virtual const unsigned int numTestChannels() = 0;
		virtual int getTestState(std::vector<bool> &outState) = 0;

	// shared variables
	protected:
};

#endif
