/*
 * EX Bus packets as the master builds and reads them.  See exbus_wire.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "exbus_wire.h"

#include <string.h>

uint16_t exbus_crc16_update(uint16_t crc, uint8_t byte)
{
    /* The specification's update step: the reflected CRC-16 with the
     * polynomial 0x1021, one byte at a time without a table. */
    uint8_t d = (uint8_t)(byte ^ (uint8_t)(crc & 0xFFu));

    d = (uint8_t)(d ^ (uint8_t)(d << 4));
    return (uint16_t)((((uint16_t)d << 8) | (uint16_t)(crc >> 8))
                      ^ (uint16_t)(d >> 4)
                      ^ (uint16_t)((uint16_t)d << 3));
}

uint16_t exbus_crc16(const uint8_t *data, size_t n)
{
    uint16_t crc = 0;

    for (size_t i = 0; data != NULL && i < n; ++i) {
        crc = exbus_crc16_update(crc, data[i]);
    }
    return crc;
}

uint16_t exbus_us_to_counts(uint32_t us)
{
    return us >= 0xFFFFu / EXBUS_COUNTS_PER_US + 1u
               ? (uint16_t)0xFFFFu
               : (uint16_t)(us * EXBUS_COUNTS_PER_US);
}

uint16_t exbus_clamp_counts(uint16_t counts)
{
    if (counts < EXBUS_CH_MIN) {
        return (uint16_t)EXBUS_CH_MIN;
    }
    if (counts > EXBUS_CH_MAX) {
        return (uint16_t)EXBUS_CH_MAX;
    }
    return counts;
}

/* The CRC of the @p len - 2 bytes in front of it, low byte first. */
static void put_crc(uint8_t *p, size_t len)
{
    const uint16_t crc = exbus_crc16(p, len - 2u);

    p[len - 2u] = (uint8_t)(crc & 0xFFu);
    p[len - 1u] = (uint8_t)(crc >> 8);
}

size_t exbus_build_channels(const uint16_t *counts, unsigned n,
                            uint8_t packet_id, bool release, uint8_t *out,
                            unsigned *clamped)
{
    unsigned changed = 0;

    if (clamped != NULL) {
        *clamped = 0;
    }
    if (counts == NULL || out == NULL || n == 0u || n > EXBUS_MAX_CHANNELS) {
        return 0;
    }
    const size_t len = EXBUS_OVERHEAD + 2u * (size_t)n;

    out[0] = EXBUS_HEAD_CHANNELS;
    out[1] = release ? EXBUS_HEAD_RELEASE : EXBUS_HEAD_HOLD;
    out[2] = (uint8_t)len;
    out[3] = packet_id;
    out[4] = EXBUS_ID_CHANNELS;
    out[5] = (uint8_t)(2u * n);
    for (unsigned i = 0; i < n; ++i) {
        const uint16_t v = exbus_clamp_counts(counts[i]);

        if (v != counts[i]) {
            ++changed;
        }
        out[6u + 2u * i] = (uint8_t)(v & 0xFFu);
        out[7u + 2u * i] = (uint8_t)(v >> 8);
    }
    put_crc(out, len);
    if (clamped != NULL) {
        *clamped = changed;
    }
    return len;
}

size_t exbus_build_request(uint8_t packet_id, uint8_t *out)
{
    if (out == NULL) {
        return 0;
    }
    out[0] = EXBUS_HEAD_REQUEST;
    out[1] = EXBUS_HEAD_RELEASE;
    out[2] = (uint8_t)EXBUS_REQUEST_LEN;
    out[3] = packet_id;
    out[4] = EXBUS_ID_TELEMETRY;
    out[5] = 0;
    put_crc(out, EXBUS_REQUEST_LEN);
    return EXBUS_REQUEST_LEN;
}

exbus_rx_t exbus_reply_parse(const uint8_t *bytes, size_t n,
                             uint8_t packet_id, exbus_reply_t *out)
{
    size_t k = 0;

    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (bytes == NULL || n == 0u) {
        return EXBUS_RX_NONE;
    }
    /* The header: 0x3B 0x01, or a lone 0x3B as the last byte. */
    while (k < n && !(bytes[k] == EXBUS_HEAD_REPLY
                      && (k + 1u == n || bytes[k + 1u] == EXBUS_HEAD_RELEASE))) {
        ++k;
    }
    if (out != NULL) {
        out->skipped = (uint8_t)(k > 0xFFu ? 0xFFu : k);
    }
    if (k == n) {
        return EXBUS_RX_HEADER;
    }
    const uint8_t *p = bytes + k;
    const size_t have = n - k;

    if (have < 3u) {
        return EXBUS_RX_TRUNCATED;
    }
    const size_t len = p[2];

    if (len < EXBUS_OVERHEAD) {
        return EXBUS_RX_LENGTH;
    }
    if (len > EXBUS_REPLY_MAX) {
        return EXBUS_RX_TOO_LONG;
    }
    if (have < len) {
        return EXBUS_RX_TRUNCATED;
    }
    if ((size_t)p[5] + EXBUS_OVERHEAD != len) {
        return EXBUS_RX_LENGTH;
    }
    const uint16_t crc = (uint16_t)(p[len - 2u] | ((uint16_t)p[len - 1u] << 8));

    if (exbus_crc16(p, len - 2u) != crc) {
        return EXBUS_RX_CRC;
    }
    if (p[4] != EXBUS_ID_TELEMETRY) {
        return EXBUS_RX_DATA_ID;
    }
    if (p[3] != packet_id) {
        return EXBUS_RX_PACKET_ID;
    }
    if (out != NULL) {
        out->packet_id = p[3];
        out->block_len = p[5];
        memcpy(out->block, p + 6, p[5]);
        out->extra = (uint8_t)(have - len > 0xFFu ? 0xFFu : have - len);
    }
    return EXBUS_RX_OK;
}

uint32_t exbus_air_us(uint32_t bytes, uint32_t baud)
{
    if (baud == 0u) {
        return 0;
    }
    const uint64_t bits = (uint64_t)bytes * EXBUS_BITS_PER_BYTE;

    return (uint32_t)((bits * 1000000u + baud - 1u) / baud);
}
