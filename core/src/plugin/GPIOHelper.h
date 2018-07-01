/**
 * A small helper class for dealing with Linux GPIOs.
 */
#ifndef GPIOHELPER_H
#define GPIOHELPER_H

#include <string>

class GPIOHelper {
	public:
		int exportGPIO(int pin);
		int unExportGPIO(int pin);
		int configureGPIO(int pin, std::string attribute, std::string value);

		int readGPIO(int pin);
		int writeGPIO(int pin, bool state);

	private:
		static const std::string gpioAttribute;

		static const std::string gpioExport;
		static const std::string gpioUnExport;
};

#endif
