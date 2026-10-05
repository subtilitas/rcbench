/*
 * The bench readings, in the units a person reads: V, A, W, rpm (revolutions
 * per minute), degrees Celsius, mAh and Wh.
 *
 * Screens read this struct and never a register.  Filled from the link's
 * BENCH page the numbers are measured; filled from the simulator they are
 * modelled; the screen does not distinguish the two.
 *
 * Floats here, fixed-point on the wire: 16 bits with a documented scale is
 * the contract between the two firmwares.  The scales are in link_pages.h.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float voltage;      /**< V   */
    float current;      /**< A   */
    float power;        /**< W   */
    float rpm;
    float temp_esc;     /**< C   */
    float temp_motor;   /**< C   */
    float charge_mah;
    float energy_wh;

    /* Peaks and sag, tracked by the end that has the fast samples. */
    float voltage_min;
    float current_max;
    float power_max;
    float rpm_max;

    uint16_t flags;     /**< link_bench_flag_t                            */
    /**
     * Which of charge_mah and energy_wh the panel has counted this run
     * (BENCH_COUNTED_*).  Not on the wire: the panel counts both itself, see
     * bench_totals_t, and a total it has not counted is shown and logged as
     * absent rather than as 0.
     */
    uint8_t  counted;
    bool     valid;     /**< a poll has answered at least once            */
    /**
     * Whether voltage_min is a measurement yet.
     *
     * The sag floor is the one peak that cannot start at the live reading
     * when there is no live reading: a run whose first samples arrive before
     * the first voltage does would keep a floor of 0 V and reject every real
     * reading after it, and report a collapsed pack for the whole run.
     */
    bool     sag_seeded;
} bench_state_t;

/** True when the numbers are modelled rather than measured. */
static inline bool bench_state_simulated(const bench_state_t *b)
{
    return b != NULL && (b->flags & LINK_BN_SIMULATED) != 0;
}

/**
 * Decode a BENCH page into @p b.
 *
 * A short read fills what arrived and leaves the rest alone, so a host that
 * polls the first four registers at 20 Hz and the whole page once a second
 * gets a coherent state either way.
 */
void bench_state_from_regs(bench_state_t *b, const uint16_t *regs,
                           uint8_t offset, uint8_t count);

/** Encode into a BENCH page, for the coprocessor and for round-trip tests. */
void bench_state_to_regs(const bench_state_t *b, uint16_t *regs);

/** Clear the peaks without disturbing the live readings. */
void bench_state_reset_peaks(bench_state_t *b);

/** bench_state_t.counted: the run's charge has counted a measured current. */
#define BENCH_COUNTED_CHARGE 0x01u
/** bench_state_t.counted: the run's energy has counted voltage and current. */
#define BENCH_COUNTED_ENERGY 0x02u

/** The longest step bench_totals_count() counts, in seconds. */
#define BENCH_TOTALS_MAX_STEP_S 1.0f

/**
 * A run's charge and energy, counted by the panel.
 *
 * One count, of what the panel shows, whichever source that came from: the
 * coprocessor's readings while the link is up -- the ESC's own, over
 * extended DShot telemetry -- and the panel's model while it is down.  The
 * count does not change hands when the source does, so a run's totals never
 * go back within one log, and they outlast the run until the next arm
 * whatever happens to the link meanwhile.  The BENCH page's charge and energy
 * registers are not used: no coprocessor fills them yet.
 *
 * The totals are only as good as the current.  An ESC that reports a
 * current without measuring one -- no current sensor, an input left
 * floating -- is counted as faithfully as one that measures it.
 */
typedef struct {
    float   mah;
    float   wh;
    uint8_t counted;   /**< BENCH_COUNTED_* */
} bench_totals_t;

/** A run begins: nothing counted. */
void bench_totals_reset(bench_totals_t *t);

/**
 * Add @p dt_s of @p b's readings, while @p driving, and only what its flags
 * mark measured: charge from a current, energy from a power with both
 * halves.  @p dt_s is the time since the last call, clamped to
 * 0 .. BENCH_TOTALS_MAX_STEP_S, so a loop that stalled counts at most one
 * second of its last reading.
 */
void bench_totals_count(bench_totals_t *t, const bench_state_t *b,
                        float dt_s, bool driving);

/** Write the totals into @p b, over whatever its source put there. */
void bench_totals_show(const bench_totals_t *t, bench_state_t *b);

/**
 * Take the live readings into the peaks.
 *
 * Only what the flags mark valid: an empty field is not a measurement of
 * zero, and a sag floor or a current peak taken from one would stand for the
 * rest of the run. The floor is seeded by the first valid voltage rather than
 * by the reset, because a reset can happen before any voltage has arrived.
 */
void bench_state_track_peaks(bench_state_t *b);

#ifdef __cplusplus
}
#endif
