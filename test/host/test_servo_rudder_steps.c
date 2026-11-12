/**
 * @file test_servo_rudder_steps.c
 * @brief SERVO's RUDDER task: the step keys -10us and +10us
 *        (servo_rudder_panel.c), on the card while SETUP > PANEL's Rotary
 *        knob is OFF or the knob does not answer; a tap moves the pulse
 *        10us, a hold repeats at the shared rate (ui_key_repeat()), the
 *        pulse is held to TRAVEL and the pulse range as 1000us and 2000us
 *        are, nothing moves while SAFE, and the pulse stays after the
 *        release.
 *
 * Cases:
 * - the_step_keys_show_without_a_working_knob_and_hide_with_one
 * - a_tap_moves_the_pulse_one_step_and_the_pulse_stays
 * - a_held_key_repeats_at_the_shared_rate_until_it_is_let_go
 * - the_steps_stop_at_travel_and_at_the_pulse_range
 * - the_keys_move_nothing_while_safe_and_are_drawn_as_the_drive_buttons
 * - the_shared_repeat_steps_at_0_45s_then_8hz_then_30hz
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_screen_rig.h"

#include "servo_rudder_int.h"
#include "ui_help.h"

/** @brief The panel's frame time, s: 39 frames a second. */
#define DT (1.0f / 39.0f)

/** @brief The commands waiting, drained. @return the last; NONE for none */
static servo_cmd_t taken(void)
{
    servo_cmd_t c = { .kind = SERVO_CMD_NONE };
    servo_cmd_t next;
    while (servo_screen_take(&next)) {
        c = next;
    }
    return c;
}

/**
 * @brief A finger down on the middle of control @p k, contact 1.
 * @param k the control
 */
static void down_on(rudder_ctl_t k)
{
    const gfx_rect_t r = rudder_rect(k);
    ev(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_DOWN, 1);
}

/**
 * @brief Contact 1 lifted over the middle of control @p k.
 * @param k the control
 */
static void up_on(rudder_ctl_t k)
{
    const gfx_rect_t r = rudder_rect(k);
    ev(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_UP, 1);
}

/**
 * @brief A tap on the middle of control @p k.
 * @param k the control
 */
static void tap_on(rudder_ctl_t k)
{
    down_on(k);
    up_on(k);
}

/**
 * @brief The screen ticked @p n frames of DT.
 * @param n the frames
 */
static void ticks(int n)
{
    for (int i = 0; i < n; ++i) {
        scr->tick(DT);
    }
}

/** @brief RUDDER on an armed bench, nothing waiting. */
static void armed_rudder(void)
{
    fresh_armed();
    go(SERVO_FN_RUDDER);
    (void)taken();
}

/* ------------------------------------------------------------- shown */

/* The keys are on the card while the knob's setting is OFF, or ON with a
 * knob that does not answer; a knob that answers hides them, drawn and
 * taken: a tap where they stood commands nothing.  The guide's page names
 * them while they are shown.  AUTO's view has its plot in their place. */
