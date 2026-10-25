/**
 * @file
 * @brief The bench readings, in the units a person reads: V, A, W, rpm
 *        (revolutions per minute), degrees Celsius, mAh and Wh.
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

/** @brief The INA3221 channels SENSE_WIN_A and SENSE_WIN_B carry (4.25). */
#define BENCH_CHW_CHANNELS 12u

/**
 * @brief One 50ms window of every INA3221 channel, from SENSE_WIN_A and
 *        SENSE_WIN_B (protocol 4.25), for the log's columns per watched
 *        target: channel c = 3 a + CHn - 1 of the part at 0x40 + a.
 *
 * Bit c of ok says channel c's window holds readings and was read under the
 * window number window; of no_current that it holds no current sample, of
 * no_voltage no bus voltage sample: those figures read 0 and are not readings.
 * Mean and highest current in mA, signed; lowest bus voltage in mV.
 */
typedef struct {
    uint16_t window;
    uint16_t ok;
    uint16_t no_current;
    uint16_t no_voltage;
    int16_t  mean_ma[BENCH_CHW_CHANNELS];
    int16_t  max_ma[BENCH_CHW_CHANNELS];
    uint16_t min_mv[BENCH_CHW_CHANNELS];
} bench_chw_t;

/**
 * @brief The bench readings in the units a person reads: V, A, W and rpm, with
 *        their peaks.
 */
typedef struct {
    float voltage;      /**< V   */
    float current;      /**< A   */
    float power;        /**< W   */
    float rpm;                          /**< motor speed, rpm */
    float temp_esc;     /**< C   */
    float temp_motor;   /**< C   */
    float charge_mah;                   /**< charge drawn, mAh */
    float energy_wh;                    /**< energy drawn, Wh */

    /* Peaks and sag, tracked by the end that has the fast samples. */
    float voltage_min;
    float current_max;
    float power_max;
    float rpm_max;

    uint16_t flags;     /**< link_bench_flag_t                            */
    /**
     * @brief Which of charge_mah and energy_wh the panel has counted this run
     *        (BENCH_COUNTED_*).
     *
     * Not on the wire: the panel counts both itself, see
     * bench_totals_t, and a total it has not counted is shown and logged as
     * absent rather than as 0.
     */
    uint8_t  counted;
    bool     valid;     /**< a poll has answered at least once            */
    /**
     * @brief Whether voltage_min is a measurement yet.
     *
     * The sag floor is the one peak that cannot start at the live reading
     * when there is no live reading: a run whose first samples arrive before
     * the first voltage does would keep a floor of 0V and reject every real
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
    /**
     * @brief The INA228 read its current at the end of its range in the last
     *        window or since the arm (SENSE FLAGS bit 3): current and power, or
     *        their peaks, are bounds and not values.
     */
    bool     clipped;

    /*
     * One 50ms window of the INA3221's channels, from SERVO_WIN (CH1) and
     * SERVO_SENSE, for the log: servo_new while it has not been written to
     * a row, so each window reaches the log once, keyed by its number.
     * Per channel the mean and highest current in mA, signed, and the
     * lowest bus voltage in mV, as the page carries them; bit n-1 of
     * servo_ok says CHn's window holds readings.  Bit n-1 of
     * servo_no_current says it holds no current sample, and of
     * servo_no_voltage no voltage sample: the figures of that quantity
     * read 0 and are not readings, and the log leaves their cells empty.
     * SERVO_WIN says which for CH1; SERVO_SENSE's flags do not, and both
     * stay 0 for a channel read from it.  Not on the BENCH page.
     */
    bool     servo_new;
    uint8_t  servo_ok;
    uint8_t  servo_no_current;
    uint8_t  servo_no_voltage;
    uint16_t servo_window;
    int16_t  servo_mean_ma[3];
    int16_t  servo_max_ma[3];
    uint16_t servo_min_mv[3];
    /**
     * @brief The same window of every channel the map reads, ok 0 for none: on
     *        the row of servo_window, as SERVO_SENSE's CH2 and CH3 are.
     */
    bench_chw_t chw;
} bench_state_t;

