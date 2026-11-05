/*
 * SETUP > SENSORS > IMU (sensors_pane.h, sensors_rows.h): the entry's rows,
 * its state column, and what a RATE or an ALGORITHM change does.
 *
 * The failures this is about: an IMU entry that shows its rows while off,
 * a state word in a colour that says connected when it is not, a RATE
 * changed in SETUP that never reaches the sensor or reaches it without the
 * operator learning it needs a power cycle, and a SENSORS row past the
 * list's cap that its keys never reach.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

#include "imu_rig.h"
#include "sensors_rows.h"

TEST_CASE(the_imu_entry_shows_its_five_rows)
{
    fresh_hub();
    CHECK_EQ(SENS_ENTRY_IMU, SENS_ENTRY_TAP + 1);
    char name[16];
    CHECK_STR_EQ(sensors_pane_name(SENS_ENTRY_IMU, name, sizeof name), "IMU");
    setting_id_t rows[SENS_ENTRY_ROWS_MAX];
    CHECK_EQ(sensors_pane_rows(SENS_ENTRY_IMU, rows, SENS_ENTRY_ROWS_MAX), 1);
    CHECK_EQ(rows[0], SET_IMU_EN);
    settings_set(SET_IMU_EN, 1.0f);
    /* ON, AXIS, SIGN, RATE, ALGORITHM, and ANGLE SOURCE under them. */
    static const setting_id_t want[] = { SET_IMU_EN, SET_IMU_AXIS,
                                         SET_IMU_SIGN, SET_IMU_RATE,
                                         SET_IMU_ALGO, SET_ANGLE_SOURCE };
    CHECK_EQ(sensors_pane_rows(SENS_ENTRY_IMU, rows, SENS_ENTRY_ROWS_MAX), 6);
    for (int i = 0; i < 6; ++i) {
        CHECK_EQ(rows[i], want[i]);
        CHECK_EQ(sensors_pane_entry_of(want[i]), SENS_ENTRY_IMU);
        CHECK_EQ(settings_def(want[i])->cat, SET_CAT_SENSORS);
    }
    /* The defaults: OFF, X, +, 50Hz, 6-AXIS. */
    fresh_hub();
    CHECK(!settings_get_bool(SET_IMU_EN));
    CHECK_EQ(settings_get_int(SET_IMU_AXIS), 0);
    CHECK_EQ(settings_get_int(SET_IMU_SIGN), 0);
    CHECK_EQ(settings_get_int(SET_IMU_RATE), 2);
    CHECK_EQ(settings_get_int(SET_IMU_ALGO), 0);
    CHECK_EQ(imu_ctl_rate_code(settings_get_int(SET_IMU_RATE)), WIT_RATE_50HZ);
    CHECK_STR_EQ(settings_text(SET_TEXT_IMU_ADDR), "");
    CHECK_STR_EQ(settings_text(SET_TEXT_IMU_NAME), "");
    CHECK_STR_EQ(settings_def(SET_IMU_EN)->key, "imu_en");
    CHECK_STR_EQ(settings_def(SET_IMU_RATE)->key, "imu_rate");
    CHECK_EQ(settings_def(SET_IMU_RATE)->option_count, 5u);
    CHECK_EQ(settings_def(SET_IMU_AXIS)->option_count, 3u);
    /* The entry and each of its rows on the glass: ON's ">" brings them. */
    open_sensor(SET_IMU_EN);
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_IMU);
    CHECK_EQ(settings_screen_rows(SET_CAT_SENSORS), 6);
    setting_id_t ids[96];
    const int n = settings_in_category(SET_CAT_SENSORS, ids, 96);
    for (int i = 0; i < n; ++i) {
        if (sensors_pane_entry_of(ids[i]) == SENS_ENTRY_IMU) {
            CHECK(settings_screen_row_of(SET_CAT_SENSORS, i) >= 0);
        }
    }
    /* A SENSORS row past the old cap of 58 is still a row. */
    CHECK(n > 58);
    CHECK_EQ(n, 64);
}

