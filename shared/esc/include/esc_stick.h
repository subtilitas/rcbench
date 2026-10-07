/*
 * Stick programming: an ESC (electronic speed controller) programmed through
 * its throttle-stick menu, with the beeps counted in the supply current.
 *
 * The ESC is powered from the bench supply with a resistor load in place
 * of the motor, or a motor mounted solid with no propeller.  It sounds its
 * menu through the load or the windings, so each beep is a pulse in the
 * supply current.  The engine drives the throttle to the
 * profile's positions, switches the supply, counts the pulses into groups
 * and moves the throttle on the group that names the wanted item or value.
 *
 * Pure C, no I/O (input/output): the caller hands over each supply reading
 * and the bench's state, and reads back what the throttle, the arm and the
 * supply are to be.  It sends nothing itself, so every move it asks for
 * goes through the same arming, output and supply paths an operator's
 * does, and a STOP or a disarm reaches the ESC without the engine.
 *
 * No ESC has been recorded.  Every time in esc_stick_timing_t is a default
 * standing in for a measurement, not a measurement; see
 * docs/StickProgramming.md.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_ESC_STICK_H
#define RCBENCH_ESC_STICK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esc_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The most selections one run makes. */
#define ESC_STICK_MAX_CHANGES 16u

/** The throttle at the entry position before the supply comes on, ms: the
 *  ESC has to see the signal when it starts. */
#define ESC_STICK_SIGNAL_MS 1000u
/** How long the bench is given to arm, and the supply to report its output
 *  on, ms. */
#define ESC_STICK_ARM_WAIT_MS   3000u
#define ESC_STICK_POWER_WAIT_MS 3000u
/** No new reading for this long ends the run, ms. */
#define ESC_STICK_STALE_MS 1000u
/** Readings ignored for the floor after the output comes on: the ESC's
 *  input capacitors charging, ms. */
#define ESC_STICK_SETTLE_MS 500u
/** This many late readings in a row end the run: the supply reads too
 *  slowly for the beeps the timing describes. */
#define ESC_STICK_LATE_RUN 3u
/** A manual step not confirmed within this long ends the run, ms.  No
 *  manual states how long an ESC waits for its jumper or button; this is
 *  the operator's time to reach the ESC, not the ESC's. */
#define ESC_STICK_HAND_WAIT_MS 60000u
/** DONE counts only this long after its step is asked, ms, so one tap
 *  meant for the step before cannot confirm the next. */
#define ESC_STICK_HAND_MIN_MS 1000u
/** The output counts as off only once the supply itself reports it off and
 *  the current has stayed at or under ESC_STICK_OFF_MA for
 *  ESC_STICK_OFF_SETTLE_MS, in readings taken after the run asked it off:
 *  the panel's own OFF is a request, and the module answers it later. */
#define ESC_STICK_OFF_MA        20
#define ESC_STICK_OFF_SETTLE_MS 200u

/** Throttle positions as the output's percentage of travel. */
#define ESC_STICK_PCT_MIN 0.0f
#define ESC_STICK_PCT_MID 50.0f
#define ESC_STICK_PCT_MAX 100.0f

/**
 * How the beeps are timed and told apart from the ESC's idle current.  None
 * of it is measured: the defaults are guesses, kept as settings so a bench
 * recording can replace them without a build.
 */
typedef struct {
    uint32_t beep_min_ms;    /**< shortest beep the ESC sounds            */
    uint32_t gap_min_ms;     /**< shortest silence between two beeps      */
    uint32_t long_ms;        /**< a beep this long or longer is long      */
    uint32_t long_max_ms;    /**< a pulse longer than this is no beep     */
    uint32_t group_gap_ms;   /**< silence that ends a group               */
    uint32_t entry_ms;       /**< supply on to the first menu group       */
    uint32_t store_ms;       /**< held after the last selection           */
    uint32_t off_ms;         /**< supply off between two entries          */
    uint32_t silence_ms;     /**< no beep for this long ends the run      */
    uint32_t timeout_ms;     /**< the wanted group not heard in this long */
    uint32_t threshold_ma;   /**< above the idle floor: a beep            */
    uint32_t hysteresis_ma;  /**< below threshold less this: silence      */
} esc_stick_timing_t;

/** The defaults the settings start from. */
void esc_stick_timing_defaults(esc_stick_timing_t *t);

/** The longest interval between two readings the timing allows: the
 *  shorter of a beep and a gap, so every beep and every gap holds at least
 *  one reading. */
