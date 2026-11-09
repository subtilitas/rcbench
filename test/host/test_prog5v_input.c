/*
 * The rbmod carrier's input supply as the PD mini reads it
 * (prog5v_box_input_low_mv()): M7 makes the programming ports' 5V from the
 * +15V net, and an input under 13.5V leaves it short of 5V.
 *
 * Under test: low at 5.1V and 13.499V, not at 13.5V, 15.0V or 20.0V; no
 * warning without a PD mini reading or on a board without the PROG 5V
 * switch; the texts in EN and DE; the PROG 5V box's red line and its ON
 * refused with no hold and no command; KST's READY supply row red, and its
 * CONNECT refused with the session left in PWM; LIMIT RUN's and RUDDER
 * AUTO's supply rows red and their ARM + START refused with nothing
 * switched, RUDDER AUTO's ANGLE row still opening its list; SUPPLY's head
 * line.  An input that goes low after the press:
 * under the box's hold, under ARM + START's hold, during the steps up to
 * the read-back, and at a limit run's CONNECT.  KST's CONNECT as a 2s hold:
 * a tap shows the hint, a finger that leaves or a touch loss connects
 * nothing, a press refused at a low input takes no hold, an input gone low
 * under the hold ends it with the reason.  The rig is
 * servo_rauto_kst_rig.h.
 *
 * Cases:
 * - low_is_under_13v5_on_the_carrier_with_a_reading
 * - the_texts_name_the_input_in_en_and_de
 * - the_box_shows_the_input_and_refuses_its_on
 * - the_box_takes_its_on_at_15v_and_20v
 * - kst_supply_row_is_red_and_connect_is_refused
 * - rudder_auto_start_is_refused_with_nothing_switched
 * - limit_run_start_is_refused_with_nothing_switched
 * - no_warning_off_the_carrier
 * - supply_names_the_input_in_its_head_line
 * - an_input_gone_low_under_the_boxs_hold_posts_no_on
 * - an_input_gone_low_under_rudder_autos_hold_refuses_it
 * - an_input_gone_low_during_the_steps_ends_them_all_off
 * - a_limit_runs_connect_is_refused_while_the_input_is_low
 * - kst_connect_is_a_2s_hold
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rauto_kst_rig.h"
#include "servo_angle_pick.h"
#include "splash_screen.h"

enum { ST_OFF = 2, ST_ON = 3 };

/* The texts at 5.1V. */
#define WHY_EN "INPUT 5.1V: NEEDS 15V. CHECK THE SUPPLY VOLTAGE"
#define TAG_EN "INPUT 5.1V < 15V"

/** @brief The router on SUPPLY, a screen with the band: a refusal's notice
 *         shows only there (ui_action_refused()). */
static void band_live(void)
{
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_SUPPLY);
}

/** @brief The text the band shows, "" for none.  @return it */
static const char *band(void)
{
    return ui_router_alert() != NULL ? ui_router_alert() : "";
}

/** @brief Whether the boxes asked ON since the last poll.  @return then */
static bool asked_on_any(void)
{
    prog5v_cmd_t c;
    bool on = false;
    while (prog5v_box_poll_cmd(&c)) {
        on = on || c.on;
    }
    return on;
}

TEST_CASE(low_is_under_13v5_on_the_carrier_with_a_reading)
{
    prog5v_box_reset();
    /* A board without the switch: no box, no warning. */
    prog5v_box_publish_input(5100u);
    CHECK_EQ(prog5v_box_input_low_mv(), 0u);
    CHECK(prog5v_box_input_why() == NULL);
    /* The carrier. */
    prog5v_box_publish(ST_OFF, 21u, (uint8_t)PROG5V_HELD_NONE);
    CHECK_EQ(prog5v_box_input_low_mv(), 5100u);
    prog5v_box_publish_input(PROG5V_INPUT_MIN_MV - 1u);
    CHECK_EQ(prog5v_box_input_low_mv(), PROG5V_INPUT_MIN_MV - 1u);
    prog5v_box_publish_input(PROG5V_INPUT_MIN_MV);
    CHECK_EQ(prog5v_box_input_low_mv(), 0u);
    prog5v_box_publish_input(15000u);
    CHECK_EQ(prog5v_box_input_low_mv(), 0u);
    prog5v_box_publish_input(20000u);
    CHECK_EQ(prog5v_box_input_low_mv(), 0u);
    /* No PD mini reading. */
    prog5v_box_publish_input(0u);
    CHECK_EQ(prog5v_box_input_low_mv(), 0u);
    CHECK(prog5v_box_input_why() == NULL);
    CHECK(!prog5v_box_input_is(NULL));
    char t[24] = "x";
    CHECK_STR_EQ(prog5v_box_input_tag(t, sizeof(t)), "");
    CHECK_STR_EQ(prog5v_box_input_tag(NULL, 4u), "");
    prog5v_box_reset();
}