TEST_CASE(the_state_column_reads_every_link_state_in_its_colour)
{
    fresh_hub();
    static const struct {
        imu_link_t l; const char *en, *de; gfx_color_t ink;
    } k[] = {
        { IMU_LINK_STARTING, "STARTING BLUETOOTH", "BLUETOOTH STARTET", 1 },
        { IMU_LINK_NO_BT, "NO BLUETOOTH ON C6", "KEIN BLUETOOTH AM C6", 0 },
        { IMU_LINK_C6_FAILED, "C6 FAILED: RESTART",
          "C6 AUSGEFALLEN: NEUSTART", 0 },
        { IMU_LINK_READY, "NO SENSOR BOUND", "KEIN SENSOR GEBUNDEN", 1 },
        { IMU_LINK_SCANNING, "SCANNING", "SUCHE LÄUFT", 1 },
        { IMU_LINK_CONNECTING, "CONNECTING", "VERBINDE", 1 },
        { IMU_LINK_BOUND, "BOUND", "GEBUNDEN", 1 },
        { IMU_LINK_CONNECTED, "CONNECTED 50Hz", "VERBUNDEN 50Hz", 2 },
        { IMU_LINK_NO_FRAMES, "NO FRAMES", "KEINE FRAMES", 0 },
    };
    char buf[48];
    gfx_color_t ink = 0;
    /* IMU OFF: OFF, dim, whatever the link said. */
    sensors_rows_set_imu(IMU_LINK_CONNECTED, 500u);
    CHECK_STR_EQ(sensors_pane_state(SENS_ENTRY_IMU, buf, sizeof buf, &ink),
                 "OFF");
    CHECK_EQ(ink, UI_TEXT_DIM);
    settings_set(SET_IMU_EN, 1.0f);
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        sensors_rows_set_imu(k[i].l, 498u);
        CHECK_EQ(sensors_rows_imu(), k[i].l);
        const gfx_color_t want = (k[i].ink == 0) ? UI_WARN
                                 : (k[i].ink == 1) ? UI_TEXT_DIM : UI_OK;
        ui_text_set_language(UI_LANG_EN);
        CHECK_STR_EQ(sensors_pane_state(SENS_ENTRY_IMU, buf, sizeof buf, &ink),
                     k[i].en);
        CHECK_EQ(ink, want);
        ui_text_set_language(UI_LANG_DE);
        CHECK_STR_EQ(sensors_pane_state(SENS_ENTRY_IMU, buf, sizeof buf, &ink),
                     k[i].de);
        CHECK(gfx_text_width(&gfx_font_8x16, buf, 1) <= 25 * 8);
    }
    ui_text_set_language(UI_LANG_EN);
    /* The rate in whole Hz: 200Hz, 0Hz; a state out of range reads OFF. */
    sensors_rows_set_imu(IMU_LINK_CONNECTED, 1995u);
    CHECK_STR_EQ(sensors_pane_state(SENS_ENTRY_IMU, buf, sizeof buf, &ink),
                 "CONNECTED 200Hz");
    sensors_rows_set_imu(IMU_LINK_CONNECTED, 4u);
    CHECK_STR_EQ(sensors_pane_state(SENS_ENTRY_IMU, buf, sizeof buf, &ink),
                 "CONNECTED 0Hz");
    CHECK_STR_EQ(sensors_rows_imu_word((imu_link_t)99, 0u, buf, sizeof buf),
                 "OFF");
    CHECK_STR_EQ(sensors_rows_imu_word(IMU_LINK_BOUND, 0u, NULL, 0u), "");
    CHECK_STR_EQ(sensors_rows_imu_state(NULL, 0u, NULL), "");
    sensors_rows_set_imu(IMU_LINK_OFF, 0u);
}

/* RATE while connected: the notice of the power cycle, and the controller
 * sends unlock, RATE, SAVE; while not connected the notice says it waits,
 * and it goes once the sensor connects.  ALGORITHM the same. */
TEST_CASE(a_rate_change_is_a_command_to_the_sensor_and_a_notice)
{
    fresh_hub();
    CHECK(sensors_rows_imu_notice(SET_IMU_RATE) == NULL);   /* IMU OFF */
    settings_set(SET_IMU_EN, 1.0f);
    CHECK(sensors_rows_imu_notice(SET_IMU_AXIS) == NULL);
    sensors_rows_set_imu(IMU_LINK_CONNECTED, 500u);
    CHECK_STR_EQ(sensors_rows_imu_notice(SET_IMU_RATE),
                 "RATE SAVED: SWITCH THE SENSOR OFF AND ON");
    CHECK_STR_EQ(sensors_rows_imu_notice(SET_IMU_ALGO),
                 "ALGORITHM SAVED IN THE SENSOR");
    /* In a calibration they wait for its end, and say so. */
    sensors_rows_set_imu_cal(true);
    CHECK_STR_EQ(sensors_rows_imu_notice(SET_IMU_RATE),
                 "RATE IS SENT AFTER CALIBRATION; THEN SWITCH THE SENSOR "
                 "OFF AND ON");
    CHECK_STR_EQ(sensors_rows_imu_notice(SET_IMU_ALGO),
                 "ALGORITHM IS SENT AFTER CALIBRATION");
    sensors_rows_set_imu_cal(false);
    sensors_rows_set_imu(IMU_LINK_NO_FRAMES, 0u);
    CHECK_STR_EQ(sensors_rows_imu_notice(SET_IMU_RATE),
                 "RATE IS SENT WHEN THE SENSOR CONNECTS; THEN SWITCH IT "
                 "OFF AND ON");
    CHECK_STR_EQ(sensors_rows_imu_notice(SET_IMU_ALGO),
                 "ALGORITHM IS SENT WHEN THE SENSOR CONNECTS");
    /* Each notice fits the alert band's two rows, in both languages. */
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        static const ui_text_id_t k[] = { TX_IMU_RATE_SAVED, TX_IMU_RATE_LATER,
                                          TX_IMU_ALGO_SAVED,
                                          TX_IMU_ALGO_LATER,
                                          TX_IMU_RATE_AFTER_CAL,
                                          TX_IMU_ALGO_AFTER_CAL };
        for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
            CHECK(ui_router_alert_rows(ui_tr(k[i])) <= 2);
        }
    }
    ui_text_set_language(UI_LANG_EN);
    sensors_rows_set_imu(IMU_LINK_OFF, 0u);
    /* The command: SETUP's RATE 100Hz to a connected controller. */
    rig_t r;
    rig_init(&r);
    rig_cfg(&r, true, RIG_ADDR, settings_get_int(SET_IMU_RATE), true);
    (void)rig_settle(&r, 2000u);
    rig_stream(&r, 100u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.nsent, 0u);
    settings_set(SET_IMU_RATE, 3.0f);
    rig_cfg(&r, true, RIG_ADDR, settings_get_int(SET_IMU_RATE), true);
    rig_stream(&r, 300u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.nsent, 3u);
    CHECK(rig_sent_unlock(&r, 0u));
    CHECK(rig_sent_is(&r, 1u, WIT_REG_RATE, WIT_RATE_100HZ, 0u));
    CHECK(rig_sent_is(&r, 2u, WIT_REG_SAVE, 0u, 0u));
}

int main(void)
{
    RUN(the_imu_entry_shows_its_five_rows);
    RUN(the_state_column_reads_every_link_state_in_its_colour);
    RUN(a_rate_change_is_a_command_to_the_sensor_and_a_notice);
    return test_summary("imu_pane");
}