/**
 * @brief True when the numbers are modelled rather than measured.
 *
 * @param b The state to test.
 * @return true when the numbers are modelled (LINK_BN_SIMULATED), not measured.
 */
static inline bool bench_state_simulated(const bench_state_t *b)
{
    return b != NULL && (b->flags & LINK_BN_SIMULATED) != 0;
}

/**
 * @brief Decode a BENCH page into @p b.
 *
 * A short read fills what arrived and leaves the rest alone, so a host that
 * polls the first four registers at 20Hz and the whole page once a second
 * gets a coherent state either way.
 *
 * @param b      The state the registers are decoded into.
 * @param regs   The page registers, count entries from offset.
 * @param offset The page register that regs[0] holds.
 * @param count  The number of entries in regs.
 */
void bench_state_from_regs(bench_state_t *b, const uint16_t *regs,
                           uint8_t offset, uint8_t count);

/**
 * @brief Encode into a BENCH page, for the IOmcu and for round-trip tests.
 *
 * @param b    The state to encode.
 * @param regs The page to fill, LINK_BN_COUNT registers.
 */
void bench_state_to_regs(const bench_state_t *b, uint16_t *regs);

/**
 * @brief A run starts: the totals of the last one go, in @p b and in the BENCH
 *        page @p regs built from it -- charge and energy 0 and
 *        LINK_BN_TOTALS_OK clear -- and every other register stays.
 *
 * For the IOmcu, on the edge into driving: it rebuilds the page at its 50Hz
 * sample, and until the next one a panel reading the page would see the last
 * run's totals marked as this run's.  Either pointer may be NULL.
 *
 * @param b    The state to update, or NULL.
 * @param regs The page to update, or NULL.
 */
void bench_state_run_starts(bench_state_t *b, uint16_t *regs);

/**
 * @brief One row of the bench log from @p b, into @p out.
 *
 * With @p whole the sample as it is.  Without, the INA3221's window alone --
 * servo_new, the servo_ fields and chw -- and every other field 0 with no flag
 * set, which the log writes as empty cells: the row of a window that closed
 * before the newest one of its pass.
 *
 * @param b     The state to take the row from.
 * @param whole true for the whole sample, false for the INA3221 window alone.
 * @param out   Where the row is written.
 */
void bench_state_log_row(const bench_state_t *b, bool whole,
                         bench_state_t *out);

/**
 * @brief Clear the peaks without disturbing the live readings.
 *
 * @param b The state whose peaks are cleared.
 */
void bench_state_reset_peaks(bench_state_t *b);

/**
 * @brief The ESC's own figures while the INA228 is the source: from the SENSE
 *        page's ESC registers, @p v_ok and @p i_ok their valid bits, and
 *        whether the INA228 reads clipped.
 *
 * All clear while it is not the source.
 *
 * @param b       The state to write.
 * @param v_ok    true when volts is valid.
 * @param volts   The ESC voltage in V.
 * @param i_ok    true when amps is valid.
 * @param amps    The ESC current in A.
 * @param clipped true when the INA228 reads clipped.
 */
void bench_state_set_esc(bench_state_t *b, bool v_ok, float volts,
                         bool i_ok, float amps, bool clipped);

/**
 * @brief The ESC's own voltage and current, whichever page carries them: the
 *        SENSE page's while the INA228 is BENCH's source, BENCH's own
 *        otherwise.
 *
 * False, and @p out untouched, when the ESC reported none -- and for
 * modelled numbers (LINK_BN_SIMULATED), which no ESC reported.
 *
 * @param b   The state to read.
 * @param out Receives the voltage in V; untouched when false.
 * @return true when an ESC voltage is available.
 */
bool bench_state_esc_voltage(const bench_state_t *b, float *out);
/**
 * @brief The ESC's own current, from the same source as
 *         bench_state_esc_voltage().
 *
 * @param b   The state to read.
 * @param out Receives the current in A; untouched when false.
 * @return true when an ESC current is available.
 */
