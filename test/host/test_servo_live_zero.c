/*
 * LIVE's ZERO row (servo_drive_ready.c): the zero MEASURED reads from,
 * taken by a tap with a reading on show.  With ANGLE SOURCE AS5600 the
 * tap stores the live count as ENC CENTER and asks a save; with ANGLE
 * SOURCE IMU it asks the IMU's own ZERO (imu_view_zero()).  No reading
 * from the current link, no magnet, or an IMU without a valid reading
 * take nothing.  The tap is taken ARMED as on RUDDER: the surface is at
 * neutral while the servo holds CENTER.  The rig is servo_rauto_kst_rig.h.
 *
 * Cases:
 * - live_shows_zero_between_port_and_supply
 * - a_tap_takes_the_as5600_count_as_enc_center
 * - no_reading_no_magnet_and_a_lost_link_take_nothing
 * - a_center_kept_from_before_reads_set
 * - the_imu_takes_its_own_zero
 * - zero_is_taken_armed_on_live_and_on_rudder
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rauto_kst_rig.h"
#include "imu_view.h"

/* LIVE's rows, as the card orders them. */
enum { LZ_PORT = 0, LZ_ZERO, LZ_SUPPLY, LZ_ARM };
/* RUDDER's ZERO row. */
enum { RZ_ZERO = 2 };

/** @brief @p s, or "(null)": a check that fails prints, not crashes. */
static const char *nn(const char *s)
{
    return s != NULL ? s : "(null)";
}

/** @brief Card row @p i as drawn now. */
static rdy_row_t drow(int i)
{
    rdy_row_t r;
    CHECK(ready_row(i, &r));
    return r;
}

/** @brief MEASURED's angle now.  @return degrees; 999 with none */
static float measured(void)
{
    float d = 999.0f;
    return servo_screen_log_angle(&d) ? d : 999.0f;
}

/** @brief A tap on card row @p i. */
static void row_tap(int i, uint8_t id)
{
    ev(SV_RDY_X, SV_DRV_ROW_Y(i), TOUCH_EVENT_DOWN, id);
    ev(SV_RDY_X, SV_DRV_ROW_Y(i), TOUCH_EVENT_UP, id);
    ra_ms(20u);
}

/** @brief Every IMU command the screens queued, dropped; how many ZEROs
 *         were among them.  @return the ZEROs */
static unsigned imu_zeros(void)
{
    unsigned n = 0u;
    imu_cmd_t c;
    while (imu_view_take(&c)) {
        n += (c.kind == IMU_CMD_ZERO) ? 1u : 0u;
    }
    return n;
}

/** @brief The IMU's view as the panel hands it: a reading or none, its
 *         zero taken or not. */
static void imu_view(bool valid, bool zeroed)
{
    imu_view_t v;
    memset(&v, 0, sizeof(v));
    v.link = IMU_LINK_CONNECTED;
    v.r.valid = valid;
    v.r.zeroed = zeroed;
    v.r.taken_ms = g_rig.ms;
    imu_view_set(&v);
}

/** @brief LIVE on a SAFE bench, the AS5600 reading the horn at 1500us,
 *         ENC CENTER @p center. */
static void live_safe(int center)
{
    ra_setup(true, false);
    ra.armed = false;
    servo_screen_set_armed(false);
    g_rig.sup_on = false;
    ra_pd_reading(false);
    settings_set(SET_ENC_CENTER, (float)center);
    go(SERVO_FN_LIVE);
    ra_ms(200u);
    settings_cancel_save();
}

/* LIVE shows PORT, ZERO, SUPPLY and ARM; SWEEP keeps PORT, SUPPLY and
 * ARM.  ZERO's label is RUDDER's. */
TEST_CASE(live_shows_zero_between_port_and_supply)
{
    live_safe(0);
    CHECK_EQ(ready_count(), 4);
    CHECK_STR_EQ(drow(LZ_PORT).label, TR(SVH_PORT));
    CHECK_STR_EQ(drow(LZ_ZERO).label, TR(RDY_ZERO));
    CHECK_STR_EQ(drow(LZ_SUPPLY).label, "PD MINI");
    CHECK_STR_EQ(drow(LZ_ARM).label, "ARM");
    go(SERVO_FN_SWEEP);
    CHECK_EQ(ready_count(), 3);
    CHECK_STR_EQ(drow(1).label, "PD MINI");
    go(SERVO_FN_RUDDER);
    CHECK_STR_EQ(drow(RZ_ZERO).label, TR(RDY_ZERO));
}

