/*
 * The automatic servo test: a servo stepped through supply voltages, its
 * current measured at rest, moving and holding, its travel timed from the
 * supply's current, then the voltage walked down until it no longer moves.
 *
 * The servo is a PWM (pulse-width modulation) servo on the bench's surface
 * outputs, and the supply is the one SUPPLY drives: the PD mini, or the
 * panel's model of it.  Nothing measures the horn, so every result is read
 * off the servo rail's current, at the rate its meter reports it.
 *
 * The meter.  A run reads one meter from its start to its end, the one its
 * configuration names (cfg.meter.kind):
 *
 *   PD mini   the supply's own readings (servo_test_reading()), about 10 a
 *             second.  The model stands in for it with no coprocessor.
 *   INA3221   the 50 ms windows of the INA3221's CH1
 *             (servo_test_window()), 20 a second: each one's mean current
 *             and mean bus voltage are the reading, its highest and lowest
 *             1 ms sample give the peak, its lowest bus voltage the step's
 *             lowest voltage.  The supply is still read for its state --
 *             answering, output, trip, set point, mode -- and none of its
 *             voltages or currents is used or logged.
 *
 * A reading is a point in time on the PD mini and 50 ms long on the
 * INA3221 (cfg.meter.span_ms).  IDLE and HOLD take the windows that lie
 * wholly inside them: a window that began before the phase is in neither.
 * The window open at a command is MOVE's.  Moves are timed by the panel
 * from the windows as from the supply's readings, so a travel time on the
 * INA3221 is late by up to one window and one poll, an upper bound, and
 * TRAVEL TIME is not checked.
 *
 * Signed current.  A negative current is a reading.  The limits, the peak
 * and STALL AT compare its magnitude, in whole mA (servo_test_over_a()),
 * and the CSV keeps the sign.  A clipped window -- a 1 ms sample at an end
 * of the INA3221's range -- is a reading as well: its figures are the
 * values the part gave, a lower bound, and the report counts such windows.
 *
 * Per voltage step, in this order:
 *
 *   SET     the set point asked, and waited for until the supply reads it
 *           back (SERVO_TEST_SET_TOL_V), at most SERVO_TEST_SET_TIMEOUT_MS;
 *           the first step also waits for the output to read on;
 *   SETTLE  the TEST page's SETTLE, with the servo at its centre;
 *   IDLE    SERVO_TEST_IDLE_MS at the centre: the idle current, and its
 *           noise, the standard deviation of those readings.  The step's
 *           threshold is the larger of SERVO_TEST_MOVE_MIN_A and
 *           SERVO_TEST_NOISE_K times that noise;
 *   MOVE    a step command to the far end.  A move shows movement when a
 *           reading lies more than the threshold from the level before
 *           the command.  It has arrived at the first reading back within
 *           SERVO_TEST_BAND_A of that end's holding level, after a reading
 *           of the movement more than the threshold above that level; the
 *           travel time runs from the command to that reading.  A
 *           destination held harder than the servo moves, an end pushing
 *           on a stop, is never passed: there the move has arrived at the
 *           first of two readings in a row within SERVO_TEST_BAND_A of the
 *           level and of each other, after movement.  A move with
 *           movement that does not arrive in SERVO_TEST_TRAVEL_TIMEOUT_MS,
 *           plus the meter's lag (servo_test_travel_window_ms()), is late;
 *           one that shows no movement in that time is unseen: not timed
 *           and not late.  The rules are servo_move.h's, fed one reading
 *           at a time;
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
 * reading of any of those moves lies more than that voltage's threshold
 * from the level before its command; the walk stops there, or at the
 * floor, which is the last step when it lies off the step's grid.
 *
 * The output encoder.  With an AS5600 on the horn shaft (cfg.enc_on) the run
 * also reads the angle, from servo_test_encoder(), and reports it beside the
 * current's figures; the current's rules above are unchanged and the angle
 * decides nothing: not the verdict, not a limit.  For each move, from its
 * command to the next:
 *
 *   moved    the angle leaves SERVO_TEST_ENC_MOVED_DEG of the angle read
 *            before the command, when that reading is younger than
 *            SERVO_TEST_ENC_STALE_MS.  The distance is taken on the
 *            circle, the shortest way round, 0 to 180 degrees;
 *   settled  after it has moved, a reading whose STILL_MS is at least
 *            SERVO_TEST_ENC_HOLD_MS and whose still time began after the
 *            command: the angle has stayed within SERVO_TEST_ENC_TOL_COUNTS
 *            counts (1.05 degrees) of an anchor for that long.  The travel
 *            time is the start of that stillness minus the command, which
 *            the coprocessor's 2 ms sample interval resolves whatever the
 *            polling interval.  It includes the command's way from the
 *            panel to the pin, up to one poll interval and one PWM frame,
 *            not measured;
 *   end angle the last reading of a settled move, from the centre count
 *            (cfg.enc_centre); the angle error is that minus the commanded
 *            angle of the end (cfg.enc_cmd_deg).
 *
 * The circle.  The sensor's count wraps from 4095 to 0 and every angle is a
 * position on one turn, so no two angles are subtracted or averaged as
 * plain numbers.  An angle is the count less the centre count taken the
 * shortest way, -180 to just under +180 degrees.  A step's end angles at
 * one end are averaged as offsets from the first of them, each offset the
 * shortest way, and the mean is put back on the circle
 * (servo_test_enc_end()); the angle error is wrapped to the same range.
 * Two readings 2 counts apart either side of the half turn from the centre
 * are 0.18 degrees apart, and ends read at +179.9 and -179.9 degrees
 * average to 180, shown as -180.0.
 *
 * More than 180 degrees.  One turn is all the sensor tells apart.  A servo
 * whose ends lie more than 180 degrees apart is reported correctly while
 * each end lies within 180 degrees of the centre: the two ends are never
 * subtracted from each other.  An end further than 180 degrees from the
 * centre is reported 360 degrees off (+200 as -160); its angle error is
 * still right while the commanded angle names the same position, since the
 * error is wrapped.  A move is "moved" by the shortest way between its
 * start and the reading, so a move that ends within
 * SERVO_TEST_ENC_MOVED_DEG of a whole turn from its start reads unmoved,
 * and the direction of a move is not judged.  Turns are not counted: a
 * winch servo's travel of several turns is not measured.
 *
 * The magnet.  A reading is an angle only while the sensor detects its
 * magnet (STATUS MD, as5600.h); the link hands out no other
 * (sense_link_enc()).  A reading without one arrives as not valid with
 * no_magnet set: it ends the angle's validity as any invalid reading does
 * -- no angle in the CSV, a move that had not settled not counted -- and
 * the report says how often it happened.  A reading with the field too weak
 * or too strong (ML, MH) is an angle and is used; the report counts them.
 *
 * A move that never left the tolerance is unmoved; one that moved and was
 * not still for SERVO_TEST_ENC_HOLD_MS before the next command is late.
 * Only counted moves are reported.  A deadband cannot be derived from
 * moves that go end to end, and is reported as not measured.
 *
 * What reads the current is described to the run (servo_test_meter_t), so
 * a meter whose readings lag and repeat, as the PD mini's do, gives travel
 * times that are an upper bound: TRAVEL TIME is reported against them and
 * not checked.
 *
 * The run is aborted -- the output asked off, the servo let go -- by STOP,
 * a disarm, link loss, leaving the screen, the operator taking the servo or
 * changing how it is driven, and touch events lost (the caller's,
 * servo_test_abort()), and by the supply (here): it stops answering, trips,
 * its output goes off, no new reading arrives for SERVO_TEST_STALE_MS, a
 * set point is not read back, a step above the voltage cap in force, a
 * current above STALL AT for SERVO_TEST_STALL_ABORT_MS, or the supply in
 * constant current for SERVO_TEST_CC_ABORT_MS.  A run on the INA3221 also
 * ends when no window arrives for SERVO_TEST_WIN_STALE_MS and when the
 * INA3221 stops being the servo rail's meter (servo_test_meter_now()): it
 * never goes on with another meter.  An aborted run is reported as ABORTED
 * with its reason and what it measured so far, and nothing fed to it after
 * its end changes either.
 *
 * Stall.  A reading whose magnitude is above STALL AT on a
 * characterisation step fails the run, and readings above it for
 * SERVO_TEST_STALL_ABORT_MS without one at or under it end the run.  On
 * the INA3221 the reading is the window's mean and the time counts from
 * the window's start, so 20 windows in a row end it.  A supply in constant
 * current holds its current limit: with STALL AT at or above the limit no
 * reading is above STALL AT, and the constant-current rule ends the run
 * instead, whatever STALL AT is.  Constant current for less than
 * SERVO_TEST_CC_ABORT_MS fails nothing: the report counts those readings.
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

#include "servo_move.h"

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

/** The smallest threshold.  A step's threshold is the larger of this and
 *  SERVO_TEST_NOISE_K times its idle noise.  A reading more than the
 *  threshold from the level before a command is the servo moving, and
 *  more than it above an end's holding level is the move not yet there.
 *  The move rules are servo_move.h's; these are its constants. */
