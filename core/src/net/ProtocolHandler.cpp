#include "ProtocolHandler.h"

#include "LichtensteinUtils.h"
#include "lichtenstein_proto.h"

#include <glog/logging.h>

#include <cstring>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/fcntl.h>

// for some platforms (macOS) HOST_NAME_MAX is not defined
#ifndef HOST_NAME_MAX
	#define HOST_NAME_MAX 255
#endif

/// packet buffer size
static const size_t kClientBufferSz = (1024 * 8);
/// control buffer size for recvfrom
static const size_t kControlBufSz = (1024);

// current software version
extern const uint32_t kLichtensteinSWVersion;



/**
 * Trampoline to get into the worker thread
 */
void ProtocolHandlerThreadEntry(void *ctx) {
	(static_cast<ProtocolHandler *>(ctx))->workerEntry();
}



/**
 * Initializes the protocol handler
 */
ProtocolHandler::ProtocolHandler(INIReader *_config) : config(_config) {
	int err = 0;
	int fd[2];

	// create the threada communicating pipe
	err = pipe(fd);
	PLOG_IF(FATAL, err != 0) << "Couldn't create pipe";

	this->workerPipeRead = fd[0];
	this->workerPipeWrite = fd[1];

	// enable non-blocking IO on the read end of the pipe
	int flags = fcntl(this->workerPipeRead, F_GETFL);
	PCHECK(flags != -1) << "Couldn't get flags";

	flags |= O_NONBLOCK;

	err = fcntl(this->workerPipeRead, F_SETFL, flags);
	PCHECK(err != -1) << "Couldn't set flags";

	// start the thread
	this->start();
}

/**
 * Cleans up all resources, and stops the handler if it hasn't been already.
 */
ProtocolHandler::~ProtocolHandler() {
	int err = 0;

	// is the thread still running?
	if(this->run || this->worker != nullptr) {
		this->stop();
	}

	// close the thread communicating pipe
	close(this->workerPipeRead);
	close(this->workerPipeWrite);
}



/**
 * Starts the protocol handler thread.
 */
void ProtocolHandler::start(void) {
	// make sure there's no existing thread
	CHECK(this->worker == nullptr) << "Trying to start thread when it's already running";

	// create a thread
	this->worker = new std::thread(ProtocolHandlerThreadEntry, this);
}

/**
 * Stops the protocol handler thread.
 */
void ProtocolHandler::stop(void) {
	int err = 0;

	// clear the running flag
	this->run = false;

	// write to the pipe to unblock the thread
	int blah = kWorkerShutdown;
	err = write(this->workerPipeWrite, &blah, sizeof(blah));

	if(err < 0) {
		PLOG(ERROR) << "Couldn't write to pipe";

		// if we can't write to the pipe we're fucked, just kill the socket
		close(this->socket);
		this->socket = -1;
	}

	// wait for thread to end
	this->worker->join();

	delete this->worker;
	this->worker = nullptr;
}



/**
 * Thread entry point
 */
