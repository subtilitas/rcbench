/*
 * A trace of INA3221 CH1's 1 ms samples as text lines, for a debug build
 * of the coprocessor (its SENSE_TRACE build option).  No released image
 * links this file.
 *
 * Two sides, one on each core, with a ring of records between them:
 *
 *   Core 1  sense_trace_feed(), once per 1 ms tick after the schedule has
 *           run.  It reads the schedule's memory and nothing else: no bus
 *           transaction, no clock.  CH1's newest sample comes out of the
 *           schedule's own history (sense_sched_t.ch1), CH1's newest bus
 *           voltage out of the window being filled.  A voltage is kept
 *           only with the current sample of its tick, so each voltage
 *           line belongs to the sample line before it.  The INA3221's
 *           set-up and state go into the ring as records of their own
 *           when they change, so they keep their place among the
 *           samples.  At most 4 records a call, 2 while the set-up
 *           stands.  A full ring drops the record and counts it; the call
 *           never waits.  A set-up record that found no room is written
 *           again at the next call.
 *   Core 0  sense_trace_trigger(), sense_trace_pulse(), sense_trace_key()
 *           and sense_trace_pump(), from the main loop.  The pump hands
 *           back whole lines, never more bytes than the caller has room
 *           for, so a write of them does not wait on the console.
 *
 * The ring is single-producer, single-consumer and lock-free: core 1
 * writes a record and then moves head, core 0 reads a record and then
 * moves tail, each index stored with release and loaded with acquire
 * ordering.  Nothing else passes between the cores.  The ring's size is
 * the caller's, a power of two.
 *
 * A trace.  With no trace running the pump keeps the newest
 * SENSE_TRACE_PRE records, none of them older than the last change of
 * the set-up or the state, and discards the rest.  A trigger starts a
 * trace: the header, the records the ring holds from before the trigger,
 * then every record until the trace's end, SENSE_TRACE_EDGE_MS after a
 * command or a capture edge and SENSE_TRACE_KEY_MS after the console
 * command.  A trigger during a trace adds its line and moves the end to
 * its own when that lies later; it never shortens a trace and never
 * starts a second one.  The console's stop moves the end to the time of
 * the stop.  A trace also ends at a record that changes the INA3221's
 * shunt or Configuration.  However a trace ends, every record from
 * before its end is written first.  With the ring empty the end line
 * waits SENSE_TRACE_END_WAIT_MS past the end, for the tick that may
 * still bring a record of before it.
 *
 * Triggers.
 *   cmd   a PWM slot renders another pulse width than in the pass before,
 *         SENSE_TRACE_HOLD_MS or more after that slot's last change
 *         (sense_trace_pulse()).  Changes closer together are one slewed
 *         command: they move the trace's end, and once the slot has held
 *         still for SENSE_TRACE_HOLD_MS a $D line gives the pulse the
 *         command ended at.  A pulse
 *         going to 0 is the bank letting go, not a command.  The pulse is
 *         the one the slice renders: no longer than its frame
 *         (sense_trace_rendered()).
 *   edge  the capture's PWM edge, when a capture is armed.
 *   key   `t` on the console.  `x` ends a trace.
 *
 * Lines, format version SENSE_TRACE_FORMAT.  Each ends CR LF and is at
 * most SENSE_TRACE_LINE_MAX bytes with them.  Times t are the 64-bit
 * microsecond clock divided by 100, modulo 2^32: 0.1 ms, as the capture
 * counts.
 *
 *   $T v=1 n=N trig=cmd|edge|key t=T ms=M len=L
 *       The trace numbered N (modulo 65536) starts.  T is the trigger's
 *       time, M the millisecond tick at it (modulo 2^32), L the length
 *       in ms the trigger asks for.
 *   $H dt_us=1000 shunt_uohm=R cfg=0xCCCC on=B rst=K
 *       The sample period, CH1's shunt in micro-ohms, the Configuration
 *       value the driver holds and reads back every 40 ms, whether the
 *       part is online, and its reset count modulo 256: as they stand at
 *       the trace's first record.  All 0 with the bus closed.
 *   $C t=T ch=C us=P     a cmd trigger: output channel C renders P µs in
 *                        the frame that starts at T
 *   $D t=T ch=C us=P     channel C's slewed command ended at P µs; T is
 *                        its last change.  Counted with the trigger lines.
 *   $E t=T               an edge trigger
 *   $K t=T               a key trigger
 *   D,I                  a CH1 sample: D is its time less the previous
 *                        sample's, 0.1 ms, for the first sample less the
 *                        header's T (negative for a sample from before
 *                        the trigger); I is the shunt code, 40 µV a step:
 *                        I * 40 / R amps.  A sample at the end of the
 *                        range reads 4094 or -4094.
 *   vU                   CH1's bus voltage, U mV, read in the tick of the
 *                        sample line before it
 *   $S on=B rst=K        the part's state or reset count changed here:
 *                        the samples above it were taken before
 *   $L n=X               X records are missing here: the ring was full
 *   $Z n=N s=S v=V l=X m=A ml=B e=t|s|k
 *       The trace ends: S sample lines, V voltage lines, X records
 *       missing, A trigger lines, B triggers that found the trigger queue
 *       full.  e is the reason: its time, a changed set-up, the console.
 *       The counts saturate: S and X at 99999999, V at 9999999, A and B
 *       at 9999.
 *
 * A console that is not connected: the pump is told so, drops a trace
 * under way without a line and counts it (out.abandoned).
 *
 * Pure C, no SDK (software development kit).  Host-tested in
 * test_sense_trace.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sense_sched.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SENSE_TRACE_FORMAT       1u
/** The longest line, its CR LF included: the USB console's transmit
 *  buffer holds 64 bytes. */
