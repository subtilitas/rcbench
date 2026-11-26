/**
 * @file test_scanner_pane.c
 * @brief SETUP > SENSORS > SCANNER (scanner_pane.h) and SETUP CODE
 *        (scanner_code.h): the rows ON and OFF, TEST's 30s window, TEST
 *        during a run, Last scan and Taken as, ACTIVE SERVO's NONE inside
 *        and outside a run, Check again, the entry's state word, the
 *        factory reset's 2s hold, and the code drawn dark on white.
 *
 * SPDX-License-Identifier: MIT
 */
#include "nav_rig.h"

#include "gm861.h"
#include "scan_flow.h"
#include "scanner_code.h"
#include "scanner_codes.h"
#include "scanner_pane.h"
#include "sensors_pane.h"
#include "servo_active.h"
#include "ui_text.h"
#include "ui_theme.h"

/** @brief A condition the rest of the case stands on: fail and return. */
#define REQUIRE(c)                                                            \
    do {                                                                      \
        if (!(c)) {                                                           \
            T_FAIL("required %s", #c);                                        \
            return;                                                           \
        }                                                                     \
    } while (0)

/** @brief An id the flow takes. */
#define ID_A "KST_X10-0000A"
/** @brief Another. */
#define ID_B "KST_X10-0000B"

/** @brief A case's start: the menu up, the scanner ON and READY, no servo,
 *         no line, a fresh clock. */
static uint32_t start(void)
{
    static uint32_t t = 3000000u;
    settings_set_store(NULL);
    settings_init();
    settings_set(SET_SCANNER_ENABLE, 1.0f);
    fresh();
    to_overview();
    scan_flow_reset();
    scanner_pane_reset();
    (void)servo_active_clear(false);
    ui_router_set_alert(NULL);
    t += 200000u;
    const scanner_view_t v = { .on = true, .state = GM861_READY,
                               .baud = 9600u, .bad_zone = GM861_NO_ZONE };
    scanner_pane_set_view(&v, t);
    return t;
}

/** @brief The clock moves to @p t. */
static void at(uint32_t t)
{
    scanner_pane_set_view(scanner_pane_view(), t);
}

/** @brief The row labelled @p label, NULL when not listed. */
static const ui_setlist_custom_t *row(ui_text_id_t label)
{
    const ui_setlist_custom_t *rows[SCANNER_ROWS_MAX] = { NULL };
    const int n = scanner_pane_rows(rows, SCANNER_ROWS_MAX);
    for (int i = 0; i < n; ++i) {
        if (rows[i]->label == label) {
            return rows[i];
        }
    }
    return NULL;
}

/** @brief A row's value. */
static const char *value(ui_text_id_t label)
{
    static char buf[64];
    buf[0] = '\0';
    const ui_setlist_custom_t *r = row(label);
    if (r != NULL && r->value_text != NULL) {
        r->value_text(buf, sizeof(buf));
    }
    return buf;
}

/** @brief The active id, "" for none. */
static const char *active(void)
{
    const char *id = servo_active_id();
    return id != NULL ? id : "";
}

/** @brief The bench ARMED on SERVO, or SAFE. */
static void armed_servo(bool on)
{
    ui_bench_status_t st = *ui_router_status();
    st.armed = on;
    st.armed_roles = on ? UI_ARMED_SERVO : 0u;
    ui_router_set_status(&st);
}

TEST_CASE(off_lists_the_line_on_typing_alone_and_on_lists_every_row)
{
    (void)start();
    settings_set(SET_SCANNER_ENABLE, 0.0f);
    const ui_setlist_custom_t *rows[SCANNER_ROWS_MAX] = { NULL };
    CHECK_EQ(scanner_pane_rows(rows, SCANNER_ROWS_MAX), 1);
    CHECK_EQ(rows[0]->label, TX_SCN_ROW_NOTE);
    CHECK_EQ(rows[0]->help, TX_SCN_ROW_NOTE_HELP);
    settings_set(SET_SCANNER_ENABLE, 1.0f);
    CHECK_EQ(scanner_pane_rows(NULL, 0), SCANNER_ROWS_MAX);
    CHECK(row(TX_SCN_ROW_NOTE) != NULL);
    CHECK(row(TX_SCN_ROW_RESET) != NULL);
    CHECK_EQ(sensors_pane_entry_of(SET_SCANNER_ENABLE), SENS_ENTRY_SCANNER);
    CHECK_EQ(sensors_pane_entry_of(SET_SCANNER_MODE), SENS_ENTRY_SCANNER);
}

TEST_CASE(not_connected_is_a_state_in_dim_and_raises_nothing)
{
    const uint32_t t = start();
    const scanner_view_t v = { .on = true, .state = GM861_NOT_CONNECTED,
                               .bad_zone = GM861_NO_ZONE };
    scanner_pane_set_view(&v, t);
    char buf[32];
    gfx_color_t ink = 0;
    CHECK_STR_EQ(scanner_pane_state(buf, sizeof(buf), &ink),
                 TR(SCN_ST_NOT_CONNECTED));
    CHECK_EQ(ink, UI_TEXT_DIM);
    CHECK(ui_router_alert() == NULL);
    CHECK_STR_EQ(value(TX_SCN_ROW_STATE), TR(SCN_ST_NOT_CONNECTED));
    const scanner_view_t r = { .on = true, .state = GM861_READY,
                               .baud = 57600u, .bad_zone = GM861_NO_ZONE };
    scanner_pane_set_view(&r, t);
    CHECK_STR_EQ(scanner_pane_state(buf, sizeof(buf), &ink), TR(SCN_ST_READY));
    CHECK_EQ(ink, UI_OK);
    CHECK_STR_EQ(value(TX_SCN_ROW_STATE), "READY 57600");
    const scanner_view_t z = { .on = true, .state = GM861_NO_ANSWER,
                               .bad_zone = 0x000Du };
    scanner_pane_set_view(&z, t);
    CHECK_STR_EQ(value(TX_SCN_ROW_STATE), "NO ANSWER 0x000D");
    settings_set(SET_SCANNER_ENABLE, 0.0f);
    CHECK_STR_EQ(scanner_pane_state(buf, sizeof(buf), &ink), TR(OFF));
    CHECK_EQ(ink, UI_TEXT_DIM);
}

/* TEST: a scan inside its 30s leaves the active servo; one at 30.1s sets
 * it.  The page shows the line and the verdict. */
TEST_CASE(a_scan_inside_test_changes_nothing_and_one_after_30s_does)
{
    const uint32_t t = start();
    CHECK_STR_EQ(value(TX_SCN_ROW_TEST), TR(SCN_V_TEST_GO));
    row(TX_SCN_ROW_TEST)->tap();
    CHECK(scanner_pane_testing(t));
    at(t + 1000u);
    CHECK_STR_EQ(value(TX_SCN_ROW_TEST), "SCAN A CODE 29s");
    at(t + 29999u);
    CHECK(scan_flow_line(ID_A, false, t + 29999u));
    CHECK_STR_EQ(active(), "");
    CHECK_EQ(scanner_pane_taken(), SCANNER_TAKEN_TEST_OK);
    CHECK_STR_EQ(scanner_pane_last(NULL), ID_A);
    CHECK_STR_EQ(value(TX_SCN_ROW_TAKEN), TR(SCN_TAKEN_TEST_OK));
    CHECK(scan_flow_line("HELLO", false, t + 29999u));
    CHECK_EQ(scanner_pane_taken(), SCANNER_TAKEN_TEST_NOT_ID);
    CHECK(scan_flow_line("X", true, t + 30000u));
    CHECK_EQ(scanner_pane_taken(), SCANNER_TAKEN_TEST_LONG);
    CHECK(ui_router_alert() == NULL);
    at(t + 30100u);
    CHECK(!scanner_pane_testing(t + 30100u));
    CHECK(scan_flow_line(ID_A, false, t + 30100u));
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_SET);
    CHECK_STR_EQ(active(), ID_A);
    CHECK_EQ(scanner_pane_taken(), SCANNER_TAKEN_SERVO);
    CHECK_STR_EQ(value(TX_SCN_ROW_TEST), TR(SCN_V_TEST_GO));
}

