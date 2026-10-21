/**
 * @file
 * @brief Limit run: the limit search on the SERVO screen and its programming.
 *
 * The limit run: the limit search (servo_limit.h) run on the SERVO screen
 * against the INA3221's windows, and for a KST or Chaservo servo on the
 * programming port, the servo's own travel limits lowered until neither end
 * binds.
 *
 * The search.  A side is searched from its centre outward, a servo_limit
 * search in steps of 10 us, the position bound once its current is above
 * 1.8 times the free current and 0.15 A above it.  A side whose end is
 * measured free is free.  The endpoints are reported; the bench's own pulse
 * range is not changed.  Where the search runs depends on the target:
 *
 *   - The bench's own PWM output (cfg.read off): from PULSE CENTRE out to
 *     PULSE MIN and PULSE MAX in force.  Nothing is read or written.
 *   - A KST or Chaservo servo on the programming port (cfg.read on): a
 *     read session comes first -- the servo held at PULSE CENTRE, the
 *     bench switched to SAFE, CONNECT with its read, LEAVE, the bench
 *     switched to ARMED again -- and the search runs from the servo's
 *     NEUTRAL out to its own PULSE LOWER and PULSE UPPER, never past them.
 *     A read that fails ends the run before any search.  NEUTRAL not
 *     between PULSE LOWER and PULSE UPPER ends it as FAIL
 *     (SERVO_LIMRUN_WHY_NEUTRAL); PULSE LOWER to PULSE UPPER not inside the
 *     bench's PULSE MIN to PULSE MAX ends it as FAIL
 *     (SERVO_LIMRUN_WHY_RANGE), the bench left SAFE and nothing driven.
 *
 * SERVO PULSES (cfg.set_pulses, programming port only).  At 1000-2000 the
 * read session writes PULSE LOWER 1000.0 us and PULSE UPPER 2000.0 us
 * before anything is driven, and reads them back; NEUTRAL is not written.
 * A servo at those values already gets no write.  The search then runs
 * out to 1000 us and 2000 us.  The pulse write counts toward
 * SERVO_LIMRUN_WRITES_MAX and RESTORE takes it back with the rest.
 *
 * The programming (cfg.program).  LEFT ANGLE and RIGHT ANGLE are the
 * servo's own travel limits, one register each, in a unit that is not known.
 * The run takes the servo as linear: PULSE LOWER and PULSE UPPER are the
 * input pulses at which it reaches the full travel LEFT ANGLE and RIGHT
 * ANGLE set, between NEUTRAL and such a pulse its travel is linear in the
 * pulse, and past it the servo holds that travel.  That is an assumption,
 * not a measurement.  The share of a side at pulse p is
 *
 *     f = (p - NEUTRAL) / (P_lim - NEUTRAL)
 *
 * with P_lim PULSE LOWER on the short-pulse side and PULSE UPPER on the
 * long one, both as the servo holds them when the search runs.  f is formed
 * from input pulses only, so REVERSION does not enter it: REVERSION decides
 * which register limits that side, and the run finds that (below).  A bound
 * side whose last free pulse is not past NEUTRAL ends the run as FAIL
 * (SERVO_LIMRUN_WHY_BEFORE_NEUTRAL) before any write.
 *
 * Two writes a bound side, v0 the value its register holds at the read:
 *
 *   1. The overset: v1 = ceil(v0 x fb x (100 + SERVO_LIMRUN_OVERSET_PCT) /
 *      100), fb the share at the first search's first bound pulse, at most
 *      v0 and at least the smallest value the hard rules take.  The first
 *      bound pulse lies at or past the stop, so the stop stays reachable,
 *      now close to the pulse limit, and the side is searched again.  Free
 *      there, the run ends as FAIL (SERVO_LIMRUN_WHY_NO_STOP) with no
 *      second write: the stop the overset was to keep in reach is not
 *      found.  An overset search that binds far inside the pulse limit
 *      still gives the final value below; no further overset follows.
 *   2. The final value: v2 = floor(v1 x f1), f1 the share at the overset
 *      search's last free pulse, at least the smallest value.  No margin is
 *      added: the last free pulse lies a 10 us step short of the bound one.
 *
 * Worked: NEUTRAL 1500.2 us, PULSE LOWER 900.0 us, v0 150, the stop at
 * 1000 us.  The first search is free at 1000 us and bound at 990 us:
 * fb = 510.2 / 600.2 = 0.850, v1 = ceil(150 x 0.850 x 1.03) = 132.  A
 * linear servo at 132 is free out to 930 us: f1 = 570.2 / 600.2 = 0.950,
 * v2 = floor(132 x 0.950) = 125.  At 125 the full travel ends at 1000.2 us.
 *
 * The overset is left out, and the first search counts as its search,
 * where v1 would not lie below v0 (fb above about 0.97) or where the model
 * moves the stop, estimated at the first search's last free pulse, less
 * than SERVO_LIMRUN_MOVED_US further out: d0 x (v0 - v1) / v1 under 20 us,
 * d0 that pulse's distance from NEUTRAL.  A search could not tell that move
 * from no move.  The final value is then v2 = floor(v0 x f0), f0 the share
 * at the first search's last free pulse.
 *
 * Which pulse side each register limits is not stated by the protocol
 * notes, so the run finds it on the first bound side's first write: that
 * value goes to LEFT ANGLE (RIGHT ANGLE where LEFT ANGLE holds the smallest
 * value already) and the side is searched.  The end moved when it is free
 * now or its endpoint lies SERVO_LIMRUN_MOVED_US further out; it stayed
 * when its endpoint lies within that of where it was.  An end that stayed
 * sends LEFT ANGLE back to its value and the value, worked out again from
 * RIGHT ANGLE's v0, to RIGHT ANGLE; stayed again, the run ends as FAIL.  An
 * end that moved inward ends it as FAIL too: nothing about the servo is
 * guessed.  The register found limits that side, the other one the other
 * side; a register whose side never bound keeps its value.
 *
 * Last, both ends are searched again: PASS when both are free, FAIL when
 * either binds.  No further write follows a FAIL; RESTORE gives the values
 * before the run back.
 *
 * Writes.  A value is written in a session of the programmer: the servo
 * held at the centre for SERVO_LIMRUN_RETURN_MS after the search, so a
 * session never starts with the servo against an end, the bench switched to
 * SAFE, the port taken (CONNECT reads all registers), the write, the port
 * given back (LEAVE), the bench switched to ARMED again.  The read session
 * writes nothing but the pulses of SERVO PULSES; each angle write has a
 * session of its own.  A LEAVE this link or supply does not complete ends
 * the run before any value changes.  No step of the protocol is known to
 * make a value permanent, and no RAM-only write exists: every write is the
 * register write the programmer reads back.  The run counts register values
 * written and stops at SERVO_LIMRUN_WRITES_MAX, the most this flow can
 * need.
 *
 * Protections, for the whole run, whatever phase it is in:
 *
 *   - a 1 ms sample of the window at or above SERVO_LIMRUN_HARD_MA, or a
 *     clipped one (the INA3221 at the end of its range), ends it;
 *   - window means above SERVO_LIMRUN_STALL_MA for SERVO_LIMRUN_STALL_MS
 *     end it;
 *   - no window for SERVO_LIMRUN_WIN_STALE_MS ends it;
 *   - each search has its own cap, SERVO_LIMRUN_SEARCH_MS;
 *   - the search's own protections stay (servo_limit.h).
 *
 * The supply is also held to what it was at the start, its set points and
 * its output, in every phase but the programmer's sessions (CONNECT to
 * LEAVE): those own the port and the supply (kst_prog.h), and a change
 * then is theirs.  While a search drives the servo the rail voltage stays
 * within SERVO_LIMRUN_RAIL_DV_MV of the run's first window.  A search's
 * window without the rail voltage ends the run
 * (SERVO_LIMRUN_WHY_NO_VOLTAGE), so that reference is a window's and no
 * window escapes the band.  A window without a current reading is not
 * counted at all: the meter's silence runs on through it.
 *
 * The caller ends the run (servo_limrun_abort()) for STOP, SAFE, link loss,
 * leaving the screen, the operator taking the servo, changed settings, lost
 * touch events, the INA3221 no longer the meter, and a supply that stops
 * answering, trips, switches off or has its set points changed.  An ended
 * run asks the output off and lets the servo go (servo_limrun_do_t.off and
 * release), and asks SAFE where it waited for ARMED; the values last
 * written stay in the servo.
 *
 * Pure: fed windows, the programmer's results and the clock, it moves
 * nothing itself.  Its report leaves through an outbox
 * (servo_limrun_peek()) as one LIMITnnn.TXT.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "servo_limit.h"
#include "servo_test.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------- the method */

