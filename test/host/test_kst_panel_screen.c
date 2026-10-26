/**
 * @file test_kst_panel_screen.c
 * @brief The programmer overlay on the SERVO screen: the pulse while the port is
 * held, the settings that cannot move, the supply and the ARM hold.
 *
 * Cases:
 * - the_supplys_controls_are_held_for_as_long_as_the_port
 * - connect_is_refused_over_the_supplys_limits
 * - a_panel_that_starts_under_a_session_reads_its_options_at_the_entry
 * - the_port_row_names_the_programming_voltage_and_current
 * - commanded_shows_no_pulse_while_the_programmer_holds_the_port
 * - program_lists_the_programmer_and_its_row_opens_it_a_level_down
 * - the_home_tag_in_a_held_session_asks_for_leave_then_climbs
 * - the_kst_row_names_the_ports_pin_from_the_feed
 * - no_setting_that_reaches_the_port_changes_while_it_is_held
 * - the_programmer_stays_on_the_screen_while_the_port_is_held
 * - leaving_servo_switches_its_supply_off_once_the_port_lets_go
 * - an_armed_bench_is_not_connected_and_a_held_port_takes_no_arm
 * - a_port_found_held_ends_a_hold_of_arm
 * - an_unknown_coordinate_is_no_row
 * - a_label_under_the_wrong_row_name_fails
 * - a_tap_asks_for_a_look_once
 * - a_lost_touch_stream_on_the_screen_drops_the_programmers_hold
 *
 * SPDX-License-Identifier: MIT
 */

#include "kst_panel_screen_rig.h"

/* The PORT row carries what CONNECT asks of the supply, where the bench
 * sets the supply; the labels of the rows that are no register's name are
 * the language's. */
TEST_CASE(the_port_row_names_the_programming_voltage_and_current)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        char want[KST_PANEL_LINE];
        kst_prog_feed_t f;

        fresh();
        ui_text_set_language((ui_lang_t)lang);
        link_up();
        CHECK_STR_EQ(kst_panel_label(PG_SERVO, 0, 0), "PORT");
        f = M->f;
        f.rd.flags |= LINK_KS_F_RAIL;
        kst_panel_feed(&f);
        show();
        snprintf(want, sizeof(want), "PORT  %u.%uV  %umA",
                 kst_prog_supply_mv(M) / 1000u,
                 kst_prog_supply_mv(M) % 1000u / 100u,
                 (unsigned)kst_prog_supply_ma(M));
        CHECK_STR_EQ(kst_panel_label(PG_SERVO, 0, 0), want);
        /* Inside the row, left of its button. */
        CHECK(kst_panel_value_rect(PG_SERVO, 0, 0).x - AREA.x - 10
              > (int)strlen(want) * 8);
        CHECK_STR_EQ(kst_panel_label(PG_TUNE, 3, 0),
                     lang == (int)UI_LANG_EN ? "SPEED LIMIT" : "TEMPOGRENZE");
        CHECK_STR_EQ(kst_panel_label(PG_TUNE, 3, 1),
                     lang == (int)UI_LANG_EN ? "SPEED" : "TEMPO");
        CHECK_STR_EQ(kst_panel_label(PG_FAILSAFE, 1, 1),
                     lang == (int)UI_LANG_EN ? "AFTER" : "NACH");
    }
    ui_text_set_language(UI_LANG_EN);
}

/* While the programmer holds the port no pulse is on it, and the tester's
 * COMMANDED row shows none. */
