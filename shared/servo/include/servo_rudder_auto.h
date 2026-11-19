/**
 * @file
 * @brief AUTO end points: a run that drives a servo to 1000us and 2000us,
 *        measures the surface's deflection on the angle source, and
 *        programs the servo's end points so that each side reaches its
 *        target there.  On KST the margin lies in the pulse length: each
 *        end of the servo's pulse range lies past 1000us and 2000us by the
 *        margin's % of that side's span from the servo's NEUTRAL, so a
 *        radio's trim past 2000us still moves the surface.  Pure: fed the
 *        angle, the meter's windows, the programmer's answers and the
 *        clock, it asks the caller to move, write and switch, as
 *        servo_limrun does.
 *
 * Steps: the servo's read (CONNECT, LEAVE); the pulse range (KST: PULSE
 * LOWER and PULSE UPPER set to the margin's range,
 * servo_rauto_kst_pulses(), 1000.0us to 2000.0us at margin 0, a NEUTRAL
 * outside SERVO_RAUTO_NEUTRAL_MIN_DUS..SERVO_RAUTO_NEUTRAL_MAX_DUS refused
 * at the read (SERVO_RAUTO_WHY_NEUTRAL); Hitec:
 * nothing and margin 0, the D series takes 1000us as 76.9% of the way to
 * 0xB2, hitec_live.h); the limit search (phase SEARCH, below), which
 * takes the first measure of each side on its way out; then
 * per side, at most SERVO_RAUTO_WRITES_MAX times: move to the end pulse,
 * wait for still (SERVO_RAUTO_STILL_MS within IMU_STILL_DEG on the axis
 * read, on the IMU the settle rule of servo_settle.h too, at most
 * servo_rauto_wait_ms()), measure, done when |measured - goal| <= tol, else
 * value = value x goal / measured within lo..hi (a linear servo between
 * neutral and the end), write in a session, move again.  Which register
 * moves which side is known from the family and its direction (Hitec D
 * series set to CW), else found on the first write.  Two writes in a row
 * that do not move their side end the run, as does a value more than 50%
 * from the model's first estimate.  The current guard of the
 * limit run holds
 * throughout (SERVO_LIMRUN_HARD_MA, SERVO_LIMRUN_STALL_MA for
 * SERVO_LIMRUN_STALL_MS): a stall before the goal ends the run with
 * SURFACE BINDS AT x°; the angle lost longer than SERVO_GAP_MAX_MS ends it
 * (servo_gap.h); every end switches the output off and asks SAFE.
 * Report RUDDRnnn.TXT; CSV RUDDRnnn.CSV one row a step and one a verify
 * measure, the CSV handed out first, as the automatic test's.
 *
 * In detail:
 *
 *   - The read session: the servo held at the bench's PULSE CENTER for
 *     SERVO_LIMRUN_RETURN_MS, SAFE asked, CONNECT with its read (phase
 *     READ), for KST the pulse write when PULSE LOWER and PULSE UPPER are
 *     not the margin's (servo_rauto_kst_pulses()), LEAVE, ARMED asked.
 *     Once the servo holds the margin's pulses, read so or read back, the
 *     bench's PULSE MIN and PULSE MAX are due to widen to the margin's
 *     range where they are narrower (servo_rauto_widen()); only the
 *     search drives the extended ends.  A read that is no
 *     servo, or whose values lie outside its own lo..hi, ends the run as
 *     FAIL (SERVO_RAUTO_WHY_NO_SERVO) with LEAVE asked; a KST read whose
 *     NEUTRAL lies outside 1360.0us to 1640.0us as FAIL
 *     (SERVO_RAUTO_WHY_NEUTRAL).  The pulse write's read back holds the
 *     read's NEUTRAL and the margin's pulses, else the run ends as
 *     ABORTED (SERVO_RAUTO_WHY_WRITE).
 *   - The limit search (phase SEARCH, servo_rudder_auto_search.c): after
 *     the read session, before anything else is driven, LIMIT RUN's search
 *     (servo_limit.h: 10us steps out from PULSE CENTER, each 120ms settled
 *     and 80ms measured, bound at 1.8 x the free current and 0.15A over
 *     it, the end the last free pulse backed off 25us) on the 1000us side,
 *     then on the 2000us side, each out to the margin's range from the
 *     read's NEUTRAL (servo_rauto_pulse_range(); 1000us and 2000us on
 *     Hitec) and from PULSE CENTER held for
 *     SERVO_LIMRUN_RETURN_MS.  It programs nothing.  Every position judged
 *     is a row of the report and the CSV (pulse, current, angle; at most
 *     SERVO_RAUTO_SEARCH_MAX).  It is the first measure: once the
 *     finder judged the end pulse, or a position past it, free on its way,
 *     the servo is held at the end pulse (1000us or 2000us), and the angle is
 *     taken there by the measure's rule below, counted from the end
 *     pulse's command; then the search goes on from where it stood
 *     (servo_limit_resume()).  The found end of a side (LOW END, HIGH END:
 *     the end found, or the margin's end where the side is free) caps
 *     every pulse driven after the search.  An end inside the end pulse,
 *     LOW END above 1000us or HIGH END below 2000us, is held for its
 *     angle by the measure's rule (the last free step's where it does not
 *     settle): short of the goal by more than the tolerance it ends the
 *     run as FAIL (SERVO_RAUTO_WHY_STOP, STOP AT 1225us BEFORE TARGET)
 *     with no value written, a 1000us side that fails leaving the 2000us
 *     side not searched; else every measure of that side drives the end
 *     (servo_rauto_side_us()), that angle its first, each angle scaled to
 *     the end pulse by the linear map from PULSE CENTER.  A finder's
 *     overcurrent or stall ends the run as HARD or STALL.  With both
 *     sides searched the two first measures are
 *     judged as a measure is, the 1000us side first, and the run goes on
 *     with the first side outside the tolerance, or with the verify pass
 *     or PASS where both lie within.
 *   - A measure: the end pulse commanded (phase MOVE), the surface judged
 *     from SERVO_RAUTO_STILL_MS after the command on (phase SETTLE): the
 *     first step with a valid angle still for SERVO_RAUTO_STILL_MS takes
 *     the angle.  On the IMU (cfg.settle_win_ms above 0) the angle must
 *     also have stayed within cfg.settle_band_deg (IMU PRECISION) over the
 *     trailing cfg.settle_win_ms (IMU SETTLING TIME), counted from the
 *     command (servo_settle.h): the sensor's own creep after a step.
 *     None by servo_rauto_wait_ms() after the command ends the run as FAIL
 *     (SERVO_RAUTO_WHY_NOT_STILL): SERVO_RAUTO_MOVE_MS plus the caller's
 *     settle_ms, and on the IMU SERVO_RAUTO_CREEP_MS plus the settling
 *     time.  settle_ms is the time the IMU's filter can take to settle
 *     after the surface stopped (imu_filter_settle_ms()); the IMU's still
 *     time reads 0 until it has, so the angle a measure takes is a
 *     settled one.  A side measured again after a write to the other
 *     register is a CHECK.
 *   - The approach (phase APPROACH): every measure, of the main pass and
 *     of the verify pass, first drives the servo to the pulse it is to
 *     come from and waits there by the measure's own rule, then commands
 *     its pulse: an end comes from PULSE CENTER, the verify pass's center
 *     from 2000us, so a pulse is always reached from the same side (the
 *     surface and the IMU read up to 0.33° apart by the side, bench of
 *     2026-11-18).  A measure that comes from where the last one took its
 *     angle, the verify pass's center after its 2000us, needs no approach.
 *     The approach takes no angle and ends the run as a measure does when
 *     it does not settle (SERVO_RAUTO_WHY_NOT_STILL).
 *   - The verify pass (phase VERIFY): a run that changed the servo's
 *     values or pulses (programmed) and has both sides within measures
 *     1000us, 2000us and PULSE CENTER once more, each as a measure, and
 *     passes when the two ends lie within the tolerance of their goals and
 *     the center within the tolerance of the zero.  Otherwise it ends as
 *     FAIL (SERVO_RAUTO_WHY_VERIFY) after all three, naming the first one
 *     outside.  A run that wrote nothing does not verify: its two measures
 *     were taken after the last change.
 *   - A side's direction is the sign of its first measure, which must lie
 *     at least the tolerance from the zero unless it is within the
 *     tolerance of the goal; the two sides of opposite signs.  A side's
 *     goal is its target, which is more than 2 x the tolerance
 *     (servo_rauto_goals_ok()), so a measure
 *     within the tolerance of it gives a direction too.  Otherwise,
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
 *   - The register of each side.  Known from the read session where the
 *     family says it: a Hitec D series servo set to CW has LEFT END (0xB2,
 *     the first register) at 1000us and RIGHT END (0xB0) at 2000us; the
 *     D series target line puts 1000us between target 400 (0xB2) and 3000
 *     (0xC2), hitec_live.h, and a D485HW measured so.  Every other family
 *     and direction (KST, Hitec classic, a D series servo set to CCW) is
 *     not known: the first write of the side measured first outside the
 *     tolerance finds it.  That write goes to the register of the side's
 *     name with REVERSION (Hitec: CCW) off, the other one with it on, or
 *     to the other register where that one cannot move toward the goal,
 *     and the side is measured again.  The side moved when it lands
 *     within the tolerance or travels at least half the change the model
 *     gives, toward it; it moved the wrong way when it travels that far
 *     the other way, which ends the run as FAIL (SERVO_RAUTO_WHY_MAPPING);
 *     otherwise it stayed: the register tried goes back to its value and
 *     the side's value, worked out from the other register, goes to that
 *     one, and the side is measured again.  Stayed a second time, neither
 *     register moved it: the run ends as FAIL
 *     (SERVO_RAUTO_WHY_NO_EFFECT).  The register found moves that side,
 *     the other one the other side.  A side within the tolerance at its
 *     first measure gets no write, so it finds nothing: the other side's
 *     first write does.
 *   - Each write is held to its effect.  For the writes of a side since
 *     the last one that moved it, E is the sum of the changes the model
 *     gives, O the measured change since the measure before the first of
 *     them.  They moved the side when it lands within the tolerance, or
 *     |E| >= tol and O lies along E by at least min(|E| / 2, tol).  Not
 *     moved after SERVO_RAUTO_NO_EFFECT_WRITES of them with |E| >= tol,
 *     the run ends as FAIL (SERVO_RAUTO_WHY_NO_EFFECT, WRITE DID NOT MOVE
 *     THE 1000us or 2000us SIDE); that covers a measure that moves the
 *     wrong way.  Writes with |E| under the tolerance are not judged: the
 *     angle source's noise is of that size.  The register-finding write is
 *     judged as above and starts the count anew once found.
 *   - A bound on each write: a register's new value lies within
 *     SERVO_RAUTO_CAP_PCT (50%) of the model's first estimate for it,
 *     value0 x goal / measured0, value0 the register's value at the read
 *     and measured0 the side's first measure (servo_rauto_in_cap()).  A
 *     value past it, the register-finding writes included, ends the run as
 *     FAIL (SERVO_RAUTO_WHY_CAP) before the write.
 *   - A write session: the servo held at PULSE CENTER for
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
 * abort (servo_rauto_restore()), offered until a later read of the
 * programmer shows them back or otherwise changed (servo_rauto_servo_seen()).
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
#include "servo_limit.h"
#include "servo_limrun.h"
#include "servo_settle.h"
#include "servo_test.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Writes a side takes at most. */
#define SERVO_RAUTO_WRITES_MAX  8u
/** @brief How far a write may move a register from the model's first
 *         estimate for it, value0 x goal / measured0 (servo_rauto_in_cap()),
 *         in % of that estimate. */