/**
 * @brief Register values a run writes at most.
 *
 * PULSE LOWER and PULSE UPPER for SERVO PULSES at 1000-2000, two a side, the
 * overset and the final value, and two for a first write that went to the
 * register of the other side, that value and LEFT ANGLE's own value back.
 */
#define SERVO_LIMRUN_WRITES_MAX   8u
/**
 * @brief Overset margin, percent: the overset write lies this far above the
 *        share at the first search's first bound pulse.
 *
 * The stop stays in reach, close to the pulse limit.
 */
#define SERVO_LIMRUN_OVERSET_PCT  3u
/**
 * @brief Pulse widths SERVO PULSES at 1000-2000 writes, 0.1 us.
 *
 * SERVO_LIMRUN_STD_LOWER_DUS is 1000.0 us and SERVO_LIMRUN_STD_UPPER_DUS is
 * 2000.0 us.
 */
#define SERVO_LIMRUN_STD_LOWER_DUS 10000u
#define SERVO_LIMRUN_STD_UPPER_DUS 20000u
/**
 * @brief Movement threshold, us: an endpoint that moves this far out moved;
 *        one within it stayed.
 */
#define SERVO_LIMRUN_MOVED_US     20u
/**
 * @brief Time limit of one search of one side, ms.
 *
 * One search of one side at most: 1000 us of travel, PULSE MIN 500 us to a
 * 1500 us centre, is 100 positions of 250 ms, 25 s; the baseline's three and
 * the slower windows of a busy panel fit in the rest.
 */