bool bench_state_esc_current(const bench_state_t *b, float *out);

/**
 * @brief The INA228's own voltage and current: BENCH's while it is the source.
 *
 * False otherwise.
 *
 * @param b   The state to read.
 * @param out Receives the voltage in V; untouched when false.
 * @return true when the INA228 voltage is valid.
 */
bool bench_state_ina_voltage(const bench_state_t *b, float *out);
/**
 * @brief The INA228's own current: BENCH's while it is the source.
 *
 * @param b   The state to read.
 * @param out Receives the current in A; untouched when false.
 * @return true when the INA228 current is valid.
 */
bool bench_state_ina_current(const bench_state_t *b, float *out);

/**
 * @brief How far the SENSE page's totals may lie from BENCH's and be the same
 *        count: half a register step of rounding, and what a few milliseconds
 *        between the two reads adds at full current.
 */
#define BENCH_FINE_MAH_TOL 2.0f
#define BENCH_FINE_WH_TOL  0.2f

/**
 * @brief The most BENCH's charge (1mAh) and energy (0.1Wh) registers carry: a
 *        run past them reads the ceiling there, and only SENSE's 32-bit totals
 *        go on.
 */
#define BENCH_CHARGE_MAH_MAX 65535.0f
#define BENCH_ENERGY_WH_MAX   6553.5f

/**
 * @brief With LINK_BN_TOTALS_OK, the INA228's totals in the SENSE page's finer
 *        steps -- @p charge_cmah in 0.01mAh, @p energy_cwh in 0.01Wh, read in
 *        the same poll -- over BENCH's 1mAh and 0.1Wh, each where it agrees
 *        with BENCH's within the tolerance above, or where BENCH reads its
 *        ceiling and the finer figure is at or above it.
 *
 * Nothing changes without TOTALS_OK.
 *
 * @param b           The state whose totals are set.
 * @param charge_cmah The INA228 charge in 0.01mAh.
 * @param energy_cwh  The INA228 energy in 0.01Wh.
 */
void bench_state_fine_totals(bench_state_t *b, int32_t charge_cmah,
                             uint32_t energy_cwh);

/**
 * @brief bench_state_t.counted: the run's charge has counted a measured
 *        current.
 */
#define BENCH_COUNTED_CHARGE 0x01u
/**
 * @brief bench_state_t.counted: the run's energy has counted voltage and
 *        current.
 */
#define BENCH_COUNTED_ENERGY 0x02u

/** @brief The longest step bench_totals_count() counts, in seconds. */
#define BENCH_TOTALS_MAX_STEP_S 1.0f

/**
 * @brief A run's charge and energy, counted by the panel.
 *
 * One count, of what the panel shows, whichever source that came from: the
 * IOmcu's readings while the link is up -- the ESC's own, over
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
 * @brief How long into a run the INA228's totals are not taken, in seconds.
 *
 * An IOmcu builds the BENCH page from its 50Hz sample, so one that does not
 * clear the page on the arm (bench_state_run_starts()) can carry the last run's
 * totals with LINK_BN_TOTALS_OK for up to 20ms after it; the panel counts on
 * its own until five such samples have passed.
 */
#define BENCH_TOTALS_SETTLE_S 0.1f

/**
 * @brief A run begins: nothing counted.
 *
 * @param t The totals to clear.
 */
void bench_totals_reset(bench_totals_t *t);

/**
 * @brief Add @p dt_s of @p b's readings, while @p driving, and only what its
 *        flags mark measured: charge from a current, energy from a power with
 *        both halves.
 *
 * @p dt_s is the time since the last call, clamped to
 * 0 .. BENCH_TOTALS_MAX_STEP_S, so a loop that stalled counts at most one
 * second of its last reading.  With LINK_BN_TOTALS_OK in @p b's flags and
 * BENCH_TOTALS_SETTLE_S of the run counted, the count is @p b's own charge
 * and energy instead, both counted.
 *
 * @param t       The running totals.
 * @param b       The readings of this sample.
 * @param dt_s    The time since the last call in s, clamped to 0 ..
 *                BENCH_TOTALS_MAX_STEP_S.
 * @param driving true while the bench is driving.
 */
