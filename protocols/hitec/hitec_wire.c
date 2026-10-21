/**
 * @file
 * @brief Hitec servo programming protocols: frames, transactions and the reply
 *        decoders.  See include/hitec_wire.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "hitec_wire.h"

#include <string.h>

uint32_t hitec_bit_rate(uint8_t family)
{
    if (family == HITEC_CLASSIC) {
        return HITEC_C_BIT_RATE;
    }
    if (family == HITEC_D) {
        return HITEC_D_BIT_RATE;
    }
    return 0;
}

uint32_t hitec_bits_ns(uint8_t family, uint32_t bits)
{
    const uint32_t rate = hitec_bit_rate(family);

    if (rate == 0u) {
        return 0;
    }
    return (uint32_t)(((uint64_t)bits * 1000000000u + rate / 2u) / rate);
}

/* --- classic frames ------------------------------------------------------- */

uint8_t hitec_c_sum(uint8_t cmd, uint8_t p1, uint8_t p2)
{
    return (uint8_t)(0u - (unsigned)cmd - (unsigned)p1 - (unsigned)p2);
}

void hitec_c_frame(uint8_t cmd, uint8_t p1, uint8_t p2,
                   uint8_t out[HITEC_C_FRAME_LEN])
{
    out[0] = cmd;
    out[1] = p1;
    out[2] = p2;
    out[3] = hitec_c_sum(cmd, p1, p2);
}

bool hitec_c_frame_ok(const uint8_t frame[HITEC_C_FRAME_LEN])
{
    return (uint8_t)(frame[0] + frame[1] + frame[2] + frame[3]) == 0u;
}

/* --- D series frames ------------------------------------------------------ */

uint8_t hitec_d_sum(const uint8_t *bytes, size_t n)
{
    unsigned sum = 0;

    for (size_t i = 0; i < n; ++i) {
        sum += bytes[i];
    }
    return (uint8_t)sum;
}

void hitec_d_read_frame(uint8_t id, uint8_t addr,
                        uint8_t out[HITEC_D_READ_LEN])
{
    out[0] = HITEC_D_HEADER_REQUEST;
    out[1] = id;
    out[2] = addr;
    out[3] = 0;
    out[4] = hitec_d_sum(&out[1], 3);
}

void hitec_d_write_frame(uint8_t id, uint8_t addr, uint16_t value,
                         uint8_t out[HITEC_D_WRITE_LEN])
{
    out[0] = HITEC_D_HEADER_REQUEST;
    out[1] = id;
    out[2] = addr;
    out[3] = HITEC_D_DATA_LEN;
    out[4] = (uint8_t)(value & 0xFFu);
    out[5] = (uint8_t)(value >> 8);
    out[6] = hitec_d_sum(&out[1], 5);
}

/* --- transactions --------------------------------------------------------- */

bool hitec_xfer_valid(const hitec_xfer_t *x)
{
    if (x == NULL || hitec_bit_rate(x->family) == 0u
        || x->window_us > HITEC_TIME_MAX_US) {
        return false;
    }
    /* A receive has a byte count and a window, or neither. */
    if ((x->window_us == 0u) != (x->max_rx == 0u)
        || x->max_rx > HITEC_RX_MAX) {
        return false;
    }
    switch (x->kind) {
    case HITEC_XFER_SEND:
        return x->n_tx >= 1u && x->n_tx <= HITEC_TX_MAX
               && x->gap_bits <= HITEC_GAP_MAX
               && x->lead_us <= HITEC_TIME_MAX_US && x->answer == 0u;
    case HITEC_XFER_LISTEN:
        return x->window_us > 0u && x->answer <= 1u;
    case HITEC_XFER_LEVEL:
        return x->level <= 1u && x->level_us >= 1u
               && x->level_us <= HITEC_TIME_MAX_US && x->window_us == 0u
               && x->answer == 0u;
    default:
        return false;
    }
}

