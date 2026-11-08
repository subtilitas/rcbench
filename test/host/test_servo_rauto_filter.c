/*
 * AUTO end points (servo_rudder_auto.h) on the IMU through its low-pass
 * (imu.h): the surface of servo_rauto_rig.h read by an imu_t at 10Hz, the
 * run fed the filtered angle, the IMU's still time and the settle
 * allowance imu_filter_settle_ms() gives.
 *
 * The failures this is about: a run that takes an angle the filter has
 * not settled to, and so programs an end point off by the lag; a run
 * that fails NOT STILL because the filter is slower than the 3000ms it
 * waits; and a run on yaw that fails NOT STILL because pitch creeps.
 *
 * SPDX-License-Identifier: MIT
 */
/* Ahead of the rig: gfx.h, which imu_sensor.h brings, names a parameter g,
 * and after the rig's g that shadows it. */
#include "imu_sensor.h"

#include "servo_rauto_rig.h"

#include "imu.h"
#include "wit_frame.h"

/** @brief The IMU in the loop: its model, IMU FILTER, its frame period,
 *         and whether the run is given the settle allowance. */
static struct {
    imu_t    m;
    int32_t  k;
    uint32_t period;
    bool     allow;
    imu_axis_t axis;    /* the axis the surface turns and the run reads */
    float    creep;     /* pitch's creep while the run reads yaw, °/s */
    float    worst;     /* the largest |measured - surface| seen, ° */
    uint8_t  steps;     /* the measures checked */
} f;

/** @brief One angle frame at @p t into the IMU: @p deg on the rig's axis;
 *         pitch creeping f.creep °/s from -3.26° and roll at 4.04° while
 *         that axis is yaw, 0° otherwise. */
static void frame_in(float deg, uint32_t t)
{
    float v[3] = { 0.0f, 0.0f, 0.0f };
    if (f.axis == IMU_AXIS_Z) {
        v[IMU_AXIS_X] = 4.04f;
        v[IMU_AXIS_Y] = -3.26f + f.creep * (float)(t - g.t0) / 1000.0f;
    }
    v[f.axis] = deg;
    uint8_t b[WIT_FRAME_LEN];
    wit_angle_frame(b, v[0], v[1], v[2]);
    imu_feed(&f.m, b, sizeof b, t);
}

/** @brief The rig with the IMU filter @p k, a frame every @p period ms,
 *         the allowance given with @p allow. */
static void filter_rig(int32_t k, uint32_t period, bool allow)
{
    rig_init(0u);
    memset(&f, 0, sizeof(f));
    imu_init(&f.m);
    imu_set_filter(&f.m, k);
    f.k = k;
    f.period = period;
    f.allow = allow;
    f.axis = IMU_AXIS_X;
}

/** @brief One ms as the rig's tick(), with the angle from the IMU: the
 *         surface into it as frames, its filtered angle, still time and
 *         allowance to the run; each new measure held to the surface. */
static void ftick(void)
{
    ++g.now;
    meter(surface());
    bench();
    programmer();
    if ((g.now - g.t0) % f.period == 0u) {
        frame_in(g.angle, g.now);
    }
    imu_tick(&f.m, g.now);
    imu_reading_t r;
    (void)imu_reading(&f.m, &r);
    /* As the panel hands it on: the rig's axis and that axis' still. */
    servo_angle_in_t a;
    memset(&a, 0, sizeof(a));
    const bool ok = imu_handout_angle(&r, f.axis, false, &a);
    if ((g.now - g.t0) % 20u == 0u) {
        a.taken_ms = g.now;
        servo_rauto_angle(&g.run, &a);
    }
    servo_rauto_in_t in = rig_in();
    in.angle_valid = ok;
    in.angle_deg = a.deg;
    in.still_ms = a.still_ms;
    in.settle_ms = f.allow ? imu_filter_settle_ms(f.k, r.rate_dhz) : 0u;
    servo_rauto_do_t d;
    servo_rauto_step(&g.run, g.now, &in, &d);
    take(&d);
    drain();
    while (f.steps < g.run.step_n) {
        /* The surface stands where the measure was taken. */
        const float e = fabsf(g.run.steps[f.steps].measured_deg
                              - fabsf(g.angle));
        f.worst = (e > f.worst) ? e : f.worst;
        ++f.steps;
    }
}

