#include "ProtocolHandler.h"

#include "LichtensteinUtils.h"
#include "lichtenstein_proto.h"

#include "../output/OutputFrame.h"

#include "../status/StatusHandler.h"

#include <glog/logging.h>
#include <INIReader.h>
#include <cpptime.h>

#include <chrono>
#include <bitset>

#include <cstring>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/select.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <net/if.h>
#include <ifaddrs.h>

// the sysctl header is different on linux
#ifdef __linux__
#include <linux/sysctl.h>
#else
#include <sys/sysctl.h>
#endif

// sysinfo header for linux
#ifdef __linux
#include <sys/sysinfo.h>
#endif

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
	PLOG_IF(FATAL, err != 0) << "Couldn't create pipe, fcntl is fucked";

	this->workerPipeRead = fd[0];
	this->workerPipeWrite = fd[1];

	// enable non-blocking IO on the read end of the pipe
	int flags = fcntl(this->workerPipeRead, F_GETFL);
	PCHECK(flags != -1) << "Couldn't get flags, fcntl is fucked";

	flags |= O_NONBLOCK;

	err = fcntl(this->workerPipeRead, F_SETFL, flags);
	PCHECK(err != -1) << "Couldn't set flags, fcntl is fucked";

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
	CHECK(this->worker == nullptr) << "Trying to start thread when it's already running, fuck off";

	// run
	this->run = true;

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
		PLOG(ERROR) << "Couldn't write to pipe, shit's fucked";

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

	// send an announcement when the thread becomes alive and alloc timer
	double initial = this->config->GetReal("client", "announcementIntervalInitial", 10);
	const unsigned long initialLong = static_cast<unsigned long>(initial * 1000);

	double subsequent = this->config->GetReal("client", "announcementInterval", 10);
	const unsigned long subsequentLong = static_cast<unsigned long>(subsequent * 1000);

	auto announcementTimer = this->timer.add(std::chrono::milliseconds(initialLong),
	[this](CppTime::timer_id) {
		// send the command
		int blah = kWorkerAnnounce;
		int err = write(this->workerPipeWrite, &blah, sizeof(blah));

		PLOG_IF(ERROR, err < 0) << "Couldn't write announcement command";

		// this->sendAnnouncement();
	}, std::chrono::milliseconds(subsequentLong));

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
				this->handlePacket(buffer, rsz, &msg);
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

	// clear the timer
	this->timer.remove(announcementTimer);

	// clean up
	this->cleanUpSocket();
}

/**
 * Handles a received packet.
 */