/* TEST during a run: the id is refused as the flow would refuse it, and
 * neither the run's servo nor anything else changes. */
TEST_CASE(test_during_a_run_shows_the_refusal_and_keeps_the_servo)
{
    const uint32_t t = start();
    CHECK(scan_flow_line(ID_A, false, t));
    REQUIRE(strcmp(active(), ID_A) == 0);
    armed_servo(true);
    row(TX_SCN_ROW_TEST)->tap();
    CHECK(scan_flow_line(ID_B, false, t + 5000u));
    CHECK_EQ(scanner_pane_taken(), SCANNER_TAKEN_TEST_RUN);
    CHECK_STR_EQ(active(), ID_A);
    armed_servo(false);
}

TEST_CASE(last_scan_names_what_each_line_did)
{
    const uint32_t t = start();
    CHECK_STR_EQ(value(TX_SCN_ROW_LAST), "--");
    CHECK(scan_flow_line("HELLO WORLD THIS IS LONG", false, t));
    CHECK_EQ(scanner_pane_taken(), SCANNER_TAKEN_NOT_ID);
    at(t + 4200u);
    CHECK_STR_EQ(value(TX_SCN_ROW_LAST), "HELLO WORLD T 4s");
    CHECK(scan_flow_line("Y", true, t + 5000u));
    CHECK_EQ(scanner_pane_taken(), SCANNER_TAKEN_LONG);
    CHECK(scan_flow_line(ID_A, false, t + 6000u));
    CHECK_EQ(scanner_pane_taken(), SCANNER_TAKEN_SERVO);
    armed_servo(true);
    CHECK(scan_flow_line(ID_B, false, t + 7000u));
    CHECK_EQ(scanner_pane_taken(), SCANNER_TAKEN_RUN);
    CHECK_STR_EQ(value(TX_SCN_ROW_TAKEN), TR(SCN_TAKEN_RUN));
    armed_servo(false);
}

