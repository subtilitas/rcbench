/*
 * AUTO end points (servo_rudder_auto.h) on an IMU that creeps after each
 * step, as the BWT901CL in 6-AXIS did on the bench of 2026-11-17
 * (RUDDR044, IMU filter 1, 100Hz): the settle rule of servo_settle.h.
 *
 * The sensor: after the surface of servo_rauto_rig.h arrives, the IMU
 * reads the surface plus an error along the step's direction that follows
 * one of three measured curves, linear between the points measured and
 * x0.55 a second after the last:
 *
 *   A  center -> 1000us, 5.00V: 61.046° (+0.5s), 60.595 (+1.5),
 *      60.408 (+2.5), 60.320 (+3.5), 60.260 (+4.5), final 60.200: past
 *      the final value, creeping back.
 *   B  center -> 2000us, 5.00V: -31.943° (+0.3s), -32.492 (+1.3),
 *      -33.497 (+2.3), -34.041 (+3.3), -34.338 (+4.3), -34.503 (+5.3),
 *      -34.596 (+6.4), final -34.711: short of it, creeping on.
 *   C  2000us -> center: 11.393° (+1.3s), 13.508 (+6.3), final 13.607.
 *   D  the slow tail of 2026-11-18 (rud1118): measures taken 4.1s to
 *      6.2s after arrival under 2.0s within 0.20° lay 0.14° to 0.26° from
 *      the value held 10s later, the reading still moving 0.07°/s to
 *      0.1°/s.  Fitted as two components, 2.11° x exp(-t / 1.7s) and
 *      1.09° x exp(-t / 3.5s): B's 2.77° at 0.3s, and under the 2.0s /
 *      0.20° rule a take at 6.9s 0.19° short, 0.07°/s.  Run short of the
 *      final value (D-) and past it (D+).
 *
 * Before its first point a curve holds that point's error.  At 6.00V the
 * same steps settle at 60.23° and -34.59° with the same shape, so the
 * 6.00V runs differ in the surface's deflections only: 46.65° and 48.17°
 * from the center at 13.58°, against 46.62° and 48.29° at 5.00V.  Goals
 * 45.0° + 5% = 47.25° a side, as RUDDR044.
 *
 * Under test: the rule of 0.23.13 (500ms within 0.5°, no window) takes
 * the first angle more than the tolerance from where the reading settles;
 * the settle rule with IMU SETTLING TIME 3.0s and IMU PRECISION 0.10°
 * takes every angle, the verify pass's included, within the tolerance
 * (0.5° and 0.1°) of the surface, on every curve, both supplies; a reading
 * that never settles ends the run NOT STILL at the measure's wait, and the
 * 0.23.13 rule takes it; noise of ±0.02° around a settled value passes
 * (2026-11-18 at rest: 0.006° in any 2s); IMU filter 10 adds its own
 * settle to the wait.
 *
 * SPDX-License-Identifier: MIT
 */
/* Ahead of the rig: gfx.h, which imu_sensor.h brings, names a parameter g,
 * and after the rig's g that shadows it. */
#include "imu_sensor.h"

#include "servo_rauto_rig.h"

#include "imu.h"

/** @brief A measured curve: seconds after the step, the error along the
 *         step's direction, °. */
typedef struct {
    const float (*p)[2];
    int n;
    float sign;          /* a curve given by its components: 0 none */
} curve_t;

static const float k_a[][2] = {
    { 0.5f, 0.846f }, { 1.5f, 0.395f }, { 2.5f, 0.208f }, { 3.5f, 0.120f },
    { 4.5f, 0.060f },
};
static const float k_b[][2] = {
    { 0.3f, -2.768f }, { 1.3f, -2.219f }, { 2.3f, -1.214f },
    { 3.3f, -0.670f }, { 4.3f, -0.373f }, { 5.3f, -0.208f },
    { 6.4f, -0.115f },
};
static const float k_c[][2] = { { 1.3f, -2.214f }, { 6.3f, -0.099f } };
#define N_CURVES 5
static const curve_t k_curve[N_CURVES] = {
    { k_a, (int)(sizeof k_a / sizeof k_a[0]), 0.0f },
    { k_b, (int)(sizeof k_b / sizeof k_b[0]), 0.0f },
    { k_c, (int)(sizeof k_c / sizeof k_c[0]), 0.0f },
    { NULL, 0, -1.0f },                     /* D- */
    { NULL, 0, 1.0f },                      /* D+ */
};

