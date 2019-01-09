/**
 * A small helper class for dealing with Linux GPIOs.
 */
#ifndef GPIOHELPER_H
#define GPIOHELPER_H

#include <string>

class GPIOHelper {
	public:
		virtual int exportGPIO(int pin);
		virtual int unExportGPIO(int pin);
		virtual int configureGPIO(int pin, std::string attribute, std::string value);

		virtual int readGPIO(int pin);
		virtual int writeGPIO(int pin, bool state);

	private:
		static const std::string gpioAttribute;

		static const std::string gpioExport;
		static const std::string gpioUnExport;
};

#endif
