/**
 * @file test_servo_rudder_zero.c
 * @brief SERVO's RUDDER task: the AS5600's zero kept across a restart
 *        (servo_rudder_panel.c, SET_RUDDER_ZERO_DEG and
 *        SET_RUDDER_ZERO_KEY), dropped with another source; an IMU zero
 *        never saved.
 *
 * The AS5600 is on with its center at count 2048; 256 counts are 22.5°.
 * A restart is the settings saved to a store in memory, the screen reset
 * and the settings loaded.
 *
 * Cases:
 * - a_zero_survives_a_restart_on_its_source_and_goes_with_another
 * - an_imu_zero_is_gone_after_a_restart
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_screen_rig.h"

#include "servo_angle.h"
#include "servo_rudder_int.h"

/** @brief The AS5600's center count in these cases. */
#define CENTER_COUNT 2048u

/** @brief The AS5600 on, its center at CENTER_COUNT. */
static void encoder_on(void)
{
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTER, (float)CENTER_COUNT);
}

/**
 * @brief A reading at @p counts from the center, as the panel hands it.
 * @param counts the counts, signed
 */
static void reading(int counts)
{
    static uint32_t at_ms = 1000u;
    at_ms += 40u;
    const servo_test_enc_t e = {
        .valid = true,
        .raw = (uint16_t)(((int)CENTER_COUNT + counts) & 4095),
        .taken_ms = at_ms };
    servo_screen_encoder(&e);
}

/** @brief A tap on RUDDER's tab. */
static void to_rudder(void) { go(SERVO_FN_RUDDER); }

/**
 * @brief A tap at the middle of control @p k.
 * @param k the control
 */
static void press(rudder_ctl_t k)
{
    const gfx_rect_t r = rudder_rect(k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/**
 * @brief A zero taken at count 0 and a MAX of +22.5deg on the source in
 *        force, both checked.
 */
static void zero_and_max(void)
{
    reading(0);
    press(RUD_C_ZERO);
    reading(256);
    reading(0);
    const servo_rudder_t *g = rudder_geo();
    CHECK(g->zeroed);
    CHECK(g->have[1]);
}

/* A store in memory: what a save wrote, a restart loads. */
static float s_kept[SETTING_COUNT];
static bool  s_have_kept;

static bool kept_load(float *values, int count)
{
    if (!s_have_kept) {
        return false;
    }
    memcpy(values, s_kept, (size_t)count * sizeof(float));
    return true;
}

static bool kept_save(const float *values, int count)
{
    memcpy(s_kept, values, (size_t)count * sizeof(float));
    s_have_kept = true;
    return true;
}

static const settings_store_t k_kept = { kept_load, kept_save, NULL, NULL };

/** @brief The settings saved and the panel started again: the screen
 *         reset, the settings loaded from the store, RUDDER shown. */
static void restart(void)
{
    CHECK(settings_save());
    fresh();
    settings_set_store(&k_kept);
    settings_init();
    to_rudder();
}

/**
 * @brief Neither a zero nor a MAX kept, and the NO ZERO line on show.
 */
static void dropped(void)
{
    char line[64];
    const servo_rudder_t *g = rudder_geo();
    CHECK(!g->zeroed);
    CHECK(!g->have[0]);
    CHECK(!g->have[1]);
    CHECK_STR_EQ(rudder_line(2, line, sizeof(line)),
                 "NO ZERO: FROM THE SENSOR'S CENTER");
}

/* ZERO on the AS5600 is saved with its source and comes back after a
 * restart, the MAX not; another ANGLE SOURCE drops it and its saved copy,
 * so the AS5600 again needs a new ZERO. */
TEST_CASE(a_zero_survives_a_restart_on_its_source_and_goes_with_another)
{
    s_have_kept = false;
    fresh();
    settings_set_store(&k_kept);
    encoder_on();
    to_rudder();
    reading(100);
    zero_and_max();
    const float z = rudder_geo()->zero_deg;
    CHECK_EQ(settings_get_int(SET_RUDDER_ZERO_KEY), 1);
    CHECK_NEAR(settings_get(SET_RUDDER_ZERO_DEG), z, 1e-3f);
    restart();
    reading(0);
    const servo_rudder_t *g = rudder_geo();
    CHECK(g->zeroed);
    CHECK_NEAR(g->zero_deg, z, 1e-3f);
    CHECK(!g->have[0]);
    CHECK(!g->have[1]);
    /* Another source: gone, and gone after coming back. */
    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_IMU);
    dropped();
    CHECK_EQ(settings_get_int(SET_RUDDER_ZERO_KEY), 0);
    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_AS5600);
    CHECK(!rudder_geo()->zeroed);
    restart();
    reading(0);
    CHECK(!rudder_geo()->zeroed);
    settings_set_store(NULL);
}

/* An IMU zero lasts until the restart and is never saved, whatever the
 * axis and algorithm: the IMU's link is not authenticated.  A zero the
 * AS5600 saved goes when the source turns to the IMU; a key a store holds
 * that is not the AS5600's is dropped and restores nothing. */
TEST_CASE(an_imu_zero_is_gone_after_a_restart)
{
    static const struct { int axis, algo; } k[] = {
        { 0, 0 }, { 1, 0 }, { 2, 1 },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        s_have_kept = false;
        fresh();
        settings_set_store(&k_kept);
        settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_IMU);
        settings_set(SET_IMU_AXIS, (float)k[i].axis);
        settings_set(SET_IMU_ALGO, (float)k[i].algo);
        servo_screen_set_imu_axis((uint8_t)k[i].axis, false);
        to_rudder();
        zero_and_max();
        CHECK_EQ(settings_get_int(SET_RUDDER_ZERO_KEY), 0);
        restart();
        servo_screen_set_imu_axis((uint8_t)k[i].axis, false);
        dropped();
    }

    /* AS5600 saved, then the IMU: the saved zero goes. */
    s_have_kept = false;
    fresh();
    settings_set_store(&k_kept);
    encoder_on();
    to_rudder();
    zero_and_max();
    CHECK_EQ(settings_get_int(SET_RUDDER_ZERO_KEY), 1);
    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_IMU);
    dropped();
    CHECK_EQ(settings_get_int(SET_RUDDER_ZERO_KEY), 0);

    /* A key an IMU zero left in an older store: dropped, nothing back. */
    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_AS5600);
    settings_set(SET_RUDDER_ZERO_KEY, 14.0f);
    settings_set(SET_RUDDER_ZERO_DEG, 12.0f);
    dropped();
    CHECK_EQ(settings_get_int(SET_RUDDER_ZERO_KEY), 0);
    settings_set_store(NULL);
}

int main(void)
{
    RUN(a_zero_survives_a_restart_on_its_source_and_goes_with_another);
    RUN(an_imu_zero_is_gone_after_a_restart);
    return test_summary("servo_rudder_zero");
}