#define SERVO_RAUTO_CAP_PCT 50u
/** @brief Writes in a row whose model change sums to at least the tolerance
 *         and that do not move their side end the run: one alone can be the
 *         angle source's noise. */
#define SERVO_RAUTO_NO_EFFECT_WRITES 2u
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
/**
 * @brief On the IMU, the time a measure gives the sensor's own creep after
 *        a step on top of SERVO_RAUTO_MOVE_MS and IMU SETTLING TIME, ms.
 *        The BWT901CL in 6-AXIS came within 0.1° of its final value 7.4s
 *        after a servo step (2026-11-17), and a slower tail of about 3.5s
 *        showed on 2026-11-18: on that tail the default rule (3.0s within
 *        0.10°) takes the angle 10.4s after the surface arrives.  10000ms
 *        with the 3000ms and the window's 3000ms leave 16000ms, 5.6s past
 *        that.
 */
#define SERVO_RAUTO_CREEP_MS    10000u
/** @brief The search's rows: positions judged on both sides.  The margin's
 *         range spans at most 1500us, 150 steps of 10us, and each side's
 *         last step can be short. */
#define SERVO_RAUTO_SEARCH_MAX  160u
/** @brief The verify pass's measures: 1000us, 2000us and PULSE CENTER. */
#define SERVO_RAUTO_VERIFY_N    3u
/** @brief The largest margin, %. */
#define SERVO_RAUTO_MARGIN_MAX  50u      /**< % */
/** @brief The NEUTRAL a KST servo may hold, 0.1us: 1360.0us to 1640.0us,
 *         KST_NEUTRAL_RAW_MIN and KST_NEUTRAL_RAW_MAX (kst_limits.h).
 *         Within it the margin's pulses at 50% lie in 680.0us to 2320.0us,
 *         inside PULSE LOWER's and PULSE UPPER's 500us to 2500us
 *         (KST_PULSE_RAW_MIN, KST_PULSE_RAW_MAX). */
