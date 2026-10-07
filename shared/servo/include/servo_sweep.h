/*
 * A servo swept through a curve: square, sine or triangle about its centre.
 *
 * The curve runs where the pins are, on the coprocessor, so its timing does
 * not depend on the link: a 5 Hz sine over a CAN round trip of tens of
 * milliseconds would be a staircase.  The panel computes the same curve with
 * the same code to draw the horn.
 *
 * One cycle, of period 1000 / mhz seconds plus the dwells, starts at the
 * centre and goes to +amplitude, holds there for dwell_ms, goes to
 * -amplitude, holds again and comes back to the centre, where the next cycle
 * carries on towards +amplitude.  A square jumps between the ends at the
 * cycle's start and half; a sine and a triangle start at the centre, so a
 * sweep begins without a jump.  An end is counted as a movement when the
 * curve reaches it -- a quarter cycle after the jump for a square.
 *
 * With a movement count the sweep ends once that many ends have been reached
 * and the last dwell is over, and the command goes back to the centre.
 *
 * Commands are in the bank's units, 0..OUT_SPAN of the channel's own travel,
 * with the centre at OUT_SPAN / 2.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SWEEP_OFF = 0,
    SWEEP_SQUARE,
    SWEEP_SINE,
    SWEEP_TRIANGLE,
    SWEEP_KIND_COUNT
} sweep_kind_t;

/** The slowest and fastest curve, in thousandths of a cycle a second. */
#define SWEEP_MHZ_MIN      50u
#define SWEEP_MHZ_MAX    5000u
/** The furthest from the centre a sweep goes, in command units. */
#define SWEEP_AMPLITUDE_MAX 500u
/** The longest hold at an end. */
#define SWEEP_DWELL_MAX_MS 5000u
/** The centre, in command units. */
#define SWEEP_CENTRE        500u

typedef struct {
    sweep_kind_t kind;
    uint16_t     mhz;        /**< cycles a second, x1000                   */
    uint16_t     amplitude;  /**< command units either side of the centre  */
    uint16_t     dwell_ms;   /**< held at each end                         */
    uint16_t     moves;      /**< ends to reach, 0 for no end              */
} sweep_cfg_t;

typedef struct {
    sweep_cfg_t cfg;
    uint32_t    start_ms;
    bool        running;
} sweep_t;

/** Whether @p cfg is one a sweep can run: a curve, a speed and an amplitude
 *  in range, and a dwell no longer than SWEEP_DWELL_MAX_MS. */
bool sweep_cfg_valid(const sweep_cfg_t *cfg);

/** Start @p cfg at @p now_ms.  False, and stopped, for a configuration
 *  sweep_cfg_valid() refuses. */
bool sweep_start(sweep_t *w, const sweep_cfg_t *cfg, uint32_t now_ms);

void sweep_stop(sweep_t *w);

/**
 * The command at @p now_ms into @p command.  True while the sweep runs;
 * false once it has stopped or made its movements, with @p command at the
 * centre.  The clock may wrap.
 */
bool sweep_step(sweep_t *w, uint32_t now_ms, uint16_t *command);

/** How many ends the sweep has reached by @p now_ms, the moves limit
 *  included once reached. */
uint32_t sweep_moves(const sweep_t *w, uint32_t now_ms);

/**
 * Whether a slew of @p slew_per_s command units a second is slower than the
 * fastest change @p cfg's curve asks for, so the slew and not the curve
 * decides how the output moves.  The fastest change, at amplitude A and
 * f = mhz / 1000 cycles a second, is 2 pi f A for a sine (at the centre)
 * and 4 f A for a triangle (throughout).  A square jumps, so any slew
 * limits it.  The dwell does not enter: it adds time at the ends, not to
 * the motion.
 *
 * A slew of 0 is no limit, as on an output channel, and limits nothing.
 * False for a configuration sweep_cfg_valid() refuses and for an amplitude
 * of 0, which asks for no change.
 */
bool sweep_slew_limited(const sweep_cfg_t *cfg, uint16_t slew_per_s);

#ifdef __cplusplus
}
#endif
