/**
 * @file
 * @brief AUTO end points: a run that drives a servo to 1000us and 2000us,
 *        measures the surface's deflection on the angle source, and
 *        programs the servo's end points so that each side reaches its
 *        target times (1 + margin).  Pure: fed the angle, the meter's
 *        windows, the programmer's answers and the clock, it asks the
 *        caller to move, write and switch, as servo_limrun does.
 *
 * Steps: the servo's read (CONNECT, LEAVE); the pulse range 1000 to 2000us
 * (KST: PULSE LOWER and PULSE UPPER, as SERVO PULSES; Hitec: nothing, the
 * D series takes 1000us as 76.9% of the way to 0xB2, hitec_live.h); then
 * per side, at most SERVO_RAUTO_WRITES_MAX times: move to the end pulse,
 * wait for still (SERVO_RAUTO_STILL_MS within IMU_STILL_DEG, at most
 * SERVO_RAUTO_MOVE_MS), measure, done when |measured - goal| <= tol, else
 * value = value x goal / measured within lo..hi (a linear servo between
 * neutral and the end), write in a session, move again.  Which register
 * moves which side is found on the first write as the limit run finds it
 * (the side that moved).  The current guard of the limit run holds
 * throughout (SERVO_LIMRUN_HARD_MA, SERVO_LIMRUN_STALL_MA for
 * SERVO_LIMRUN_STALL_MS): a stall before the goal ends the run with
 * SURFACE BINDS AT x°; the angle lost longer than SERVO_GAP_MAX_MS ends it
 * (servo_gap.h); every end switches the output off and asks SAFE.
 * Report RUDDRnnn.TXT; CSV RUDDRnnn.CSV one row a step, the CSV handed out
 * first, as the automatic test's.
 *
 * In detail:
 *
 *   - The read session: the servo held at the bench's PULSE CENTRE for
 *     SERVO_LIMRUN_RETURN_MS, SAFE asked, CONNECT with its read (phase
 *     READ), for KST the pulse write when PULSE LOWER and PULSE UPPER are
 *     not 1000.0us and 2000.0us, LEAVE, ARMED asked.  A read that is no
 *     servo, or whose values lie outside its own lo..hi, ends the run as
 *     FAIL (SERVO_RAUTO_WHY_NO_SERVO) with LEAVE asked.
 *   - A measure: the end pulse commanded (phase MOVE), the surface judged
 *     from SERVO_RAUTO_STILL_MS after the command on (phase SETTLE): the
 *     first step with a valid angle still for SERVO_RAUTO_STILL_MS takes
 *     the angle.  None by SERVO_RAUTO_MOVE_MS after the command ends the
 *     run as FAIL (SERVO_RAUTO_WHY_NOT_STILL).  A side measured again after
 *     a write to the other register is a CHECK.
 *   - A side's direction is the sign of its first measure, which must lie
 *     at least the tolerance from the zero unless it is within the
 *     tolerance of the goal; the two sides of opposite signs.  Otherwise,
 *     or a later measure at or past the zero, the run ends as FAIL
 *     (SERVO_RAUTO_WHY_MAPPING).  The measured deflection of a side is
 *     taken along its direction, so it is positive.
 *   - A side outside the tolerance gets its next value from
 *     servo_rauto_scaled().  A value that rounds to the one in force moves
 *     one count toward the goal.  One that cannot move because the goal
 *     needs more than lo..hi gives ends the run as FAIL
 *     (SERVO_RAUTO_WHY_LIMIT), while the side's register is not found yet
 *     only once neither register can move; a side measured outside after
 *     SERVO_RAUTO_WRITES_MAX writes as FAIL (SERVO_RAUTO_WHY_WRITE_CAP).
 *   - The first write goes to the first register (KST LEFT ANGLE), or to
 *     the second where the first cannot move toward the goal, and the
 *     side is measured again.  The side moved when it lands within the
 *     tolerance or travels at least half the change the model gives,
 *     toward it; it moved the wrong way when it travels that far the other
 *     way, which ends the run as FAIL (SERVO_RAUTO_WHY_MAPPING); otherwise
 *     it stayed: the first register goes back to its value and the side's
 *     value, worked out from the second register, goes to that one, and
 *     the side is measured again.  Stayed a second time, the run ends as
 *     FAIL (SERVO_RAUTO_WHY_MAPPING).  The register found moves that side,
 *     the other one the other side.
 *   - A write session: the servo held at PULSE CENTRE for
 *     SERVO_LIMRUN_RETURN_MS, SAFE asked, CONNECT, the write, LEAVE, ARMED
 *     asked, the written side measured.  A CONNECT that reads other values
 *     than those the run left ends it (SERVO_RAUTO_WHY_PROG); a write that
 *     reads back other values ends it (SERVO_RAUTO_WHY_WRITE).
 *   - PASS: both sides within the tolerance, each measured after the last
 *     write.
 *
 * Every end lets the servo go and asks SAFE, and switches the output off;
 * an end inside the programmer's session that is no abort (a read that is
 * no servo) asks LEAVE in place of the output off, so the port comes
 * back.  The values last written stay in the servo; RESTORE on the
 * programming page gives back those before the run after a FAIL or an
 * abort (servo_rauto_restore()).
 *
 * The protections, in every phase: a 1ms sample at or above
 * SERVO_LIMRUN_HARD_MA, or a clipped one, ends the run
 * (SERVO_RAUTO_WHY_HARD); window means above SERVO_LIMRUN_STALL_MA for
 * SERVO_LIMRUN_STALL_MS end it, as SERVO_RAUTO_WHY_BINDS with the angle
 * while a measure drives the servo and the surface lies more than the
 * tolerance short of its goal, else as SERVO_RAUTO_WHY_STALL; no window
 * for SERVO_LIMRUN_WIN_STALE_MS ends it; the angle source not answering
 * for longer than SERVO_GAP_MAX_MS, or again sooner than
 * SERVO_GAP_EVERY_MS after the start of the last gap, ends it
 * (SERVO_RAUTO_WHY_ANGLE).  A measure takes no angle inside a gap.
 * SERVO_LIMRUN_BENCH_MS bounds each switch to SAFE or ARMED and
 * SERVO_LIMRUN_PROG_MS each step of the programmer.
 *
 * The caller ends the run (servo_rauto_abort()) for STOP, SAFE, link loss,
 * leaving the task, the operator taking the servo, changed settings, lost
 * touch events, the INA3221 no longer the meter, and a supply that stops
 * answering, trips, switches off or has its set points changed.
 *
 * The model: the deflection at an end pulse is linear in the end point's
 * value, from the surface's zero.  That is an assumption, not a
 * measurement; the run measures after every write.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "servo_angle.h"
#include "servo_gap.h"
#include "servo_limrun.h"
#include "servo_test.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Writes a side takes at most. */
#define SERVO_RAUTO_WRITES_MAX  8u
/**
 * @brief The measures a run can take: between two writes at most two (the
 *        written side, then the other side's CHECK), and as many before
 *        the first write and after the last, over 2 x SERVO_RAUTO_WRITES_MAX
 *        writes.
 */