#define SERVO_TEST_MOVE_MIN_A        SERVO_MOVE_MIN_A
#define SERVO_TEST_NOISE_K           SERVO_MOVE_NOISE_K
/** Back within this of the holding level, and not above it by the
 *  threshold, is the move arrived. */
#define SERVO_TEST_BAND_A            SERVO_MOVE_BAND_A

#define SERVO_TEST_IDLE_MS           1000u
/** The shortest hold measured at an end, whatever DWELL says: at 10
 *  readings a second, six readings. */
#define SERVO_TEST_HOLD_MIN_MS       600u
/** A move not arrived this long after its command is late; a meter's lag
 *  is added, since its readings show an arrival that much later. */
#define SERVO_TEST_TRAVEL_TIMEOUT_MS SERVO_MOVE_TIMEOUT_MS
/** A set point counts as taken once read back within this. */
#define SERVO_TEST_SET_TOL_V         0.05f
#define SERVO_TEST_SET_TIMEOUT_MS    3000u
/** No new reading for this long ends the run: SUPPLY_LINK_STALE_MS. */
#define SERVO_TEST_STALE_MS          1500u
/** Above STALL AT for this long ends the run. */
#define SERVO_TEST_STALL_ABORT_MS    1000u
/** The supply in constant current for this long ends the run. */
#define SERVO_TEST_CC_ABORT_MS       1000u
/** A run on the INA3221: no window for this long ends it, 10 windows. */
#define SERVO_TEST_WIN_STALE_MS      500u
/** One window of the INA3221: SENSE_WINDOW_MS. */
#define SERVO_TEST_WIN_MS            50u
/** A run whose idle current is below minus this says the shunt is fitted
 *  the other way round. */
