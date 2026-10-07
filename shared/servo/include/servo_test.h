/*
 * The automatic servo test: a servo stepped through supply voltages, its
 * current measured at rest, moving and holding, its travel timed from the
 * supply's current, then the voltage walked down until it no longer moves.
 *
 * The servo is a PWM (pulse-width modulation) servo on the bench's surface
 * outputs, and the supply is the one SUPPLY drives: the PD mini, or the
 * panel's model of it.  Nothing measures the horn, so every result is read
 * off the supply's current, at the rate the supply reports it.
 *
 * Per voltage step, in this order:
 *
 *   SET     the set point asked, and waited for until the supply reads it
 *           back (SERVO_TEST_SET_TOL_V), at most SERVO_TEST_SET_TIMEOUT_MS;
 *           the first step also waits for the output to read on;
 *   SETTLE  the TEST page's SETTLE, with the servo at its centre;
 *   IDLE    SERVO_TEST_IDLE_MS at the centre: the idle current;
 *   MOVE    a step command to the far end.  A move shows movement when a
 *           reading lies more than SERVO_TEST_MOVE_A from the level before
 *           the command.  It has arrived at the first reading back
 *           within SERVO_TEST_BAND_A of that end's holding level, after a
 *           reading of the movement more than SERVO_TEST_MOVE_A above that
 *           level; the travel time runs from the command to that reading.
 *           A destination held harder than the servo moves, an end pushing
 *           on a stop, is never passed: there the move has arrived at the
 *           first of two readings in a row within SERVO_TEST_BAND_A of the
 *           level and of each other, after movement.  A move that does not
 *           arrive in SERVO_TEST_TRAVEL_TIMEOUT_MS is late;
 *   HOLD    the longer of DWELL and SERVO_TEST_HOLD_MIN_MS at that end:
 *           the holding current there, and the holding level the next move
 *           to that end falls back to.
 *
 * A step's first two moves, from the centre to the low end and on to the
 * high end, are not counted: they measure each end's holding level.  The
 * counted moves go end to end from there, for MOVEMENTS moves or TEST
 * TIME, as LENGTH BY says.  Before an end's holding level is known the
 * idle current stands in for it.
 *
 * The brown-out test follows: from SERVO_TEST_BROWNOUT_START_V (or the cap,
 * if lower) down in SERVO_TEST_BROWNOUT_STEP_V steps, each one SET, SETTLE
 * and IDLE as above and then SERVO_TEST_BROWNOUT_MOVES moves, centre to the
 * high end and back to the low end.  A voltage shows no movement when no
 * reading of any of those moves lies more than SERVO_TEST_MOVE_A from the
 * level before its command; the walk stops there, or at the floor, which
 * is the last step when it lies off the step's grid.
 *
 * The run is aborted -- the output asked off, the servo let go -- by STOP,
 * a disarm, link loss, leaving the screen, the operator taking the servo or
 * changing how it is driven, and touch events lost (the caller's,
 * servo_test_abort()), and by the supply (here): it stops answering, trips,
 * its output goes off, no new reading arrives for SERVO_TEST_STALE_MS, a
 * set point is not read back, a step above the voltage cap in force, or a
 * current above STALL AT for SERVO_TEST_STALL_ABORT_MS.  An aborted run is
 * reported as ABORTED with its reason and what it measured so far.
 *
 * A state machine fed readings and stepped with the time, so the host suite
 * runs it against the servo and supply models; it moves nothing itself.
 * What it wants done comes back from servo_test_step().  Its log rows and
 * report lines leave through an outbox (servo_test_peek()), so the card is
 * written by whoever owns the card.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------- the method */

/** The characterisation steps a run can carry: 4.8, 6.0, 7.4, 8.4 V. */
#define SERVO_TEST_STEPS_MAX         4u
/** Above this a step is an HV (high voltage) step: a standard servo's
 *  rating is 4.8 to 6.0 V. */
#define SERVO_TEST_HV_ABOVE_V        6.0f

/** The brown-out walk: where it starts, how far each step goes down, and
 *  the lowest voltage it asks for.  A supply whose lowest set point is
 *  higher stops it there. */
