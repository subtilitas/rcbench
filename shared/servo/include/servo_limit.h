/*
 * Finding a servo's installed mechanical limit by current.
 *
 * A servo held against a mechanical stop draws stall current for as long as
 * it is commanded there.  The stop belongs to the linkage, the horn position
 * and the surface stops, not to the servo, so it differs per installation
 * and per end of travel and has to be measured in place.
 *
 * The stop has a signature.  While the surface is free, current against
 * commanded position is flat and low.  When the linkage binds, current rises
 * steeply: the servo has stopped moving the surface and is pushing.  The
 * search walks out from centre in small steps, watches for the rise, stops
 * at the first one rather than pushing through it, backs off by a margin,
 * and reports that position as the endpoint.
 *
 * The search drives toward a stop by design, so it carries three
 * protections that need nothing from the panel:
 *
 *   - a hard current ceiling that aborts immediately, wherever it is hit
 *   - a stall timeout on its own threshold, so current above the working
 *     level cannot persist, across positions as well as within one
 *   - a step small enough that one step cannot cross from free to bound
 *
 * The search is a state machine fed measurements, not a loop that sleeps,
 * so it runs on the host against a modelled servo and on the coprocessor
 * against a real one without change.
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
    /** The whole permitted travel was covered without binding.  Either the
     *  surface is free over that range or the sensor does not resolve the
     *  bind; reported as such rather than as a limit. */
    SERVO_LIMIT_FAULT_NO_LIMIT,
} servo_limit_fault_t;

typedef struct {
    uint16_t centre_us;      /**< where the search starts */
    int16_t  step_us;        /**< signed: direction and size of a step */
    uint16_t max_travel_us;  /**< how far from centre the search may go */

    uint16_t settle_ms;      /**< wait for the servo to arrive before reading */
    uint16_t measure_ms;     /**< average over this long once settled */

    /**
     * The knee.  Current must exceed the free-running baseline by both a
     * ratio and an absolute margin before a step counts as bound.
     *
     * Either test alone misreads a common case.  A ratio alone is fooled by
     * a very low baseline, where sensor noise is a large multiple of almost
     * nothing.  An absolute margin alone is fooled by a large servo, whose
     * free current exceeds a threshold chosen for a small one.
     */
    float    knee_ratio;
    float    knee_margin_a;

    float    hard_limit_a;   /**< abort at once, wherever it happens */

    /**
     * Working hard: above this the servo is doing real work, and it may not
     * do so for longer than stall_ms.
     *
     * A threshold of its own, independent of the knee test.  Armed from the
     * knee, the stall protection would depend on a tuning value: a knee set
     * too high to find anything would also disarm the timer meant to catch a
     * search pushing into a stop.
     *
     * It sits between the free current of any servo on this bench and the
     * hard ceiling, so ordinary work does not trip it and a bind does.
     */
    float    stall_above_a;
    uint16_t stall_ms;       /**< how long that may persist */
    uint16_t backoff_us;     /**< how far short of the bind to programme */
} servo_limit_cfg_t;

/** Defaults for a hobby servo on a control surface. */
void servo_limit_defaults(servo_limit_cfg_t *cfg, uint16_t centre_us,
                          bool positive);

typedef struct {
    servo_limit_cfg_t   cfg;
    servo_limit_state_t state;
    servo_limit_fault_t fault;

    uint16_t cmd_us;         /**< the current command to the servo */
    uint16_t last_free_us;   /**< the furthest position that was still free */
    uint16_t limit_us;       /**< the answer, once state is FOUND */

    float    baseline_a;     /**< free-running current, from the first steps */
    float    knee_a;         /**< the current measured at the bound position */

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
 * Call it at the sampling rate of the current sensor.  Calling after the
 * search has finished is safe; the returned width then holds the servo at
 * centre, so a finished search leaves no surface deflected.
 */
uint16_t servo_limit_step(servo_limit_t *lf, float current_a, uint32_t now_ms);

/** How far the search has gone, 0..1, for a progress bar. */
float servo_limit_progress(const servo_limit_t *lf);