TEST_CASE(commanded_shows_no_pulse_while_the_programmer_holds_the_port)
{
    fresh_screen();
    link_up();
    CHECK(strstr(servo_screen_commanded_text(), "us") != NULL);
    program_open();
    CHECK(kst_panel_is_open());
    tap(val(PG_SERVO, 0, 0));
    CHECK(kst_panel_holds_port());
    CHECK_STR_EQ(servo_screen_commanded_text(), "---");
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    scr->render(&cv, 0);
    CHECK_STR_EQ(servo_screen_commanded_text(), "---");
    tap(val(PG_SERVO, 0, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    hold(2.1f);
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK(!kst_panel_holds_port());
    CHECK(strstr(servo_screen_commanded_text(), "us") != NULL);
}

TEST_CASE(program_lists_the_programmer_and_its_row_opens_it_a_level_down)
{
    fresh_screen();
    link_up();
    CHECK(!kst_panel_is_open());
    /* PROGRAM is a tab: its list, no session yet. */
    screen_tap(PROG_X, BTN_Y);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_PROGRAM);
    CHECK(!kst_panel_is_open());
    /* The row opens the session over the left column, and the session
     * takes the taps there. */
    screen_tap(SV_PROG_ROW_X, SV_PROG_ROW_Y(0));
    CHECK(kst_panel_is_open());
    tap(val(PG_SERVO, 2, 1));
    CHECK_EQ(kst_panel_editor(), 2);
    tap(kst_panel_choice_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);
    /* CLOSE climbs to the list. */
    program_close();
    CHECK(!kst_panel_is_open());
    CHECK_EQ(servo_screen_task(), (int)UI_WB_PROGRAM);
    /* The session covers the strip: a tap where LIVE is goes to the
     * session (its own page tabs), and the task stays. */
    screen_tap(SV_PROG_ROW_X, SV_PROG_ROW_Y(0));
    CHECK(kst_panel_is_open());
    screen_tap(LIVE_X, BTN_Y);
    CHECK(kst_panel_is_open());
    CHECK_EQ(servo_screen_task(), (int)UI_WB_PROGRAM);
    program_close();
    CHECK(!kst_panel_is_open());
    /* Under the one row the list opens nothing. */
    screen_tap(SV_PROG_ROW_X, SV_PROG_ROW_Y(3));
    CHECK(!kst_panel_is_open());
    /* Another task and back: the list, not a session. */
    screen_tap(SETB_X, BTN_Y);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_SETTINGS);
    screen_tap(PROG_X, BTN_Y);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_PROGRAM);
    CHECK(!kst_panel_is_open());
}

/*
 * The home tag in the session (the router calls the screen's back()): over
 * an editor it closes the editor; while the port moves it does nothing;
 * with the port in programming it asks for LEAVE and the session stays,
 * and over that question it closes the question; once the port is back in
 * PWM it climbs to PROGRAM's list, and the next one lets the screen go.
 */
TEST_CASE(the_home_tag_in_a_held_session_asks_for_leave_then_climbs)
{
    fresh_screen();
    link_up();
    program_open();
    /* MORE's list: one tag closes it, the session stays. */
    tap(val(PG_SERVO, 2, 1));
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK(scr->back());
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK(kst_panel_is_open());
    /* CONNECT: while the port goes into programming the tag waits. */
    tap(val(PG_SERVO, 0, 0));
    CHECK(kst_panel_holds_port());
    CHECK(kst_prog_state(M) != KST_PROG_PROGRAMMING);
    CHECK(scr->back());
    CHECK(kst_panel_is_open());
    CHECK_EQ(kst_panel_editor(), 0);
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    /* In programming: the tag asks for LEAVE and stays on PROGRAM. */
    CHECK(scr->back());
    CHECK(kst_panel_is_open());
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_PROGRAM);
    /* Over the question it closes the question; the port stays held. */
    CHECK(scr->back());
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK(kst_panel_is_open());
    CHECK(kst_panel_holds_port());
    /* Asked again, and LEAVE held: the port is back in PWM. */
    CHECK(scr->back());
    CHECK_EQ(kst_panel_editor(), 3);
    hold(2.1f);
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK(!kst_panel_holds_port());
    /* Now the tag climbs to the list, then lets the screen go. */
    CHECK(scr->back());
    CHECK(!kst_panel_is_open());
    CHECK_EQ(servo_screen_task(), (int)UI_WB_PROGRAM);
    CHECK(!scr->back());
}

/* PROGRAM's row names the pin of the port the session works on (the
 * TARGET's) once the feed carries it, and the transport alone without. */
TEST_CASE(the_kst_row_names_the_ports_pin_from_the_feed)
{
    size_t n = 0;
    const prog_reg_row_t *rows = prog_registry(&n);
    char out[48];
    char want[24];

    fresh_screen();
    link_up();
    settle();
    CHECK_EQ(kst_panel_pin(), (uint8_t)RIG_PIN);
    CHECK(!prog_reg_detail(rows, n, 0, LINK_CAP_SERVO_PWM, out, sizeof(out)));
    snprintf(want, sizeof(want), "PROG PORT GPIO %u", (unsigned)RIG_PIN);
    CHECK_STR_EQ(out, want);

    fresh_screen();
    g_rig.no_pin = true;
    link_up();
    settle();
    CHECK_EQ(kst_panel_pin(), (uint8_t)PROG_REG_NO_PIN);
    CHECK(!prog_reg_detail(rows, n, 0, LINK_CAP_SERVO_PWM, out, sizeof(out)));
    CHECK_STR_EQ(out, "PROG PORT");
}

/* Every call that changes a setting, on the tester's target and the port's
 * pulse.  True when one of them took. */
