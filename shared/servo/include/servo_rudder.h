/**
 * @file
 * @brief The control surface's geometry and the RUDDER task's readings:
 *        deflection from a zero, its travel in mm at the trailing edge
 *        (L x sin(theta)), and the largest deflection each side since
 *        RESET.  Pure C.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief LENGTH's range: the shortest surface taken, mm. */
#define SERVO_RUDDER_LEN_MIN_MM 10.0f    /**< LENGTH's range */
/** @brief LENGTH's range: the longest surface taken, mm. */
#define SERVO_RUDDER_LEN_MAX_MM 500.0f

/**
 * @brief L x sin(theta); 0 for a length under SERVO_RUDDER_LEN_MIN_MM.
 * @param deg The deflection theta, degrees.
 * @param len_mm The surface's length L, hinge line to trailing edge, mm.
 * @return The trailing edge's travel, mm, signed as @p deg.
 */
float servo_rudder_mm(float deg, float len_mm);
/**
 * @brief theta = asin(mm / L).
 * @param mm The trailing edge's travel, mm, signed.
 * @param len_mm The surface's length L, mm.
 * @param deg Where theta goes, degrees; untouched when refused.
 * @return false for |mm| > L, a length under the minimum, or NULL.
 */
bool  servo_rudder_deg_of_mm(float mm, float len_mm, float *deg);

/** @brief The zero and the largest deflection each side. */
typedef struct {
    bool  zeroed;            /**< a zero was taken */
    float zero_deg;          /**< the surface's neutral, source degrees */
    bool  have[2];           /**< a MAX each side: 0 negative, 1 positive */
    float max_deg[2];        /**< the largest magnitude seen each side */
} servo_rudder_t;

/**
 * @brief No zero, no MAX.
 * @param r The state; NULL does nothing.
 */
void  servo_rudder_init(servo_rudder_t *r);
/**
 * @brief Take @p deg_now as the zero and clear the MAX.
 * @param r The state; NULL does nothing.
 * @param deg_now The source's angle now, degrees.
 */
void  servo_rudder_zero(servo_rudder_t *r, float deg_now);
/**
 * @brief Deflection from the zero, wrapped to -180 to 180; @p deg_now
 *        itself while not zeroed.
 * @param r The state; NULL is not zeroed.
 * @param deg_now The source's angle now, degrees.
 * @return The deflection, degrees.
 */
float servo_rudder_defl(const servo_rudder_t *r, float deg_now);
/**
 * @brief Record @p defl in the MAX of its side.
 * @param r The state; NULL does nothing.
 * @param defl A deflection, degrees; 0 and NaN have no side.
 */
void  servo_rudder_track(servo_rudder_t *r, float defl);
/**
 * @brief Clear both MAX.
 * @param r The state; NULL does nothing.
 */
void  servo_rudder_reset_max(servo_rudder_t *r);

#ifdef __cplusplus
}
#endif
