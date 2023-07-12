#ifndef __QTC_H__
#define __QTC_H__

#include <stdint.h>

/**
 * Compress a 1-bit raster image into a quad tree.
 *
 * @param data pointer to raster image data in row-major order. Rows should be byte-aligned
 * @param w image width
 * @param h image height
 * @param out_size size, in bytes, of compressed data
 *
 * @return pointer to compressed data upon success. NULL if unsuccessful
 */
uint8_t *qtc_encode(const uint8_t *data, uint16_t w, uint16_t h, uint32_t *out_size);

/**
 * Decompress a compressed quad tree into a 1-bit raster image.
 *
 * @param data pointer to compressed quad tree
 * @param w image width
 * @param h image height
 * @return pointer to decompressed, row-major ordered, 1-bit raster image. NULL if unsuccessful
 */
uint8_t *qtc_decode(const uint8_t *data, uint16_t w, uint16_t h);

#endif // __QTC_H__