uint32_t esc_stick_read_max_ms(const esc_stick_timing_t *t);

/* ------------------------------------------------------- what can run */

typedef enum {
    ESC_STICK_KIND_NONE = 0,  /**< the engine does not run this profile   */
    ESC_STICK_KIND_TWO_STAGE, /**< select picks an item, value_select
                                   stores the value sounded               */
    ESC_STICK_KIND_ONE_STAGE, /**< the groups sound values; select stores */
} esc_stick_kind_t;

/**
 * Which menu the engine runs for @p p, or ESC_STICK_KIND_NONE with the
 * reason in @p why (a static string; may be NULL).
 *
 * An assisted profile runs only through its manual steps (esc_profile_t's
 * manual), and only when each can be waited for: no step during the menu,
 * whose moment the run cannot know, and a hand at a powered ESC -- an
 * at_power_up or before_menu step -- only with the stick at MIN for the
 * entry.  Otherwise the reason is "manual step".
 */
esc_stick_kind_t esc_stick_kind(const esc_profile_t *p, const char **why);

/** Where the stick rests while the menu sounds: the profile's listen move,
 *  else where the entry left it. */
esc_throttle_t esc_stick_listen(const esc_profile_t *p);

/** A throttle position as the output's percentage. */
float esc_stick_pct(esc_throttle_t pos);

/**
 * The supply voltage a profile asks for, mV: the lowest stated cell count
 * among its models at 3800 mV a LiPo cell or 1200 mV a NiMH cell, which is
 * above that model's cut-off and under every model's maximum.  0 when no
 * model states a cell count.
 */
uint32_t esc_stick_profile_mv(const esc_profile_t *p);

/** Whether @p it is an action rather than a setting: keyed reset or exit.
 *  Selecting one makes the ESC act on the select move; it sounds no values,
 *  and the engine does not offer it. */
bool esc_stick_is_action(const esc_item_t *it);

/**
 * Why @p it is not offered as a change, or NULL when it is: an action, or
 * an item with a single value, which is either the only setting there is
 * or a rule written as a value ("N beeps = N cells") -- nothing to choose
 * either way, and nothing the menu would sound in order.
 */
const char *esc_stick_not_offered(const esc_item_t *it);

/** One selection: the profile's item and value, as indices. */
typedef struct {
    uint8_t item;
    uint8_t value;
} esc_stick_change_t;

/**
 * Whether a run of @p n changes under @p t can start on @p p; the reason in
 * @p why when not.  Refused, never adjusted: a timing that cannot fit the
 * profile's select window is a setting to change, not one to guess.
 */
bool esc_stick_check(const esc_profile_t *p, const esc_stick_change_t *ch,
                     size_t n, const esc_stick_timing_t *t, const char **why);

/* ---------------------------------------------------------- the beeps */

typedef enum {
    ESC_DET_NONE = 0,
    ESC_DET_PULSE,     /**< a pulse ended                                */
    ESC_DET_GROUP,     /**< a group ended: count and valid say what      */
    ESC_DET_STUCK,     /**< the current has stayed high past a beep      */
} esc_det_event_t;

/**
 * The beep detector: hysteresis on the current above its idle floor, pulse
 * and gap lengths judged in readings, groups split by silence.
 *
 * Lengths come from readings, so they are known to one reading interval.
 * A pulse or gap is judged short when it holds fewer readings than the
 * shortest beep or gap must hold at the longest interval seen; a pulse is
 * judged long from the time between its first and last reading, which is
 * never longer than the beep.  A group with any pulse or gap judged wrong,
 * or with a late reading inside it, is heard but not valid.
 *
 * Counting starts on a quiet line: after esc_det_count() no group opens
 * until GROUP GAP of quiet has passed, so the first group heard is a whole
 * one and not the tail of a group already under way.
 */
typedef struct {
    esc_stick_timing_t t;
    uint8_t  long_equals_short;  /**< 0: every beep is short             */

    bool     counting;           /**< else only the floor is tracked     */
    bool     seeded;
    int32_t  floor_x16;          /**< the idle current, mA x 16          */
    bool     have_prev;
    uint32_t prev_ms;
    uint32_t iv_max_ms;          /**< longest interval not late          */

    bool     high;
    uint32_t rise_ms, last_high_ms, before_rise_ms;
    uint32_t n_high;
    bool     stuck_told;
    bool     wait_quiet;         /**< no group opens until a quiet gap   */
    bool     quiet_known;
    uint32_t quiet_from;         /**< the quiet began here               */
    bool     busy;               /**< not quiet while waiting            */
    uint32_t busy_from;
    uint32_t fall_ms;            /**< first low reading after a pulse    */
    uint32_t n_low;

    bool     group_open, group_bad;
    uint8_t  shorts, longs;

    /* The last group that ended. */
    uint8_t  count;
    bool     valid;
} esc_det_t;