#define SERVO_TEST_BROWNOUT_START_V  5.0f
#define SERVO_TEST_BROWNOUT_STEP_V   0.2f
#define SERVO_TEST_BROWNOUT_FLOOR_V  3.0f
/** The steps from START to FLOOR, both included. */
#define SERVO_TEST_BROWNOUT_MAX      11u
/** Moves at each brown-out voltage: centre to the high end, then to the
 *  low end. */
#define SERVO_TEST_BROWNOUT_MOVES    2u

/** A reading this far from the level before a command is the servo
 *  moving, and this far above an end's holding level is the move not yet
 *  there. */
#define SERVO_TEST_MOVE_A            0.10f
/** Back within this of the holding level is the move arrived. */
#define SERVO_TEST_BAND_A            0.05f

#define SERVO_TEST_IDLE_MS           1000u
/** The shortest hold measured at an end, whatever DWELL says: at 10
 *  readings a second, six readings. */
#define SERVO_TEST_HOLD_MIN_MS       600u
#define SERVO_TEST_TRAVEL_TIMEOUT_MS 3000u
/** A set point counts as taken once read back within this. */
#define SERVO_TEST_SET_TOL_V         0.05f
#define SERVO_TEST_SET_TIMEOUT_MS    3000u
/** No new reading for this long ends the run: SUPPLY_LINK_STALE_MS. */
#define SERVO_TEST_STALE_MS          1500u
/** Above STALL AT for this long ends the run. */
#define SERVO_TEST_STALL_ABORT_MS    1000u
/** The most moves a step makes, LENGTH BY TIME included. */
#define SERVO_TEST_MOVES_MAX         1000u

/* ------------------------------------------------------- what it reads */

/** One reading of the supply, as SUPPLY has it (supply_state_t). */
typedef struct {
    float    v, i;          /**< at the output, V and A                 */
    float    set_v;         /**< the voltage set point, read back       */
    float    set_i;         /**< the current limit, read back           */
    uint8_t  mode;          /**< 0 off, 1 CV, 2 CC (supply_mode_t)      */
    bool     output;        /**< switched on                            */
    bool     online;        /**< the supply answers                     */
    bool     ok;            /**< voltage and current both arrived       */
    uint8_t  trip;          /**< nonzero: tripped (supply_trip_kind_t)  */
    uint16_t samples;       /**< readings the supply took, mod 65536    */
    uint32_t taken_ms;      /**< when the panel had it, on the run's clock */
} servo_test_reading_t;

/** What the run wants done, from servo_test_step(). */
typedef struct {
    bool     set;           /**< the set points to set_v and set_i      */
    float    set_v, set_i;
    bool     on;            /**< the output on, after the set points    */
    bool     off;           /**< the output off                         */
    bool     command;       /**< the servo to cmd_us, at once (no slew) */
    uint16_t cmd_us;
    bool     release;       /**< the servo let go, to its centre        */
} servo_test_do_t;

/** What the caller knows each step. */
typedef struct {
    bool  armed;
    float v_max;            /**< the voltage cap in force: SUPPLY's caps,
                                 VOLTAGE MAX and the module's input clamp */
} servo_test_in_t;

/* ------------------------------------------------------ what it is told */

typedef struct {
    /* What runs. */
    float    steps_v[SERVO_TEST_STEPS_MAX];
    uint8_t  step_count;
    bool     brownout;
    float    i_limit;       /**< the current limit through the run, A   */

    /* The servo: its centre and the two ends the moves go between. */
    uint16_t centre_us, end_lo_us, end_hi_us;

    /* The TEST page. */
    uint16_t settle_ms, dwell_ms;
    bool     by_moves;      /**< LENGTH BY MOVES; TIME otherwise        */
    uint16_t moves;
    uint16_t time_s;

    /* The LIMITS page; 0 is not checked, and STALL AT always is. */
    float    idle_max_a, hold_max_a;
    uint16_t travel_max_ms;
    float    stall_a;

    bool     report;        /**< a TXT report beside the CSV            */

    /* For the report only: the settings in force. */
    char     dut[24];
    char     type[20];
    char     danger[40];    /**< the red tag, or empty                  */
    bool     hv;            /**< HV SERVO                               */
    uint16_t min_us, max_us, frame_hz;
    int16_t  trim_us;
    bool     reverse;
    uint8_t  travel_deg, range_pct;
    bool     model;         /**< the supply is the panel's model        */
    char     firmware[16];
} servo_test_cfg_t;

