/*
 * The IOmcu's sampling schedule for the sensor bus: what is read on
 * each 1 ms tick, the 50 ms windows made from it, the run's peaks and
 * totals, and the servo move capture on the FAST channel.
 *
 * Pure logic.  The wire is a sense_i2c_t and the time a callback, both
 * given at init, so the host suite runs it against fake_ina.h; the
 * IOmcu gives its I2C (Inter-Integrated Circuit) block and its 64-bit
 * microsecond timer.  Nothing here trips or disarms anything: the monitors
 * measure only.
 *
 * The parts.  Up to SENSE_I3221_PARTS INA3221 at 0x40 to 0x43, part a at
 * 0x40 + a, three channels each: channel c (0 to 11) is part c / 3's
 * CH(c % 3 + 1), SENSE_CH(a, ch).  Each channel has its own shunt, and a
 * shunt of 0 leaves the channel off.  One channel, FAST, is read every
 * tick; its windows go into the ring and the move capture runs on it.
 * Beside them one INA228 and the AS5600 output encoder.
 *
 * Each call of sense_sched_tick(), every 1 ms: a bus recovery when
 * sense_bus says one is due (the io's recover callback: 9 clocks on SCL, a
 * STOP, the block re-initialised); a part's probe when due (at the start,
 * and SENSE_RETRY_MS after it was found absent, wrong or offline), one
 * INA3221 probe a tick at most; the INA228's accumulators cleared when
 * sense_sched_arm() owes it; then the reads, on a 400 kHz bus:
 *
 *   Read                                              Rate
 *   FAST channel current                              1000 Hz
 *   every other channel's current, one a tick         1000 / n Hz for n
 *                                                     (90.9 Hz with 11)
 *   INA228 CURRENT, VBUS, alternating                 500 Hz each
 *   every channel's bus voltage, one an even tick     41.7 Hz each
 *   INA228 DIAG_ALRT, ENERGY, CHARGE                  50 Hz each
 *   INA228 DIETEMP, ADC_CONFIG; each INA3221's
 *   Mask/Enable and Configuration                     25 Hz each
 *
 * A transaction of N bytes is 9 * (3 + N) + 3 clocks, 2.5 µs each at
 * 400 kHz: a 2-byte read 120 µs, the INA228's 3-byte CURRENT and VBUS
 * 142.5 µs, its 5-byte ENERGY and CHARGE 187.5 µs.  Every tick reads
 * FAST, one other channel and one INA228 register; an even tick then one
 * bus voltage and one item of the rotation (SENSE_ROTATION slots, 40 ms:
 * eight for the INA228, eight for the INA3221s' Mask/Enable and
 * Configuration, four left empty); an odd tick the encoder.  An even tick
 * is at most 690 µs of bus time, 742.5 µs in the tick in which a set-up
 * register is read a second time; an odd tick 600 µs, 720 µs in an
 * encoder field slot.  Bus time over a second, arithmetic only, with 4
 * parts and all 12 channels: 49.7 % without the encoder, 60.8 % with it;
 * with one part reading CH1 and the INA228, 30.4 %.  Not counted: the
 * controller's own time between transactions, not measured.  The bus
 * runs at 400 kHz only: at 100 kHz a read takes 480 to 750 µs, and the
 * tick does not fit.  A probe is 10 transactions on the INA228, 1.1 ms,
 * and 4 on the INA3221, 455 µs: the ticks it covers run late.
 *
 * A part that reset itself.  Both kinds answer every read after a reset
 * of their own (supply under about 1.26 V at the INA228, SLYS021A §7.4.2),
 * on their power-on set-up: the INA228 at ADCRANGE 0, where a set-up at
 * ADCRANGE 1 reads a quarter of the current; the INA3221 on all three
 * channels with 1.1 ms conversions, a result every 6.6 ms.  The
 * rotation reads ADC_CONFIG and each Configuration back (ina228_verify(),
 * ina3221_verify()).  A part found on another value than the one written,
 * twice in a row or once as FFFFh (the bus that no part drives), goes
 * offline and is probed and set up again SENSE_RETRY_MS on; each time
 * is counted modulo 256 (i228_resets, i3221_resets[a]); the windows
 * being filled from it are emptied; the INA228's totals are no longer the
 * run's; and a capture under way on that INA3221 ends lost.  A reset is
 * found within one rotation, 40 ms: the samples read in that time are in
 * the window before the one emptied when a window boundary lies between
 * -- the INA228's at the reset range, the INA3221's right in value and
 * up to 6.6 ms old.
 * The readings are not checked for all ones: until the read-back finds a
 * bus that no part drives, within the same 40 ms, an all-ones reading is
 * code -1, -390 µA on a 200 µΩ INA228 shunt, -400 µA on a 0.1 Ω INA3221
 * shunt, and -8 mV of INA3221 bus voltage.
 *
 * A channel that is off, and a part that is not fitted, not online or on
 * a stuck bus, is not read: its windows stay empty.  A stuck bus takes
 * every part offline within 3 reads of each.  After a recovery the bus
 * counts as stuck until a transaction goes through, and with every part
 * offline the next one is a probe: the flag can outlast a freed bus by up
 * to SENSE_RETRY_MS.
 *
 * Windows.  Fixed SENSE_WINDOW_MS windows on the IOmcu clock,
 * counted in 64 bits from the first tick, each with its number.  A read is
 * stamped when it is done and counts in the window that time falls in,
 * whatever ran before it in the tick.  For each source -- the 12 INA3221
 * channels and the INA228 -- the mean, lowest and highest current and the
 * mean and lowest bus voltage.  A clipped current
 * sample carries no value: it adds nothing to the figures and sets the
 * window's clip flag for its end of the range.  Reading a window ends
 * nothing: the last complete window stays readable until the next one
 * closes.  A tick late by more than a window leaves the windows it missed
 * out, so their numbers are skipped.
 *
 * The ring.  FAST's last SENSE_WIN_RING complete windows, newest first:
 * entry k is the window numbered k before the newest, and reads not closed
 * for a number that was skipped or lies before the first window.  With no
 * FAST channel every entry closes empty.  A ring entry counts every FAST
 * sample: a clipped one is the end of the range it
 * read (ina3221_end_ua(): 1.638 A or -1.6384 A on the 0.1 Ω shunt) in the
 * mean, the highest and the lowest, and is counted in n_clip.  The ring's
 * figures and sense_sched_window()'s differ in a window with a clipped
 * sample, and only there.
 *
 * The run.  sense_sched_arm() is the edge into driving: the run's lowest
 * INA228 voltage and highest current and power start again, and the
 * INA228's ENERGY and CHARGE are cleared (CONFIG.RSTACC) on the next tick.
 * Power is a current sample times the voltage read in the INA228 slot just
 * before it, 1 ms earlier, and none when that read failed.  A step in both
 * within that 1 ms pairs the new current with the old voltage.  The totals are this run's while totals_ok holds: the
 * clear went through, and no INA228 transaction has failed since.  The
 * first one that does ends them for the run, as does a part not online at
 * the clear.  The totals start at the clear, up to one tick after the arm,
 * later when the clear's write fails and is repeated.
 *
 * The move capture.  sense_sched_cap_arm() gives the move's levels;
 * sense_sched_cap_edge() gives the time of the PWM (pulse-width
 * modulation) frame that carries the new pulse, which the caller reads off
 * the slice's counter.  FAST samples are then judged by servo_move.h's
 * rules, times in 0.1 ms from that edge and resolved to FAST's 1 ms sample
 * interval: the moving mean of
 * SENSE_CAP_FILTER_N samples, seeded at the edge with the last of them
 * taken while armed, settled over SENSE_CAP_SETTLE_N samples, and a window
 * of SERVO_MOVE_TIMEOUT_MS plus the capture's lag, the deadline checked
 * before a sample is judged: a sample at or past it is never the arrival.
 * FAST samples are stamped when FAST's read is done.  A one-sample
 * excursion starts a move only when a quarter of it passes the threshold.
 * A clipped FAST sample is at or past the full scale less a step: 1.6376 A
 * on the 0.1 Ω shunt.  FAST's INA3221 leaving online ends a capture as
 * lost, and so
 * does an armed capture given no edge within SENSE_CAP_EDGE_WAIT_MS of the
 * arm.  The capture states take link_cap_state_t's numbers.
 *
 * The level before the command.  A capture armed with rise_ua
 * SENSE_CAP_RISE_AUTO takes it from FAST itself: the mean of the FAST
 * samples stamped in the SENSE_WINDOW_MS before the edge, a clipped one
 * counted at the end of the range as the filter takes it.  The schedule
 * keeps the last SENSE_WINDOW_MS FAST samples for it, whether a capture is
 * armed or not.  The level is taken when the first FAST sample at or past
 * the edge arrives, so an edge that lies ahead counts the samples taken
 * up to it.  A capture with no FAST sample in those 50 ms ends as lost:
 * the INA3221 did not answer.
 *
 * The output encoder.  An AS5600 on the same bus (as5600.h), when enabled,
 * is read on the ticks that read no rotation item -- every second tick,
 * 500 Hz: STATUS (1 byte) and RAW ANGLE (2 bytes), two transactions, 217.5
 * µs of bus time.  Every SENSE_ENC_MAG_EVERY-th slot (20 Hz) reads RAW ANGLE,
 * AGC (1 byte) and MAGNITUDE (2 bytes) instead, 337.5 µs: the angle is read
 * in every slot, STATUS in 24 of 25.  The angle is kept as the 12-bit count; the schedule
 * judges nothing from it but how long it has held still.
 *
 * Still time.  The angle holds an anchor, the count and the time of the
 * last sample that lay more than SENSE_ENC_STILL_TOL counts from the
 * previous anchor on the circle; that sample is the new anchor.
 * sense_sched_enc_still_ms() is the time since the anchor, saturating at
 * 65535 ms: how long the output has stayed within SENSE_ENC_STILL_TOL
 * counts of where it is.  A move that settles gives the time its last
 * anchor was set -- the moment the angle came within the tolerance of its
 * final value -- as now minus the still time, to the sample interval of
 * 2 ms.  The first sample after a set-up, and the first after the part
 * has been offline, set the anchor.  A sample whose STATUS has MD clear (no
 * magnet, as5600.h) is no position: it clears the anchor, the still time
 * reads 0, and the first sample with MD set is the new anchor.  A field
 * slot reads no STATUS and goes by the last one read, in the slot 2 ms
 * before.
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

#include "as5600.h"
#include "ina228.h"
#include "ina3221.h"
#include "sense_bus.h"
#include "servo_move.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SENSE_WINDOW_MS      50u   /**< one window                        */
/** Slots in the rotation; one is read every 2 ms, so a rotation is 40 ms.
 *  An item read at 50 Hz has two of them. */
