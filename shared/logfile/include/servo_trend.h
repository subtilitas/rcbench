/**
 * @file
 * @brief The call that tells the labelled servos' analyzer a run is filed:
 *        its files in `Servos/<id>/` are closed.
 *
 * Called on the frame task once the logger has closed a run's files in a
 * servo's folder: an AUTOMATIC TEST, a LIMIT RUN, a RUDDER AUTO or an ARMED
 * run from SERVO.  The body counts the calls and keeps the last; the
 * analyzer's figures (part B, P6e) hang off it.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "servo_id.h"
#include "servo_name.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The last run filed, as servo_trend_filed() heard it. */
typedef struct {
    unsigned     count;              /**< calls since power-on            */
    char         id[SERVO_ID_SIZE];  /**< the servo                       */
    servo_kind_t kind;               /**< the run's kind                  */
    int          number;             /**< its number, 0 to 999            */
} servo_trend_last_t;

/**
 * @brief Run @p number of kind @p kind of servo @p id is filed.
 * @param id     The servo's id.
 * @param kind   The kind.
 * @param number The number.
 */
void servo_trend_filed(const char *id, servo_kind_t kind, int number);

/**
 * @brief The last call, for the console and the tests.
 * @return The record; count 0 before the first call.
 */
const servo_trend_last_t *servo_trend_last(void);

#ifdef __cplusplus
}
#endif
