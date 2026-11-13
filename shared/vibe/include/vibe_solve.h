/**
 * @file
 * @brief The balance corrections from measured runs: two runs with an
 *        index, four runs without one.
 *
 * Model.  The rotor is linear: the 1x vibration vector V is C U, where U
 * is the unbalance as a complex mass (mass, angle) at the trial radius and
 * C the influence coefficient of the rotor, its mount and the sensor.  A
 * constant sensor delay and the sensor's direction are part of C, so they
 * cancel.  All runs are at the same rpm.
 *
 * Angles.  Every angle here, the vibration phase, the trial position and
 * the correction, counts in degrees from the index mark in the order the
 * points pass the index sensor: a point at 30deg passes the sensor 30deg
 * of rotation after the mark does, which is against the direction of
 * rotation.  The node's phase (vibe_report_t::phase_cdeg, the rotation
 * from the index edge to the 1x peak) grows by the same angle a mass is
 * moved in this sense, so V = C U holds with no sign change.  Counted the
 * other way round, the trial and the correction would both be mirrored.
 * Without an index the angles count from any mark chosen on the rotor,
 * in the same sense.
 *
 * Correction.  The mass that cancels the measured unbalance, at the trial
 * radius, with the trial mass removed.
 *
 * Arithmetic in float; no double.  The panel's ESP32-P4 is RV32IMAFC:
 * single precision in hardware, double in software.
 *
 * Pure C, no SDK.  Host-tested in test_vibe_solve.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The trial changes the vibration by less than this fraction: WEAK. */
#define VIBE_SOLVE_WEAK_EFFECT 0.2f
/** @brief The four-run fit's rms residual over the mean magnitude above
 *         which the runs disagree: INCONSISTENT. */
#define VIBE_SOLVE_MAX_RESIDUAL 0.1f

/** @brief A 1x vibration vector in polar form. */
typedef struct {
    float mag;  /**< magnitude, any unit (the node's g) */
    float deg;  /**< phase in degrees from the index (vibe_solve.h, Angles) */
} vibe_polar_t;

/** @brief How a solution came out. */
typedef enum {
    VIBE_SOLVE_OK = 0,      /**< a correction */
    VIBE_SOLVE_WEAK,        /**< a correction; the trial changed the vibration by under VIBE_SOLVE_WEAK_EFFECT */
    VIBE_SOLVE_NO_EFFECT,   /**< the trial changed nothing measurable: no correction */
    VIBE_SOLVE_INCONSISTENT,/**< four runs that no rotor model fits: no correction */
    VIBE_SOLVE_BAD_INPUT    /**< a negative, zero or non-finite input: no correction */
} vibe_solve_status_t;

/** @brief A correction and what it was derived from. */
typedef struct {
    vibe_solve_status_t status; /**< how it came out */
    float mass;     /**< correction mass, in the trial mass's unit */
    float deg;      /**< where it goes, 0 to below 360 degrees */
    float effect;   /**< the trial's effect over the baseline magnitude */
    float sens;     /**< |C|: vibration per unit of mass */
    float lag_deg;  /**< arg C, two runs only: the phase the response lags the mass by */
    float baseline; /**< the baseline magnitude the correction cancels (four runs: as fitted) */
    float residual; /**< four runs: rms of the fit's magnitude residuals; two runs: 0 */
} vibe_solution_t;

/**
 * @brief The two-run method: a baseline and a run with a trial mass.
 *
 * C = (V1 - V0) / (m at its angle); the correction is -V0 / C.
 *
 * @param base        The baseline run's 1x vector.
 * @param trial       The trial run's 1x vector.
 * @param trial_mass  The trial mass, > 0.
 * @param trial_deg   Where it sat.
 * @param out         The solution.
 * @return            out->status.
 */
vibe_solve_status_t vibe_solve_two_run(vibe_polar_t base, vibe_polar_t trial,
                                       float trial_mass, float trial_deg,
                                       vibe_solution_t *out);

/**
 * @brief The four-run method: a baseline and the trial mass at 0, 120 and
 *        240 degrees, magnitudes only.
 *
 * Unknowns: the baseline vector's magnitude M and its angle d against the
 * trial frame, and the trial's effect t.  Run i measures
 * sqrt(M^2 + t^2 + 2 M t cos(d - a_i)).  The closed form of the
 * three-circle construction starts a Gauss-Newton least-squares fit of
 * M, d and t to all four magnitudes; the correction is m M / t at d + 180.
 *
 * @param m0          The baseline magnitude.
 * @param m1          The magnitude with the trial at 0 degrees.
 * @param m2          At 120 degrees.
 * @param m3          At 240 degrees.
 * @param trial_mass  The trial mass, > 0.
 * @param out         The solution.
 * @return            out->status.
 */
vibe_solve_status_t vibe_solve_four_run(float m0, float m1, float m2, float m3,
                                        float trial_mass, vibe_solution_t *out);

/**
 * @brief An angle in degrees brought into 0 to below 360.
 *
 * @param deg  The angle.
 * @return     The same angle, 0 <= result < 360.
 */
float vibe_wrap_deg(float deg);

#ifdef __cplusplus
}
#endif