static bool port_settings_move(void)
{
    const int target = settings_get_int(SET_SERVO_TARGET);
    const bool pwm = settings_get_bool(SET_SERVO_PORT_PWM);
    bool moved;

    settings_set(SET_SERVO_TARGET, (float)(1 - target));
    settings_set(SET_SERVO_PORT_PWM, pwm ? 0.0f : 1.0f);
    moved = settings_get_int(SET_SERVO_TARGET) != target
         || settings_get_bool(SET_SERVO_PORT_PWM) != pwm;
    settings_set(SET_SERVO_TARGET, (float)target);
    settings_set(SET_SERVO_PORT_PWM, pwm ? 1.0f : 0.0f);
    settings_adjust(SET_SERVO_TARGET, 1);
    settings_adjust(SET_SERVO_PORT_PWM, -1);
    moved = moved || settings_get_int(SET_SERVO_TARGET) != target
         || settings_get_bool(SET_SERVO_PORT_PWM) != pwm;
    settings_set(SET_SERVO_TARGET, 1.0f);
    settings_set(SET_SERVO_PORT_PWM, 0.0f);
    settings_reset(SET_CAT_SERVO);
    moved = moved || settings_get_int(SET_SERVO_TARGET) != target
         || settings_get_bool(SET_SERVO_PORT_PWM) != pwm;
    settings_set(SET_SERVO_TARGET, 1.0f);
    settings_set(SET_SERVO_PORT_PWM, 0.0f);
    settings_reset_all();
    moved = moved || settings_get_int(SET_SERVO_TARGET) != target
         || settings_get_bool(SET_SERVO_PORT_PWM) != pwm;
    /* As it was, for the next look. */
    if (moved) {
        settings_set(SET_SERVO_TARGET, (float)target);
        settings_set(SET_SERVO_PORT_PWM, pwm ? 1.0f : 0.0f);
    }
    return moved;
}

static unsigned g_held_frames;

static unsigned g_free_frames;

/* One frame, and what the settings take in it: nothing while the port is
 * out of PWM, an operation is on its way or the model has a session. */
static bool held_frame(void)
{
    kst_prog_feed_t f;
    const bool moved = port_settings_move();
    bool taken;

    rig_feed_now(&f);
    taken = kst_port_state(&g_rig.port) != LINK_KST_PWM
         || kst_prog_port_taken(&f.rd) || kst_panel_holds_port();
    if (taken && moved) {
        return false;
    }
    if (moved != !settings_held(SET_SERVO_TARGET)
        || moved != !settings_held(SET_SERVO_PORT_PWM)) {
        return false;
    }
    g_held_frames += moved ? 0u : 1u;
    g_free_frames += moved ? 1u : 0u;
    rig_frame();
    return true;
}

static bool held_frames(unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        if (!held_frame()) {
            return false;
        }
    }
    return true;
}

/* The tester's target and the port's pulse are settings.  From CONNECT to
 * the end of DISCONNECT no call of the settings model changes either, for
 * each pair of values they stand at: through the entry, the read, a write,
 * a stop that leaves the port out of PWM and the leave.  Before and after,
 * every call takes. */