#define SERVO_LIMRUN_SEARCH_MS    40000u
/**
 * @brief Time the servo is held at PULSE CENTRE before a step, ms.
 *
 * Each search starts with the servo held at PULSE CENTRE this long, and each
 * session waits this long at PULSE CENTRE before the bench is asked to SAFE:
 * the return from the last end, 600 us at 0.67 us/ms (0.3 s for 60 degrees)
 * is 900 ms, and the search's baseline is not to see it travel.
 */
#define SERVO_LIMRUN_RETURN_MS    1000u
/**
 * @brief Time limit of one programmer step, ms.
 *
 * One step of the programmer -- CONNECT with its read, a write, LEAVE -- at
 * most: past the programmer's own 120 s of an operation.
 */
#define SERVO_LIMRUN_PROG_MS      150000u
/** @brief Time the bench has to switch to SAFE or to ARMED, ms. */
#define SERVO_LIMRUN_BENCH_MS     3000u
/**
 * @brief Protection thresholds: hard current, stall current and stall time.
 *
 * The protections: a 1 ms sample, and the window means for a time.
 * SERVO_LIMRUN_HARD_MA and SERVO_LIMRUN_STALL_MA are in mA.
 */
#define SERVO_LIMRUN_HARD_MA      3000
#define SERVO_LIMRUN_STALL_MA     1000
#define SERVO_LIMRUN_STALL_MS     400u
/**
 * @brief No window for this long ends the run, ms.
 *
 * The automatic test's figure.
 */
#define SERVO_LIMRUN_WIN_STALE_MS SERVO_TEST_WIN_STALE_MS
/**
 * @brief Rail voltage band, mV: a window's mean this far from the first
 *        window's ends the run as a changed supply.
 */
#define SERVO_LIMRUN_RAIL_DV_MV   1000
/**
 * @brief Lowest servo rail voltage a run starts on, mV.
 *
 * The servo rail a run starts on at least, the last INA3221 window's mean
 * (the screen's START check): an output that is off reads 0 V, and a search
 * there finds two free ends on a servo that never moved.  3.0 V lies under
 * the 4.8 V servos are commonly specified from, so a single lithium cell's
 * rail, 3.0 V to 4.2 V, still starts.  The voltage a given servo stops moving
 * at is not measured.
 */
#define SERVO_LIMRUN_RAIL_MIN_MV  3000

/* ------------------------------------------------------ what it is told */

/** @brief The two sides of PULSE CENTRE: shorter and longer pulses. */
enum { SERVO_LIMRUN_LO = 0, SERVO_LIMRUN_HI = 1 };
/** @brief The two registers: LEFT ANGLE and RIGHT ANGLE. */
enum { SERVO_LIMRUN_LEFT = 0, SERVO_LIMRUN_RIGHT = 1 };
/** @brief Indexes of the servo's pulses in pulse_dus. */
enum {
    SERVO_LIMRUN_P_NEUTRAL = 0,
    SERVO_LIMRUN_P_LOWER = 1,
    SERVO_LIMRUN_P_UPPER = 2,
};

/** @brief Configuration of one run, given to servo_limrun_start(). */
typedef struct {
    uint16_t centre_us, min_us, max_us;   /**< PULSE CENTRE, MIN, MAX   */
    /** A KST or Chaservo servo on the programming port: read first, and
     *  searched from its NEUTRAL out to its PULSE LOWER and PULSE UPPER. */
    bool     read;
    bool     program;     /**< lower LEFT and RIGHT ANGLE after the search;
                               read only */
    bool     set_pulses;  /**< PULSE LOWER and PULSE UPPER to 1000 and
                               2000 us before the search; read only */

    /* For the report only. */
    char     dut[24];     /**< device under test name, 24 bytes          */
    char     type[20];    /**< servo type label, 20 bytes                */
    uint16_t frame_hz;    /**< PWM frame rate, Hz                        */
    char     output[28];  /**< what it drives: "PROG PORT GP13", "GP2"  */
    uint16_t shunt_dmohm; /**< the INA3221's shunt, 0.1 mOhm           */
    char     meter[20];   /**< its channel: "INA3221 0x41 CH2"; "" names
                               it "INA3221"                             */
    bool     enc_on;      /**< an AS5600 reads the horn shaft           */
    char     firmware[16]; /**< firmware version text, 16 bytes         */
    /** The report's language: SERVO_STR_COUNT entries, NULL for English. */
    const char *const *text;
} servo_limrun_cfg_t;

