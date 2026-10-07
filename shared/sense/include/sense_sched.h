/*
 * The coprocessor's sampling schedule for the sensor bus: what is read on
 * each 1 ms tick, the 50 ms windows made from it, the run's peaks and
 * totals, and the servo move capture on INA3221 CH1.
 *
 * Pure logic.  The wire is a sense_i2c_t and the time a callback, both
 * given at init, so the host suite runs it against fake_ina.h; the
 * coprocessor gives its I2C (Inter-Integrated Circuit) block and its 64-bit
 * microsecond timer.  Nothing here trips or disarms anything: the monitors
 * measure only.
 *
 * Each call of sense_sched_tick(), every 1 ms:
 *
 *   1. a bus recovery when sense_bus says one is due (the io's recover
 *      callback: 9 clocks on SCL, a STOP, the block re-initialised);
 *   2. each part's probe when due (sense_bus.h: at the start, and
 *      SENSE_RETRY_MS after it was found absent, wrong or offline);
 *   3. the INA228's accumulators cleared when sense_sched_arm() owes it;
 *   4. INA3221 CH1 current: 1000 Hz;
 *   5. INA3221 CH2 and CH3 current while sense_sched_fast_pair() is on:
 *      1000 Hz;
 *   6. INA228 CURRENT on even ticks, VBUS on odd ones: 500 Hz each;
 *   7. one slot of a SENSE_ROTATION-tick rotation, each 50 Hz: CH2 and CH3
 *      current (when not read in step 5), CH1 to CH3 bus voltage, INA228
 *      DIETEMP, DIAG_ALRT, ENERGY and CHARGE, INA3221 Mask/Enable;
 *   8. the move capture's clock.
 *
 * A channel the INA3221's set-up does not enable, and a part that is
 * disabled, not online or on a stuck bus, is not read: its windows stay
 * empty.  A stuck bus takes both parts offline within 3 ticks.  After a
 * recovery the bus counts as stuck until a transaction goes through, and
 * with both parts offline the next one is a probe: the flag can outlast a
 * freed bus by up to SENSE_RETRY_MS.  Bus time at 400 kHz, without the controller's own time between
 * transactions: 29.9 % of each tick in normal running, 52.7 % with the
 * pair at 1000 Hz (DESIGN §3.5).
 *
 * Windows.  Fixed SENSE_WINDOW_MS windows on the coprocessor clock,
 * counted from the first tick, each with its number.  For each source --
 * INA3221 CH1 to CH3 and the INA228 -- the mean, lowest and highest
 * current and the mean and lowest bus voltage.  A clipped current sample
 * carries no value: it adds nothing to the figures and sets the window's
 * clip flag for its end of the range.  Reading a window ends nothing: the
 * last complete window stays readable until the next one closes.  A tick
 * late by more than a window leaves the windows it missed out, so their
 * numbers are skipped.
 *
 * The run.  sense_sched_arm() is the edge into driving: the run's lowest
 * INA228 voltage and highest current and power start again, and the
 * INA228's ENERGY and CHARGE are cleared (CONFIG.RSTACC) on the next tick.
 * Power is each current sample times the voltage read 1 ms before it: a
 * step in both within that 1 ms pairs the new current with the old
 * voltage.
 * The totals are this run's while totals_ok holds: the clear went through
 * on that tick and the part has answered on every tick since.  A part not
 * online on that tick leaves the run without totals.  The totals start at
 * the clear, up to one tick after the arm, later when the clear's write
 * fails and is repeated.
 *
 * The move capture.  sense_sched_cap_arm() gives the move's levels;
 * sense_sched_cap_edge() gives the time of the PWM (pulse-width
 * modulation) frame that carries the new pulse, which the caller reads off
 * the slice's counter.  CH1 samples are then judged by servo_move.h's
 * rules at 1 kHz, times in 0.1 ms from that edge: the moving mean of
 * SENSE_CAP_FILTER_N samples, settled over SENSE_CAP_SETTLE_N samples, and
 * a window of SERVO_MOVE_TIMEOUT_MS plus SENSE_CAP_LAG_MS.  A clipped CH1
 * sample is at or past the full scale less a step: 1.6376 A on the 0.1 Ω
 * shunt.  The INA3221 leaving online ends a capture as lost.
 *
 * Not known: the INA3221's noise at 140 µs conversions, and so whether 4
 * samples of filter and 10 of settling suit it; the controller's time
 * between transactions.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ina228.h"
#include "ina3221.h"
#include "sense_bus.h"
#include "servo_move.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SENSE_WINDOW_MS      50u   /**< one window                        */
#define SENSE_ROTATION       20u   /**< ticks in the rotation: 50 Hz      */
/** The capture's filter: a moving mean of 4 samples, 4 ms at 1 kHz, which
 *  lags a step by 1.5 ms.  Chosen, not measured: the noise is not known. */
