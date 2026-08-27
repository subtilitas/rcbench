/*
 * A servo on a control surface, modelled well enough to be searched.
 *
 * Not a general servo model and not trying to be.  What the limit finder
 * needs is one thing rendered honestly: the shape of current against
 * commanded position as the linkage goes from free to bound. Everything else
 * here exists to stop the search succeeding for the wrong reason -- travel
 * takes time, readings are noisy, and a preloaded surface does not draw zero
 * when it is doing nothing.
 *
 * It shares its purpose with telemetry_sim: a bench with no hardware fitted
 * must still be able to run its procedures end to end, and everything they
 * produce must be marked as invented. Nothing here is used to decide anything
 * on real hardware.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /** Where the linkage binds, as a pulse width. Two of them, because a
     *  surface's stops are not symmetric about the servo's centre once a horn
     *  and a pushrod are in the way -- which is the entire reason the limit
     *  has to be measured per installation. */
    uint16_t stop_lo_us;
    uint16_t stop_hi_us;

    float    free_a;        /**< holding a free surface */
    float    stall_a;       /**< hard against the stop */
    /** How far past the stop the current takes to go from free to stall. A
     *  linkage is not infinitely stiff; this is the width of the knee. */
    uint16_t bind_us;

    /**
     * What the servo draws while it is slewing.
     *
     * Not a detail. A servo accelerating a surface draws real current -- a
     * large fraction of stall on the first part of a step -- and it is
     * indistinguishable from a bind to anything that reads the sensor before
     * the horn has arrived. That is the entire reason the search settles
     * before it measures, so the model has to be capable of the mistake.
     */
    float    travel_a;
    /**
     * A stiff patch: a band of travel that costs more to hold than the rest
     * of it without being the end of anything.
     *
     * Real installations have these -- a bellcrank that binds slightly at one
     * angle, a pushrod rubbing where it passes through a former, a hinge
     * that is a little tight in the middle of its range. They are the reason
     * the knee test carries an absolute margin as well as a ratio: on a
     * lightly loaded surface a tight spot is a large *multiple* of almost
     * nothing, and a search that stopped at every one of them would report a
     * limit a long way short of the real one.
     */
    uint16_t stiff_lo_us;
    uint16_t stiff_hi_us;
    float    stiff_a;

    float    slew_us_per_ms; /**< how fast the horn actually moves */
    float    noise_a;        /**< peak-to-peak sensor noise */
} servo_sim_cfg_t;

typedef struct {
    servo_sim_cfg_t cfg;
    float    position_us;   /**< where the horn is, not where it was told */
    uint32_t last_ms;
    uint32_t seed;
    bool     started;
} servo_sim_t;

/** A standard servo on a well-set-up surface, binding at 1150 and 1880 us. */
void servo_sim_defaults(servo_sim_cfg_t *cfg);

void servo_sim_init(servo_sim_t *s, const servo_sim_cfg_t *cfg);

/**
 * Advance the model to @p now_ms with the servo commanded to @p cmd_us, and
 * return the current its sensor would report.
 */
float servo_sim_step(servo_sim_t *s, uint16_t cmd_us, uint32_t now_ms);