/* ACTIVE SERVO > NONE: refused during a run, with its notice; outside one
 * it clears the servo. */
TEST_CASE(none_during_a_run_is_refused_and_clears_outside_one)
{
    const uint32_t t = start();
    CHECK(scan_flow_line(ID_A, false, t));
    const ui_setlist_custom_t *r = row(TX_SCN_ROW_ACTIVE);
    REQUIRE(r != NULL);
    CHECK_STR_EQ(value(TX_SCN_ROW_ACTIVE), ID_A);
    CHECK_EQ(r->choices(), 1);
    CHECK_STR_EQ(r->choice(0), TR(SCN_V_NO_SERVO));
    CHECK_EQ(r->chosen(), -1);
    armed_servo(true);
    ui_router_set_alert(NULL);
    r->pick(0);
    CHECK_STR_EQ(active(), ID_A);
    CHECK(ui_router_alert() != NULL
          && strcmp(ui_router_alert(), TR(SCAN_CLEAR_RUN)) == 0);
    armed_servo(false);
    r->pick(0);
    CHECK_STR_EQ(active(), "");
    CHECK_EQ(r->chosen(), 0);
    CHECK_STR_EQ(value(TX_SCN_ROW_ACTIVE), TR(SCN_V_NO_SERVO));
}

TEST_CASE(check_again_asks_the_panel_and_off_locks_it)
{
    (void)start();
    scanner_cmd_t c;
    CHECK(!scanner_pane_take(&c));
    row(TX_SCN_ROW_RECHECK)->tap();
    CHECK(scanner_pane_take(&c));
    CHECK_EQ(c, SCANNER_CMD_RECHECK);
    CHECK(row(TX_SCN_ROW_RECHECK)->locked() == NULL);
    settings_set(SET_SCANNER_ENABLE, 0.0f);
    settings_set(SET_SCANNER_ENABLE, 1.0f);
    for (int i = 0; i < 6; ++i) {
        scanner_pane_ask(SCANNER_CMD_RECHECK);
    }
    int n = 0;
    while (scanner_pane_take(&c)) {
        ++n;
    }
    CHECK_EQ(n, 4);
    CHECK(!scanner_pane_take(NULL));
}

/** @brief Hold the factory reset's button for @p s seconds, in frames of
 *         1/60s, and release. */
