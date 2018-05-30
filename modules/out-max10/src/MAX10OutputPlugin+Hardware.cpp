/**
 * Hardware interface routines for the MAX10 output plugin, such as resetting
 * the peripheral or sending/receiving commands.
 */
#include "MAX10OutputPlugin.h"

#include "GPIOHelper.h"

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
 * Configures the SPI bus and output GPIOs.
 */
void MAX10OutputPlugin::configureHardware(void) {
	int err;

	INIReader *config = this->handler->getConfig();

	// Get GPIO for reset pin and set it up
	this->resetGPIO = config->GetInteger("output_max10", "gpio_reset", -1);
	CHECK(this->resetGPIO > 0) << "Invalid reset GPIO value: " << this->resetGPIO;

	err = GPIOHelper::exportGPIO(this->resetGPIO);
	CHECK(err == 0) << "Couldn't export reset GPIO: " << err;

	err = GPIOHelper::configureGPIO(this->resetGPIO, "direction", "high");
	CHECK(err == 0) << "Couldn't configure reset GPIO: " << err;

	// Get GPIO for enable pin
	this->enableGPIO = config->GetInteger("output_max10", "gpio_enable", -1);
	CHECK(this->enableGPIO > 0) << "Invalid enable GPIO value: " << this->enableGPIO;

	err = GPIOHelper::exportGPIO(this->enableGPIO);
	CHECK(err == 0) << "Couldn't export enable GPIO: " << err;

	err = GPIOHelper::configureGPIO(this->enableGPIO, "direction", "high");
	CHECK(err == 0) << "Couldn't configure enable GPIO: " << err;


	// TODO: read from EEPROM data
	this->spiBaud = config->GetInteger("output_max10", "baud", 2500000);

	this->spiDeviceFile = config->Get("output_max10", "device", "");
	CHECK(this->spiDeviceFile != "") << "Invalid device file: " << this->spiDeviceFile;

	// get EEPROM address
	this->i2cEeepromAddr = config->GetInteger("output_max10", "eeprom", 0x40);
	CHECK(this->i2cEeepromAddr >= 0) << "Invalid EEPROM address " << this->i2cEeepromAddr;

	// open SPI device
#ifdef __linux__
	const char *file = this->spiDeviceFile.c_str();

	this->spiDevice = open(file, O_RDWR);
	PLOG_IF(FATAL, this->spiDevice == -1) << "Couldn't open SPI device at " << file;
#endif


#ifdef __linux__
	// configure SPI mode (CPHA)
	const uint8_t mode = SPI_CPHA;

	err = ioctl(this->spiDevice, SPI_IOC_WR_MODE, &mode);
	PLOG_IF(FATAL, err == -1) << "Couldn't set write mode";

	err = ioctl(this->spiDevice, SPI_IOC_RD_MODE, &mode);
	PLOG_IF(FATAL, err == -1) << "Couldn't set read mode";

	// bits per word (8)
	const uint8_t bits = 8;

	err = ioctl(this->spiDevice, SPI_IOC_WR_BITS_PER_WORD, &bits);
	PLOG_IF(FATAL, err == -1) << "Couldn't set write word length";

	err = ioctl(this->spiDevice, SPI_IOC_RD_BITS_PER_WORD, &bits);
	PLOG_IF(FATAL, err == -1) << "Couldn't set read word length";

	// configure maximum speed
	uint32_t speed = this->spiBaud;

	err = ioctl(this->spiDevice, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
	PLOG_IF(FATAL, err == -1) << "Couldn't set write max speed";

	err = ioctl(this->spiDevice, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
	PLOG_IF(FATAL, err == -1) << "Couldn't set read max speed";
#endif
}

/**
 * Cleanly resets the hardware into the default state.
 */
void MAX10OutputPlugin::cleanUpHardware(void) {
	int err;

	// close the SPI device
	if(this->spiDevice != -1) {
		err = close(this->spiDevice);
		PLOG_IF(ERROR, err < 0) << "Couldn't close SPI device: " << err;
	}

	// Assert reset again
	this->reset();

	// Un-export the GPIOs
	err = GPIOHelper::unExportGPIO(this->resetGPIO);
	CHECK(err == 0) << "Couldn't unexport reset GPIO: " << err;

	err = GPIOHelper::unExportGPIO(this->enableGPIO);
	CHECK(err == 0) << "Couldn't unexport enable GPIO: " << err;
}



 /**
  * Resets the output chip.
  *
  * I honestly have no clue how long reset needs to be asserted for, but 10ms
  * should be long enough.
  */
void MAX10OutputPlugin::reset(void) {
	int err;

	// pull the reset line low
	err = GPIOHelper::writeGPIO(this->resetGPIO, false);
	CHECK(err == 0) << "Couldn't assert reset: " << err;

	// wait for 10ms
	usleep((1000 * 10));

	// pull the reset line back high
	err = GPIOHelper::writeGPIO(this->resetGPIO, true);
	CHECK(err == 0) << "Couldn't deassert reset: " << err;
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
	struct spi_ioc_transfer txn;
	memset(&txn, 0, sizeof(txn));

	txn.tx_buf = (unsigned long) write;
	txn.rx_buf = (unsigned long) read;

	txn.len = static_cast<uint32_t>(length);

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
	int err = -1;

	// buffer for command
	uint8_t cmdBuf = command;

#ifdef __linux__
	int i = 0;

	// allocate SPI structs
	struct spi_ioc_transfer txn[4];
	memset(&txn, 0, sizeof(txn));

	// set up the command to write the command
	txn[i].tx_buf = (unsigned long) &cmdBuf;
	txn[i].rx_buf = (unsigned long) nullptr;
	txn[i].len = 1;
	txn[i].cs_change = false;

	i++;

	// is there a header?
	if(header && headerLen > 0) {
		txn[i].tx_buf = (unsigned long) header;
		txn[i].rx_buf = (unsigned long) nullptr;
		txn[i].len = static_cast<uint32_t>(headerLen);
		txn[i].cs_change = false;

		i++;
	}

	// is there data to read/write?
	if((read || write) && length > 0) {
		txn[i].rx_buf = (unsigned long) read;
		txn[i].tx_buf = (unsigned long) write;

		txn[i].len = static_cast<uint32_t>(length);

		txn[i].cs_change = false;

		i++;
	}

	// ensure we de-select the device after the last transfer
	txn[(i - 1)].cs_change = true;

	// perform transfer
	err = ioctl(this->spiDevice, SPI_IOC_MESSAGE(i), &txn);
	PLOG_IF(ERROR, err < 0) << "Couldn't do SPI transfers";
#else
	// on development platforms (that aren't linux) just pretend success happened
	err = 1 + headerLen + length;
#endif

	// done!
	return err;
}



/**
 * Reads the status register to determine which channels are actively sending
 * data.
 *
 * @return 0 if successful, a negative error code otherwise.
 */
int MAX10OutputPlugin::readStatusReg(std::bitset<16> &status) {
	int err;

	// read the register
	uint16_t read = 0;

	err = this->sendSpiCommand(kCommandReadStatus, &read, nullptr, 2);

	if(err > 0) {
		// clear status
		status.reset();

		// interpret it from the bitfield
		status = std::bitset<16>(read);
	}

	// return the error code, or 0 if successful
	return (err < 0) ? err : 0;
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

	LOG_IF(ERROR, err <= 0) << "Couldn't write to memory 0x" << std::hex << addr
		<< ", length 0x" << length << std::dec << ": " << err;

	// return error codes as is, subtract length of header and command otherwise
	return (err < 0) ? err : (err - 4);;
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
	err =  this->sendSpiCommand(kCommandWriteReg, &header, headerSz, nullptr, &data, dataSz);

	LOG_IF(ERROR, err <= 0) << "Couldn't write addr 0x" << std::hex << addr
		<< ", length 0x" << length << " for channel " << std::dec << channel
		<< ": " << err;

	// return error code or 0 if successful
	return (err < 0) ? err : 0;
}