#define SERVO_RAUTO_NEUTRAL_MIN_DUS 13600u   /**< 1360.0us */
#define SERVO_RAUTO_NEUTRAL_MAX_DUS 16400u   /**< 1640.0us */
/** @brief The NEUTRAL the margin's range assumes before a read, 0.1us. */
#define SERVO_RAUTO_NEUTRAL_DUS     15000u
/** @brief The KST's pulse step: PULSE LOWER and PULSE UPPER hold whole
 *         0.8us (kst_reg.h, CONV_PULSE), 0.1us. */
#define SERVO_RAUTO_KST_STEP_DUS 8u
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
    uint8_t  margin_pct;      /**< 0 to SERVO_RAUTO_MARGIN_MAX on KST, 0 on
                                   Hitec: the pulse range's extension */
    uint16_t tol_ddeg;        /**< 0.1° */
    uint8_t  family;          /**< servo_rauto_family_t */
    uint16_t center_us;       /**< the bench's PULSE CENTER */
    uint16_t min_us,          /**< the bench's PULSE MIN, at most 1000us */
             max_us;          /**< its PULSE MAX, at least 2000us */
    float    len_mm;          /**< 0 for none: the report in ° only */
    uint16_t settle_win_ms;   /**< IMU SETTLING TIME, ms, at most
                                   SERVO_SETTLE_WIN_MAX_MS; 0 no window:
                                   the AS5600 */
    float    settle_band_deg; /**< IMU PRECISION, °, above 0 with a
                                   window */
    /* For the report only. */
    char     dut[24];         /**< DUT NAME */
    char     type[20];        /**< the servo type's name */
    char     output[28];      /**< what it drives: "PROG PORT 1 GPIO 13" */
    char     meter[20];       /**< the INA3221 channel; "" names it INA3221 */
    char     source[12];      /**< servo_angle_name() */
    char     firmware[16];    /**< the bench's version */
    const char *const *text;  /**< SERVO_STR_COUNT entries, NULL English */
} servo_rauto_cfg_t;

