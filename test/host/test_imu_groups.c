/**
 * @file test_imu_groups.c
 * @brief The Bluetooth IMU's command groups across link drops (imu_ctl.h on
 * imu_rig.h): a group is unlock, register writes and SAVE, and it reaches
 * the sensor whole on one connection or is sent again whole.
 *
 * The failures this is about: a link that drops in the 50ms between two
 * commands of a group, after which the register write and SAVE go at the
 * next connection without their unlock and the write is not due again; a
 * calibration's exit cut the same way, which leaves the sensor in CALSW 1
 * under a card that reads CALIBRATION DONE.
 *
 * Cases:
 * - a_drop_between_two_commands_of_a_group_sends_the_group_again
 * - a_drop_in_the_calibrations_exit_sends_the_exit_again
 * - imu_off_between_two_commands_sends_the_group_again
 * - random_runs_never_write_a_register_without_its_unlock
 *
 * SPDX-License-Identifier: MIT
 */
#include "imu_rig.h"

/* Bound to the sensor, subscribed, and streaming at 50Hz. */
static void connected(rig_t *r)
{
    rig_init(r);
    rig_cfg(r, true, RIG_ADDR, 2, true);
    (void)rig_settle(r, 2000u);
    CHECK(imu_ble_can_send(&r->c.ble));
    rig_stream(r, 100u, 20u, 1.0f, 2.0f, 3.0f);
    CHECK_EQ(imu_ctl_link(&r->c), IMU_LINK_CONNECTED);
}

/* Stream until sensor command @p n has gone and its write was answered,
 * @p max ms at most. */
static void stream_until_answered(rig_t *r, unsigned n, uint32_t max)
{
    for (uint32_t k = 0; k < max; ++k) {
        if (r->nsent >= n && r->c.ble.b.wr != BLEU_WR_BUSY) {
            return;
        }
        rig_stream(r, 1u, 20u, 0.0f, 0.0f, 0.0f);
    }
}

/* RATE changes to 200Hz; the link drops after the unlock was answered and
 * before RATE goes.  At the next connection the whole group goes: unlock,
 * RATE 200Hz, SAVE. */
TEST_CASE(a_drop_between_two_commands_of_a_group_sends_the_group_again)
{
    rig_t r;
    connected(&r);
    const unsigned at = r.nsent;
    rig_cfg(&r, true, RIG_ADDR, 4, true);
    stream_until_answered(&r, at + 1u, 1000u);
    CHECK_EQ(r.nsent, at + 1u);
    CHECK(rig_sent_unlock(&r, at));
    bsim_drop(&r.sim, r.t, 0x208u);
    rig_run(&r, 10u);
    CHECK_EQ(r.c.sensor.out_n, 0u);
    (void)rig_settle(&r, BLEU_RETRY_MS + 2000u);
    rig_stream(&r, 1000u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.nsent, at + 4u);
    CHECK(rig_sent_unlock(&r, at + 1u));
    CHECK(rig_sent_is(&r, at + 2u, WIT_REG_RATE, WIT_RATE_200HZ, 0u));
    CHECK(rig_sent_is(&r, at + 3u, WIT_REG_SAVE, 0u, 0u));
    rig_stream(&r, 1000u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.nsent, at + 4u);              /* once */
}

/* The calibration ends DONE and its exit's unlock goes; the link drops
 * before CALSW 0.  At the next connection unlock, CALSW 0 and SAVE go. */
TEST_CASE(a_drop_in_the_calibrations_exit_sends_the_exit_again)
{
    rig_t r;
    connected(&r);
    const unsigned at = r.nsent;
    const imu_cmd_t cal = { IMU_CMD_CALIBRATE, 0u };
    CHECK(imu_ctl_command(&r.c, &cal, r.t));
    stream_until_answered(&r, at + 3u, IMU_CAL_MS + 1000u);
    CHECK_EQ(r.nsent, at + 3u);
    CHECK(rig_sent_unlock(&r, at + 2u));
    imu_view_t v;
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.cal, IMU_CAL_DONE);
    bsim_drop(&r.sim, r.t, 0x208u);
    (void)rig_settle(&r, BLEU_RETRY_MS + 2000u);
    rig_stream(&r, 1000u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.nsent, at + 6u);
    CHECK(rig_sent_unlock(&r, at + 3u));
    CHECK(rig_sent_is(&r, at + 4u, WIT_REG_CALSW, 0u, 0u));
    CHECK(rig_sent_is(&r, at + 5u, WIT_REG_SAVE, 0u, 0u));
}

