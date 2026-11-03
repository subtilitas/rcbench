/*
 * SETUP > SENSORS on the rbmod carrier: the INA3221 it solders at 0x40
 * (ui_board.c, settings_sensors.c, sensors_rows.c, log_cols.c).
 *
 * Under test: the board holds 0x40 FITTED at ON and its three WATCHES rows
 * at SERVO RAIL B, SERVO RAIL A and PROG PORTS 5V, without keys and with
 * the soldered line, and lets every row go on the module; the stored
 * values come back; the rails are options no step and no stored word
 * reaches; the channel read at 1kHz is the rail of the lowest SERVO PWM
 * pin the tester drives, or CH3 for a programming port; the screens, the
 * limit alert, the WATCHED BY line and the log columns name the rails.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

#include "log_cols.h"
#include "sense_limit_link.h"
#include "sensors_rows.h"
#include "settings_sensors.h"
#include "ui_board.h"

#define RBMOD  ((uint16_t)OUTBIND_BOARD_RBMOD)
#define MODULE ((uint16_t)OUTBIND_BOARD_PICO_HEADER)
#define GP(n)  ((uint64_t)1u << (n))

/* Each setting the apply announced, counted. */
static int s_moved[SETTING_COUNT];

static void moved(setting_id_t id)
{
    if (id >= 0 && id < SETTING_COUNT) {
        ++s_moved[id];
    }
}

/* The model at its defaults on @p board, its rows applied, no tester
 * rail. */
