/**
 * @file
 * @brief The balance corrections from measured runs (vibe_solve.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "vibe_solve.h"

#include <float.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>

/** @brief pi in float. */
#define PI_F 3.14159265f
/** @brief Degrees to radians. */
#define RAD_F (PI_F / 180.0f)
/** @brief Below this share of the larger magnitude a difference is nothing. */
#define NOTHING_F 1e-6f
/** @brief Gauss-Newton steps at most. */
#define GN_STEPS 40
/** @brief Halvings of a step that does not lower the residual, at most. */
#define GN_HALVINGS 12

float vibe_wrap_deg(float deg)
{
    if (!isfinite(deg)) {
        return 0.0f;
    }
    float r = fmodf(deg, 360.0f);
    if (r < 0.0f) {
        r += 360.0f;
    }
    /* fmodf of a tiny negative angle plus 360 rounds to 360. */
    return (r >= 360.0f) ? 0.0f : r;
}

/**
 * @brief Whether a value is a finite magnitude: finite and not negative.
 *
 * @param v  The value.
 * @return   true when it is.
 */
static bool good_mag(float v)
{
    return isfinite(v) && v >= 0.0f;
}

vibe_solve_status_t vibe_solve_two_run(vibe_polar_t base, vibe_polar_t trial,
                                       float trial_mass, float trial_deg,
                                       vibe_solution_t *out)
{
    vibe_solution_t s;
    memset(&s, 0, sizeof(s));
    s.status = VIBE_SOLVE_BAD_INPUT;
    if (!good_mag(base.mag) || !good_mag(trial.mag) || !isfinite(base.deg)
            || !isfinite(trial.deg) || !isfinite(trial_mass)
            || !(trial_mass > 0.0f) || !isfinite(trial_deg)) {
        if (out != NULL) {
            *out = s;
        }
        return s.status;
    }
    const float x0 = base.mag * cosf(base.deg * RAD_F);
    const float y0 = base.mag * sinf(base.deg * RAD_F);
    const float x1 = trial.mag * cosf(trial.deg * RAD_F);
    const float y1 = trial.mag * sinf(trial.deg * RAD_F);
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float dm = hypotf(dx, dy);
    const float big = (base.mag > trial.mag) ? base.mag : trial.mag;
    s.baseline = base.mag;
    if (!(dm > NOTHING_F * big)) {
        s.status = VIBE_SOLVE_NO_EFFECT;
        if (out != NULL) {
            *out = s;
        }
        return s.status;
    }
    /* C = (V1 - V0) / (m at its angle). */
    const float lag = atan2f(dy, dx) / RAD_F - trial_deg;
    s.sens = dm / trial_mass;
    s.lag_deg = vibe_wrap_deg(lag);
    /* U = -V0 / C. */
    s.mass = base.mag / s.sens;
    s.deg = vibe_wrap_deg(base.deg + 180.0f - lag);
    s.effect = (base.mag > 0.0f) ? dm / base.mag : FLT_MAX;
    s.status = (s.effect < VIBE_SOLVE_WEAK_EFFECT) ? VIBE_SOLVE_WEAK
                                                   : VIBE_SOLVE_OK;
    if (out != NULL) {
        *out = s;
    }
    return s.status;
}

/**
 * @brief The four magnitudes the model predicts and their sum of squared
 *        residuals.
 *
 * @param p  M, t and d (radians).
 * @param m  The measured magnitudes, baseline first.
 * @param r  The residuals, model minus measured; may be NULL.
 * @param j  The Jacobian's rows, d/dM, d/dt, d/dd; may be NULL.
 * @return   The sum of squared residuals.
 */
static float model(const float p[3], const float m[4], float r[4],
                   float j[4][3])
{
    static const float k_at[3] = { 0.0f, 120.0f * RAD_F, 240.0f * RAD_F };
    const float mm = p[0];
    const float t = p[1];
    float cost = 0.0f;
    float res[4];
    res[0] = mm - m[0];
    if (j != NULL) {
        j[0][0] = 1.0f;
        j[0][1] = 0.0f;
        j[0][2] = 0.0f;
    }
    for (int i = 0; i < 3; ++i) {
        const float c = cosf(p[2] - k_at[i]);
        const float sn = sinf(p[2] - k_at[i]);
        float f2 = mm * mm + t * t + 2.0f * mm * t * c;
        float f = sqrtf((f2 > 0.0f) ? f2 : 0.0f);
        if (f < 1e-12f) {
            f = 1e-12f;
        }
        res[i + 1] = f - m[i + 1];
        if (j != NULL) {
            j[i + 1][0] = (mm + t * c) / f;
            j[i + 1][1] = (t + mm * c) / f;
            j[i + 1][2] = -mm * t * sn / f;
        }
    }
    for (int i = 0; i < 4; ++i) {
        cost += res[i] * res[i];
        if (r != NULL) {
            r[i] = res[i];
        }
    }
    return cost;
}

/**
 * @brief Solves a 3 x 3 system by elimination with partial pivoting.
 *
 * @param a  The matrix; overwritten.
 * @param b  The right side; overwritten by the solution.
 * @return   false when the matrix is singular.
 */