/** @brief The servo as the programmer read it. */
typedef struct {
    bool     ok;          /**< read, a KST or Chaservo servo, editable   */
    uint16_t angle[2];    /**< LEFT and RIGHT ANGLE                       */
    uint16_t lo_raw;      /**< the smallest value the hard rules take    */
    uint16_t hi_raw;      /**< the largest value the hard rules take     */
    bool     reversion;   /**< REVERSION on                               */
    unsigned type;        /**< the servo's type number, 0 not known       */
    /** NEUTRAL, PULSE LOWER and PULSE UPPER, 0.1 us. */
    uint16_t pulse_dus[3];
    /** The file the values before the run are backed up in; "" for the
     *  panel's memory only. */
    char     backup[16];
} servo_limrun_servo_t;

/** @brief What the caller knows each step. */
typedef struct {
    bool  live;           /**< the bench is ARMED and the value live      */
    bool  safe;           /**< the bench is SAFE, no ARM on its way       */
    bool  enc_valid;      /**< the AS5600 has an angle                    */
    float enc_deg;        /**< that angle from ENC CENTRE                 */
} servo_limrun_in_t;

/** @brief What the run wants done. */
typedef struct {
    bool     command;     /**< the servo to cmd_us, at once               */
    uint16_t cmd_us;
    bool     release;     /**< the servo let go                           */
    bool     off;         /**< the supply's output off                    */
    bool     disarm;      /**< the bench to SAFE: the run's own           */
    bool     arm;         /**< the bench to ARMED: the run's own          */
    bool     connect;     /**< the programmer: CONNECT, which reads       */
    bool     write;       /**< the programmer: these two values           */
    uint16_t angle[2];
    bool     pulses;      /**< the programmer: PULSE LOWER and PULSE UPPER,
                               0.1 us                                     */
    uint16_t pulse_dus[2];
    bool     leave;       /**< the programmer: LEAVE                      */
} servo_limrun_do_t;

/* ------------------------------------------------------ what it reports */

/** @brief Life cycle of a run. */
typedef enum {
    SERVO_LIMRUN_IDLE = 0,     /**< not started                               */
    SERVO_LIMRUN_RUNNING,      /**< in progress                               */
    SERVO_LIMRUN_DONE,         /**< ended; verdict and why are set            */
} servo_limrun_state_t;

/** @brief Phase of a running run. */
typedef enum {
    SERVO_LIMRUN_PH_NONE = 0,   /**< no phase set                          */
    SERVO_LIMRUN_PH_SEARCH,     /**< the first search of a side          */
    SERVO_LIMRUN_PH_SAFE,       /**< the servo back at the centre, then
                                     the bench on its way to SAFE        */
    SERVO_LIMRUN_PH_CONNECT,    /**< CONNECT asked of the programmer     */
    SERVO_LIMRUN_PH_WRITE,      /**< a value or the pulses being written */
    SERVO_LIMRUN_PH_LEAVE,      /**< LEAVE asked of the programmer       */
    SERVO_LIMRUN_PH_ARM,        /**< the bench on its way to ARMED       */
    SERVO_LIMRUN_PH_PROBE,      /**< a side searched after a write: the
                                     overset's search, or the first
                                     write's for the side it limits      */
    SERVO_LIMRUN_PH_CHECK,      /**< both sides searched at the end      */
} servo_limrun_phase_t;

/** @brief Outcome of a run. */
typedef enum {
    SERVO_LIMRUN_FOUND = 0,     /**< search only: the ends as found      */
    SERVO_LIMRUN_FREE,          /**< neither end binds: nothing written  */
    SERVO_LIMRUN_PASS,          /**< programmed, both ends free after    */
    SERVO_LIMRUN_FAIL,          /**< ended with a failure; why is set    */
    SERVO_LIMRUN_ABORTED,       /**< stopped by the caller or a protection */
} servo_limrun_verdict_t;

