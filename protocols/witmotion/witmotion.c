/**
 * @file
 * @brief WitMotion serial protocol: frame parser, decoders and command
 *        encoder.
 *
 * See witmotion.h for the frame layout.  Every field is assembled byte by
 * byte; no struct is cast over a buffer.
 *
 * SPDX-License-Identifier: MIT
 */

#include "witmotion.h"

/** @brief Standard gravity, m/s² per g. */
#define WIT_G 9.80665f
/** @brief Full scale of a signed 16-bit word: the divisor of every unit. */
#define WIT_FULL 32768.0f
/** @brief Acceleration range, +-g. */
#define WIT_ACC_G 16.0f
/** @brief Angular rate range, +-°/s. */
#define WIT_GYRO_DPS 2000.0f
/** @brief Angle range, +-°. */
#define WIT_ANGLE_DEG 180.0f
/** @brief Magnetic field, µT per LSB. */
#define WIT_MAG_UT 0.15f
/** @brief Temperature and battery words are hundredths. */
#define WIT_HUNDREDTH 100.0f
/** @brief Highest RATE code. */
#define WIT_RATE_MAX 0x0Bu
/** @brief Highest CALSW mode. */
#define WIT_CAL_MAX 2u

/**
 * @brief Low byte of the sum of the first WIT_FRAME_LEN - 1 bytes.
 * @param b A buffer of at least WIT_FRAME_LEN - 1 bytes.
 * @return The sum modulo 256.
 */
static uint8_t frame_sum(const uint8_t *b)
{
    uint8_t sum = 0u;
    for (unsigned i = 0; i < WIT_FRAME_LEN - 1u; ++i) {
        sum = (uint8_t)(sum + b[i]);
    }
    return sum;
}

void wit_parser_init(wit_parser_t *p)
{
    if (p == NULL) {
        return;
    }
    p->n = 0u;                  /* buf is written before it is read */
    p->frames = 0u;
    p->bad_sum = 0u;
    p->resyncs = 0u;
}

/**
 * @brief Drop a held frame whose sum failed and keep what follows its first
 *        byte from the next 0x55 on.
 * @param p The parser with WIT_FRAME_LEN bytes held.
 *
 * The first byte (the sync of the dropped frame) counts in bad_sum; each
 * byte skipped before the next 0x55 counts in resyncs.  The bytes from that
 * 0x55 on stay in the buffer, so a frame that began inside the dropped one
 * is not lost.
 */
static void drop_bad_frame(wit_parser_t *p)
{
    unsigned k = 1u;
    while (k < WIT_FRAME_LEN && p->buf[k] != WIT_SYNC) {
        ++k;
    }
    p->bad_sum++;
    p->resyncs += k - 1u;
    p->n = (uint8_t)(WIT_FRAME_LEN - k);
    for (unsigned i = 0; i < p->n; ++i) {
        p->buf[i] = p->buf[k + i];
    }
}

bool wit_parser_feed(wit_parser_t *p, uint8_t b, wit_frame_t *out)
{
    if (p == NULL || out == NULL) {
        return false;
    }
    if (p->n >= WIT_FRAME_LEN) {
        p->n = 0u;                      /* a state the parser never leaves */
    }
    if (p->n == 0u && b != WIT_SYNC) {
        p->resyncs++;
        return false;
    }
    p->buf[p->n++] = b;
    if (p->n < WIT_FRAME_LEN) {
        return false;
    }
    if (frame_sum(p->buf) != p->buf[WIT_FRAME_LEN - 1u]) {
        drop_bad_frame(p);
        return false;
    }
    out->type = p->buf[1];
    for (unsigned i = 0; i < 4u; ++i) {
        out->v[i] = (int16_t)(uint16_t)((uint16_t)p->buf[2u + 2u * i]
                      | (uint16_t)((uint16_t)p->buf[3u + 2u * i] << 8));
    }
    p->n = 0u;
    p->frames++;
    return true;
}

/**
 * @brief Check a decoder's arguments.
 * @param f    The frame.
 * @param out  The decoder's result, any type.
 * @param type The type the decoder takes.
 * @return true when both are set and the frame has that type.
 */
static bool takes(const wit_frame_t *f, const void *out, wit_type_t type)
{
    return f != NULL && out != NULL && f->type == (uint8_t)type;
}