TEST_CASE(the_texts_name_the_input_in_en_and_de)
{
    char t[24];
    prog5v_box_reset();
    prog5v_box_publish(ST_OFF, 21u, (uint8_t)PROG5V_HELD_NONE);
    prog5v_box_publish_input(5100u);
    ui_text_set_language(UI_LANG_EN);
    const char *why = prog5v_box_input_why();
    CHECK(why != NULL && prog5v_box_input_is(why));
    CHECK_STR_EQ(why, WHY_EN);
    CHECK_STR_EQ(prog5v_box_input_tag(t, sizeof(t)), TAG_EN);
    char line[48];
    CHECK_STR_EQ(prog5v_box_text(1, line, sizeof(line)),
                 "INPUT 5.1V: NEEDS 15V");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(prog5v_box_input_why(),
                 "EINGANG 5.1V: BENÖTIGT 15V. VERSORGUNGSSPANNUNG PRÜFEN");
    CHECK_STR_EQ(prog5v_box_input_tag(t, sizeof(t)), "EINGANG 5.1V < 15V");
    CHECK_STR_EQ(prog5v_box_text(1, line, sizeof(line)),
                 "EINGANG 5.1V: BENÖTIGT 15V");
    /* Tenths rounded down: 13.499V is 13.4V, under the 13.5V. */
    prog5v_box_publish_input(13499u);
    CHECK_STR_EQ(prog5v_box_input_tag(t, sizeof(t)), "EINGANG 13.4V < 15V");
    ui_text_set_language(UI_LANG_EN);
    prog5v_box_reset();
}

TEST_CASE(the_box_shows_the_input_and_refuses_its_on)
{
    band_live();
    carrier_port(false);
    go(SERVO_FN_HUB);
    ui_router_set_alert(NULL);
    prog5v_box_publish_input(5100u);
    prog5v_box_t bx;
    prog5v_box_init(&bx, (gfx_rect_t){ 648, 160, 356, PROG5V_BOX_H },
                    PROG5V_BY_SUPPLY);
    const uint32_t rev = bx.rev;
    prog5v_box_tick(&bx, 0.02f);
    CHECK_EQ(bx.rev, rev);               /* low since its start */
    /* A press is taken and refused: no hold, the input named. */
    while (prog5v_box_poll_cmd(NULL)) { }
    CHECK(prog5v_box_press(&bx));
    CHECK(!bx.pressed);
    for (int i = 0; i < 150; ++i) {
        prog5v_box_tick(&bx, 0.02f);
    }
    CHECK(!asked_on_any());
    CHECK_STR_EQ(band(), WHY_EN);
    /* The input back to 15V: the line moves. */
    prog5v_box_publish_input(15000u);
    prog5v_box_tick(&bx, 0.02f);
    CHECK(bx.rev != rev);
    carrier_done();
}

TEST_CASE(the_box_takes_its_on_at_15v_and_20v)
{
    static const uint16_t k_in[] = { 15000u, 20000u, 0u };
    for (size_t i = 0; i < sizeof(k_in) / sizeof(k_in[0]); ++i) {
        prog5v_box_reset();
        prog5v_box_publish(ST_OFF, 21u, (uint8_t)PROG5V_HELD_NONE);
        prog5v_box_publish_input(k_in[i]);
        prog5v_box_t bx;
        prog5v_box_init(&bx, (gfx_rect_t){ 648, 160, 356, PROG5V_BOX_H },
                        PROG5V_BY_SUPPLY);
        CHECK(prog5v_box_press(&bx));
        CHECK(bx.pressed);
        for (int k = 0; k < 110; ++k) {
            prog5v_box_tick(&bx, 0.02f);
        }
        CHECK(asked_on_any());
        char line[48];
        CHECK(strstr(prog5v_box_text(1, line, sizeof(line)), "INPUT") == NULL);
    }
    prog5v_box_reset();
}