TEST_CASE(no_setting_that_reaches_the_port_changes_while_it_is_held)
{
    for (unsigned was = 0; was < 4u; ++was) {
        fresh();
        link_up();
        settings_set(SET_SERVO_TARGET, (float)(was & 1u));
        settings_set(SET_SERVO_PORT_PWM, (float)((was >> 1) & 1u));
        g_held_frames = 0u;
        g_free_frames = 0u;
        CHECK(held_frames(20u));
        CHECK_EQ(g_held_frames, 0u);
        CHECK_EQ(g_free_frames, 20u);
        CHECK(!settings_held(SET_SERVO_TARGET));
        /* Another setting is nobody's to hold. */
        CHECK(!settings_held(SET_SERVO_PROG_WAIT));
        CHECK(!settings_held(SET_BRIGHTNESS));

        /* CONNECT: held from the press, before a frame has gone out. */
        tap(val(PG_SERVO, 0, 0));
        CHECK_EQ(kst_prog_state(M), KST_PROG_ENTERING);
        CHECK(settings_held(SET_SERVO_TARGET));
        CHECK(settings_held(SET_SERVO_PORT_PWM));
        CHECK(!settings_held(SET_BRIGHTNESS));
        g_free_frames = 0u;
        CHECK(held_frames(6000u));
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        CHECK(kst_prog_have_servo(M));
        CHECK_EQ(g_free_frames, 0u);

        /* A write. */
        enable();
        edits();
        tap(val(PG_SERVO, 2, 0));
        show();
        for (unsigned i = 0; i < 40u && kst_panel_editor() != 0; ++i) {
            hold(2.1f);
            CHECK(held_frames(200u));
        }
        CHECK(held_frames(8000u));
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        CHECK_EQ(g_free_frames, 0u);

        /* DISCONNECT, stopped on its way: the port stays out of PWM. */
        tap(val(PG_SERVO, 0, 0));
        hold(2.1f);
        CHECK_EQ(kst_prog_state(M), KST_PROG_LEAVING);
        rig_stop(true);
        CHECK(held_frames(4000u));
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        CHECK_EQ(g_free_frames, 0u);
        rig_stop(false);
        CHECK(held_frames(600u));

        /* And carried out: free once the port is back in PWM. */
        tap(val(PG_SERVO, 0, 0));
        hold(2.1f);
        const unsigned held = g_held_frames;
        CHECK(held_frames(12000u));
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK_EQ(kst_port_state(&g_rig.port), LINK_KST_PWM);
        CHECK(g_held_frames > held);
        CHECK(g_free_frames > 0u);
        CHECK(!settings_held(SET_SERVO_TARGET));
        CHECK(!settings_held(SET_SERVO_PORT_PWM));
        CHECK_EQ(settings_get_int(SET_SERVO_TARGET), (int)(was & 1u));
        CHECK_EQ((unsigned)settings_get_bool(SET_SERVO_PORT_PWM),
                 (was >> 1) & 1u);
    }

    /* A link that goes with the port in programming and comes back: held
     * from the first answer on, with the model not in a session. */
    fresh();
    connected();
    rig_link_lost();
    CHECK_EQ(kst_prog_state(M), KST_PROG_DOWN);
    CHECK(kst_port_state(&g_rig.port) != LINK_KST_PWM);
    kst_link_came_up(&g_rig.link, LINK_PROTOCOL_MINOR);
    /* The frame that feeds the model the answer. */
    rig_frame();
    g_free_frames = 0u;
    g_held_frames = 0u;
    CHECK(held_frames(3000u));
    CHECK(kst_port_state(&g_rig.port) != LINK_KST_PWM);
    CHECK_EQ(g_free_frames, 0u);
    CHECK_EQ(g_held_frames, 3000u);
}

TEST_CASE(the_programmer_stays_on_the_screen_while_the_port_is_held)
{
    servo_cmd_t junk;

    fresh_screen();
    link_up();
    program_open();
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK(kst_panel_holds_port());
    /* A tap where the strip's LIVE is does not uncover it. */
    ev(LIVE_X, BTN_Y, TOUCH_EVENT_DOWN, 1);
    ev(LIVE_X, BTN_Y, TOUCH_EVENT_UP, 1);
    CHECK(kst_panel_is_open());
    CHECK_EQ(servo_screen_task(), (int)UI_WB_PROGRAM);
    /* CLOSE asks for the way back and closes nothing. */
    program_close();
    CHECK(kst_panel_is_open());
    CHECK_EQ(kst_panel_editor(), 3);
    tap(kst_panel_cancel_rect());
    /* The switch goes off when the screen is entered again, and the
     * overlay is there again. */
    enable();
    scr->leave();
    CHECK(!kst_panel_is_open());
    CHECK(kst_prog_write_enabled(M));
    scr->enter();
    CHECK(!kst_prog_write_enabled(M));
    CHECK_EQ(servo_screen_task(), (int)UI_WB_LIVE);
    scr->tick(0.02f);
    /* On PROGRAM, one level down, as the port is held. */
    CHECK(kst_panel_is_open());
    CHECK_EQ(servo_screen_task(), (int)UI_WB_PROGRAM);
    scr->render(&cv, 0);
    scr->render(&cv, 1);
    /* Leaving programming gives the tester its screen back. */
    tap(val(PG_SERVO, 0, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    hold(2.1f);
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    program_close();
    CHECK(!kst_panel_is_open());
    /* The tester's controls answer: the strip switches tasks. */
    while (servo_screen_take(&junk)) { }
    screen_tap(SETB_X, BTN_Y);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_SETTINGS);
    scr->render(&cv, 0);
    screen_tap(LIVE_X, BTN_Y);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_LIVE);
    CHECK(!kst_panel_is_open());
}

/* The SET row of the SERVO screen's right card, as the screen lays it
 * out. */
#define SUP_ROW_Y SV_SUP_Y

#define SUP_V_X   SV_SUP_V_X

#define SUP_I_X   SV_SUP_I_X

#define SUP_OUT_X SV_SUP_OUT_X

/* A key of the SERVO screen's own keypad, where the screen opens it. */
static void screen_key(ui_key_t key_id)
{
    ui_keypad_t probe;
    gfx_rect_t r;

    memset(&probe, 0, sizeof(probe));
    ui_keypad_open(&probe, AREA, "", "", 0.0f, 0.0f, 1.0f, 2);
    r = ui_keypad_key_rect(&probe, key_id);
    screen_tap(cx(r), cy(r));
}

