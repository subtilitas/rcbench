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
    bool     valid;     /**< a poll has answered at least once            */
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

#ifdef __cplusplus
}
#endif
