#include "LichtensteinUtils.h"

#include <iostream>

#include <glog/logging.h>

#include "lichtenstein_proto.h"
#include "crc32/crc32.h"

// for htons and friends
#include <arpa/inet.h>

using namespace std;

/**
 * Validates the given packet. If the packet cannot be verified, an error is
 * returned. This assumes that the packet is right off the wire, i.e. that all
 * multibyte values are still in network order.
 */
LichtensteinUtils::PacketErrors LichtensteinUtils::validatePacket(void *data, size_t length) {
	lichtenstein_header_t *header = static_cast<lichtenstein_header_t *>(data);

	// validate packet length
	if(length < sizeof(lichtenstein_header_t)) {
		LOG(WARNING) << "attempted to convert a packet smaller than the header";
		return kPacketTooSmall;
	}

	// verify the checksum, if requested
	if(header->flags & htons(kFlagChecksummed)) {
		size_t checksummedDataStart = offsetof(lichtenstein_header_t, opcode);
		void *checksummedData = static_cast<uint8_t *>(data) + checksummedDataStart;
		size_t checksummedLength = (length - checksummedDataStart);

		uint32_t crc = crc32_fast(checksummedData, checksummedLength);
		uint32_t packetCrc = ntohl(header->checksum);

		LOG_IF(WARNING, crc != packetCrc) << "CRC mismatch on packet 0x" << data
										  << "! Got " << hex << packetCrc
										  << ", expected " << hex << crc;

		if(crc != packetCrc) {
			return kInvalidChecksum;
		}
	}

	// validate the header magic value
	if(header->magic != htonl(kLichtensteinMagic)) {
		LOG(WARNING) << "invalid magic value, got 0x" << hex << ntohl(header->magic);
		return kInvalidMagic;
	}

	// if we get down here, the packet must be valid. neat!
	return kNoError;
}

/**
 * Adds a checksum to the packet.
 *
 * TODO: The CRC code is somehow broken, in that the table is wrong, because the
 * embedded code (with the 1 byte table) gets a different result than the code
 * we use that uses the whole table.
 */
LichtensteinUtils::PacketErrors LichtensteinUtils::applyChecksum(void *data, size_t length) {
	lichtenstein_header_t *header = static_cast<lichtenstein_header_t *>(data);

	// validate packet length
	if(length < sizeof(lichtenstein_header_t)) {
		LOG(WARNING) << "attempted to convert a packet smaller than the header";
		return kPacketTooSmall;
	}

	// get CRC offset into the packet
	size_t offset = offsetof(lichtenstein_header_t, opcode);
	void *ptr = ((uint8_t *) header) + offset;
	size_t len = (length - offset);

	uint32_t crc = crc32_fast(ptr, len);

	header->checksum = __builtin_bswap32(crc);

	return kNoError;
}

/**
 * Swaps all multibyte fields in a packet.
 *
 * TODO: Add error checking
 *
 * @param _packet Packet
 * @param fromNetworkorder Set if the packet is in network order
 * @param length Total number of bytes in packet
 *
 * @return 0 if the conversion was a success, error code otherwise.
 */
