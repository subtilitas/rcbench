/*
 * Protocol 2.0 packets of ROBOTIS Dynamixel servos.  See dxl_packet.h.
 *
 * ROBOTIS and Dynamixel are names or trademarks of their owner; they are
 * used here only to say which servos this module works with.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dxl_packet.h"

#include <string.h>

#define CRC_POLY 0x8005u

uint16_t dxl_crc(uint16_t crc, const uint8_t *p, size_t n)
{
    if (p == NULL) {
        return crc;
    }
    for (size_t i = 0; i < n; ++i) {
        crc = (uint16_t)(crc ^ ((uint16_t)p[i] << 8));
        for (unsigned b = 0; b < 8u; ++b) {
            const bool top = (crc & 0x8000u) != 0u;

            crc = (uint16_t)(crc << 1);
            if (top) {
                crc = (uint16_t)(crc ^ CRC_POLY);
            }
        }
    }
    return crc;
}

/* body[i] ends FF FF FD in the unstuffed body. */
static bool ends_triple(const uint8_t *body, size_t i)
{
    return i >= 2u && body[i] == 0xFDu && body[i - 1u] == 0xFFu
           && body[i - 2u] == 0xFFu;
}

size_t dxl_stuffed_size(const uint8_t *body, size_t n)
{
    size_t out = n;

    if (body == NULL) {
        return 0;
    }
    for (size_t i = 0; i < n; ++i) {
        if (ends_triple(body, i)) {
            ++out;
        }
    }
    return out;
}

static bool id_ok(uint8_t id)
{
    return id <= DXL_ID_MAX || id == DXL_ID_BROADCAST;
}

/* Header, stuffed body, CRC.  The body is @p head (1 or 2 bytes:
 * instruction, and the ERR byte of a status packet) followed by @p params;
 * stuffing runs over both as one sequence. */
static size_t build(uint8_t id, const uint8_t *head, size_t n_head,
                    const uint8_t *params, size_t n, uint8_t *out, size_t cap)
{
    uint8_t prev2 = 0;
    uint8_t prev1 = 0;
    size_t at = DXL_HEADER_SIZE;

    if (out == NULL || (params == NULL && n > 0u) || n > DXL_PARAM_MAX) {
        return 0;
    }
    for (size_t i = 0; i < n_head + n; ++i) {
        const uint8_t b = i < n_head ? head[i] : params[i - n_head];

        if (at + 1u + DXL_CRC_SIZE > cap) {
            return 0;
        }
        out[at++] = b;
        if (b == 0xFDu && prev1 == 0xFFu && prev2 == 0xFFu) {
            if (at + 1u + DXL_CRC_SIZE > cap) {
                return 0;
            }
            out[at++] = 0xFDu;
            /* The inserted byte starts no new FF FF FD. */
            prev1 = 0;
            prev2 = 0;
        } else {
            prev2 = prev1;
            prev1 = b;
        }
    }
    /* The loop ran at least once and left room for the CRC. */
    const size_t len = at - DXL_HEADER_SIZE + DXL_CRC_SIZE;

    out[0] = 0xFFu;
    out[1] = 0xFFu;
    out[2] = 0xFDu;
    out[3] = 0x00u;
    out[4] = id;
    dxl_put_u16(&out[5], (uint16_t)len);
    const uint16_t crc = dxl_crc(0, out, at);

    dxl_put_u16(&out[at], crc);
    return at + DXL_CRC_SIZE;
}

size_t dxl_encode(uint8_t id, uint8_t instr, const uint8_t *params, size_t n,
                  uint8_t *out, size_t cap)
{
    if (!id_ok(id) || instr == (uint8_t)DXL_INS_FACTORY_RESET
        || instr == (uint8_t)DXL_INS_STATUS) {
        return 0;
    }
    return build(id, &instr, 1u, params, n, out, cap);
}

size_t dxl_encode_status(uint8_t id, uint8_t err, const uint8_t *params,
                         size_t n, uint8_t *out, size_t cap)
{
    const uint8_t head[2] = { (uint8_t)DXL_INS_STATUS, err };

    if (id > DXL_ID_MAX) {
        return 0;
    }
    return build(id, head, 2u, params, n, out, cap);
}