#define SENSE_TRACE_LINE_MAX    64u
/** Records kept from before a trigger: 64 ms of samples less the 3
 *  voltages read in that time. */
#define SENSE_TRACE_PRE         64u
/** A trace's length after a command or a capture edge, ms. */
#define SENSE_TRACE_EDGE_MS   4000u
/** A trace's length after the console command, ms. */
#define SENSE_TRACE_KEY_MS   10000u
/** A slot's pulse changing this long after its last change is a command
 *  of its own, ms. */
#define SENSE_TRACE_HOLD_MS     50u
/** With the ring empty, the end line waits this long past the trace's
 *  end, ms: 2 ticks of core 1. */
#define SENSE_TRACE_END_WAIT_MS  2u
/** PWM slots watched for a changed pulse: OUT_MAX_SLOTS. */
#define SENSE_TRACE_SLOTS        8u
/** Trigger lines waiting for the console. */
#define SENSE_TRACE_MARKS        8u
/** The trace's time unit: 0.1 ms. */
#define SENSE_TRACE_T_PER_MS    10u
/** CH1's sample period, µs: the schedule's tick. */
#define SENSE_TRACE_PERIOD_US 1000u
/** The most records a record's count of missing ones holds. */
#define SENSE_TRACE_LOST_MAX  0xFFFFFFu

/** What a record holds. */
typedef enum {
    SENSE_TRACE_CURRENT = 0,  /**< a CH1 sample, µA                        */
    SENSE_TRACE_BUS,          /**< CH1's bus voltage, mV                   */
    SENSE_TRACE_CFG,          /**< the Configuration in bits 15-0, online
                                   in bit 16, the reset count in 31-24    */
    SENSE_TRACE_SHUNT,        /**< CH1's shunt, µΩ                         */
} sense_trace_kind_t;

/** What started or extended a trace. */
typedef enum {
    SENSE_TRACE_TRIG_CMD = 0,
    SENSE_TRACE_TRIG_EDGE,
    SENSE_TRACE_TRIG_KEY,
    SENSE_TRACE_MARK_DEST,    /**< no trigger: a slewed command's end      */
} sense_trace_trig_t;

/** Why a trace ended. */
typedef enum {
    SENSE_TRACE_END_NONE = 0,
    SENSE_TRACE_END_TIME,
    SENSE_TRACE_END_SETUP,
    SENSE_TRACE_END_KEY,
} sense_trace_end_t;

/** One record: 12 bytes. */
typedef struct {
    uint32_t t;      /**< the newest sample's time, 0.1 ms                 */
    int32_t  v;      /**< as its kind says                                 */
    uint32_t meta;   /**< the kind in bits 31-24; below it the records
                          dropped just before this one, saturating        */
} sense_trace_rec_t;

/** Core 1's own. */
typedef struct {
    bool     have;        /**< head and t are a sample taken               */
    uint8_t  head;        /**< the history's head at it                    */
    uint32_t t;           /**< its time                                    */
    uint64_t win;         /**< the window the voltage count is of          */
    uint16_t n_v;         /**< CH1 voltages in it at the last call         */
    int64_t  v_sum;       /**< and their sum, µV                           */
    uint32_t lost;        /**< records dropped since the last one kept     */
    uint32_t cfg, shunt;  /**< the set-up as last seen                     */
    bool     cfg_owed;    /**< its record is still to be written           */
    bool     shunt_owed;
} sense_trace_src_t;