/**
 * @brief The servo as read, in travel units (see IMPL.md 3.5): KST's and a
 *        classic Hitec servo's raw register values; a Hitec D series
 *        servo's distance of each end point from NEUTRAL, in position
 *        counts.  The report and the screen show registers as the
 *        programming pages do (servo_rauto_reg_of()).
 */
typedef struct {
    bool     ok;              /**< read, a servo of the family, editable */
    uint16_t value[2];        /**< the two registers' travel values */
    uint16_t lo,              /**< the smallest value the hard rules take */
             hi;              /**< the largest */
    bool     reversion;       /**< REVERSION on */
    uint16_t pulse_dus[3];    /**< KST: NEUTRAL, LOWER, UPPER; else 0 */
    uint16_t mid;             /**< Hitec D series: NEUTRAL (0xC2) in
                                   position counts, the travel's origin;
                                   else 0 */
    char     backup[16];      /**< the backup file; "" for memory only */
} servo_rauto_servo_t;

/** @brief What the caller knows each step. */
typedef struct {
    bool  live,               /**< the bench is ARMED and the value live */
          safe;               /**< the bench is SAFE, no ARM on its way */
    bool  angle_valid;        /**< the angle source has an angle */
    float angle_deg;          /**< deflection from the zero */
    uint16_t still_ms;        /**< the angle still this long, ms; from the
                                   IMU, the still time of the axis read */
    uint32_t settle_ms;       /**< the angle source's filter may take this
                                   much longer to settle, ms; 0 none */
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
    SERVO_RAUTO_PH_CHECK, SERVO_RAUTO_PH_VERIFY, SERVO_RAUTO_PH_APPROACH,
    SERVO_RAUTO_PH_SEARCH,
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
    SERVO_RAUTO_WHY_NO_EFFECT, SERVO_RAUTO_WHY_CAP, SERVO_RAUTO_WHY_VERIFY,
    SERVO_RAUTO_WHY_STOP,
    SERVO_RAUTO_WHY_NEUTRAL,
    SERVO_RAUTO_WHY_COUNT,
} servo_rauto_why_t;

