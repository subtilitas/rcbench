/**
 * @file
 * @brief Hitec servo programming protocols: the arithmetic and the judges
 *        of a horn moved inside a programming session.
 *
 * Implements the live end-point loop's core for the Hitec D series.  Hitec
 * is a name or trademark of its owner; it is used here only to say which
 * servos this module works with (README.md, "Legal notice and disclaimer").
 *
 * A D series servo in programming mode takes a target in register 0x1E,
 * 0 to 6000, and reports its position in register 0x0C, 0 to 16383
 * (hitec_session_move()).  This header holds what lies between the two:
 *
 *   - the mapping from target to position through the servo's stored
 *     neutral (0xC2) and end points (0xB2, 0xB0), and back;
 *   - the reach: the positions targets 0 and 6000 give;
 *   - the commanded position of one pass: clamped to the reach, slew
 *     limited, and its target;
 *   - the judge of one pass: free, following, or bound by its lag, by the
 *     supply current or by the AS5600 angle;
 *   - FIND STOP: the search for a mechanical stop from the neutral outward,
 *     which fills an end point and writes nothing.
 *
 * Nothing here touches the wire or reads a clock: the caller passes the
 * reading of a free-running 32-bit millisecond clock, and every time is an
 * unsigned difference of two readings, so the wrap at 2^32ms does not
 * shorten or lengthen one.  No heap, no floating point, no global or
 * static mutable state.
 *
 * Measured and not measured (README.md, "Live loop"):
 *
 *   - Targets 400, 3000 and 5600 give the stored left end, neutral and
 *     right end (the D series manual); through the IOmcu targets 2700,
 *     3300 and 3000 landed within 13 counts of the line on a D485HW.
 *     Whether the servo follows the line below 400 and above 5600 or stops
 *     at the end points is not measured: the reach assumes the line goes
 *     on.
 *   - The pulse width of a target, t = (us - 750) x 4
 *     (HITEC_LIVE_D_US_ZERO, HITEC_LIVE_D_T_PER_US), is not measured.
 *   - The mapping of a servo set to CCW is not measured;
 *     hitec_live_d_ends() takes CW only.
 *   - Every threshold of the judge is chosen, not measured on a blocked
 *     horn.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HITEC_LIVE_H
#define HITEC_LIVE_H

#include <stdbool.h>
#include <stdint.h>

#include "hitec_reg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- the D series target ------------------------------------------------- */

/** @brief The target that gives the stored neutral, 0xC2. */
#define HITEC_LIVE_D_T_MID 3000u
/** @brief Targets from the neutral to an end point: 400 gives 0xB2, 5600
 *         gives 0xB0. */
#define HITEC_LIVE_D_T_HALF 2600u
/** @brief The pulse width of target 0, us; not measured. */
#define HITEC_LIVE_D_US_ZERO 750u
/** @brief Targets per us of pulse width; not measured. */
#define HITEC_LIVE_D_T_PER_US 4u
/** @brief Degrees the position range 0 to 16383 spans on a standard
 *         servo, in 0.01deg (the D series manual). */
#define HITEC_LIVE_D_SPAN_CDEG 22000u

/* --- the pass ------------------------------------------------------------ */

/** @brief The most the commanded position moves in one pass, counts
 *         (2.0deg). */
#define HITEC_LIVE_SLEW 150u
/** @brief A pass is free while the position lies this close to the
 *         command, counts (0.7deg). */
#define HITEC_LIVE_LAG_FREE 50u
/** @brief A pass whose position lies further than this from the command
 *         counts toward a bind, counts (2.0deg). */
#define HITEC_LIVE_LAG_BIND 150u
/** @brief Passes in a row over HITEC_LIVE_LAG_BIND that bind. */
#define HITEC_LIVE_LAG_PASSES 2u
/** @brief A bind moves the command back from the last free position by
 *         this much toward the neutral, counts (1.0deg). */
#define HITEC_LIVE_BACK 75u
/** @brief The stall level, % of the session's current limit. */
#define HITEC_LIVE_STALL_PCT 80u
/** @brief The time the current stays at or above the stall level before
 *         it binds, ms: the limit run's SERVO_LIMRUN_STALL_MS. */
#define HITEC_LIVE_STALL_MS 400u
/** @brief The commanded change the AS5600 judge waits for, 0.01deg. */
#define HITEC_LIVE_ENC_MIN_CDEG 300u
/** @brief AS5600 counts in one turn. */
#define HITEC_LIVE_ENC_COUNTS 4096u

/* --- FIND STOP ----------------------------------------------------------- */

