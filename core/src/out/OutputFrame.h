/**
 * Stores a single frame of data for a particular channel.
 */
#ifndef OUTPUTFRAME_H
#define OUTPUTFRAME_H

#ifndef LICHTENSTEINPROTO_H
	// forward declare framebuffer data type
	struct lichtenstein_framebuffer_data;
	typedef struct lichtenstein_framebuffer_data lichtenstein_framebuffer_data_t;

	// forward declare header type
	struct lichtenstein_header;
	typedef struct lichtenstein_header lichtenstein_header_t;
#endif

#include <cstddef>

// for struct in_addr
#include <netinet/in.h>

class ProtocolHandler;

class OutputFrame {
	// allow the handler to access the packet
	// friend class ProtocolHandler;

	public:
		OutputFrame() = delete;
		~OutputFrame();

		OutputFrame(size_t channel, void *data, size_t dataLen);
		OutputFrame(lichtenstein_framebuffer_data_t *packet, ProtocolHandler *handler, struct in_addr *source);

	public:
		size_t getChannel(void) const {
			return this->channel;
		}

		void *getData(void) const {
			return this->data;
		}
		size_t getDataLen(void) const {
			return this->dataLen;
		}

		lichtenstein_header_t *getAckPacket(void) const {
			return this->ackPacket;
		}
		struct in_addr *getAckDest(void) const {
			return const_cast<struct in_addr *>(&this->ackDest);
		}

	private:
		size_t channel = 0;

		void *data = nullptr;
		size_t dataLen = 0;

	private:
		ProtocolHandler *protoHandler = nullptr;

		lichtenstein_header_t *ackPacket = nullptr;
		struct in_addr ackDest;
};

#endif