#define SENSE_ROTATION       20u
/** FAST windows the ring keeps: 200 ms. */
#define SENSE_WIN_RING        4u
/** The capture's filter: a moving mean of 4 samples, 4 ms.  A step shows
 *  half after 1.5 samples and whole after 3: a fall from a moving current
 *  far above the band arrives 3 ms late.  Chosen, not measured: the noise
 *  is not known. */
#define SENSE_CAP_FILTER_N    4u
/** The encoder's tolerance for holding still: 12 counts, 1.05 degrees. */
#define SENSE_ENC_STILL_TOL  12u
/** One slot in this many reads AGC and MAGNITUDE besides the angle. */
#define SENSE_ENC_MAG_EVERY  25u
/** Samples in a row at an end held harder than the servo moves: 10 ms. */
#define SENSE_CAP_SETTLE_N   10u
/** The capture's lag, added to the window: the filter's 3 ms, up to 1 ms
 *  from a conversion to its read, and the 140 µs conversion, rounded
 *  up. */
#define SENSE_CAP_LAG_MS      5u
/** The capture's time unit: 0.1 ms. */
#define SENSE_CAP_T_PER_MS   10u
/** An armed capture given no edge in this long ends lost: nothing renders
 *  the frame it waits for.  The panel writes the command straight after
 *  the arm, and a frame at the slowest PWM rate, 40 Hz, is 25 ms; the
 *  bound is the move's own window. */
