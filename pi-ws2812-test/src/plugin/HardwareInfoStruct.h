/**
 * Information struct, as written into the EEPROMs on the hardware.
 *
 * The hardware is described in units: an unit may either provide one or more
 * outputs, inputs, or nothing at all.
 *
 * All multibyte values are stored in
 */
#ifndef HARDWARE_INFO_STRUCT_H
#define HARDWARE_INFO_STRUCT_H

#include <stdint.h>

/// current magic value
#define kHwMagicValue       0xDEADBEEF
/// current HW struct version
#define kHwVersionCurrent   0x01

/**
 * Unit
 */
typedef struct {
  // type of unit
  uint8_t type;
  // version (4 bits major and minor)
  uint8_t version;
  // revision
  uint8_t rev;

  // hardware UUID: plugins will match to this
  uint8_t uuid[16];
} __attribute__((packed)) hw_unit_t;

/**
 * Hardware description
 *
 * @note The struct is immediately followed by a CRC-16.
 */
typedef struct {
  /// magic value: should be 0xDEADBEEF
  uint32_t structMagic;
  /// struct version
  uint8_t structVersion;

  /// hardware version
  uint8_t hwVersion;
  /// hardware revision
  uint8_t hwRevision;

  /// name of this hardware, in ASCII. fill excess characters with \0
  char name[16];

  /// number of units
  uint8_t numUnits;
  /// unit data
  hw_unit_t units[];

  // not shown: CRC-16
  // uint16_t crc;
} __attribute__((packed)) hw_t;

#endif
