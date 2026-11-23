/**
 * @file
 * @brief The pass-through's pulse decoder: the states the capture pushes,
 *        with their microsecond stamps, turned into pulse widths, frame
 *        periods, missed frames, glitches and lost signals.
 *
 * The capture (rp2350/pass.pio) samples 18 pins, GPIO 16 to GPIO 33 of the
 * remote servo node, and pushes the whole state on every change.  Bit b of
 * a state is GPIO 16 + b: an even bit is output channel b / 2, an odd bit
 * input channel (b - 1) / 2.  Every pin is decoded by the same rules:
 *
 *   pulse width   fall stamp less rise stamp, 1us resolution.
 *   glitch        a high under PASS_PULSE_MIN_US (400us) or over
 *                 PASS_PULSE_MAX_US (2600us): counted, not taken as a
 *                 pulse, and its rise is no frame.
 *   frame period  rise to rise of two pulses taken.
 *   missed frames a rise-to-rise gap over 1.5 periods counts
 *                 round(gap / period) - 1.  Two such gaps in a row within
 *                 1/8 of each other are a new rate: the second sets the
 *                 period and counts nothing.
 *   lost signal   no pulse taken for PASS_LOST_PERIODS (3) periods or
 *                 PASS_LOST_MIN_US (100ms), whichever is longer.  A lost
 *                 pin reads width 0 and period 0; the gap that ends the loss
 *                 counts no missed frame.  A period over PASS_PERIOD_MAX_US
 *                 (1s) is not learnt.
 *
 * Stamps are the RP2350's 32-bit microsecond timer and wrap every 71.6
 * minutes; every difference is taken modulo 2^32.  A pin in the skip mask
 * (a bidirectional DShot output, whose own receiver decodes its line) keeps
 * no figure and counts nothing.
 *
 * The first state after pass_dec_init() or pass_dec_restart() gives the
 * levels and no edge: a pin already high starts no pulse.
 *
 * Pure C, no SDK (software development kit).  Host-tested in
 * test_pass_decode.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_PASS_DECODE_H
#define RCBENCH_PASS_DECODE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Channels: 9 inputs, 9 outputs. */
#define PASS_CHANNELS      9u
/** @brief Pins the capture samples, GPIO 16 to GPIO 33. */
#define PASS_PINS          18u
/** @brief The bits of a state that are pins. */
#define PASS_PIN_MASK      0x3FFFFu
/** @brief The state bit of input channel @p k (0 to 8). */
#define PASS_IN_BIT(k)     (2u * (unsigned)(k) + 1u)
/** @brief The state bit of output channel @p k (0 to 8). */
#define PASS_OUT_BIT(k)    (2u * (unsigned)(k))

/** @brief The shortest high taken as a pulse, us. */
#define PASS_PULSE_MIN_US  400u
/** @brief The longest high taken as a pulse, us. */
#define PASS_PULSE_MAX_US  2600u
/** @brief The shortest time without a pulse that is a lost signal, us. */
#define PASS_LOST_MIN_US   100000u
/** @brief Periods without a pulse that are a lost signal. */
#define PASS_LOST_PERIODS  3u
/** @brief The longest frame period learnt, us. */
#define PASS_PERIOD_MAX_US 1000000u

/** @brief One pin's state and figures. */
typedef struct {
    uint32_t rise_us;    /**< the rise of the high in progress */
    uint32_t frame_us;   /**< the rise of the last pulse taken */
    uint32_t ref_us;     /**< the lost time runs from here */
    uint32_t width_us;   /**< the last pulse, 0 for none or lost */
    uint32_t period_us;  /**< the frame period, 0 when not known */
    uint32_t odd_us;     /**< the last gap over 1.5 periods, 0 for none */
    uint32_t frames;     /**< pulses taken, modulo 2^32 */
    uint32_t missed;     /**< frames missed, modulo 2^32 */
    uint32_t glitches;   /**< highs not taken, modulo 2^32 */
    bool     rise_seen;  /**< a rise starts the high in progress */
    bool     have_frame; /**< frame_us holds a pulse's rise */
    bool     lost;       /**< no pulse for the lost time */
} pass_pin_t;

/** @brief The decoder. */
typedef struct {
    pass_pin_t pin[PASS_PINS]; /**< by state bit */
    uint32_t   level;          /**< the last state */
    uint32_t   skip;           /**< state bits not decoded */
    bool       started;        /**< level holds a state */
} pass_dec_t;

/**
 * @brief A decoder with no figure, the lost time of every pin starting at
 *        @p now_us.
 *
 * @param d       The decoder.
 * @param now_us  The microsecond timer now.
 */
void pass_dec_init(pass_dec_t *d, uint32_t now_us);

/**
 * @brief The pins not decoded from now on; a pin entering the mask loses
 *        its figures, one leaving it starts as after pass_dec_init().
 *
 * @param d       The decoder.
 * @param mask    State bits to skip.
 * @param now_us  The microsecond timer now.
 */
void pass_dec_skip(pass_dec_t *d, uint32_t mask, uint32_t now_us);

/**
 * @brief One state the capture pushed.
 *
 * @param d         The decoder.
 * @param state     The 18 pins after the change.
 * @param stamp_us  The microsecond timer when it was taken.
 */
void pass_dec_event(pass_dec_t *d, uint32_t state, uint32_t stamp_us);

/**
 * @brief The lost signals as of @p now_us.  Call after the states up to
 *        @p now_us: a stamp later than @p now_us counts as no time passed.
 *
 * @param d       The decoder.
 * @param now_us  The microsecond timer now.
 */
void pass_dec_now(pass_dec_t *d, uint32_t now_us);

/**
 * @brief Start again after states were lost: no high in progress, no gap
 *        from the last pulse, the lost time from @p now_us.  Widths,
 *        periods and counts stay.
 *
 * @param d       The decoder.
 * @param level   The newest state.
 * @param now_us  The microsecond timer now.
 */
void pass_dec_restart(pass_dec_t *d, uint32_t level, uint32_t now_us);

/**
 * @brief A pin's lost time with its period as it stands.
 *
 * @param p  The pin.
 * @return   us.
 */
uint32_t pass_dec_lost_us(const pass_pin_t *p);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PASS_DECODE_H */