/* ------------------------------------------------------ what it reports */

typedef enum {
    SERVO_TEST_IDLE = 0,    /**< never started                          */
    SERVO_TEST_RUNNING,
    SERVO_TEST_DONE,        /**< finished or aborted                    */
} servo_test_state_t;

typedef enum {
    SERVO_TEST_PH_NONE = 0,
    SERVO_TEST_PH_SET,
    SERVO_TEST_PH_SETTLE,
    SERVO_TEST_PH_IDLE,
    SERVO_TEST_PH_MOVE,
    SERVO_TEST_PH_HOLD,
} servo_test_phase_t;

typedef enum {
    SERVO_TEST_AB_NONE = 0,
    SERVO_TEST_AB_STOP,
    SERVO_TEST_AB_DISARMED,
    SERVO_TEST_AB_LINK,
    SERVO_TEST_AB_LEFT,
    SERVO_TEST_AB_OPERATOR,
    SERVO_TEST_AB_SETTINGS,
    SERVO_TEST_AB_TOUCH,
    SERVO_TEST_AB_SUPPLY_LOST,
    SERVO_TEST_AB_TRIPPED,
    SERVO_TEST_AB_SUPPLY_OFF,
    SERVO_TEST_AB_STALE,
    SERVO_TEST_AB_NOT_ON,
    SERVO_TEST_AB_SET_NOT_TAKEN,
    SERVO_TEST_AB_CAP,
    SERVO_TEST_AB_STALL,
    SERVO_TEST_AB_COUNT
} servo_test_abort_t;

typedef enum {
    SERVO_TEST_PASS = 0,
    SERVO_TEST_FAIL,
    SERVO_TEST_ABORTED,
} servo_test_verdict_t;

/** Why a run would not start. */
typedef enum {
    SERVO_TEST_START_OK = 0,
    SERVO_TEST_START_NO_STEPS,   /**< no step and no brown-out chosen   */
    SERVO_TEST_START_ABOVE_CAP,  /**< a step above the voltage cap      */
    SERVO_TEST_START_NO_SUPPLY,  /**< the supply does not answer        */
    SERVO_TEST_START_BAD_ENDS,   /**< the ends are not either side of the
                                      centre                            */
} servo_test_start_t;

/** A mean, kept as a sum. */
typedef struct {
    float    sum;
    uint32_t n;
} servo_test_mean_t;

/** One voltage step's results. */
typedef struct {
    float    set_v;
    bool     brownout;
    bool     begun;         /**< its set point was asked                */
    bool     done;          /**< ran to its end                         */
    servo_test_mean_t v;    /**< the voltage after SETTLE               */
    servo_test_mean_t idle;
    servo_test_mean_t move; /**< the readings while travelling          */
    servo_test_mean_t hold[2];   /**< at the low end, at the high end   */
    float    move_peak_a;   /**< the highest reading while travelling   */
    float    peak_a;        /**< the highest reading after SETTLE       */
    uint32_t travel_max_ms;
    uint32_t travel_sum_ms;
    uint16_t travels;       /**< moves that arrived                     */
    uint16_t moves;         /**< moves counted                          */
    uint16_t no_rise;       /**< counted moves with no movement seen    */
    uint16_t timeouts;      /**< counted moves that did not arrive      */
    bool     moved;         /**< movement seen in any move              */
} servo_test_step_t;

#define SERVO_TEST_STEP_SLOTS (SERVO_TEST_STEPS_MAX + SERVO_TEST_BROWNOUT_MAX)

/* --------------------------------------------------------- the outbox */

/** The longest line a log row or a report line takes, terminator included. */
#define SERVO_TEST_LINE_MAX 160u
/** Log rows waiting for the card.  A row is a reading, 10 to 20 a second. */
#define SERVO_TEST_OUTBOX   16u

typedef enum {
    SERVO_TEST_OUT_NONE = 0,
    SERVO_TEST_OUT_OPEN,    /**< a run begins: its files are opened     */
    SERVO_TEST_OUT_CSV,     /**< one line of the CSV                    */
    SERVO_TEST_OUT_TXT,     /**< one line of the report                 */
    SERVO_TEST_OUT_END,     /**< the run's files are complete           */
} servo_test_out_t;

