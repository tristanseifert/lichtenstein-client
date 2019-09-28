// //////////////////////////////////////////////////////////
// Crc32.h
// Copyright (c) 2011-2016 Stephan Brumme. All rights reserved.
// Slicing-by-16 contributed by Bulat Ziganshin
// Tableless bytewise CRC contributed by Hagai Gold
// see http://create.stephan-brumme.com/disclaimer.html
//

// uint8_t, uint32_t, int32_t
#include <stdint.h>
// size_t
#include <stddef.h>

// crc32_fast selects the fastest algorithm depending on flags (CRC32_USE_LOOKUP_...)
/// compute CRC32 using the fastest algorithm for large datasets on modern CPUs
uint32_t crc32_fast(const void* data, size_t length, uint32_t previousCrc32 = 0);

/// compute CRC32 (bitwise algorithm)
uint32_t crc32_bitwise (const void* data, size_t length, uint32_t previousCrc32 = 0);

// single byte algorithm
uint32_t crc32_1byte(const void* data, size_t length, uint32_t previousCrc32);

/// compute CRC32 (Slicing-by-16 algorithm)
uint32_t crc32_16bytes (const void* data, size_t length, uint32_t previousCrc32 = 0);
/// compute CRC32 (Slicing-by-16 algorithm, prefetch upcoming data blocks)
uint32_t crc32_16bytes_prefetch(const void* data, size_t length, uint32_t previousCrc32 = 0, size_t prefetchAhead = 256);
