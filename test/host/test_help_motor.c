/**
 * @file test_help_motor.c
 * @brief MOTOR & ESC's screen guide: LIVE's and PROGRAM's page and the
 * rail's, the OPTIONS list's two, ESC STICK's over its session, none over
 * the signal-wire session and none off SAFE; every page fits in both
 * languages and themes, SAFE and ARMED, and no dock covers DISARM.
 *
 * Driven through the router, in panel coordinates, as the panel drives it.
 *
 * Cases:
 * - every_page_of_motor_fits
 * - live_marks_the_column_then_the_rail_with_target_in_view
 * - program_marks_each_row_with_its_own_text
 * - armed_or_an_arm_asked_leaves_no_page
 * - options_offers_the_list_pages
 * - the_stick_session_gives_its_own_pages_and_the_wire_session_none
 *
 * SPDX-License-Identifier: MIT
 */

#include "nav_rig.h"

#include "esc_wire_panel.h"
#include "help_check.h"
#include "link_pages.h"
#include "prog_registry.h"
#include "ui_help.h"

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

/** @brief A body y in panel coordinates. */
#define P(y) (UI_BAND_H + (y))

/** @brief MOTOR & ESC on LIVE, SAFE, DSHOT300 on GPIO 0 and GPIO 1. */
static void pose_live(void)
{
    on_screen(SCREEN_MOTOR);
    motor_screen_set_armed(false);
    motor_screen_set_throttle_pins(3u, true);
    motor_screen_set_throttle_protocol(4u);
    esc_stick_panel_set_throttle(true, 1u);
    drain_motor();
}

/** @brief As pose_live(), on PROGRAM's list. */
static void pose_program(void)
{
    pose_live();
    tap(MO_TAB_X(1), P(MO_TAB_Y));
}

/** @brief As pose_live(), the OPTIONS list open. */
static void pose_options(void)
{
    pose_live();
    tap(MO_SETTINGS_X, P(MO_SETTINGS_Y));
}

/** @brief As pose_program(), ESC STICK open on its makers. */
static void pose_stick(void)
{
    pose_program();
    tap(MO_PROG_ROW_X, P(MO_PROG_ROW_Y));
}

/**
 * @brief Whether @p p marks an item with @p text.
 * @param p    The page.
 * @param text The text.
 * @return True when one of its items has it.
 */
static bool marks(const ui_help_page_t *p, ui_text_id_t text)
{
    for (int i = 0; i < p->count; ++i) {
        if (p->item[i].text == text) {
            return true;
        }
    }
    return false;
}

TEST_CASE(every_page_of_motor_fits)
{
    CHECK_EQ(check_every_state(motor_screen(), pose_live, "live"), 2);
    CHECK_EQ(check_every_state(motor_screen(), pose_program, "program"), 2);
    CHECK_EQ(check_every_state(motor_screen(), pose_options, "options"), 2);
    CHECK_EQ(check_every_state(motor_screen(), pose_stick, "stick"), 2);
}

TEST_CASE(live_marks_the_column_then_the_rail_with_target_in_view)
{
    ui_help_page_t p;
    pose_live();
    REQUIRE(ui_help_count(motor_screen(), false) == 2);
    REQUIRE(ui_help_page(motor_screen(), false, 0, &p));
    CHECK_EQ(p.title, TX_HG_MO_TITLE_LIVE);
    CHECK_EQ(p.side, UI_HELP_DOCK_RIGHT);
    CHECK_EQ(p.count, 4);
    CHECK(marks(&p, TX_HG_MO_TABS));
    CHECK(marks(&p, TX_HG_MO_PLOT));
    CHECK(marks(&p, TX_HG_MO_THROTTLE));
    CHECK(marks(&p, TX_HG_MO_LOG));
    REQUIRE(ui_help_page(motor_screen(), false, 1, &p));
    CHECK_EQ(p.title, TX_HG_MO_TITLE_CTL);
    CHECK_EQ(p.side, UI_HELP_DOCK_LEFT);
    CHECK_EQ(p.count, 5);
    CHECK(marks(&p, TX_HG_MO_TARGET));
    CHECK(marks(&p, TX_HG_MO_ARM));
    CHECK(marks(&p, TX_HG_MO_OPTIONS));
    /* TARGET, the row that names what ARM drives, stays clear of the
     * dock. */
    REQUIRE(p.must_show.w > 0);
    gfx_rect_t m = p.must_show;
    m.y = (int16_t)(m.y + UI_BAND_H);
    CHECK(!near(m, ui_help_dock_rect(p.side), 0));
    /* The page opens through "?" and the router shows it. */
    tap(ui_band_guide_rect().x + 4, ui_band_guide_rect().y + 4);
    CHECK(ui_router_guide_open());
}