/** @brief FIND STOP's step, counts (0.5deg). */
#define HITEC_LIVE_FIND_STEP 37u
/** @brief Passes FIND STOP judges each position. */
#define HITEC_LIVE_FIND_PASSES 2u
/** @brief FIND STOP's cap for one side, ms: the limit run's
 *         SERVO_LIMRUN_SEARCH_MS. */
#define HITEC_LIVE_FIND_MS 40000u

/** @brief The stored positions the mapping runs through. */
typedef struct {
    uint16_t left;   /**< 0xB2, target 400 */
    uint16_t mid;    /**< 0xC2, target 3000 */
    uint16_t right;  /**< 0xB0, target 5600 */
} hitec_live_ends_t;

/** @brief The command of one pass. */
typedef struct {
    uint16_t p;        /**< the position commanded this pass */
    uint16_t target;   /**< its target, the 0x1E value, 0 to 6000 */
    uint16_t lo;       /**< the reach: the position of target 0 */
    uint16_t hi;       /**< the reach: the position of target 6000 */
    uint8_t  clamped;  /**< 1: the wanted position lies outside the
                            reach; the command stops at its edge */
} hitec_live_cmd_t;

/** @brief What one pass saw. */
typedef struct {
    uint32_t now_ms;     /**< the clock when the readings were taken */
    uint16_t p;          /**< the position commanded this pass */
    uint16_t pos;        /**< 0x0C, read after the command */
    uint16_t ma;         /**< the servo supply's current, mA */
    uint16_t angle;      /**< the AS5600 angle, 0 to 4095 */
    uint8_t  has_ma;     /**< 1: ma is a reading of this pass */
    uint8_t  has_angle;  /**< 1: angle is a reading of this pass */
} hitec_live_obs_t;

/** @brief The judge's verdict on one pass. */
typedef enum {
    HITEC_LIVE_FREE = 0,      /**< the position within
                                   HITEC_LIVE_LAG_FREE of the command */
    HITEC_LIVE_FOLLOWING,     /**< neither free nor bound */
    HITEC_LIVE_BOUND_LAG,     /**< the position further than
                                   HITEC_LIVE_LAG_BIND from a command
                                   standing still or moving outward, on
                                   HITEC_LIVE_LAG_PASSES passes in a
                                   row */
    HITEC_LIVE_BOUND_CURRENT, /**< the current at or above the stall level
                                   for HITEC_LIVE_STALL_MS */
    HITEC_LIVE_BOUND_ENC,     /**< the AS5600 angle moved less than half
                                   of a commanded change of
                                   HITEC_LIVE_ENC_MIN_CDEG or more */
} hitec_live_verdict_t;

/** @brief The judge of a loop's passes. */
typedef struct {
    uint32_t t_over;     /**< the first reading of the current at or
                              above the stall level, of the run that
                              stands */
    uint16_t mid;        /**< the neutral the loop moves back toward */
    uint16_t stall_ma;   /**< the stall level, mA */
    uint16_t last_p;     /**< the command of the pass before */
    uint16_t last_free;  /**< the position read on the last free pass */
    uint16_t enc_p;      /**< the command at the AS5600 reference */
    uint16_t enc_a;      /**< the angle at the AS5600 reference */
    uint8_t  over;       /**< passes in a row that count toward a bind */
    uint8_t  i_on;       /**< 1: t_over holds a run */
    uint8_t  enc_on;     /**< 1: the AS5600 reference holds */
} hitec_live_judge_t;

/** @brief Where FIND STOP stands. */
typedef enum {
    HITEC_FIND_RUN = 0,  /**< searching: command p this pass */
    HITEC_FIND_FOUND,    /**< bound: stop and end hold the result */
    HITEC_FIND_NONE,     /**< no bind up to the reach edge */
    HITEC_FIND_TIMEOUT,  /**< HITEC_LIVE_FIND_MS passed */
    HITEC_FIND_ABORTED,  /**< hitec_live_find_abort() */
} hitec_find_state_t;

/** @brief The side FIND STOP searches. */
typedef enum {
    HITEC_FIND_LEFT = 0,  /**< toward 0, for 0xB2 */
    HITEC_FIND_RIGHT,     /**< toward 16383, for 0xB0 */
} hitec_find_side_t;