/** @brief The sensor, the curve it follows, and what the run took. */
static struct {
    imu_t    m;
    int32_t  k;          /* IMU FILTER */
    const curve_t *c;
    float    noise;      /* ± around the reading, ° */
    bool     wander;     /* the reading never settles */
    float    target;     /* the surface's goal now */
    float    dir;        /* the step's direction toward it */
    bool     moving;
    uint32_t arrived;    /* when the surface got there */
    float    worst;      /* the largest |taken - surface|, ° */
    float    first;      /* the first measure's |taken - surface| */
    uint32_t took_max;   /* the longest from arrival to a take, ms */
    uint8_t  steps, vn;  /* the measures looked at */
    bool     took[2];    /* the search's first measures looked at */
} f;

/** @brief Error of curve @p c at @p t s after arrival, °. */
static float err_at(const curve_t *c, float t)
{
    if (c->sign != 0.0f) {
        return c->sign * (2.11f * expf(-t / 1.7f) + 1.09f * expf(-t / 3.5f));
    }
    if (t <= c->p[0][0]) {
        return c->p[0][1];
    }
    for (int i = 1; i < c->n; ++i) {
        if (t <= c->p[i][0]) {
            const float t0 = c->p[i - 1][0], t1 = c->p[i][0];
            return c->p[i - 1][1]
                   + (c->p[i][1] - c->p[i - 1][1]) * (t - t0) / (t1 - t0);
        }
    }
    return c->p[c->n - 1][1] * powf(0.55f, t - c->p[c->n - 1][0]);
}

/** @brief The rig with a surface of @p lo and @p hi degrees from the
 *         center, the run's tolerance @p tol_ddeg, curve @p c, the settle
 *         rule on with @p rule. */
static void creep_rig(float lo, float hi, uint16_t tol_ddeg,
                      const curve_t *c, bool rule)
{
    rig_init(0u);
    memset(&f, 0, sizeof(f));
    imu_init(&f.m);
    f.k = 1;
    imu_set_filter(&f.m, 1);
    f.c = c;
    /* 0.01° a count: the writes land within 0.005° of the goal. */
    g.gain = 0.01f;
    g.lo = 500u;
    g.hi = 10000u;
    g.v[0] = (uint16_t)lroundf(lo * 100.0f);
    g.v[1] = (uint16_t)lroundf(hi * 100.0f);
    g.dir[0] = 1.0f;          /* 1000us on the positive side, as roll */
    g.dir[1] = -1.0f;
    g.cfg.target_deg[0] = 47.25f;
    g.cfg.target_deg[1] = 47.25f;
    g.cfg.margin_pct = 0u;
    g.cfg.tol_ddeg = tol_ddeg;
    if (rule) {
        g.cfg.settle_win_ms = (uint16_t)SERVO_SETTLE_WIN_DEFAULT_MS;
        g.cfg.settle_band_deg = SERVO_SETTLE_BAND_DEFAULT_DEG;
    }
}

/** @brief The IMU's angle now: the surface, and after it arrived the
 *         curve's error along the step. */
static float sensor_now(void)
{
    const float want = rig_target(g.cmd);
    if (fabsf(want - f.target) > 1e-4f) {
        f.dir = (want > f.target) ? 1.0f : -1.0f;
        f.target = want;
        f.moving = true;
    }
    if (f.moving && fabsf(g.angle - want) < 1e-4f) {
        f.moving = false;
        f.arrived = g.now;
    }
    float v = g.angle;
    if (!f.moving && g.now != g.t0) {
        v += f.dir * err_at(f.c, (float)(g.now - f.arrived) / 1000.0f);
    }
    if (f.noise > 0.0f) {
        v += ((g.now / 10u) % 3u == 0u) ? f.noise : -f.noise;
    }
    if (f.wander) {
        /* ±0.3°, a period of 4s: inside the 0.5° still test, never inside
         * 0.20° for 2s. */
        v += 0.3f * sinf((float)g.now * 6.2832f / 4000.0f);
    }
    return v;
}