void ProtocolHandler::workerEntry(void) {
	int err = 0, rsz;
	fd_set readfds;

	// set up sockets
	this->setUpSocket();

	// allocate the read buffer
	char *buffer = new char[kClientBufferSz];

	// used for recvmsg
	struct msghdr msg;
	struct sockaddr_storage srcAddr;

	struct iovec iov[1];
	iov[0].iov_base = buffer;
	iov[0].iov_len = kClientBufferSz;

	char *controlBuf = new char[kControlBufSz];

	// send an announcement when the thread becomes alive
	this->sendAnnouncement();

	// main loop; wait on socket and pipe
	while(this->run) {
		// set up the read descriptors we wait on
		int max = this->socket;

		if(this->workerPipeRead > max) {
			max = this->workerPipeRead;
		}

		FD_ZERO(&readfds);
		FD_SET(this->socket, &readfds);
		FD_SET(this->workerPipeRead, &readfds);

		// block on the file descriptors
		err = select((max + 1), &readfds, nullptr, nullptr, nullptr);

		if(err < 0) {
			PLOG(INFO) << "select failed";
			continue;
		}

		// did we receive anything on the socket?
		if(FD_ISSET(this->socket, &readfds)) {
			// populate the message buffer
			memset(&msg, 0, sizeof(msg));

			msg.msg_name = &srcAddr;
			msg.msg_namelen = sizeof(srcAddr);
			msg.msg_iov = iov;
			msg.msg_iovlen = 1;
			msg.msg_control = controlBuf;
			msg.msg_controllen = kControlBufSz;

			// clear the buffer, then read from the socket
			memset(buffer, 0, kClientBufferSz);
			rsz = recvmsg(this->socket, &msg, 0);

			// if the read size was zero, the connection was closed
			if(rsz == 0) {
				LOG(WARNING) << "Connection " << this->socket << " closed by host";
				break;
			}
			// handle error conditions
			else if(rsz == -1) {
				PLOG(WARNING) << "Couldn't read from socket: ";
				continue;
			}
			// otherwise, try to parse the packet
			else {
				VLOG(3) << "Received " << rsz << " bytes";
				// this->handlePacket(buffer, rsz, &msg);
			}
		}

		// did we receive something on the pipe?
		if(FD_ISSET(this->workerPipeRead, &readfds)) {
			// read an int from the pipe
			int command = kWorkerNOP;
			rsz = read(this->workerPipeRead, &command, sizeof(command));

			if(rsz <= 0) {
				PLOG(WARNING) << "Couldn't read from worker pipe:";
			} else {
				// process the command
				switch(command) {
					// No-op, do nothing
					case kWorkerNOP: {
						break;
					}

					// Shut down the thread
					case kWorkerShutdown: {
						LOG(INFO) << "Shutting down worker thread";
						break;
					}

					// send an announcement packet
					case kWorkerAnnounce: {
						this->sendAnnouncement();
						break;
					}

					// shouldn't get here
					default: {
						LOG(WARNING) << "Unknown command " << command;
						break;
					}
				}
			}
		}
	}

	// clean up
	this->cleanUpSocket();
}

/**
 * Handles a received packet.
 */
void ProtocolHandler::handlePacket(void *packet, size_t length, struct msghdr *msg) {
	int err;
	struct cmsghdr *cmhdr;

	static const socklen_t destAddrSz = 128;
	char destAddr[destAddrSz];

	bool isMulticast = false;

	// go through the buffer and find IP_PKTINFO
	for(cmhdr = CMSG_FIRSTHDR(msg); cmhdr != nullptr; cmhdr = CMSG_NXTHDR(msg, cmhdr)) {
		// check if it's the right type
		if(cmhdr->cmsg_level == IPPROTO_IP && cmhdr->cmsg_type == IP_PKTINFO) {
			void *data = CMSG_DATA(cmhdr);
			struct in_pktinfo *info = static_cast<struct in_pktinfo *>(data);

			// check if it's multicast (they're class D, i.e. 1110 MSB)
			unsigned int addr = ntohl(info->ipi_addr.s_addr);
			isMulticast = ((addr >> 28) == 0x0E);

			// convert the destination address
			const char *ptr = inet_ntop(AF_INET, &info->ipi_addr, destAddr, destAddrSz);
			CHECK(ptr != nullptr) << "Couldn't convert destination address";
		}
    }

	// if it's a multicast packet, pass it to the discovery handler
	if(isMulticast) {
		VLOG(2) << "Received multicast packet: forwarding to discovery handler";
	}
	// it's not a multicast packet. neat
	else {
		LichtensteinUtils::PacketErrors pErr;

		// check validity
		pErr = LichtensteinUtils::validatePacket(packet, length);

		if(err != LichtensteinUtils::kNoError) {
			LOG(ERROR) << "Couldn't verify unicast packet: " << err;
			return;
		}

		// check to see what type of packet it is
		LichtensteinUtils::convertToHostByteOrder(packet, length);
		lichtenstein_header_t *header = static_cast<lichtenstein_header_t *>(packet);

		VLOG(3) << "Received unicast packet with opcode " << header->opcode;

		// is it an acknowledgement?
		if((header->flags & kFlagAck)) {
			// TODO: handle acknowledgements
		} else {
			// TODO: handle all others
		}
	}
}

/**
 * Sends a node announcement packet.
 */
