/**
 * @file test_servo_port_button.c
 * @brief The PORT button over SERVO's output card: the choice of OUTPUTS
 * and PROG PORT 1 to 4 over the left card on every function screen but the
 * programmers', a pick as SET_SERVO_TARGET, and the TARGET lock that greys
 * it and names itself in the alert band.  The carrier's refusals and its
 * pick are test_prog_choice's.
 *
 * Cases:
 * - port_opens_outputs_and_four_ports_over_the_left_card
 * - the_hub_has_no_port_button
 * - a_pick_sets_the_target_asks_for_a_save_and_closes_the_list
 * - under_the_target_lock_port_opens_nothing_and_says_why
 * - a_lock_after_the_list_opened_takes_no_pick
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

#include "prog_choice.h"

/** @brief A tap on the PORT button. */
static void tap_port(void) { tap(SV_PORT_X, SV_PORT_Y); }

/** @brief A tap on entry @p i of the port list. */
static void tap_choice(int i)
{
    const gfx_rect_t r = servo_screen_port_choice_rect(i);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/** @brief Whether @p r lies inside @p a. */
static bool inside(gfx_rect_t r, gfx_rect_t a)
{
    return r.w > 0 && r.h > 0 && r.x >= a.x && r.y >= a.y
           && r.x + r.w <= a.x + a.w && r.y + r.h <= a.y + a.h;
}

/** @brief The alert band's text, "" for none. */
static const char *alert(void)
{
    const char *a = ui_router_alert();
    return a != NULL ? a : "";
}

static bool s_held;

static bool hold_target(setting_id_t id)
{
    return s_held && id == SET_SERVO_TARGET;
}

/* On every function screen with the output card, a tap on PORT opens the
 * list over the left card: OUTPUTS, then PROG PORT 1 to 4, none refused
 * on a board whose pins are chosen.  CANCEL's place closes it. */
TEST_CASE(port_opens_outputs_and_four_ports_over_the_left_card)
{
    static const servo_fn_t k_fn[] = {
        SERVO_FN_LIVE, SERVO_FN_SWEEP, SERVO_FN_TEST, SERVO_FN_LIMRUN,
        SERVO_FN_RUDDER, SERVO_FN_RAUTO,
    };
    for (size_t f = 0; f < sizeof(k_fn) / sizeof(k_fn[0]); ++f) {
        fresh();
        go(k_fn[f]);
        CHECK_EQ(servo_screen_port_choices(), -1);
        tap_port();
        CHECK_EQ(servo_screen_port_choices(), 1 + (int)LINK_PG_PORTS_MAX);
        bool refused = true;
        CHECK_STR_EQ(servo_screen_port_choice(0, &refused),
                     TR(OUT_TESTER_OUTPUTS));
        CHECK(!refused);
        for (int i = 1; i <= (int)LINK_PG_PORTS_MAX; ++i) {
            char want[32];
            snprintf(want, sizeof(want), TR(SV_TARGET_PN), (unsigned)i);
            refused = true;
            CHECK_STR_EQ(servo_screen_port_choice(i, &refused), want);
            CHECK(!refused);
        }
        for (int i = 0; i <= (int)LINK_PG_PORTS_MAX; ++i) {
            CHECK(inside(servo_screen_port_choice_rect(i), SV_OV_AREA));
        }
        CHECK_STR_EQ(servo_screen_port_choice(5, NULL), "");
        tap(CH_CANCEL_X, CH_CANCEL_Y);
        CHECK_EQ(servo_screen_port_choices(), -1);
        CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    }
}

/* The hub drives nothing and has no PORT button: a tap there opens no
 * list. */
TEST_CASE(the_hub_has_no_port_button)
{
    fresh();
    go(SERVO_FN_HUB);
    tap_port();
    CHECK_EQ(servo_screen_port_choices(), -1);
}

/* A pick is SET_SERVO_TARGET, asked to be saved, as OUTPUTS' SERVO TESTER
 * rows set it, and the list closes; the port line follows what the drive
 * path reports.  OUTPUTS picks 0. */
TEST_CASE(a_pick_sets_the_target_asks_for_a_save_and_closes_the_list)
{
    fresh();
    (void)settings_save();
    CHECK(!settings_save_asked());
    tap_port();
    tap_choice(2);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 2);
    CHECK(settings_save_asked());
    CHECK_EQ(servo_screen_port_choices(), -1);
    tap_port();
    tap_choice(0);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    CHECK_EQ(servo_screen_port_choices(), -1);
}

/* ARMED, ARM asked and a port a programmer holds lock the target: a tap
 * on PORT opens nothing and the alert band names the lock -- the hold by
 * its own line. */
TEST_CASE(under_the_target_lock_port_opens_nothing_and_says_why)
{
    /* ARMED. */
    fresh_armed();
    ui_router_set_alert(NULL);
    CHECK(prog_choice_locked());
    tap_port();
    CHECK_EQ(servo_screen_port_choices(), -1);
    CHECK(alert()[0] != '\0');
    CHECK_STR_EQ(alert(), TR(OUT_PORT_LOCKED));

    /* ARM held until it posts, not yet answered. */
    fresh();
    ui_router_set_alert(NULL);
    arm_press();
    hold_for(2.3f);
    arm_release();
    CHECK(servo_screen_arm_asked());
    tap_port();
    CHECK_EQ(servo_screen_port_choices(), -1);
    CHECK_STR_EQ(alert(), TR(OUT_PORT_LOCKED));
    servo_screen_cancel_arm();

    /* A programmer holds the port. */
    fresh();
    ui_router_set_alert(NULL);
    s_held = true;
    settings_set_hold(hold_target);
    CHECK(prog_choice_locked());
    tap_port();
    CHECK_EQ(servo_screen_port_choices(), -1);
    CHECK_STR_EQ(alert(), TR(OUT_PORT_HELD));
    s_held = false;
    settings_set_hold(NULL);
    CHECK(!prog_choice_locked());
    tap_port();
    CHECK_EQ(servo_screen_port_choices(), 1 + (int)LINK_PG_PORTS_MAX);
}

/* A lock that comes while the list is open: the pick is taken by nothing
 * and the target stays. */
TEST_CASE(a_lock_after_the_list_opened_takes_no_pick)
{
    fresh();
    tap_port();
    CHECK_EQ(servo_screen_port_choices(), 1 + (int)LINK_PG_PORTS_MAX);
    s_held = true;
    settings_set_hold(hold_target);
    tap_choice(3);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    s_held = false;
    settings_set_hold(NULL);
}

int main(void)
{
    RUN(port_opens_outputs_and_four_ports_over_the_left_card);
    RUN(the_hub_has_no_port_button);
    RUN(a_pick_sets_the_target_asks_for_a_save_and_closes_the_list);
    RUN(under_the_target_lock_port_opens_nothing_and_says_why);
    RUN(a_lock_after_the_list_opened_takes_no_pick);
    return test_summary("servo_port_button");
}