/**
 * @brief Set up a SEND transaction with the given bytes and no receive window.
 *
 * @param family  hitec_family_t value.
 * @param bytes   Bytes to send.
 * @param n       Number of bytes, at most HITEC_TX_MAX.
 * @param out     Receives the transaction.
 */
static void send_xfer(uint8_t family, const uint8_t *bytes, uint8_t n,
                      hitec_xfer_t *out)
{
    memset(out, 0, sizeof(*out));
    out->kind = HITEC_XFER_SEND;
    out->family = family;
    out->n_tx = n;
    memcpy(out->tx, bytes, n);
}

void hitec_c_xfer(uint8_t cmd, uint8_t p1, uint8_t p2, hitec_xfer_t *out)
{
    uint8_t f[HITEC_C_FRAME_LEN];

    hitec_c_frame(cmd, p1, p2, f);
    send_xfer(HITEC_CLASSIC, f, HITEC_C_FRAME_LEN, out);
    out->gap_bits = HITEC_C_GAP_BITS;
    out->window_us = HITEC_C_WINDOW_US;
    out->max_rx = HITEC_C_REPLY_LEN;
}

void hitec_d_read_xfer(uint8_t id, uint8_t addr, hitec_xfer_t *out)
{
    uint8_t f[HITEC_D_READ_LEN];

    hitec_d_read_frame(id, addr, f);
    send_xfer(HITEC_D, f, HITEC_D_READ_LEN, out);
    out->lead_us = HITEC_D_LEAD_US;
    out->window_us = HITEC_D_WINDOW_US;
    out->max_rx = HITEC_D_READ_RX;
}

void hitec_d_write_xfer(uint8_t id, uint8_t addr, uint16_t value,
                        hitec_xfer_t *out)
{
    uint8_t f[HITEC_D_WRITE_LEN];

    hitec_d_write_frame(id, addr, value, f);
    send_xfer(HITEC_D, f, HITEC_D_WRITE_LEN, out);
    out->lead_us = HITEC_D_LEAD_US;
}

bool hitec_listen_xfer(uint8_t family, uint32_t window_us, uint8_t max_rx,
                       bool answer, uint8_t answer_byte, hitec_xfer_t *out)
{
    hitec_xfer_t x;

    memset(&x, 0, sizeof(x));
    x.kind = HITEC_XFER_LISTEN;
    x.family = family;
    x.window_us = window_us;
    x.max_rx = max_rx;
    x.answer = answer ? 1u : 0u;
    x.answer_byte = answer_byte;
    if (out == NULL || !hitec_xfer_valid(&x)) {
        return false;
    }
    *out = x;
    return true;
}

bool hitec_level_xfer(uint8_t family, uint8_t level, uint32_t time_us,
                      hitec_xfer_t *out)
{
    hitec_xfer_t x;

    memset(&x, 0, sizeof(x));
    x.kind = HITEC_XFER_LEVEL;
    x.family = family;
    x.level = level;
    x.level_us = time_us;
    if (out == NULL || !hitec_xfer_valid(&x)) {
        return false;
    }
    *out = x;
    return true;
}

/* --- decoders ------------------------------------------------------------- */

bool hitec_c_expect_of(uint8_t cmd, hitec_c_expect_t *out)
{
    hitec_c_expect_t e;

    switch (cmd) {
    case HITEC_C_READ_EEPROM:
    case HITEC_C_READ_RAM:
    case HITEC_C_FIRST:
        e = HITEC_C_EXPECT_VALUE;
        break;
    case HITEC_C_WRITE_EEPROM:
    case HITEC_C_WRITE_RAM:
    case HITEC_C_MOVE:
        e = HITEC_C_EXPECT_ACK;
        break;
    case HITEC_C_READ_POSITION:
        e = HITEC_C_EXPECT_WORD;
        break;
    default:
        return false;
    }
    if (out != NULL) {
        *out = e;
    }
    return true;
}

/**
 * @brief The checks every decoder starts with.
 *
 * Checks abort, overflow and that the capture holds at least one byte.
 *
 * @param cap  Capture to check.
 * @param out  Receives the byte count in out->n.
 * @return HITEC_RX_OK when the capture holds at least one byte to look at, or
 *         the code of the failed check.
 */