/** @brief Why a run failed or was aborted. */
typedef enum {
    SERVO_LIMRUN_WHY_NONE = 0,      /**< no reason: the run did not fail    */
    SERVO_LIMRUN_WHY_TEST,          /**< a reason of servo_test_abort_t,
                                         in test_why                      */
    SERVO_LIMRUN_WHY_HARD,          /**< a sample at SERVO_LIMRUN_HARD_MA */
    SERVO_LIMRUN_WHY_CLIPPED,       /**< a sample at the meter's end      */
    SERVO_LIMRUN_WHY_STALL,         /**< above SERVO_LIMRUN_STALL_MA      */
    SERVO_LIMRUN_WHY_SEARCH_LONG,   /**< a search past its cap            */
    SERVO_LIMRUN_WHY_SUPPLY_CHANGED,/**< set points or rail voltage moved */
    SERVO_LIMRUN_WHY_BENCH,         /**< the bench did not switch         */
    SERVO_LIMRUN_WHY_NOT_KST,       /**< no KST or Chaservo servo read    */
    SERVO_LIMRUN_WHY_PROG,          /**< CONNECT or LEAVE did not end well */
    SERVO_LIMRUN_WHY_WRITE,         /**< a write refused or lost          */
    SERVO_LIMRUN_WHY_OTHER_VALUES,  /**< the servo read holds other values */
    SERVO_LIMRUN_WHY_MAPPING,       /**< which end LEFT ANGLE moves is not
                                         found                            */
    SERVO_LIMRUN_WHY_SMALLEST,      /**< binds at the smallest value      */
    SERVO_LIMRUN_WHY_WRITE_CAP,     /**< SERVO_LIMRUN_WRITES_MAX reached  */
    SERVO_LIMRUN_WHY_STILL_BINDS,   /**< an end binds after programming   */
    SERVO_LIMRUN_WHY_NO_VOLTAGE,    /**< a search's window without the rail
                                         voltage                          */
    SERVO_LIMRUN_WHY_NO_STOP,       /**< free after the overset write     */
    SERVO_LIMRUN_WHY_BEFORE_NEUTRAL,/**< a free end not past NEUTRAL      */
    SERVO_LIMRUN_WHY_NEUTRAL,       /**< NEUTRAL not between PULSE LOWER and
                                         PULSE UPPER                      */
    SERVO_LIMRUN_WHY_RANGE,         /**< PULSE LOWER to PULSE UPPER not
                                         inside PULSE MIN to PULSE MAX    */
    SERVO_LIMRUN_WHY_COUNT          /**< number of reasons                */
} servo_limrun_why_t;

/** @brief What one search of one side found. */
typedef struct {
    bool     done;          /**< the search has ended                      */
    bool     bound;         /**< FOUND: the side binds                     */
    uint16_t limit_us;      /**< the endpoint; the end of the travel if free */
    uint16_t bind_us;       /**< the first bound position                  */
    uint16_t free_us;       /**< the last free position                    */
    float    baseline_a, knee_a; /**< baseline and knee current, A          */
    bool     enc;           /**< enc_deg is the angle at the last free one */
    float    enc_deg;
} servo_limrun_side_t;

/** @brief Outbox size constant: 2. */
#define SERVO_LIMRUN_OUTBOX 2u