#define SENSE_CAP_EDGE_WAIT_MS SERVO_MOVE_TIMEOUT_MS
/** sense_cap_arm_t.rise_ua: the level before the command taken from FAST's
 *  SENSE_WINDOW_MS before the edge. */
#define SENSE_CAP_RISE_AUTO  INT32_MIN
/** FAST samples kept for that level: one window at 1 kHz. */
#define SENSE_FAST_HISTORY   SENSE_WINDOW_MS

/** INA3221 parts, at 0x40 to 0x43, and their channels. */
#define SENSE_I3221_PARTS   4u
#define SENSE_CHANNELS      12u
_Static_assert(SENSE_CHANNELS == SENSE_I3221_PARTS * INA3221_CHANNELS,
               "three channels a part");
/** Channel @p ch (1 to 3) of part @p a (0 to 3): 0 to 11. */
static inline unsigned sense_ch(unsigned a, unsigned ch)
{
    return a * INA3221_CHANNELS + ch - 1u;
}
#define SENSE_CH(a, ch)     sense_ch((unsigned)(a), (unsigned)(ch))
/** sense_i3221_cfg_t.fast: no channel is read every tick. */
#define SENSE_FAST_NONE     0xFFu

/** Where a window's readings come from: channel c (0 to 11) is source c,
 *  the INA228 (the ESC's path, electronic speed controller) the one after
 *  them. */