static hitec_rx_t common_checks(const hitec_capture_t *cap,
                                hitec_reply_t *out)
{
    if (cap->aborted != 0u) {
        return HITEC_RX_ERR_ABORTED;
    }
    if (cap->overflow != 0u || cap->n > HITEC_RX_MAX) {
        return HITEC_RX_ERR_OVERFLOW;
    }
    out->n = cap->n;
    if (cap->n == 0u) {
        return HITEC_RX_ERR_SILENT;
    }
    return HITEC_RX_OK;
}

/**
 * @brief Duration of half bit times.
 *
 * @param family  hitec_family_t value.
 * @param half    Number of half bit times.
 * @return @p half half bit times of @p family in ns, rounded to nearest.
 */
static uint32_t half_bits_ns(uint8_t family, uint32_t half)
{
    const uint32_t rate = hitec_bit_rate(family);

    return (uint32_t)(((uint64_t)half * 1000000000u + rate) / (2ull * rate));
}

/**
 * @brief Check the spacing of the reply bytes.
 *
 * Bytes 1 to @p n - 1 each start 9.5 to 15 bit times after the one before.
 *
 * @param cap  Capture to check.
 * @param n    Number of bytes to check.
 * @return true when every gap is within HITEC_NEXT_MIN_HALF_BITS to
 *         HITEC_NEXT_MAX_HALF_BITS half bit times.
 */
