/**
 * @file
 * @brief Supvan print protocol pages: the print buffer, the data frame
 *        and the speed byte (supvan.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "supvan.h"

/** @brief The checksum reads back the byte before each 256-byte step. */
#define CHK_STRIDE 256u
/** @brief Byte 3 of a print buffer: density << 2, material type 1 << 6. */
#define PAGE_BYTE1 ((uint8_t)((SV_DENSITY << 2) | (1u << 6)))

uint16_t sv_buf_count(uint16_t cols)
{
    return (uint16_t)((cols + SV_BUF_COLS - 1u) / SV_BUF_COLS);
}

bool sv_buf_fill(uint8_t out[SV_BUF_SIZE], const uint8_t *bits,
                 uint16_t cols, uint16_t index)
{
    const uint16_t count = sv_buf_count(cols);
    if (out == NULL || bits == NULL || index >= count) {
        return false;
    }
    const uint32_t first = (uint32_t)index * SV_BUF_COLS;
    uint32_t n = (uint32_t)cols - first;
    if (n > SV_BUF_COLS) {
        n = SV_BUF_COLS;
    }
    memset(out, 0, SV_BUF_SIZE);
    uint8_t page = 0u;
    if (index == 0u) {
        page |= SV_PAGE_START;
    }
    if (index + 1u == count) {
        page |= SV_PAGE_END | SV_PRINT_END;
    }
    out[2] = page;
    out[3] = PAGE_BYTE1;
    out[4] = (uint8_t)n;
    out[5] = (uint8_t)(n >> 8);
    out[6] = SV_LINE_BYTES;
    out[8] = SV_MARGIN;
    out[10] = SV_MARGIN;
    out[12] = SV_DENSITY;
    const uint32_t data = n * SV_LINE_BYTES;
    memcpy(out + SV_BUF_HEAD, bits + (size_t)first * SV_LINE_BYTES, data);
    uint16_t sum = 0u;
    for (unsigned i = 2u; i < SV_BUF_HEAD; ++i) {
        sum = (uint16_t)(sum + out[i]);
    }
    const uint32_t end = SV_BUF_HEAD + data;
    for (uint32_t i = CHK_STRIDE; i <= end; i += CHK_STRIDE) {
        sum = (uint16_t)(sum + out[i - 1u]);
    }
    out[0] = (uint8_t)sum;
    out[1] = (uint8_t)(sum >> 8);
    return true;
}

uint16_t sv_chunk_count(uint32_t len)
{
    const uint32_t count = (len + SV_CHUNK - 1u) / SV_CHUNK;
    return (count > SV_FRAMES_MAX) ? 0u : (uint16_t)count;
}

bool sv_chunk_build(uint8_t out[SV_FRAME_LEN], const uint8_t *stream,
                    uint32_t len, uint8_t index)
{
    const uint16_t count = sv_chunk_count(len);
    if (out == NULL || stream == NULL || index >= count) {
        return false;
    }
    static const uint8_t head[6] = { SV_MAGIC0, SV_MAGIC1, 0xFCu, 0x01u,
                                     0x10u, 0x02u };
    memset(out, 0, SV_FRAME_LEN);
    memcpy(out, head, sizeof head);
    uint8_t *pkt = out + sizeof head;
    pkt[0] = 0xAAu;
    pkt[1] = 0xBBu;
    pkt[4] = index;
    pkt[5] = (uint8_t)count;
    const uint32_t from = (uint32_t)index * SV_CHUNK;
    uint32_t n = len - from;
    if (n > SV_CHUNK) {
        n = SV_CHUNK;
    }
    memcpy(pkt + 6, stream + from, n);
    uint16_t sum = 0u;
    for (unsigned i = 4u; i < SV_FRAME_LEN - sizeof head; ++i) {
        sum = (uint16_t)(sum + pkt[i]);
    }
    pkt[2] = (uint8_t)sum;
    pkt[3] = (uint8_t)(sum >> 8);
    return true;
}

uint8_t sv_speed(uint32_t mean)
{
    static const struct {
        uint32_t above;
        uint8_t speed;
    } k[] = { { 3000u, 10u }, { 2800u, 15u }, { 2500u, 20u },
              { 2000u, 25u }, { 1500u, 40u }, { 1000u, 45u },
              { 500u, 55u } };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        if (mean > k[i].above) {
            return k[i].speed;
        }
    }
    return 60u;
}