void esc_det_init(esc_det_t *d, const esc_stick_timing_t *t,
                  uint8_t long_equals_short);
/** Floor only: no pulses, no groups. */
void esc_det_floor_only(esc_det_t *d);
/** Pulses and groups from the next reading on; the floor is kept. */
void esc_det_count(esc_det_t *d);
/** Forget any group under way, as if the line had been silent. */
void esc_det_drop_group(esc_det_t *d);
/** One reading, at @p t_ms, of @p ma; @p late marks it as having come too
 *  long after the one before. */
esc_det_event_t esc_det_reading(esc_det_t *d, uint32_t t_ms, int32_t ma,
                                bool late);
/** The idle floor, mA. */
int32_t esc_det_floor_ma(const esc_det_t *d);

/* ------------------------------------------------------------ the run */

typedef enum {
    ESC_STICK_IDLE = 0,
    ESC_STICK_ARMING,     /**< waiting for the bench to arm             */
    ESC_STICK_SIGNAL,     /**< throttle at entry, supply still off      */
    ESC_STICK_POWER,      /**< supply asked on, waiting for the output  */
    ESC_STICK_ENTRY,      /**< powered, waiting for the menu            */
    ESC_STICK_ITEMS,      /**< counting item groups                     */
    ESC_STICK_VALUES,     /**< counting value groups                    */
    ESC_STICK_STORE,      /**< held at the selection while it is stored */
    ESC_STICK_CYCLE,      /**< supply off before the next entry         */
    ESC_STICK_HAND_OFF,   /**< supply off, waiting for a manual step's
                               DONE before the power-up                 */
    ESC_STICK_HAND_ON,    /**< powered, waiting for a manual step's DONE
                               before the menu                          */
    ESC_STICK_OFF,        /**< supply off, the stick where it stored    */
    ESC_STICK_DONE,
    ESC_STICK_ABORTED,
} esc_stick_phase_t;

typedef enum {
    ESC_STICK_R_NONE = 0,
    ESC_STICK_R_STOP,         /**< STOP pressed                          */
    ESC_STICK_R_BENCH_STOP,   /**< a stop the bench raised itself: touch
                                   that stopped answering or was lost, the
                                   coprocessor's refusal or failsafe       */
    ESC_STICK_R_DISARMED,     /**< the bench disarmed under the run      */
    ESC_STICK_R_LINK,         /**< the coprocessor stopped answering     */
    ESC_STICK_R_SUPPLY_OFF,   /**< the output went off: a trip, a lost ON */
    ESC_STICK_R_SUPPLY_LOST,  /**< the supply stopped answering          */
    ESC_STICK_R_STALE,        /**< no new reading for ESC_STICK_STALE_MS */
    ESC_STICK_R_RATE,         /**< readings too far apart for the beeps  */
    ESC_STICK_R_NOT_ARMED,    /**< the bench did not arm                 */
    ESC_STICK_R_NO_POWER,     /**< the output did not come on            */
    ESC_STICK_R_SUPPLY_ON,    /**< the output did not go off when asked  */
    ESC_STICK_R_NO_BEEPS,     /**< no beep for silence_ms                */
    ESC_STICK_R_HIGH,         /**< the current stayed above the threshold */
    ESC_STICK_R_TIMEOUT,      /**< the wanted group not heard            */
    ESC_STICK_R_HAND,         /**< a manual step not confirmed in time   */
    ESC_STICK_R_TOUCH,        /**< touch lost before the arm was taken   */
    ESC_STICK_R_USER,         /**< ABORT pressed                         */
    ESC_STICK_R_LEFT,         /**< the screen was left                   */
} esc_stick_reason_t;

/** A word or two for the screen. */
const char *esc_stick_reason_text(esc_stick_reason_t r);

/**
 * The end @p stops stops give, @p pressed of them pressed by an operator,
 * both counted over the same span: STOP when every one was pressed, BENCH
 * STOPPED when any was not, so a stop the bench raised is never hidden by
 * a press that came with it.
 */
