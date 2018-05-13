#ifndef PROTOCOLHANDLER_H
#define PROTOCOLHANDLER_H

#include <atomic>
#include <thread>

#include <INIReader.h>

class ProtocolHandler {
	public:
		ProtocolHandler(INIReader *config);
		~ProtocolHandler();

		void start(void);
		void stop(void);

	private:
		friend void ProtocolHandlerThreadEntry(void *);

		void workerEntry(void);

		void setUpSocket(void);
		void cleanUpSocket(void);

	private:
		INIReader *config = nullptr;

		int socket = -1;
		int workerPipeRead = -1;
		int workerPipeWrite = -1;

		std::atomic_bool run = true;
		std::thread *worker = nullptr;
};

#endif
