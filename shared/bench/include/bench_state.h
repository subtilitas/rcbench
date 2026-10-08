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

    /*
     * The ESC's own telemetry while the INA228 is BENCH's source
     * (LINK_BN_SENSED), from the SENSE page: voltage and current, each
     * valid with its bit in esc_ok (LINK_SN_ESC_VOLTAGE_OK,
     * LINK_SN_ESC_CURRENT_OK).  Not on the BENCH page; without SENSED the
     * BENCH numbers are the ESC's and these are not used.
     */
    float    esc_voltage;   /**< V */
    float    esc_current;   /**< A */
    uint8_t  esc_ok;
    /** The INA228 read its current at the end of its range in the last
     *  window or since the arm (SENSE FLAGS bit 3): current and power, or
     *  their peaks, are bounds and not values. */
    bool     clipped;

    /*
     * One 50 ms window of the INA3221's channels, from SERVO_SENSE, for
     * the log: servo_new while it has not been written to a row, so each
     * window reaches the log once, keyed by its number.  Per channel the
     * mean and highest current in mA, signed, and the lowest bus voltage
     * in mV, as the page carries them; bit n-1 of servo_ok says CHn's
     * window holds readings.  Not on the BENCH page.
     */
    bool     servo_new;
    uint8_t  servo_ok;
    uint16_t servo_window;
    int16_t  servo_mean_ma[3];
    int16_t  servo_max_ma[3];
    uint16_t servo_min_mv[3];
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

/**
 * A run starts: the totals of the last one go, in @p b and in the BENCH
 * page @p regs built from it -- charge and energy 0 and LINK_BN_TOTALS_OK
 * clear -- and every other register stays.  For the coprocessor, on the
 * edge into driving: it rebuilds the page at its 50 Hz sample, and until
 * the next one a panel reading the page would see the last run's totals
 * marked as this run's.  Either pointer may be NULL.
 */
void bench_state_run_starts(bench_state_t *b, uint16_t *regs);

/** Clear the peaks without disturbing the live readings. */
void bench_state_reset_peaks(bench_state_t *b);

/**
 * The ESC's own figures while the INA228 is the source: from the SENSE
 * page's ESC registers, @p v_ok and @p i_ok their valid bits, and whether
 * the INA228 reads clipped.  All clear while it is not the source.
 */
void bench_state_set_esc(bench_state_t *b, bool v_ok, float volts,
                         bool i_ok, float amps, bool clipped);

/**
 * The ESC's own voltage and current, whichever page carries them: the
 * SENSE page's while the INA228 is BENCH's source, BENCH's own otherwise.
 * False, and @p out untouched, when the ESC reported none -- and for
 * modelled numbers (LINK_BN_SIMULATED), which no ESC reported.
 */
bool bench_state_esc_voltage(const bench_state_t *b, float *out);
bool bench_state_esc_current(const bench_state_t *b, float *out);

/** The INA228's own voltage and current: BENCH's while it is the source.
 *  False otherwise. */
bool bench_state_ina_voltage(const bench_state_t *b, float *out);
bool bench_state_ina_current(const bench_state_t *b, float *out);

/** How far the SENSE page's totals may lie from BENCH's and be the same
 *  count: half a register step of rounding, and what a few milliseconds
 *  between the two reads adds at full current. */
#define BENCH_FINE_MAH_TOL 2.0f
#define BENCH_FINE_WH_TOL  0.2f

/** The most BENCH's charge (1 mAh) and energy (0.1 Wh) registers carry: a
 *  run past them reads the ceiling there, and only SENSE's 32-bit totals
 *  go on. */
#define BENCH_CHARGE_MAH_MAX 65535.0f
#define BENCH_ENERGY_WH_MAX   6553.5f

/**
 * With LINK_BN_TOTALS_OK, the INA228's totals in the SENSE page's finer
 * steps -- @p charge_cmah in 0.01 mAh, @p energy_cwh in 0.01 Wh, read in
 * the same poll -- over BENCH's 1 mAh and 0.1 Wh, each where it agrees
 * with BENCH's within the tolerance above, or where BENCH reads its
 * ceiling and the finer figure is at or above it.  Nothing changes without
 * TOTALS_OK.
 */
void bench_state_fine_totals(bench_state_t *b, int32_t charge_cmah,
                             uint32_t energy_cwh);

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
 * whatever happens to the link meanwhile.
 *
 * While BENCH carries LINK_BN_TOTALS_OK, the INA228 counts: its charge and
 * energy since the run's arm, accumulated in the part at every conversion,
 * become the count, and the panel adds nothing of its own.  When the bit
 * goes -- the part stopped answering -- the count goes on from the last
 * total the part gave, from the readings shown, so it neither restarts nor
 * goes back.
 *
 * The totals are only as good as the current.  An ESC that reports a
 * current without measuring one -- no current sensor, an input left
 * floating -- is counted as faithfully as one that measures it.
 */
typedef struct {
    float   mah;
    float   wh;
    uint8_t counted;   /**< BENCH_COUNTED_* */
    float   run_s;     /**< driving time counted since the reset */
} bench_totals_t;

/**
 * How long into a run the INA228's totals are not taken, in seconds.  A
 * coprocessor builds the BENCH page from its 50 Hz sample, so one that does
 * not clear the page on the arm (bench_state_run_starts()) can carry the
 * last run's totals with LINK_BN_TOTALS_OK for up to 20 ms after it; the
 * panel counts on its own until five such samples have passed.
 */
#define BENCH_TOTALS_SETTLE_S 0.1f

/** A run begins: nothing counted. */
void bench_totals_reset(bench_totals_t *t);

/**
 * Add @p dt_s of @p b's readings, while @p driving, and only what its flags
 * mark measured: charge from a current, energy from a power with both
 * halves.  @p dt_s is the time since the last call, clamped to
 * 0 .. BENCH_TOTALS_MAX_STEP_S, so a loop that stalled counts at most one
 * second of its last reading.  With LINK_BN_TOTALS_OK in @p b's flags and
 * BENCH_TOTALS_SETTLE_S of the run counted, the count is @p b's own charge
 * and energy instead, both counted.
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
