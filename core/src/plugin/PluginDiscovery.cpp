#include "PluginDiscovery.h"

#include "LichtensteinPluginHandler.h"
#include "HardwareInfoStruct.h"

#include "../util/StringUtils.h"

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
      LOG(ERROR) << "Failed reading EEPROM at " << std::hex << addr << std::dec
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
    LOG(ERROR) << "Couldn't read ROM:" << err;
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
  // TODO: implement
  return kDiscoveryErrNoSuchDevice;
}