#define SENSE_SRC_INA228    SENSE_CHANNELS
#define SENSE_SRC_COUNT     (SENSE_CHANNELS + 1u)

/** The wire, the clock and the bus recovery. */
typedef struct {
    sense_i2c_t i2c;
    uint64_t  (*now_us)(void *ctx);  /**< the IOmcu's µs timer     */
    void      (*recover)(void *ctx); /**< 9 clocks, a STOP, the block
                                          re-initialised; NULL: none     */
    void       *ctx;
} sense_sched_io_t;

/** The INA3221s: which are fitted, each channel's shunt, and FAST. */
typedef struct {
    uint8_t  fitted;                      /**< bit a: the part at 0x40 + a */
    uint32_t shunt_uohm[SENSE_CHANNELS];  /**< 0: the channel is off       */
    uint8_t  fast;                        /**< a channel, or SENSE_FAST_NONE */
} sense_i3221_cfg_t;

/** What is fitted and how; a part not enabled is never addressed. */
typedef struct {
    bool     ina228_en;
    uint8_t  ina228_addr;
    uint32_t ina228_shunt_uohm;
    uint32_t ina228_max_ma;
    sense_i3221_cfg_t i3221;
    bool     as5600_en;            /**< the output encoder, at 0x36      */
} sense_sched_cfg_t;

/** One INA3221 as the SENSE page sets it up before protocol 4.25: the part
 *  at @p addr fitted when @p en and @p addr lies in 0x40 to 0x43, each
 *  channel in @p channels (bit 0 CH1) on @p shunt_uohm, FAST its CH1 when
 *  CH1 is one of them.  Anything else in @p m is cleared. */
void sense_i3221_one(sense_i3221_cfg_t *m, bool en, uint8_t addr,
                     uint32_t shunt_uohm, uint8_t channels);

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
    uint16_t n_hi, n_lo;  /**< current samples at the top, at the bottom  */
    bool     clip_hi, clip_lo;
    int32_t  i_min, i_max, v_min;
} sense_acc_t;

