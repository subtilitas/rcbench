/**
 * @file
 * @brief A servo on a control surface, modelled for the limit search.
 *
 * Not a general servo model.  What the limit finder needs is the shape of
 * current against commanded position as the linkage goes from free to
 * bound.  The rest exists so the search cannot succeed for the wrong reason:
 * travel takes time, readings are noisy, and a preloaded surface does not
 * draw zero at rest.
 *
 * As with telemetry_sim: a bench with no hardware fitted runs its procedures
 * end to end, and everything the model produces is marked as simulated.
 * Nothing here decides anything on real hardware.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** @brief Configuration of the single-servo model. */
typedef struct {
    /** Where the linkage binds, as a pulse width.  Two values: a surface's
     *  stops are not symmetric about the servo's centre once a horn and a
     *  pushrod are involved, which is why the limit is measured per
     *  installation. */
    uint16_t stop_lo_us;
    uint16_t stop_hi_us;   /**< upper stop, pulse width, us */

    float    free_a;        /**< holding a free surface, A */
    float    stall_a;       /**< hard against the stop, A */
    /** How far past the stop the current takes to go from free to stall. A
     *  linkage is not infinitely stiff; this is the width of the knee. */
    uint16_t bind_us;

    /**
     * What the servo draws while it is slewing.
     *
     * A servo accelerating a surface draws a large fraction of stall current
     * on the first part of a step, indistinguishable from a bind to a reading
     * taken before the horn has arrived.  The search settles before it
     * measures for this reason, so the model reproduces it.
     */
    float    travel_a;
    /**
     * A stiff patch: a band of travel that costs more to hold than the rest
     * without being the end of anything, such as a bellcrank binding at one
     * angle, a pushrod rubbing through a former, or a tight hinge mid-range.
     *
     * The knee test carries an absolute margin as well as a ratio because of
     * these: on a lightly loaded surface a tight spot is a large multiple of
     * almost nothing, and a search that stops at one reports a limit short
     * of the real one.
     */
    uint16_t stiff_lo_us;
    uint16_t stiff_hi_us;
    float    stiff_a;

    float    slew_us_per_ms; /**< how fast the horn actually moves */
    float    noise_a;        /**< peak-to-peak sensor noise */

    /**
     * The servo's own travel limits, as the pulse widths they stop it at: a
     * command past one moves the horn to it and no further, and pushes no
     * harder than there.  A programmable servo's end angles (KST's LEFT and
     * RIGHT ANGLE) are such limits.  0 is none.
     */
    uint16_t limit_lo_us;
    uint16_t limit_hi_us;
} servo_sim_cfg_t;

/** @brief State of the single-servo model, advanced by servo_sim_step(). */
typedef struct {
    servo_sim_cfg_t cfg;     /**< configuration copied by servo_sim_init() */
    float    position_us;   /**< where the horn is, not where it was told */
    uint32_t last_ms;       /**< time of the last step, ms                */
    uint32_t seed;          /**< noise generator state                    */
    bool     started;       /**< true after the first step                */
} servo_sim_t;

/**
 * @brief Defaults for a standard servo on a well-set-up surface.
 *
 * A standard servo on a well-set-up surface, binding at 1150 and 1880 us.
 *
 * @param cfg  Configuration to fill. Does nothing when NULL.
 */
void servo_sim_defaults(servo_sim_cfg_t *cfg);

/**
 * @brief Start the single-servo model at the middle of its stops.
 *
 * @param s    Model state to initialise. Does nothing when NULL.
 * @param cfg  Configuration to copy. Does nothing when NULL.
 */
void servo_sim_init(servo_sim_t *s, const servo_sim_cfg_t *cfg);

/**
 * @brief Advance the model to now_ms with the servo commanded to cmd_us, and
 *        return the current its sensor would report.
 *
 * @param s       Model state.
 * @param cmd_us  Commanded pulse width, us.
 * @param now_ms  Current time, ms.
 * @return Current the sensor reports, A. 0 when s is NULL.
 */
float servo_sim_step(servo_sim_t *s, uint16_t cmd_us, uint32_t now_ms);

/* ------------------------------------------ two servos on one surface */

/*
 * A pair, coupled through the surface they both drive.
 *
 * The model renders two facts: disagreement costs current and agreement does
 * not, and the disagreement is settled mechanically rather than reported.
 * Servo B's centre and its throw are each wrong by a configurable amount,
 * the installation error the synchroniser measures.  The two errors are
 * separate parameters because the synchroniser separates them.
 */

/** @brief Configuration of two servos on one surface. */
typedef struct {
    uint16_t centre_us;     /**< centre pulse width, us                   */
    /** How far servo B's real centre sits from where it is commanded. */
    int16_t  offset_us;
    /** B's real throw as a multiple of what it is commanded. 1.0 is right. */
    float    travel;
    /** What the pair draws with nothing to fight about. */
    float    free_a;
    /** Extra *total* current per microsecond of disagreement. */
    float    fight_a_per_us;
    /** What the pair draws while either of them is still travelling.  As
     *  with the single servo, a reading taken too early sees this. */
    float    travel_a;
    float    slew_us_per_ms; /**< how fast each servo moves, us per ms     */
    float    noise_a;        /**< peak-to-peak sensor noise, A            */
} servo_pair_cfg_t;

/** @brief State of the two-servo model, advanced by servo_pair_step(). */
typedef struct {
    servo_pair_cfg_t cfg;   /**< configuration copied by servo_pair_init() */
    float    pos_a_us;      /**< where servo A is, us                     */
    float    pos_b_us;      /**< where servo B is, us                     */
    uint32_t last_ms;       /**< time of the last step, ms                */
    bool     started;       /**< true after the first step                */
    uint32_t seed;          /**< noise generator state                    */
} servo_pair_t;

/**
 * @brief Defaults for two servos on one surface.
 *
 * @param cfg        Configuration to fill.
 * @param centre_us  Centre pulse width, us.
 */
void servo_pair_defaults(servo_pair_cfg_t *cfg, uint16_t centre_us);

/**
 * @brief Start the two-servo model with both servos at the centre.
 *
 * @param p    Model state to initialise.
 * @param cfg  Configuration to copy.
 */
void servo_pair_init(servo_pair_t *p, const servo_pair_cfg_t *cfg);

/**
 * @brief Advance both servos to now_ms with the commands given, and return
 *        the total current the pair draws.
 *
 * Both servos take time to arrive, which is why this takes a clock: a
 * reading taken before they have arrived is a reading of two servos in
 * transit, not of two servos disagreeing.  The settle time separates the two.
 *
 * @param p         Model state.
 * @param cmd_a_us  Commanded pulse width of servo A, us.
 * @param cmd_b_us  Commanded pulse width of servo B, us.
 * @param now_ms    Current time, ms.
 * @return Total current the pair draws, A.
 */
float servo_pair_step(servo_pair_t *p, uint16_t cmd_a_us, uint16_t cmd_b_us,
                      uint32_t now_ms);

/**
 * @brief How far apart the two hold the surface.
 *
 * How far apart the two hold the surface, in microseconds.  Hardware cannot
 * report this; the search is blind to it and a test may read it.
 *
 * @param p         Model state.
 * @param cmd_a_us  Commanded pulse width of servo A, us.
 * @param cmd_b_us  Commanded pulse width of servo B, us.
 * @return Distance between the two positions, us.
 */
float servo_pair_disagreement(const servo_pair_t *p, uint16_t cmd_a_us,
                              uint16_t cmd_b_us);
