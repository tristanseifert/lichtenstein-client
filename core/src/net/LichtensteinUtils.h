/**
 * Various utilities for dealing with Lichtenstein packets, including checksum
 * verification.
 */
#ifndef LICHTENSTEINUTILS_H
#define LICHTENSTEINUTILS_H

#include <cstdint>
#include <cstddef>

class LichtensteinUtils {
	public:
		enum PacketErrors {
			kNoError = 0,

			kInvalidChecksum = 0x100,
			kPacketTooSmall,
			kInvalidMagic
		};

	public:
		static void populateHeader(void *header, uint16_t opcode);

		static PacketErrors validatePacket(void *data, size_t length);

		static PacketErrors applyChecksum(void *Data, size_t length);

		static int convertToHostByteOrder(void *data, size_t length) {
			return LichtensteinUtils::convertPacketByteOrder(data, true, length);
		}

		static int convertToNetworkByteOrder(void *data, size_t length) {
			return LichtensteinUtils::convertPacketByteOrder(data, false, length);
		}

		static bool validatePacketSimple(void *data, size_t length) {
			return (validatePacket(data, length) == kNoError);
		}

	private:
		static int convertPacketByteOrder(void *_packet, bool fromNetworkOrder, size_t length);

	private:
		static void _convertToHostNodeAnnouncement(void *data, size_t length);
};

#endif