esc_stick_reason_t esc_stick_stop_reason(uint32_t stops, uint32_t pressed);

/**
 * Whether a run that ended for @p r ended because something was not as
 * expected: a reading, the supply, the link, the arm, the ESC, or a stop
 * the bench raised itself.  False for the ends an operator chose (STOP
 * pressed, ABORT, leaving the screen) and for none.  The screen's red
 * light.
 */
bool esc_stick_reason_is_fault(esc_stick_reason_t r);
const char *esc_stick_phase_text(esc_stick_phase_t ph);

/** What the bench is doing, each step. */
typedef struct {
    uint32_t now_ms;
    bool     armed;
    uint32_t stops;          /**< every stop counted, by any means         */
    bool     link_up;
    uint32_t pressed;        /**< of the stops, the ones an operator
                                  pressed, counted with the stop itself  */
} esc_stick_bench_t;

/**
 * One supply sample.  @p seq is the supply's count of its own readings,
 * modulo 65536 (supply_state_t's samples): it moves when the reading is
 * new, and a step of more than one is a reading the panel never saw.
 * @p at_ms is when the panel first had the reading (taken_ms), so a supply
 * whose count stops is a supply whose readings stop, however often the
 * panel reads its page.
 */
typedef struct {
    uint32_t seq;
    uint32_t at_ms;          /**< when the reading was taken               */
    int32_t  ma;
    bool     current_ok;     /**< the current was read                     */
    bool     output;         /**< the panel has the output on: its request */
    bool     reported_on;    /**< the supply itself reports it on: what
                                  the module read, not what was asked      */
    bool     online;         /**< the supply answers                       */
} esc_stick_sample_t;

/** What the bench is to do, after a step. */
typedef struct {
    bool     arm;
    float    throttle_pct;
    bool     supply_on;
    uint32_t supply_mv;
    uint32_t supply_ma;
} esc_stick_out_t;

typedef struct {
    const esc_profile_t *p;
    esc_stick_kind_t     kind;
    esc_stick_timing_t   t;
    esc_stick_change_t   ch[ESC_STICK_MAX_CHANGES];
    uint8_t              n;
    bool                 done[ESC_STICK_MAX_CHANGES];
    uint8_t              active;        /**< the change being made       */

    esc_stick_phase_t    phase;
    esc_stick_reason_t   reason;
    uint32_t             phase_ms;      /**< when this phase began       */
    uint32_t             now_ms;

    uint32_t             stops0;
    uint32_t             pressed0;
    bool                 link0;
    bool                 link_seen;     /**< the link was up at some time */
    bool                 off_seen;      /**< the output is off: reported,
                                             and settled                 */
    bool                 reported_on;   /**< the newest sample's          */
    uint32_t             off_asked_ms;  /**< when the run asked it off   */
    uint32_t             off_seq;       /**< the reading count then      */
    uint32_t             off_since_ms;  /**< reported off from here      */
    bool                 off_since_known;
    bool                 store_moved;   /**< the store move is made      */
    bool                 cycle_moved;   /**< the stick is at the entry   */
    bool                 armed_seen;

    esc_stick_out_t      out;

    /* The supply as the newest sample had it. */
    bool                 have_sample;
    uint32_t             seq;
    uint32_t             read_ms;       /**< the newest reading's time   */
    bool                 have_reading;  /**< one since this power-up     */
    bool                 output;
    bool                 online;
    bool                 current_ok;
    int32_t              ma;
    uint32_t             on_ms;         /**< when the output came on     */
    uint32_t             late_run;
    uint32_t             iv_ms;         /**< the last reading interval   */

    esc_det_t            det;
    uint32_t             heard_ms;      /**< the last pulse, or the
                                             listening's start           */
    /* The groups of this phase, for the sequence check. */
    uint8_t              prev;          /**< 0: no group to follow       */
    bool                 prev_trusted;  /**< prev was reached in order   */
    uint8_t              run;           /**< groups of prev in a row     */
    uint8_t              lo, hi;        /**< the loop's lowest, highest  */
    uint32_t             groups;        /**< groups heard this run       */
    uint8_t              last_count;
    bool                 last_valid;
    bool                 last_in_order; /**< the last group followed its
                                             predecessor                 */
    bool                 last_trusted;  /**< and so did that one: the
                                             last group could be acted on */
    uint8_t              entries;       /**< power-ups this run          */
    uint8_t              hand;          /**< the manual step asked, an
                                             index into p->manual        */
    bool                 hand_done;     /**< DONE taken, for the next
                                             step to act on               */
    uint32_t             pulses;        /**< pulses begun this run: the
                                             detector's rises            */
} esc_stick_t;