/** @brief FIND STOP on one side. */
typedef struct {
    hitec_live_judge_t j;  /**< the judge of its passes */
    uint32_t t0;           /**< the clock at the start */
    uint16_t p;            /**< the position to command this pass */
    uint16_t edge;         /**< the reach's edge on this side */
    uint16_t stop;         /**< FOUND: the last free position, read;
                                NONE: the edge */
    uint16_t end;          /**< FOUND: the end point, stop moved
                                HITEC_LIVE_BACK toward the neutral */
    uint8_t  side;         /**< hitec_find_side_t */
    uint8_t  passes;       /**< passes judged at p */
    uint8_t  state;        /**< hitec_find_state_t */
    uint8_t  why;          /**< FOUND: the hitec_live_verdict_t */
} hitec_live_find_t;

/* --- mapping ------------------------------------------------------------- */

/**
 * @brief The stored positions of a D series image, for the mapping.
 *
 * @param img  The image: 0xB2, 0xC2, 0xB0 and 0x5E read.
 * @param out  Receives the positions.
 * @return false for NULL, a classic image, a register of the four not
 *         known, left < neutral < right not holding, a position above
 *         HITEC_D_POS_MAX, and a direction (0x5E) other than 0, CW: the
 *         CCW mapping is not measured.
 */
bool hitec_live_d_ends(const hitec_image_t *img, hitec_live_ends_t *out);

/**
 * @brief The position target @p t gives.
 *
 * Below 3000: neutral - (3000 - t) x (neutral - left) / 2600; from 3000:
 * neutral + (t - 3000) x (right - neutral) / 2600, rounded half away from
 * the neutral and clamped to 0 to HITEC_D_POS_MAX.
 *
 * @param e  The stored positions, left < mid < right.
 * @param t  Target, clamped to 0 to HITEC_D_TARGET_MAX.
 * @return The position; 0 for NULL or positions that do not ascend.
 */
uint16_t hitec_live_d_pos(const hitec_live_ends_t *e, uint16_t t);

/**
 * @brief The target that gives position @p pos: the inverse of
 *        hitec_live_d_pos(), rounded half away from 3000 and clamped to
 *        0 to HITEC_D_TARGET_MAX.
 *
 * @param e    The stored positions, left < mid < right.
 * @param pos  Position.
 * @return The target; HITEC_LIVE_D_T_MID, the neutral, for NULL or
 *         positions that do not ascend.
 */
uint16_t hitec_live_d_target(const hitec_live_ends_t *e, uint16_t pos);

/**
 * @brief The reach: the positions of targets 0 and HITEC_D_TARGET_MAX.
 *
 * @param e   The stored positions.
 * @param lo  Receives the position of target 0.
 * @param hi  Receives the position of target 6000.
 */
void hitec_live_d_reach(const hitec_live_ends_t *e, uint16_t *lo,
                        uint16_t *hi);

/**
 * @brief The target of a pulse width: (us - HITEC_LIVE_D_US_ZERO) x
 *        HITEC_LIVE_D_T_PER_US, clamped to 0 to HITEC_D_TARGET_MAX.  Not
 *        measured.
 *
 * @param us  Pulse width, us.
 * @return The target.
 */
uint16_t hitec_live_d_us_target(uint16_t us);

/**
 * @brief How far a pulse width takes the horn toward the stored end point
 *        of its side, per mille: |target - 3000| x 1000 / 2600, rounded.
 *
 * 1000us gives 769: the horn stops at 76.9% of the way to the left end.
 * Rests on hitec_live_d_us_target(), not measured.
 *
 * @param us  Pulse width, us.
 * @return Per mille, 0 for 1500us; above 1000 past the end point.
 */
uint16_t hitec_live_d_share_pm(uint16_t us);

/* --- the pass ------------------------------------------------------------ */

/**
 * @brief Move @p from toward @p to by HITEC_LIVE_SLEW at most.
 *
 * @param from  The command of the pass before.
 * @param to    The command wanted.
 * @return The command of this pass.
 */
uint16_t hitec_live_slew(uint16_t from, uint16_t to);

/**
 * @brief The command of one pass: @p want clamped to the reach, slew
 *        limited from @p last_p, and its target.
 *
 * A @p last_p outside the reach, up to HITEC_LIVE_LAG_FREE from its
 * edge, counts as the edge, so the first move is at most
 * HITEC_LIVE_SLEW + HITEC_LIVE_LAG_FREE.
 *
 * @param e       The stored positions, left < mid < right.
 * @param last_p  The command of the pass before; at the loop's start the
 *                position read.
 * @param want    The position wanted: the value being edited.
 * @param out     Receives the command.
 * @return false, and no command, for NULL, positions that do not ascend,
 *         and a @p last_p further than HITEC_LIVE_LAG_FREE outside the
 *         reach: no target puts the horn there, and the slew would not
 *         bound the first move.
 */
bool hitec_live_d_command(const hitec_live_ends_t *e, uint16_t last_p,
                          uint16_t want, hitec_live_cmd_t *out);