#define SERVO_TEST_NEG_IDLE_A        0.020f
/** The INA3221's shunt voltage at the end of its range, in microvolts:
 *  code 4095 at 40 uV. */
#define SERVO_TEST_INA3221_END_UV    163800u
/** The most moves a step makes, LENGTH BY TIME included. */
#define SERVO_TEST_MOVES_MAX         1000u

/* The output encoder's constants.  The tolerance is the coprocessor's
 * SENSE_ENC_STILL_TOL, test_servo_test holds the two equal. */
#define SERVO_TEST_ENC_TOL_COUNTS    12u
/** The same in degrees: 12 counts of 360 / 4096. */
#define SERVO_TEST_ENC_TOL_DEG       1.0547f
/** The angle is still when its STILL_MS reaches this. */
#define SERVO_TEST_ENC_HOLD_MS       100u
/** A move has moved when the angle is further than this from where it
 *  started. */
#define SERVO_TEST_ENC_MOVED_DEG     2.0f
/** The angle read before a command is the move's start while younger than
 *  this. */
#define SERVO_TEST_ENC_STALE_MS      500u

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

/** One 50 ms window of INA3221 CH1, as the window ring carries it
 *  (sense_link_win_t).  A clipped sample counts in the three currents at
 *  the end of the range it read. */
typedef struct {
    uint16_t number;        /**< the window's, modulo 65536             */
    bool     current;       /**< it holds current samples               */
    bool     voltage;       /**< it holds bus voltage samples           */
    uint8_t  clipped;       /**< samples at an end of the range         */
    int16_t  mean_ma, max_ma, min_ma;   /**< mA, signed                 */
    uint16_t mean_mv, min_mv;           /**< at the load side of the shunt */
    uint32_t taken_ms;      /**< when the panel had it, on the run's clock */
} servo_test_win_t;

/** Readings of the encoder kept for matching with the supply's rows.  The
 *  panel has one about every 40 ms, so 16 span 640 ms: more than
 *  SERVO_TEST_ENC_STALE_MS. */
#define SERVO_TEST_ENC_HIST          16u

/** A kept reading. */
typedef struct {
    uint32_t ms;            /**< when the panel had it                   */
    uint16_t raw;           /**< RAW ANGLE, 0 to 4095                    */
} servo_test_enc_sample_t;

/** One reading of the output encoder, as the SENSE page has it. */
typedef struct {
    bool     valid;         /**< the part answers, detects its magnet and
                                 has an angle                           */
    uint16_t raw;           /**< RAW ANGLE, 0 to 4095                   */
    uint16_t still_ms;      /**< the angle within the tolerance for this
                                 long at the reading                    */
    uint32_t taken_ms;      /**< when the panel had it, on the run's clock */
    bool     gap;           /**< readings before this one were lost: the
                                 angle between is not known, as after a
                                 reading that is not valid             */
    bool     no_magnet;     /**< not valid because the part answers and
                                 detects no magnet (STATUS MD clear)   */
    bool     weak, strong;  /**< valid, with the field too weak (ML) or
                                 too strong (MH): the angle's noise is
                                 not specified                          */
} servo_test_enc_t;

/** @p raw from @p centre on the circle, degrees, -180 to just under 180:
 *  360 / 4096 a count.  The difference of any two counts the shortest way
 *  round. */
float servo_test_enc_deg(uint16_t raw, uint16_t centre);

/** @p deg on the circle: -180 to just under 180. */
float servo_test_enc_wrap(float deg);

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

/** Which meter a run reads. */
typedef enum {
    SERVO_TEST_METER_PDMINI = 0,    /**< the supply's own readings      */
    SERVO_TEST_METER_INA3221,       /**< CH1's 50 ms windows            */
    SERVO_TEST_METER_MODEL,         /**< the panel's model of the supply */
} servo_test_meter_kind_t;

/** Why a run reads the PD mini with the INA3221 on in SETUP: the
 *  condition of servo_source.h that does not hold. */