/** @brief One measure of one side. */
typedef struct {
    uint8_t  side;            /**< 0 at 1000us, 1 at 2000us */
    uint8_t  n;               /**< the write's number, 0 the first measure */
    uint16_t value;           /**< the register's travel value in force */
    uint8_t  reg;             /**< the register value belongs to; 0xFF
                                   none */
    float    measured_deg;    /**< along the side's direction */
    float    goal_deg;        /**< the side's target */
    bool     within;          /**< within the tolerance of the goal */
} servo_rauto_step_t;

/** @brief One position the limit search judged. */
typedef struct {
    uint16_t us;              /**< the pulse, us */
    uint16_t ma;              /**< the mean current over its 80ms, mA */
    float    deg;             /**< the angle when judged, signed, ° */
    bool     valid;           /**< deg holds an angle */
    uint8_t  side;            /**< 0 the 1000us side, 1 the 2000us side */
} servo_rauto_srow_t;

/** @brief A side's result of the limit search. */
typedef struct {
    bool     done;            /**< searched */
    bool     bound;           /**< a stop found; else free to the range */
    uint16_t end_us;          /**< LOW END or HIGH END, us: the cap */
    uint16_t bind_us;         /**< the pulse judged bound, us */
    float    free_a,          /**< the free current, A */
             bound_a;         /**< the current judged bound, A */
    bool     deg_taken;       /**< an end inside the end pulse: its angle
                                   taken */
    float    deg;             /**< that angle, signed, ° */
    bool     fallback;        /**< the end did not settle: deg is the last
                                   free step's */
    uint16_t deg_us;          /**< where deg was taken, us */
    bool     at_end;          /**< the side is measured at end_us, its
                                   angles scaled to the end pulse */
} servo_rauto_end_t;

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
    float    goal_deg[2],             /**< each side's target, ° */
             measured_deg[2];         /**< each side's last measure, ° */
    float    bind_deg;                /**< the angle at a bind */
    uint32_t settle_ms;               /**< the largest settle_ms a step had */
    servo_gap_t gap;                  /**< the angle source's gaps */
    servo_settle_t hold;              /**< the measure's readings, for the
                                           settle rule on the IMU */
    servo_rauto_phase_t next_ph;      /**< APPROACH: the measure's phase
                                           once the servo settled there */
    uint16_t next_us;                 /**< APPROACH: the measure's pulse */
    bool     held_ok;                 /**< the servo stands where the last
                                           measure took a settled angle */
    uint16_t held_us;                 /**< that pulse, us */
    uint8_t  vstep;                   /**< VERIFY: the measure under way,
                                           0 1000us, 1 2000us, 2 center */
    uint8_t  vn;                      /**< verify measures taken */
    uint8_t  vbad;                    /**< the first one outside */
    float    vdeg[SERVO_RAUTO_VERIFY_N];  /**< each: the ends along their
                                               direction, the center
                                               signed, ° */
    bool     vwithin[SERVO_RAUTO_VERIFY_N]; /**< each within the
                                                 tolerance */
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
    bool     pulses_landed;           /**< the margin's pulses read back */
    bool     pulses_held;             /**< KST: the servo holds the margin's
                                           pulses, read so or read back */
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
    bool     eff_due;                 /**< the next measure of side judges
                                           the writes not yet seen to move
                                           it */
    uint8_t  eff_writes[2];           /**< those writes, by side */
    float    eff_from[2],             /**< the measure before the first, ° */
             eff_expect[2];           /**< the change the model gives for
                                           them, summed, ° */
    bool     left_unknown;            /**< a write's answer missing at the
                                           end: what the servo holds is not
                                           known */
    bool     seen_base;               /**< base_value and base_pulse hold
                                           the programmer's servo as first
                                           seen after the end */
    uint16_t base_value[2],           /**< its two values */
             base_pulse[3];           /**< its pulses (KST) */
    bool     have_first[2];           /**< first_deg holds the side's first
                                           measure */
    float    first_deg[2];            /**< each side's first measure along
                                           its direction, ° */
    bool     restore_off;             /**< RESTORE no longer offered: a
                                           later read holds the values
                                           before the run, or values the
                                           run did not leave */
    /* The limit search (servo_rudder_auto_search.c). */
    servo_limit_t lf;                 /**< the side's finder */
    servo_rauto_end_t ends[2];        /**< LOW END and HIGH END */
    bool     searched;                /**< both sides searched */
    bool     s_hold;                  /**< held at the end pulse for the
                                           first measure */
    bool     s_endhold;               /**< the hold is at a found end
                                           inside the end pulse */
    uint16_t s_next_us;               /**< the finder's command after it */
    uint32_t s_from_ms;               /**< the side's start, ms */
    bool     s_took[2];               /**< the side's first measure taken */
    float    s_deg[2];                /**< its angle, signed, ° */
    uint16_t s_steps,                 /**< positions commanded, both sides */
             s_plan;                  /**< positions planned, both sides */
    uint16_t stop_us;                 /**< SERVO_RAUTO_WHY_STOP: the end */
    servo_rauto_srow_t srow[SERVO_RAUTO_SEARCH_MAX]; /**< positions judged */
    uint16_t srow_n;                  /**< rows recorded */
} servo_rauto_t;