#define SENSE_CAP_FILTER_N    4u
/** Samples in a row at an end held harder than the servo moves: 10 ms at
 *  1 kHz. */
#define SENSE_CAP_SETTLE_N   10u
/** The capture's lag, added to the window: the filter's 1.5 ms, up to 1 ms
 *  from a conversion to its read, and the 140 µs conversion, rounded up. */
#define SENSE_CAP_LAG_MS      3u
/** The capture's time unit: 0.1 ms. */
#define SENSE_CAP_T_PER_MS   10u

/** Where a window's readings come from. */
typedef enum {
    SENSE_SRC_CH1 = 0,     /**< INA3221 CH1: the servo under test        */
    SENSE_SRC_CH2,         /**< INA3221 CH2 and CH3: the synchronised    */
    SENSE_SRC_CH3,         /**< pair                                     */
    SENSE_SRC_INA228,      /**< the ESC (electronic speed controller) path */
    SENSE_SRC_COUNT
} sense_src_t;

/** The wire, the clock and the bus recovery. */
typedef struct {
    sense_i2c_t i2c;
    uint64_t  (*now_us)(void *ctx);  /**< the coprocessor's µs timer     */
    void      (*recover)(void *ctx); /**< 9 clocks, a STOP, the block
                                          re-initialised; NULL: none     */
    void       *ctx;
} sense_sched_io_t;

/** What is fitted and how; a part not enabled is never addressed. */
typedef struct {
    bool     ina228_en;
    uint8_t  ina228_addr;
    uint32_t ina228_shunt_uohm;
    uint32_t ina228_max_ma;
    bool     ina3221_en;
    uint8_t  ina3221_addr;
    uint32_t ina3221_shunt_uohm;
    uint8_t  ina3221_channels;     /**< bit 0 CH1 to bit 2 CH3           */
} sense_sched_cfg_t;

/** One complete window of one source. */
typedef struct {
    uint16_t number;      /**< window number, modulo 65536                */
    uint16_t n_i;         /**< current samples with a value               */
    uint16_t n_v;         /**< voltage samples                            */
    bool     clip_hi;     /**< a current sample at the top of the range   */
    bool     clip_lo;     /**< one at the bottom                          */
    int32_t  i_mean_ua, i_min_ua, i_max_ua;   /**< 0 with n_i 0           */
    int32_t  v_mean_uv, v_min_uv;             /**< 0 with n_v 0           */
} sense_window_t;

/** A window being filled. */
typedef struct {
    int64_t  i_sum, v_sum;
    uint16_t n_i, n_v;
    bool     clip_hi, clip_lo;
    int32_t  i_min, i_max, v_min;
} sense_acc_t;

/** The run since sense_sched_arm(), from the INA228. */
typedef struct {
    bool     have_v;      /**< v_min_uv holds a sample                    */
    bool     have_i;      /**< i_max_ua holds a sample                    */
    bool     have_p;      /**< p_max_uw holds a sample                    */
    bool     i_clipped;   /**< a clipped current: the peaks lack it       */
    int32_t  v_min_uv;
    int32_t  i_max_ua;
    int64_t  p_max_uw;    /**< a current times the voltage read before it */
    bool     clear_owed;  /**< the accumulators are to be cleared         */
    bool     totals_ok;   /**< energy and charge are this run's           */
    uint64_t energy_mj;   /**< ENERGY as last read                        */
    int64_t  charge_uc;   /**< CHARGE as last read                        */
} sense_run_t;

/** The move capture's state, in the order a capture passes through. */
typedef enum {
    SENSE_CAP_IDLE = 0,
    SENSE_CAP_ARMED,      /**< levels given, waiting for the edge         */
    SENSE_CAP_WAITING,    /**< edge given, no movement yet                */
    SENSE_CAP_MOVING,
    SENSE_CAP_ARRIVED,
    SENSE_CAP_SETTLED,    /**< arrived at an end held harder than it moves */
    SENSE_CAP_LATE,       /**< movement, no arrival in the window         */
    SENSE_CAP_UNSEEN,     /**< no movement in the window                  */
    SENSE_CAP_LOST,       /**< the INA3221 stopped answering              */
} sense_cap_state_t;