typedef enum {
    SERVO_TEST_INA_NONE = 0,    /**< not on in SETUP, or it is the meter */
    SERVO_TEST_INA_OLD,         /**< the coprocessor is older than 4.11  */
    SERVO_TEST_INA_NOT_HELD,    /**< it does not hold the set-up         */
    SERVO_TEST_INA_SILENT,      /**< the part does not answer            */
    SERVO_TEST_INA_NO_WINDOW,   /**< no fresh window with current        */
    SERVO_TEST_INA_RESET,       /**< it reset itself                     */
    SERVO_TEST_INA_SETTLING,    /**< working for less than 1 s           */
    SERVO_TEST_INA_MODEL,       /**< the supply is the panel's model     */
    SERVO_TEST_INA_COUNT
} servo_test_ina_t;

/** What reads the servo's current, for what its travel times are worth. */
typedef struct {
    uint8_t  kind;          /**< servo_test_meter_kind_t                */
    uint16_t span_ms;       /**< how long one reading is: 0 for a point
                                 in time, 50 for a window               */
    uint16_t shunt_dmohm;   /**< the INA3221's shunt in 0.1 mOhm, as SETUP
                                 holds it; 0 for none stated            */
    uint16_t range_ma;      /**< the current at the end of its range; 0
                                 for none stated                        */
    char     name[16];      /**< for the report: "PD mini"              */
    uint16_t lag_ms;        /**< from a change of current to the first
                                 reading that shows it, typical; 0 for
                                 none stated                            */
    bool     repeats;       /**< a reading can repeat the last value for
                                 several readings                       */
    bool     upper_bound;   /**< travel times are an upper bound: TRAVEL
                                 TIME is reported, not checked          */
} servo_test_meter_t;

/** The PD mini's own meter: read every 102 to 106 ms, a change of current
 *  shows about 300 ms later (median 0.31 s over two runs, 2026-10-07), and
 *  its value often repeats over several readings. */
#define SERVO_TEST_PDMINI_LAG_MS     300u

void servo_test_meter_pdmini(servo_test_meter_t *m);

/** The panel's model of the supply: no lag of its own and no repeats, and
 *  its travel times held to what a run on the PD mini can check. */
void servo_test_meter_model(servo_test_meter_t *m);

/** The INA3221's CH1 on a shunt of @p shunt_dmohm, in 0.1 mOhm (1000 for
 *  0.1 Ohm, which ends the range at 1.638 A), read in 50 ms windows: a
 *  change of current shows in the window that closes after it, up to 50 ms
 *  later, and the panel times a move from the windows, so travel times are
 *  an upper bound. */
void servo_test_meter_ina3221(servo_test_meter_t *m, uint16_t shunt_dmohm);

/** Whether @p a is above @p limit_a: both in whole mA, @p a by its
 *  magnitude.  The one comparison the verdict and the report make of a
 *  current against IDLE CURRENT, HOLD CURRENT and STALL AT. */
bool servo_test_over_a(float a, float limit_a);

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

    /* The output encoder: off leaves the run, the CSV and the report as
     * they are without one. */
    bool     enc_on;
    uint16_t enc_centre;    /**< the count at the servo's neutral       */
    float    enc_cmd_deg[2];/**< the commanded angle of the low and the
                                 high end, the screen's degrees          */

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
    servo_test_meter_t meter;
    uint32_t meter_changes; /**< the meter's change count at the start
                                 (servo_test_meter_now())               */
    uint8_t  ina_why;       /**< servo_test_ina_t: why the INA3221 that is
                                 on in SETUP is not this run's meter    */
    char     firmware[16];
    /**
     * The report's language: a table of SERVO_STR_COUNT entries, each NULL
     * for the English, or NULL for English throughout.  The CSV is English
     * whatever this says, so tools parse every run alike.
     */
    const char *const *text;
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
    SERVO_TEST_AB_CC,           /**< the supply in constant current      */
    /* A run on the INA3221. */
    SERVO_TEST_AB_WIN_STALE,    /**< no window for SERVO_TEST_WIN_STALE_MS */
    SERVO_TEST_AB_INA_RESET,    /**< the part reset itself               */
    SERVO_TEST_AB_INA_SILENT,   /**< it stopped answering                */
    SERVO_TEST_AB_INA_NO_WINDOW,/**< its windows hold no current         */
    SERVO_TEST_AB_INA_SETUP,    /**< the coprocessor no longer holds its
                                     set-up, or it is off in SETUP       */
    SERVO_TEST_AB_INA_METER,    /**< it stopped being the meter, the
                                     condition not known                 */
    SERVO_TEST_AB_COUNT
} servo_test_abort_t;