TEST_CASE(kst_supply_row_is_red_and_connect_is_refused)
{
    band_live();
    carrier_port(false);
    go(SERVO_FN_KST);
    ra_ms(100u);
    rdy_row_t r;
    CHECK(ready_row((int)PRR_SUPPLY, &r));
    CHECK_STR_EQ(r.label, "PROG 5V");
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    prog5v_box_publish_input(5100u);
    ra_ms(20u);
    CHECK(ready_row((int)PRR_SUPPLY, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    CHECK_STR_EQ(r.value, TAG_EN);
    CHECK(r.why != NULL);
    if (r.why != NULL) {
        CHECK_STR_EQ(r.why, WHY_EN);
    }
    /* CONNECT: refused, the session stays in PWM, the 5V not asked. */
    ui_router_set_alert(NULL);
    while (prog5v_box_poll_cmd(NULL)) { }
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    press_port();
    ra_ms(200u);
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK_STR_EQ(band(), WHY_EN);
    /* 15.0V: the row is amber again and CONNECT goes. */
    prog5v_box_publish_input(15000u);
    ra_ms(20u);
    CHECK(ready_row((int)PRR_SUPPLY, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    ui_router_set_alert(NULL);
    press_port();
    CHECK(kst_prog_state(M) != KST_PROG_PWM);
    CHECK(strcmp(band(), WHY_EN) != 0);
    carrier_done();
}

TEST_CASE(rudder_auto_start_is_refused_with_nothing_switched)
{
    carrier_safe_off(40u);
    CHECK(rauto_hold_blocked(NULL) == NULL);
    prog5v_box_publish_input(5100u);
    ra_ms(20u);
    rauto_row_t row = RAR_COUNT;
    const char *why = rauto_hold_blocked(&row);
    CHECK(prog5v_box_input_is(why));
    CHECK_EQ(row, RAR_SUPPLY);
    rdy_row_t r = row_now(RAR_SUPPLY);
    CHECK_STR_EQ(r.label, "PROG 5V");
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    CHECK_STR_EQ(r.value, TAG_EN);
    char b[128];
    CHECK(ready_refusal(b, sizeof(b)) != NULL);
    CHECK_STR_EQ(b, "PROG 5V: " WHY_EN);
    ra_hold_start();
    CHECK(!servo_screen_rauto_running());
    CHECK(!svs_seq_busy());
    CHECK_EQ(ra.p5_ons, 0u);
    CHECK_EQ(ra.arms, 0u);
    CHECK(!prog5v_box_on());
    /* 15.0V and 20.0V: nothing of the input refuses. */
    prog5v_box_publish_input(15000u);
    ra_ms(20u);
    CHECK(rauto_hold_blocked(NULL) == NULL);
    prog5v_box_publish_input(20000u);
    ra_ms(20u);
    CHECK(rauto_hold_blocked(NULL) == NULL);
    CHECK_EQ(row_now(RAR_SUPPLY).st, (uint8_t)RDY_SEQ);
    /* Low again: the ANGLE row's tap opens the ANGLE SOURCE list, switches
     * nothing, and SUPPLY's row keeps the reason. */
    prog5v_box_publish_input(5100u);
    ra_ms(20u);
    rauto_ready_tap((int)RAR_ANGLE);
    CHECK(angle_pick_count() >= 0);
    CHECK_EQ(ra.p5_ons, 0u);
    CHECK(!prog5v_box_on());
    CHECK_STR_EQ(row_now(RAR_SUPPLY).value, TAG_EN);
    ra.p5_task = false;
    carrier_done();
}

TEST_CASE(limit_run_start_is_refused_with_nothing_switched)
{
    carrier_safe_off(40u);
    go(SERVO_FN_LIMRUN);
    ra_ms(100u);
    CHECK(lim_hold_blocked(NULL) == NULL);
    prog5v_box_publish_input(PROG5V_INPUT_MIN_MV - 1u);
    ra_ms(20u);
    limrun_row_t row = LRR_COUNT;
    CHECK(prog5v_box_input_is(lim_hold_blocked(&row)));
    CHECK_EQ(row, LRR_SUPPLY);
    rdy_row_t r;
    CHECK(lim_ready_row((int)LRR_SUPPLY, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    CHECK_STR_EQ(r.value, "INPUT 13.4V < 15V");
    ev(SV_HOLD_X, SV_HOLD_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(2300u);
    ev(SV_HOLD_X, SV_HOLD_Y, TOUCH_EVENT_UP, 1);
    ra_ms(20u);
    CHECK(!servo_screen_limit_running());
    CHECK(!svs_seq_busy());
    CHECK_EQ(ra.p5_ons, 0u);
    CHECK_EQ(ra.arms, 0u);
    /* At 13.5V the input refuses nothing. */
    prog5v_box_publish_input(PROG5V_INPUT_MIN_MV);
    ra_ms(20u);
    CHECK(lim_hold_blocked(NULL) == NULL);
    ra.p5_task = false;
    carrier_done();
}

/* Off the carrier the same reading changes nothing: no box, no rows. */
TEST_CASE(no_warning_off_the_carrier)
{
    ra_setup(true, true);
    prog5v_box_publish_input(5100u);
    ra_ms(20u);
    CHECK_EQ(prog5v_box_input_low_mv(), 0u);
    const char *why = rauto_hold_blocked(NULL);
    CHECK(!prog5v_box_input_is(why));
    prog5v_box_reset();
}

TEST_CASE(supply_names_the_input_in_its_head_line)
{
    char line[80];
    prog5v_box_reset();
    supply_screen()->reset();
    supply_screen_set_model(false);
    supply_screen_set_pins(7, 6);
    prog5v_box_publish(ST_OFF, 21u, (uint8_t)PROG5V_HELD_NONE);
    supply_screen_header_text(line, sizeof(line));
    CHECK(strstr(line, "INPUT") == NULL);
    prog5v_box_publish_input(5100u);
    supply_screen_header_text(line, sizeof(line));
    CHECK_STR_EQ(line, "PD MINI TX GPIO 7 RX GPIO 6   " TAG_EN);
    prog5v_box_publish_input(15000u);
    supply_screen_header_text(line, sizeof(line));
    CHECK(strstr(line, "INPUT") == NULL);
    prog5v_box_reset();
}

/* PROG 5V ON held at 15V; 5.1V published before the hold completes:
 * the hold ends, no ON is posted, the band names the input.  The bench's
 * own ON (prog5v_box_ask_on()) asks nothing while the input is low. */
TEST_CASE(an_input_gone_low_under_the_boxs_hold_posts_no_on)
{
    band_live();
    prog5v_box_reset();
    ui_text_set_language(UI_LANG_EN);
    prog5v_box_publish(ST_OFF, 21u, (uint8_t)PROG5V_HELD_NONE);
    prog5v_box_publish_input(15000u);
    ui_router_set_alert(NULL);
    prog5v_box_t bx;
    prog5v_box_init(&bx, (gfx_rect_t){ 648, 160, 356, PROG5V_BOX_H },
                    PROG5V_BY_SUPPLY);
    while (prog5v_box_poll_cmd(NULL)) { }
    CHECK(prog5v_box_press(&bx));
    CHECK(bx.pressed);
    for (int k = 0; k < 50; ++k) {           /* 1000ms of the 2000ms */
        prog5v_box_tick(&bx, 0.02f);
    }
    prog5v_box_publish_input(5100u);
    for (int k = 0; k < 100; ++k) {
        prog5v_box_tick(&bx, 0.02f);
    }
    CHECK(!bx.pressed);
    CHECK(!asked_on_any());
    CHECK_STR_EQ(band(), WHY_EN);
    prog5v_box_ask_on(PROG5V_BY_SERVO);
    CHECK(!asked_on_any());
    prog5v_box_publish_input(15000u);
    prog5v_box_ask_on(PROG5V_BY_SERVO);
    CHECK(asked_on_any());
    prog5v_box_reset();
}

/* ARM + START held at 15V; 5.1V published before it fires: the gate at
 * the fire refuses it, nothing is switched. */
TEST_CASE(an_input_gone_low_under_rudder_autos_hold_refuses_it)
{
    carrier_safe_off(40u);
    CHECK(rauto_hold_blocked(NULL) == NULL);
    ui_router_set_alert(NULL);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(1000u);
    prog5v_box_publish_input(5100u);
    ra_ms(1300u);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    ra_ms(20u);
    CHECK(!servo_screen_rauto_running());
    CHECK(!svs_seq_busy());
    CHECK_EQ(ra.p5_ons, 0u);
    CHECK_EQ(ra.arms, 0u);
    CHECK(!prog5v_box_on());
    CHECK(strstr(band(), WHY_EN) != NULL);
    ra.p5_task = false;
    carrier_done();
}

/* The hold fired at 15V, the PROG 5V read back after 800ms; 5.1V
 * published while the steps wait: they end, every output off, the
 * failure naming the input; no ARM. */
TEST_CASE(an_input_gone_low_during_the_steps_ends_them_all_off)
{
    carrier_safe_off(800u);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(2060u);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    CHECK(svs_seq_busy());
    ra_ms(100u);
    prog5v_box_publish_input(5100u);
    ra_ms(100u);
    CHECK(!svs_seq_busy());
    CHECK(!servo_screen_rauto_running());
    CHECK_EQ(ra.arms, 0u);
    const char *f = svs_seq_failure();
    CHECK(f != NULL && strstr(f, WHY_EN) != NULL);
    ra_ms(1000u);
    CHECK(!prog5v_box_on());
    ra.p5_task = false;
    carrier_done();
}

/* The limit run's seam to the programmer (servo_screen_int.h). */
bool lim_seam_connect(uint8_t fam, uint16_t mv, uint16_t ma);

/* A limit run's CONNECT, asked after its START, is refused while the
 * input is low, and taken once it is back. */
TEST_CASE(a_limit_runs_connect_is_refused_while_the_input_is_low)
{
    carrier_port(false);
    go(SERVO_FN_LIMRUN);
    ra_ms(100u);
    prog5v_box_publish_input(5100u);
    CHECK(!lim_seam_connect(SERVO_LIMRUN_FAM_KST, 0u, 0u));
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    carrier_done();
}

/** @brief A DOWN on CONNECT.  @return the button */
static gfx_rect_t connect_down(void)
{
    const gfx_rect_t r = val(PG_SERVO, 0, 0);

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 1);
    return r;
}

TEST_CASE(kst_connect_is_a_2s_hold)
{
    band_live();
    carrier_port(false);
    go(SERVO_FN_KST);
    ra_ms(100u);
    prog5v_box_publish_input(15000u);
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    /* A tap: the hint, no session. */
    ui_router_set_alert(NULL);
    tap(val(PG_SERVO, 0, 0));
    ticks(0.1f);
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK_STR_EQ(band(), TR(HOLD_HINT_CONNECT));
    /* 1s, then the finger leaves: nothing, no hint. */
    ui_router_set_alert(NULL);
    gfx_rect_t r = connect_down();
    ticks(1.0f);
    ev(r.x - 20, cy(r), TOUCH_EVENT_MOVE, 1);
    ticks(1.5f);
    ev(r.x - 20, cy(r), TOUCH_EVENT_UP, 1);
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK_STR_EQ(band(), "");
    /* 1s, then the touch is lost. */
    r = connect_down();
    ticks(1.0f);
    kst_panel_cancel();
    ticks(1.5f);
    ev(cx(r), cy(r), TOUCH_EVENT_UP, 1);
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    /* Pressed at 5.1V: refused, and no hold runs after the input is back. */
    prog5v_box_publish_input(5100u);
    r = connect_down();
    CHECK_STR_EQ(band(), WHY_EN);
    prog5v_box_publish_input(15000u);
    ticks(2.5f);
    ev(cx(r), cy(r), TOUCH_EVENT_UP, 1);
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    /* Gone low at 1s: the hold ends with the reason and stays ended. */
    ui_router_set_alert(NULL);
    r = connect_down();
    ticks(1.0f);
    prog5v_box_publish_input(5100u);
    ticks(0.1f);
    CHECK_STR_EQ(band(), WHY_EN);
    prog5v_box_publish_input(15000u);
    ticks(1.5f);
    ev(cx(r), cy(r), TOUCH_EVENT_UP, 1);
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK(!asked_on_any());
    /* Held 2s: the session starts while the finger is still down. */
    r = connect_down();
    ticks(2.1f);
    CHECK(kst_prog_state(M) != KST_PROG_PWM);
    ev(cx(r), cy(r), TOUCH_EVENT_UP, 1);
    CHECK(kst_prog_state(M) != KST_PROG_PWM);
    carrier_done();
}

int main(void)
{
    RUN(low_is_under_13v5_on_the_carrier_with_a_reading);
    RUN(the_texts_name_the_input_in_en_and_de);
    RUN(the_box_shows_the_input_and_refuses_its_on);
    RUN(the_box_takes_its_on_at_15v_and_20v);
    RUN(kst_supply_row_is_red_and_connect_is_refused);
    RUN(rudder_auto_start_is_refused_with_nothing_switched);
    RUN(limit_run_start_is_refused_with_nothing_switched);
    RUN(no_warning_off_the_carrier);
    RUN(supply_names_the_input_in_its_head_line);
    RUN(an_input_gone_low_under_the_boxs_hold_posts_no_on);
    RUN(an_input_gone_low_under_rudder_autos_hold_refuses_it);
    RUN(an_input_gone_low_during_the_steps_ends_them_all_off);
    RUN(a_limit_runs_connect_is_refused_while_the_input_is_low);
    RUN(kst_connect_is_a_2s_hold);
    return test_summary("prog5v_input");
}
