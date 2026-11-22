/**
 * @file
 * @brief COBS (consistent overhead byte stuffing) and CRC-16 framing of link
 * frames for a UART; see link_uart.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "link_uart.h"

#include <string.h>

#include "link_crc.h"

/**
 * @brief COBS-encode @p len bytes into @p out, without the delimiter.
 *
 * Each block is a code byte, then code - 1 bytes that are not 0x00; a code
 * under 0xFF stands for a 0x00 after its block, except after the last one.
 * @p len is at most LINK_UART_RAW_MAX here, so no block reaches the 254-byte
 * limit and the output is always @p len + 1 bytes.
 *
 * @param in The bytes to encode.
 * @param len Bytes in @p in, 1 to LINK_UART_RAW_MAX.
 * @param out Receives @p len + 1 bytes, none of them 0x00.
 * @return The bytes written: @p len + 1.
 */
static size_t cobs_encode(const uint8_t *in, size_t len, uint8_t *out)
{
    size_t code_at = 0;
    size_t w = 1;
    uint8_t code = 1;

    for (size_t i = 0; i < len; ++i) {
        if (in[i] == 0u) {
            out[code_at] = code;
            code_at = w++;
            code = 1;
        } else {
            out[w++] = in[i];
            ++code;
        }
    }
    out[code_at] = code;
    return w;
}

/**
 * @brief COBS-decode @p len bytes that held no 0x00.
 *
 * @param in The COBS bytes, delimiter not included, none of them 0x00.
 * @param len Bytes in @p in, 1 to LINK_UART_COBS_MAX.
 * @param out Receives the decoded bytes; holds LINK_UART_COBS_MAX bytes.
 * @return The bytes decoded, or -1 when a code byte points past the end.
 */
static int cobs_decode(const uint8_t *in, size_t len, uint8_t *out)
{
    size_t r = 0;
    size_t w = 0;

    while (r < len) {
        /* A code of n, 1 to 255, is followed by n - 1 bytes.  The decoder
         * holds no 0x00, and a code of 0xFF, the block without a 0x00 after
         * it, points past the 16 bytes held, so every block that ends before
         * the last one stands for a 0x00. */
        const size_t code = in[r++];
        if (r + code - 1u > len) {
            return -1;
        }
        for (size_t k = 1; k < code; ++k) {
            out[w++] = in[r++];
        }
        if (r < len) {
            out[w++] = 0u;
        }
    }
    return (int)w;
}

size_t link_uart_encode(const link_can_frame_t *f, uint8_t *out, size_t cap)
{
    if (f == NULL || out == NULL || f->dlc > 8u || f->id > LINK_UART_ID_MAX) {
        return 0;
    }
    const size_t raw_len = LINK_UART_RAW_MIN + f->dlc;
    if (cap < raw_len + 2u) {
        return 0;
    }

    uint8_t raw[LINK_UART_RAW_MAX];
    for (size_t i = 0; i < LINK_UART_ID_BYTES; ++i) {
        raw[i] = (uint8_t)((f->id >> (8u * i)) & 0xFFu);
    }
    raw[LINK_UART_ID_BYTES] = f->dlc;
    memcpy(&raw[LINK_UART_HEAD_BYTES], f->data, f->dlc);
    const size_t body = LINK_UART_HEAD_BYTES + f->dlc;
    const uint16_t crc = link_crc(LINK_CRC_INIT, raw, body);
    raw[body]      = (uint8_t)(crc & 0xFFu);
    raw[body + 1u] = (uint8_t)(crc >> 8);

    const size_t n = cobs_encode(raw, raw_len, out);
    out[n] = LINK_UART_DELIM;
    return n + 1u;
}

void link_uart_rx_init(link_uart_rx_t *rx)
{
    if (rx != NULL) {
        memset(rx, 0, sizeof(*rx));
    }
}

/**
 * @brief Count one dropped frame in its own count and in @p refused.
 *
 * @param count The decoder's count of this kind of refusal.
 * @param refused The STATUS counter; may be NULL.
 */
static void refuse(uint32_t *count, uint32_t *refused)
{
    ++*count;
    if (refused != NULL) {
        ++*refused;
    }
}

/**
 * @brief Check the frame the decoder holds at its 0x00 and hand it out.
 *
 * The CRC is checked before the DLC and the identifier: a byte changed on
 * the line fails the CRC, and a frame that passes it with a DLC over 8, a
 * length that disagrees with its DLC or an identifier over 29 bits is a
 * fault of the sender.
 *
 * @param rx The decoder, holding 1 to LINK_UART_COBS_MAX bytes.
 * @param out Receives the frame.
 * @param refused The STATUS counter; may be NULL.
 * @return true when @p out holds a frame.
 */
static bool finish(link_uart_rx_t *rx, link_can_frame_t *out,
                   uint32_t *refused)
{
    uint8_t raw[LINK_UART_COBS_MAX];
    const int n = cobs_decode(rx->buf, rx->len, raw);

    if (n < 0) {
        refuse(&rx->counts.bad_cobs, refused);
        return false;
    }
    const size_t len = (size_t)n;
    if (len < LINK_UART_RAW_MIN) {
        refuse(&rx->counts.bad_len, refused);
        return false;
    }
    const size_t body = len - LINK_UART_CRC_BYTES;
    const uint16_t sent = (uint16_t)((uint16_t)raw[body]
                                     | ((uint16_t)raw[body + 1u] << 8));
    if (link_crc(LINK_CRC_INIT, raw, body) != sent) {
        refuse(&rx->counts.bad_crc, refused);
        return false;
    }
    const uint8_t dlc = raw[LINK_UART_ID_BYTES];
    if (dlc > 8u) {
        refuse(&rx->counts.bad_dlc, refused);
        return false;
    }
    if (len != LINK_UART_RAW_MIN + dlc) {
        refuse(&rx->counts.bad_len, refused);
        return false;
    }
    uint32_t id = 0;
    for (size_t i = 0; i < LINK_UART_ID_BYTES; ++i) {
        id |= (uint32_t)raw[i] << (8u * i);
    }
    if (id > LINK_UART_ID_MAX) {
        refuse(&rx->counts.bad_id, refused);
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->id  = id;
    out->dlc = dlc;
    memcpy(out->data, &raw[LINK_UART_HEAD_BYTES], dlc);
    ++rx->counts.frames;
    return true;
}

bool link_uart_feed(link_uart_rx_t *rx, uint8_t byte, link_can_frame_t *out,
                    uint32_t *refused)
{
    if (rx == NULL || out == NULL) {
        return false;
    }

    if (byte != LINK_UART_DELIM) {
        if (!rx->synced) {
            rx->dirty = true;
        } else if (!rx->overlong) {
            if (rx->len < LINK_UART_COBS_MAX) {
                rx->buf[rx->len++] = byte;
            } else {
                /* Counted here, once; the rest up to the 0x00 is dropped. */
                rx->overlong = true;
                rx->len = 0;
                refuse(&rx->counts.overlong, refused);
            }
        }
        return false;
    }

    /* A 0x00: the end of whatever came before it. */
    bool took = false;
    if (!rx->synced) {
        if (rx->dirty) {
            refuse(&rx->counts.garbage, refused);
        }
        rx->synced = true;
    } else if (!rx->overlong && rx->len > 0u) {
        took = finish(rx, out, refused);
    }
    rx->len = 0;
    rx->overlong = false;
    return took;
}
