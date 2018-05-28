#include "GPIOHelper.h"

#include <glog/logging.h>

#include <string>
#include <cstdio>



// define locations of the GPIO files
#ifdef __linux__
		const std::string GPIOHelper::gpioAttribute = "/sys/class/gpio/gpio$PIN/$ATTRIBUTE";

		const std::string GPIOHelper::gpioExport = "/sys/class/gpio/export";
		const std::string GPIOHelper::gpioUnExport = "/sys/class/gpio/unexport";
#else
		const std::string GPIOHelper::gpioAttribute = "./sysfs/gpio_$PIN_$ATTRIBUTE";

		const std::string GPIOHelper::gpioExport = "./sysfs/export";
		const std::string GPIOHelper::gpioUnExport = "./sysfs/unexport";
#endif



/**
 * String replacement helper
 */
static bool replace(std::string& str, const std::string& from, const std::string& to) {
    size_t start_pos = str.find(from);

	if(start_pos == std::string::npos) {
        return false;
	}

    str.replace(start_pos, from.length(), to);
    return true;
}



/**
 * Exports the GPIO on the specified pin.
 *
 * @returns 0 if successful, error code otherwise.
 */
int GPIOHelper::exportGPIO(int pin) {
	int err;

	// get the location of the file
	std::string path = GPIOHelper::gpioExport;
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
int GPIOHelper::unExportGPIO(int pin) {
	int err;

	// get the location of the file
	std::string path = GPIOHelper::gpioUnExport;
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
 * Writes the given value to the given GPIO configuration file.
 *
 * @return 0 if successful, an error code otherwise.
 */
int GPIOHelper::configureGPIO(int pin, std::string attribute, std::string value) {
	int err;

	// get the location of the file
	std::string path = GPIOHelper::gpioAttribute;
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
 * Writes the value of an output GPIO.
 */
int GPIOHelper::writeGPIO(int pin, bool value) {
	if(value) {
		return GPIOHelper::configureGPIO(pin, "value", "1");
	} else {
		return GPIOHelper::configureGPIO(pin, "value", "0");
	}
}

/**
 * Reads the specified GPIO pin.
 *
 * @return positive pin state if successful, negative error code otherwise.
 */
int GPIOHelper::readGPIO(int pin) {
	int err;
	bool hadError = false;

	// get the location of the file
	std::string path = GPIOHelper::gpioAttribute;
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