/* OUTPUT ON held on the SERVO screen's SET row until it asks. */
static void servo_output_on(void)
{
    supply_cmd_t c;

    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 120; ++i) {
        scr->tick(0.025f);
    }
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.on);
    supply_screen_set_output(true);         /* the supply reports it */
    scr->tick(0.025f);
}

/* CONNECT, with the PD mini's output on behind it as the port reports. */
static void connect_powered(void)
{
    g_rig.rail = true;
    link_up();
    program_open();
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK(kst_panel_holds_port());
    supply_screen_set_output(true);
    scr->tick(0.02f);
}

/*
 * A supply the SERVO screen switched on goes off when the screen is left:
 * by OUTPUT ON, or by the programmer's CONNECT.  The programmer's overlay
 * and the SET row's keypad are the screen's own and are no leave.  Left
 * while the port holds the supply, the OFF waits for the port to let go:
 * an OFF now would end the session and cut a write on the wire.  Shown
 * again first, the OFF is dropped and the next leave owes it again.
 */
TEST_CASE(leaving_servo_switches_its_supply_off_once_the_port_lets_go)
{
    supply_cmd_t c;

    /* OUTPUT ON, the overlay opened and closed, a keypad: still on. */
    fresh_screen();
    g_rig.rail = true;
    link_up();
    servo_output_on();
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_SERVO);
    program_open();
    CHECK(kst_panel_is_open());
    program_close();
    CHECK(!kst_panel_is_open());
    screen_tap(SUP_V_X, SUP_ROW_Y);
    screen_key(UI_KEY_CANCEL);
    CHECK(!supply_screen_poll_cmd(&c));
    scr->leave();
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off && !c.on);
    supply_screen_set_output(false);

    /* With the output off the leave asks nothing. */
    fresh_screen();
    scr->leave();
    CHECK(!supply_screen_poll_cmd(&c));

    /* CONNECT's supply, left under the held port: owed, not sent. */
    fresh_screen();
    connect_powered();
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_SERVO);
    scr->leave();
    CHECK(!supply_screen_poll_cmd(&c));
    CHECK(supply_screen_off_owed());
    CHECK(kst_panel_holds_port());
    /* Shown again: dropped, and the way back sends no OFF of its own. */
    scr->enter();
    scr->tick(0.02f);
    CHECK(!supply_screen_off_owed());
    tap(val(PG_SERVO, 0, 0));
    hold(2.1f);
    settle();
    CHECK(!kst_panel_holds_port());
    CHECK(!supply_screen_poll_cmd(&c));
    /* The port back in PWM, the supply still on: leaving switches it off. */
    scr->leave();
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off && !c.on);

    /* Left under the held port, which then lets go behind the screen: a
     * STOP, and the PD mini off under the port, which ends the session.
     * The panel's report of the output is a sample behind: the owed OFF
     * goes as the port lets go. */
    fresh_screen();
    connect_powered();
    scr->leave();
    CHECK(!supply_screen_poll_cmd(&c));
    rig_stop(true);
    g_rig.sup_on = false;
    for (unsigned i = 0; i < 60000u && supply_screen_port_held(); ++i) {
        rig_frame();
    }
    CHECK(!supply_screen_port_held());
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
    CHECK(!supply_screen_off_owed());
    rig_stop(false);
    settle();
}

/* From CONNECT to the end of DISCONNECT the supply's own controls offer an
 * OFF and nothing else: the SET row of the SERVO screen and the SUPPLY
 * screen take no set point and no ON. */