void ProtocolHandler::handlePacket(void *packet, size_t length, struct msghdr *msg) {
	int err;
	struct cmsghdr *cmhdr;

	static const socklen_t srcAddrSz = 128;
	char srcAddr[srcAddrSz];
	struct in_addr srcAddrStruct;

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
			const char *ptr = inet_ntop(AF_INET, &info->ipi_addr, srcAddr, srcAddrSz);
			CHECK(ptr != nullptr) << "Couldn't convert destination address";

			// copy the address
			memcpy(&srcAddrStruct, &info->ipi_addr, sizeof(srcAddrStruct));
		}
    }

	// validate the packet
	LichtensteinUtils::PacketErrors pErr;

	// check validity
	pErr = LichtensteinUtils::validatePacket(packet, length);

	if(err != LichtensteinUtils::kNoError) {
		// is it a checksum error?
		if(err == LichtensteinUtils::kInvalidChecksum) {
			// if so, increment that counter
			this->packetsWithInvalidCRC++;
		}

		LOG(ERROR) << "Couldn't verify packet: " << err
			<< "(multicast: " << isMulticast << ")";
		return;
	}

	// reset the timer
	this->lastServerMessageOn = time(nullptr);

	// check to see what type of packet it is
	LichtensteinUtils::convertToHostByteOrder(packet, length);
	lichtenstein_header_t *header = static_cast<lichtenstein_header_t *>(packet);

	// apply the multicast and request masks
	unsigned int type = header->opcode;

	const unsigned int kMulticastMask = 0x100;
	const unsigned int kRequestMask = 0x200;
	const unsigned int kAckMask = 0x400;

	if(isMulticast) type |= kMulticastMask;
	if(header->payloadLength == 0) type |= kRequestMask;
	if((header->flags & kFlagAck)) type |= kAckMask;

	VLOG(3) << "Received packet with opcode " << header->opcode
		<< "(multicast " << isMulticast << ")";

	// handle the packet
	switch(type) {
		// status request?
		case (kOpcodeNodeStatusReq | kRequestMask):
			this->sendStatusResponse(header, &srcAddrStruct);
			break;

		// node adoption?
		case kOpcodeNodeAdoption:
			if(this->isAdopted == false) {
				// TODO: handle adoption

				// set status
				StatusHandler::sharedInstance()->setAdoptionState(true);
			} else {
				LOG(WARNING) << "Attempted adoption by " << srcAddr << "!";
			}
			break;

		// received framebuffer data?
		case kOpcodeFramebufferData:
			if(this->isAdopted) {
				// create an output frame…
				lichtenstein_framebuffer_data_t *packet = reinterpret_cast<lichtenstein_framebuffer_data_t *>(header);
				OutputFrame *fr = new OutputFrame(packet, this, &srcAddrStruct);

				// …and run the callback.
				err = this->frameReceiveCallback(fr);

				if(err != 0) {
					// increment counter
					this->framebufferPacketsDiscarded++;

					// log
					LOG(WARNING) << "Couldn't process framebuffer data: " << err;

					// create a negative ack
					lichtenstein_header_t *hdr = fr->getAckPacket();

					hdr->flags &= ~kFlagAck;
					hdr->flags |= ~kFlagNAck;

					// send it
					this->ackOutputFrame(fr);

					// delete frame
					delete fr;
				}
			} else {
				LOG(WARNING) << "Received framebuffer data from " << srcAddr << ", but node isn't adopted, that server needs to fuck off";
			}
			break;

		// received sync output? (this _should_ be multicast but handle unicast too)
		case kOpcodeSyncOutput:
		case (kOpcodeSyncOutput | kMulticastMask):
			if(this->isAdopted) {
				lichtenstein_sync_output_t *packet = reinterpret_cast<lichtenstein_sync_output_t *>(header);

				// make it a bitfield
				std::bitset<32> channels(packet->channels);

				// call into the plugin handler
				err = this->channelOutputCallback(channels);

				if(err != 0) {
					// increment counter
					this->outputPacketsDiscarded++;

					// log
					LOG(WARNING) << "Couldn't process channel output: " << err;

					// nack
					this->ackUnicast(header, &srcAddrStruct, true);
				}
			} else {
				LOG(WARNING) << "Received output request from " << srcAddr << ", but node isn't adopted";
			}
			break;

		// keepalive; just ack it and reset the adoption timer
		case kOpcodeKeepalive:
			// TODO: reset adoption timer
			this->ackUnicast(header, &srcAddrStruct);
			break;

		// unhandled packet type
		default:
			LOG(INFO) << "Request for unimplemented opcode " << header->opcode
				<< " (flags 0x" << std::hex << type << ")";
			break;
	}
}



/**
 * Acknowledges an unicast packet without sending any additional data back to
 * the server.
 */
void ProtocolHandler::ackUnicast(lichtenstein_header_t *header, struct in_addr *source, bool nack) {
	int err;

	// get the packet
	void *packet = this->createUnicastAck(header, nack);

	// send
	err = this->sendPacketToHost(packet, sizeof(lichtenstein_header_t), source);
	LOG_IF(ERROR, err != 0) << "Couldn't send packet: " << err;

	// clean up
	free(packet);
}

/**
 * Generates an acknowledgement packet.
 */
lichtenstein_header_t *ProtocolHandler::createUnicastAck(lichtenstein_header_t *header, bool nack) {
	// allocate the packet
	size_t totalPacketLen = sizeof(lichtenstein_header_t);

	lichtenstein_header_t *packet = static_cast<lichtenstein_header_t *>(malloc(totalPacketLen));
	CHECK(packet != nullptr) << "Couldn't allocate packet!";

	memset(packet, 0, totalPacketLen);

	// prepare the packet
	LichtensteinUtils::populateHeader(packet, header->opcode);

	// set the nack/ack flags
	if(!nack) {
		packet->flags |= kFlagAck;
	} else {
		packet->flags |= kFlagNAck;
	}

	packet->txn = header->txn;

	LichtensteinUtils::convertToNetworkByteOrder(packet, totalPacketLen);
	LichtensteinUtils::applyChecksum(packet, totalPacketLen);

	// done
	return packet;
}

/**
 * Acknowledges an output frame.
 */
void ProtocolHandler::ackOutputFrame(OutputFrame *frame, bool nack) {
	int err;

	// get the packet
	void *packet = frame->getAckPacket();
	struct in_addr *dest = frame->getAckDest();

	// modify the packet if nack is true
	if(nack) {
		lichtenstein_header_t *hdr = static_cast<lichtenstein_header_t *>(packet);

		// clear ACK flag, set NACK
		hdr->flags &= ~kFlagAck;
		hdr->flags |= kFlagNAck;
	}

	// send
	err = this->sendPacketToHost(packet, sizeof(lichtenstein_header_t), dest);
	LOG_IF(ERROR, err != 0) << "Couldn't send packet: " << err;
}