/* No zero: amber, SET ZERO WITH THE SURFACE AT NEUTRAL, which refuses
 * nothing.  A tap takes the live count, 2048 at 1500us, as ENC CENTER,
 * asks a save, and MEASURED reads 0.0 there; the row reads SET.  Moved
 * 256 counts on, a tap on SET takes the new count. */
TEST_CASE(a_tap_takes_the_as5600_count_as_enc_center)
{
    live_safe(0);
    rdy_row_t r = drow(LZ_ZERO);
    CHECK_EQ(r.st, (uint8_t)RDY_WARN);
    CHECK_STR_EQ(nn(r.why), TR(RA_NO_ZERO));
    CHECK(r.fix);
    CHECK(drive_hold_blocked(NULL) == NULL);
    CHECK(!drive_zero_set());
    row_tap(LZ_ZERO, 2);
    CHECK_EQ(settings_get_int(SET_ENC_CENTER), CENTER_COUNT);
    CHECK(settings_save_asked());
    r = drow(LZ_ZERO);
    CHECK_EQ(r.st, (uint8_t)RDY_OK);
    CHECK_STR_EQ(r.value, TR(RDY_SET));
    CHECK_NEAR(measured(), 0.0f, 0.01f);
    settings_cancel_save();

    /* 256 counts on: 22.5deg from the zero, then 0.0 from a new one. */
    const servo_test_enc_t e = { .valid = true,
                                 .raw = (uint16_t)(CENTER_COUNT + 256),
                                 .taken_ms = g_rig.ms };
    ra.enc = false;
    servo_screen_encoder(&e);
    CHECK_NEAR(measured(), 22.5f, 0.01f);
    row_tap(LZ_ZERO, 2);
    CHECK_EQ(settings_get_int(SET_ENC_CENTER), CENTER_COUNT + 256);
    CHECK_NEAR(measured(), 0.0f, 0.01f);
    settings_cancel_save();
}

/* Without a reading from the current link, after a link loss until a
 * new reading arrives, or with the sensor reporting no magnet, the row
 * takes no tap: the band names its text and ENC CENTER stays. */
TEST_CASE(no_reading_no_magnet_and_a_lost_link_take_nothing)
{
    live_safe(0);
    ra.enc = false;
    const servo_test_enc_t none = { .valid = false, .no_magnet = true,
                                    .taken_ms = g_rig.ms };
    servo_screen_encoder(&none);
    rdy_row_t r = drow(LZ_ZERO);
    CHECK(!r.fix);
    CHECK_EQ(r.st, (uint8_t)RDY_WARN);
    row_tap(LZ_ZERO, 2);
    CHECK_EQ(settings_get_int(SET_ENC_CENTER), 0);
    CHECK(!settings_save_asked());
    char want[128];
    snprintf(want, sizeof(want), TR(RDY_BAND), TR(RDY_ZERO), TR(RA_NO_ZERO));
    CHECK_STR_EQ(nn(ui_router_alert()), want);

    /* A reading, then the link lost: the last count is no angle now. */
    const servo_test_enc_t e = { .valid = true, .raw = 1000u,
                                 .taken_ms = g_rig.ms };
    servo_screen_encoder(&e);
    CHECK(drow(LZ_ZERO).fix);
    servo_screen_set_link(false);
    servo_screen_encoder(&e);
    CHECK(!drow(LZ_ZERO).fix);
    row_tap(LZ_ZERO, 2);
    CHECK_EQ(settings_get_int(SET_ENC_CENTER), 0);

    /* The link back and a new reading: taken. */
    servo_screen_set_link(true);
    servo_screen_encoder(&e);
    row_tap(LZ_ZERO, 2);
    CHECK_EQ(settings_get_int(SET_ENC_CENTER), 1000);
    settings_cancel_save();
}

/* An ENC CENTER other than 0 is a zero kept across a restart: SET.
 * Without a reading SET stays, and a tap names that there is none.
 * ENC CENTER on LIVE's OPTIONS list is the same setting. */