typedef enum {
    SERVO_TEST_PASS = 0,
    SERVO_TEST_FAIL,
    SERVO_TEST_ABORTED,
    /** Nothing failed, and a counted move showed no movement: the current
     *  cannot tell that servo moving from one standing still. */
    SERVO_TEST_NOT_MEASURABLE,
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
    float    v_min;         /**< the lowest voltage after SETTLE, while
                                 v holds a reading                      */
    servo_test_mean_t idle;
    float    idle_sq;       /**< the idle readings' squares, summed     */
    float    noise_a;       /**< the idle readings' standard deviation  */
    float    move_a;        /**< the threshold, once IDLE is over        */
    servo_test_mean_t move; /**< the readings while travelling          */
    servo_test_mean_t hold[2];   /**< at the low end, at the high end   */
    float    move_peak_a;   /**< the reading of the largest magnitude
                                 while travelling; on the INA3221 the 1 ms
                                 sample                                 */
    float    peak_a;        /**< likewise, of every reading after SETTLE */
    uint32_t travel_max_ms;
    uint32_t travel_sum_ms;
    uint16_t travels;       /**< moves that arrived                     */
    uint16_t moves;         /**< moves counted                          */
    uint16_t no_rise;       /**< counted moves with no movement seen:
                                 unseen, neither timed nor late          */
    uint16_t timeouts;      /**< counted moves with movement that did not
                                 arrive: late                            */
    bool     moved;         /**< movement seen in any move              */

    /* The angle, with the encoder on. */
    uint16_t enc_moves;     /**< counted moves with a start angle        */
    uint16_t enc_travels;   /**< ... that settled                        */
    uint16_t enc_unmoved;   /**< ... whose angle never moved             */
    uint16_t enc_late;      /**< ... that moved and did not settle       */
    uint32_t enc_travel_sum_ms, enc_travel_max_ms;
    /* The settled end angles at the low and the high end: the count of
     * the first, and every one's offset from it in degrees, the shortest
     * way round.  servo_test_enc_end() gives the mean. */
    uint16_t enc_end_ref[2];
    servo_test_mean_t enc_end_off[2];
} servo_test_step_t;

#define SERVO_TEST_STEP_SLOTS (SERVO_TEST_STEPS_MAX + SERVO_TEST_BROWNOUT_MAX)

/* --------------------------------------------------------- the outbox */

/** The longest line a log row or a report line takes, terminator included:
 *  the CSV's header with the encoder's columns is 219 characters. */
#define SERVO_TEST_LINE_MAX 224u
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
    servo_move_t move;      /**< the move under way, judged by servo_move */
    float    hold_ref[2];
    bool     hold_known[2];
    uint32_t hold_from_ms;
    servo_test_mean_t hold_now;
    uint32_t moves_from_ms;
    uint16_t moves_done;
    uint8_t  placed;        /**< uncounted moves to the ends, this step   */
    uint32_t travel_now_ms; /**< the arrival to log, 0 for none          */

    /* The encoder. */
    /* The last SERVO_TEST_ENC_HIST readings, oldest overwritten, each with
     * the time it was taken: a log row takes the newest not later than
     * itself (servo_test_enc_at()). */
    servo_test_enc_sample_t enc_hist[SERVO_TEST_ENC_HIST];
    uint8_t  enc_hist_n, enc_hist_next;
    uint32_t enc_reads;     /**< readings that reached the run           */
    uint32_t enc_no_magnet; /**< times the part reported no magnet       */
    uint32_t enc_weak;      /**< readings with the field too weak        */
    uint32_t enc_strong;    /**< ... and too strong                      */
    bool     enc_open;      /**< a move is being judged                  */
    bool     enc_counted;
    bool     enc_ok;        /**< it has a start angle                    */
    uint8_t  enc_end;
    uint32_t enc_cmd_ms;
    uint16_t enc_start_raw; /**< the count read before the command       */
    uint16_t enc_last_raw;  /**< the last since                          */
    bool     enc_moved, enc_settled;
    uint32_t enc_travel_ms;
    uint32_t enc_travel_now_ms;     /**< the settle to log, 0 for none   */
    uint32_t enc_travel_at_ms;      /**< the reading that found it       */

    /* The meter's readings: the supply's, or the INA3221's windows. */
    bool     have_reading;
    uint16_t samples;
    uint32_t first_ms, last_ms;   /**< taken_ms of the first and last    */
    uint32_t readings;      /**< new readings during the run              */
    uint32_t module_samples;
    uint32_t skipped;       /**< readings the meter took that never
                                 reached the test: its count stepped by
                                 more than one between two samples       */
    uint32_t clipped;       /**< windows with a sample at an end of the
                                 INA3221's range                         */
    bool     stalling;
    uint32_t stall_since_ms;
    bool     stalled;       /**< a characterisation reading over STALL AT */
    float    stall_peak_a;  /**< the characterisation reading of the
                                 largest magnitude                       */
    float    move_peak_now; /**< the 1 ms sample of the largest magnitude
                                 since the command, on the INA3221       */

    /* The supply's state, on either meter. */
    bool     sup_have;      /**< a reading of it arrived                 */
    uint16_t sup_samples;
    uint32_t sup_ms;        /**< taken_ms of its last new reading        */
    uint8_t  sup_mode;      /**< its mode at that reading                */
    bool     cc_on;         /**< its last new reading was constant current */
    uint32_t cc_since_ms;
    uint32_t cc_readings;   /**< new readings in constant current        */
    uint32_t cc_longest_ms; /**< the longest stretch of them             */

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
 *  is checked for the supply's state and measures nothing.  In a run on
 *  the INA3221 no sample measures: each is the supply's state alone. */