/* ALGORITHM changes to 9-AXIS; IMU goes OFF after the unlock was answered.
 * With IMU ON again the whole group goes. */
TEST_CASE(imu_off_between_two_commands_sends_the_group_again)
{
    rig_t r;
    connected(&r);
    const unsigned at = r.nsent;
    rig_cfg(&r, true, RIG_ADDR, 2, false);
    stream_until_answered(&r, at + 1u, 1000u);
    CHECK_EQ(r.nsent, at + 1u);
    rig_cfg(&r, false, RIG_ADDR, 2, false);
    rig_run(&r, 300u);
    CHECK_EQ(r.nsent, at + 1u);
    CHECK_EQ(r.c.sensor.out_n, 0u);
    rig_cfg(&r, true, RIG_ADDR, 2, false);
    (void)rig_settle(&r, 3000u);
    rig_stream(&r, 1000u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.nsent, at + 4u);
    CHECK(rig_sent_unlock(&r, at + 1u));
    CHECK(rig_sent_is(&r, at + 2u, WIT_REG_AXIS6, 0u, 0u));
    CHECK(rig_sent_is(&r, at + 3u, WIT_REG_SAVE, 0u, 0u));
}

/* ------------------------------------------------------- the random run */

/** How long the model's unlock holds, ms. */
#define FZ_UNLOCK_MS 10000u

/** A sensor that takes a register write only within FZ_UNLOCK_MS of an
 *  unlock received on the same connection. */
typedef struct {
    bool     unlocked;
    uint32_t unlock_at;
    uint8_t  rate;       /* RATE as held */
    bool     six;        /* AXIS6 as held */
    uint8_t  calsw;      /* CALSW as held */
    unsigned refused;    /* register writes it did not take */
} fz_sensor_t;

typedef struct {
    rig_t       r;
    fz_sensor_t s;
    bool        en;
    int         rate;
    bool        six;
    uint32_t    rng;
} fz_t;

/* xorshift32: the next value of @p f's sequence. */
static uint32_t fz_next(fz_t *f)
{
    uint32_t x = f->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    f->rng = x;
    return x;
}

/* The commands the rig logged since the last call, into the model. */
static void fz_take(fz_t *f)
{
    rig_t *r = &f->r;
    fz_sensor_t *s = &f->s;
    if (!r->sim.connected) {
        s->unlocked = false;        /* the unlock ends with the link */
    }
    for (unsigned i = 0; i < r->nsent; ++i) {
        if (!r->sim.connected) {
            continue;               /* written to nothing */
        }
        if (rig_sent_unlock(r, i)) {
            s->unlocked = true;
            s->unlock_at = r->sent_at[i];
            continue;
        }
        if (!s->unlocked || r->sent_at[i] - s->unlock_at > FZ_UNLOCK_MS) {
            ++s->refused;
            continue;
        }
        const uint8_t reg = r->sent[i][2];
        const uint8_t lo = r->sent[i][3];
        if (reg == WIT_REG_RATE) {
            s->rate = lo;
        } else if (reg == WIT_REG_AXIS6) {
            s->six = lo != 0u;
        } else if (reg == WIT_REG_CALSW) {
            s->calsw = lo;
        }
    }
    r->nsent = 0u;
}

/* @p ms ms of a sensor streaming at 50Hz while connected. */
static void fz_stream(fz_t *f, uint32_t ms)
{
    uint8_t b[2u * WIT_FRAME_LEN];
    rig_frame(b, WIT_T_GYRO, 0, 0, 0, 400);
    rig_angle(&b[WIT_FRAME_LEN], 0.0f, 0.0f, 0.0f);
    for (uint32_t k = 0; k < ms; ++k) {
        if (f->r.t % 20u == 0u) {
            rig_notify(&f->r, b, sizeof b, 20u);
        }
        rig_tick(&f->r);
        fz_take(f);
    }
}