/** A capture's levels, µA. */
typedef struct {
    int32_t rise_ua;      /**< the level before the command               */
    int32_t hold_ua;      /**< the destination's holding level            */
    int32_t move_ua;      /**< the threshold                              */
    int32_t band_ua;      /**< the arrival band                           */
} sense_cap_arm_t;

typedef struct {
    sense_cap_state_t state;
    uint16_t seq;         /**< captures ended, modulo 65536               */
    sense_cap_arm_t arm;
    uint32_t edge_t;      /**< the edge, 0.1 ms                           */
    /* Once over: */
    uint32_t move_t;      /**< edge to movement, 0.1 ms, when ARRIVED,
                               SETTLED or LATE; 0 otherwise               */
    uint32_t arrive_t;    /**< edge to arrival, 0.1 ms, when ARRIVED or
                               SETTLED; 0 otherwise                       */
    int32_t  peak_ua;     /**< highest filtered sample of the move        */
    int32_t  mean_ua;     /**< mean of the move's filtered samples        */
    uint32_t samples;     /**< filtered samples with a value in the move  */
    bool     clipped;     /**< a clipped sample: peak and mean lack it    */
    servo_move_t mv;
} sense_cap_t;

typedef struct {
    sense_sched_io_t    io;
    sense_sched_cfg_t   cfg;
    sense_bus_t         bus;
    ina228_t            i228;
    ina3221_t           i3221;
    ina228_setup_err_t  i228_setup;   /**< INA228_SETUP_OK when disabled  */
    ina3221_setup_err_t i3221_setup;
    int32_t  ch_clip_ua;     /**< a clipped INA3221 sample is at least this */
    uint32_t ticks;
    bool     started;
    uint32_t t0_ms;          /**< the first tick                          */
    uint32_t win;            /**< the window being filled, from t0_ms     */
    sense_acc_t    acc[SENSE_SRC_COUNT];
    sense_window_t last[SENSE_SRC_COUNT];
    bool     have_last;
    bool     fast_pair;
    bool     have_vbus;      /**< vbus_uv holds the INA228's last voltage */
    int32_t  vbus_uv;
    bool     have_temp, have_diag, have_flags;
    int32_t  temp_mdegc;     /**< INA228 DIETEMP as last read             */
    uint16_t diag;           /**< INA228 DIAG_ALRT as last read           */
    uint16_t flags;          /**< INA3221 Mask/Enable as last read        */
    sense_run_t run;
    sense_cap_t cap;
} sense_sched_t;

/** A schedule over @p io for the parts @p cfg enables.  A part whose
 *  set-up the driver refuses is left unset, never addressed, and its
 *  error kept in i228_setup or i3221_setup. */
void sense_sched_init(sense_sched_t *s, const sense_sched_io_t *io,
                      const sense_sched_cfg_t *cfg);

/** One tick: every 1 ms. */
void sense_sched_tick(sense_sched_t *s);

/** CH2 and CH3 at 1000 Hz while a two-servo synchronisation runs. */
void sense_sched_fast_pair(sense_sched_t *s, bool on);

/** The edge into driving: the run's peaks start again and the INA228's
 *  totals are cleared on the next tick. */
void sense_sched_arm(sense_sched_t *s);

/** The last complete window of @p src; false before one has closed. */
bool sense_sched_window(const sense_sched_t *s, sense_src_t src,
                        sense_window_t *out);

/** Arm a capture on CH1 with @p levels.  Refused, and nothing changed,
 *  while the INA3221 is not online or CH1 is not enabled. */
bool sense_sched_cap_arm(sense_sched_t *s, const sense_cap_arm_t *levels);

/** The PWM frame carrying the new pulse starts at @p edge_us, on the clock
 *  now_us() reads; it may lie ahead.  Nothing unless a capture is armed. */
void sense_sched_cap_edge(sense_sched_t *s, uint64_t edge_us);

/** Stop a capture; its count stays. */
void sense_sched_cap_disarm(sense_sched_t *s);

#ifdef __cplusplus
}
#endif