TEST_CASE(the_supplys_controls_are_held_for_as_long_as_the_port)
{
    supply_cmd_t c;
    static gfx_color_t a[W * H];
    static gfx_color_t b[W * H];
    gfx_canvas_t ca, cb;
    gfx_canvas_init(&ca, a, W, H, W);
    gfx_canvas_init(&cb, b, W, H, W);
    int moved = 0;

    fresh_screen();
    g_rig.rail = true;
    link_up();
    program_open();
    CHECK(!supply_screen_port_held());
    const float v0 = supply_screen_set_v();
    const float i0 = supply_screen_set_i();
    scr->tick(0.02f);
    scr->render(&ca, 0);
    /* CONNECT: held from the tap, before the port has answered. */
    tap(val(PG_SERVO, 0, 0));
    CHECK_EQ(kst_prog_state(M), KST_PROG_ENTERING);
    CHECK(supply_screen_port_held());
    settle();
    CHECK(kst_panel_holds_port());
    CHECK(supply_screen_port_held());
    /* The row is drawn as taking no press. */
    scr->tick(0.02f);
    scr->render(&cb, 0);
    for (int y = SUP_ROW_Y - 12; y < SUP_ROW_Y + 12; ++y) {
        for (int x = SUP_V_X - 20; x < SUP_OUT_X + 30; ++x) {
            moved += (a[y * W + x] != b[y * W + x]) ? 1 : 0;
        }
    }
    CHECK(moved > 0);
    /* No set point: the row opens no keypad, and SUPPLY takes none. */
    screen_tap(SUP_V_X, SUP_ROW_Y);
    screen_key(UI_KEY_7);
    screen_key(UI_KEY_OK);
    screen_tap(SUP_I_X, SUP_ROW_Y);
    screen_key(UI_KEY_1);
    screen_key(UI_KEY_OK);
    CHECK(kst_panel_is_open());
    supply_screen_put(8.0f, 3.0f);
    CHECK_NEAR(supply_screen_set_v(), v0, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), i0, 1e-4f);
    /* No ON: the hold on OUTPUT ON asks for nothing. */
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 120; ++i) {
        scr->tick(0.025f);
    }
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(&c));
    /* OFF is a tap, as ever. */
    supply_screen_set_output(true);
    scr->tick(0.025f);
    screen_tap(SUP_OUT_X, SUP_ROW_Y);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off && !c.on);
    supply_screen_set_output(false);
    scr->tick(0.025f);
    /* Held through a read and through the way back. */
    tap(val(PG_SERVO, 1, 0));
    CHECK(supply_screen_port_held());
    settle();
    tap(val(PG_SERVO, 0, 0));
    hold(2.1f);
    CHECK_EQ(kst_prog_state(M), KST_PROG_LEAVING);
    CHECK(supply_screen_port_held());
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK(!supply_screen_port_held());
    /* And free again. */
    program_close();
    CHECK(!kst_panel_is_open());
    screen_tap(SUP_V_X, SUP_ROW_Y);
    screen_key(UI_KEY_5);
    screen_key(UI_KEY_OK);
    CHECK_NEAR(supply_screen_set_v(), 5.0f, 1e-4f);

    /* A hold on OUTPUT ON under way when CONNECT is taken ends unasked. */
    fresh_screen();
    g_rig.rail = true;
    link_up();
    program_open();
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 20; ++i) {
        scr->tick(0.025f);
    }
    ev(cx(val(PG_SERVO, 0, 0)), cy(val(PG_SERVO, 0, 0)), TOUCH_EVENT_DOWN, 2);
    ev(cx(val(PG_SERVO, 0, 0)), cy(val(PG_SERVO, 0, 0)), TOUCH_EVENT_UP, 2);
    if (kst_prog_state(M) != KST_PROG_PWM) {
        for (int i = 0; i < 120; ++i) {
            scr->tick(0.025f);
        }
        ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
        CHECK(!supply_screen_poll_cmd(&c));
    }

    /* A refused CONNECT holds nothing. */
    fresh_screen();
    link_up();
    program_open();
    servo_screen_set_armed(true);
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_DISARM);
    CHECK(!supply_screen_port_held());
    servo_screen_set_armed(false);
}

/* Programming values above the SUPPLY screen's limits: CONNECT is refused
 * by name, nothing is sent and the supply is not touched.  A session never
 * runs at values other than the ones the programmer shows. */
TEST_CASE(connect_is_refused_over_the_supplys_limits)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        for (int which = 0; which < 2; ++which) {
            /* 4.9V or 0.8A allowed; the programmer asks 5.0V 1.0A. */
            fresh_screen();
            ui_text_set_language((ui_lang_t)lang);
            settings_set(SET_SUPPLY_V_MAX, which == 0 ? 4.9f : 21.0f);
            settings_set(SET_SUPPLY_I_MAX, which == 0 ? 5.0f : 0.8f);
            supply_screen_settings_loaded();
            g_rig.rail = true;
            link_up();
            program_open();
            const unsigned starts = g_rig.starts;
            tap(val(PG_SERVO, 0, 0));
            settle();
            CHECK_EQ(msg(), KST_PROG_MSG_SUPPLY_CAPS);
            CHECK_STR_EQ(said(), TR(KP_M_SUPPLY_CAPS));
            CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
            CHECK(!kst_panel_holds_port());
            CHECK(!supply_screen_port_held());
            CHECK_EQ(g_rig.starts, starts);
            CHECK_EQ(g_rig.sup_sets, 0);
            /* The limit raised to the value: taken. */
            settings_set(SET_SUPPLY_V_MAX, which == 0 ? 5.0f : 21.0f);
            settings_set(SET_SUPPLY_I_MAX, which == 0 ? 5.0f : 1.0f);
            supply_screen_settings_loaded();
            tap(val(PG_SERVO, 0, 0));
            settle();
            CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
            CHECK_EQ(g_rig.sup_mv, KST_PROG_SUPPLY_MV);
            CHECK_EQ(g_rig.sup_ma, 1000u);
        }
    }
    ui_text_set_language(UI_LANG_EN);
    settings_set(SET_SUPPLY_V_MAX, 21.0f);
    settings_set(SET_SUPPLY_I_MAX, 5.0f);
}

