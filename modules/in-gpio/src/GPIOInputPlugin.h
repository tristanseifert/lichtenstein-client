#ifndef GPIOINPUTPLUGIN_H
#define GPIOINPUTPLUGIN_H

#include <lichtenstein_plugin.h>

#include <string>
#include <vector>
#include <atomic>
#include <thread>

class GPIOInputPlugin : public InputPlugin {
	public:
		GPIOInputPlugin(PluginHandler *handler);
		~GPIOInputPlugin();

		static InputPlugin *create(PluginHandler *handler);

	public:
		virtual const std::string name(void);

		virtual const bool supportsInterrupts(void);

		virtual const unsigned int numInputChannels() {
			return this->inputChannels;
		}
		virtual int getInputState(std::vector<bool> &outState);

		virtual const unsigned int numTestChannels() {
			return this->testChannels;
		}
		virtual int getTestState(std::vector<bool> &outState);

	private:
		// emulate sysfs on non-linux filesystems
#ifdef __linux__
		const std::string gpioAttribute = "/sys/class/gpio/gpio$PIN/$ATTRIBUTE";

		const std::string gpioExport = "/sys/class/gpio/export";
		const std::string gpioUnExport = "/sys/class/gpio/unexport";
#else
		const std::string gpioAttribute = "./sysfs/gpio_$PIN_$ATTRIBUTE";

		const std::string gpioExport = "./sysfs/export";
		const std::string gpioUnExport = "./sysfs/unexport";
#endif

	private:
		unsigned int inputChannels = 0;
		unsigned int testChannels = 0;

	private:
		int parseCsvList(std::string &, std::vector<std::string> &);

		void exportGPIOs(void);
		void initGPIOs(void);
		void cleanUpGPIOs(void);

		void getAllGPIOs(std::vector<int> &);

		int exportGPIO(int);
		int unExportGPIO(int);
		int configureGPIO(int);
		int configureGPIO(int, std::string, std::string);

		int readGPIO(int);

	private:
		friend void GPIOInputPluginWorkerEntry(void *ctx);

		void workerEntry(void);

		std::vector<int> inputPins;
		std::vector<int> testInputPins;

		std::vector<bool> inputState;
		std::vector<bool> testState;

	private:
		PluginHandler *handler = nullptr;

		std::atomic_bool run;
		std::thread *worker = nullptr;
};

#endif
