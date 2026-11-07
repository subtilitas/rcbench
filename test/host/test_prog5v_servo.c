/**
 * @file test_prog5v_servo.c
 * @brief The ports' 5V on SERVO, which shows no PROG 5V box: KST's supply
 *        row switches it off or names the lock; the ON RUDDER AUTO's ARM +
 *        START asks (prog5v_box_ask_on()) is SERVO's until SERVO is left;
 *        the hub names it in its footer.  The box's hold, lock, slide and
 *        stop are SUPPLY's (test_prog5v_supply, test_prog5v_box); ARM's
 *        5V across SERVO's screens is test_servo_drive_ready's.
 *
 * Cases:
 * - kst_supply_row_switches_the_5v_off_or_says_why
 * - the_5v_rudder_autos_hold_asks_is_servos_and_goes_with_the_hub
 * - the_hub_names_prog_5v_on_in_its_footer
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

#include "out_bind.h"
#include "prog5v_box.h"
#include "servo_ready_int.h"
#include "ui_board.h"
#include "ui_footer.h"

enum { ST_OFF = 2, ST_ON = 3 };

/* SERVO fresh on a board that switches the ports' 5V, which reads
 * @p state with no port holding it. */
static void fresh_p5(uint8_t state)
{
    prog5v_box_reset();
    ui_text_set_language(UI_LANG_EN);
    prog5v_box_publish(state, 21u, (uint8_t)PROG5V_HELD_NONE);
    fresh();
    ui_router_set_alert(NULL);
}

static bool asked_off(void)
{
    prog5v_cmd_t c;
    return prog5v_box_poll_cmd(&c) && c.off && !c.on;
}

static bool asked_on(void)
{
    prog5v_cmd_t c;
    return prog5v_box_poll_cmd(&c) && c.on && !c.off && c.by == PROG5V_BY_SERVO;
}

/* KST's READY card row @p i: its middle tapped. */
static void tap_kst_row(int i)
{
    const gfx_rect_t r = ready_row_rect(i);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/*
 * No SERVO screen shows the PROG 5V box.  On the rbmod carrier KST's
 * supply row names PROG 5V: read ON it is a safe control and a tap asks
 * OFF; held by a port a tap says why and asks nothing; read OFF it reads
 * ON AT CONNECT and a tap switches nothing on.
 */
TEST_CASE(kst_supply_row_switches_the_5v_off_or_says_why)
{
    fresh_p5(ST_ON);
    ui_board_set(OUTBIND_BOARD_RBMOD);
    servo_screen_set_drive(1u, 13u, false);
    go(SERVO_FN_KST);
    scr->tick(0.02f);
    rdy_row_t r;
    CHECK(ready_row((int)PRR_SUPPLY, &r));
    CHECK_STR_EQ(r.label, "PROG 5V");
    CHECK_EQ(r.st, (uint8_t)RDY_LIVE);
    const gfx_rect_t a = ready_row_rect((int)PRR_SUPPLY);
    CHECK(scr->safe_at(a.x + a.w / 2, a.y + a.h / 2));
    while (prog5v_box_poll_cmd(NULL)) { }
    tap_kst_row((int)PRR_SUPPLY);
    CHECK(asked_off());
    /* PROG PORT 1 holds it: the lock's reason, no OFF. */
    prog5v_box_publish(ST_ON, 21u, 0u);
    scr->tick(0.02f);
    ui_router_set_alert(NULL);
    tap_kst_row((int)PRR_SUPPLY);
    CHECK(!prog5v_box_poll_cmd(NULL));
    CHECK(ui_router_alert() != NULL);
    if (ui_router_alert() != NULL) {
        CHECK_STR_EQ(ui_router_alert(), ui_tr(TX_P5_LOCKED));
    }
    /* Read OFF: amber, and a tap asks for nothing. */
    prog5v_box_publish(ST_OFF, 21u, (uint8_t)PROG5V_HELD_NONE);
    scr->tick(0.02f);
    CHECK(ready_row((int)PRR_SUPPLY, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK_STR_EQ(r.value, TR(PRR_AT_CONNECT));
    CHECK(!scr->safe_at(a.x + a.w / 2, a.y + a.h / 2));
    tap_kst_row((int)PRR_SUPPLY);
    CHECK(!prog5v_box_poll_cmd(NULL));
    ui_board_set(OUTBIND_BOARD_PICO_HEADER);
    prog5v_box_reset();
}

/* The ON ARM + START's sequence asks on RUDDER AUTO is SERVO's, as the
 * box's hold: it stays from RUDDER AUTO to the hub and LIVE, and goes off
 * when SERVO is left; an OFF asked in the same poll stands over it. */
TEST_CASE(the_5v_rudder_autos_hold_asks_is_servos_and_goes_with_the_hub)
{
    fresh_p5(ST_OFF);
    ui_router_goto(SCREEN_SERVO_RAUTO);
    scr = servo_fn_screen(SERVO_FN_RAUTO);
    prog5v_box_ask_on(PROG5V_BY_SERVO);
    CHECK(asked_on());
    prog5v_box_publish(ST_ON, 21u, (uint8_t)PROG5V_HELD_NONE);
    scr->tick(0.02f);
    static const ui_screen_id_t k_way[] = {
        SCREEN_SERVO, SCREEN_SERVO_LIVE, SCREEN_SERVO, SCREEN_SERVO_RAUTO,
    };
    for (size_t i = 0; i < sizeof(k_way) / sizeof(k_way[0]); ++i) {
        ui_router_goto(k_way[i]);
        CHECK_EQ(ui_router_current(), k_way[i]);
        CHECK(!prog5v_box_poll_cmd(NULL));
    }
    ui_router_goto(SCREEN_SERVO);
    ui_router_goto(SCREEN_OVERVIEW);
    CHECK(asked_off());
    /* An OFF and an ON in one poll: the OFF. */
    prog5v_box_ask_off(PROG5V_BY_SERVO);
    prog5v_box_ask_on(PROG5V_BY_SERVO);
    CHECK(asked_off());
}

/* The hub shows no PROG 5V box: while the ports' 5V reads ON, PROG 5V ON
 * stands in the footer's middle slot in the warning colour; OFF leaves the
 * slot empty. */
static int hold_slot_warn_pixels(void)
{
    const gfx_rect_t r = ui_footer_rect(UI_FOOTER_HOLD);
    int n = 0;
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            n += gfx_pixel_get(&cv, x, y) == ui_theme_color(UI_C_WARN);
        }
    }
    return n;
}

TEST_CASE(the_hub_names_prog_5v_on_in_its_footer)
{
    fresh_p5(ST_ON);
    go(SERVO_FN_HUB);
    scr->render(&cv, 0);
    CHECK(hold_slot_warn_pixels() > 100);
    prog5v_box_publish(ST_OFF, 21u, (uint8_t)PROG5V_HELD_NONE);
    scr->tick(0.02f);
    scr->render(&cv, 0);
    CHECK_EQ(hold_slot_warn_pixels(), 0);
}

int main(void)
{
    RUN(kst_supply_row_switches_the_5v_off_or_says_why);
    RUN(the_5v_rudder_autos_hold_asks_is_servos_and_goes_with_the_hub);
    RUN(the_hub_names_prog_5v_on_in_its_footer);
    return test_summary("prog5v_servo");
}