/**
 * @brief Clear a run state to idle.
 * @param r Run state; NULL does nothing.
 */
void servo_rauto_init(servo_rauto_t *r);
/**
 * @brief Whether both targets, the goals at 1000us and 2000us, are large
 *        enough for the tolerance: each more than 2 x tolerance, by 0.001°
 *        or more.  The margin does not count: it lies in the pulse length.
 *
 * A measure within the tolerance of such a goal lies at least the
 * tolerance from the zero, so it gives its side a direction.  Under it a
 * surface that stays at the zero is within a goal of the tolerance or
 * less, and one side can be within without a direction, which lets two
 * end pulses on one side of the zero pass.
 *
 * @param target_deg The two targets, 1000us and 2000us, °.
 * @param tol_ddeg   The tolerance, 0.1°.
 * @return true when both targets are large enough; false for NULL.
 */
bool servo_rauto_goals_ok(const float target_deg[2], uint16_t tol_ddeg);
/**
 * @brief The margin's pulse range in whole us: servo_rauto_kst_pulses()'
 *        PULSE LOWER floored and PULSE UPPER ceiled, so the range holds
 *        the servo's, by less than 1us an end.  By the linear map from N
 *        each end reaches target x (1 + margin) within that.  At N
 *        1500.0us 950 to 2050us at 10%; at N 1640.0us and 50% 680 to
 *        2180us.
 * @param margin_pct  The margin, %; above SERVO_RAUTO_MARGIN_MAX as that.
 * @param neutral_dus The servo's NEUTRAL, 0.1us; 0 as
 *                    SERVO_RAUTO_NEUTRAL_DUS (before a read); outside
 *                    SERVO_RAUTO_NEUTRAL_MIN_DUS..SERVO_RAUTO_NEUTRAL_MAX_DUS
 *                    as the nearer bound.
 * @param us          Out: the lower and the upper end, us; NULL does
 *                    nothing.
 */
