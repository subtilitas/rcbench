/**
 * @file
 * @brief The IOmcu's sensor service: what core 1 does each 1 ms tick with the
 * orders core 0 gives it, and the snapshot of what it read that core 0
 * publishes on the link.
 *
 * Core 0 owns the link pages and the outputs; core 1 owns the I2C
 * (Inter-Integrated Circuit) bus.  Two plain structs pass between them, each
 * copied whole under a lock by the firmware: sense_cmd_t from core 0 to core 1,
 * sense_snap_t back.  This module is core 1's side in pure C, so the host suite
 * runs it against fake_ina.h; the lock, the timer and the I2C block are the
 * firmware's (firmware/iomcu/src/sense_core1.c, sense_i2c.c).
 *
 * Each step, in this order:
 *
 * Set-up  cfg_gen moved: the bus is closed, then opened on the command's pins
 * when a part is enabled, and the schedule started afresh -- every part probed
 * again, the windows, the run and the capture empty.  A bus that does not open
 * reads closed and nothing is ticked. Run  run_gen moved: sense_sched_arm(),
 * the run's peaks and totals start again. Capture  cap_gen moved: armed with
 * the command's levels, or disarmed. An arm the schedule refuses -- no FAST
 * channel, its INA3221 not online, or no bus open -- ends at once as lost and
 * counts as a capture ended.  The count runs on across set-ups. Edge  edge_set
 * for this cap_gen, once: sense_sched_cap_edge(). Tick  sense_sched_tick().
 * Scan  at the first tick of an open bus, then every SENSE_RETRY_MS while an
 * enabled part is not online: every address from 0x40 to 0x4F that no online
 * part holds is asked for one byte.  Not on a stuck bus and not while a capture
 * is under way.  Each answer goes to sense_bus (sense_bus_note()): a held line,
 * or SENSE_STUCK_TIMEOUTS timeouts in a row, mark the bus stuck and end the
 * scan, and the recovery is due at the next tick, parts online or not; a NACK
 * is an empty address and no error.  An address that gave no answer keeps its
 * last one. An online part's address counts as answering without being asked: a
 * read at it would clear the flags its last register pointer holds.  16
 * addresses at 400 kHz are about 0.8 ms of bus time, in the tick they run in.
 * Encoder  with the AS5600 enabled, the bus is opened and the schedule reads
 * the part at 0x36 on the ticks that read no rotation item (sense_sched.h).
 * The scan does not look for it: its address is fixed and outside 0x40 to 0x4F,
 * and it counts for nothing in "a part is missing". Snapshot every field from
 * the schedule, with the generations the step has taken, so core 0 can tell a
 * reading of the set-up, run and capture in force from one that came before
 * them. FAST's window ring and the reset counts are the schedule's, and start
 * again with a set-up.
 *
 * Pure C, no SDK (software development kit).  Host-tested in test_sense_svc.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sense_bus.h"
#include "sense_sched.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The first address a scan asks, and how many.
 */
#define SENSE_SCAN_FIRST 0x40u
/**
 * @brief Address scan range of the sensor service.
 */
#define SENSE_SCAN_COUNT 16u

/**
 * @brief What core 0 asks for.
 *
 * Each generation moves when what it covers changes; core 1 acts on a
 * generation it has not seen.
 */
typedef struct {
    uint16_t cfg_gen;            /**< the bus's pins and the parts          */
    uint8_t  sda, scl;           /**< GPIO numbers, one I2C block's pair    */
    sense_sched_cfg_t parts;     /**< neither enabled: the bus is closed    */
    uint16_t run_gen;            /**< moves on each edge into driving       */
    uint16_t cap_gen;            /**< moves on each arm and each disarm     */
    bool     cap_on;             /**< cap_gen is an arm; false a disarm     */
    sense_cap_arm_t cap;         /**< the arm's levels                      */
    bool     edge_set;           /**< edge_us is cap_gen's PWM edge         */
    uint64_t edge_us;            /**< on the clock now_us() reads           */
    uint16_t limit_ma[SENSE_CHANNELS]; /**< each channel's current limit in
                                      mA, 0 none (SENSE_LIMIT): taken
                                      from the next window that closes,
                                      with no new set-up                 */
} sense_cmd_t;

/**
 * @brief What core 1 hands back after each step.
 */
