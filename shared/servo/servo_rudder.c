/**
 * @file
 * @brief The control surface's geometry and the RUDDER task's readings.
 *
 * See servo_rudder.h.  The travel in mm is the trailing edge's distance
 * from the neutral plane, what a ruler held upright at the trailing edge
 * reads: L x sin(theta) for a surface L mm from the hinge line to the
 * trailing edge.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_rudder.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/** @brief Degrees to radians. */
#define RAD_PER_DEG (3.14159265358979f / 180.0f)

/**
 * @brief Whether @p len_mm is a surface length the readings take.
 * @param len_mm The length, mm.
 * @return true for SERVO_RUDDER_LEN_MIN_MM or more; false for NaN
 */
static bool len_ok(float len_mm)
{
    return len_mm >= SERVO_RUDDER_LEN_MIN_MM;
}

float servo_rudder_mm(float deg, float len_mm)
{
    if (!len_ok(len_mm)) {
        return 0.0f;
    }
    return len_mm * sinf(deg * RAD_PER_DEG);
}

bool servo_rudder_deg_of_mm(float mm, float len_mm, float *deg)
{
    if (deg == NULL || !len_ok(len_mm) || !(fabsf(mm) <= len_mm)) {
        return false;
    }
    *deg = asinf(mm / len_mm) / RAD_PER_DEG;
    return true;
}

void servo_rudder_init(servo_rudder_t *r)
{
    if (r != NULL) {
        memset(r, 0, sizeof(*r));
    }
}

void servo_rudder_zero(servo_rudder_t *r, float deg_now)
{
    if (r == NULL) {
        return;
    }
    r->zeroed   = true;
    r->zero_deg = deg_now;
    servo_rudder_reset_max(r);
}

/**
 * @brief @p deg onto -180 to 180: 180 itself reads -180.
 * @param deg The angle, degrees.
 * @return The same direction in -180 up to 180.
 */
static float wrap180(float deg)
{
    float d = fmodf(deg + 180.0f, 360.0f);
    if (d < 0.0f) {
        d += 360.0f;
    }
    return d - 180.0f;
}

float servo_rudder_defl(const servo_rudder_t *r, float deg_now)
{
    if (r == NULL || !r->zeroed) {
        return deg_now;
    }
    return wrap180(deg_now - r->zero_deg);
}

void servo_rudder_track(servo_rudder_t *r, float defl)
{
    /* No side for 0 and none for NaN: neither is a deflection. */
    if (r == NULL || defl == 0.0f || isnan(defl)) {
        return;
    }
    const int side = (defl > 0.0f) ? 1 : 0;
    const float mag = fabsf(defl);
    if (!r->have[side] || mag > r->max_deg[side]) {
        r->have[side]    = true;
        r->max_deg[side] = mag;
    }
}

void servo_rudder_reset_max(servo_rudder_t *r)
{
    if (r == NULL) {
        return;
    }
    r->have[0]    = false;
    r->have[1]    = false;
    r->max_deg[0] = 0.0f;
    r->max_deg[1] = 0.0f;
}