void servo_test_reading(servo_test_t *t, const servo_test_reading_t *r,
                        uint16_t position_us);

/**
 * A window of INA3221 CH1, each number once and in order, with
 * @p position_us as servo_test_reading() takes it.  The reading of a run on
 * the INA3221, and nothing to any other run.  A window without current or
 * without voltage samples, and one that repeats the last number, measures
 * nothing and does not count as a window that arrived.
 */
void servo_test_window(servo_test_t *t, const servo_test_win_t *w,
                       uint16_t position_us);

/** The servo rail's meter as one poll of the panel decided it
 *  (servo_source.h). */
typedef struct {
    bool     ina3221;       /**< the INA3221 is the meter                */
    uint32_t changes;       /**< how often the meter has changed, modulo
                                 2^32                                    */
    /** Why the INA3221 last stopped being the meter -- one of the
     *  SERVO_TEST_AB_INA_ reasons or SERVO_TEST_AB_LINK -- and the change
     *  count that step left; SERVO_TEST_AB_NONE while it never did. */
    servo_test_abort_t dropped;
    uint32_t dropped_at;
} servo_test_meter_now_t;

/**
 * The meter as a poll decided it, for every answer the caller has: with
 * each window, each supply sample and each frame.  A run on the INA3221
 * holds the change count of its start (cfg.meter_changes).  It ends, as
 * servo_test_abort() ends a run, at the first answer that is not the
 * INA3221 or carries another count: the meter changed under it, whatever
 * it is now.  The reason is @p m->dropped when that drop came after the
 * start, and SERVO_TEST_AB_INA_METER otherwise.  Nothing for a run on
 * another meter, which goes on with the meter it started with.
 */
void servo_test_meter_now(servo_test_t *t, const servo_test_meter_now_t *m,
                          uint32_t now_ms);

/**
 * A reading of the output encoder, each one once.  Judged against the move
 * under way; kept as the angle the CSV and the SERVO screen show.  A reading
 * that is not valid, or that carries gap, ends the angle's validity before
 * it.  Nothing is judged without cfg.enc_on.
 */
void servo_test_encoder(servo_test_t *t, const servo_test_enc_t *e);

/** The angle at @p at_ms, degrees from cfg.enc_centre: the newest kept
 *  reading taken at or before @p at_ms (a signed difference, so a reading
 *  taken later than the row does not count), when it is no older than
 *  SERVO_TEST_ENC_STALE_MS.  False when there is none. */
bool servo_test_enc_at(const servo_test_t *t, uint32_t at_ms, float *deg);

/**
 * The mean settled end angle of step @p s at @p end (0 low, 1 high) into
 * @p deg, degrees from cfg.enc_centre in the commanded direction, -180 to
 * just under 180, and its error against cfg.enc_cmd_deg[end] into @p err,
 * the same range; either may be NULL.  The mean is taken on the circle,
 * from the first end angle of the step.  False when no move settled there.
 * End angles of one end that lie 180 degrees or more apart have no mean
 * that says anything.
 */
bool servo_test_enc_end(const servo_test_t *t, const servo_test_step_t *s,
                        unsigned end, float *deg, float *err);

/** One pass at @p now_ms: the timers, and what the run wants done since the
 *  last pass into @p out. */
void servo_test_step(servo_test_t *t, uint32_t now_ms,
                     const servo_test_in_t *in, servo_test_do_t *out);

/** End a running run for @p why: the output is asked off and the servo let
 *  go at the next servo_test_step().  Nothing for a run not running. */
void servo_test_abort(servo_test_t *t, servo_test_abort_t why,
                      uint32_t now_ms);

bool servo_test_running(const servo_test_t *t);

/** How long a move of @p t may take before it is late, or unseen:
 *  SERVO_TEST_TRAVEL_TIMEOUT_MS plus the meter's lag, 3300 ms on the PD
 *  mini. */
uint32_t servo_test_travel_window_ms(const servo_test_t *t);

servo_test_verdict_t servo_test_verdict(const servo_test_t *t);

/** Steps planned, the brown-out counted as one, and the one running,
 *  1-based, for "step 2 of 3". */
unsigned servo_test_steps_planned(const servo_test_t *t);
unsigned servo_test_step_now(const servo_test_t *t);

/** The highest, the longest and the lowest of a run, over its
 *  characterisation steps; false when none was measured.  A current is the
 *  one of the largest magnitude, with its sign. */
bool servo_test_max_idle(const servo_test_t *t, float *a);
bool servo_test_max_hold(const servo_test_t *t, float *a);
bool servo_test_max_travel(const servo_test_t *t, uint32_t *ms);
/** The lowest brown-out voltage the servo moved at, and whether one below
 *  it showed none. */