/** @brief Complete state of one run, advanced by servo_limrun_step(). */
typedef struct {
    servo_limrun_cfg_t   cfg;
    servo_limrun_state_t state;      /**< IDLE, RUNNING or DONE             */
    servo_limrun_phase_t phase;      /**< phase of the run                  */
    servo_limrun_verdict_t verdict;  /**< outcome, set when DONE            */
    servo_limrun_why_t   why;        /**< reason for FAIL or ABORTED        */
    servo_test_abort_t   test_why;   /**< reason when why is WHY_TEST       */
    uint32_t start_ms, end_ms, phase_ms; /**< run start, end, phase start, ms */
    bool     safe_asked;    /**< PH_SAFE: SAFE asked, after the return    */

    /* The search under way: from ctr_us out to end_us of its side. */
    uint16_t ctr_us;        /**< search centre, us                        */
    uint16_t end_us[2];     /**< search end of each side, us              */
    servo_limit_t lf;       /**< the limit search under way               */
    uint8_t  side;          /**< the side it searches                    */
    uint32_t search_ms;     /**< the search's start; it walks from
                                 SERVO_LIMRUN_RETURN_MS after it          */
    uint16_t cmd_us;        /**< the command last asked                   */
    bool     cmd_due;       /**< cmd_us is still to be handed out         */
    bool     enc_valid;     /**< the AS5600 angle is valid                */
    float    enc_deg;       /**< AS5600 angle from ENC CENTRE, degrees    */

    servo_limrun_side_t first[2];   /**< the first search of each side  */
    servo_limrun_side_t after[2];   /**< the last check of each side    */
    servo_limrun_side_t probe;      /**< the last probe                 */

    /* The programming. */
    bool     read_once;     /**< the servo has been read once             */
    bool     dry;           /**< the read-only first session is under way */
    servo_limrun_servo_t servo;     /**< as the first session read it  */
    uint16_t cur[2];        /**< what the servo holds, register by register */
    uint16_t pulse_now[3];  /**< its NEUTRAL, PULSE LOWER, PULSE UPPER now */
    bool     pulse_op;      /**< the write under way is the pulse write   */
    bool     searched;      /**< the first search has begun              */
    uint16_t want[2];       /**< the write under way                     */
    int8_t   reg_side[2];   /**< the side each register limits, -1 not known */
    bool     need[2];       /**< the side bound in the first search       */
    bool     mapped;        /**< the register of the first side is known  */
    uint8_t  map_side;      /**< the side whose first write finds it      */
    uint8_t  map_reg;       /**< the register that write goes to          */
    uint8_t  map_tries;     /**< registers tried for it                   */
    /* By side. */
    uint8_t  stage[2];      /**< servo_limrun_int.h's limrun_stage_t      */
    uint16_t v0[2];         /**< the register's value at the read         */
    uint16_t v1[2];         /**< the overset; 0 where it is left out      */
    uint16_t v2[2];         /**< the final value, once known              */
    int32_t  d0_dus[2];     /**< the first search's free end from NEUTRAL */
    int32_t  db_dus[2];     /**< its first bound pulse from NEUTRAL       */
    int32_t  d1_dus[2];     /**< the overset search's; 0 not searched     */
    int8_t   probe_side;    /**< the side the next search looks at, -1 both */
    unsigned writes;        /**< register values written                  */
    unsigned write_ops;     /**< writes sent, one that failed included    */
    unsigned sessions;      /**< CONNECT answers received                 */
    bool     programmed;    /**< a value differs from the servo's own, or
                                 *   a write failed once sent              */

    /* The meter. */
    bool     have_win;      /**< a window has been received               */
    uint32_t last_win_ms;   /**< time of the last window, ms              */
    uint32_t windows;       /**< windows received with a current reading  */
    bool     have_rail;     /**< rail_mv holds the first search window's rail */
    uint16_t rail_mv;       /**< rail voltage reference, mV               */
    bool     elevated;      /**< window means are above SERVO_LIMRUN_STALL_MA */
    uint32_t elevated_ms;   /**< start of that elevation, ms              */

    /* The supply as at its first reading, when one answered. */
    bool     sup_watch;     /**< the supply is watched from its first reading */
    bool     sup_online;    /**< it answered                             */
    bool     sup_out;       /**< its output was on                       */

    float    sup_v, sup_i;  /**< set voltage V, set current A, first reading */

    /* The outbox: OPEN_TXT, the report's lines, END. */
    unsigned out_line;      /**< the next line of the report              */
    bool     out_open, out_end, out_done; /**< OPEN_TXT out, END next,
                                               report out in full */
    char     scratch[SERVO_TEST_LINE_MAX]; /**< the line being handed out  */

    servo_limrun_do_t pend; /**< what the next servo_limrun_step() returns */
} servo_limrun_t;

/**
 * @brief Clear a run state to idle.
 *
 * Does nothing when r is NULL.
 *
 * @param r  Run state to clear.
 */
void servo_limrun_init(servo_limrun_t *r);

/**
 * @brief Start a run.
 *
 * Starts at now_ms. False, and nothing started, for a configuration whose
 * centre is not between its MIN and MAX.
 *
 * @param r       Run state. Cleared before the run starts.
 * @param cfg     Configuration of the run.
 * @param now_ms  Start time, ms.
 * @return true when the run started.
 */
bool servo_limrun_start(servo_limrun_t *r, const servo_limrun_cfg_t *cfg,
                        uint32_t now_ms);

/**
 * @brief Feed one INA3221 window, as the automatic test takes it.
 *
 * @param r  Run state.
 * @param w  Window to feed. A window without a current reading is ignored.
 */
void servo_limrun_window(servo_limrun_t *r, const servo_test_win_t *w);

/**
 * @brief Feed the supply's reading.
 *
 * The first one after the start, answering or not, is what it must stay at:
 * its set points, and its output on or off. An output off then and on later,
 * or a supply that did not answer and answers later, its output on or off,
 * ends the run as a changed supply (SERVO_LIMRUN_WHY_SUPPLY_CHANGED). The
 * caller passes a reading that does not answer where it has none at the
 * start.
 *
 * @param r       Run state.
 * @param s       Supply reading.
 * @param now_ms  Time of the reading, ms.
 */
void servo_limrun_supply(servo_limrun_t *r, const servo_test_reading_t *s,
                         uint32_t now_ms);

/**
 * @brief Feed the programmer's answer to CONNECT: the servo read.
 *
 * @param r       Run state.
 * @param sv      Servo as read.
 * @param now_ms  Time of the answer, ms.
 */
void servo_limrun_connected(servo_limrun_t *r, const servo_limrun_servo_t *sv,
                       uint32_t now_ms);