typedef struct {
    servo_test_cfg_t   cfg;
    servo_test_state_t state;
    servo_test_phase_t phase;
    servo_test_abort_t why;
    uint32_t start_ms, end_ms;
    uint32_t phase_ms;      /**< when the phase began                    */
    float    v_max;         /**< the cap as last told                    */
    float    floor_v;       /**< where the brown-out walk stops           */
    float    bo_start_v;

    servo_test_step_t steps[SERVO_TEST_STEP_SLOTS];
    uint8_t  step;          /**< the step running                        */
    uint8_t  step_count;    /**< steps[] in use                           */
    bool     on_seen;       /**< the output has read on                  */

    /* The servo. */
    uint16_t cmd_us;
    uint8_t  end;           /**< 0 low, 1 high: where the move goes      */
    uint32_t cmd_ms;
    bool     counted;       /**< the move counts                          */
    bool     rose;          /**< movement seen: away from rise_a          */
    bool     left;          /**< since then above ref_a: not there yet    */
    bool     near_prev;     /**< the last reading, after movement, was
                                 within the band of ref_a                */
    float    prev_i;
    uint32_t prev_at;
    float    ref_a;         /**< the level the move falls back to         */
    float    rise_a;        /**< the level before the command             */
    float    hold_ref[2];
    bool     hold_known[2];
    uint32_t hold_from_ms;
    servo_test_mean_t hold_now;
    servo_test_mean_t move_now;
    float    move_peak_now;
    uint32_t moves_from_ms;
    uint16_t moves_done;
    uint8_t  placed;        /**< uncounted moves to the ends, this step   */
    uint32_t travel_now_ms; /**< the arrival to log, 0 for none          */

    /* The readings. */
    bool     have_reading;
    uint16_t samples;
    uint32_t first_ms, last_ms;   /**< taken_ms of the first and last    */
    uint32_t readings;      /**< new readings during the run              */
    uint32_t module_samples;
    uint32_t skipped;       /**< readings the supply took that never
                                 reached the test: its count stepped by
                                 more than one between two samples       */
    bool     stalling;
    uint32_t stall_since_ms;
    bool     stalled;       /**< a characterisation reading over STALL AT */
    float    stall_peak_a;  /**< the highest characterisation reading    */

    /* The outbox. */
    char     box[SERVO_TEST_OUTBOX][SERVO_TEST_LINE_MAX];
    uint8_t  box_kind[SERVO_TEST_OUTBOX];
    uint8_t  box_head, box_n;
    uint32_t rows, rows_lost;
    unsigned report_line;
    bool     ended;         /**< END handed over                          */
    char     scratch[SERVO_TEST_LINE_MAX];

    servo_test_do_t pend;
} servo_test_t;

void servo_test_init(servo_test_t *t);

/**
 * Start a run at @p now_ms with the supply as @p last read it and its set
 * points' range @p v_min to @p v_max.  Refused, and nothing changed, for
 * no step and no brown-out, a step outside the range, a supply that does
 * not answer, or ends not either side of the centre.  A run that starts
 * hands over OPEN and the CSV header first.
 */
servo_test_start_t servo_test_start(servo_test_t *t,
                                    const servo_test_cfg_t *cfg,
                                    uint32_t now_ms,
                                    const servo_test_reading_t *last,
                                    float v_min, float v_max);

/** A sample of the supply, with @p position_us the horn's measured pulse
 *  width or 0 for none.  One that repeats the last reading's sample count
 *  is checked for the supply's state and measures nothing. */
void servo_test_reading(servo_test_t *t, const servo_test_reading_t *r,
                        uint16_t position_us);

/** One pass at @p now_ms: the timers, and what the run wants done since the
 *  last pass into @p out. */
void servo_test_step(servo_test_t *t, uint32_t now_ms,
                     const servo_test_in_t *in, servo_test_do_t *out);

/** End a running run for @p why: the output is asked off and the servo let
 *  go at the next servo_test_step().  Nothing for a run not running. */
void servo_test_abort(servo_test_t *t, servo_test_abort_t why,
                      uint32_t now_ms);

