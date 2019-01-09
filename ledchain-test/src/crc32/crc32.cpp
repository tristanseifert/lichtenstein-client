// //////////////////////////////////////////////////////////
// Crc32.cpp
// Copyright (c) 2011-2016 Stephan Brumme. All rights reserved.
// Slicing-by-16 contributed by Bulat Ziganshin
// Tableless bytewise CRC contributed by Hagai Gold
// see http://create.stephan-brumme.com/disclaimer.html
//

// this code has been modified from the original by removing all CRC routines
// besides crc32_16bytes.


#include "crc32.h"

// define endianess and some integer data types
#if defined(_MSC_VER) || defined(__MINGW32__)
  #define __LITTLE_ENDIAN 1234
  #define __BIG_ENDIAN    4321
  #define __BYTE_ORDER    __LITTLE_ENDIAN

  #include <xmmintrin.h>
  #ifdef __MINGW32__
    #define PREFETCH(location) __builtin_prefetch(location)
  #else
    #define PREFETCH(location) _mm_prefetch(location, _MM_HINT_T0)
  #endif
#else
  // defines __BYTE_ORDER as __LITTLE_ENDIAN or __BIG_ENDIAN
  #include <sys/param.h>

  #ifdef __GNUC__
    #define PREFETCH(location) __builtin_prefetch(location)
  #else
    #define PREFETCH(location) ;
  #endif
#endif


/// zlib's CRC32 polynomial
const uint32_t Polynomial = 0x04C11DB7;

/// swap endianess
static inline uint32_t swap(uint32_t x)
{
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_bswap32(x);
#else
  return (x >> 24) |
        ((x >>  8) & 0x0000FF00) |
        ((x <<  8) & 0x00FF0000) |
         (x << 24);
#endif
}

const size_t MaxSlice = 16;

/// forward declaration, table is at the end of this file
extern const uint32_t Crc32Lookup[MaxSlice][256]; // extern is needed to keep compiler happy

/// compute CRC32 (bitwise algorithm)
uint32_t crc32_bitwise(const void* data, size_t length, uint32_t previousCrc32) {
  uint32_t crc = ~previousCrc32; // same as previousCrc32 ^ 0xFFFFFFFF
  const uint8_t* current = (const uint8_t*) data;

  while (length-- != 0)
  {
    crc ^= *current++;

    for (int j = 0; j < 8; j++)
    {
      // branch-free
      crc = (crc >> 1) ^ (-int32_t(crc & 1) & Polynomial);

      // branching, much slower:
      //if (crc & 1)
      //  crc = (crc >> 1) ^ Polynomial;
      //else
      //  crc =  crc >> 1;
    }
  }

  return ~crc; // same as crc ^ 0xFFFFFFFF
}