/**
 * @brief Feed the end of a write: the values the servo holds.
 *
 * @param r       Run state.
 * @param ok      false when the write was not answered or not taken.
 * @param angle   LEFT and RIGHT ANGLE the servo holds, two values.
 * @param now_ms  Time of the answer, ms.
 */
void servo_limrun_written(servo_limrun_t *r, bool ok, const uint16_t *angle,
                          uint32_t now_ms);

/**
 * @brief Feed the end of the pulse write: the pulses the servo holds.
 *
 * @param r          Run state.
 * @param ok         false when the write was not answered or not taken.
 * @param pulse_dus  NEUTRAL, PULSE LOWER and PULSE UPPER, 0.1 us.
 * @param now_ms     Time of the answer, ms.
 */
void servo_limrun_pulsed(servo_limrun_t *r, bool ok,
                         const uint16_t *pulse_dus, uint32_t now_ms);

/**
 * @brief Feed the answer to LEAVE: the port is given back.
 *
 * @param r       Run state.
 * @param ok      false when LEAVE did not end well.
 * @param now_ms  Time of the answer, ms.
 */
void servo_limrun_left(servo_limrun_t *r, bool ok, uint32_t now_ms);

/**
 * @brief Report a PD mini the caller will not hand the programmer's CONNECT.
 *
 * The caller will not hand the programmer's CONNECT a PD mini that the run
 * did not start with, or one whose output is off (@p output_on false). A
 * supply the run started with, its output off, ends the run as a reading
 * outside a session does (SERVO_TEST_AB_SUPPLY_OFF); one the run did not
 * start with ends it as a changed supply (SERVO_LIMRUN_WHY_SUPPLY_CHANGED).
 *
 * @param r          Run state.
 * @param output_on  Whether the supply's output is on.
 * @param now_ms     Time of the refusal, ms.
 */
void servo_limrun_supply_refused(servo_limrun_t *r, bool output_on,
                                 uint32_t now_ms);

/**
 * @brief Name the file the servo's values before the run were saved in.
 *
 * The file is named once the programmer names it: its first write saves it.
 *
 * @param r     Run state.
 * @param name  File name. Ignored when NULL or empty, or when the servo was
 *              not read.
 */
void servo_limrun_backup(servo_limrun_t *r, const char *name);

/**
 * @brief Report that the caller refused the programmer's CONNECT, write or
 *        LEAVE: it never started.
 *
 * @param r       Run state.
 * @param now_ms  Time of the refusal, ms.
 */
void servo_limrun_refused(servo_limrun_t *r, uint32_t now_ms);

/**
 * @brief Advance the run to now_ms.
 *
 * What to do comes back in out.
 *
 * @param r       Run state.
 * @param now_ms  Current time, ms.
 * @param in      What the caller knows this step.
 * @param out     Out: what the run wants done. Cleared first. NULL to skip.
 */
void servo_limrun_step(servo_limrun_t *r, uint32_t now_ms,
                       const servo_limrun_in_t *in, servo_limrun_do_t *out);

/**
 * @brief Abort the run on the caller's reason.
 *
 * @param r       Run state. Does nothing unless the run is RUNNING.
 * @param why     Reason from the caller.
 * @param now_ms  Time of the abort, ms.
 */
void servo_limrun_abort(servo_limrun_t *r, servo_test_abort_t why,
                        uint32_t now_ms);

/**
 * @brief True while the run is RUNNING.
 *
 * @param r  Run state. NULL gives false.
 * @return true when the state is RUNNING.
 */
bool servo_limrun_running(const servo_limrun_t *r);
/**
 * @brief True while the run asks the bench to SAFE or to ARMED.
 *
 * The run asked the bench to SAFE or to ARMED and waits for it, or holds the
 * programmer: a disarm then is its own, not the operator's. Not while the
 * servo returns to the centre before a session: a disarm then is the
 * operator's.
 *
 * @param r  Run state.
 * @return true while the bench is being switched.
 */
bool servo_limrun_switching(const servo_limrun_t *r);
/**
 * @brief True while the run holds the programmer: CONNECT to LEAVE.
 *
 * @param r  Run state.
 * @return true in the CONNECT, WRITE and LEAVE phases.
 */
bool servo_limrun_programming(const servo_limrun_t *r);
/**
 * @brief True once a value was written and the servo may not hold its own.
 *
 * @param r  Run state.
 * @return true when at least one register value was written.
 */
bool servo_limrun_wrote(const servo_limrun_t *r);
/**
 * @brief True when RESTORE gives back the servo's values from before the run.
 *
 * The run ended as FAIL or ABORTED with the servo's values changed: RESTORE
 * gives back the ones before it.
 *
 * @param r  Run state.
 * @return true when RESTORE is offered.
 */