TEST_CASE(the_step_keys_show_without_a_working_knob_and_hide_with_one)
{
    armed_rudder();
    CHECK(!settings_get_bool(SET_KNOB_EN));        /* the default */
    CHECK(rudder_steps_shown());
    const gfx_rect_t m = rudder_rect(RUD_C_MINUS);
    const gfx_rect_t p = rudder_rect(RUD_C_PLUS);
    CHECK_EQ(m.x, 358);
    CHECK_EQ(m.x + m.w, 433);
    CHECK_EQ(p.x, 443);
    CHECK_EQ(p.x + p.w, 518);
    CHECK_EQ(m.y, rudder_rect(RUD_C_ZERO).y);
    CHECK_EQ(m.h, UI_TAP_MIN);
    CHECK_EQ(p.h, UI_TAP_MIN);
    /* A knob that answers with its setting OFF moves nothing: the keys
     * stay. */
    servo_screen_set_knob(true);
    CHECK(rudder_steps_shown());
    /* ON, and not answering. */
    settings_set(SET_KNOB_EN, 1.0f);
    servo_screen_set_knob(false);
    CHECK(rudder_steps_shown());
    ui_help_page_t pg;
    CHECK(ui_help_page(scr, false, 0, &pg));
    CHECK_EQ(pg.count, 5);
    CHECK_EQ(pg.item[4].text, TX_HG_SV_RUD_STEP);
    scr->render(&cv, 0);
    const gfx_color_t panel = ui_theme_color(UI_C_PANEL);
    CHECK(fb[(size_t)(p.y + 4) * W + p.x + 4] != panel);

    /* ON and answering: gone from the card, the guide and the touch. */
    servo_screen_set_knob(true);
    CHECK(!rudder_steps_shown());
    CHECK_EQ(rudder_rect(RUD_C_MINUS).w, 0);
    CHECK_EQ(rudder_rect(RUD_C_PLUS).w, 0);
    CHECK(ui_help_page(scr, false, 0, &pg));
    CHECK_EQ(pg.count, 4);
    scr->render(&cv, 0);
    CHECK_EQ(fb[(size_t)(p.y + 4) * W + p.x + 4], panel);
    CHECK_EQ(fb[(size_t)(m.y + 4) * W + m.x + 4], panel);
    const uint16_t before = servo_screen_commanded();
    tap(p.x + p.w / 2, p.y + p.h / 2);
    tap(m.x + m.w / 2, m.y + m.h / 2);
    CHECK_EQ(taken().kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), before);

    /* The knob lost again: back. */
    servo_screen_set_knob(false);
    CHECK(rudder_steps_shown());
    CHECK_EQ(rudder_rect(RUD_C_PLUS).x, p.x);

    /* RUDDER AUTO: not there. */
    go(SERVO_FN_RAUTO);
    CHECK(!rudder_steps_shown());
    CHECK_EQ(rudder_rect(RUD_C_PLUS).w, 0);
}

/* --------------------------------------------------------------- tap */

/* A tap on +10us posts the pulse 10us longer, -10us 10us shorter, from
 * the pulse commanded; the release posts nothing, and the pulse stays
 * through a second of frames after it, as after 1000us. */
TEST_CASE(a_tap_moves_the_pulse_one_step_and_the_pulse_stays)
{
    armed_rudder();
    tap_on(RUD_C_CENTER);
    CHECK_EQ(taken().value_us, 1500u);

    down_on(RUD_C_PLUS);
    servo_cmd_t c = taken();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK_EQ(c.value_us, 1500u + RUD_STEP_US);
    up_on(RUD_C_PLUS);
    CHECK_EQ(taken().kind, SERVO_CMD_NONE);
    ticks(39);
    CHECK_EQ(taken().kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), 1510u);
    char line[64];
    CHECK_STR_EQ(rudder_line(1, line, sizeof(line)), "PULSE 1510us");

    tap_on(RUD_C_MINUS);
    tap_on(RUD_C_MINUS);
    c = taken();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK_EQ(c.value_us, 1490u);
    CHECK_EQ(servo_screen_commanded(), 1490u);
    ticks(39);
    CHECK_EQ(servo_screen_commanded(), 1490u);

    /* From 2000us the next tap down is 1990us. */
    tap_on(RUD_C_2000);
    (void)taken();
    tap_on(RUD_C_MINUS);
    CHECK_EQ(taken().value_us, 1990u);

    /* The labels name the step. */
    char want[8];
    snprintf(want, sizeof(want), "+%dus", RUD_STEP_US);
    CHECK_STR_EQ(TR(RUD_STEP_PLUS), want);
    snprintf(want, sizeof(want), "-%dus", RUD_STEP_US);
    CHECK_STR_EQ(TR(RUD_STEP_MINUS), want);
}

/* ------------------------------------------------------------ repeat */

/* A held key steps on the press, again 0.45s later, then 8 a second:
 * held 1.0s it has stepped 1 + 1 + 4 times.  The steps follow
 * ui_key_repeat() frame by frame, as KST's and HITEC's keys do.  The
 * release, a finger that slides off and a disarm end it. */
