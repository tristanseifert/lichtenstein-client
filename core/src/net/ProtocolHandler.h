#ifndef PROTOCOLHANDLER_H
#define PROTOCOLHANDLER_H

#include <atomic>
#include <thread>

#include <cstddef>
#include <cstdint>

#include <functional>
#include <bitset>

// for struct in_addr
#include <netinet/in.h>
// for struct msghdr
#include <sys/socket.h>

#include <INIReader.h>
#include <cpptime.h>

#ifndef LICHTENSTEINPROTO_H
	// Forward declare header type
	struct lichtenstein_header;
	typedef struct lichtenstein_header lichtenstein_header_t;
#endif

class OutputFrame;

class ProtocolHandler {
	// OutputFrame can generate ack packets
	friend class OutputFrame;
	// plugin handler may acknowledge packets
	friend class LichtensteinPluginHandler;

	// allow main to set the callback
	friend int main(int, const char *[]);

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
		void handleAdoption(lichtenstein_header_t *, struct in_addr *);

		int sendPacketToHost(void *, size_t, struct in_addr *);

		lichtenstein_header_t *createUnicastAck(lichtenstein_header_t *, bool = false);
		void ackUnicast(lichtenstein_header_t *, struct in_addr *, bool = false);

		void ackOutputFrame(OutputFrame *, bool = false);

	private:
		static unsigned int getUptime(void);

	private:
		// config reader
		INIReader *config = nullptr;

		// sockets used for receiving and announcements
		int socket = -1;
		int announcementSocket = -1;

		// pipe for communicating with worker
		int workerPipeRead = -1;
		int workerPipeWrite = -1;

		// worker thread
		std::atomic_bool run;
		std::thread *worker = nullptr;

		// timer used for announcements/adoption
		CppTime::Timer timer;

		// callback to notify plugins of received frames
		std::function<int(OutputFrame *)> frameReceiveCallback;
		// callback to notify plugins of output requests
		std::function<int(std::bitset<32> &)> channelOutputCallback;

	private:
		bool isAdopted = false;

		time_t lastServerMessageOn = 0;

	// counters
	private:
		size_t packetsWithInvalidCRC = 0;

		size_t framebufferPacketsDiscarded = 0;
		size_t outputPacketsDiscarded = 0;
};

#endif
