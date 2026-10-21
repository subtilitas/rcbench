/**
 * @file
 * @brief Which PWM (pulse-width modulation) register drives which pin on the
 *        RP2350.
 *
 * The part has 12 slices of two channels, A and B. A slice counts against one
 * wrap register, so its two channels share a frame rate; each channel holds
 * its own compare register, and that compare register is the pulse width.
 *
 * GPIO (general-purpose input/output) numbers are folded onto 12 slices. GP0
 * to GP31 take slice (pin / 2) modulo 8; GP32 to GP47 take slice 8 + (pin / 2)
 * modulo 4. The channel is the low bit of the pin number in both ranges.
 *
 * The fold is what this header exists for. Two pins 16 apart below GP32 --
 * GP0 and GP16, GP5 and GP21 -- are one slice and one channel, which is one
 * compare register muxed to two pads. Above GP32 the distance is 8: GP32 and
 * GP40. Driving both pins of such a pair puts one pulse width on two leads,
 * and only the pin written last says what it is.
 *
 * This is the pico-sdk's PWM_GPIO_SLICE_NUM restated in pure C, so the host
 * suite can hold it. firmware/iomcu/src/out_pwm.c compares the two on every
 * bind rather than assuming they agree.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_OUT_PWM_MAP_H
#define RCBENCH_OUT_PWM_MAP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Slices in the block, and channels in a slice. */
#define OUT_PWM_SLICES    12u
#define OUT_PWM_CHANNELS   2u

/**
 * @brief The GPIO numbers the fold is defined over: 0 to 47.
 *
 * The widest RP2350 bank, the RP2350B's. The RP2350A brings out GP0 to GP29
 * and the rest of the fold is unreachable there, but which pins a package has
 * is the firmware's fact, not this arithmetic's.
 */
#define OUT_PWM_GPIOS     48u

/**
 * @brief Returned for a pin outside OUT_PWM_GPIOS, as a slice and as a channel.
 */
#define OUT_PWM_NONE      0xFFu

/**
 * @brief The slice @p pin is wired to.
 *
 * @param pin GPIO number, 0 to OUT_PWM_GPIOS - 1 (47).
 * @return Slice index, 0 to 11. OUT_PWM_NONE for a pin of 48 or more.
 */
uint8_t out_pwm_slice_of(uint8_t pin);

/**
 * @brief Which of that slice's channels: 0 is A, 1 is B.
 *
 * OUT_PWM_NONE for a pin outside OUT_PWM_GPIOS.
 *
 * @param pin GPIO number, 0 to OUT_PWM_GPIOS - 1 (47).
 * @return Channel index, 0 or 1. OUT_PWM_NONE for a pin of 48 or more.
 */
uint8_t out_pwm_channel_of(uint8_t pin);

/**
 * @brief Whether two GPIOs count against one wrap register.
 *
 * True for the two channels of a slice, which can carry different pulse
 * widths but not different frame rates. False when either pin is outside
 * OUT_PWM_GPIOS: a pin the fold does not reach shares nothing.
 *
 * @param a First GPIO number.
 * @param b Second GPIO number.
 * @return true when both pins are in range and fold to one slice, false
 *         otherwise.
 */
bool out_pwm_same_slice(uint8_t a, uint8_t b);

/**
 * @brief Whether two GPIOs are driven from one compare register.
 *
 * True for a pin against itself, and for the folded pairs -- GP0 and GP16,
 * GP32 and GP40. Two such pins cannot carry different pulse widths at all,
 * so a driver that has one of them must refuse the other rather than bind
 * both. False when either pin is outside OUT_PWM_GPIOS.
 *
 * @param a First GPIO number.
 * @param b Second GPIO number.
 * @return true when both pins are in range and fold to one slice and one
 *         channel, false otherwise.
 */
bool out_pwm_same_compare(uint8_t a, uint8_t b);

/**
 * @brief The counter's tick: one count per microsecond, so a pulse width in
 *        microseconds is a compare value and a frame of rate R Hz is 1,000,000
 *        / R counts, rounded down.
 */
#define OUT_PWM_TICK_HZ 1000000u

/**
 * @brief The longest pulse a frame of @p rate_hz carries, in microseconds: the
 *        frame's last count, one less than its period.
 *
 * 0 for a rate of 0 and for a period the sixteen-bit counter cannot hold
 * (below 16 Hz).
 *
 * A pin is high while the counter is below the compare value, so a pulse of
 * this length leaves one count of low at the end of each frame.  One count
 * more never goes low: the pin sits high, which is no pulse at all.  This
 * is the rule the output pages refuse an endpoint by, and the wrap
 * firmware/iomcu/src/out_pwm.c programs, so the two cannot disagree.
 *
 * 1784 us at 560 Hz, 1999 us at 500 Hz, 2499 us at 400 Hz and 19999 us at
 * 50 Hz.
 *
 * @param rate_hz Frame rate in Hz. The counter tick is OUT_PWM_TICK_HZ.
 * @return Longest pulse in microseconds. 0 for a rate of 0 and for a period
 *         above 65536 counts.
 */
uint16_t out_pwm_pulse_max(uint16_t rate_hz);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_OUT_PWM_MAP_H */