TEST_CASE(a_held_key_repeats_at_the_shared_rate_until_it_is_let_go)
{
    armed_rudder();
    tap_on(RUD_C_CENTER);
    (void)taken();
    down_on(RUD_C_PLUS);
    CHECK_EQ(servo_screen_commanded(), 1510u);
    ticks(15);                                  /* 0.38s */
    CHECK_EQ(servo_screen_commanded(), 1510u);
    ticks(24);                                  /* 1.0s */
    CHECK_EQ(servo_screen_commanded(), 1500u + 6u * RUD_STEP_US);
    /* Frame by frame against the shared repeat, out to 3s. */
    float held = 39.0f * DT;
    float ref_held = 0.0f;
    int steps = 1;
    for (int i = 0; i < 39; ++i) {
        steps += ui_key_repeat(&ref_held, DT);
    }
    CHECK_EQ(steps, 6);
    for (int i = 0; i < 78; ++i) {
        steps += ui_key_repeat(&ref_held, DT);
        scr->tick(DT);
        held += DT;
        const unsigned want = 1500u + (unsigned)steps * RUD_STEP_US;
        CHECK_EQ(servo_screen_commanded(),
                 (uint16_t)((want > 2000u) ? 2000u : want));
    }
    CHECK(held > 2.9f);
    CHECK_EQ(taken().kind, SERVO_CMD_POSITION);

    /* The release ends it: the pulse stays where the last step left it. */
    up_on(RUD_C_PLUS);
    const uint16_t kept = servo_screen_commanded();
    ticks(78);
    CHECK_EQ(servo_screen_commanded(), kept);
    CHECK_EQ(taken().kind, SERVO_CMD_NONE);

    /* A finger that slides off the key ends it too. */
    tap_on(RUD_C_CENTER);
    (void)taken();
    down_on(RUD_C_MINUS);
    const gfx_rect_t m = rudder_rect(RUD_C_MINUS);
    ev(m.x + m.w / 2, m.y + m.h + 30, TOUCH_EVENT_MOVE, 1);
    ticks(78);
    CHECK_EQ(servo_screen_commanded(), 1490u);
    ev(m.x + m.w / 2, m.y + m.h + 30, TOUCH_EVENT_UP, 1);

    /* A disarm under a held key ends it: no step while SAFE. */
    down_on(RUD_C_MINUS);
    CHECK_EQ(servo_screen_commanded(), 1480u);
    servo_screen_set_armed(false);
    (void)taken();
    ticks(78);
    CHECK_EQ(taken().kind, SERVO_CMD_NONE);
    servo_screen_set_armed(true);
    (void)taken();
    ticks(78);
    CHECK(taken().kind != SERVO_CMD_POSITION);
    up_on(RUD_C_MINUS);
}

/* ------------------------------------------------------------ clamps */

/* TRIM +20us: TRAVEL's -90° is 1020us, its +90° 2020us, which the pulse
 * range holds to 2000us.  A hold on either key stops there and stays,
 * the ends 1000us and 2000us reach too.  TRAVEL 45°: 1270us and
 * 1770us. */
TEST_CASE(the_steps_stop_at_travel_and_at_the_pulse_range)
{
    fresh();
    open_settings();
    for (int i = 0; i < 4; ++i) {
        sl_step(HL_TRIM, +1);                   /* 5us a step */
    }
    go(SERVO_FN_RUDDER);
    servo_screen_set_armed(true);
    (void)taken();

    tap_on(RUD_C_2000);
    const uint16_t hi_end = taken().value_us;
    tap_on(RUD_C_1000);
    const uint16_t lo_end = taken().value_us;
    CHECK_EQ(hi_end, 2000u);                    /* the pulse range */
    CHECK_EQ(lo_end, 1020u);                    /* TRAVEL */

    tap_on(RUD_C_CENTER);
    (void)taken();
    down_on(RUD_C_PLUS);
    ticks(8 * 39);
    CHECK_EQ(servo_screen_commanded(), hi_end);
    up_on(RUD_C_PLUS);
    tap_on(RUD_C_PLUS);
    servo_cmd_t c = taken();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK_EQ(c.value_us, hi_end);
    tap_on(RUD_C_MINUS);
    CHECK_EQ(taken().value_us, hi_end - RUD_STEP_US);

    down_on(RUD_C_MINUS);
    ticks(8 * 39);
    CHECK_EQ(servo_screen_commanded(), lo_end);
    up_on(RUD_C_MINUS);
    tap_on(RUD_C_MINUS);
    CHECK_EQ(taken().value_us, lo_end);
    tap_on(RUD_C_PLUS);
    CHECK_EQ(taken().value_us, lo_end + RUD_STEP_US);

    /* TRAVEL 45: the steps stop at 1520us -/+ 250us. */
    servo_screen_set_armed(false);
    open_settings();
    sl_tap(HL_TRAVEL);
    keys("45");
    go(SERVO_FN_RUDDER);
    servo_screen_set_armed(true);
    (void)taken();
    down_on(RUD_C_PLUS);
    ticks(5 * 39);
    up_on(RUD_C_PLUS);
    CHECK_EQ(servo_screen_commanded(), 1770u);
    tap_on(RUD_C_2000);
    CHECK_EQ(taken().value_us, 1770u);
    down_on(RUD_C_MINUS);
    ticks(5 * 39);
    up_on(RUD_C_MINUS);
    CHECK_EQ(servo_screen_commanded(), 1270u);
    tap_on(RUD_C_1000);
    CHECK_EQ(taken().value_us, 1270u);
}