void servo_rauto_pulse_range(uint8_t margin_pct, uint16_t neutral_dus,
                             uint16_t us[2]);
/**
 * @brief PULSE LOWER and PULSE UPPER the run writes to a KST servo: the
 *        margin's range from the servo's NEUTRAL N, each side extended by
 *        the margin's % of its own span from N, lower 1000us - (N -
 *        1000us) x margin, upper 2000us + (2000us - N) x margin, on the
 *        KST's 0.8us step, each end rounded toward the center.  At N
 *        1500.0us and 10% 950.4us and 2049.6us; at N 1640.0us and 50%
 *        680.0us and 2180.0us; at 0% 1000.0us and 2000.0us at any N.
 * @param margin_pct  The margin, %; above SERVO_RAUTO_MARGIN_MAX as that.
 * @param neutral_dus The servo's NEUTRAL, 0.1us; 0 as
 *                    SERVO_RAUTO_NEUTRAL_DUS (before a read); outside
 *                    SERVO_RAUTO_NEUTRAL_MIN_DUS..SERVO_RAUTO_NEUTRAL_MAX_DUS
 *                    as the nearer bound.
 * @param dus         Out: PULSE LOWER and PULSE UPPER, 0.1us; NULL does
 *                    nothing.
 */
void servo_rauto_kst_pulses(uint8_t margin_pct, uint16_t neutral_dus,
                            uint16_t dus[2]);
/**
 * @brief Whether the bench's PULSE MIN and PULSE MAX are to widen: the run
 *        left a KST servo holding the margin's pulses, read so or read
 *        back, and the bench's range at the start (cfg.min_us, cfg.max_us)
 *        is narrower than the margin's range on either side.  Never
 *        narrower: a side already wider keeps its value.
 * @param r       Run state.
 * @param prof_us Out: PULSE MIN and PULSE MAX widened, us; NULL to skip.
 * @return true when either changes; false for NULL.
 */
bool servo_rauto_widen(const servo_rauto_t *r, uint16_t prof_us[2]);
/**
 * @brief Start at @p now_ms.
 * @param r      Run state, cleared before the run starts.
 * @param cfg    What the run is asked.
 * @param now_ms Start time, ms.
 * @return false for a target outside 0.1 to 90°, a margin above the
 *          maximum or above 0 on a Hitec family, a tolerance outside its
 *          range, a target too small for the tolerance
 *          (servo_rauto_goals_ok()), PULSE MIN above 1000us or PULSE MAX
 *          below 2000us, a settling window above
 *          SERVO_SETTLE_WIN_MAX_MS or one without a band above 0, or
 *          RUNNING; also for
 *          NULL, a family not in servo_rauto_family_t, and a center not
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
 * @brief The longest a measure waits for its angle after the command:
 *        SERVO_RAUTO_MOVE_MS plus the largest settle_ms a step had, and
 *        with a settling window SERVO_RAUTO_CREEP_MS plus the window.
 * @param r Run state; NULL gives SERVO_RAUTO_MOVE_MS.
 * @return The wait, ms.
 */
