/**
 * Data structures to implement the Lichtenstein network protocol, used to
 * communicate between the server and nodes.
 *
 * All multibyte values in this protocol are sent in network byte order.
 */
#ifndef LICHTENSTEINPROTO_H
#define LICHTENSTEINPROTO_H

#include <stdint.h>

// pack structs
#pragma pack(push, 1)

/// current protocol version
const uint32_t kLichtensteinVersion10	= 0x00010000;
/// magic value in packet header
const uint32_t kLichtensteinMagic		= 0x4c494348;

/**
 * Maximum number of supported channels per node; this affects the size of some
 * arrays in packets.
 */
const uint32_t kLichtensteinMaxChannels	= 128;

/**
 * Defined flags. The "flags" field in the packet header may only contain a
 * bitwise-OR of these flags, except Ack and NAck; they are exclusive and only
 * one may be set.
 */
typedef enum {
	kFlagMulticast				= (1 << 15),
	kFlagResponse				= (1 << 14),
	kFlagAck					= (1 << 13),
	kFlagNAck					= (1 << 12),

	kFlagChecksummed			= (1 << 0)
} lichtenstein_header_flags_t;

/**
 * Defined packet opcodes. The "opcode" field in the packet header may only
 * contain one of these values.
 */
typedef enum {
	kOpcodeNodeAnnouncement		= 0,
	kOpcodeServerAnnouncement	= 1,
	kOpcodeNodeAdoption			= 2,
	kOpcodeNodeStatusReq		= 3,
	kOpcodeFramebufferData		= 4,
	kOpcodeNodeConfig			= 5,
	kOpcodeSyncOutput			= 6,
	kOpcodeReadGPIO				= 7,
	kOpcodeWriteGPIO			= 8,
	kOpcodeSystemReset			= 9,
	kOpcodeSystemSleep			= 10,
	kOpcodeKeepalive			= 11,
	kOpcodeNodeReconfig			= 12,
} lichtenstein_header_opcode_t;

/**
 * Packet header structure: every packet starts with this header.
 */
typedef struct {
	uint32_t magic;
	uint32_t version;
	uint32_t checksum;

	uint16_t opcode;
	uint16_t flags;

	uint16_t sequenceIndex;
	uint16_t sequenceNumPackets;

	uint32_t txn;
	uint32_t payloadLength;
} lichtenstein_header_t;


/**
 * Node announcement packet: nodes will multicast this packet at regular
 * intervals across the network.
 */
typedef struct {
	lichtenstein_header_t header;

	uint32_t swVersion;
	uint32_t hwVersion;

	uint8_t macAddr[6];
	uint16_t port; // port on which the node's lichtenstein server listens
	uint32_t ip; // IP address on which the node's lichtenstein server listens

	uint32_t fbSize;
	uint16_t channels;

	uint16_t numGpioDigitalIn;
	uint16_t numGpioDigitalOut;
	uint16_t numGpioAnalogIn;
	uint16_t numGpioAnalogOut;

	uint16_t hostnameLen;
	char hostname[];
} lichtenstein_node_announcement_t;


/**
 * Server announcement packet: the server multicasts this packet at regular
 * intervals on the network so nodes know the server exists.
 */
typedef struct {
	lichtenstein_header_t header;

	uint32_t swVersion;

	uint32_t capabilities;

	uint32_t ip;
	uint16_t port;

	uint16_t status;

	uint16_t hostnameLen;
	char hostname[];
} lichtenstein_server_announcement_t;


/**
 * Node adoption packet: when adopting a node, the server sends this packet to
 * the node. This packet also indicates to the node how many pixels are
 * connected to each of its output channels so that it can adjust its output
 * and memory usage.
 */
typedef struct {
	lichtenstein_header_t header;

	uint32_t ip;
	uint16_t port;

	uint16_t flags;

	uint32_t numChannels;
	uint32_t pixelsPerChannel[];
} lichtenstein_node_adoption_t;


/**
 * Values possible for the "output state" field in the node status packet
 */
typedef enum {
	kOutputStateIdle			= 0,
	kOutputStateActive			= 1,
	kOutputStateConversion		= 2,
} lichtenstein_node_status_output_state_t;

/**
 * Values possible for the "medium duplex" field in the node status packet.
 */
typedef enum {
	kDuplexHalf					= 0,
	kDuplexFull					= 1
} lichtenstein_node_status_duplex_t;

/**
 * Node status request: the server may periodically poll the nodes on their
 * status, such as resource usage and other counters. This packet defines the
 * fields that the node responds with.
 */
typedef struct {
	lichtenstein_header_t header;

	uint32_t uptime;

	uint32_t totalMem;
	uint32_t freeMem;

	uint32_t rxPackets;
	uint32_t txPackets;
	uint32_t packetsWithInvalidCRC;

	uint32_t framesOutput;

	uint16_t outputState;
	uint16_t cpuUsagePercent;

	int32_t avgConversionTimeUs;

	uint32_t rxBytes;
	uint32_t txBytes;

	uint32_t rxSymbolErrors;

	uint32_t mediumSpeed;
	uint32_t mediumDuplex;
} lichtenstein_node_status_t;


/**
 * Possible values for the "data format" field of the framebuffer data packet.
 */
typedef enum {
	kDataFormatRGB				= 0,
	kDataFormatRGBW				= 1,
} lichtenstein_framebuffer_data_format_t;


/**
 * Framebuffer data packet: when new framebuffer data is available, the server
 * will send this packet to the node to fill its framebuffer with. Each channel
 * will receive a separate packet.
 *
 * @note dataElements is in terms of pixels, not bytes. If data is in the RGBW
 * format, each element is four bytes, for example.
 */
typedef struct {
	lichtenstein_header_t header;

	uint32_t destChannel;

	uint32_t dataFormat;
	uint32_t dataElements;

	char data[];
} lichtenstein_framebuffer_data_t;


/**
 * Sync output packet: When a node receives this packet, it will begin the
 * output of the previously received data. This is used to synchronize output
 * across multiple nodes, and will usually be sent multicast.
 *
 * Note that channel in this case is a bitfield: bit 0 (the least significant
 * bit) indicates channel 0, and so forth up to channel 31.
 */
typedef struct {
	lichtenstein_header_t header;

	uint32_t channel;
} lichtenstein_sync_output_t;


/**
 * Node reconfiguration: Sends a new configuration to the node. The values in
 * this packet are persisted into nonvolatile storage on the node.
 */
typedef struct {
	lichtenstein_header_t header;

	uint16_t hostnameLen;
	char hostname[];
} lichtenstein_reconfig_t;


// restore packing mode
#pragma pack(pop)

#endif