#define SERVO_RAUTO_STEPS_MAX   (2u * (2u * SERVO_RAUTO_WRITES_MAX + 1u))
/** @brief A measure takes the angle once it is still this long, ms. */
#define SERVO_RAUTO_STILL_MS    500u
/** @brief A measure ends the run when the angle is not still this long after
 *         the command, ms. */
#define SERVO_RAUTO_MOVE_MS     3000u
/** @brief The largest margin, %. */
#define SERVO_RAUTO_MARGIN_MAX  50u      /**< % */
/** @brief The tolerance's range, 0.1°. */
#define SERVO_RAUTO_TOL_MIN_DDEG 1u      /**< 0.1° */
#define SERVO_RAUTO_TOL_MAX_DDEG 50u     /**< 5.0° */
/** @brief The outbox: the report's files, TXT and CSV. */
#define SERVO_RAUTO_OUTBOX      2u

/** @brief The servo's family: which programmer's registers the values are. */
typedef enum {
    SERVO_RAUTO_FAM_KST = 0,
    SERVO_RAUTO_FAM_HITEC_D,
    SERVO_RAUTO_FAM_HITEC_C,
} servo_rauto_family_t;

/** @brief What the run is asked. */
typedef struct {
    float    target_deg[2];   /**< at 1000us (0) and 2000us (1), magnitudes */
    uint8_t  margin_pct;      /**< 0 to SERVO_RAUTO_MARGIN_MAX */
    uint16_t tol_ddeg;        /**< 0.1° */
    uint8_t  family;          /**< servo_rauto_family_t */
    uint16_t centre_us;       /**< the bench's PULSE CENTRE */
    float    len_mm;          /**< 0 for none: the report in ° only */
    /* For the report only. */
    char     dut[24];         /**< DUT NAME */
    char     type[20];        /**< the servo type's name */
    char     output[28];      /**< what it drives: "PROG PORT 1 GPIO 13" */
    char     meter[20];       /**< the INA3221 channel; "" names it INA3221 */
    char     source[12];      /**< servo_angle_name() */
    char     firmware[16];    /**< the bench's version */
    const char *const *text;  /**< SERVO_STR_COUNT entries, NULL English */
} servo_rauto_cfg_t;

