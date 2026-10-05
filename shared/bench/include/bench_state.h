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
     * LINK_BN_CHARGE_OK and LINK_BN_ENERGY_OK as this run has earned them.
     * Kept apart from flags, which the coprocessor rebuilds every sample,
     * and folded into it by bench_state_count_totals().
     */
    uint16_t totals;
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

/** A run begins: charge and energy start again from zero, uncounted. */
void bench_state_reset_totals(bench_state_t *b);

/** The longest step bench_state_count_totals() counts, in seconds. */
#define BENCH_TOTALS_MAX_STEP_S 1.0f

/**
 * Add @p dt_s of the live readings to the run's charge and energy.
 *
 * Only while @p driving, and only what the flags mark measured: charge from
 * a current, energy from a power that has both halves.  @p dt_s is clamped
 * to 0 .. BENCH_TOTALS_MAX_STEP_S, so a sampler that stalled does not count
 * its last reading across the whole stall.  Every call folds the run's
 * LINK_BN_CHARGE_OK and LINK_BN_ENERGY_OK into flags, driving or not, so the
 * totals stay readable after the run until the next one resets them.
 *
 * The totals are only as good as the current.  An ESC that reports a
 * current without measuring one -- no current sensor, an input left
 * floating -- is counted as faithfully as one that measures it.
 */
void bench_state_count_totals(bench_state_t *b, float dt_s, bool driving);

/**
 * A run's totals, carried across a change of the source that counts them.
 *
 * The panel shows the coprocessor's totals while the link is up and its own
 * model's while it is down, and each source counts from its own start: the
 * model from where it took over, the coprocessor from the arm it accepted
 * after its failsafe.  Without a carry the run's charge and energy would
 * jump back towards zero in the middle of one log.
 *
 * Reset when the run starts.  Taken from what is shown, just before a new
 * source first writes; added to every update after that, because each
 * source writes its own count over the field every time.
 */
typedef struct {
    float    mah;
    float    wh;
    uint16_t flags;   /**< LINK_BN_CHARGE_OK, LINK_BN_ENERGY_OK carried */
} bench_carry_t;

void bench_carry_reset(bench_carry_t *c);
void bench_carry_take(bench_carry_t *c, const bench_state_t *shown);
void bench_carry_apply(const bench_carry_t *c, bench_state_t *b);

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