bool servo_test_brownout(const servo_test_t *t, float *moved_v,
                         bool *stopped);

/** Whether a step's idle current reads below -SERVO_TEST_NEG_IDLE_A: the
 *  shunt is fitted the other way round. */
bool servo_test_negative_at_rest(const servo_test_t *t);

/** Whether STALL AT cannot be reached in a run of @p cfg: at or above the
 *  current limit, where the supply holds the current (@p by_limit), or at
 *  or above the end of the meter's range (@p by_range).  Either may be
 *  NULL.  Compared in whole mA. */
bool servo_test_stall_unreachable(const servo_test_cfg_t *cfg,
                                  bool *by_limit, bool *by_range);

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
 * table, so another language is another table.  The SERVO_STR_R_ entries
 * are the report's: some are printf formats, and a translation converts the
 * same arguments in the same order.
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
    SERVO_STR_NOT_MEASURABLE,
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
    SERVO_STR_AB_CC,
    SERVO_STR_AB_WIN_STALE,
    SERVO_STR_AB_INA_RESET,
    SERVO_STR_AB_INA_SILENT,
    SERVO_STR_AB_INA_NO_WINDOW,
    SERVO_STR_AB_INA_SETUP,
    SERVO_STR_AB_INA_METER,
    SERVO_STR_START_OK,
    SERVO_STR_START_NO_STEPS,
    SERVO_STR_START_ABOVE_CAP,
    SERVO_STR_START_NO_SUPPLY,
    SERVO_STR_START_BAD_ENDS,
    /* The SERVO screen's, before a run is asked of the engine. */
    SERVO_STR_START_NOT_ARMED,
    SERVO_STR_START_BUSY,
    /* The TXT report, in the order it is written. */
    SERVO_STR_R_TITLE,
    SERVO_STR_R_RESULT,
    SERVO_STR_R_RESULT_WHY,
    SERVO_STR_R_RESULT_UNSEEN,
    SERVO_STR_R_RESULT_BO_UNSEEN,
    SERVO_STR_R_DEVICE,
    SERVO_STR_R_FIRMWARE,
    SERVO_STR_R_LOG,
    SERVO_STR_R_SUPPLY,
    SERVO_STR_R_SUPPLY_MODEL,
    SERVO_STR_R_READINGS,
    SERVO_STR_R_READINGS_FEW,
    SERVO_STR_R_SKIPPED,
    SERVO_STR_R_RESOLUTION,
    SERVO_STR_R_RESOLUTION_UNKNOWN,
    SERVO_STR_R_LAG,
    SERVO_STR_R_REPEATS,
    SERVO_STR_R_UPPER_BOUND,
    SERVO_STR_R_DURATION,
    SERVO_STR_R_ROWS,
    SERVO_STR_R_SETTINGS,
    SERVO_STR_R_TYPE,
    SERVO_STR_R_RATE,
    SERVO_STR_R_DANGER,
    SERVO_STR_R_DANGER_NONE,
    SERVO_STR_R_HV,
    SERVO_STR_R_HV_OFF,
    SERVO_STR_R_HV_ON_RUN,
    SERVO_STR_R_HV_ON_NONE,
    SERVO_STR_R_ENDS,
    SERVO_STR_R_STEPS,
    SERVO_STR_R_STEPS_NONE,
    SERVO_STR_R_BROWNOUT,
    SERVO_STR_R_BROWNOUT_NOT_RUN,
    SERVO_STR_R_I_LIMIT,
    SERVO_STR_R_TIMING,
    SERVO_STR_R_LEN_MOVES,
    SERVO_STR_R_LEN_TIME,
    SERVO_STR_R_LIMITS,
    SERVO_STR_R_ON,
    SERVO_STR_R_OFF,
    SERVO_STR_R_PER_STEP,
    SERVO_STR_R_COLUMNS,
    SERVO_STR_R_STEP_NOT_RUN,
    SERVO_STR_R_CUT_SHORT,
    SERVO_STR_R_NO_STEP,
    SERVO_STR_R_LATE,
    SERVO_STR_R_UNSEEN,
    SERVO_STR_R_THRESHOLD,
    SERVO_STR_R_ARRIVAL,
    SERVO_STR_R_BO_HEAD,
    SERVO_STR_R_BO_NOT_RUN,
    SERVO_STR_R_BO_NOT_REACHED,
    SERVO_STR_R_BO_STOPPED,
    SERVO_STR_R_BO_ALL,
    SERVO_STR_R_BO_NONE,
    SERVO_STR_R_BO_RULE,
    SERVO_STR_R_LIM_HEAD,
    SERVO_STR_R_LIM_IDLE,
    SERVO_STR_R_LIM_HOLD,
    SERVO_STR_R_LIM_TRAVEL,
    SERVO_STR_R_LIM_TRAVEL_OFF,
    SERVO_STR_R_LIM_TRAVEL_BOUND,
    SERVO_STR_R_LIM_TRAVEL_NONE,
    SERVO_STR_R_LIM_STALL,
    SERVO_STR_R_LIM_LATE,
    SERVO_STR_R_LIM_UNSEEN,
    SERVO_STR_R_LIM_BO_SEEN,
    SERVO_STR_R_LIM_BO_UNSEEN,
    SERVO_STR_R_NOT_CHECKED,
    SERVO_STR_R_NOT_MEASURED,
    SERVO_STR_R_UNM_HEAD,
    SERVO_STR_R_UNM_POSITION,
    SERVO_STR_R_UNM_PEAKS,
    SERVO_STR_R_UNM_PATH,
    /* The output encoder's. */
    SERVO_STR_R_ENC_DEVICE,
    SERVO_STR_R_ENC_HEAD,
    SERVO_STR_R_ENC_COLUMNS,
    SERVO_STR_R_ENC_CMD,
    SERVO_STR_R_ENC_END,
    SERVO_STR_R_ENC_SETTLED,
    SERVO_STR_R_ENC_TRAVEL,
    SERVO_STR_R_ENC_UNMOVED,
    SERVO_STR_R_ENC_LATE,
    SERVO_STR_R_ENC_DEADBAND,
    SERVO_STR_R_ENC_NONE,
    SERVO_STR_R_ENC_NO_MAGNET,
    SERVO_STR_R_ENC_FIELD,
    SERVO_STR_R_UNM_POSITION_ENC,
    /* The meter's. */
    SERVO_STR_R_LOG_WIN,
    SERVO_STR_R_SUPPLY_PDMINI,
    SERVO_STR_R_CURRENT,
    SERVO_STR_R_CURRENT_INA,
    SERVO_STR_R_VOLTAGE,
    SERVO_STR_R_VOLTAGE_INA,
    SERVO_STR_R_SHUNT,
    SERVO_STR_R_INA_UNUSED,
    SERVO_STR_R_INA_OLD,
    SERVO_STR_R_INA_NOT_HELD,
    SERVO_STR_R_INA_SILENT,
    SERVO_STR_R_INA_NO_WINDOW,
    SERVO_STR_R_INA_RESET,
    SERVO_STR_R_INA_SETTLING,
    SERVO_STR_R_INA_MODEL,
    SERVO_STR_R_READINGS_WIN,
    SERVO_STR_R_SKIPPED_WIN,
    SERVO_STR_R_RESOLUTION_WIN,
    SERVO_STR_R_CLIPPED,
    SERVO_STR_R_CC,
    SERVO_STR_R_NEGATIVE,
    SERVO_STR_R_LIM_STALL_LIMIT,
    SERVO_STR_R_LIM_STALL_RANGE,
    SERVO_STR_R_UNM_POSITION_INA,
    SERVO_STR_R_UNM_PEAKS_INA,
    SERVO_STR_R_METER_MODEL,
    SERVO_STR_COUNT
} servo_str_t;