/** A trigger line waiting. */
typedef struct {
    uint8_t  kind;        /**< sense_trace_trig_t                          */
    uint32_t t;
    uint16_t ch, us;      /**< a cmd trigger's channel and pulse           */
} sense_trace_mark_t;

/** One PWM slot's pulse as last seen. */
typedef struct {
    bool     have;        /**< pulse is one seen                           */
    bool     changed;     /**< changed_t is a change seen                  */
    bool     slewed;      /**< the command's pulse changed after its
                               trigger: its end is still to be said        */
    uint16_t pulse;
    uint32_t changed_t;   /**< its last change, 0.1 ms                     */
} sense_trace_watch_t;

/** Where the header stands. */
typedef enum {
    SENSE_TRACE_IDLE = 0,
    SENSE_TRACE_HEAD_T,   /**< the $T line is owed                         */
    SENSE_TRACE_HEAD_H,   /**< the $H line is owed                         */
    SENSE_TRACE_BODY,
} sense_trace_stage_t;

/** Core 0's own. */
typedef struct {
    sense_trace_stage_t stage;
    uint16_t id;          /**< traces started, modulo 65536                */
    uint8_t  trig;        /**< what started this one                       */
    uint32_t t0, ms0, len_ms;
    uint32_t end_t;       /**< the trace ends at a sample at or past it    */
    uint32_t last_t;      /**< the last sample line's time                 */
    bool     stopped;     /**< the console moved the end                   */
    uint32_t cfg, shunt;  /**< the set-up as it stands at the ring's tail  */
    bool     lost_shown;  /**< the $L line of the record at the tail is
                               written                                     */
    uint32_t n_s, n_v, n_m, n_lost, n_mlost;
    sense_trace_mark_t q[SENSE_TRACE_MARKS];
    uint8_t  q_n, q_head;
    uint32_t abandoned;   /**< traces dropped with no console              */
    uint32_t scanned;     /**< idle: records looked at for a set-up record */
    sense_trace_watch_t watch[SENSE_TRACE_SLOTS];
} sense_trace_out_t;

typedef struct {
    sense_trace_rec_t *buf;
    uint32_t          mask;       /**< the size less 1                     */
    atomic_uint_fast32_t head;    /**< records written; core 1 moves it    */
    atomic_uint_fast32_t tail;    /**< records read; core 0 moves it       */
    sense_trace_src_t src;
    sense_trace_out_t out;
} sense_trace_t;

/** A trace over @p buf of @p size records.  False, and nothing usable,
 *  unless @p size is a power of two from 2 to 2^24. */
bool sense_trace_init(sense_trace_t *tr, sense_trace_rec_t *buf,
                      uint32_t size);

/** Core 1, after each tick of @p s; NULL while no bus is open. */
void sense_trace_feed(sense_trace_t *tr, const sense_sched_t *s);

/** Core 0: a trigger of @p kind at @p at_us on the clock the schedule
 *  reads; @p ch and @p us are a cmd trigger's channel and pulse. */
void sense_trace_trigger(sense_trace_t *tr, sense_trace_trig_t kind,
                         uint64_t at_us, uint16_t ch, uint16_t us);

/** The pulse a PWM slice renders when asked for @p pulse_us with its
 *  counter wrapping at @p top: the frame's length at most, as the driver
 *  clamps it. */
uint16_t sense_trace_rendered(uint16_t pulse_us, uint32_t top);

/** Core 0, every pass, for each PWM slot: the output channel @p ch it
 *  renders and the pulse, 0 for none.  True when this is a command: the caller then gives the time of
 *  the frame that carries it to sense_trace_trigger(). */
bool sense_trace_pulse(sense_trace_t *tr, unsigned slot, uint16_t ch,
                       uint16_t pulse_us, uint64_t now_us);

/** Core 0: a character from the console. */
void sense_trace_key(sense_trace_t *tr, int c, uint64_t now_us);

/** Core 0, every pass: up to @p room bytes of whole lines into @p out;
 *  returns their length.  @p connected false drops a trace under way. */
size_t sense_trace_pump(sense_trace_t *tr, uint64_t now_us, bool connected,
                        char *out, size_t room);

/** Whether a trace is under way. */
bool sense_trace_active(const sense_trace_t *tr);

/** Records the ring holds. */
uint32_t sense_trace_held(const sense_trace_t *tr);

/** The shunt code of @p ua on a shunt of @p shunt_uohm: the inverse of
 *  ina3221_current_ua(), 0 for no shunt. */
int32_t sense_trace_code(int32_t ua, uint32_t shunt_uohm);

#ifdef __cplusplus
}
#endif