size_t dxl_ping(uint8_t id, uint8_t *out, size_t cap)
{
    return dxl_encode(id, (uint8_t)DXL_INS_PING, NULL, 0, out, cap);
}

size_t dxl_read(uint8_t id, uint16_t addr, uint16_t len, uint8_t *out,
                size_t cap)
{
    uint8_t p[4];

    if (id == DXL_ID_BROADCAST || len == 0u || len > DXL_READ_MAX) {
        return 0;
    }
    dxl_put_u16(&p[0], addr);
    dxl_put_u16(&p[2], len);
    return dxl_encode(id, (uint8_t)DXL_INS_READ, p, sizeof(p), out, cap);
}

static size_t write_like(uint8_t instr, uint8_t id, uint16_t addr,
                         const uint8_t *data, size_t n, uint8_t *out,
                         size_t cap)
{
    uint8_t p[DXL_PARAM_MAX];

    if (data == NULL || n == 0u || n > DXL_WRITE_MAX) {
        return 0;
    }
    dxl_put_u16(&p[0], addr);
    memcpy(&p[2], data, n);
    return dxl_encode(id, instr, p, n + 2u, out, cap);
}

size_t dxl_write(uint8_t id, uint16_t addr, const uint8_t *data, size_t n,
                 uint8_t *out, size_t cap)
{
    return write_like((uint8_t)DXL_INS_WRITE, id, addr, data, n, out, cap);
}

size_t dxl_reg_write(uint8_t id, uint16_t addr, const uint8_t *data, size_t n,
                     uint8_t *out, size_t cap)
{
    return write_like((uint8_t)DXL_INS_REG_WRITE, id, addr, data, n, out,
                      cap);
}

size_t dxl_action(uint8_t id, uint8_t *out, size_t cap)
{
    return dxl_encode(id, (uint8_t)DXL_INS_ACTION, NULL, 0, out, cap);
}

size_t dxl_reboot(uint8_t id, uint8_t *out, size_t cap)
{
    return dxl_encode(id, (uint8_t)DXL_INS_REBOOT, NULL, 0, out, cap);
}

size_t dxl_factory_reset(uint8_t id, uint8_t option, uint32_t confirm,
                         uint8_t *out, size_t cap)
{
    const uint8_t instr = (uint8_t)DXL_INS_FACTORY_RESET;

    if (confirm != DXL_FACTORY_RESET_CONFIRM || id > DXL_ID_MAX
        || (option != DXL_RESET_ALL && option != DXL_RESET_ALL_BUT_ID
            && option != DXL_RESET_ALL_BUT_ID_BAUD)) {
        return 0;
    }
    return build(id, &instr, 1u, &option, 1u, out, cap);
}

static bool ids_ok(const uint8_t *ids, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (ids[i] > DXL_ID_MAX) {
            return false;
        }
    }
    return true;
}

size_t dxl_sync_read(uint16_t addr, uint16_t len, const uint8_t *ids,
                     size_t n_ids, uint8_t *out, size_t cap)
{
    uint8_t p[DXL_PARAM_MAX];

    if (ids == NULL || n_ids == 0u || n_ids > DXL_PARAM_MAX - 4u
        || len == 0u || len > DXL_READ_MAX || !ids_ok(ids, n_ids)) {
        return 0;
    }
    dxl_put_u16(&p[0], addr);
    dxl_put_u16(&p[2], len);
    memcpy(&p[4], ids, n_ids);
    return dxl_encode(DXL_ID_BROADCAST, (uint8_t)DXL_INS_SYNC_READ, p,
                      n_ids + 4u, out, cap);
}

