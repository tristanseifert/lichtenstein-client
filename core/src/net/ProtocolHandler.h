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
		enum {
			kWorkerNOP,
			kWorkerShutdown,
			kWorkerAnnounce
		};

	private:
		friend void ProtocolHandlerThreadEntry(void *);

		void workerEntry(void);

		void handlePacket(void *, size_t, struct msghdr *);
		void sendAnnouncement(void);

		void setUpSocket(void);
		void cleanUpSocket(void);

	private:
		INIReader *config = nullptr;

		int socket = -1;
		int announcementSocket = -1;
		int workerPipeRead = -1;
		int workerPipeWrite = -1;

		std::atomic_bool run = true;
		std::thread *worker = nullptr;
};

#endif
