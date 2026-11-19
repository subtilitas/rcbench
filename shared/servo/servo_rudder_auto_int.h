/**
 * @file
 * @brief What servo_rudder_auto.c and servo_rudder_auto_verify.c share and
 *        nothing else reads: the run's end, its tolerance and the verify
 *        pass's two steps.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "servo_rudder_auto.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief What a float difference of two angles may lie past the tolerance
 *         and still count as within, °: 27.6 - 27.5 in float is 0.1000004. */
#define RAUTO_EPS_DEG 0.0005f

/**
 * @brief End the run (servo_rudder_auto.c's finish()).
 * @param r      Run state.
 * @param v      Verdict.
 * @param why    Reason.
 * @param now_ms Time, ms.
 */
void rauto_end(servo_rauto_t *r, servo_rauto_verdict_t v,
               servo_rauto_why_t why, uint32_t now_ms);

/**
 * @brief The tolerance, °.
 * @param r Run state.
 * @return cfg.tol_ddeg / 10.
 */
float rauto_tol_deg(const servo_rauto_t *r);

/**
 * @brief Command a measure's pulse: the phase, its start, the command and
 *        the settle window emptied.
 * @param r      Run state.
 * @param ph     MOVE, CHECK or VERIFY.
 * @param us     The pulse, us.
 * @param now_ms Time, ms.
 */
void rauto_command(servo_rauto_t *r, servo_rauto_phase_t ph, uint16_t us,
                   uint32_t now_ms);

/**
 * @brief A measure next, reached from @p from_us: the approach first
 *        (phase APPROACH) unless the servo stands settled at @p from_us
 *        already, then @p ph at @p us (servo_rudder_auto.h, "The
 *        approach").
 * @param r       Run state.
 * @param ph      MOVE, CHECK or VERIFY.
 * @param us      The measure's pulse, us.
 * @param from_us The pulse it comes from, us.
 * @param now_ms  Time, ms.
 */
void rauto_measure_via(servo_rauto_t *r, servo_rauto_phase_t ph, uint16_t us,
                       uint16_t from_us, uint32_t now_ms);

/**
 * @brief The verify pass's measure @p step next (servo_rudder_auto.h, "The
 *        verify pass").
 * @param r      Run state.
 * @param step   0 1000us, 1 2000us, 2 PULSE CENTER.
 * @param now_ms Time, ms.
 */
void rauto_verify_begin(servo_rauto_t *r, uint8_t step, uint32_t now_ms);

/**
 * @brief Whether a measure may take its angle now: SERVO_RAUTO_STILL_MS after
 *        its command, a valid angle still that long, on the IMU held within
 *        the settle rule, and no gap open (servo_rudder_auto.h, "A
 *        measure").
 * @param r      Run state.
 * @param now_ms Time, ms.
 * @param in     What the caller knows.
 * @return true when the angle is taken now.
 */
bool rauto_still(const servo_rauto_t *r, uint32_t now_ms,
                 const servo_rauto_in_t *in);

/**
 * @brief The limit search of side @p side next (servo_rudder_auto_search.c):
 *        the servo to PULSE CENTER, the finder stepping out once it had
 *        SERVO_LIMRUN_RETURN_MS there.
 * @param r      Run state.
 * @param side   0 the 1000us side, 1 the 2000us side.
 * @param now_ms Time, ms.
 */
void rauto_search_begin(servo_rauto_t *r, uint8_t side, uint32_t now_ms);

/**
 * @brief One INA3221 window in phase SEARCH: the finder stepped, its
 *        position recorded once judged, the hold at the end pulse begun.
 * @param r     Run state.
 * @param ma    The window's mean current, its magnitude, mA.
 * @param at_ms The window's time, ms.
 */
void rauto_search_window(servo_rauto_t *r, uint16_t ma, uint32_t at_ms);

/**
 * @brief Phase SEARCH's own step: the first measure at the end pulse taken
 *        by the measure's rule while the search holds there.
 * @param r      Run state.
 * @param now_ms Time, ms.
 * @param in     What the caller knows.
 */
void rauto_search_step(servo_rauto_t *r, uint32_t now_ms,
                       const servo_rauto_in_t *in);

/**
 * @brief Both sides searched: their first measures judged, the 1000us side
 *        first, then the first side outside the tolerance written, or the
 *        verify pass, or PASS (servo_rudder_auto.c).
 * @param r      Run state.
 * @param now_ms Time, ms.
 */
void rauto_searched(servo_rauto_t *r, uint32_t now_ms);

/**
 * @brief The angle the verify pass's measure took: recorded, then the next
 *        measure, or the end once all three are in.
 * @param r      Run state.
 * @param deg    The deflection from the zero, signed, °.
 * @param now_ms Time, ms.
 */
void rauto_verify_take(servo_rauto_t *r, float deg, uint32_t now_ms);
