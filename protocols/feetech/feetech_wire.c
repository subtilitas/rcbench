/*
 * SPDX-License-Identifier: MIT
 */

#include "feetech_wire.h"

#include <string.h>

uint8_t feetech_checksum(const uint8_t *from_id, size_t n)
{
    unsigned sum = 0;

    for (size_t i = 0; i < n; ++i) {
        sum += from_id[i];
    }
    return (uint8_t)(~sum & 0xFFu);
}

bool feetech_build(feetech_frame_t *out, uint8_t id, uint8_t instr,
                   const uint8_t *params, unsigned n_params)
{
    if (out == NULL) {
        return false;
    }
    out->n = 0;
    if (id > FEETECH_ID_BROADCAST || n_params > FEETECH_PARAMS_MAX
        || (params == NULL && n_params > 0u)) {
        return false;
    }
    out->bytes[0] = 0xFFu;
    out->bytes[1] = 0xFFu;
    out->bytes[2] = id;
    out->bytes[3] = (uint8_t)(n_params + 2u);
    out->bytes[4] = instr;
    if (n_params > 0u) {
        memcpy(&out->bytes[5], params, n_params);
    }
    out->bytes[5u + n_params] = feetech_checksum(&out->bytes[2],
                                                 3u + n_params);
    out->n = (uint8_t)(n_params + FEETECH_FRAME_OVERHEAD);
    return true;
}

bool feetech_ping(feetech_frame_t *out, uint8_t id)
{
    return feetech_build(out, id, FEETECH_PING, NULL, 0);
}

bool feetech_read(feetech_frame_t *out, uint8_t id, uint8_t addr,
                  uint8_t count)
{
    const uint8_t p[2] = { addr, count };

    if (count == 0u || count > FEETECH_DATA_MAX) {
        if (out != NULL) {
            out->n = 0;
        }
        return false;
    }
    return feetech_build(out, id, FEETECH_READ, p, 2u);
}

static bool write_kind(feetech_frame_t *out, uint8_t instr, uint8_t id,
                       uint8_t addr, const uint8_t *data, unsigned n)
{
    uint8_t p[FEETECH_DATA_MAX + 1u];

    if (n == 0u || n > FEETECH_DATA_MAX || data == NULL) {
        if (out != NULL) {
            out->n = 0;
        }
        return false;
    }
    p[0] = addr;
    memcpy(&p[1], data, n);
    return feetech_build(out, id, instr, p, n + 1u);
}

bool feetech_write(feetech_frame_t *out, uint8_t id, uint8_t addr,
                   const uint8_t *data, unsigned n)
{
    return write_kind(out, FEETECH_WRITE, id, addr, data, n);
}

bool feetech_reg_write(feetech_frame_t *out, uint8_t id, uint8_t addr,
                       const uint8_t *data, unsigned n)
{
    return write_kind(out, FEETECH_REG_WRITE, id, addr, data, n);
}

bool feetech_action(feetech_frame_t *out, uint8_t id)
{
    return feetech_build(out, id, FEETECH_ACTION, NULL, 0);
}

bool feetech_reset(feetech_frame_t *out, uint8_t id)
{
    return feetech_build(out, id, FEETECH_RESET, NULL, 0);
}

bool feetech_sync_write(feetech_frame_t *out, uint8_t addr, uint8_t len,
                        const uint8_t *blocks, unsigned n_ids)
{
    uint8_t p[FEETECH_PARAMS_MAX];
    const unsigned body = ((unsigned)len + 1u) * n_ids;

    if (out != NULL) {
        out->n = 0;
    }
    if (len == 0u || n_ids == 0u || blocks == NULL
        || body + 2u > FEETECH_PARAMS_MAX) {
        return false;
    }
    p[0] = addr;
    p[1] = len;
    memcpy(&p[2], blocks, body);
    return feetech_build(out, FEETECH_ID_BROADCAST, FEETECH_SYNC_WRITE, p,
                         body + 2u);
}

feetech_rx_t feetech_parse(const uint8_t *bytes, unsigned n, unsigned framing,
                           uint8_t want_id, unsigned want_params,
                           feetech_status_t *out)
{
    bool bad_sum = false;
    bool short_frame = false;

    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (bytes == NULL || n == 0u) {
        return FEETECH_RX_NONE;
    }
    for (unsigned at = 0; at + 1u < n; ++at) {
        unsigned len;
        unsigned total;

        if (bytes[at] != 0xFFu || bytes[at + 1u] != 0xFFu) {
            continue;
        }
        if (at + 4u > n) {
            /* The header and too little after it to say how long. */
            short_frame = true;
            break;
        }
        /* FF FF FF ...: the first FF is no header, the next may be. */
        if (bytes[at + 2u] == 0xFFu) {
            continue;
        }
        len = bytes[at + 3u];
        if (len < 2u || len > FEETECH_DATA_MAX + 2u) {
            continue;
        }
        total = len + 4u;
        if (at + total > n) {
            short_frame = true;
            continue;
        }
        if (feetech_checksum(&bytes[at + 2u], len + 1u)
            != bytes[at + total - 1u]) {
            bad_sum = true;
            continue;
        }
        if (out != NULL) {
            out->id = bytes[at + 2u];
            out->error = bytes[at + 4u];
            out->n_params = (uint8_t)(len - 2u);
            memcpy(out->params, &bytes[at + 5u], len - 2u);
            out->skipped = (uint8_t)(at > 255u ? 255u : at);
        }
        if (bytes[at + 2u] != want_id) {
            return FEETECH_RX_WRONG_ID;
        }
        if (want_params <= FEETECH_DATA_MAX && len - 2u != want_params) {
            return FEETECH_RX_LENGTH;
        }
        return FEETECH_RX_OK;
    }
    if (framing > 0u) {
        return FEETECH_RX_FRAMING;
    }
    if (bad_sum) {
        return FEETECH_RX_CHECKSUM;
    }
    if (short_frame) {
        return FEETECH_RX_TRUNCATED;
    }
    return FEETECH_RX_NOISE;
}

unsigned feetech_echo(const uint8_t *rx, unsigned n_rx, const uint8_t *tx,
                      unsigned n_tx)
{
    if (rx == NULL || tx == NULL || n_tx == 0u || n_rx < n_tx
        || memcmp(rx, tx, n_tx) != 0) {
        return 0;
    }
    return n_tx;
}

uint32_t feetech_bytes_us(uint32_t baud, unsigned n_bytes)
{
    if (baud == 0u) {
        return 0;
    }
    return (uint32_t)(((uint64_t)n_bytes * FEETECH_BITS_PER_BYTE * 1000000u
                       + baud - 1u) / baud);
}