TEST_CASE(a_panel_that_starts_under_a_session_reads_its_options_at_the_entry)
{
    for (int wait = 0; wait <= 10000; wait += 2500) {
        fresh_screen();
        link_up();
        program_open();
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK(kst_panel_holds_port());
        /* The panel starts again: its screens are set up before the
         * settings are read, and the port is still in programming. */
        scr->reset();
        CHECK_EQ(kst_prog_wait_ms(M), KST_PROG_WAIT_MS);
        settings_set(SET_SERVO_PROG_WAIT, (float)wait);
        settings_set(SET_SERVO_PROG_V, 5.5f);
        settings_set(SET_SERVO_PROG_I, 0.8f);
        scr->enter();
        settle();
        CHECK(kst_panel_holds_port());
        CHECK(kst_panel_is_open());
        CHECK_EQ(kst_prog_wait_ms(M), (uint32_t)wait);
        CHECK_EQ(kst_prog_supply_mv(M), 5500u);
        CHECK_EQ(kst_prog_supply_ma(M), 800u);
        /* A session that began with its values keeps them over the settings
         * until it ends. */
        settings_set(SET_SERVO_PROG_WAIT, 77.0f);
        scr->leave();
        scr->enter();
        settle();
        CHECK_EQ(kst_prog_wait_ms(M), (uint32_t)wait);
    }
    settings_init();
}

TEST_CASE(an_armed_bench_is_not_connected_and_a_held_port_takes_no_arm)
{
    servo_cmd_t c;
    bool armed = false;

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh_screen();
        ui_text_set_language((ui_lang_t)lang);
        link_up();
        program_open();
        /* Armed: CONNECT is refused by name and nothing is sent. */
        servo_screen_set_armed(true);
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_NEED_DISARM);
        CHECK_STR_EQ(said(), TR(KP_M_NEED_DISARM));
        CHECK_EQ(g_rig.starts, 0);
        CHECK(!kst_panel_holds_port());
        /* A finger on ARM is an arm on its way: the same. */
        servo_screen_set_armed(false);
        ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 2);
        ticks(0.5f);
        CHECK_EQ(kst_prog_connect(M, RIG_PORT), KST_PROG_MSG_NEED_DISARM);
        ev(ARM_X + 300, ARM_Y, TOUCH_EVENT_MOVE, 2);
        ev(ARM_X + 300, ARM_Y, TOUCH_EVENT_UP, 2);
        ticks(0.1f);
        /* Disarmed: the port is taken. */
        while (servo_screen_take(&c)) { }
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK(g_rig.starts > 0);
        CHECK(kst_panel_holds_port());
        /* And ARM takes no hold while it is held; the press says why in
         * the alert band (station, #612). */
        ui_router_set_alert(NULL);
        ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 2);
        CHECK(ui_router_alert() != NULL);
        CHECK_STR_EQ(ui_router_alert(), TR(ALERT_ARM_PROG));
        ticks(2.5f);
        ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 2);
        scr->render(&cv, 0);
        while (servo_screen_take(&c)) {
            armed = armed || c.kind == SERVO_CMD_ARM;
        }
        CHECK(!armed);
        /* The port left, ARM is taken again. */
        tap(val(PG_SERVO, 0, 0));
        hold(2.1f);
        settle();
        CHECK(!kst_panel_holds_port());
        ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 2);
        ticks(2.5f);
        ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 2);
        while (servo_screen_take(&c)) {
            armed = armed || c.kind == SERVO_CMD_ARM;
        }
        CHECK(armed);
        armed = false;
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_port_found_held_ends_a_hold_of_arm)
{
    servo_cmd_t c;

    fresh_screen();
    link_up();
    program_open();
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK(kst_panel_holds_port());
    /* The panel starts again and a finger is on ARM before the port's page
     * is read. */
    scr->reset();
    scr->enter();
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 2);
    settle();
    CHECK(kst_panel_holds_port());
    ticks(2.5f);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 2);
    while (servo_screen_take(&c)) {
        CHECK(c.kind != SERVO_CMD_ARM);
    }
}

