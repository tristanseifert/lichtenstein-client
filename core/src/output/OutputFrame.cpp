#include "lichtenstein_proto.h"

#include "OutputFrame.h"
#include "../net/ProtocolHandler.h"

#include <glog/logging.h>

#include <cstdlib>
#include <cstring>

/**
 * Allocates an output frame by making a copy of the given data.
 */
OutputFrame::OutputFrame(size_t _channel, void *_data, size_t _dataLen) : channel(_channel),  dataLen(_dataLen) {
	// make a copy of the buffer
	this->data = malloc(_dataLen);
	CHECK(this->data != nullptr) << "Couldn't allocate buffer!";

	memcpy(this->data, _data, _dataLen);
}

/**
 * Given a Lichtenstein packet, allocate an output frame from it.
 */
OutputFrame::OutputFrame(lichtenstein_framebuffer_data_t *packet, ProtocolHandler *_handler, struct in_addr *_source) : protoHandler(_handler), ackDest(*_source) {
	// copy the destination channel
	this->channel = packet->destChannel;

	// calculate number of bytes required
	size_t numBytes = packet->dataElements;

	switch(packet->dataFormat) {
		// 3 bytes per element
		case kDataFormatRGB:
			numBytes *= 3;
			break;

		// 4 bytes per element
		case kDataFormatRGBW:
			numBytes *= 4;
			break;

		// shouldn't happen
		default:
			LOG(ERROR) << "Unknown data format " << packet->dataFormat;
			return;
	}

	// allocate buffer and copy
	this->dataLen = numBytes;

	this->data = malloc(this->dataLen);
	CHECK(this->data != nullptr) << "Couldn't allocate buffer!";

	memcpy(this->data, &packet->data, this->dataLen);

	// generate the ack packet
	this->ackPacket = this->protoHandler->createUnicastAck(&packet->header);
}

/**
 * Releases the memory held by the output frame.
 */
OutputFrame::~OutputFrame() {
	// deallocate data
	if(this->data) {
		free(this->data);
	}

	// deallocate the acknowledgement packet, if we allocated it earlier
	if(this->ackPacket) {
		free(this->ackPacket);
	}
}