/** One FAST window as the ring keeps it. */
typedef struct {
    bool     closed;      /**< a window closed with this entry's number   */
    uint16_t n_i;         /**< current samples, clipped ones among them   */
    uint16_t n_clip;      /**< of them at an end of the range             */
    uint16_t n_v;         /**< voltage samples                            */
    bool     clip_hi;     /**< a current sample at the top of the range   */
    bool     clip_lo;     /**< one at the bottom                          */
    int32_t  i_mean_ua, i_min_ua, i_max_ua;   /**< a clipped sample at the
                               end of the range it read; 0 with n_i 0     */
    int32_t  v_mean_uv, v_min_uv;             /**< 0 with n_v 0           */
} sense_ring_win_t;

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

/** The move capture's state, in the order a capture passes through; the
 *  numbers are link_cap_state_t's. */
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
    int32_t rise_ua;      /**< the level before the command, or
                               SENSE_CAP_RISE_AUTO                        */
    int32_t hold_ua;      /**< the destination's holding level            */
    int32_t move_ua;      /**< the threshold                              */
    int32_t band_ua;      /**< the arrival band                           */
} sense_cap_arm_t;

/** A FAST sample held while armed, for the filter. */
typedef struct {
    float  a;
    int8_t clip;          /**< servo_move_clip_t                          */
} sense_cap_pre_t;

/** A FAST sample kept for the level before a command. */
typedef struct {
    uint32_t t;           /**< when it was read, 0.1 ms                   */
    int32_t  ua;          /**< its value; a clip at the end of the range  */
} sense_fast_t;

