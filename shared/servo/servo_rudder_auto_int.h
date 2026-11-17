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
 * @brief The angle the verify pass's measure took: recorded, then the next
 *        measure, or the end once all three are in.
 * @param r      Run state.
 * @param deg    The deflection from the zero, signed, °.
 * @param now_ms Time, ms.
 */
void rauto_verify_take(servo_rauto_t *r, float deg, uint32_t now_ms);
