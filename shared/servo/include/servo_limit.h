/*
 * Finding a servo's real mechanical limit, in the aircraft, by current.
 *
 * Endpoints set by eye in a transmitter are guesses, and the cost of guessing
 * high is not a warning: a servo held against a stop draws stall current for
 * as long as it is asked to.  It cooks itself, empties the pack and wears the
 * gears, and nobody finds out until something strips.
 *
 * The stop has a signature.  While the surface is free, current against
 * commanded position is flat and low -- it takes almost nothing to hold a
 * balanced surface at an angle.  The moment the linkage binds, current climbs
 * steeply, because the servo is no longer moving anything and is simply
 * pushing.  So: walk out from centre slowly, watch for the knee, stop at the
 * first rise rather than pushing through it, back off by a margin, and that
 * is the endpoint to programme.
 *
 * It has to be measured in place.  The limit belongs to the linkage, the horn
 * position and the surface stops -- not to the servo -- so it is different in
 * every installation and cannot be looked up.  Which is exactly why a bench
 * that can measure it is worth having.
 *
 * This routine deliberately drives toward a stop, which makes it the clearest
 * case for the coprocessor's rule of protecting hardware without asking.
 * Three independent protections, none of which needs the panel:
 *
 *   - a hard current ceiling that aborts immediately, wherever it is hit
 *   - a stall timeout on its own threshold, so current above "working hard"
 *     can never be held for long, across positions as well as within one
 *   - an approach slow enough that one step cannot cross from free to bound
 *
 * The search is written as a state machine fed measurements rather than as a
 * loop that sleeps, so that it runs on the host against a modelled servo and
 * on the coprocessor against a real one with no difference between them.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SERVO_LIMIT_RUNNING = 0,
    SERVO_LIMIT_FOUND,
    SERVO_LIMIT_FAULT,
} servo_limit_state_t;

typedef enum {
    SERVO_LIMIT_FAULT_NONE = 0,
    /** Current crossed the hard ceiling. The search stops where it stands. */
    SERVO_LIMIT_FAULT_OVERCURRENT,
    /** Current stayed above the knee for longer than the stall timeout. */
    SERVO_LIMIT_FAULT_STALL,
    /** Ran the whole permitted travel without ever binding. Not a failure of
     *  the servo: either the surface really is free over that range, or the
     *  sensor is not seeing it. Reported rather than guessed at. */
    SERVO_LIMIT_FAULT_NO_LIMIT,
} servo_limit_fault_t;

typedef struct {
    uint16_t centre_us;      /**< where the search starts */
    int16_t  step_us;        /**< signed: which way to walk, and how far a step */
    uint16_t max_travel_us;  /**< how far from centre the search may go */

    uint16_t settle_ms;      /**< let the servo arrive before believing a reading */
    uint16_t measure_ms;     /**< average over this long once settled */

    /**
     * The knee. Current must exceed the free-running baseline by *both* a
     * ratio and an absolute margin before a step counts as bound.
     *
     * Two tests rather than one because either alone misreads a common case.
     * A ratio alone is fooled by a very low baseline, where sensor noise is a
     * large multiple of almost nothing. An absolute margin alone is fooled by
     * a large servo, whose free current already exceeds a threshold chosen
     * for a small one. Requiring both costs nothing and removes both.
     */
    float    knee_ratio;
    float    knee_margin_a;

    float    hard_limit_a;   /**< abort at once, wherever it happens */

    /**
     * Working hard: above this the servo is doing real work, and it may not
     * do so for longer than stall_ms.
     *
     * Deliberately a threshold of its own rather than the knee's. An earlier
     * version armed the stall timer from the knee test, which made the
     * protection depend on a knob that has nothing to do with safety: set the
     * knee too high to find anything and the timer went with it, so the one
     * configuration that makes the search push blindly into a stop was also
     * the one that switched off the timer meant to catch it.
     *
     * It sits between the free current of any servo this bench will see and
     * the hard ceiling, so ordinary work does not trip it and a bind does.
     */
    float    stall_above_a;
    uint16_t stall_ms;       /**< how long that may persist */
    uint16_t backoff_us;     /**< how far short of the bind to programme */
} servo_limit_cfg_t;

/** Sensible defaults for a hobby servo on a control surface. */
void servo_limit_defaults(servo_limit_cfg_t *cfg, uint16_t centre_us,
                          bool positive);

typedef struct {
    servo_limit_cfg_t   cfg;
    servo_limit_state_t state;
    servo_limit_fault_t fault;

    uint16_t cmd_us;         /**< what the servo is being told right now */
    uint16_t last_free_us;   /**< the furthest position that was still free */
    uint16_t limit_us;       /**< the answer, once state is FOUND */

    float    baseline_a;     /**< free-running current, from the first steps */
    float    knee_a;         /**< what was measured at the position that bound */

    /* Internals: the settle/measure cycle at each position. */
    uint32_t phase_start_ms;
    bool     measuring;
    double   sum_a;
    uint32_t samples;
    uint32_t baseline_steps; /**< how many positions have fed the baseline */
    double   baseline_sum;
    uint32_t elevated_since_ms;
    bool     elevated;
    bool     started;
} servo_limit_t;

void servo_limit_init(servo_limit_t *lf, const servo_limit_cfg_t *cfg);

/**
 * Advance the search with one current reading, and return the pulse width the
 * servo should be commanded to.
 *
 * Call it at the sampling rate of the current sensor. It is safe to keep
 * calling after the search has finished; the returned width then holds the
 * servo at centre, because a search that is over has no business leaving a
 * surface deflected.
 */
uint16_t servo_limit_step(servo_limit_t *lf, float current_a, uint32_t now_ms);

/** How far the search has gone, 0..1, for a progress bar. */
float servo_limit_progress(const servo_limit_t *lf);