/** @brief A taken angle against the surface it stood on. */
static void judge(float taken)
{
    const float e = fabsf(fabsf(taken) - fabsf(g.angle));
    f.worst = fmaxf(f.worst, e);
    if (f.steps == 0u && f.vn == 0u && !f.took[1]) {
        f.first = e;
    }
    const uint32_t ms = g.now - f.arrived;
    f.took_max = ms > f.took_max ? ms : f.took_max;
}

/** @brief One ms: the rig, a frame into the IMU every 10ms (100Hz), the
 *         IMU's angle and still time to the run. */
static void ctick(void)
{
    ++g.now;
    meter(surface());
    bench();
    programmer();
    if ((g.now - g.t0) % 10u == 0u) {
        imu_feed_angles(&f.m, sensor_now(), 0.0f, 0.0f, g.now);
    }
    imu_tick(&f.m, g.now);
    imu_reading_t r;
    (void)imu_reading(&f.m, &r);
    servo_angle_in_t a;
    memset(&a, 0, sizeof(a));
    const bool ok = imu_handout_angle(&r, IMU_AXIS_X, false, &a);
    if ((g.now - g.t0) % 20u == 0u) {
        a.taken_ms = g.now;
        servo_rauto_angle(&g.run, &a);
    }
    servo_rauto_in_t in = rig_in();
    in.angle_valid = ok;
    in.angle_deg = a.deg;
    in.still_ms = a.still_ms;
    in.settle_ms = imu_filter_settle_ms(f.k, r.rate_dhz);
    servo_rauto_do_t d;
    servo_rauto_step(&g.run, g.now, &in, &d);
    take(&d);
    drain();
    /* The search's first measures when taken, the surface at the end
     * pulse; their steps, the first two, are the same angles. */
    for (int s = 0; s < 2; ++s) {
        if (g.run.s_took[s] && !f.took[s]) {
            f.took[s] = true;
            judge(g.run.s_deg[s]);
        }
    }
    if (f.steps < 2u && g.run.step_n >= 2u) {
        f.steps = 2u;
    }
    for (; f.steps < g.run.step_n; ++f.steps) {
        judge(g.run.steps[f.steps].measured_deg);
    }
    for (; f.vn < g.run.vn; ++f.vn) {
        judge(g.run.vdeg[f.vn]);
    }
}

/** @brief The run to its end, @p ms at most. */
static bool crun(uint32_t ms)
{
    rig_fix();
    f.target = rig_target(g.cmd);
    /* 2s of frames first: the IMU's rate measured, as on the bench. */
    for (int k = 0; k < 2000; ++k) {
        ctick();
    }
    if (!servo_rauto_start(&g.run, &g.cfg, g.now)) {
        return false;
    }
    for (uint32_t k = 0; k < ms && servo_rauto_running(&g.run); ++k) {
        ctick();
    }
    for (int k = 0; k < 50; ++k) {
        ctick();
    }
    return !servo_rauto_running(&g.run);
}

/** @brief The surfaces from the center, 5.00V and 6.00V, °. */
static const float k_side[2][2] = { { 46.62f, 48.29f }, { 46.65f, 48.17f } };

TEST_CASE(the_0_23_13_rule_takes_the_first_angle_outside_the_tolerance)
{
    for (int v = 0; v < 2; ++v) {
        for (int c = 0; c < N_CURVES; ++c) {
            creep_rig(k_side[v][0], k_side[v][1], 5u, &k_curve[c], false);
            CHECK(crun(RAUTO_MS));
            CHECK(f.steps + f.vn >= 1u);
            /* Taken 500ms after the command, the reading still moving. */
            CHECK(f.first > 0.5f);
        }
    }
}

