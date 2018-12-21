/**
 * Allows discovery of plugins by reading an information struct from EEPROMs
 * at I2C addresses 0x50 - 0x57.
 *
 * It is assumed these chips are AT24C02 or a compatible chip.
 */
#ifndef PLUGIN_DISCOVERY_H
#define PLUGIN_DISCOVERY_H

#include <INIReader.h>

class LichtensteinPluginHandler;

class PluginDiscovery {
  public:
    PluginDiscovery(LichtensteinPluginHandler *handler);
    ~PluginDiscovery();

  public:
    int discover(void);

  private:
    int discoverAtAddress(int addr);

    int readConfigRom(int addr, void **data, size_t *len);

  public:
    /// error codes for discovery
    enum {
      kDiscoveryErrNoSuchDevice    = -42000,
      kDiscoveryErrRead            = -42001,
      kDiscoveryErrVersion        = -42002,
    };

  private:
		INIReader *config = nullptr;
    LichtensteinPluginHandler *handler = nullptr;
};

#endif