static void hold_reset(float s)
{
    const gfx_rect_t h = scanner_code_part(SCANNER_PART_HOLD);
    const int x = h.x + h.w / 2, y = UI_BAND_H + h.y + h.h / 2;
    touch(x, y, TOUCH_EVENT_DOWN, 1);
    const int frames = (int)(s * 60.0f + 0.5f);
    for (int i = 0; i < frames; ++i) {
        ui_router_tick(1.0f / 60.0f);
    }
    touch(x, y, TOUCH_EVENT_UP, 1);
    ui_router_tick(1.0f / 60.0f);
}

/* FACTORY RESET released at 1.9s asks nothing and shows the hint; held
 * past 2s it asks the panel once. */
TEST_CASE(factory_reset_released_at_1_9s_does_nothing)
{
    (void)start();
    ui_router_goto(SCREEN_SETUP);
    row(TX_SCN_ROW_RESET)->tap();
    REQUIRE(ui_router_current() == SCREEN_SCAN_CODE);
    CHECK(scanner_code_hold_ready());
    scanner_cmd_t c;
    hold_reset(1.9f);
    CHECK(!scanner_pane_take(&c));
    CHECK(ui_router_alert() != NULL
          && strcmp(ui_router_alert(), TR(HOLD_HINT_SCN_RESET)) == 0);
    hold_reset(2.1f);
    CHECK(scanner_pane_take(&c));
    CHECK_EQ(c, SCANNER_CMD_RESET);
    CHECK(!scanner_pane_take(&c));
    /* OFF: the hold takes no press. */
    settings_set(SET_SCANNER_ENABLE, 0.0f);
    CHECK(!scanner_code_hold_ready());
    hold_reset(2.5f);
    CHECK(!scanner_pane_take(&c));
    /* BACK returns to SETUP. */
    const gfx_rect_t b = scanner_code_part(SCANNER_PART_BACK);
    tap(b.x + b.w / 2, UI_BAND_H + b.y + b.h / 2);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
}

/* SHOW SETUP CODE draws the matrix at 12px a module, dark on white, with
 * 48px of white around it. */
TEST_CASE(the_code_is_drawn_dark_on_white_at_12px_a_module)
{
    (void)start();
    ui_router_goto(SCREEN_SETUP);
    row(TX_SCN_ROW_CODE)->tap();
    REQUIRE(ui_router_current() == SCREEN_SCAN_CODE);
    ui_router_tick(1.0f / 60.0f);
    ui_router_render(&cv, 0);
    const gfx_rect_t sq = scanner_code_part(SCANNER_PART_CODE);
    CHECK_EQ(sq.w, 252 + 2 * SCANNER_CODE_QUIET);
    const int x0 = sq.x + SCANNER_CODE_QUIET;
    const int y0 = UI_BAND_H + sq.y + SCANNER_CODE_QUIET;
    int wrong = 0;
    for (int y = 0; y < (int)SCANNER_CODE_MODULES; ++y) {
        for (int x = 0; x < (int)SCANNER_CODE_MODULES; ++x) {
            const gfx_color_t px = fb[(size_t)(y0 + y * SCANNER_CODE_PX + 6) * W
                                      + (size_t)(x0 + x * SCANNER_CODE_PX + 6)];
            const gfx_color_t want =
                scanner_code_dark(scanner_code_serial, x, y) ? GFX_BLACK
                                                             : GFX_WHITE;
            wrong += px != want;
        }
    }
    CHECK_EQ(wrong, 0);
    CHECK_EQ(fb[(size_t)(UI_BAND_H + sq.y + 4) * W + (size_t)(sq.x + 4)],
             GFX_WHITE);
}

int main(void)
{
    RUN(off_lists_the_line_on_typing_alone_and_on_lists_every_row);
    RUN(not_connected_is_a_state_in_dim_and_raises_nothing);
    RUN(a_scan_inside_test_changes_nothing_and_one_after_30s_does);
    RUN(test_during_a_run_shows_the_refusal_and_keeps_the_servo);
    RUN(last_scan_names_what_each_line_did);
    RUN(none_during_a_run_is_refused_and_clears_outside_one);
    RUN(check_again_asks_the_panel_and_off_locks_it);
    RUN(factory_reset_released_at_1_9s_does_nothing);
    RUN(the_code_is_drawn_dark_on_white_at_12px_a_module);
    return test_summary("scanner_pane");
}