/** @brief The run started and stepped until it ends, @p ms at most. */
static bool run_out(uint32_t ms)
{
    rig_fix();
    if (!servo_rauto_start(&g.run, &g.cfg, g.now)) {
        return false;
    }
    for (uint32_t k = 0; k < ms && servo_rauto_running(&g.run); ++k) {
        ftick();
    }
    for (int k = 0; k < 50; ++k) {
        ftick();
    }
    return !servo_rauto_running(&g.run);
}

/* IMU FILTER 1, 10 and 50 at 10Hz: the run passes, programs both end
 * points into the tolerance, and every angle it measures lies within
 * IMU_SETTLE_DEG and a word's LSB of the surface. */
TEST_CASE(auto_on_the_filtered_imu_measures_settled_angles_and_passes)
{
    static const int32_t k_k[] = { 1, 10, 50 };
    uint32_t took[3] = { 0u, 0u, 0u };
    for (size_t j = 0; j < sizeof k_k / sizeof k_k[0]; ++j) {
        filter_rig(k_k[j], 100u, true);
        CHECK(run_out(RAUTO_MS));
        CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
        CHECK(f.steps >= 2u);
        CHECK(f.worst <= IMU_SETTLE_DEG + 180.0f / 32768.0f);
        for (int s = 0; s < 2; ++s) {
            CHECK(fabsf(g.run.measured_deg[s] - g.run.goal_deg[s])
                  <= (float)g.cfg.tol_ddeg / 10.0f);
        }
        took[j] = g.run.end_ms - g.run.start_ms;
        /* The report names the wait the run gave. */
        char want[96];
        snprintf(want, sizeof(want), "within %ums of the command",
                 (unsigned)(SERVO_RAUTO_MOVE_MS + g.run.settle_ms));
        CHECK(in_report(want));
        CHECK_EQ(g.run.settle_ms == 0u, k_k[j] == 1);
    }
    /* The filter makes the run slower, not wrong. */
    CHECK(took[1] > took[0]);
    CHECK(took[2] > took[1]);
}

/* Without the allowance IMU FILTER 50 at 10Hz cannot settle in 3000ms: the
 * run ends NOT STILL, and it takes no angle on the way. */
TEST_CASE(without_the_allowance_a_slow_filter_ends_not_still)
{
    filter_rig(50, 100u, false);
    CHECK(run_out(RAUTO_MS));
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_NOT_STILL);
    CHECK_EQ(f.steps, 0u);
}

/* The bench of 2026-11-08: IMU FILTER 1 at 50Hz, the run on yaw (Z+),
 * pitch creeping 1.1°/s and roll at 4.04°.  The run waits for yaw's own
 * still time and passes, both end points in the tolerance; pitch's still
 * time never reaches SERVO_RAUTO_STILL_MS on the way. */
TEST_CASE(auto_on_yaw_passes_while_pitch_creeps)
{
    filter_rig(1, 20u, true);
    f.axis = IMU_AXIS_Z;
    f.creep = 1.1f;
    CHECK(run_out(RAUTO_MS));
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK(f.steps >= 2u);
    for (int s = 0; s < 2; ++s) {
        CHECK(fabsf(g.run.measured_deg[s] - g.run.goal_deg[s])
              <= (float)g.cfg.tol_ddeg / 10.0f);
    }
    imu_reading_t r;
    CHECK(imu_reading(&f.m, &r));
    CHECK(imu_still_ms(&r, IMU_AXIS_Z) >= SERVO_RAUTO_STILL_MS);
    CHECK(imu_still_ms(&r, IMU_AXIS_Y) < SERVO_RAUTO_STILL_MS);
}

int main(void)
{
    RUN(auto_on_the_filtered_imu_measures_settled_angles_and_passes);
    RUN(without_the_allowance_a_slow_filter_ends_not_still);
    RUN(auto_on_yaw_passes_while_pitch_creeps);
    return test_summary("servo_rauto_filter");
}