int LichtensteinUtils::convertPacketByteOrder(void *_packet, bool fromNetworkOrder, size_t length) {
	lichtenstein_header_opcode_t opcode;

	// first, process the header
	lichtenstein_header_t *header = (lichtenstein_header_t *) _packet;

	header->magic = __builtin_bswap32(header->magic);
	header->version = __builtin_bswap32(header->version);
	header->checksum = __builtin_bswap32(header->checksum);

	if(fromNetworkOrder) {
		header->opcode = __builtin_bswap16(header->opcode);
		opcode = (lichtenstein_header_opcode_t) header->opcode;
	} else {
		opcode = (lichtenstein_header_opcode_t) header->opcode;
		header->opcode = __builtin_bswap16(header->opcode);
	}

	header->flags = __builtin_bswap16(header->flags);

	header->sequenceIndex = __builtin_bswap16(header->sequenceIndex);
	header->sequenceNumPackets = __builtin_bswap16(header->sequenceNumPackets);

	header->txn = __builtin_bswap32(header->txn);
	header->payloadLength = __builtin_bswap32(header->payloadLength);

	// return immediately if payload length is zero (its a request)
	if(header->payloadLength == 0) {
		return 0;
	}

	// handle each packet type individually
	switch(opcode) {
		// node announcement
		case kOpcodeNodeAnnouncement: {
			// ensure the length is correct
			if(length < sizeof(lichtenstein_node_announcement_t)) {
				LOG(WARNING) << "Node announcement packet too small!";
				return -1;
			}

			lichtenstein_node_announcement_t *announce;
			announce = (lichtenstein_node_announcement_t *) _packet;

			// byteswap all fields
			announce->swVersion = __builtin_bswap32(announce->swVersion);
			announce->hwVersion = __builtin_bswap32(announce->hwVersion);

			announce->port = __builtin_bswap16(announce->port);
			// don't byteswap IP, it's already in network byte order

			announce->fbSize = __builtin_bswap32(announce->fbSize);
			announce->channels = __builtin_bswap16(announce->channels);

			announce->numGpioDigitalIn = __builtin_bswap16(announce->numGpioDigitalIn);
			announce->numGpioDigitalOut = __builtin_bswap16(announce->numGpioDigitalOut);
			announce->numGpioAnalogIn = __builtin_bswap16(announce->numGpioAnalogIn);
			announce->numGpioAnalogOut = __builtin_bswap16(announce->numGpioAnalogOut);

			announce->hostnameLen = __builtin_bswap16(announce->hostnameLen);
			break;
		}
		// server announcement
		case kOpcodeServerAnnouncement: {
			// ensure the length is correct
			if(length < sizeof(lichtenstein_server_announcement_t)) {
				LOG(WARNING) << "Server announcement packet too small!";
				return -1;
			}

			lichtenstein_server_announcement_t *announce;
			announce = (lichtenstein_server_announcement_t *) _packet;

			// byteswap all fields
			announce->swVersion = __builtin_bswap32(announce->swVersion);
			announce->capabilities = __builtin_bswap32(announce->capabilities);

			announce->port = __builtin_bswap16(announce->port);
			announce->hostnameLen = __builtin_bswap16(announce->hostnameLen);

			break;
		}

		// node status
		case kOpcodeNodeStatusReq: {
			// ensure the length is correct
			if(length < sizeof(lichtenstein_node_status_t)) {
				LOG(WARNING) << "Status request packet too small!";
				return -1;
			}

			lichtenstein_node_status_t *status;
			status = (lichtenstein_node_status_t *) _packet;

			// byteswap all fields
			status->uptime = __builtin_bswap32(status->uptime);

			status->totalMem = __builtin_bswap32(status->totalMem);
			status->freeMem = __builtin_bswap32(status->freeMem);

			status->rxPackets = __builtin_bswap32(status->rxPackets);
			status->txPackets = __builtin_bswap32(status->txPackets);
			status->packetsWithInvalidCRC = __builtin_bswap32(status->packetsWithInvalidCRC);

			status->framesOutput = __builtin_bswap32(status->framesOutput);

			status->outputState = __builtin_bswap16(status->outputState);
			status->cpuUsagePercent = __builtin_bswap16(status->cpuUsagePercent);

			status->avgConversionTimeUs = __builtin_bswap32(status->avgConversionTimeUs);

			status->rxBytes = __builtin_bswap32(status->rxBytes);
			status->txBytes = __builtin_bswap32(status->txBytes);

			status->rxSymbolErrors = __builtin_bswap32(status->rxSymbolErrors);

			status->mediumSpeed = __builtin_bswap32(status->mediumSpeed);
			status->mediumDuplex = __builtin_bswap32(status->mediumDuplex);

			break;
		}


		// framebuffer data
		case kOpcodeFramebufferData: {
			// ensure the length is correct
			if(length < sizeof(lichtenstein_framebuffer_data_t)) {
				LOG(WARNING) << "Framebuffer data packet too small!";
				return -1;
			}

			lichtenstein_framebuffer_data_t *fb;
			fb = (lichtenstein_framebuffer_data_t *) _packet;

			fb->destChannel = __builtin_bswap32(fb->destChannel);

			fb->dataFormat = __builtin_bswap32(fb->dataFormat);
			fb->dataElements = __builtin_bswap32(fb->dataElements);
			break;
		}
		// output command
		case kOpcodeSyncOutput: {
			// ensure the length is correct
			if(length < sizeof(lichtenstein_sync_output_t)) {
				LOG(WARNING) << "Sync output packet too small!";
				return -1;
			}

			lichtenstein_sync_output_t *out;
			out = (lichtenstein_sync_output_t *) _packet;

			out->channels = __builtin_bswap32(out->channels);
			break;
		}

		// node adoption
		case kOpcodeNodeAdoption: {
			size_t numChannels = 0;

			// ensure the length is correct
			if(length < sizeof(lichtenstein_node_adoption_t)) {
				LOG(WARNING) << "Node adoption packet too small!";
				return -1;
			}

			lichtenstein_node_adoption_t *adopt;
			adopt = (lichtenstein_node_adoption_t *) _packet;

			// byteswap regular fields
			adopt->port = __builtin_bswap16(adopt->port);
			adopt->flags = __builtin_bswap16(adopt->flags);

			if(fromNetworkOrder) {
				adopt->numChannels = __builtin_bswap32(adopt->numChannels);
				numChannels = adopt->numChannels;
			} else {
				numChannels = adopt->numChannels;
				adopt->numChannels = __builtin_bswap32(adopt->numChannels);
			}

			// byteswap the pixels per channel array
			for(size_t i = 0; i < numChannels; i++) {
				adopt->pixelsPerChannel[i] = __builtin_bswap32(adopt->pixelsPerChannel[i]);
			}

			break;
		}

		// reconfig
		case kOpcodeNodeReconfig: {
			// ensure the length is correct
			if(length < sizeof(lichtenstein_node_adoption_t)) {
				LOG(WARNING) << "Node reconfig packet too small!";
				return -1;
			}

			lichtenstein_reconfig_t *adopt;
			adopt = (lichtenstein_reconfig_t *) _packet;

			break;
		}

		// should never get here
		default: {
			LOG(ERROR) << "Unknown packet type " << opcode;
			return -1;
		}
	}

	// if we get down here, conversion was a success
	return 0;
}

/**
 * Populates the header of a Lichtenstein packet.
 *
 * @param header Pointer to either lichtenstein_header_t or another packet struct
 * that has the header as its first element.
 * @param opcode Opcode to insert into the packet.
 */
void LichtensteinUtils::populateHeader(void *data, uint16_t opcode) {
	lichtenstein_header_t *header = static_cast<lichtenstein_header_t *>(data);

	// insert magic, version, and opcode
	header->magic = kLichtensteinMagic;
	header->version = kLichtensteinVersion10;

	header->opcode = opcode;

	// we don't really care about sequences
	header->sequenceIndex = 0;
	header->sequenceNumPackets = 0;

	// insert random transaction number; if we need a meaningful one, it can be overridden
	header->txn = std::rand();

	// set a checksum flag
	header->flags |= kFlagChecksummed;
	header->checksum = 0;
}