/** @brief The servo as read, in travel units (see IMPL.md 3.5). */
typedef struct {
    bool     ok;              /**< read, a servo of the family, editable */
    uint16_t value[2];        /**< the two registers' travel values */
    uint16_t lo,              /**< the smallest value the hard rules take */
             hi;              /**< the largest */
    bool     reversion;       /**< REVERSION on */
    uint16_t pulse_dus[3];    /**< KST: NEUTRAL, LOWER, UPPER; else 0 */
    char     backup[16];      /**< the backup file; "" for memory only */
} servo_rauto_servo_t;

/** @brief What the caller knows each step. */
typedef struct {
    bool  live,               /**< the bench is ARMED and the value live */
          safe;               /**< the bench is SAFE, no ARM on its way */
    bool  angle_valid;        /**< the angle source has an angle */
    float angle_deg;          /**< deflection from the zero */
    uint16_t still_ms;        /**< the angle still this long, ms */
} servo_rauto_in_t;

/** @brief What the run wants done. */
typedef struct {
    bool     command;         /**< the servo to cmd_us, at once */
    uint16_t cmd_us;          /**< the pulse, us */
    bool     release,         /**< the servo let go */
             off,             /**< the supply's output off */
             disarm,          /**< the bench to SAFE */
             arm;             /**< the bench to ARMED */
    bool     connect,         /**< the programmer: CONNECT, which reads */
             write,           /**< the programmer: value[] */
             pulses,          /**< the programmer: pulse_dus[] (KST) */
             leave;           /**< the programmer: LEAVE */
    uint16_t value[2];        /**< the two registers' values to write */
    uint16_t pulse_dus[2];    /**< PULSE LOWER and PULSE UPPER, 0.1us */
} servo_rauto_do_t;

/** @brief Where a run stands overall. */
typedef enum { SERVO_RAUTO_IDLE = 0, SERVO_RAUTO_RUNNING, SERVO_RAUTO_DONE } servo_rauto_state_t;
/** @brief The run's phases. */
typedef enum {
    SERVO_RAUTO_PH_NONE = 0, SERVO_RAUTO_PH_READ, SERVO_RAUTO_PH_MOVE,
    SERVO_RAUTO_PH_SETTLE, SERVO_RAUTO_PH_SAFE, SERVO_RAUTO_PH_CONNECT,
    SERVO_RAUTO_PH_WRITE, SERVO_RAUTO_PH_LEAVE, SERVO_RAUTO_PH_ARM,
    SERVO_RAUTO_PH_CHECK,
} servo_rauto_phase_t;
/** @brief How a run ended. */
typedef enum { SERVO_RAUTO_PASS = 0, SERVO_RAUTO_FAIL, SERVO_RAUTO_ABORTED } servo_rauto_verdict_t;
/** @brief Why a run failed or was aborted. */
typedef enum {
    SERVO_RAUTO_WHY_NONE = 0, SERVO_RAUTO_WHY_TEST, SERVO_RAUTO_WHY_HARD,
    SERVO_RAUTO_WHY_STALL, SERVO_RAUTO_WHY_BINDS, SERVO_RAUTO_WHY_NOT_STILL,
    SERVO_RAUTO_WHY_ANGLE, SERVO_RAUTO_WHY_NO_SERVO, SERVO_RAUTO_WHY_PROG,
    SERVO_RAUTO_WHY_WRITE, SERVO_RAUTO_WHY_MAPPING, SERVO_RAUTO_WHY_LIMIT,
    SERVO_RAUTO_WHY_WRITE_CAP, SERVO_RAUTO_WHY_BENCH, SERVO_RAUTO_WHY_SUPPLY,
    SERVO_RAUTO_WHY_COUNT,
} servo_rauto_why_t;