/* -------------------------------------------------------------- SAFE */

/* While SAFE a tap or a hold on either key posts nothing and moves
 * nothing, and the keys are drawn as 1000us is: off while SAFE, green
 * while ARMED. */
TEST_CASE(the_keys_move_nothing_while_safe_and_are_drawn_as_the_drive_buttons)
{
    fresh();
    go(SERVO_FN_RUDDER);
    (void)taken();
    const uint16_t before = servo_screen_commanded();
    tap_on(RUD_C_PLUS);
    tap_on(RUD_C_MINUS);
    down_on(RUD_C_PLUS);
    ticks(3 * 39);
    up_on(RUD_C_PLUS);
    CHECK_EQ(taken().kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), before);

    for (int armed = 0; armed < 2; ++armed) {
        servo_screen_set_armed(armed != 0);
        (void)taken();
        servo_invalidate();
        scr->render(&cv, 0);
        const gfx_rect_t d = rudder_rect(RUD_C_1000);
        const gfx_color_t want = fb[(size_t)(d.y + 4) * W + d.x + 4];
        CHECK_EQ(want, armed ? ui_action_fill(UI_ACTION_AVAILABLE)
                             : ui_button_off_fill());
        for (int k = RUD_C_MINUS; k <= RUD_C_PLUS; ++k) {
            const gfx_rect_t r = rudder_rect((rudder_ctl_t)k);
            CHECK_EQ(fb[(size_t)(r.y + 4) * W + r.x + 4], want);
        }
    }
}

/* ------------------------------------------------------ the repeat */

/* The shared repeat: nothing before 0.45s, one as it ends, 8 a second to
 * 2.25s, 30 a second after; a stalled frame is worth 0.25s at most. */
TEST_CASE(the_shared_repeat_steps_at_0_45s_then_8hz_then_30hz)
{
    float held = 0.0f;
    CHECK_EQ(ui_key_repeat(&held, 0.2f), 0);
    CHECK_EQ(ui_key_repeat(&held, 0.24f), 0);   /* 0.44s */
    CHECK_EQ(ui_key_repeat(&held, 0.02f), 1);   /* 0.46s */
    CHECK_EQ(ui_key_repeat(&held, 1.0f), 2);    /* 0.71s: the stall's 0.25s */
    int n = 0;
    while (held < 2.25f - 0.001f) {
        n += ui_key_repeat(&held, 0.005f);
    }
    /* From 0.71s to 2.25s at 8Hz: repeats 4 to 15. */
    CHECK_EQ(n, 12);
    n = 0;
    for (int i = 0; i < 200; ++i) {
        n += ui_key_repeat(&held, 0.005f);      /* 1.0s more */
    }
    CHECK(n >= 29 && n <= 31);
    CHECK_EQ(ui_key_repeat(NULL, 0.1f), 0);
    held = 0.0f;
    CHECK_EQ(ui_key_repeat(&held, -1.0f), 0);
    CHECK_EQ(held, 0.0f);
}

int main(void)
{
    RUN(the_step_keys_show_without_a_working_knob_and_hide_with_one);
    RUN(a_tap_moves_the_pulse_one_step_and_the_pulse_stays);
    RUN(a_held_key_repeats_at_the_shared_rate_until_it_is_let_go);
    RUN(the_steps_stop_at_travel_and_at_the_pulse_range);
    RUN(the_keys_move_nothing_while_safe_and_are_drawn_as_the_drive_buttons);
    RUN(the_shared_repeat_steps_at_0_45s_then_8hz_then_30hz);
    return test_summary("servo_rudder_steps");
}