TEST_CASE(program_marks_each_row_with_its_own_text)
{
    ui_help_page_t p;
    pose_program();
    REQUIRE(motor_screen_task() == (int)UI_WB_PROGRAM);
    REQUIRE(ui_help_page(motor_screen(), false, 0, &p));
    CHECK_EQ(p.title, TX_HG_MO_TITLE_PROG);
    CHECK_EQ(p.count, 4);
    CHECK(marks(&p, TX_HG_MO_STICK));
    CHECK(marks(&p, TX_HG_MO_WIRE));
    CHECK_STR_EQ(p.item[1].label, TR(PL_STICK));
    CHECK_STR_EQ(p.item[2].label, TR(PL_ESC_WIRE));
    REQUIRE(ui_help_page(motor_screen(), false, 1, &p));
    CHECK_EQ(p.title, TX_HG_MO_TITLE_CTL);
}

TEST_CASE(armed_or_an_arm_asked_leaves_no_page)
{
    /* ARMED: either dock covers part of DISARM. */
    pose_live();
    motor_screen_set_armed(true);
    CHECK_EQ(ui_help_count(motor_screen(), false), 0);
    /* The hold done, the bench's answer not yet in. */
    pose_live();
    touch(MO_ARM_X, P(MO_ARM_Y), TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 60; ++i) {
        ui_router_tick(0.05f);
    }
    touch(MO_ARM_X, P(MO_ARM_Y), TOUCH_EVENT_UP, 1);
    motor_cmd_t c;
    bool armed_asked = false;
    while (motor_posted(&c)) {
        armed_asked = armed_asked || c.kind == MOTOR_CMD_ARM;
    }
    REQUIRE(armed_asked);
    CHECK_EQ(ui_help_count(motor_screen(), false), 0);
    /* SAFE again: both pages. */
    motor_screen_set_armed(true);
    motor_screen_set_armed(false);
    ui_router_tick(0.05f);
    CHECK_EQ(ui_help_count(motor_screen(), false), 2);
}

TEST_CASE(options_offers_the_list_pages)
{
    ui_help_page_t p;
    pose_options();
    REQUIRE(motor_screen()->covered());
    REQUIRE(ui_help_count(motor_screen(), false) == 2);
    REQUIRE(ui_help_page(motor_screen(), false, 0, &p));
    CHECK_EQ(p.title, TX_HG_SL_TITLE_ROWS);
    REQUIRE(ui_help_page(motor_screen(), false, 1, &p));
    CHECK_EQ(p.title, TX_HG_SL_TITLE_VALUES);
}

TEST_CASE(the_stick_session_gives_its_own_pages_and_the_wire_session_none)
{
    ui_help_page_t p;
    pose_stick();
    REQUIRE(motor_screen_session_shown());
    REQUIRE(ui_help_page(motor_screen(), false, 0, &p));
    CHECK_EQ(p.title, TX_HG_ES_TITLE_LIST);
    REQUIRE(ui_help_page(motor_screen(), false, 1, &p));
    CHECK_EQ(p.title, TX_HG_ES_TITLE_FIND);
    /* The list's pages cover no stop: they show while ARMED. */
    CHECK_EQ(ui_help_count(motor_screen(), true), 2);

    /* The signal-wire session, live with the IOmcu's PROGRAM. */
    pose_program();
    ui_bench_status_t st = *ui_router_status();
    st.capabilities = LINK_CAP_PROGRAM;
    ui_router_set_status(&st);
    tap(MO_PROG_ROW_X, P(MO_PROG_ROW_Y + 56));
    REQUIRE(motor_screen_session_shown());
    CHECK_EQ(ui_help_count(motor_screen(), false), 0);
    CHECK(!ui_help_page(motor_screen(), false, 0, &p));
}

int main(void)
{
    RUN(every_page_of_motor_fits);
    RUN(live_marks_the_column_then_the_rail_with_target_in_view);
    RUN(program_marks_each_row_with_its_own_text);
    RUN(armed_or_an_arm_asked_leaves_no_page);
    RUN(options_offers_the_list_pages);
    RUN(the_stick_session_gives_its_own_pages_and_the_wire_session_none);
    return test_summary("help_motor");
}
