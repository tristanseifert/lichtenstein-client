/**
 * A small helper class for dealing with Linux GPIOs.
 */
#ifndef GPIOHELPER_H
#define GPIOHELPER_H

#include <string>

class GPIOHelper {
	public:
		GPIOHelper() = delete;


	private:
		static const std::string gpioAttribute;

		static const std::string gpioExport;
		static const std::string gpioUnExport;

	public:
		static int exportGPIO(int);
		static int unExportGPIO(int);
		static int configureGPIO(int, std::string, std::string);

		static int readGPIO(int);
		static int writeGPIO(int, bool);
};

#endif