/** @brief One measure of one side. */
typedef struct {
    uint8_t  side;            /**< 0 at 1000us, 1 at 2000us */
    uint8_t  n;               /**< the write's number, 0 the first measure */
    uint16_t value;           /**< the register's travel value in force */
    float    measured_deg;    /**< along the side's direction */
    float    goal_deg;        /**< target x (1 + margin) */
    bool     within;          /**< within the tolerance of the goal */
} servo_rauto_step_t;

/** @brief A run's state.  The members after pend are the run's own. */
typedef struct {
    servo_rauto_cfg_t   cfg;          /**< as started */
    servo_rauto_state_t state;        /**< IDLE, RUNNING, DONE */
    servo_rauto_phase_t phase;        /**< the phase under way */
    servo_rauto_verdict_t verdict;    /**< once DONE */
    servo_rauto_why_t   why;          /**< once DONE */
    servo_test_abort_t  test_why;     /**< for SERVO_RAUTO_WHY_TEST */
    uint32_t start_ms,                /**< the start, ms */
             end_ms,                  /**< the end, ms */
             phase_ms;                /**< the phase's start, ms */
    servo_rauto_servo_t servo;        /**< as the read session read it */
    uint16_t cur[2];                  /**< what the servo holds now */
    int8_t   reg_side[2];             /**< the side each register moves, -1 */
    uint8_t  side;                    /**< the side measured or written */
    uint8_t  writes[2];               /**< writes by side */
    unsigned sessions;                /**< CONNECTs answered */
    float    goal_deg[2],             /**< target x (1 + margin), ° */
             measured_deg[2];         /**< each side's last measure, ° */
    float    bind_deg;                /**< the angle at a bind */
    servo_gap_t gap;                  /**< the angle source's gaps */
    /** @brief Every measure, in order. */
    servo_rauto_step_t steps[SERVO_RAUTO_STEPS_MAX];
    uint8_t  step_n;                  /**< steps recorded */
    bool     have_win;                /**< a window arrived */
    uint32_t last_win_ms;             /**< the last window's time */
    bool     elevated;                /**< window means above STALL_MA */
    uint32_t elevated_ms;             /**< since then */
    unsigned out_line;                /**< the outbox's next line */
    bool     out_open,                /**< OPEN handed out */
             out_end,                 /**< the last line handed out */
             out_done,                /**< END handed out */
             csv_open;                /**< the CSV handed out */
    char     scratch[SERVO_TEST_LINE_MAX];  /**< the line handed out */
    servo_rauto_do_t pend;            /**< asked, for the next step */

    bool     safe_asked;              /**< PH_SAFE: SAFE asked */
    bool     read_once;               /**< the read session read the servo */
    bool     pulse_op;                /**< the write under way is the pulses */
    bool     pulses_landed;           /**< 1000.0us and 2000.0us read back */
    bool     in_flight;               /**< a write or the pulse write handed
                                           out, its answer not in: an end now
                                           may leave it landed */
    bool     programmed;              /**< a value differs, or a write failed
                                           once sent */
    unsigned write_ops;               /**< writes sent, pulses included */
    unsigned total;                   /**< value writes that landed */
    uint16_t pulse_now[3];            /**< KST: NEUTRAL, LOWER, UPPER now */
    uint16_t want[2];                 /**< the write under way */
    uint16_t cmd_us;                  /**< the command last asked */
    bool     cmd_due;                 /**< the command still to hand out */
    uint32_t move_ms;                 /**< the measure's command */
    bool     angle_valid;             /**< the last step's angle */
    float    angle_deg;               /**< its deflection, ° */
    int8_t   sign[2];                 /**< each side's direction, 0 unknown */
    bool     verified[2];             /**< within at the last measure */
    unsigned verified_at[2];          /**< total at that measure */
    bool     map_open;                /**< the register is being found */
    uint8_t  map_reg;                 /**< the register tried */
    uint8_t  map_tries;               /**< registers tried */
    float    map_before;              /**< the side's measure before it */
    float    map_expect;              /**< the change the model gives */
} servo_rauto_t;

/**
 * @brief Clear a run state to idle.
 * @param r Run state; NULL does nothing.
 */