static void on_board(uint16_t board)
{
    fresh_model();
    ui_board_set(MODULE);
    (void)ui_board_apply_pins(NULL);
    memset(s_moved, 0, sizeof(s_moved));
    ui_board_set(board);
    (void)ui_board_apply_pins(moved);
    (void)settings_sensors_set_tester_rail(LINK_SM_WATCH_NONE);
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(the_carrier_holds_its_part_fitted_and_its_rails)
{
    on_board(MODULE);
    /* The operator's own set-up, which the carrier covers and keeps. */
    settings_set(SET_INA_FIT(0), 0.0f);
    settings_set(SET_INA_W(0), (float)(SENS_W_OUT_1 + 2));
    settings_set(SET_INA_W(2), (float)SENS_W_SUPPLY);
    settings_set(SET_INA_FIT(1), 1.0f);
    settings_set(SET_INA_W(3), (float)SENS_W_PORT_1);
    memset(s_moved, 0, sizeof(s_moved));

    ui_board_set(RBMOD);
    CHECK(ui_board_apply_pins(moved));
    CHECK(settings_fixed(SET_INA_FIT(0)));
    CHECK(settings_get_bool(SET_INA_FIT(0)));
    CHECK_EQ(settings_get_int(SET_INA_W(0)), SENS_W_RAIL_B);
    CHECK_EQ(settings_get_int(SET_INA_W(1)), SENS_W_RAIL_A);
    CHECK_EQ(settings_get_int(SET_INA_W(2)), SENS_W_PROG_5V);
    for (int c = 0; c < 3; ++c) {
        CHECK(settings_fixed(SET_INA_W(c)));
        CHECK_EQ(s_moved[SET_INA_W(c)], 1);
        /* Shunt and limit stay the operator's. */
        CHECK(!settings_fixed(SET_INA_SH(c)));
        CHECK(!settings_fixed(SET_INA_LIM(c)));
    }
    CHECK_EQ(s_moved[SET_INA_FIT(0)], 1);
    /* The other parts are the operator's, on the sensor bus's J5. */
    for (int a = 1; a < SET_INA_PARTS; ++a) {
        CHECK(!settings_fixed(SET_INA_FIT(a)));
    }
    CHECK(settings_get_bool(SET_INA_FIT(1)));
    CHECK(!settings_fixed(SET_INA_W(3)));
    CHECK_EQ(settings_get_int(SET_INA_W(3)), SENS_W_PORT_1);
    /* The map the panel writes: 0x40 and 0x41, the board's codes. */
    settings_sensors_map_t m;
    settings_sensors_map(&m);
    CHECK_EQ(m.parts, 0x03u);
    CHECK_EQ(m.regs[LINK_SM_WATCH(0)], LINK_SM_WATCH_RAIL_B);
    CHECK_EQ(m.regs[LINK_SM_WATCH(1)], LINK_SM_WATCH_RAIL_A);
    CHECK_EQ(m.regs[LINK_SM_WATCH(2)], LINK_SM_WATCH_PROG_5V);
    CHECK_EQ(m.regs[LINK_SM_WATCH(3)], LINK_SM_WATCH_PORT(0));

    /* A step on a held row moves nothing the screen reads. */
    settings_adjust(SET_INA_W(0), 1);
    settings_set(SET_INA_FIT(0), 0.0f);
    CHECK_EQ(settings_get_int(SET_INA_W(0)), SENS_W_RAIL_B);
    CHECK(settings_get_bool(SET_INA_FIT(0)));

    /* Back on the module: the stored rows again, each one announced. */
    ui_board_set(MODULE);
    CHECK(ui_board_apply_pins(moved));
    CHECK(!settings_fixed(SET_INA_FIT(0)));
    CHECK(!settings_get_bool(SET_INA_FIT(0)));
    CHECK_EQ(s_moved[SET_INA_FIT(0)], 2);
    for (int c = 0; c < 3; ++c) {
        CHECK(!settings_fixed(SET_INA_W(c)));
        CHECK_EQ(s_moved[SET_INA_W(c)], 2);
    }
    /* The step taken while held landed on the stored value. */
    CHECK_EQ(settings_get_int(SET_INA_W(0)), SENS_W_OUT_1 + 3);
    CHECK_EQ(settings_get_int(SET_INA_W(2)), SENS_W_SUPPLY);
}

TEST_CASE(the_rails_are_shown_and_never_stepped_to)
{
    on_board(MODULE);
    /* SUPPLY, the last option a step reaches, goes round to NONE. */
    settings_set(SET_INA_W(4), (float)SENS_W_SUPPLY);
    settings_adjust(SET_INA_W(4), 1);
    CHECK_EQ(settings_get_int(SET_INA_W(4)), SENS_W_NONE);
    settings_adjust(SET_INA_W(4), -1);
    CHECK_EQ(settings_get_int(SET_INA_W(4)), SENS_W_SUPPLY);
    settings_adjust(SET_INA_W(4), SENS_W_OFFERED);
    CHECK_EQ(settings_get_int(SET_INA_W(4)), SENS_W_SUPPLY);
    /* A set to a rail is a stored word out of the list: the default. */
    settings_set(SET_INA_W(4), (float)SENS_W_RAIL_A);
    CHECK_EQ(settings_get_int(SET_INA_W(4)), SENS_W_NONE);
    settings_set(SET_INA_W(4), (float)(SENS_W_OFFERED - 1));
    CHECK_EQ(settings_get_int(SET_INA_W(4)), SENS_W_SUPPLY);
    CHECK_EQ(settings_sensors_offered(settings_def(SET_INA_W(4))),
             SENS_W_OFFERED);
    CHECK_EQ(settings_sensors_offered(settings_def(SET_INA3221_ADDR)), 4);
    CHECK_EQ(settings_sensors_offered(NULL), 1);
    /* Each rail's option is its page code. */
    CHECK_EQ(settings_sensors_watch_code(SENS_W_RAIL_A), LINK_SM_WATCH_RAIL_A);
    CHECK_EQ(settings_sensors_watch_code(SENS_W_RAIL_B), LINK_SM_WATCH_RAIL_B);
    CHECK_EQ(settings_sensors_watch_code(SENS_W_PROG_5V),
             LINK_SM_WATCH_PROG_5V);
    CHECK_EQ(settings_sensors_watch_option(LINK_SM_WATCH_PROG_5V),
             SENS_W_PROG_5V);

    /* On the carrier the rows read the rails, in both languages, and say
     * they are soldered. */
    on_board(RBMOD);
    char v[32];
    CHECK_STR_EQ(ui_setting_value(SET_INA_W(0), v, sizeof(v)),
                 "SERVO RAIL B");
    CHECK_STR_EQ(ui_setting_value(SET_INA_W(1), v, sizeof(v)),
                 "SERVO RAIL A");
    CHECK_STR_EQ(ui_setting_value(SET_INA_W(2), v, sizeof(v)),
                 "PROG PORTS 5V");
    CHECK_STR_EQ(settings_screen_help(SET_INA_W(0)),
                 "Soldered on this board. Not a setting here.");
    CHECK_STR_EQ(settings_screen_help(SET_INA_FIT(0)),
                 "Soldered on this board. Not a setting here.");
    CHECK(strcmp(settings_screen_help(SET_INA_SH(0)),
                 "Soldered on this board. Not a setting here.") != 0);
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(ui_setting_value(SET_INA_W(0), v, sizeof(v)),
                 "SCHIENE B");
    CHECK_STR_EQ(ui_setting_value(SET_INA_W(2), v, sizeof(v)),
                 "PROG PORTS 5V");
    ui_text_set_language(UI_LANG_EN);
    on_board(MODULE);
}

TEST_CASE(the_tester_reads_the_rail_of_its_lowest_pin)
{
    on_board(MODULE);
    /* No rail on the module, whatever the pins. */
    CHECK_EQ(ui_board_tester_rail(GP(2)), LINK_SM_WATCH_NONE);
    on_board(RBMOD);
    /* PWM04 GP1 and PWM01 GP2 are rail A, PWM02 GP4 and PWM03 GP5 rail B. */
    CHECK_EQ(ui_board_tester_rail(GP(1)), LINK_SM_WATCH_RAIL_A);
    CHECK_EQ(ui_board_tester_rail(GP(2)), LINK_SM_WATCH_RAIL_A);
    CHECK_EQ(ui_board_tester_rail(GP(4)), LINK_SM_WATCH_RAIL_B);
    CHECK_EQ(ui_board_tester_rail(GP(5)), LINK_SM_WATCH_RAIL_B);
    /* Both rails: the lowest pin's. */
    CHECK_EQ(ui_board_tester_rail(GP(5) | GP(2)), LINK_SM_WATCH_RAIL_A);
    CHECK_EQ(ui_board_tester_rail(GP(5) | GP(4)), LINK_SM_WATCH_RAIL_B);
    /* No pin, a programming port's pin, a pin on no rail, past GPIO 31. */
    CHECK_EQ(ui_board_tester_rail(0u), LINK_SM_WATCH_NONE);
    CHECK_EQ(ui_board_tester_rail(GP(13)), LINK_SM_WATCH_NONE);
    CHECK_EQ(ui_board_tester_rail(GP(3)), LINK_SM_WATCH_NONE);
    CHECK_EQ(ui_board_tester_rail(GP(40)), LINK_SM_WATCH_NONE);

    /* FAST follows the rail; a new rail says so once. */
    CHECK_EQ(settings_sensors_fast(), LINK_SM_FAST_NONE);
    CHECK(settings_sensors_set_tester_rail(LINK_SM_WATCH_RAIL_A));
    CHECK(!settings_sensors_set_tester_rail(LINK_SM_WATCH_RAIL_A));
    CHECK_EQ(settings_sensors_fast(), 1u);                 /* CH2 */
    CHECK(settings_sensors_set_tester_rail(LINK_SM_WATCH_RAIL_B));
    CHECK_EQ(settings_sensors_fast(), 0u);                 /* CH1 */
    /* The ports' 5V is no tester rail. */
    CHECK(settings_sensors_set_tester_rail(LINK_SM_WATCH_PROG_5V));
    CHECK_EQ(settings_sensors_fast(), LINK_SM_FAST_NONE);
    CHECK(!settings_sensors_set_tester_rail(LINK_SM_WATCH_OUT(0)));
    /* A rail's channel off: nothing read at 1kHz. */
    (void)settings_sensors_set_tester_rail(LINK_SM_WATCH_RAIL_A);
    settings_set(SET_INA_SH(1), 0.0f);
    CHECK_EQ(settings_sensors_fast(), LINK_SM_FAST_NONE);
    settings_set(SET_INA_SH(1), 100.0f);
    /* A programming port: CH3, the ports' 5V, for any of the four. */
    for (int port = 1; port <= 4; ++port) {
        settings_set(SET_SERVO_TARGET, (float)port);
        CHECK_EQ(settings_sensors_fast(), 2u);
    }
    settings_set(SET_SERVO_TARGET, 0.0f);
    CHECK_EQ(settings_sensors_fast(), 1u);
    (void)settings_sensors_set_tester_rail(LINK_SM_WATCH_NONE);
    on_board(MODULE);
}

TEST_CASE(the_screens_and_the_logs_name_the_rails)
{
    on_board(RBMOD);
    char buf[32];
    CHECK_STR_EQ(sensors_rows_channel(0, buf, sizeof(buf)), "RAIL B");
    CHECK_STR_EQ(sensors_rows_channel(1, buf, sizeof(buf)), "RAIL A");
    CHECK_STR_EQ(sensors_rows_channel(2, buf, sizeof(buf)), "PROG 5V");
    CHECK_STR_EQ(sensors_rows_channel(3, buf, sizeof(buf)), "0x41 CH1");
    (void)settings_sensors_set_tester_rail(LINK_SM_WATCH_RAIL_A);
    CHECK_STR_EQ(sensors_rows_meter(buf, sizeof(buf)), "INA3221 RAIL A");
    CHECK_STR_EQ(sensors_rows_short(buf, sizeof(buf)), "INA RAIL A");
    /* The reports' 20 bytes hold the longest, in German. */
    ui_text_set_language(UI_LANG_DE);
    char meter[20];
    CHECK_STR_EQ(sensors_rows_meter(meter, sizeof(meter)),
                 "INA3221 SCHIENE A");
    char alert[UI_ALERT_MAX];
    CHECK_STR_EQ(sensors_rows_alert(alert, sizeof(alert),
                                    SENSE_LIMIT_LINK_EV_OVER, 1u, 800u,
                                    SENS_W_RAIL_A),
                 "INA3221 0x40 CH2 (SCHIENE A) \xc3\xbc"
                 "ber der Grenze von 0.80A.");
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(sensors_rows_alert(alert, sizeof(alert),
                                    SENSE_LIMIT_LINK_EV_OVER, 2u, 500u,
                                    SENS_W_PROG_5V),
                 "INA3221 0x40 CH3 (PROG PORTS 5V) over its 0.50A limit.");
    (void)settings_sensors_set_tester_rail(LINK_SM_WATCH_NONE);
    CHECK_STR_EQ(sensors_rows_meter(buf, sizeof(buf)), "INA3221");

    /* Every port is watched by CH3 on the carrier. */
    for (unsigned port = 0u; port < 4u; ++port) {
        CHECK_EQ(sensors_rows_watcher((uint16_t)LINK_SM_WATCH_PORT(port)), -1);
    }
    CHECK_EQ(sensors_rows_watcher(LINK_SM_WATCH_PROG_5V), 2);
    for (unsigned port = 0u; port < 4u; ++port) {
        CHECK_STR_EQ(sensors_rows_port_watch(port, buf, sizeof(buf)),
                     "INA3221 PROG 5V");
    }
    CHECK_STR_EQ(sensors_rows_port_watch(4u, buf, sizeof(buf)), "");
    CHECK_STR_EQ(sensors_rows_port_watch(0u, NULL, 0u), "");
    /* CH3 off: no port is watched. */
    settings_set(SET_INA_SH(2), 0.0f);
    CHECK_STR_EQ(sensors_rows_port_watch(0u, buf, sizeof(buf)), "");
    settings_set(SET_INA_SH(2), 100.0f);

    /* The log's columns, from the map the panel writes. */
    settings_sensors_map_t m;
    settings_sensors_map(&m);
    log_cols_t cols;
    log_cols_from_map(m.regs, &cols);
    CHECK_EQ(cols.n, 3u);
    CHECK_STR_EQ(cols.name[0], "rail b");
    CHECK_STR_EQ(cols.name[1], "rail a");
    CHECK_STR_EQ(cols.name[2], "prog 5v");
    on_board(MODULE);
}

int main(void)
{
    RUN(the_carrier_holds_its_part_fitted_and_its_rails);
    RUN(the_rails_are_shown_and_never_stepped_to);
    RUN(the_tester_reads_the_rail_of_its_lowest_pin);
    RUN(the_screens_and_the_logs_name_the_rails);
    return test_summary("sensors_carrier");
}