typedef struct {
    sense_cap_state_t state;
    uint16_t seq;         /**< captures ended, modulo 65536               */
    sense_cap_arm_t arm;
    uint32_t edge_t;      /**< the edge, 0.1 ms                           */
    uint32_t arm_t;       /**< the arm, 0.1 ms                            */
    bool     rise_owed;   /**< the level before the command is FAST's,
                               taken at the first sample past the edge    */
    /* The last SENSE_CAP_FILTER_N FAST samples while armed. */
    sense_cap_pre_t pre[SENSE_CAP_FILTER_N];
    uint8_t  pre_n, pre_head;
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

/** The output encoder as last read. */
typedef struct {
    as5600_t dev;
    bool     have_angle;   /**< raw holds a reading of this set-up          */
    uint8_t  status;       /**< STATUS as last read                         */
    uint16_t raw;          /**< RAW ANGLE, 0 to 4095                        */
    uint16_t samples;      /**< angle reads taken, modulo 65536             */
    bool     have_mag;
    uint8_t  agc;
    uint16_t magnitude;
    uint32_t slots;        /**< encoder slots taken                         */
    uint16_t anchor;       /**< the count the still time is held to         */
    uint64_t anchor_ms;    /**< when it was set                             */
    bool     anchored;     /**< an anchor is set                            */
} sense_enc_t;

typedef struct {
    sense_sched_io_t    io;
    sense_sched_cfg_t   cfg;
    sense_bus_t         bus;
    ina228_t            i228;
    ina3221_t           i3221[SENSE_I3221_PARTS];
    ina228_setup_err_t  i228_setup;   /**< INA228_SETUP_OK when disabled  */
    ina3221_setup_err_t i3221_setup[SENSE_I3221_PARTS]; /**< OK when not
                                                           fitted          */
    /* Per channel: a clipped sample is at least clip, the top and bottom
     * codes of the range stand for top and bottom; 0 for a channel off. */
    int32_t  ch_clip_ua[SENSE_CHANNELS];
    int32_t  ch_top_ua[SENSE_CHANNELS];
    int32_t  ch_bottom_ua[SENSE_CHANNELS];
    /* The channels read in turn beside FAST, one a tick. */
    uint8_t  others[SENSE_CHANNELS];
    uint8_t  n_others, other_at;
    uint8_t  volt_at;        /**< the channel whose voltage is next       */
    uint32_t ticks;
    uint32_t rot;            /**< rotation items read                     */
    bool     started;
    uint64_t t0_ms;          /**< the first tick                          */
    uint64_t win;            /**< the window being filled, from t0_ms     */
    sense_acc_t    acc[SENSE_SRC_COUNT];
    sense_window_t last[SENSE_SRC_COUNT];
    bool     have_last;
    /* FAST's last windows, newest first; ring_at numbers entry 0. */
    sense_ring_win_t ring[SENSE_WIN_RING];
    uint64_t ring_at;
    uint8_t  i228_resets;    /**< INA228 found reset, modulo 256           */
    uint8_t  i3221_resets[SENSE_I3221_PARTS]; /**< each INA3221, mod 256  */
    bool     have_vbus;      /**< vbus_uv is the last INA228 slot's voltage */
    int32_t  vbus_uv;
    bool     have_temp, have_diag;
    uint8_t  have_flags;     /**< bit a: flags[a] holds a reading          */
    int32_t  temp_mdegc;     /**< INA228 DIETEMP as last read             */
    uint16_t diag;           /**< INA228 DIAG_ALRT as last read           */
    uint16_t flags[SENSE_I3221_PARTS]; /**< each Mask/Enable as last read */
    sense_run_t run;
    sense_cap_t cap;
    sense_enc_t enc;
    /* The last SENSE_FAST_HISTORY FAST samples, oldest overwritten. */
    sense_fast_t hist[SENSE_FAST_HISTORY];
    uint8_t  hist_n, hist_head;
} sense_sched_t;

/** A schedule over @p io for the parts @p cfg enables.  A part whose
 *  set-up the driver refuses is left unset, never addressed, and its
 *  error kept in i228_setup or i3221_setup[a]; a FAST that names a channel
 *  off is taken as none.  The bus is the caller's to open at 400 kHz. */
void sense_sched_init(sense_sched_t *s, const sense_sched_io_t *io,
                      const sense_sched_cfg_t *cfg);

/** One tick: every 1 ms. */
void sense_sched_tick(sense_sched_t *s);

/** The edge into driving: the run's peaks start again and the INA228's
 *  totals are cleared on the next tick. */
void sense_sched_arm(sense_sched_t *s);

/** The last complete window of source @p src (a channel, or
 *  SENSE_SRC_INA228); false before one has closed. */
bool sense_sched_window(const sense_sched_t *s, unsigned src,
                        sense_window_t *out);

/** FAST's window numbered @p back before the newest complete one, @p back
 *  under SENSE_WIN_RING; false, and @p out not closed, for a number that
 *  was skipped, lies before the first window, or before one has closed. */
bool sense_sched_ring(const sense_sched_t *s, unsigned back,
                      sense_ring_win_t *out);

/** Arm a capture on FAST with @p levels; rise_ua SENSE_CAP_RISE_AUTO takes
 *  the level before the command from FAST.  Refused, and nothing changed,
 *  with no FAST channel or while its INA3221 is not online. */
bool sense_sched_cap_arm(sense_sched_t *s, const sense_cap_arm_t *levels);

/** The PWM frame carrying the new pulse starts at @p edge_us, on the clock
 *  now_us() reads; it may lie ahead.  The samples taken while armed seed
 *  the filter.  Nothing unless a capture is armed. */
void sense_sched_cap_edge(sense_sched_t *s, uint64_t edge_us);

/** Stop a capture; its count stays. */
void sense_sched_cap_disarm(sense_sched_t *s);

/** FAST's INA3221, or NULL with no FAST channel. */
const ina3221_t *sense_sched_fast_part(const sense_sched_t *s);

/** How long the encoder's angle has stayed within SENSE_ENC_STILL_TOL
 *  counts of its anchor, ms, saturating at 65535; 0 with no anchor. */
uint16_t sense_sched_enc_still_ms(const sense_sched_t *s);

#ifdef __cplusplus
}
#endif
