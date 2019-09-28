/**
 * Provides a centralized status reporting mechanism. The status information is
 * sent to whatever output sources have been set up.
 */
#ifndef STATUSHANDLER_H
#define STATUSHANDLER_H

class INIReader;

class LEDHandler;

class StatusHandler {
	friend int main(int, const char *[]);

	public:
		static StatusHandler *sharedInstance(void);

		void assertErrorState();

		void setOutputState(bool isActive);
		void setAdoptionState(bool isAdopted);

	private:
		StatusHandler(INIReader *config);
		~StatusHandler();

		static void initSingleton(INIReader *config);
		static void deallocSingleton(void);

	private:

	private:
		INIReader *config = nullptr;

		LEDHandler *led = nullptr;

};

#endif