static bool gaps_ok(const hitec_capture_t *cap, unsigned n)
{
    const uint32_t lo = half_bits_ns(cap->family, HITEC_NEXT_MIN_HALF_BITS);
    const uint32_t hi = half_bits_ns(cap->family, HITEC_NEXT_MAX_HALF_BITS);

    for (unsigned i = 1; i < n; ++i) {
        const uint32_t a = cap->byte[i - 1u].at_ns;
        const uint32_t b = cap->byte[i].at_ns;

        if (b < a || b - a < lo || b - a > hi) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Check the stop bits of the first bytes of a capture.
 *
 * @param cap  Capture to check.
 * @param n    Number of bytes to check.
 * @return true when none of the first @p n bytes has a bad stop bit.
 */
static bool framing_ok(const hitec_capture_t *cap, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        if (cap->byte[i].framing != 0u) {
            return false;
        }
    }
    return true;
}

hitec_rx_t hitec_c_decode(const hitec_capture_t *cap, hitec_c_expect_t expect,
                          hitec_reply_t *out)
{
    hitec_rx_t r;
    uint64_t delay;
    const hitec_rx_byte_t *b;

    if (out == NULL) {
        return HITEC_RX_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (cap == NULL || cap->family != HITEC_CLASSIC
        || (unsigned)expect > (unsigned)HITEC_C_EXPECT_WORD) {
        return HITEC_RX_ERR_ARG;
    }
    r = common_checks(cap, out);
    if (r != HITEC_RX_OK) {
        return r;
    }
    b = cap->byte;
    /* From the start of the master's last byte. */
    delay = (uint64_t)b[0].at_ns
            + hitec_bits_ns(HITEC_CLASSIC, HITEC_C_ARM_BITS);
    out->delay_ns = delay > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)delay;
    if (delay < HITEC_C_REPLY_MIN_NS) {
        return HITEC_RX_ERR_EARLY;
    }
    if (delay > HITEC_C_REPLY_MAX_NS) {
        return HITEC_RX_ERR_LATE;
    }
    if (cap->n == 1u && b[0].value == HITEC_C_NAK && b[0].framing == 0u) {
        return HITEC_RX_ERR_NAK;
    }
    if (cap->n != HITEC_C_REPLY_LEN) {
        return HITEC_RX_ERR_LENGTH;
    }
    if (!framing_ok(cap, HITEC_C_REPLY_LEN)) {
        return HITEC_RX_ERR_FRAMING;
    }
    if (!gaps_ok(cap, HITEC_C_REPLY_LEN)) {
        return HITEC_RX_ERR_GAP;
    }
    switch (expect) {
    case HITEC_C_EXPECT_VALUE:
        if (b[1].value != HITEC_C_ACK) {
            return HITEC_RX_ERR_ACK;
        }
        out->value = b[0].value;
        break;
    case HITEC_C_EXPECT_ACK:
        if (b[0].value != HITEC_C_ACK || b[1].value != HITEC_C_ACK) {
            return HITEC_RX_ERR_ACK;
        }
        break;
    default:
        out->value = (uint16_t)((unsigned)b[0].value << 8 | b[1].value);
        break;
    }
    return HITEC_RX_OK;
}

hitec_rx_t hitec_d_decode(const hitec_capture_t *cap, uint8_t addr,
                          hitec_reply_t *out)
{
    hitec_rx_t r;
    uint64_t delay;
    uint8_t a[HITEC_D_ANSWER_LEN];

    if (out == NULL) {
        return HITEC_RX_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (cap == NULL || cap->family != HITEC_D) {
        return HITEC_RX_ERR_ARG;
    }
    r = common_checks(cap, out);
    if (r != HITEC_RX_OK) {
        return r;
    }
    /* From the end of the request's last stop bit. */
    delay = (uint64_t)cap->byte[0].at_ns
            + hitec_bits_ns(HITEC_D, HITEC_D_SETTLE_BITS);
    out->delay_ns = delay > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)delay;
    if (delay < HITEC_D_ANSWER_MIN_NS) {
        return HITEC_RX_ERR_EARLY;
    }
    if (delay > HITEC_D_ANSWER_MAX_NS) {
        return HITEC_RX_ERR_LATE;
    }
    if (cap->n < HITEC_D_ANSWER_LEN) {
        return HITEC_RX_ERR_LENGTH;
    }
    if (!framing_ok(cap, HITEC_D_ANSWER_LEN)) {
        return HITEC_RX_ERR_FRAMING;
    }
    if (!gaps_ok(cap, HITEC_D_ANSWER_LEN)) {
        return HITEC_RX_ERR_GAP;
    }
    for (unsigned i = 0; i < HITEC_D_ANSWER_LEN; ++i) {
        a[i] = cap->byte[i].value;
    }
    out->id = a[1];
    if (a[0] != HITEC_D_HEADER_ANSWER || a[3] != HITEC_D_DATA_LEN) {
        return HITEC_RX_ERR_HEADER;
    }
    if (a[2] != addr) {
        return HITEC_RX_ERR_ADDRESS;
    }
    if (a[6] != hitec_d_sum(&a[1], 5)) {
        return HITEC_RX_ERR_SUM;
    }
    out->value = (uint16_t)(a[4] | (unsigned)a[5] << 8);
    return HITEC_RX_OK;
}

hitec_rx_t hitec_listen_decode(const hitec_capture_t *cap, uint8_t want,
                               hitec_reply_t *out)
{
    hitec_rx_t r;

    if (out == NULL) {
        return HITEC_RX_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (cap == NULL || hitec_bit_rate(cap->family) == 0u) {
        return HITEC_RX_ERR_ARG;
    }
    r = common_checks(cap, out);
    if (r != HITEC_RX_OK) {
        return r;
    }
    out->delay_ns = cap->byte[0].at_ns;
    out->value = cap->byte[0].value;
    if (cap->byte[0].framing != 0u) {
        return HITEC_RX_ERR_FRAMING;
    }
    if (cap->byte[0].value != want) {
        return HITEC_RX_ERR_ACK;
    }
    if (cap->n > 1u) {
        return HITEC_RX_ERR_LENGTH;
    }
    return HITEC_RX_OK;
}

/* --- driver helpers ------------------------------------------------------- */

uint32_t hitec_elapsed_us(uint32_t now, uint32_t since)
{
    return now - since;
}

bool hitec_due(uint32_t now, uint32_t since, uint32_t span_us)
{
    return hitec_elapsed_us(now, since) >= span_us;
}