void ProtocolHandler::sendAnnouncement(void) {
	int err = 0;
	struct sockaddr_in addr;

	// set up the destination address of the multicast group
	int port = this->config->GetInteger("client", "port", 7420);
	string address = this->config->Get("client", "multicastGroup", "239.42.0.69");

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	err = inet_pton(AF_INET, address.c_str(), &addr.sin_addr.s_addr);
	PLOG_IF(FATAL, err != 1) << "Couldn't convert IP address";

	// get MAC address and hostname
	uint8_t macAddr[6] = {0xd8, 0xde, 0xad, 0xbe, 0xef, 0x00};

	char hostname[HOST_NAME_MAX];
	memset(hostname, 0, sizeof(hostname));

	err = gethostname(hostname, sizeof(hostname));
	PLOG_IF(FATAL, err != 0) << "Couldn't get hostname";

	size_t hostnameLen = strlen(hostname) + 1;

	// allocate the packet
	size_t totalPacketLen = sizeof(lichtenstein_node_announcement_t) + hostnameLen;

	lichtenstein_node_announcement_t *announce = static_cast<lichtenstein_node_announcement_t *>(malloc(totalPacketLen + 16));
	memset(announce, 0, (totalPacketLen + 16));

	// versions
	announce->swVersion = kLichtensteinSWVersion;
	announce->hwVersion = 0x00001000; // TODO: figure this out properly

	// IP address and port
	announce->port = port;

	// TODO: properly get IP address
	inet_pton(AF_INET, "172.16.20.69", &announce->ip);

	// copy hostname and MAC address
	announce->hostnameLen = hostnameLen;
	strncpy(static_cast<char *>(announce->hostname), hostname, (hostnameLen - 1));

	memcpy(announce->macAddr, macAddr, 6);

	// populate number of channels and framebuffer size
	announce->fbSize = this->config->GetInteger("output", "fbsize", 1048576);
	announce->channels = this->config->GetInteger("output", "channels", 1);


	// prepare it for sending
	announce->header.flags |= kFlagMulticast;

	LichtensteinUtils::populateHeader(announce, kOpcodeNodeAnnouncement);
	LichtensteinUtils::convertToNetworkByteOrder(announce, totalPacketLen);
	LichtensteinUtils::applyChecksum(announce, totalPacketLen);

	// send it
	err = sendto(this->announcementSocket, announce, totalPacketLen,
				 0, (struct sockaddr *) &addr, sizeof(addr));
	PLOG_IF(FATAL, err == -1) << "Couldn't send announcement";

	// clean up
	free(announce);
}



/**
 * Sets up the UDP listening socket, and a socket to send node announcements
 * with.
 */
void ProtocolHandler::setUpSocket(void) {
	int err = 0;
	struct sockaddr_in addr;
	int nbytes, addrlen;
	struct ip_mreq mreq;

	unsigned int yes = 1;

	// clear the address
	memset(&addr, 0, sizeof(addr));

	// get the port and IP address to listen on
	int port = this->config->GetInteger("client", "port", 7420);
	string address = this->config->Get("client", "listen", "0.0.0.0");

	err = inet_pton(AF_INET, address.c_str(), &addr.sin_addr.s_addr);
	PLOG_IF(FATAL, err != 1) << "Couldn't convert IP address: ";

	// create the socket
	this->socket = ::socket(AF_INET, SOCK_DGRAM, 0);
	PLOG_IF(FATAL, this->socket < 0) << "Couldn't create listening socket";

	// allow re-use of the address
	err = setsockopt(this->socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	PLOG_IF(FATAL, err < 0) << "Couldn't set SO_REUSEADDR";

	// enable the socket info struct
	err = setsockopt(this->socket, IPPROTO_IP, IP_PKTINFO, &yes, sizeof(yes));
	PLOG_IF(FATAL, err < 0) << "Couldn't set SO_REUSEADDR";

	// set up the destination address
	addr.sin_family = AF_INET;
	// addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);

	// bind to this address
	err = ::bind(this->socket, (struct sockaddr *) &addr, sizeof(addr));
	PLOG_IF(FATAL, err < 0) << "Couldn't bind listening socket on port " << port;

	LOG(INFO) << "Opened listening socket on port " << port;

	// enable non-blocking IO on the socket
	int flags = fcntl(this->socket, F_GETFL);
	PCHECK(flags != -1) << "Couldn't get flags";

	flags |= O_NONBLOCK;

	err = fcntl(this->socket, F_SETFL, flags);
	PCHECK(err != -1) << "Couldn't set flags";


	// set up the announcement socket
	this->announcementSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
	PLOG_IF(FATAL, this->announcementSocket < 0) << "Couldn't create announcement socket";
}

/**
 * Closes the UDP socket used to receive data.
 */
void ProtocolHandler::cleanUpSocket(void) {
	int err = 0;

	// all we're doing is closing the socket
	err = close(this->socket);
	PLOG_IF(ERROR, err != 0) << "Couldn't close socket";

	// close the announcement socket
	err = close(this->announcementSocket);
	PLOG_IF(ERROR, err != 0) << "Couldn't close announcement socket";
}