/**
 * Sends a status response packet.
 */
void ProtocolHandler::sendStatusResponse(lichtenstein_header_t *header, struct in_addr *source) {
	int err;

	// allocate the packet
	size_t totalPacketLen = sizeof(lichtenstein_node_status_t);

	lichtenstein_node_status_t *status = static_cast<lichtenstein_node_status_t *>(malloc(totalPacketLen));
	memset(status, 0, totalPacketLen);

	// get uptime
	status->uptime = this->getUptime();

	// get total and free memory (this only works on linux)
#ifdef __linux__
	struct sysinfo info;

	// get the struct info
	err = sysinfo(&info);
	PLOG_IF(ERROR, err != 0) << "sysinfo failed";

	unsigned int unitSz = info.mem_unit;

	status->totalMem = (info.totalram * unitSz);
	status->freeMem = (info.freeram * unitSz);
#endif

	// packets with invalid CRC
	status->packetsWithInvalidCRC = this->packetsWithInvalidCRC;

	// get CPU load (in percent)
#ifdef __linux__
	status->cpuUsagePercent = (info.loads[0] * 100);
#endif

	// prepare to send
	LichtensteinUtils::populateHeader(status, kOpcodeNodeStatusReq);

	status->header.flags |= kFlagAck;
	status->header.flags |= kFlagResponse;

	status->header.txn = header->txn;

	LichtensteinUtils::convertToNetworkByteOrder(status, totalPacketLen);
	LichtensteinUtils::applyChecksum(status, totalPacketLen);

	// send
	err = this->sendPacketToHost(status, totalPacketLen, source);
	LOG_IF(ERROR, err != 0) << "Couldn't send packet: " << err;

	// clean up
	free(status);
}

/**
 * Sends the specified data packet to the host whose address is specified.
 */
int ProtocolHandler::sendPacketToHost(void *data, size_t length, struct in_addr *dest) {
	int err;

	// prepare address
	struct sockaddr_in addr;

	// prefill it with the port
	int port = this->config->GetInteger("client", "port", 7420);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	// copy in the IP address
	memcpy(&addr.sin_addr, dest, sizeof(addr.sin_addr));

	// send the packet
	err = sendto(this->announcementSocket, data, length,
				 0, (struct sockaddr *) &addr, sizeof(addr));

	// handle errors
	if(err == -1) {
		return errno;
	}

	// no errors, yay
	return 0;
}



/**
 * Sends a node announcement packet.
 */
void ProtocolHandler::sendAnnouncement(void) {
	int err = 0;
	struct in_addr addr;

	// set up the destination address of the multicast group
	string address = this->config->Get("client", "multicastGroup", "239.42.0.69");

	err = inet_pton(AF_INET, address.c_str(), &addr);
	PLOG_IF(FATAL, err != 1) << "Couldn't convert IP address";

	// get MAC address and hostname
	uint8_t macAddr[6] = {0xd8, 0xde, 0xad, 0xbe, 0xef, 0x00};
	this->getMacAddress(reinterpret_cast<uint8_t *>(&macAddr));

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

	// get the port, then figure out how to get the IP address
	announce->port = this->config->GetInteger("client", "port", 7420);;

	// figure out if we need to get the IP address from the config
	std::string advertiseAddress = this->config->Get("client", "advertiseAddress", "0.0.0.0");

	if(advertiseAddress != "0.0.0.0") {
		err = inet_pton(AF_INET, advertiseAddress.c_str(), &announce->ip);

		PLOG_IF(FATAL, err != 1) << "Couldn't convert address '" << advertiseAddress << "'";
	} else {
		std::string listenAddress = this->config->Get("client", "listen", "0.0.0.0");

		// is the listen address non-null?
		if(listenAddress != "0.0.0.0") {
			// use the listen address instead.
			err = inet_pton(AF_INET, listenAddress.c_str(), &announce->ip);

			PLOG_IF(FATAL, err != 1) << "Couldn't convert address '" << listenAddress << "'";
		} else {
			// automatically detect the address
			char addrBuffer[32];
			this->getIpAddress(reinterpret_cast<char *>(&addrBuffer), sizeof(addrBuffer));

			err = inet_pton(AF_INET, addrBuffer, &announce->ip);
			PLOG_IF(FATAL, err != 1) << "Couldn't convert address '" << addrBuffer << "'";
		}
	}

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
	err = this->sendPacketToHost(announce, totalPacketLen, &addr);
	LOG_IF(FATAL, err == -1) << "Couldn't send announcement: " << err;

/*	err = sendto(this->announcementSocket, announce, totalPacketLen,
				 0, (struct sockaddr *) &addr, sizeof(addr));
	PLOG_IF(FATAL, err == -1) << "Couldn't send announcement";*/

	// clean up
	free(announce);
}