/// compute CRC32 (Slicing-by-16 algorithm)
uint32_t crc32_16bytes(const void* data, size_t length, uint32_t previousCrc32)
{
  uint32_t crc = ~previousCrc32; // same as previousCrc32 ^ 0xFFFFFFFF
  const uint32_t* current = (const uint32_t*) data;

  // enabling optimization (at least -O2) automatically unrolls the inner for-loop
  const size_t Unroll = 4;
  const size_t BytesAtOnce = 16 * Unroll;

  while (length >= BytesAtOnce)
  {
    for (size_t unrolling = 0; unrolling < Unroll; unrolling++)
    {
#if __BYTE_ORDER == __BIG_ENDIAN
    uint32_t one   = *current++ ^ swap(crc);
    uint32_t two   = *current++;
    uint32_t three = *current++;
    uint32_t four  = *current++;
    crc  = Crc32Lookup[ 0][ four         & 0xFF] ^
           Crc32Lookup[ 1][(four  >>  8) & 0xFF] ^
           Crc32Lookup[ 2][(four  >> 16) & 0xFF] ^
           Crc32Lookup[ 3][(four  >> 24) & 0xFF] ^
           Crc32Lookup[ 4][ three        & 0xFF] ^
           Crc32Lookup[ 5][(three >>  8) & 0xFF] ^
           Crc32Lookup[ 6][(three >> 16) & 0xFF] ^
           Crc32Lookup[ 7][(three >> 24) & 0xFF] ^
           Crc32Lookup[ 8][ two          & 0xFF] ^
           Crc32Lookup[ 9][(two   >>  8) & 0xFF] ^
           Crc32Lookup[10][(two   >> 16) & 0xFF] ^
           Crc32Lookup[11][(two   >> 24) & 0xFF] ^
           Crc32Lookup[12][ one          & 0xFF] ^
           Crc32Lookup[13][(one   >>  8) & 0xFF] ^
           Crc32Lookup[14][(one   >> 16) & 0xFF] ^
           Crc32Lookup[15][(one   >> 24) & 0xFF];
#else
    uint32_t one   = *current++ ^ crc;
    uint32_t two   = *current++;
    uint32_t three = *current++;
    uint32_t four  = *current++;
    crc  = Crc32Lookup[ 0][(four  >> 24) & 0xFF] ^
           Crc32Lookup[ 1][(four  >> 16) & 0xFF] ^
           Crc32Lookup[ 2][(four  >>  8) & 0xFF] ^
           Crc32Lookup[ 3][ four         & 0xFF] ^
           Crc32Lookup[ 4][(three >> 24) & 0xFF] ^
           Crc32Lookup[ 5][(three >> 16) & 0xFF] ^
           Crc32Lookup[ 6][(three >>  8) & 0xFF] ^
           Crc32Lookup[ 7][ three        & 0xFF] ^
           Crc32Lookup[ 8][(two   >> 24) & 0xFF] ^
           Crc32Lookup[ 9][(two   >> 16) & 0xFF] ^
           Crc32Lookup[10][(two   >>  8) & 0xFF] ^
           Crc32Lookup[11][ two          & 0xFF] ^
           Crc32Lookup[12][(one   >> 24) & 0xFF] ^
           Crc32Lookup[13][(one   >> 16) & 0xFF] ^
           Crc32Lookup[14][(one   >>  8) & 0xFF] ^
           Crc32Lookup[15][ one          & 0xFF];
#endif
    }

    length -= BytesAtOnce;
  }

  const uint8_t* currentChar = (const uint8_t*) current;
  // remaining 1 to 63 bytes (standard algorithm)
  while (length-- != 0)
    crc = (crc >> 8) ^ Crc32Lookup[0][(crc & 0xFF) ^ *currentChar++];

  return ~crc; // same as crc ^ 0xFFFFFFFF
}


