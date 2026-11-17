/**
 * @file
 * @brief AUTO end points: the verify pass after programming, 1000us,
 *        2000us and PULSE CENTER measured once more; see
 *        servo_rudder_auto.h, "The verify pass".
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_rudder_auto_int.h"

#include <math.h>

/** @brief The verify pass's pulses but the center, us. */
static const uint16_t k_verify_us[2] = { 1000u, 2000u };

void rauto_verify_begin(servo_rauto_t *r, uint8_t step, uint32_t now_ms)
{
    r->vstep = step;
    if (step < 2u) {
        r->side = step;
        /* An end from PULSE CENTER, as the main pass reaches it. */
        rauto_measure_via(r, SERVO_RAUTO_PH_VERIFY, k_verify_us[step],
                          r->cfg.center_us, now_ms);
    } else {
        /* The center always from the 2000us side: the measure before
         * stands there. */
        rauto_measure_via(r, SERVO_RAUTO_PH_VERIFY, r->cfg.center_us,
                          k_verify_us[1], now_ms);
    }
}

void rauto_verify_take(servo_rauto_t *r, float deg, uint32_t now_ms)
{
    const uint8_t k = r->vstep;
    if (k >= SERVO_RAUTO_VERIFY_N) {
        return;
    }
    const float tol = rauto_tol_deg(r) + RAUTO_EPS_DEG;
    float v = deg;
    bool within;
    if (k < 2u) {
        /* An end along its direction, known from its first measure. */
        v = r->sign[k] != 0 ? deg * (float)r->sign[k] : fabsf(deg);
        within = fabsf(v - r->goal_deg[k]) <= tol;
    } else {
        within = fabsf(deg) <= tol;
    }
    r->vdeg[k]    = v;
    r->vwithin[k] = within;
    r->vn         = (uint8_t)(k + 1u);
    if (k + 1u < SERVO_RAUTO_VERIFY_N) {
        rauto_verify_begin(r, (uint8_t)(k + 1u), now_ms);
        return;
    }
    for (uint8_t j = 0; j < SERVO_RAUTO_VERIFY_N; ++j) {
        if (!r->vwithin[j]) {
            r->vbad = j;
            rauto_end(r, SERVO_RAUTO_FAIL, SERVO_RAUTO_WHY_VERIFY, now_ms);
            return;
        }
    }
    rauto_end(r, SERVO_RAUTO_PASS, SERVO_RAUTO_WHY_NONE, now_ms);
}
