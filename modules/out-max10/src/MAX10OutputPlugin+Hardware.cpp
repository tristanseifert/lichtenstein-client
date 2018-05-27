/**
 * Hardware interface routines for the MAX10 output plugin, such as resetting
 * the peripheral or sending/receiving commands.
 */
#include "MAX10OutputPlugin.h"

#include <glog/logging.h>

#include <bitset>
#include <cstdint>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/ioctl.h>

// SPI stuff
#ifdef __linux__
	#include <linux/types.h>
	#include <linux/spi/spidev.h>
#else
	// lmao
	struct spi_ioc_transfer {
		uint64_t	tx_buf;
		uint64_t	rx_buf;

		uint32_t	len;
		uint32_t	speed_hz;

		uint16_t	delay_usecs;
		uint8_t		bits_per_word;
		uint8_t		cs_change;
		uint32_t	pad;
	};
#endif



 /**
  * Resets the output chip.
  */
void MAX10OutputPlugin::reset(void) {
	// TODO: implement
}

 /**
  * Does an SPI transaction, reading/writing `length` bytes into the specified
  * buffers.
  *
  * @return 0 if successful, a negative error code otherwise.
  */
int MAX10OutputPlugin::doSpiTransaction(void *read, void *write, size_t length) {
	int err;

#ifdef __linux__
	// set up the SPI struct
	struct spi_ioc_transfer txn = {
		.tx_buf = (unsigned long) write,
		.rx_buf = (unsigned long) read,

		.len = static_cast<uint32_t>(length),
		.speed_hz = this->spiBaud,

		.delay_usecs = 0,
		.bits_per_word = 8,
		.cs_change = true,
	};

	// perform transfer
	err = ioctl(this->spiDevice, SPI_IOC_MESSAGE(1), &txn);
	PLOG_IF(ERROR, err < 0) << "Couldn't do SPI transfer";
#endif

	// done!
	return err;
}

 /**
  * Sends the given single-byte command, followed by an (optional) header, then
  * reads/writes the given number of bytes
  */
int MAX10OutputPlugin::sendSpiCommand(uint8_t command, void *header, size_t headerLen, void *read, void *write, size_t length) {
	int err;

	// buffer for command
	uint8_t cmdBuf = command;

#ifdef __linux__
	int i = 0;

	// allocate SPI structs
	struct spi_ioc_transfer txn[4];
	memset(&txn, 0, sizeof(txn));

	// set up the command to write the command
	txn[i++] = {
		.tx_buf = (unsigned long) nullptr,
		.rx_buf = (unsigned long) &cmdBuf,

		.len = 1,
		.speed_hz = this->spiBaud,

		.delay_usecs = 0,
		.bits_per_word = 8,
		.cs_change = false,
	};

	// is there a header?
	if(header && headerLen > 0) {
		txn[i++] = {
			.tx_buf = (unsigned long) header,
			.rx_buf = (unsigned long) nullptr,

			.len = static_cast<uint32_t>(headerLen),
			.speed_hz = this->spiBaud,

			.delay_usecs = 0,
			.bits_per_word = 8,
			.cs_change = false,
		};
	}

	// is there data to read/write?
	if((read | write) && length > 0) {
		txn[i++] = {
			.tx_buf = (unsigned long) read,
			.rx_buf = (unsigned long) write,

			.len = static_cast<uint32_t>(length),
			.speed_hz = this->spiBaud,

			.delay_usecs = 0,
			.bits_per_word = 8,
			.cs_change = false,
		};
	}

	// ensure we de-select the device after the last transfer
	txn[(i - 1)].cs_change = true;

	// perform transfer
	err = ioctl(this->spiDevice, SPI_IOC_MESSAGE(i), &txn);
	PLOG_IF(ERROR, err < 0) << "Couldn't do SPI transfers";
#endif

	// done!
	return err;
}



/**
 * Reads the status register to determine which channels are actively sending
 * data.
 */
int MAX10OutputPlugin::readStatusReg(std::bitset<16> &status) {
	int err;

	// read the register
	uint16_t read = 0;

	err = this->sendSpiCommand(kCommandReadStatus, &read, nullptr, 2);

	if(err == 0) {
		// clear status
		status.reset();

		// interpret it from the bitfield
		status = std::bitset<16>(read);
	}

	// return the error code
	return err;
}

/**
 * Writes the given data into the peripheral's memory.
 *
 * @return The number of bytes written if successful, a negative error code
 * otherwise.
 */
int MAX10OutputPlugin::writePeriphMem(uint32_t addr, void *data, size_t length) {
	int err;

	// construct the write header
	const size_t headerSz = 3;
	uint8_t header[headerSz];

	memset(&header, 0, headerSz);

	// put the address in it, in big endian
	header[0] = (addr & 0xFF0000) >> 16;
	header[1] = (addr & 0x00FF00) >> 8;
	header[2] = (addr & 0x0000FF) >> 0;

	// perform the SPI command
	err = this->sendSpiCommand(kCommandWriteMem, &header, headerSz, nullptr, data, length);

	LOG_IF(ERROR, err != 0) << "Couldn't write to memory 0x" << std::hex << addr
		<< ", length 0x" << ": " << err;
	return err;
}

/**
 * Writes the address and length into the given channel's output registers; this
 * will immediately start outputting data for that channel.
 *
 * @return 0 if successful, negative error code otherwise.
 */
int MAX10OutputPlugin::writePeriphReg(unsigned int channel, uint32_t addr, uint16_t length) {
	int err;

	// construct the header (channel number)
	const size_t headerSz = 1;
	uint8_t header[headerSz];

	memset(&header, 0, headerSz);

	// fill the channel number in the header
	header[0] = (channel & 0xFF);


	// construct the register values (3 byte address, 2 byte length)
	const size_t dataSz = 5;
	uint8_t data[dataSz];

	memset(&data, 0, dataSz);

	// fill the read start address
	data[0] = (addr & 0xFF0000) >> 16;
	data[1] = (addr & 0x00FF00) >> 8;
	data[2] = (addr & 0x0000FF) >> 0;

	// fill the read length
	data[3] = (length & 0xFF00) >> 8;
	data[4] = (length & 0x00FF) >> 0;


	// perform the SPI command
	err =  this->sendSpiCommand(kCommandWriteMem, &header, headerSz, nullptr, &data, dataSz);

	LOG_IF(ERROR, err != 0) << "Couldn't write addr 0x" << std::hex << addr
		<< ", length 0x" << length << " for channel " << std::dec << channel
		<< ": " << err;
	return err;
}
