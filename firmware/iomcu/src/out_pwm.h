/*
 * Servo pulses from the RP2350's hardware PWM (pulse-width modulation).
 *
 * A servo pulse is a level held for a number of microseconds, repeated at a
 * frame rate, and that is what a PWM slice does with no processor involved.
 * There is no reason to spend a PIO (programmable input/output) state machine
 * on it: the part has twelve slices and twenty-four channels, more outputs
 * than this bench has connectors.
 *
 * The one thing a caller has to know is that a slice is two channels sharing
 * one counter.  Two pins on the same slice run at the same frame rate,
 * whatever the second one asked for, so a second binding with a different
 * rate is refused rather than quietly retimed.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_OUT_PWM_H
#define RCBENCH_OUT_PWM_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Take @p pin at @p rate_hz frames a second.
 *
 * Refuses a pin that is already bound to a different rate's slice, and a rate
 * whose period does not fit the counter.  Binding an already-bound pin at the
 * same rate succeeds and changes nothing, so a reconfiguration that did not
 * move a slot does not glitch its output.
 */
bool out_pwm_bind(uint8_t pin, uint16_t rate_hz);

/** Give the pin back: the counter stops and the pin is left low. */
void out_pwm_release(uint8_t pin);

/**
 * Emit @p pulse_us on every frame.  Zero stops the pin edging altogether,
 * which is what the bank means by not driving -- a servo held at a pulse it
 * was not commanded is worse than one that is let go.
 */
void out_pwm_write(uint8_t pin, uint16_t pulse_us);

#endif /* RCBENCH_OUT_PWM_H */
