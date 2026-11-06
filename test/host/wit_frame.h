/*
 * A WitMotion angle frame (0x53) built byte by byte from the layout in
 * witmotion.h, for the suites that feed an IMU model without the BLE rig
 * of imu_rig.h: 0x55, the type, roll, pitch and yaw as int16 words at 180°
 * a 32768, the version 0x1234, the low byte of the sum.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef WIT_FRAME_H
#define WIT_FRAME_H

#include <math.h>
#include <stdint.h>

#include "witmotion.h"

/**
 * @brief An angle frame of roll @p x, pitch @p y and yaw @p z, each held
 *        to the word's range.
 * @param f Takes WIT_FRAME_LEN bytes.
 * @param x Roll, °.
 * @param y Pitch, °.
 * @param z Yaw, °.
 */
static inline void wit_angle_frame(uint8_t f[WIT_FRAME_LEN], float x, float y,
                                   float z)
{
    const float d[3] = { x, y, z };
    uint16_t w[4] = { 0u, 0u, 0u, 0x1234u };
    for (int i = 0; i < 3; ++i) {
        long v = lround((double)d[i] * 32768.0 / 180.0);
        v = (v > 32767) ? 32767 : (v < -32768) ? -32768 : v;
        w[i] = (uint16_t)(int16_t)v;
    }
    uint8_t sum = (uint8_t)(0x55u + WIT_T_ANGLE);
    f[0] = 0x55u;
    f[1] = WIT_T_ANGLE;
    for (unsigned i = 0; i < 4u; ++i) {
        f[2u + 2u * i] = (uint8_t)(w[i] & 0xFFu);
        f[3u + 2u * i] = (uint8_t)(w[i] >> 8);
        sum = (uint8_t)(sum + f[2u + 2u * i] + f[3u + 2u * i]);
    }
    f[10] = sum;
}

#endif