void bench_totals_count(bench_totals_t *t, const bench_state_t *b,
                        float dt_s, bool driving);

/**
 * @brief Write the totals into @p b, over whatever its source put there.
 *
 * @param t The totals to show.
 * @param b The state that receives them.
 */
void bench_totals_show(const bench_totals_t *t, bench_state_t *b);

/**
 * @brief Take the live readings into the peaks.
 *
 * Only what the flags mark valid: an empty field is not a measurement of
 * zero, and a sag floor or a current peak taken from one would stand for the
 * rest of the run. The floor is seeded by the first valid voltage rather than
 * by the reset, because a reset can happen before any voltage has arrived.
 *
 * @param b The state whose peaks are updated.
 */
void bench_state_track_peaks(bench_state_t *b);

/*
 * How stale a speed may be before it stops being reported.
 *
 * Frames go out at a kilohertz, so a reply older than this is not a slow
 * update but an ESC that has stopped answering -- unplugged, or one that
 * never did bidirectional DShot.  A held-over speed on a stopped motor is
 * exactly the plausible wrong number this bench exists to avoid.
 */
#define BENCH_RPM_STALE_MS  200u

/*
 * How old an extended-telemetry reading may be before the field goes empty.
 *
 * Longer than BENCH_RPM_STALE_MS because the rates are not the same: an ESC
 * answers every frame with a period and interleaves temperature, voltage and
 * current between them a few times a second.  Holding those to the speed's
 * window would blank fields that are arriving exactly as the protocol sends
 * them.
 */
#define BENCH_EDT_STALE_MS  2000u

/** @brief One extended-telemetry reading as the DShot driver holds it. */
typedef struct {
    bool     ok;        /**< the driver has one (outputs_hw_edt() true)  */
    uint16_t count;     /**< the frame's payload, dshot_edt_*() units     */
    uint32_t age_ms;    /**< how long ago it arrived                      */
} bench_edt_reading_t;

/**
 * @brief The ESC's telemetry as the IOmcu reads it at a sample: raw, with each
 *        reading's age and whether there is one.
 */
typedef struct {
    uint16_t poles;         /**< MOTOR_POLES: magnets, not pole pairs     */
    bool     erpm_ok;       /**< the driver has a speed (outputs_hw_erpm()) */
    uint32_t erpm;          /**< electrical rpm                            */
    uint32_t erpm_age_ms;               /**< age of the speed, ms; stale past BENCH_RPM_STALE_MS */
    bench_edt_reading_t voltage;        /**< voltage reading */
    bench_edt_reading_t current;        /**< current reading */
    bench_edt_reading_t temperature;    /**< temperature reading */
} bench_telem_t;

/**
 * @brief The live readings of @p b and their valid bits from @p t, for the
 *        IOmcu's sample.
 *
 * voltage, current, power, rpm, temp_esc, temp_motor and flags are rebuilt; the
 * peaks and every other field stay.
 *
 * rpm with LINK_BN_RPM_OK from a speed no older than BENCH_RPM_STALE_MS
 * and poles not 0; voltage, current and temp_esc with LINK_BN_VOLTAGE_OK,
 * LINK_BN_CURRENT_OK and LINK_BN_TEMP_OK from readings no older than
 * BENCH_EDT_STALE_MS, in the units of dshot_edt_*(); power, the product,
 * only with both halves.  LINK_BN_TEMP_MOT_OK is never set.
 *
 * @param b The state that is written.
 * @param t The telemetry sample that is read.
 */
void bench_state_from_telem(bench_state_t *b, const bench_telem_t *t);

#ifdef __cplusplus
}
#endif