/*
 * A coordinate no row has is no row.  It used to read as the first row of the
 * table, so a test naming a wrong row got that row's answer.
 */
TEST_CASE(an_unknown_coordinate_is_no_row)
{
    fresh();
    connected();
    CHECK(kst_panel_label(PG_SERVO, 1, 1) != NULL);
    CHECK(kst_panel_label(PG_SERVO, 1, 9) == NULL);
    CHECK(kst_panel_label(PG_SERVO, 99, 0) == NULL);
    CHECK(kst_panel_label(-1, 0, 0) == NULL);
    CHECK(kst_panel_button(PG_SERVO, 99, 0) == NULL);
    CHECK(!kst_panel_row_live(PG_SERVO, 99, 0));
    CHECK_EQ(kst_panel_value_rect(PG_SERVO, 99, 0).w, 0);
    CHECK_EQ(kst_panel_value_rect(PG_SERVO, 99, 0).h, 0);
    CHECK_EQ(kst_panel_step_rect(PG_SERVO, 99, true).w, 0);
    /* A real row still has a size. */
    CHECK(kst_panel_value_rect(PG_SERVO, 1, 1).w > 0);
}

/* A label asked for under the wrong row name fails instead of matching. */
TEST_CASE(a_label_under_the_wrong_row_name_fails)
{
    fresh();
    connected();
    CHECK_STR_EQ(kst_panel_label(PG_SERVO, 1, 1), TR(KP_FILES));
    CHECK(kst_panel_label(PG_SERVO, 2, 1) == NULL
          || strcmp(kst_panel_label(PG_SERVO, 2, 1), TR(KP_FILES)) != 0);
}

TEST_CASE(a_tap_asks_for_a_look_once)
{
    fresh();
    /* Nothing is known of the card before the first feed. */
    CHECK(!kst_panel_card_look());
    link_up();
    /* The opening is one. */
    card_out();
    kst_panel_close();
    kst_panel_open();
    CHECK(kst_panel_card_look());
    CHECK(!kst_panel_card_look());
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK(kst_panel_holds_port());
    CHECK(kst_panel_card_look());
    CHECK(!kst_panel_card_look());
    /* FILES with no card says so and asks for the look. */
    tap(val(PG_SERVO, 1, 1));
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_NONE);
    CHECK(kst_panel_card_look());
    CHECK(!kst_panel_card_look());
    /* With a card the look is asked for too: the card in the slot can be
     * another one, or none. */
    card_in();
    tap(val(PG_SERVO, 1, 0));
    settle();
    CHECK(kst_panel_card_look());
    CHECK(!kst_panel_card_look());
}

TEST_CASE(a_lost_touch_stream_on_the_screen_drops_the_programmers_hold)
{
    const gfx_rect_t apply = (fresh_screen(), kst_panel_apply_rect());

    link_up();
    program_open();
    tap(val(PG_SERVO, 0, 0));
    settle();
    tap(val(PG_SERVO, 3, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    ev(cx(apply), cy(apply), TOUCH_EVENT_DOWN, 1);
    ticks(1.0f);
    scr->render(&cv, 0);
    scr->cancel();
    ticks(2.0f);
    ev(cx(apply), cy(apply), TOUCH_EVENT_UP, 1);
    CHECK(!kst_prog_write_enabled(M));
}

int main(void)
{
    RUN(the_supplys_controls_are_held_for_as_long_as_the_port);
    RUN(connect_is_refused_over_the_supplys_limits);
    RUN(a_panel_that_starts_under_a_session_reads_its_options_at_the_entry);
    RUN(the_port_row_names_the_programming_voltage_and_current);
    RUN(commanded_shows_no_pulse_while_the_programmer_holds_the_port);
    RUN(program_lists_the_programmer_and_its_row_opens_it_a_level_down);
    RUN(the_home_tag_in_a_held_session_asks_for_leave_then_climbs);
    RUN(the_kst_row_names_the_ports_pin_from_the_feed);
    RUN(no_setting_that_reaches_the_port_changes_while_it_is_held);
    RUN(the_programmer_stays_on_the_screen_while_the_port_is_held);
    RUN(leaving_servo_switches_its_supply_off_once_the_port_lets_go);
    RUN(an_armed_bench_is_not_connected_and_a_held_port_takes_no_arm);
    RUN(a_port_found_held_ends_a_hold_of_arm);
    RUN(an_unknown_coordinate_is_no_row);
    RUN(a_label_under_the_wrong_row_name_fails);
    RUN(a_tap_asks_for_a_look_once);
    RUN(a_lost_touch_stream_on_the_screen_drops_the_programmers_hold);
    return test_summary("kst_panel_screen");
}
