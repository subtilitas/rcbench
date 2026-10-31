/**
 * @file ew_a.c
 * @brief Protocol A's frames and answers; see ew_a.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_a.h"

#include <string.h>

uint16_t ew_crc16_add(uint16_t crc, uint8_t byte)
{
    uint16_t c = (uint16_t)(crc ^ byte);

    for (unsigned i = 0; i < 8u; ++i) {
        c = (c & 1u) ? (uint16_t)((c >> 1) ^ 0xA001u) : (uint16_t)(c >> 1);
    }
    return c;
}

uint16_t ew_crc16(const uint8_t *p, size_t n)
{
    uint16_t c = 0u;

    for (size_t i = 0; i < n; ++i) {
        c = ew_crc16_add(c, p[i]);
    }
    return c;
}

/**
 * @brief Append the CRC of the first @p n bytes of @p out behind them.
 * @param out the frame
 * @param n   its length without the CRC
 * @return @p n + 2
 */
static size_t seal(uint8_t *out, size_t n)
{
    const uint16_t c = ew_crc16(out, n);

    out[n] = (uint8_t)(c & 0xFFu);
    out[n + 1u] = (uint8_t)(c >> 8);
    return n + 2u;
}

size_t ew_a_contact(uint8_t *out)
{
    static const uint8_t k_contact[EW_A_CONTACT_LEN] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D,
        0x42, 0x4C, 0x48, 0x65, 0x6C, 0x69, 0xF4, 0x7D,
    };

    memcpy(out, k_contact, sizeof(k_contact));
    return sizeof(k_contact);
}

size_t ew_a_address(uint8_t *out, uint16_t addr)
{
    out[0] = EW_A_CMD_ADDRESS;
    out[1] = 0u;
    out[2] = (uint8_t)(addr >> 8);
    out[3] = (uint8_t)(addr & 0xFFu);
    return seal(out, 4u);
}

size_t ew_a_read(uint8_t *out, uint16_t n)
{
    if (n == 0u || n > EW_A_READ_MAX) {
        return 0u;
    }
    return ew_a_short(out, EW_A_CMD_READ, (uint8_t)(n & 0xFFu));
}

size_t ew_a_announce(uint8_t *out, uint16_t count)
{
    if (count == 0u || count > EW_A_READ_MAX) {
        return 0u;
    }
    out[0] = EW_A_CMD_ANNOUNCE;
    out[1] = 0u;
    out[2] = (uint8_t)(count >> 8);
    out[3] = (uint8_t)(count & 0xFFu);
    return seal(out, 4u);
}

size_t ew_a_data(uint8_t *out, const uint8_t *data, size_t n)
{
    memcpy(out, data, n);
    return seal(out, n);
}

size_t ew_a_short(uint8_t *out, uint8_t cmd, uint8_t arg)
{
    out[0] = cmd;
    out[1] = arg;
    return seal(out, 2u);
}

/**
 * @brief What is wrong with a capture before its bytes are looked at.
 * @param c the capture
 * @return EW_ANS_OK when its bytes count
 */
static ew_ans_t capture_state(const ew_capture_t *c)
{
    if (c->aborted || c->overflow) {
        return EW_ANS_ABORTED;
    }
    if (c->n == 0u) {
        return EW_ANS_SILENT;
    }
    if (c->bad_stop != 0u) {
        return EW_ANS_FRAMING;
    }
    return EW_ANS_OK;
}

ew_ans_t ew_a_one(const ew_capture_t *c, const uint8_t *rx)
{
    const ew_ans_t st = capture_state(c);

    if (st != EW_ANS_OK) {
        return st;
    }
    if (c->n != 1u) {
        return EW_ANS_LENGTH;
    }
    switch (rx[0]) {
    case EW_A_ACK:         return EW_ANS_OK;
    case EW_A_REF_VERIFY:  return EW_ANS_VERIFY;
    case EW_A_REF_UNKNOWN: return EW_ANS_UNKNOWN;
    case EW_A_REF_CRC:     return EW_ANS_BAD_CRC;
    case EW_A_REF_PROGRAM: return EW_ANS_REFUSED;
    default:               return EW_ANS_OTHER;
    }
}

ew_ans_t ew_a_read_reply(const ew_capture_t *c, const uint8_t *rx, uint16_t n)
{
    const ew_ans_t st = capture_state(c);

    if (st != EW_ANS_OK) {
        return st;
    }
    if (c->n == 1u && n != 0u) {
        const ew_ans_t one = ew_a_one(c, rx);

        return one == EW_ANS_OK ? EW_ANS_LENGTH : one;
    }
    if (c->n != (uint16_t)(n + 3u)) {
        return EW_ANS_LENGTH;
    }
    if (ew_crc16(rx, (size_t)n + 2u) != 0u) {
        return EW_ANS_CRC;
    }
    return rx[n + 2u] == EW_A_ACK ? EW_ANS_OK : EW_ANS_NO_ACK;
}

ew_ans_t ew_a_contact_reply(const ew_capture_t *c, const uint8_t *rx)
{
    const ew_ans_t st = capture_state(c);

    if (st != EW_ANS_OK) {
        return st;
    }
    if (c->n != EW_A_ANSWER_LEN) {
        return EW_ANS_LENGTH;
    }
    return (rx[0] == 0x34u && rx[1] == 0x37u && rx[2] == 0x31u) ? EW_ANS_OK
                                                                : EW_ANS_OTHER;
}