/* SETUP's values of @p f into the controller. */
static void fz_cfg(fz_t *f)
{
    rig_cfg(&f->r, f->en, RIG_ADDR, f->rate, f->six);
}

/* One random action. */
static void fz_act(fz_t *f)
{
    switch (fz_next(f) % 8u) {
    case 0:
    case 1:
        f->rate = (int)(fz_next(f) % 5u);
        fz_cfg(f);
        break;
    case 2:
        f->six = !f->six;
        fz_cfg(f);
        break;
    case 3: {
        const imu_cmd_t cal = { IMU_CMD_CALIBRATE, 0u };
        (void)imu_ctl_command(&f->r.c, &cal, f->r.t);
        break;
    }
    case 4:
    case 5:
        bsim_drop(&f->r.sim, f->r.t, 0x208u);
        break;
    case 6:
        f->en = !f->en;
        fz_cfg(f);
        break;
    default:
        f->r.sim.lat = 5u + fz_next(f) % 121u;      /* 5 to 125ms */
        break;
    }
}

/* One seed: 60 random actions, then IMU ON and a quiet stream until the
 * sensor holds SETUP's values, 120s at most.
 * @return whether the run ended with no refused write and the sensor as
 *         SETUP says. */
static bool fz_seed(uint32_t seed, fz_t *f)
{
    memset(f, 0, sizeof *f);
    f->rng = seed * 2654435761u + 1u;
    f->en = true;
    f->rate = 2;
    f->six = true;
    f->s.rate = WIT_RATE_50HZ;
    f->s.six = true;
    rig_init(&f->r);
    fz_cfg(f);
    fz_stream(f, 1500u);
    for (int i = 0; i < 60; ++i) {
        fz_act(f);
        /* Most waits fall into a group's 50ms gaps; one in 8 outlasts a
         * calibration. */
        const uint32_t wait = (fz_next(f) % 8u == 0u)
                                  ? 1u + fz_next(f) % 4000u
                                  : 1u + fz_next(f) % 200u;
        fz_stream(f, wait);
    }
    f->en = true;
    f->r.sim.lat = 20u;
    fz_cfg(f);
    uint32_t quiet = 0u;
    for (uint32_t k = 0; k < 120000u && quiet < 2000u; ++k) {
        fz_stream(f, 1u);
        const bool settled =
            imu_ctl_link(&f->r.c) == IMU_LINK_CONNECTED
            && imu_sensor_quiet(&f->r.c.sensor)
            && f->r.c.ble.b.wr != BLEU_WR_BUSY;
        quiet = settled ? quiet + 1u : 0u;
    }
    return quiet >= 2000u && f->s.refused == 0u
           && f->s.rate == imu_ctl_rate_code(f->rate) && f->s.six == f->six
           && f->s.calsw == 0u;
}

/* 3,000 seeds of RATE and ALGORITHM changes, CALIBRATE, link drops, IMU
 * OFF and ON and GATT answers 5 to 125ms late, against a sensor whose
 * unlock holds 10s on one connection: no register write goes without its
 * unlock, and every run ends with the sensor holding SETUP's values and
 * CALSW 0. */
TEST_CASE(random_runs_never_write_a_register_without_its_unlock)
{
    static fz_t f;
    unsigned bad = 0u;
    for (uint32_t seed = 1u; seed <= 3000u; ++seed) {
        if (!fz_seed(seed, &f)) {
            if (bad < 5u) {
                printf("seed %u: SETUP rate %02x six %d; sensor rate %02x "
                       "six %d calsw %u; refused %u; outbox %u link %d\n",
                       (unsigned)seed, imu_ctl_rate_code(f.rate), f.six,
                       f.s.rate, f.s.six, f.s.calsw, f.s.refused,
                       f.r.c.sensor.out_n, (int)imu_ctl_link(&f.r.c));
            }
            ++bad;
        }
    }
    CHECK_EQ(bad, 0u);
}

int main(void)
{
    RUN(a_drop_between_two_commands_of_a_group_sends_the_group_again);
    RUN(a_drop_in_the_calibrations_exit_sends_the_exit_again);
    RUN(imu_off_between_two_commands_sends_the_group_again);
    RUN(random_runs_never_write_a_register_without_its_unlock);
    return test_summary("imu_groups");
}
