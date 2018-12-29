#include "PluginDiscovery.h"

#include "LichtensteinPluginHandler.h"
#include "HardwareInfoStruct.h"

#include "../util/StringUtils.h"

#include <unistd.h>
#include <fcntl.h>

// includes for i2c
#ifdef __linux__
  #include <linux/i2c-dev.h>
  #include <i2c/smbus.h>
#endif

#include <glog/logging.h>

/**
 * Initializes the plugin discovery system.
 */
PluginDiscovery::PluginDiscovery(LichtensteinPluginHandler *_handler) : handler(_handler) {
  this->config = this->handler->getConfig();
}

/**
 * Cleans up any allocated resources.
 */
PluginDiscovery::~PluginDiscovery() {

}



/**
 * Attempts to discover hardware. All EEPROMs in the specified range are read,
 * and their info structs parsed.
 *
 * If the first I2C read times out, it's assumed no chips are at that address;
 * any subsequent timeouts are considered errors.
 */
int PluginDiscovery::discover(void) {
  int err = 0, numAddresses = 0;

  // get a vector of all addresses
	std::string addrStr = this->config->Get("discovery", "eeprom_addresses", "0x50,0x51,0x52,0x53");

  std::vector<int> addr;
  numAddresses = StringUtils::parseCsvList(addrStr, addr, 16);

  LOG(INFO) << "Checking " << numAddresses << " addresses";

  // read each memory
  for(auto it = addr.begin(); it != addr.end(); it++) {
    int addr = *it;

    VLOG(1) << "Reading EEPROM at 0x" << std::hex << addr;

    err = this->discoverAtAddress(addr);

    // handle errors
    if(err < 0) {
      LOG(ERROR) << "Failed reading EEPROM at 0x" << std::hex << addr << std::dec
        << ": " << err << std::endl;

      // errors other than a nonexistent device are fatal
      if(err != kDiscoveryErrNoSuchDevice) {
        return err;
      }
    }
  }

  // return error status
  return err;
}

/**
 * Reads the memory at the given I2C location, and attempts to match plugins to
 * the input and output channels defined by the plugin.
 */
int PluginDiscovery::discoverAtAddress(int addr) {
  int err;

  void *romData = nullptr;
  size_t romDataLen = 0;

  // read the ROM
  err = this->readConfigRom(addr, &romData, &romDataLen);

  if(err < 0) {
    LOG(ERROR) << "Couldn't read ROM: " << err;
    return err;
  }

  // TODO: implement
  return 0;
}

/**
 * Reads the config ROM into memory, then provides the caller the address and
 * length of the data.
 *
 * @note Release the buffer with free() when done.
 */
int PluginDiscovery::readConfigRom(int addr, void **data, size_t *len) {
  int err = 0, fd = -1;
  void *readBuf = nullptr;

  // validate arguments
  CHECK(data != nullptr) << "data out pointer may not be null";
  CHECK(len != nullptr) << "length out pointer may not be null";

  CHECK(addr > 0) << "I2C address may not be negative";
  CHECK(addr < 0x80) << "I2C address may not be more than 0x7F";

  // get bus number from config
  int busNr = this->config->GetInteger("discovery", "eeprom_bus", 0);

  CHECK(busNr >= 0) << "Address is negative, wtf (got " << busNr << ", check discovery.eeprom_bus)";

  // create bus name
  const size_t busPathSz = 32 + 1;
  char busPath[busPathSz];

  memset(&busPath, 0, busPathSz);

  snprintf(busPath, (busPathSz - 1), "/dev/i2c-%d", busNr);

  // open i2c device
  fd = open(busPath, O_RDWR);
  if(fd < 0) {
    err = kDiscoveryErrIo;

    PLOG(ERROR) << "Couldn't open I2C bus at " << busPath;
    goto beach;
  }

  // send slave address
#ifdef __linux__
  err = ioctl(fd, I2C_SLAVE, addr);

  if(err < 0) {
    err = kDiscoveryErrIoctl;

    PLOG(ERROR) << "Couldn't set I2C slave address";
    goto beach;
  }
#endif

  // allocate buffer and clear it (fixed size for now); then set its location
  static const size_t readBufLen = 256;

  readBuf = malloc(readBufLen);
  memset(readBuf, 0, readBufLen);

  *data = readBuf;

#ifdef __linux__
  // write buffer for read command
  static const char readCmdBuf[1] = { 0x00 };

  // build i2c txn: random read starting at 0x00, 256 bytes.
  static const size_t txMsgsCount = 2;

  struct i2c_msg txMsgs[txMsgsCount] = {
    {
      .addr = addr,

      .buf = readCmdBuf,
      .len = sizeof(readCmdBuf),

      .flags = I2C_M_RD,
    },
    {
      .addr = addr,

      .buf = readBuf,
      .len = readBufLen,

      .flags = I2C_M_RD,
    }
  };

  struct i2c_rdwr_ioctl_data txn = {
    .msgs = txnMsgs,
    .nMsgs = txMsgsCount
  };


  err = ioctl(fd, I2C_RDWR, &txn);

  if(err < 0) {
    err = kDiscoveryErrNoSuchDevice;

    PLOG(ERROR) << "Couldn't read from EEPROM (assuming no such device)";
    goto beach;
  }
#endif

  // success, write the output buffer location
  *len = readBufLen;
  return 0;

  // close i2c device
beach: ;
if(fd != -1) {
  close(fd);
}

  return err;
}
