#include "ProtocolHandler.h"

#include <glog/logging.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/fcntl.h>

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
	int blah = 0;
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
	int err = 0;
	fd_set readfds;

	// set up sockets
	this->setUpSocket();

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

		}

		// did we receive something on the pipe?
		if(FD_ISSET(this->workerPipeRead, &readfds)) {

		}
	}

	// clean up
	this->cleanUpSocket();
}

/**
 * Sets up the UDP socket used to receive data.
 */
void ProtocolHandler::setUpSocket(void) {
	int err = 0;
	struct sockaddr_in addr;
	int nbytes, addrlen;
	struct ip_mreq mreq;

	unsigned int yes = 1;

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
	memset(&addr, 0, sizeof(addr));
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
}

/**
 * Closes the UDP socket used to receive data.
 */
void ProtocolHandler::cleanUpSocket(void) {
	int err = 0;

	// all we're doing is closing the socket
	err = close(this->socket);
	PLOG_IF(ERROR, err != 0) << "Couldn't close socket";
}