size_t dxl_sync_write(uint16_t addr, uint16_t len, const uint8_t *ids,
                      const uint8_t *data, size_t n_ids, uint8_t *out,
                      size_t cap)
{
    uint8_t p[DXL_PARAM_MAX];
    size_t at = 4;

    if (ids == NULL || data == NULL || n_ids == 0u || len == 0u
        || 4u + n_ids * (1u + (size_t)len) > DXL_PARAM_MAX
        || !ids_ok(ids, n_ids)) {
        return 0;
    }
    dxl_put_u16(&p[0], addr);
    dxl_put_u16(&p[2], len);
    for (size_t i = 0; i < n_ids; ++i) {
        p[at++] = ids[i];
        memcpy(&p[at], &data[i * len], len);
        at += len;
    }
    return dxl_encode(DXL_ID_BROADCAST, (uint8_t)DXL_INS_SYNC_WRITE, p, at,
                      out, cap);
}

/* The first header start at or after @p from: a whole FF FF FD 00, or a
 * prefix of one that the end of the bytes cuts.  @p n when none. */
static size_t find_header(const uint8_t *buf, size_t n, size_t from)
{
    static const uint8_t hdr[4] = { 0xFFu, 0xFFu, 0xFDu, 0x00u };

    for (size_t i = from; i < n; ++i) {
        size_t k = 0;

        while (k < 4u && i + k < n && buf[i + k] == hdr[k]) {
            ++k;
        }
        if (k == 4u || i + k == n) {
            return i;
        }
    }
    return n;
}

dxl_rx_t dxl_parse(const uint8_t *buf, size_t n, dxl_packet_t *out,
                   size_t *used)
{
    size_t dummy;

    if (used == NULL) {
        used = &dummy;
    }
    *used = 0;
    if (buf == NULL || out == NULL) {
        return DXL_RX_ERR_ARG;
    }
    const size_t s = find_header(buf, n, 0);

    *used = s;
    if (n - s < DXL_HEADER_SIZE) {
        return DXL_RX_MORE;
    }
    const uint8_t id = buf[s + 4u];
    const size_t len = dxl_get_u16(&buf[s + 5u]);

    if (id == 253u || id == 255u) {
        *used = s + DXL_HEADER_SIZE;
        return DXL_RX_ERR_ID;
    }
    if (len < 1u + DXL_CRC_SIZE) {
        *used = s + DXL_HEADER_SIZE;
        return DXL_RX_ERR_LENGTH;
    }
    if (len > DXL_WIRE_MAX - DXL_HEADER_SIZE) {
        *used = s + DXL_HEADER_SIZE;
        return DXL_RX_ERR_OVERSIZED;
    }
    if (n - s < DXL_HEADER_SIZE + len) {
        return DXL_RX_MORE;
    }
    const uint8_t *body = &buf[s + DXL_HEADER_SIZE];
    const size_t n_body = len - DXL_CRC_SIZE;
    const uint16_t crc = dxl_crc(0, &buf[s], DXL_HEADER_SIZE + n_body);

    *used = s + DXL_HEADER_SIZE + len;
    if (crc != dxl_get_u16(&body[n_body])) {
        return DXL_RX_ERR_CRC;
    }

    /* Destuff into the packet: the instruction, the ERR byte of a status
     * packet, then the parameters. */
    const bool status = body[0] == (uint8_t)DXL_INS_STATUS;
    const size_t n_head = status ? 2u : 1u;
    size_t k = 0;

    out->id = id;
    out->instr = body[0];
    out->error = 0;
    out->n_params = 0;
    out->wire_len = (uint16_t)(DXL_HEADER_SIZE + len);
    for (size_t i = 0; i < n_body; ++i) {
        const uint8_t b = body[i];

        if (k == 1u && status) {
            out->error = b;
        } else if (k >= n_head) {
            if (k - n_head >= DXL_PARAM_MAX) {
                return DXL_RX_ERR_OVERSIZED;
            }
            out->params[k - n_head] = b;
        }
        ++k;
        if (ends_triple(body, i)) {
            if (i + 1u >= n_body || body[i + 1u] != 0xFDu) {
                return DXL_RX_ERR_STUFFING;
            }
            ++i;
        }
    }
    if (k < n_head) {
        return DXL_RX_ERR_LENGTH;
    }
    out->n_params = (uint16_t)(k - n_head);
    return DXL_RX_OK;
}
