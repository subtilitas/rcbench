/*
 * Servo pulses from the RP2350's hardware PWM (pulse-width modulation).
 *
 * A servo pulse is a level held for a number of microseconds, repeated at a
 * frame rate, and that is what a PWM slice does with no processor involved.
 * There is no reason to spend a PIO (programmable input/output) state machine
 * on it: the part has twelve slices and twenty-four channels, more outputs
 * than this bench has connectors.
 *
 * What a caller has to know is how GPIO (general-purpose input/output)
 * numbers fold onto them.  A slice is two channels sharing one counter, so
 * two pins on the same slice run at the same frame rate whatever the second
 * one asked for, and a second binding with a different rate is refused rather
 * than quietly retimed.  Below that, a channel is one compare register: pins
 * 16 apart under GP32 -- GP0 and GP16 -- and pins 8 apart above it -- GP32 and
 * GP40 -- are the same channel of the same slice and cannot hold two pulse
 * widths, so the second of such a pair is refused outright.  out_pwm_map.h
 * has the fold and the host suite tests it.
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
 * Refuses a pin whose compare register another bound pin already holds, a pin
 * that is already bound to a different rate's slice, a pin the package does
 * not have, and a rate whose period does not fit the counter.  Binding an
 * already-bound pin at the same rate succeeds and changes nothing, so a
 * reconfiguration that did not move a slot does not glitch its output.
 *
 * A refused bind leaves the slot unbound and the OUTPUTS page reading back
 * what was asked for.  Nothing on the wire reports whether a slot is bound,
 * so an unbound slot is not distinguishable from a bound one at the panel.
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

/** How long the stamped write may wait for the counter to leave the end
 *  of its frame, us.  Two counts at most when the slice runs. */
#define OUT_PWM_STAMP_WAIT_US 5u

/**
 * out_pwm_write(), and the time the first frame carrying @p pulse_us
 * starts, on the 64-bit microsecond timer (time_us_64()), into
 * @p frame_us.
 *
 * The compare register is double-buffered: a level written during a frame
 * takes effect at the wrap that ends it, and the pin rises at counter 0.
 * The counter is read and the level written with interrupts off, never in
 * the frame's last 2 counts -- a write there could meet the wrap that
 * latches it, and the frame it lands in would be unknown -- so the write
 * lands in the frame the counter was read in, and that frame ends
 * (wrap - counter + 1) counts later.  One count is 1 us, from clk_sys
 * divided to 1 MHz; the timer ticks at 1 MHz from the same crystal.  The
 * stamp is good to the 1 us of each, within +-1 us.
 *
 * False, with the level written and nothing stamped, for a pin not bound
 * or a slice whose counter does not leave the frame's end within
 * OUT_PWM_STAMP_WAIT_US: one that is not counting.
 */
bool out_pwm_write_stamped(uint8_t pin, uint16_t pulse_us, uint64_t *frame_us);

#endif /* RCBENCH_OUT_PWM_H */
