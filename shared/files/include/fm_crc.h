/**
 * @file
 * @brief CRC-32 (cyclic redundancy check), the sum ZIP writes and the
 *        Wi-Fi page's file API answers as a file's ETag: polynomial
 *        0x04C11DB7 reflected (0xEDB88320), start and end inverted.
 *
 * One function for the three users: the page's ZIP archive and its ETag
 * (wa_zip.h wa_crc32()), and FILES' editor, which compares the file on
 * the card with the one it opened before SAVE writes over it
 * (fm_ops.h fm_job_match()).  The table is constant, so tasks that sum
 * at the same time share nothing they write.
 *
 * Pure C: no card access, no heap.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The CRC-32 of @p n more bytes after @p crc; 0 before the first.
 *        "123456789" sums to 0xCBF43926.
 * @param crc   the sum so far, 0 at the start
 * @param data  the bytes; may be NULL only with @p n 0
 * @param n     how many
 * @return the sum with them
 */
uint32_t fm_crc32(uint32_t crc, const void *data, size_t n);

#ifdef __cplusplus
}
#endif