TEST_CASE(a_center_kept_from_before_reads_set)
{
    live_safe(CENTER_COUNT);
    CHECK(drive_zero_set());
    CHECK_EQ(drow(LZ_ZERO).st, (uint8_t)RDY_OK);
    ra.enc = false;
    const servo_test_enc_t none = { .valid = false, .taken_ms = g_rig.ms };
    servo_screen_encoder(&none);
    rdy_row_t r = drow(LZ_ZERO);
    CHECK_EQ(r.st, (uint8_t)RDY_OK);
    CHECK(!r.fix);
    row_tap(LZ_ZERO, 2);
    char want[128];
    snprintf(want, sizeof(want), TR(RDY_BAND), TR(RDY_ZERO), TR(RA_NO_ANGLE));
    CHECK_STR_EQ(nn(ui_router_alert()), want);
    CHECK_EQ(settings_get_int(SET_ENC_CENTER), CENTER_COUNT);

    /* A count of 0 taken reads SET: the tap, not the count, says so. */
    const servo_test_enc_t at0 = { .valid = true, .raw = 0u,
                                   .taken_ms = g_rig.ms };
    servo_screen_encoder(&at0);
    row_tap(LZ_ZERO, 2);
    CHECK_EQ(settings_get_int(SET_ENC_CENTER), 0);
    CHECK_EQ(drow(LZ_ZERO).st, (uint8_t)RDY_OK);
    settings_cancel_save();
}

/* ANGLE SOURCE IMU: the tap asks the IMU's ZERO, as SETUP > WIRELESS >
 * IMU's ZERO does, and leaves ENC CENTER; the row reads SET once the IMU reports its
 * zero.  An IMU view without a valid reading queues nothing. */
TEST_CASE(the_imu_takes_its_own_zero)
{
    ra_setup_on(true, false, true);
    ra.armed = false;
    servo_screen_set_armed(false);
    g_rig.sup_on = false;
    ra_pd_reading(false);
    go(SERVO_FN_LIVE);
    (void)imu_zeros();
    imu_view(true, false);
    ra_ms(400u);
    rdy_row_t r = drow(LZ_ZERO);
    CHECK_EQ(r.st, (uint8_t)RDY_WARN);
    CHECK(r.fix);
    row_tap(LZ_ZERO, 2);
    CHECK_EQ(imu_zeros(), 1u);
    CHECK_EQ(settings_get_int(SET_ENC_CENTER), CENTER_COUNT);
    imu_view(true, true);
    CHECK_EQ(drow(LZ_ZERO).st, (uint8_t)RDY_OK);
    CHECK_STR_EQ(drow(LZ_ZERO).value, TR(RDY_SET));

    /* The view without a reading: nothing queued. */
    imu_view(false, true);
    row_tap(LZ_ZERO, 2);
    CHECK_EQ(imu_zeros(), 0u);
    CHECK(!imu_view_zero());
    imu_view(false, false);
    settings_cancel_save();
}

/* ARMED, the servo at CENTER, the tap takes the zero on LIVE as on
 * RUDDER: a zero drives nothing. */
TEST_CASE(zero_is_taken_armed_on_live_and_on_rudder)
{
    ra_setup(true, false);
    settings_set(SET_ENC_CENTER, 0.0f);
    go(SERVO_FN_LIVE);
    ra_ms(200u);
    CHECK_STR_EQ(drow(LZ_ARM).value, "ARMED");
    row_tap(LZ_ZERO, 2);
    CHECK_EQ(settings_get_int(SET_ENC_CENTER), CENTER_COUNT);
    CHECK_STR_EQ(drow(LZ_ARM).value, "ARMED");
    settings_cancel_save();

    go(SERVO_FN_RUDDER);
    ra_ms(200u);
    CHECK_STR_EQ(drow(4).value, "ARMED");
    CHECK_EQ(drow(RZ_ZERO).st, (uint8_t)RDY_WARN);
    row_tap(RZ_ZERO, 2);
    CHECK_EQ(drow(RZ_ZERO).st, (uint8_t)RDY_OK);
    settings_cancel_save();
}

int main(void)
{
    RUN(live_shows_zero_between_port_and_supply);
    RUN(a_tap_takes_the_as5600_count_as_enc_center);
    RUN(no_reading_no_magnet_and_a_lost_link_take_nothing);
    RUN(a_center_kept_from_before_reads_set);
    RUN(the_imu_takes_its_own_zero);
    RUN(zero_is_taken_armed_on_live_and_on_rudder);
    return test_summary("servo_live_zero");
}