/**
 * Start a run.  @p mv and @p ma are the supply's set points, already held
 * to the caps by the caller.  Refused with @p why when esc_stick_check()
 * refuses, and the run stays idle.
 */
bool esc_stick_start(esc_stick_t *e, const esc_profile_t *p,
                     const esc_stick_change_t *ch, size_t n,
                     const esc_stick_timing_t *t, uint32_t mv, uint32_t ma,
                     const esc_stick_bench_t *b, const char **why);

/** One supply sample, as it arrives. */
void esc_stick_sample(esc_stick_t *e, const esc_stick_sample_t *s);

/** Advance to @p b->now_ms and judge the bench. */
void esc_stick_step(esc_stick_t *e, const esc_stick_bench_t *b);

/** End the run now, for @p why: throttle to its rest, supply off,
 *  disarmed.  A run already over is left as it ended. */
void esc_stick_abort(esc_stick_t *e, esc_stick_reason_t why);

/** Whether the run is under way: started and neither done nor aborted. */
bool esc_stick_running(const esc_stick_t *e);

/** What the bench is to do now. */
const esc_stick_out_t *esc_stick_out(const esc_stick_t *e);

/**
 * The manual step the run waits for, or NULL when it waits for none.
 *
 * A run stops for a step a person does at the ESC where the profile says
 * it is due, and goes on only on esc_stick_confirm():
 *
 *   - before the supply comes on (ESC_STICK_HAND_OFF): each at_power_up
 *     step, and from the second power-up on each before_power step too --
 *     the first power-up's are on the warning a run starts from.  The
 *     supply is off and the stick at the entry position; DONE switches the
 *     supply on.
 *   - once the entry has had its time (ESC_STICK_HAND_ON): each
 *     before_menu step, with the ESC powered and the stick at the entry
 *     position, which is MIN on every profile that has one
 *     (esc_stick_kind()).  DONE starts the menu.
 *
 * STOP, ABORT, a disarm and every supply rule end a waiting run as any
 * other: throttle to MIN, supply off, disarmed.  No DONE within
 * ESC_STICK_HAND_WAIT_MS ends it with ESC_STICK_R_HAND.
 */
const esc_manual_t *esc_stick_hand(const esc_stick_t *e);

/** Whether DONE would count now: ESC_STICK_HAND_MIN_MS after the step was
 *  asked. */
bool esc_stick_hand_ready(const esc_stick_t *e);

/** The time left to confirm the step asked, ms; 0 when none is. */
uint32_t esc_stick_hand_left_ms(const esc_stick_t *e);

/**
 * DONE: the step asked is done.  The run asks the next step due at the
 * same point, or goes on.  Ignored, returning false, when no step is asked
 * or the step was asked less than ESC_STICK_HAND_MIN_MS ago.
 */
bool esc_stick_confirm(esc_stick_t *e);

/** How many selections have been made. */
unsigned esc_stick_done_count(const esc_stick_t *e);

/** The beeps of the group under way. */
unsigned esc_stick_beeps(const esc_stick_t *e);

/** A beep shown for less than this would fall between two frames, ms: the
 *  green light stays on at least this long from the frame that first sees
 *  the pulse. */
#define ESC_STICK_BEEP_LIGHT_MS 150u

/**
 * The green light: on while the detector holds a pulse, from the reading
 * that rose above the threshold to the one that fell below the release,
 * and for at least ESC_STICK_BEEP_LIGHT_MS from the first look that sees a
 * pulse begun, so a pulse that rises and falls between two looks still
 * shows.  Every pulse lights it, trusted group or not.  Off while the
 * supply is not on for the menu (outside ENTRY, HAND_ON, ITEMS, VALUES and
 * STORE) and while no run is under way.
 */
typedef struct {
    uint32_t pulses;    /**< the run's count at the last look */
    uint32_t from_ms;   /**< when that look saw a new pulse   */
    bool     held;      /**< the minimum time is running      */
} esc_stick_light_t;

/** Start watching @p e: pulses already counted do not light it. */
void esc_stick_light_reset(esc_stick_light_t *l, const esc_stick_t *e);

/** Whether the green light is on at @p now_ms. */
bool esc_stick_light_green(esc_stick_light_t *l, const esc_stick_t *e,
                           uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ESC_STICK_H */