/**
 * @brief A signed word scaled to a range.
 * @param w     The word.
 * @param range The value at 32768.
 * @return @p w / 32768 x @p range.
 */
static float scaled(int16_t w, float range)
{
    return (float)w / WIT_FULL * range;
}

bool wit_decode_acc(const wit_frame_t *f, wit_acc_t *out)
{
    if (!takes(f, out, WIT_T_ACC)) {
        return false;
    }
    out->ax = scaled(f->v[0], WIT_ACC_G) * WIT_G;
    out->ay = scaled(f->v[1], WIT_ACC_G) * WIT_G;
    out->az = scaled(f->v[2], WIT_ACC_G) * WIT_G;
    out->temp_c = (float)f->v[3] / WIT_HUNDREDTH;
    return true;
}

bool wit_decode_gyro(const wit_frame_t *f, wit_gyro_t *out)
{
    if (!takes(f, out, WIT_T_GYRO)) {
        return false;
    }
    out->wx = scaled(f->v[0], WIT_GYRO_DPS);
    out->wy = scaled(f->v[1], WIT_GYRO_DPS);
    out->wz = scaled(f->v[2], WIT_GYRO_DPS);
    out->battery_v = (float)(uint16_t)f->v[3] / WIT_HUNDREDTH;
    return true;
}

bool wit_decode_angle(const wit_frame_t *f, wit_angle_t *out)
{
    if (!takes(f, out, WIT_T_ANGLE)) {
        return false;
    }
    out->roll = scaled(f->v[0], WIT_ANGLE_DEG);
    out->pitch = scaled(f->v[1], WIT_ANGLE_DEG);
    out->yaw = scaled(f->v[2], WIT_ANGLE_DEG);
    out->version = (uint16_t)f->v[3];
    return true;
}

bool wit_decode_mag(const wit_frame_t *f, wit_mag_t *out)
{
    if (!takes(f, out, WIT_T_MAG)) {
        return false;
    }
    out->mx = (float)f->v[0] * WIT_MAG_UT;
    out->my = (float)f->v[1] * WIT_MAG_UT;
    out->mz = (float)f->v[2] * WIT_MAG_UT;
    return true;
}

size_t wit_cmd(uint8_t out[WIT_CMD_LEN], uint8_t reg, uint16_t value)
{
    if (out == NULL) {
        return 0u;
    }
    out[0] = 0xFFu;
    out[1] = 0xAAu;
    out[2] = reg;
    out[3] = (uint8_t)(value & 0xFFu);
    out[4] = (uint8_t)(value >> 8);
    return WIT_CMD_LEN;
}

size_t wit_cmd_unlock(uint8_t out[WIT_CMD_LEN])
{
    if (out == NULL) {
        return 0u;
    }
    out[0] = 0xFFu;
    out[1] = 0xAAu;
    out[2] = 0x69u;
    out[3] = 0x88u;
    out[4] = 0xB5u;
    return WIT_CMD_LEN;
}

size_t wit_cmd_save(uint8_t out[WIT_CMD_LEN])
{
    return wit_cmd(out, WIT_REG_SAVE, 0u);
}

size_t wit_cmd_rate(uint8_t out[WIT_CMD_LEN], uint8_t code)
{
    if (code < WIT_RATE_0_2HZ || code > WIT_RATE_MAX) {
        return 0u;
    }
    return wit_cmd(out, WIT_REG_RATE, code);
}

size_t wit_cmd_rsw(uint8_t out[WIT_CMD_LEN], uint16_t mask)
{
    return wit_cmd(out, WIT_REG_RSW, mask);
}

size_t wit_cmd_calsw(uint8_t out[WIT_CMD_LEN], uint8_t mode)
{
    if (mode > WIT_CAL_MAX) {
        return 0u;
    }
    return wit_cmd(out, WIT_REG_CALSW, mode);
}

size_t wit_cmd_axis6(uint8_t out[WIT_CMD_LEN], bool six)
{
    return wit_cmd(out, WIT_REG_AXIS6, six ? 1u : 0u);
}

uint16_t wit_rate_dhz(uint8_t code)
{
    static const uint16_t k_dhz[WIT_RATE_MAX] = {
        2u, 5u, 10u, 20u, 50u, 100u, 200u, 500u, 1000u, 1250u, 2000u,
    };
    if (code < WIT_RATE_0_2HZ || code > WIT_RATE_MAX) {
        return 0u;
    }
    return k_dhz[code - 1u];
}
