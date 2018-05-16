#ifndef PROTOCOLHANDLER_H
#define PROTOCOLHANDLER_H

#include <atomic>
#include <thread>

#include <cstddef>
#include <cstdint>

#include <INIReader.h>
#include <cpptime.h>

struct lichtenstein_header;
typedef struct lichtenstein_header lichtenstein_header_t;

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
		void getMacAddress(uint8_t *);
		void getIpAddress(char *, size_t);

		void setUpSocket(void);
		void cleanUpSocket(void);

		void sendStatusResponse(lichtenstein_header_t *, struct in_addr *);
		int sendPacketToHost(void *, size_t, struct in_addr *);

	private:
		static unsigned int getUptime(void);

	private:
		INIReader *config = nullptr;

		int socket = -1;
		int announcementSocket = -1;
		int workerPipeRead = -1;
		int workerPipeWrite = -1;

		std::atomic_bool run = true;
		std::thread *worker = nullptr;

		CppTime::Timer timer;

	private:
		size_t packetsWithInvalidCRC = 0;
};

#endif
