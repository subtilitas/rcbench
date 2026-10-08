/*
 * The coprocessor's sensor service: what core 1 does each 1 ms tick with
 * the orders core 0 gives it, and the snapshot of what it read that core 0
 * publishes on the link.
 *
 * Core 0 owns the link pages and the outputs; core 1 owns the I2C
 * (Inter-Integrated Circuit) bus.  Two plain structs pass between them,
 * each copied whole under a lock by the firmware: sense_cmd_t from core 0
 * to core 1, sense_snap_t back.  This module is core 1's side in pure C,
 * so the host suite runs it against fake_ina.h; the lock, the timer and
 * the I2C block are the firmware's (firmware/iomcu/src/sense_core1.c,
 * sense_i2c.c).
 *
 * Each step, in this order:
 *
 *   Set-up   cfg_gen moved: the bus is closed, then opened on the
 *            command's pins when a part is enabled, and the schedule
 *            started afresh -- every part probed again, the windows, the
 *            run and the capture empty.  A bus that does not open reads
 *            closed and nothing is ticked.
 *   Run      run_gen moved: sense_sched_arm(), the run's peaks and totals
 *            start again.
 *   Capture  cap_gen moved: armed with the command's levels, or disarmed.
 *            An arm the schedule refuses -- the INA3221 not online, CH1
 *            not read, or no bus open -- ends at once as lost and counts
 *            as a capture ended.  The count runs on across set-ups.
 *   Edge     edge_set for this cap_gen, once: sense_sched_cap_edge().
 *   Tick     sense_sched_tick().
 *   Scan     at the first tick of an open bus, then every SENSE_RETRY_MS
 *            while an enabled part is not online: every address from 0x40
 *            to 0x4F that no online part holds is asked for one byte.  Not
 *            on a stuck bus and not while a capture is under way; a fault
 *            other than a NACK ends the scan, and the answers so far stand.
 *            An online part's address counts as answering without being
 *            asked: a read at it would clear the flags its last register
 *            pointer holds.  16 addresses at 400 kHz are about 0.8 ms of
 *            bus time, in the tick they run in.
 *   Snapshot every field from the schedule, with the generations the
 *            step has taken, so core 0 can tell a reading of the set-up,
 *            run and capture in force from one that came before them.
 *
 * Pure C, no SDK (software development kit).  Host-tested in
 * test_sense_svc.
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

/** The first address a scan asks, and how many. */
#define SENSE_SCAN_FIRST 0x40u
#define SENSE_SCAN_COUNT 16u

/** What core 0 asks for.  Each generation moves when what it covers
 *  changes; core 1 acts on a generation it has not seen. */
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
} sense_cmd_t;

/** What core 1 hands back after each step. */
typedef struct {
    uint16_t cfg_gen, run_gen, cap_gen;   /**< the generations taken     */
    bool     open;               /**< the bus runs on its pins              */
    bool     stuck;              /**< nothing is sent until a recovery     */
    uint64_t held;               /**< the pins the bus holds, a pin mask   */
    uint16_t errors;             /**< transactions failed, modulo 65536    */
    uint16_t present;            /**< bit n: 0x40 + n answered             */
    sense_state_t i228, i3221;
    uint16_t i228_maker, i228_device;     /**< the last identity read    */
    uint16_t i3221_maker, i3221_die;
    bool     have_temp, have_diag;
    int32_t  temp_mdegc;         /**< the INA228's die                      */
    uint16_t diag;               /**< its DIAG_ALRT as last read            */
    bool     have_win;           /**< a window has closed                   */
    sense_window_t win[SENSE_SRC_COUNT];
    sense_run_t run;
    sense_cap_state_t cap_state;
    uint16_t cap_seq;            /**< captures ended, modulo 65536          */
    uint32_t cap_move_t;         /**< 0.1 ms                                */
    uint32_t cap_arrive_t;       /**< 0.1 ms                                */
    int32_t  cap_peak_ua, cap_mean_ua;
    uint32_t cap_samples;
    bool     cap_clipped;
} sense_snap_t;

/** The wire, the clock, the recovery and the pins. */
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
    sense_sched_t sched;
} sense_svc_t;

/** A service with the bus closed; the first step takes its command
 *  whole. */
void sense_svc_init(sense_svc_t *v, const sense_svc_io_t *io);

/** One tick, every 1 ms: @p cmd acted on, the schedule ticked while the
 *  bus is open, and @p out filled. */
void sense_svc_step(sense_svc_t *v, const sense_cmd_t *cmd, sense_snap_t *out);

#ifdef __cplusplus
}
#endif
