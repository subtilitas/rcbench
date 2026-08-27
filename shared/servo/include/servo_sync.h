/*
 * Synchronising two servos driving one control surface.
 *
 * Dual ailerons, elevator halves, a big rudder on two horns: whenever two
 * servos are bolted to the same surface, any disagreement between them is
 * settled through the surface. They do not fail, they do not buzz, and
 * nothing about the model tells you. The surface simply sits there, stiff,
 * with both servos pushing against each other continuously and drawing extra
 * current to do it -- until something wears out or a hot day makes a gearbox
 * give up.
 *
 * The important consequence for a bench: **the objective is not a position.**
 * There is no reference to compare against and no angle that is known to be
 * right. What there is, is a physical minimum -- the total current the pair
 * draws is smallest exactly where they stop fighting -- and that turns the
 * whole problem into a one-dimensional search with an answer rather than a
 * judgement.
 *
 * The two errors separate cleanly, which is what saves this from being a
 * blind search over two variables at once:
 *
 *     fighting at centre      is an offset error   -> trim one servo's centre
 *     fighting at the extremes is a travel error   -> scale one servo's throw
 *
 * Measure at the centre and then at each end, and each correction falls out of
 * its own measurement. Each end gets its own number, because a linkage is not
 * symmetric about centre once a horn and a pushrod are involved -- the same
 * reason the limit search measures both ends separately.
 *
 * What current cannot say is whether the two servos agree with each other and
 * are *both* wrong -- a surface that is stiff nowhere but sits five degrees
 * off. That needs the accelerometer on the surface, and it is a separate
 * measurement rather than a refinement of this one.
 *
 * Fed measurements and returning commands, for the same reason as
 * servo_limit: the host suite runs it against a modelled pair and the
 * coprocessor will run it against a real one unchanged.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SERVO_SYNC_RUNNING = 0,
    SERVO_SYNC_DONE,
    SERVO_SYNC_FAULT,
} servo_sync_state_t;

typedef enum {
    SERVO_SYNC_FAULT_NONE = 0,
    /** Current crossed the hard ceiling. Stops where it stands. */
    SERVO_SYNC_FAULT_OVERCURRENT,
    /**
     * The scan found no minimum it could tell apart from the noise.
     *
     * Note what this does *not* mean. A pair that is already synchronised
     * still produces a clean minimum, because moving one servo away from
     * agreement is exactly what the scan does -- a correct installation shows
     * as a minimum at zero correction, not as an absent one. So the only
     * honest reading left is that the sensor cannot resolve the fight, and
     * saying so beats returning a correction assembled out of noise, which
     * would have the shape of an answer without being one.
     */
    SERVO_SYNC_FAULT_NO_MINIMUM,
} servo_sync_fault_t;

/** Which of the three measurements the search is on. */
typedef enum {
    SERVO_SYNC_STAGE_CENTRE = 0,
    SERVO_SYNC_STAGE_HIGH,
    SERVO_SYNC_STAGE_LOW,
    SERVO_SYNC_STAGE_COUNT,
} servo_sync_stage_t;

/** The most points one round of a scan may have. */
#define SERVO_SYNC_MAX_POINTS 9

typedef struct {
    uint16_t centre_us;
    uint16_t deflection_us; /**< how far out the ends are measured */

    int16_t  window_us;     /**< half-width of the first scan, each stage */
    uint8_t  points;        /**< samples per round, <= SERVO_SYNC_MAX_POINTS */
    uint8_t  rounds;        /**< how many times the window narrows */

    /**
     * How long to wait after a move before the reading means anything.
     *
     * Two parts, because the moves are not all the same size. Stepping to the
     * next point of a scan is a few microseconds; changing stage swings a
     * servo across its whole deflection, which on a slow servo is well over a
     * second. A single fixed wait either has to be sized for the big move --
     * and then two thirds of the search is spent waiting for small ones -- or
     * it is sized for the small move and the first reading of every stage is
     * taken of two servos still travelling.
     *
     * So the wait is @c settle_ms plus however long the move just commanded
     * ought to take at @c slew_us_per_ms. That figure is an estimate of the
     * servos under test and wants to be on the pessimistic side; getting it
     * wrong slow costs time, and getting it wrong fast costs the answer.
     */
    uint16_t settle_ms;
    float    slew_us_per_ms;
    uint16_t measure_ms;

    /**
     * How far the best reading of a scan must sit below the worst before the
     * minimum is believed.
     *
     * Two servos that are already synchronised produce a flat scan, and the
     * argmin of a flat scan is wherever the noise happened to dip. Without
     * this the routine would confidently return a correction derived from
     * nothing, which is worse than returning no correction at all -- it looks
     * like an answer.
     */
    float    min_depth_a;

    float    hard_limit_a;
} servo_sync_cfg_t;

typedef struct {
    servo_sync_cfg_t   cfg;
    servo_sync_state_t state;
    servo_sync_fault_t fault;

    /* The answers. All three are what the *second* servo should be commanded
     * to, given the first is at centre, at +deflection and at -deflection. */
    int16_t  trim_us;       /**< signed offset applied at centre */
    int16_t  travel_hi_us;  /**< B's throw when A is at +deflection */
    int16_t  travel_lo_us;  /**< B's throw when A is at -deflection */

    /** What the pair drew at the best point of each stage, and at the worst.
     *  The difference is what the operator actually gained. */
    float    best_a[SERVO_SYNC_STAGE_COUNT];
    float    worst_a[SERVO_SYNC_STAGE_COUNT];

    /* Internals. */
    servo_sync_stage_t stage;
    uint8_t  round;
    uint8_t  point;
    int32_t  win_centre_us;
    int32_t  win_half_us;
    float    scan_a[SERVO_SYNC_MAX_POINTS];
    uint32_t phase_start_ms;
    uint32_t travel_ms;     /**< extra wait for the move just commanded */
    bool     measuring;
    double   sum_a;
    uint32_t samples;
    bool     started;
    uint16_t cmd_a_us;
    uint16_t cmd_b_us;
} servo_sync_t;

void servo_sync_defaults(servo_sync_cfg_t *cfg, uint16_t centre_us);

void servo_sync_init(servo_sync_t *sy, const servo_sync_cfg_t *cfg);

/**
 * Advance the search with one reading of the pair's *total* current.
 *
 * Total, not per-servo: what is being minimised is what the two of them draw
 * together, and a per-servo split is not needed to find it. One sensor across
 * both outputs is enough, which matters because sensors are what this whole
 * family of measurements is short of.
 *
 * Writes the two pulse widths to command through @p cmd_a and @p cmd_b.
 */
void servo_sync_step(servo_sync_t *sy, float total_a, uint32_t now_ms,
                     uint16_t *cmd_a, uint16_t *cmd_b);

/** 0..1 across all three stages. */
float servo_sync_progress(const servo_sync_t *sy);