bool servo_test_running(const servo_test_t *t);

servo_test_verdict_t servo_test_verdict(const servo_test_t *t);

/** Steps planned, the brown-out counted as one, and the one running,
 *  1-based, for "step 2 of 3". */
unsigned servo_test_steps_planned(const servo_test_t *t);
unsigned servo_test_step_now(const servo_test_t *t);

/** The highest, the longest and the lowest of a run, over its
 *  characterisation steps; false when none was measured. */
bool servo_test_max_idle(const servo_test_t *t, float *a);
bool servo_test_max_hold(const servo_test_t *t, float *a);
bool servo_test_max_travel(const servo_test_t *t, uint32_t *ms);
/** The lowest brown-out voltage the servo moved at, and whether one below
 *  it showed none. */
bool servo_test_brownout(const servo_test_t *t, float *moved_v,
                         bool *stopped);

/** Readings that reached the run per second, the supply's own per second,
 *  and the mean interval between two readings; false before two. */
bool servo_test_rates(const servo_test_t *t, float *per_s,
                      float *module_per_s, uint32_t *interval_ms);

/**
 * The next item for the card, without taking it: OPEN, the CSV's lines,
 * then once the run is over the report's (if REPORT) and END.  @p text is
 * the line, valid until the next call.  NONE while nothing waits.
 */
servo_test_out_t servo_test_peek(servo_test_t *t, const char **text);

/** The item servo_test_peek() gave has been taken. */
void servo_test_pop(servo_test_t *t);

/** Whether everything of the last run has been handed over. */
bool servo_test_drained(const servo_test_t *t);

/* ------------------------------------------------- report (servo_report.c) */

/**
 * Every word the run puts in its files and on the SERVO screen, in one
 * table, so another language is another table.
 */
typedef enum {
    SERVO_STR_PHASE_NONE = 0,
    SERVO_STR_PHASE_SET,
    SERVO_STR_PHASE_SETTLE,
    SERVO_STR_PHASE_IDLE,
    SERVO_STR_PHASE_MOVE,
    SERVO_STR_PHASE_HOLD,
    SERVO_STR_PASS,
    SERVO_STR_FAIL,
    SERVO_STR_ABORTED,
    SERVO_STR_TEST_STEP,
    SERVO_STR_TEST_BROWNOUT,
    SERVO_STR_AB_NONE,
    SERVO_STR_AB_STOP,
    SERVO_STR_AB_DISARMED,
    SERVO_STR_AB_LINK,
    SERVO_STR_AB_LEFT,
    SERVO_STR_AB_OPERATOR,
    SERVO_STR_AB_SETTINGS,
    SERVO_STR_AB_TOUCH,
    SERVO_STR_AB_SUPPLY_LOST,
    SERVO_STR_AB_TRIPPED,
    SERVO_STR_AB_SUPPLY_OFF,
    SERVO_STR_AB_STALE,
    SERVO_STR_AB_NOT_ON,
    SERVO_STR_AB_SET_NOT_TAKEN,
    SERVO_STR_AB_CAP,
    SERVO_STR_AB_STALL,
    SERVO_STR_START_OK,
    SERVO_STR_START_NO_STEPS,
    SERVO_STR_START_ABOVE_CAP,
    SERVO_STR_START_NO_SUPPLY,
    SERVO_STR_START_BAD_ENDS,
    /* The SERVO screen's, before a run is asked of the engine. */
    SERVO_STR_START_NOT_ARMED,
    SERVO_STR_START_BUSY,
    SERVO_STR_COUNT
} servo_str_t;

/** The English text of @p id; "" for one out of range. */
const char *servo_str(servo_str_t id);

const char *servo_test_phase_name(servo_test_phase_t ph);
const char *servo_test_abort_name(servo_test_abort_t why);
const char *servo_test_verdict_name(servo_test_verdict_t v);
const char *servo_test_start_name(servo_test_start_t why);

/** The CSV's header row. */
const char *servo_test_csv_header(void);

/** Line @p idx of the report into @p buf; false past the last. */
bool servo_report_line(const servo_test_t *t, unsigned idx, char *buf,
                       size_t n);

#ifdef __cplusplus
}
#endif