static bool solve3(float a[3][3], float b[3])
{
    for (int c = 0; c < 3; ++c) {
        int piv = c;
        for (int r = c + 1; r < 3; ++r) {
            if (fabsf(a[r][c]) > fabsf(a[piv][c])) {
                piv = r;
            }
        }
        if (!(fabsf(a[piv][c]) > 1e-20f)) {
            return false;
        }
        if (piv != c) {
            for (int k = 0; k < 3; ++k) {
                const float t = a[c][k];
                a[c][k] = a[piv][k];
                a[piv][k] = t;
            }
            const float t = b[c];
            b[c] = b[piv];
            b[piv] = t;
        }
        for (int r = c + 1; r < 3; ++r) {
            const float f = a[r][c] / a[c][c];
            for (int k = c; k < 3; ++k) {
                a[r][k] -= f * a[c][k];
            }
            b[r] -= f * b[c];
        }
    }
    for (int c = 2; c >= 0; --c) {
        float v = b[c];
        for (int k = c + 1; k < 3; ++k) {
            v -= a[c][k] * b[k];
        }
        b[c] = v / a[c][c];
    }
    return true;
}

vibe_solve_status_t vibe_solve_four_run(float m0, float m1, float m2, float m3,
                                        float trial_mass, vibe_solution_t *out)
{
    vibe_solution_t s;
    memset(&s, 0, sizeof(s));
    s.status = VIBE_SOLVE_BAD_INPUT;
    const float m[4] = { m0, m1, m2, m3 };
    bool good = isfinite(trial_mass) && trial_mass > 0.0f;
    float mean = 0.0f;
    float big = 0.0f;
    for (int i = 0; i < 4; ++i) {
        good = good && good_mag(m[i]);
        mean += m[i] * 0.25f;
        big = (m[i] > big) ? m[i] : big;
    }
    if (!good || !(big > 0.0f)) {
        if (out != NULL) {
            *out = s;
        }
        return s.status;
    }

    /* The closed form: m_i^2 = a + b cos(d - a_i) with a = M^2 + t^2 and
     * b = 2 M t; over 0, 120 and 240 degrees the cosines sum to 0. */
    const float s1 = m1 * m1;
    const float s2 = m2 * m2;
    const float s3 = m3 * m3;
    const float a = (s1 + s2 + s3) / 3.0f;
    const float p = (2.0f / 3.0f) * (s1 - 0.5f * s2 - 0.5f * s3);
    const float q = (2.0f / 3.0f) * (0.8660254f * s2 - 0.8660254f * s3);
    const float b = hypotf(p, q);
    float par[3];
    par[0] = m0;
    if (m0 > NOTHING_F * big) {
        par[1] = b / (2.0f * m0);
    } else {
        par[1] = sqrtf((a > 0.0f) ? a : 0.0f);
    }
    par[2] = atan2f(q, p);
    /* No effect is trials that read as the baseline did.  Trials that
     * differ from it but not from each other (b = 0) are no rotor:
     * the fit starts from a^2 - m0^2 and the residual says so. */
    bool differ = false;
    for (int i = 1; i < 4; ++i) {
        differ = differ || fabsf(m[i] - m0) > NOTHING_F * big;
    }
    if (!differ) {
        s.status = VIBE_SOLVE_NO_EFFECT;
        s.baseline = m0;
        if (out != NULL) {
            *out = s;
        }
        return s.status;
    }
    if (!(par[1] > NOTHING_F * big)) {
        const float t2 = a - m0 * m0;
        par[1] = (t2 > 0.0f) ? sqrtf(t2) : big;
    }

    /* Gauss-Newton on all four magnitudes from there. */
    float r[4];
    float jac[4][3];
    float cost = model(par, m, r, jac);
    for (int it = 0; it < GN_STEPS && cost > 0.0f; ++it) {
        float na[3][3] = { { 0.0f } };
        float g[3] = { 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < 4; ++i) {
            for (int u = 0; u < 3; ++u) {
                g[u] -= jac[i][u] * r[i];
                for (int v = 0; v < 3; ++v) {
                    na[u][v] += jac[i][u] * jac[i][v];
                }
            }
        }
        if (!solve3(na, g)) {
            break;
        }
        float scale = 1.0f;
        bool better = false;
        for (int h = 0; h < GN_HALVINGS; ++h) {
            const float trial[3] = { par[0] + scale * g[0],
                                     par[1] + scale * g[1],
                                     par[2] + scale * g[2] };
            const float c = model(trial, m, NULL, NULL);
            if (c < cost) {
                par[0] = trial[0];
                par[1] = trial[1];
                par[2] = trial[2];
                cost = model(par, m, r, jac);
                better = true;
                break;
            }
            scale *= 0.5f;
        }
        if (!better) {
            break;
        }
    }
    /* A negative M or t is the same rotor half a turn round. */
    if (par[0] < 0.0f) {
        par[0] = -par[0];
        par[2] += PI_F;
    }
    if (par[1] < 0.0f) {
        par[1] = -par[1];
        par[2] += PI_F;
    }
    s.residual = sqrtf(cost / 4.0f);
    s.baseline = par[0];
    s.sens = par[1] / trial_mass;
    s.effect = (par[0] > 0.0f) ? par[1] / par[0] : FLT_MAX;
    if (!(par[1] > NOTHING_F * big)
            || s.residual > VIBE_SOLVE_MAX_RESIDUAL * mean) {
        s.status = VIBE_SOLVE_INCONSISTENT;
    } else {
        s.mass = trial_mass * par[0] / par[1];
        s.deg = vibe_wrap_deg(par[2] / RAD_F + 180.0f);
        s.status = (s.effect < VIBE_SOLVE_WEAK_EFFECT) ? VIBE_SOLVE_WEAK
                                                       : VIBE_SOLVE_OK;
    }
    if (out != NULL) {
        *out = s;
    }
    return s.status;
}