typedef struct {
    uint16_t cfg_gen, run_gen, cap_gen;   /**< the generations taken     */
    bool     open;               /**< the bus runs on its pins              */
    bool     stuck;              /**< nothing is sent until a recovery     */
    uint64_t held;               /**< the pins the bus holds, a pin mask   */
    uint16_t errors;             /**< transactions failed, modulo 65536    */
    uint16_t present;            /**< bit n: 0x40 + n answered             */
    sense_state_t i228;
    sense_state_t i3221[SENSE_I3221_PARTS];  /**< UNSET when not fitted */
    uint16_t i228_maker, i228_device;     /**< the last identity read    */
    uint16_t i3221_maker[SENSE_I3221_PARTS], i3221_die[SENSE_I3221_PARTS];
    uint8_t  fast;               /**< the FAST channel in force, or
                                      SENSE_FAST_NONE                    */
    bool     have_temp, have_diag;
    int32_t  temp_mdegc;         /**< the INA228's die                      */
    uint16_t diag;               /**< its DIAG_ALRT as last read            */
    bool     have_win;           /**< a window has closed                   */
    sense_window_t win[SENSE_SRC_COUNT];
    /** FAST's last windows, newest first: entry k is the window numbered k
     *  before the number of win[]. */
    sense_ring_win_t ring[SENSE_WIN_RING];
    uint8_t  i228_resets;        /**< found reset under this set-up, mod 256 */
    uint8_t  i3221_resets[SENSE_I3221_PARTS];
    sense_run_t run;
    sense_cap_state_t cap_state;
    uint16_t cap_seq;            /**< captures ended, modulo 65536          */
    uint32_t cap_move_t;         /**< 0.1 ms                                */
    uint32_t cap_arrive_t;       /**< 0.1 ms                                */
    int32_t  cap_peak_ua, cap_mean_ua;
    uint32_t cap_samples;
    bool     cap_clipped;
    /* The output encoder (AS5600). */
    sense_state_t enc;
    bool     enc_have_angle;     /**< enc_raw is a reading of this set-up  */
    uint8_t  enc_status;         /**< STATUS as last read                  */
    uint8_t  enc_id;             /**< STATUS at the probe                  */
    uint16_t enc_raw;            /**< RAW ANGLE, 0 to 4095                 */
    uint16_t enc_samples;        /**< angle reads taken, modulo 65536      */
    uint16_t enc_still_ms;       /**< held within the tolerance, ms        */
    bool     enc_have_mag;
    uint8_t  enc_agc;
    uint16_t enc_magnitude;
    uint16_t over_n[SENSE_CHANNELS]; /**< each channel's windows over its
                                      limit, modulo 65536, since core 1
                                      started: across set-ups            */
} sense_snap_t;

/**
 * @brief The wire, the clock, the recovery and the pins.
 */
typedef struct {
    sense_sched_io_t sched;      /**< its ctx is passed to the three below */
    /** The I2C block on @p sda and @p scl at 400 kHz; false when those
     *  pins cannot carry it, and nothing is changed. */
    bool        (*open)(void *ctx, uint8_t sda, uint8_t scl);
    /** The block off and its pins let go. */
    void        (*close)(void *ctx);
    /** The address byte and one byte read back: SENSE_OK when @p addr
     *  acknowledged, SENSE_NACK when it did not, or a fault. */
    sense_err_t (*ask)(void *ctx, uint8_t addr);
} sense_svc_io_t;

/**
 * @brief The sensor service: its schedule and the command state it acts on.
 */
typedef struct {
    sense_svc_io_t io;
    bool     started;            /**< a command has been taken             */
    uint16_t cfg_gen, run_gen, cap_gen;
    bool     open;
    uint8_t  sda, scl;
    bool     edge_given;         /**< cap_gen's edge went to the schedule  */
    bool     refused;            /**< cap_gen's arm was refused            */
    uint16_t refusals;           /**< refused arms, modulo 65536           */
    uint16_t seq_base;           /**< captures ended under earlier set-ups */
    bool     scan_due;
    uint64_t scan_at_us;         /**< the next scan while a part is missing */
    uint16_t present;
    bool     lim_have;           /**< lim_win is a window judged            */
    uint16_t lim_win;            /**< the number of the last one judged    */
    uint16_t over_n[SENSE_CHANNELS];
    uint32_t setups;             /**< schedules set up, modulo 2^32: each
                                      one's sched.gen                     */
    sense_sched_t sched;
} sense_svc_t;

/**
 * @brief A service with the bus closed; the first step takes its command whole.
 *
 * @param v The service to set up.
 * @param io The callbacks the service calls.
 */
void sense_svc_init(sense_svc_t *v, const sense_svc_io_t *io);

/**
 * @brief One tick, every 1 ms: @p cmd acted on, the schedule ticked while the
 * bus is open, and @p out filled.
 *
 * @param v The service.
 * @param cmd The command to act on.
 * @param out Receives the snapshot.
 */
void sense_svc_step(sense_svc_t *v, const sense_cmd_t *cmd, sense_snap_t *out);

/**
 * @brief Whether window @p w of a channel passed @p limit_ma: its highest
 * current rounded to mA (as SENSE_WIN shows it) above the limit, or a sample
 * clipped at the top of the range when that top, @p clip_ua (the top code's
 * current, 163.8 mV across the shunt), is above the limit.
 *
 * Never with a limit of 0, and never for a negative current.  The service
 * judges each channel's window once, as it closes, on core 1, so no window is
 * missed however late core 0 takes the snapshot.
 *
 * @param w The window to judge.
 * @param clip_ua The current at the top code, in µA.
 * @param limit_ma The limit, in mA.
 * @return true when the window passed the limit.
 */
bool sense_window_over(const sense_window_t *w, int32_t clip_ua,
                       uint16_t limit_ma);

#ifdef __cplusplus
}
#endif