/**
 * @brief The stall level: HITEC_LIVE_STALL_PCT of @p limit_ma, rounded
 *        down.
 *
 * @param limit_ma  The session's current limit, mA.
 * @return The level, mA: 800 for 1000.
 */
uint16_t hitec_live_stall_ma(uint16_t limit_ma);

/**
 * @brief Prepare a judge.
 *
 * @param j         Judge.
 * @param mid       The neutral the loop moves back toward.
 * @param start_p   The position read at the loop's start: the first last
 *                  free position and the first command before.
 * @param limit_ma  The session's current limit, mA.
 */
void hitec_live_judge_init(hitec_live_judge_t *j, uint16_t mid,
                           uint16_t start_p, uint16_t limit_ma);

/**
 * @brief Judge one pass.
 *
 * - Lag: |pos - p|.  Free at HITEC_LIVE_LAG_FREE or less.  A pass over
 *   HITEC_LIVE_LAG_BIND whose command stands still or moves outward
 *   (|p - mid| >= |p before - mid|) counts; one that moves inward, and
 *   one at HITEC_LIVE_LAG_BIND or less, starts the count again.
 *   HITEC_LIVE_LAG_PASSES counted in a row bind.
 * - Current: readings at or above the stall level from the first of a
 *   run for HITEC_LIVE_STALL_MS or longer bind; a reading below ends the
 *   run; a pass with no reading leaves it.
 * - AS5600: the first pass with an angle sets the reference (the command
 *   and the angle); a pass with none drops it, and a command that turns
 *   back from the way it went since the reference sets it again.  Once
 *   the command of the pass before lies HITEC_LIVE_ENC_MIN_CDEG or more
 *   from the reference's, an angle that moved less than half of that
 *   change binds; one that moved half or more sets the reference to this
 *   pass.  Judged against
 *   the pass before, the horn has one pass to follow; the direction of
 *   the angle is not judged.
 *
 * The position read on a free pass becomes the last free position.  A bind
 * names the first of lag, current and AS5600 that binds.
 *
 * @param j  Judge.
 * @param o  The pass.
 * @return The verdict; HITEC_LIVE_FOLLOWING for NULL.
 */
hitec_live_verdict_t hitec_live_judge(hitec_live_judge_t *j,
                                      const hitec_live_obs_t *o);

/**
 * @brief Where a bind moves the command: the last free position moved
 *        HITEC_LIVE_BACK toward the neutral, not past it.
 *
 * @param j  Judge.
 * @return The position; 0 for NULL.
 */
uint16_t hitec_live_back(const hitec_live_judge_t *j);

/* --- FIND STOP ----------------------------------------------------------- */

/**
 * @brief Start FIND STOP on one side, from the neutral.
 *
 * The horn stands at the neutral when it starts.  Each pass the caller
 * commands p, reads the position and hands the pass to
 * hitec_live_find_pass().  p moves HITEC_LIVE_FIND_STEP outward after
 * HITEC_LIVE_FIND_PASSES passes at a position, up to the reach's edge.
 *
 * @param f         Search.
 * @param e         The stored positions, left < mid < right.
 * @param side      hitec_find_side_t.
 * @param limit_ma  The session's current limit, mA.
 * @param now_ms    The clock.
 * @return false for NULL, positions that do not ascend or an unknown
 *         side.
 */
bool hitec_live_find_start(hitec_live_find_t *f, const hitec_live_ends_t *e,
                           uint8_t side, uint16_t limit_ma, uint32_t now_ms);

/**
 * @brief Judge one pass of FIND STOP.
 *
 * The pass's command is f->p, whatever @p o names.  A bind ends it FOUND:
 * stop is the last free position, end that moved HITEC_LIVE_BACK toward
 * the neutral.  HITEC_LIVE_FIND_PASSES passes at the edge with no bind
 * end it NONE.  Otherwise, at HITEC_LIVE_FIND_MS or more after the start, it ends
 * TIMEOUT.
 *
 * @param f  Search.
 * @param o  The pass.
 * @return The hitec_find_state_t; HITEC_FIND_ABORTED for NULL.
 */
uint8_t hitec_live_find_pass(hitec_live_find_t *f, const hitec_live_obs_t *o);

/**
 * @brief End a running FIND STOP with HITEC_FIND_ABORTED; an ended one
 *        keeps its result.
 *
 * @param f  Search.
 */
void hitec_live_find_abort(hitec_live_find_t *f);

#ifdef __cplusplus
}
#endif

#endif /* HITEC_LIVE_H */