/**
 * Gets the MAC address of the primary interface.
 */
void ProtocolHandler::getMacAddress(uint8_t *addr) {
#ifndef __APPLE__
	// ioctls
	struct ifreq ifr;
	struct ifconf ifc;

	struct ifreq* it = nullptr;
	struct ifreq* end = nullptr;

	// buffer for hardware addresses
	const size_t bufSz = 1024;
	char buf[bufSz];
	int success = 0;

	// create a socket to get interface info
	int sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if(sock < 0) {
		PLOG(ERROR) << "Couldn't create socket";
		return;
	}

	// request a list of all interface layer addresses
	ifc.ifc_buf = buf;
	ifc.ifc_len = bufSz;

	if(ioctl(sock, SIOCGIFCONF, &ifc) == -1) {
		PLOG(ERROR) << "Couldn't get interface config struct";
		goto done;
	}

	// prepare to iterate over them
	it = ifc.ifc_req;
	end = it + (ifc.ifc_len / sizeof(struct ifreq));

	for (; it != end; ++it) {
		// copy the interface name into the interface flag struct
		strncpy(ifr.ifr_name, it->ifr_name, IFNAMSIZ);

		// get flags of that device
		if(ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
			// ignore loopback style interfaces
			if(!(ifr.ifr_flags & IFF_LOOPBACK)) {

// SIOCGIFHWADDR is not declared on macOS, so this function is a no-op
#ifdef SIOCGIFHWADDR
				// get the hardware address of that interface
				if(ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
					success = 1;
					break;
				}
#endif
			}
		} else {
			PLOG(ERROR) << "Couldn't get interface flags for " << ifr.ifr_name;
		}
	}

	// if success, copy the address
#ifdef SIOCGIFHWADDR
	if(success) {
		memcpy(addr, ifr.ifr_hwaddr.sa_data, 6);
	}
#endif

	// close socket
done: ;
	success = close(sock);
	PLOG_IF(ERROR, success != 0) << "Couldn't close socket";

#endif
}

/**
 * Gets the IP address of the system.
 *
 * This is a really dumb approach because it just picks the first interface with
 * an IPv4 address.
 */
void ProtocolHandler::getIpAddress(char *addrOut, size_t addrOutLen) {
	struct ifaddrs *interfaces;
	struct ifaddrs *current;
	int err;

	// get info
	err = getifaddrs(&interfaces);

	if(err != 0) {
		PLOG(ERROR) << "Couldn't get address info";
		return;
	}

	// iterate through it
	current = interfaces;

	while(current != nullptr) {
		// ignore loopback interfaces
		if(!(current->ifa_flags & IFF_LOOPBACK)) {
			// get its address (if IPv4 address present)
			if(current->ifa_addr->sa_family == AF_INET) {
				char ipStr[INET_ADDRSTRLEN];

				// convert address
				struct sockaddr_in *addr = (struct sockaddr_in *) current->ifa_addr;

				inet_ntop(AF_INET, &addr->sin_addr, ipStr, INET_ADDRSTRLEN);

				// get info
				VLOG(3) << "Interface name: " << current->ifa_name << " addr " << ipStr;

				strncpy(addrOut, ipStr, addrOutLen);
				goto done;
			}
		}

		// go to next
		current = current->ifa_next;
	}

	// clean up
done: ;
	freeifaddrs(interfaces);
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



/**
 * Returns the system uptime, in seconds.
 */
unsigned int ProtocolHandler::getUptime(void) {
	int err;

#ifdef __linux__

#else
	// parameters for the sysctl
	int mib[2] = {CTL_KERN, KERN_BOOTTIME};

	struct timeval boottime;
	size_t size = sizeof(boottime);

	// get current time to compare
	time_t now;
    time(&now);

	// attempt to get the info
	err = sysctl(mib, 2, &boottime, &size, NULL, 0);
    if(err != -1 && boottime.tv_sec != 0) {
		// subtract the times
        return (now - boottime.tv_sec);
    } else {
		PLOG(ERROR) << "Can't get uptime: " << err;
	}
#endif

	// couldn't get uptime
	return -1;
}
