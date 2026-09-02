/*
 * Synchronising two servos that drive one control surface.
 *
 * Dual ailerons, elevator halves, a rudder on two horns: two servos on one
 * surface settle any disagreement between them through the surface.  Nothing
 * fails and nothing reports it; both servos push against each other and draw
 * extra current continuously.
 *
 * The objective is not a position.  There is no reference angle.  The total
 * current the pair draws is smallest where they stop working against each
 * other, which makes the problem a one-dimensional search per stage.
 *
 * The two errors separate:
 *
 *     a difference at centre   is an offset error  -> trim one servo's centre
 *     a difference at the ends is a travel error   -> scale one servo's throw
 *
 * Measured at the centre and then at each end, each correction comes from
 * its own measurement.  Each end gets its own number: a linkage is not
 * symmetric about centre once a horn and a pushrod are involved, the same
 * reason the limit search measures both ends.
 *
 * Current cannot show two servos that agree with each other and are both
 * wrong: a surface that is stiff nowhere and sits 5 degrees off.  That is a
 * separate measurement with the accelerometer on the surface.
 *
 * Fed measurements and returning commands, as servo_limit is: the host suite
 * runs it against a modelled pair and the coprocessor runs it against a real
 * one unchanged.
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
     * The scan found no minimum distinguishable from the noise.
     *
     * A pair that is already synchronised still produces a minimum, at zero
     * correction, because the scan moves one servo away from agreement.  So
     * this fault means the sensor cannot resolve the difference, and it is
     * reported rather than a correction derived from noise.
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
     * How long to wait after a move before a reading counts.
     *
     * Two parts, because the moves differ in size.  Stepping to the next scan
     * point is a few microseconds of pulse width; changing stage swings a
     * servo across its whole deflection, over 1 s on a slow servo.  One fixed
     * wait is either sized for the large move and wastes two thirds of the
     * search, or sized for the small move and reads two servos still
     * travelling at the start of every stage.
     *
     * The wait is @c settle_ms plus the time the commanded move takes at
     * @c slew_us_per_ms.  That rate is an estimate of the servos under test
     * and belongs on the pessimistic side: too slow costs time, too fast
     * costs the answer.
     */
    uint16_t settle_ms;
    float    slew_us_per_ms;
    uint16_t measure_ms;

    /**
     * How far the best reading of a scan must sit below the worst before the
     * minimum counts.
     *
     * Two servos that are already synchronised produce a flat scan, and the
     * argmin of a flat scan is wherever the noise dips.  Without this test
     * the routine returns a correction derived from noise.
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

    /** What the pair draws at the best and the worst point of each stage.
     *  The difference is the gain from the correction. */
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
 * Advance the search with one reading of the pair's total current.
 *
 * Total, not per servo: the quantity minimised is what the two draw
 * together, so one sensor across both outputs is enough.
 *
 * Writes the two pulse widths to command through @p cmd_a and @p cmd_b.
 */
void servo_sync_step(servo_sync_t *sy, float total_a, uint32_t now_ms,
                     uint16_t *cmd_a, uint16_t *cmd_b);

/** 0..1 across all three stages. */
float servo_sync_progress(const servo_sync_t *sy);