TEST_CASE(the_settle_rule_takes_every_angle_within_the_tolerance)
{
    static const uint16_t k_tol[2] = { 5u, 1u };
    for (int v = 0; v < 2; ++v) {
        for (int c = 0; c < N_CURVES; ++c) {
            for (int t = 0; t < 2; ++t) {
                creep_rig(k_side[v][0], k_side[v][1], k_tol[t], &k_curve[c],
                          true);
                CHECK(crun(RAUTO_MS));
                CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
                CHECK_EQ(g.run.vn, SERVO_RAUTO_VERIFY_N);
                CHECK(f.worst <= (float)k_tol[t] / 10.0f);
                /* The defaults: 0.07° at most on every curve. */
                CHECK(f.worst <= 0.07f);
                /* After arrival: 6.5s on A, 9.4s on B, 9.3s on C, 10.4s
                 * on D. */
                CHECK(f.took_max <= 10600u);
                for (int s = 0; s < 2; ++s) {
                    CHECK(fabsf(g.run.measured_deg[s] - g.run.goal_deg[s])
                          <= (float)k_tol[t] / 10.0f + 0.0005f);
                }
            }
        }
    }
    CHECK(in_report("its span at most 0.10° over 3.0s, within 16000ms"));
}

TEST_CASE(a_reading_that_never_settles_ends_not_still_at_the_wait)
{
    creep_rig(46.62f, 48.29f, 5u, &k_curve[0], true);
    f.wander = true;
    CHECK(crun(RAUTO_MS));
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_NOT_STILL);
    CHECK_EQ(g.run.step_n, 0u);
    CHECK_EQ(g.run.end_ms - g.run.move_ms,
             SERVO_RAUTO_MOVE_MS + SERVO_RAUTO_CREEP_MS
                 + SERVO_SETTLE_WIN_DEFAULT_MS);
    CHECK(ended_safe());
    /* The 0.23.13 rule takes it. */
    creep_rig(46.62f, 48.29f, 5u, &k_curve[0], false);
    f.wander = true;
    CHECK(crun(RAUTO_MS));
    CHECK(g.run.step_n >= 1u);
}

TEST_CASE(noise_around_a_settled_value_passes)
{
    creep_rig(46.62f, 48.29f, 5u, &k_curve[1], true);
    f.noise = 0.02f;
    CHECK(crun(RAUTO_MS));
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK(f.worst <= 0.5f);
}

TEST_CASE(imu_filter_10_adds_its_own_settle_to_the_wait)
{
    creep_rig(46.62f, 48.29f, 5u, &k_curve[1], true);
    f.k = 10;
    imu_set_filter(&f.m, 10);
    CHECK(crun(RAUTO_MS));
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK(f.worst <= 0.5f);
    /* 78 frames at 15ms at 100Hz. */
    CHECK_EQ(g.run.settle_ms, 78u * 15u);
    CHECK_EQ(servo_rauto_wait_ms(&g.run),
             SERVO_RAUTO_MOVE_MS + 78u * 15u + SERVO_RAUTO_CREEP_MS
                 + SERVO_SETTLE_WIN_DEFAULT_MS);
    /* A start with a window and no band, or a window past the longest, is
     * refused. */
    servo_rauto_t r;
    servo_rauto_init(&r);
    servo_rauto_cfg_t c = g.cfg;
    c.settle_band_deg = 0.0f;
    CHECK(!servo_rauto_start(&r, &c, 0u));
    c = g.cfg;
    c.settle_win_ms = (uint16_t)(SERVO_SETTLE_WIN_MAX_MS + 1u);
    CHECK(!servo_rauto_start(&r, &c, 0u));
    c.settle_win_ms = (uint16_t)SERVO_SETTLE_WIN_MAX_MS;
    CHECK(servo_rauto_start(&r, &c, 0u));
}

int main(void)
{
    RUN(the_0_23_13_rule_takes_the_first_angle_outside_the_tolerance);
    RUN(the_settle_rule_takes_every_angle_within_the_tolerance);
    RUN(a_reading_that_never_settles_ends_not_still_at_the_wait);
    RUN(noise_around_a_settled_value_passes);
    RUN(imu_filter_10_adds_its_own_settle_to_the_wait);
    return test_summary("servo_rauto_settle");
}