/** The English text of @p id; "" for one out of range. */
const char *servo_str(servo_str_t id);

/** @p id from @p table, or the English where @p table is NULL or holds
 *  none; "" for one out of range. */
const char *servo_str_in(const char *const *table, servo_str_t id);

/** The entries the names below come from, for a caller with its own
 *  table. */
servo_str_t servo_test_phase_str(servo_test_phase_t ph);
servo_str_t servo_test_abort_str(servo_test_abort_t why);
servo_str_t servo_test_verdict_str(servo_test_verdict_t v);
servo_str_t servo_test_start_str(servo_test_start_t why);

const char *servo_test_phase_name(servo_test_phase_t ph);
const char *servo_test_abort_name(servo_test_abort_t why);
const char *servo_test_verdict_name(servo_test_verdict_t v);
const char *servo_test_start_name(servo_test_start_t why);

/** The CSV's header row: the 13 columns of every run, then the meter's
 *  six -- meter, window, current max, current min, voltage min, clipped. */
const char *servo_test_csv_header(void);
/** And with the encoder's two columns, angle_deg and travel_angle_ms,
 *  between the two groups. */
const char *servo_test_csv_header_enc(void);

/** The word a row's meter column carries: INA3221, PDMINI or MODEL. */
const char *servo_test_meter_word(uint8_t kind);

/**
 * The line that says STALL AT cannot be reached in a run of @p cfg, in the
 * language of @p cfg->text, into @p buf: against the current limit where
 * that is the reason, else against the meter's range
 * (servo_test_stall_unreachable()).  False, and @p buf empty, when a
 * reading can pass STALL AT.
 */
bool servo_test_stall_note(const servo_test_cfg_t *cfg, char *buf, size_t n);

/** Line @p idx of the report into @p buf, in the language of
 *  @p t->cfg.text; false past the last. */
bool servo_report_line(const servo_test_t *t, unsigned idx, char *buf,
                       size_t n);

#ifdef __cplusplus
}
#endif