/// compute CRC32 (Slicing-by-16 algorithm, prefetch upcoming data blocks)
uint32_t crc32_16bytes_prefetch(const void* data, size_t length, uint32_t previousCrc32, size_t prefetchAhead)
{
  // CRC code is identical to crc32_16bytes (including unrolling), only added prefetching
  // 256 bytes look-ahead seems to be the sweet spot on Core i7 CPUs

  uint32_t crc = ~previousCrc32; // same as previousCrc32 ^ 0xFFFFFFFF
  const uint32_t* current = (const uint32_t*) data;

  // enabling optimization (at least -O2) automatically unrolls the for-loop
  const size_t Unroll = 4;
  const size_t BytesAtOnce = 16 * Unroll;

  while (length >= BytesAtOnce + prefetchAhead)
  {
    PREFETCH(((const char*) current) + prefetchAhead);

    for (size_t unrolling = 0; unrolling < Unroll; unrolling++)
    {
#if __BYTE_ORDER == __BIG_ENDIAN
    uint32_t one   = *current++ ^ swap(crc);
    uint32_t two   = *current++;
    uint32_t three = *current++;
    uint32_t four  = *current++;
    crc  = Crc32Lookup[ 0][ four         & 0xFF] ^
           Crc32Lookup[ 1][(four  >>  8) & 0xFF] ^
           Crc32Lookup[ 2][(four  >> 16) & 0xFF] ^
           Crc32Lookup[ 3][(four  >> 24) & 0xFF] ^
           Crc32Lookup[ 4][ three        & 0xFF] ^
           Crc32Lookup[ 5][(three >>  8) & 0xFF] ^
           Crc32Lookup[ 6][(three >> 16) & 0xFF] ^
           Crc32Lookup[ 7][(three >> 24) & 0xFF] ^
           Crc32Lookup[ 8][ two          & 0xFF] ^
           Crc32Lookup[ 9][(two   >>  8) & 0xFF] ^
           Crc32Lookup[10][(two   >> 16) & 0xFF] ^
           Crc32Lookup[11][(two   >> 24) & 0xFF] ^
           Crc32Lookup[12][ one          & 0xFF] ^
           Crc32Lookup[13][(one   >>  8) & 0xFF] ^
           Crc32Lookup[14][(one   >> 16) & 0xFF] ^
           Crc32Lookup[15][(one   >> 24) & 0xFF];
#else
    uint32_t one   = *current++ ^ crc;
    uint32_t two   = *current++;
    uint32_t three = *current++;
    uint32_t four  = *current++;
    crc  = Crc32Lookup[ 0][(four  >> 24) & 0xFF] ^
           Crc32Lookup[ 1][(four  >> 16) & 0xFF] ^
           Crc32Lookup[ 2][(four  >>  8) & 0xFF] ^
           Crc32Lookup[ 3][ four         & 0xFF] ^
           Crc32Lookup[ 4][(three >> 24) & 0xFF] ^
           Crc32Lookup[ 5][(three >> 16) & 0xFF] ^
           Crc32Lookup[ 6][(three >>  8) & 0xFF] ^
           Crc32Lookup[ 7][ three        & 0xFF] ^
           Crc32Lookup[ 8][(two   >> 24) & 0xFF] ^
           Crc32Lookup[ 9][(two   >> 16) & 0xFF] ^
           Crc32Lookup[10][(two   >>  8) & 0xFF] ^
           Crc32Lookup[11][ two          & 0xFF] ^
           Crc32Lookup[12][(one   >> 24) & 0xFF] ^
           Crc32Lookup[13][(one   >> 16) & 0xFF] ^
           Crc32Lookup[14][(one   >>  8) & 0xFF] ^
           Crc32Lookup[15][ one          & 0xFF];
#endif
    }

    length -= BytesAtOnce;
  }

  const uint8_t* currentChar = (const uint8_t*) current;
  // remaining 1 to 63 bytes (standard algorithm)
  while (length-- != 0)
    crc = (crc >> 8) ^ Crc32Lookup[0][(crc & 0xFF) ^ *currentChar++];

  return ~crc; // same as crc ^ 0xFFFFFFFF
}

/// compute CRC32 (standard algorithm)
uint32_t crc32_1byte(const void* data, size_t length, uint32_t previousCrc32)
{
  uint32_t crc = ~previousCrc32; // same as previousCrc32 ^ 0xFFFFFFFF
  const uint8_t* current = (const uint8_t*) data;

  while (length-- != 0)
    crc = (crc >> 8) ^ Crc32Lookup[0][(crc & 0xFF) ^ *current++];

  return ~crc; // same as crc ^ 0xFFFFFFFF
}


/// compute CRC32 using the fastest algorithm for large datasets on modern CPUs
uint32_t crc32_fast(const void* data, size_t length, uint32_t previousCrc32) {
	// return crc32_16bytes(data, length, previousCrc32);
	// return crc32_16bytes_prefetch(data, length, previousCrc32);
	return crc32_1byte(data, length, previousCrc32);
}


// //////////////////////////////////////////////////////////
// constants

#include "crclut.h"