bool servo_limrun_restore(const servo_limrun_t *r);
/**
 * @brief Read the values the servo held before the run.
 *
 * Valid once read.
 *
 * @param r      Run state.
 * @param left   Out: LEFT ANGLE before the run. NULL to skip.
 * @param right  Out: RIGHT ANGLE before the run. NULL to skip.
 * @return true when the servo was read, false otherwise.
 */
bool servo_limrun_original(const servo_limrun_t *r, uint16_t *left,
                           uint16_t *right);

/**
 * @brief Look at the next outbox entry without taking it.
 *
 * The report's lines, in order, then END; OPEN_TXT first.
 *
 * @param r     Run state.
 * @param text  Out: the entry's text.
 * @return The kind of entry, servo_test_out_t.
 */
servo_test_out_t servo_limrun_peek(servo_limrun_t *r, const char **text);
/**
 * @brief Take the outbox entry last returned by servo_limrun_peek().
 *
 * @param r  Run state.
 */
void servo_limrun_pop(servo_limrun_t *r);
/**
 * @brief True when the report has been handed out in full.
 *
 * @param r  Run state.
 * @return true after the END entry.
 */
bool servo_limrun_drained(const servo_limrun_t *r);

/**
 * @brief Format one line of the report.
 *
 * Line idx of the report into buf; false past the last.
 *
 * @param r    Run state.
 * @param idx  Line index, from 0.
 * @param buf  Out: the line text.
 * @param n    Size of buf, bytes.
 * @return true when line idx exists.
 */
bool servo_limrun_report_line(const servo_limrun_t *r, unsigned idx,
                              char *buf, size_t n);

/**
 * @brief Name table entry for a verdict.
 *
 * The table entries the screen and the report name these by.
 *
 * @param v  Verdict to name.
 * @return The table entry for the verdict, servo_str_t.
 */
servo_str_t servo_limrun_verdict_str(servo_limrun_verdict_t v);

/**
 * @brief Name table entry for the reason of a run.
 *
 * The table entries the screen and the report name these by.
 *
 * @param r  Run state whose why selects the reason.
 * @return The table entry for the reason, servo_str_t.
 */
servo_str_t servo_limrun_why_str(const servo_limrun_t *r);

/**
 * @brief Name table entry for a phase.
 *
 * The table entries the screen and the report name these by.
 *
 * @param ph  Phase to name.
 * @return The table entry for the phase, servo_str_t.
 */
servo_str_t servo_limrun_phase_str(servo_limrun_phase_t ph);

/**
 * @brief Distance of a pulse from NEUTRAL on one side.
 *
 * The share of travel, 0.1 us of pulse: @p p_us's distance from NEUTRAL on
 * @p side, positive outward, and the pulse limit's.
 *
 * @param sv     Servo pulses, 0.1 us.
 * @param side   SERVO_LIMRUN_LO or SERVO_LIMRUN_HI.
 * @param p_us   Pulse width, microseconds.
 * @return Distance from NEUTRAL, 0.1 us, positive outward.
 */
int32_t servo_limrun_dist(const servo_limrun_servo_t *sv, uint8_t side,
                          uint16_t p_us);
/**
 * @brief Span of one side, as servo_limrun_span() reports it.
 *
 * @param sv    Servo pulses, 0.1 us.
 * @param side  SERVO_LIMRUN_LO or SERVO_LIMRUN_HI.
 * @return Distance from NEUTRAL to the side's pulse limit, 0.1 us.
 */
int32_t servo_limrun_span(const servo_limrun_servo_t *sv, uint8_t side);
/**
 * @brief The overset value for a register.
 *
 * The overset for value @p v0 and share @p d / @p span: ceil(v0 x share x
 * (100 + SERVO_LIMRUN_OVERSET_PCT) / 100), within @p lo_raw to @p v0.
 *
 * @param v0     Register value at the read.
 * @param d      Distance of the bound pulse from NEUTRAL, 0.1 us.
 * @param span   Span of the side, 0.1 us.
 * @param lo_raw Smallest value the hard rules take.
 * @return The overset value.
 */
uint16_t servo_limrun_overset(uint16_t v0, int32_t d, int32_t span,
                              uint16_t lo_raw);
/**
 * @brief The final value for a register.
 *
 * The final value for value @p v and share @p d / @p span: floor(v x share),
 * within @p lo_raw to @p v.
 *
 * @param v      Value the share applies to.
 * @param d      Distance of the free pulse from NEUTRAL, 0.1 us.
 * @param span   Span of the side, 0.1 us.
 * @param lo_raw Smallest value the hard rules take.
 * @return The final value.
 */
uint16_t servo_limrun_scaled(uint16_t v, int32_t d, int32_t span,
                             uint16_t lo_raw);

#ifdef __cplusplus
}
#endif