void servo_rauto_init(servo_rauto_t *r);
/**
 * @brief Start at @p now_ms.
 * @param r      Run state, cleared before the run starts.
 * @param cfg    What the run is asked.
 * @param now_ms Start time, ms.
 * @return false for a target outside 0.1 to 90°, a margin above the
 *          maximum, a tolerance outside its range, or RUNNING; also for
 *          NULL, a family not in servo_rauto_family_t, and a centre not
 *          strictly between 1000us and 2000us.
 */
bool servo_rauto_start(servo_rauto_t *r, const servo_rauto_cfg_t *cfg, uint32_t now_ms);
/**
 * @brief One INA3221 window: the current guard.
 * @param r Run state; nothing unless RUNNING.
 * @param w The window; NULL, or one without a current reading, is ignored.
 */
void servo_rauto_window(servo_rauto_t *r, const servo_test_win_t *w);
/**
 * @brief A reading of the angle source, for its gaps: one not valid opens a
 *        gap at its taken_ms, the next valid one closes it.
 * @param r Run state; nothing unless RUNNING.
 * @param a The reading; NULL is ignored.
 */
void servo_rauto_angle(servo_rauto_t *r, const servo_angle_in_t *a);
/**
 * @brief The programmer's CONNECT answered with the servo it read.
 * @param r      Run state; nothing outside READ and CONNECT.
 * @param sv     The servo; NULL is ignored.
 * @param now_ms Time, ms.
 */
void servo_rauto_connected(servo_rauto_t *r, const servo_rauto_servo_t *sv, uint32_t now_ms);
/**
 * @brief A value write ended.
 * @param r      Run state; nothing outside WRITE.
 * @param ok     false ends the run as ABORTED with WRITE.
 * @param value  The two values the servo holds; NULL as not ok.
 * @param now_ms Time, ms.
 */
void servo_rauto_written(servo_rauto_t *r, bool ok, const uint16_t *value, uint32_t now_ms);
/**
 * @brief The KST pulse write ended.
 * @param r         Run state; nothing outside the pulse write.
 * @param ok        false ends the run as ABORTED with WRITE.
 * @param pulse_dus NEUTRAL, PULSE LOWER and PULSE UPPER, 0.1us; NULL as not
 *                  ok.
 * @param now_ms    Time, ms.
 */
void servo_rauto_pulsed(servo_rauto_t *r, bool ok, const uint16_t *pulse_dus, uint32_t now_ms);
/**
 * @brief LEAVE ended: the port is back in PWM.
 * @param r      Run state; nothing outside LEAVE.
 * @param ok     false ends the run as ABORTED with PROG.
 * @param now_ms Time, ms.
 */
void servo_rauto_left(servo_rauto_t *r, bool ok, uint32_t now_ms);
/**
 * @brief The programmer refused the step asked: CONNECT and LEAVE end the
 *        run with PROG, a write with WRITE.
 * @param r      Run state.
 * @param now_ms Time, ms.
 */
void servo_rauto_refused(servo_rauto_t *r, uint32_t now_ms);
/**
 * @brief CONNECT ended with no servo answering: the read session's ends the
 *        run as FAIL with NO_SERVO, a later one as servo_rauto_refused().
 * @param r      Run state; nothing outside READ and CONNECT.
 * @param now_ms Time, ms.
 */
void servo_rauto_no_servo(servo_rauto_t *r, uint32_t now_ms);
/**
 * @brief The file the servo's values before the run were saved in.
 * @param r    Run state; nothing before the read.
 * @param name The file; NULL or "" is ignored.
 */
void servo_rauto_backup(servo_rauto_t *r, const char *name);
/**
 * @brief Advance to @p now_ms: what to do comes back in @p out.
 * @param r      Run state.
 * @param now_ms Time, ms.
 * @param in     What the caller knows; NULL skips the checks on it.
 * @param out    Cleared, then filled; NULL to skip.
 */
void servo_rauto_step(servo_rauto_t *r, uint32_t now_ms, const servo_rauto_in_t *in, servo_rauto_do_t *out);
/**
 * @brief The caller's reasons: the run ends as ABORTED with WHY_TEST.
 * @param r      Run state; nothing unless RUNNING.
 * @param why    The reason.
 * @param now_ms Time, ms.
 */