uint32_t servo_rauto_wait_ms(const servo_rauto_t *r);
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
 *        answer not in (it may have landed), and no read since shows the
 *        values put back or changed otherwise (servo_rauto_servo_seen()).
 * @param r Run state; NULL gives false.
 * @return true when DONE, programmed, not PASS, and not cleared.
 */
bool servo_rauto_restore(const servo_rauto_t *r);
/**
 * @brief The programmer's servo after the run, handed in each frame while
 *        RESTORE is offered.  The first one handed in is the base: what
 *        the programmer held at the end.  One that differs from the base
 *        is a later read or write, and becomes the base.  It clears
 *        RESTORE when its values (KST: and pulses) are the ones before the
 *        run, or when they are not the ones the run left; the latter not
 *        after an end with a write's answer missing, whose values are not
 *        known.
 * @param r  Run state; nothing unless servo_rauto_restore().
 * @param sv The servo; NULL or one not ok is ignored.
 */
void servo_rauto_servo_seen(servo_rauto_t *r, const servo_rauto_servo_t *sv);
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
 * @brief A register's travel value as the programming page shows it: on a
 *        Hitec D series servo the end point's position, NEUTRAL - travel
 *        for LEFT END (register 0) and NEUTRAL + travel for RIGHT END, in
 *        position counts with 8192 the center (hitec_run_to_reg() writes
 *        the same); on KST and a classic Hitec servo the raw value itself.
 * @param r      Run state; NULL gives @p travel.
 * @param g      The register: 0 LEFT, 1 RIGHT.
 * @param travel Its travel value.
 * @return the value shown.
 */
uint16_t servo_rauto_reg_of(const servo_rauto_t *r, int g, uint16_t travel);
/**
 * @brief The unit of servo_rauto_reg_of()'s values.
 * @param r Run state; NULL gives the raw one.
 * @return SERVO_STR_RA_R_COUNTS on a Hitec D series servo, else
 *         SERVO_STR_RA_R_RAW.
 */
servo_str_t servo_rauto_unit(const servo_rauto_t *r);
/**
 * @brief Whether a register's new value lies within SERVO_RAUTO_CAP_PCT of
 *        the model's first estimate for it, value0 x goal / measured0.
 * @param value0    The register's value at the read.
 * @param goal_deg  The side's goal, °.
 * @param first_deg The side's first measure along its direction, °.
 * @param next      The new value.
 * @return true within; false past it, or for a first measure of 0 or less.
 */
bool servo_rauto_in_cap(uint16_t value0, float goal_deg, float first_deg,
                        uint16_t next);
/**
 * @brief Whether a run that ended for @p why ended on an error of the bench:
 *        a current guard, the meter, or the angle source.
 * @param why      The run's reason.
 * @param test_why For SERVO_RAUTO_WHY_TEST, the caller's.
 * @return true for HARD, STALL, BINDS, STOP, ANGLE and a WHY_TEST that
 *         servo_test_abort_error() calls an error.
 */
bool servo_rauto_why_error(servo_rauto_why_t why, servo_test_abort_t test_why);
/**
 * @brief The pulse a side's measures drive: 1000us or 2000us, or the found
 *        end where the search found it inside them and the surface reaches
 *        TARGET there (servo_rudder_auto_search.c).
 * @param r Run state; NULL gives the end pulse.
 * @param s The side: 0 the 1000us side, 1 the 2000us side.
 * @return the pulse, us.
 */
uint16_t servo_rauto_side_us(const servo_rauto_t *r, uint8_t s);
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