void servo_rauto_abort(servo_rauto_t *r, servo_test_abort_t why, uint32_t now_ms);
/**
 * @brief The supply changed under the run outside a session: it answers
 *        where it did not at the start, its output went on, or its set
 *        points moved.  The run ends as ABORTED with WHY_SUPPLY, the limit
 *        run's `supply or rail voltage changed`.
 * @param r      Run state; nothing unless RUNNING.
 * @param now_ms Time, ms.
 */
void servo_rauto_supply_changed(servo_rauto_t *r, uint32_t now_ms);
/**
 * @brief Whether the run is RUNNING.
 * @param r Run state; NULL gives false.
 * @return true while RUNNING.
 */
bool servo_rauto_running(const servo_rauto_t *r);
/**
 * @brief The run asked SAFE or ARMED and waits for it, or holds the
 *        programmer: a disarm then is its own.
 * @param r Run state; NULL gives false.
 * @return true in SAFE after SAFE was asked, READ, CONNECT, WRITE, LEAVE
 *         and ARM.
 */
bool servo_rauto_switching(const servo_rauto_t *r);
/**
 * @brief The run holds the programmer.
 * @param r Run state; NULL gives false.
 * @return true in READ, CONNECT, WRITE and LEAVE.
 */
bool servo_rauto_programming(const servo_rauto_t *r);
/**
 * @brief A value was written.
 * @param r Run state; NULL gives false.
 * @return true once a write landed or failed once sent.
 */
bool servo_rauto_wrote(const servo_rauto_t *r);
/**
 * @brief RESTORE is offered: the run ended as FAIL or ABORTED with the
 *        servo's values changed, or with a write handed out and its
 *        answer not in (it may have landed).
 * @param r Run state; NULL gives false.
 * @return true when DONE, programmed, and not PASS.
 */
bool servo_rauto_restore(const servo_rauto_t *r);
/**
 * @brief The outbox: OPEN, the CSV's lines, the report's lines (TXT), END.
 * @param r    Run state; NULL gives NONE.
 * @param text Out: the entry's text; "" for none.
 * @return the next entry; NONE before DONE or once handed out.
 */
servo_test_out_t servo_rauto_peek(servo_rauto_t *r, const char **text);
/**
 * @brief Take the entry servo_rauto_peek() returned.
 * @param r Run state.
 */
void servo_rauto_pop(servo_rauto_t *r);
/**
 * @brief Nothing left to hand over.
 * @param r Run state.
 * @return true for NULL, IDLE, or DONE with the outbox handed out.
 */
bool servo_rauto_drained(const servo_rauto_t *r);
/**
 * @brief Line @p idx of the report.
 * @param r   Run state.
 * @param idx Line, from 0.
 * @param buf Out: the line.
 * @param n   Size of @p buf, bytes.
 * @return false past the last line, for NULL, n of 0, or a run not DONE.
 */
bool servo_rauto_report_line(const servo_rauto_t *r, unsigned idx, char *buf, size_t n);
/**
 * @brief value x goal / measured, rounded, within lo..hi; lo for a
 *        measured of 0 or less.
 * @param value        The value in force.
 * @param goal_deg     The goal, °.
 * @param measured_deg The deflection measured with @p value, °.
 * @param lo           The smallest value the hard rules take.
 * @param hi           The largest.
 * @return the value for the goal.
 */
uint16_t servo_rauto_scaled(uint16_t value, float goal_deg, float measured_deg, uint16_t lo, uint16_t hi);
/**
 * @brief Whether a run that ended for @p why ended on an error of the bench:
 *        a current guard, the meter, or the angle source.
 * @param why      The run's reason.
 * @param test_why For SERVO_RAUTO_WHY_TEST, the caller's.
 * @return true for HARD, STALL, BINDS, ANGLE and a WHY_TEST that
 *         servo_test_abort_error() calls an error.
 */
bool servo_rauto_why_error(servo_rauto_why_t why, servo_test_abort_t test_why);
/**
 * @brief The table entry the screen and the report name the reason by.
 * @param r Run state; NULL gives the NONE entry.
 * @return the entry.
 */
servo_str_t servo_rauto_why_str(const servo_rauto_t *r);
/**
 * @brief The table entry of a phase.
 * @param ph The phase.
 * @return the entry; PHASE_NONE for one not in the enum.
 */
servo_str_t servo_rauto_phase_str(servo_rauto_phase_t ph);

#ifdef __cplusplus
}
#endif
