/*
 * The Hitec programmer's overlay (shared/ui/hitec_panel*.c) in every state
 * and with every editor, on the loopback of hitec_prog_rig.h: the state
 * line of each state, a retry and its code, a port without a pin; MORE
 * with the programming voltage and current on the keypad at their limits
 * and CANCEL; READ ALL, ABORT and CLEAR by their buttons; the home tag over
 * an editor, without a port and over another session's port; a hold that
 * the finger leaves; values that show raw, a fail-safe position, LIMP and
 * an overload limit; a classic resolution and a classic servo whose name
 * no model has; a D series servo whose number no model has, a classic
 * checksum that does not balance, a voltage above 6.0V, a message the state
 * line says, the registry's hooks, a D series leave and a note that does
 * not fit above the message.  The overlay's bench is hitec_panel_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel_rig.h"

#include "ui_keypad.h"

/* The state line: the SERVO tab's first note. */
static const char *line(void)
{
    const char *n = hitec_panel_note(0);

    return n != NULL ? n : "";
}

/* @p fmt with @p a (and @p b) in the language in force, for a compare. */
static const char *fmt1(ui_text_id_t id, unsigned a)
{
    static char t[HITEC_PANEL_LINE];

    snprintf(t, sizeof(t), ui_tr(id), a);
    return t;
}

static const char *fmt2(ui_text_id_t id, unsigned a, unsigned b)
{
    static char t[HITEC_PANEL_LINE];

    snprintf(t, sizeof(t), ui_tr(id), a, b);
    return t;
}

static void key(ui_key_t k)
{
    tap(ui_keypad_key_rect(hitec_panel_keypad(), k));
}

/* Run the bench until the model is in @p s, the overlay drawn. */
static void reach(hitec_prog_state_t s)
{
    until(s);
    show();
}

/* ---------------------------------------------------------------- states */

TEST_CASE(the_state_line_follows_the_link)
{
    open_en();
    /* The link goes. */
    hitec_link_lost(&g_l);
    g_link = false;
    reach(HITEC_PROG_DOWN);
    CHECK_EQ(strcmp(line(), TR(KP_ST_DOWN)), 0);
    /* An IOmcu without the page. */
    hitec_link_came_up(&g_l, LINK_MINOR_HITEC - 1u);
    g_link = true;
    reach(HITEC_PROG_NO_PAGE);
    CHECK_EQ(strcmp(line(), TR(HP_NO_PAGE)), 0);
    CHECK_EQ(strcmp(hitec_panel_note(1), TR(HP_NO_PAGE_2)), 0);
    CHECK(!hitec_panel_row_live(HITEC_PANEL_SERVO, 0, 0));
    /* One with it, not read yet. */
    hitec_link_lost(&g_l);
    hitec_link_came_up(&g_l, LINK_PROTOCOL_MINOR);
    publish(&g_f);
    frame();
    show();
    CHECK_EQ(pstate(), HITEC_PROG_SYNC);
    CHECK_EQ(strcmp(line(), TR(KP_STATE_SYNC)), 0);
    reach(HITEC_PROG_PWM);
}

TEST_CASE(the_state_line_names_each_entry_and_the_read)
{
    unsigned done = 0, total = 0;

    /* D series with a PD mini. */
    open_en();
    servo_d485();
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    reach(HITEC_PROG_ENTERING);
    CHECK_EQ(strcmp(line(), TR(HP_ST_ENTRY_D)), 0);
    CHECK_EQ(strcmp(hitec_panel_value(HITEC_PANEL_SERVO, 0, 0),
                    TR(KP_ABORT)),
             0);
    reach(HITEC_PROG_READING);
    prun(100u);
    show();
    CHECK(hitec_prog_progress(model(), &done, &total));
    CHECK_EQ(strcmp(line(), fmt2(TX_HP_ST_READ, done + 1u, total)), 0);
    /* The supply goes off under the read: the tries fail with a code. */
    g_hw.v.on = false;
    g_hw.v.asked_on = false;
    for (unsigned i = 0; i < 2000u && hitec_prog_retry_code(model()) == 0u;
         ++i) {
        pms();
    }
    show();
    CHECK(hitec_prog_retry_code(model()) != 0u);
    CHECK_EQ(strcmp(line(), fmt1(TX_HP_BAD_ANSWER,
                                 hitec_prog_retry_code(model()))),
             0);
    reach(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_NO_D);

    /* D series without a PD mini: the servo on its own supply. */
    open_at(SUPPLY_NONE, UI_LANG_EN);
    servo_d485();
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    reach(HITEC_PROG_ENTERING);
    CHECK_EQ(strcmp(line(), TR(HP_ST_MODEL)), 0);
    CHECK(strstr(hitec_panel_port_line(1), TR(HP_SUPPLY_AS_IS)) != NULL);
    reach(HITEC_PROG_HELD);
    /* Held, no supply read: the port and PWM off. */
    CHECK_EQ(strcmp(line(), fmt1(TX_KP_STATE_PROGRAMMING, 1u)), 0);

    /* Classic. */
    open_en();
    servo_hs5645();
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(1));
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    reach(HITEC_PROG_ENTERING);
    CHECK_EQ(strcmp(line(), TR(HP_ST_ENTRY_C)), 0);
    reach(HITEC_PROG_HELD);
}

TEST_CASE(the_state_line_names_clear_leave_and_release)
{
    char want[HITEC_PANEL_LINE];

    /* CLEAR after a switch to SAFE at the bench, by its button. */
    open_en();
    servo_d485();
    tap_connect();
    CHECK(!hitec_panel_row_live(HITEC_PANEL_SERVO, 2, 1));
    disarm();
    prun(200u);
    show();
    CHECK(hitec_prog_stopped(model()));
    CHECK_EQ(strcmp(line(), TR(HP_ST_STOPPED)), 0);
    CHECK(hitec_panel_row_live(HITEC_PANEL_SERVO, 2, 1));
    tap(val(HITEC_PANEL_SERVO, 2, 1));
    CHECK_EQ(pstate(), HITEC_PROG_CLEARING);
    CHECK_EQ(strcmp(line(), TR(HP_ST_CLEARING)), 0);
    CHECK_EQ(strcmp(hitec_panel_value(HITEC_PANEL_SERVO, 0, 0), TR(KP_WAIT)),
             0);
    past(HITEC_PROG_CLEARING);
    settled();
    /* With a STOP latched, CLEAR takes no tap. */
    press_stop();
    prun(50u);
    show();
    CHECK_EQ(strcmp(line(), TR(HP_ST_STOP_LATCHED)), 0);
    CHECK(!hitec_panel_row_live(HITEC_PANEL_SERVO, 2, 1));

    /* LEAVE, D series and classic. */
    open_en();
    servo_d485();
    tap_connect();
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    hold(2.1f);
    show();
    if (pstate() == HITEC_PROG_LEAVING) {
        CHECK_EQ(strcmp(line(), TR(HP_ST_LEAVE_D)), 0);
    }
    reach(HITEC_PROG_PWM);
    open_en();
    servo_hs5645();
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(1));
    tap_connect();
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    hold(2.1f);
    reach(HITEC_PROG_LEAVING);
    CHECK_EQ(strcmp(line(), TR(HP_ST_LEAVE_C)), 0);
    reach(HITEC_PROG_PWM);

    /* RELEASE PORT. */
    open_en();
    (void)open_as(CH, HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    reach(HITEC_PROG_OTHER);
    CHECK_EQ(strcmp(line(), TR(HP_HELD_OTHER)), 0);
    CHECK_EQ(hitec_prog_release(model()), HITEC_PROG_MSG_NONE);
    show();
    CHECK_EQ(strcmp(line(), TR(HP_ST_RELEASING)), 0);
    reach(HITEC_PROG_PWM);
    snprintf(want, sizeof(want), TR(HP_RELEASED), 1u);
    CHECK_EQ(strcmp(said(), want), 0);
}

TEST_CASE(an_abort_in_the_entry_leaves_the_servo_out_of_programming)
{
    open_en();
    servo_d485();
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    reach(HITEC_PROG_ENTERING);
    tap(val(HITEC_PANEL_SERVO, 0, 0));            /* ABORT */
    reach(HITEC_PROG_HELD);
    CHECK_EQ(strcmp(line(), fmt1(TX_HP_ST_NOT_IN_MODE, 1u)), 0);
    CHECK(!hitec_panel_row_live(HITEC_PANEL_SERVO, 1, 1));
}

TEST_CASE(read_all_and_abort_by_their_buttons)
{
    open_en();
    servo_d485();
    tap_connect();
    g_drv.dreg[0x32] = 3u;
    tap(val(HITEC_PANEL_SERVO, 1, 1));            /* READ ALL */
    CHECK_EQ(pstate(), HITEC_PROG_READING);
    CHECK_EQ(strcmp(hitec_panel_value(HITEC_PANEL_SERVO, 0, 0),
                    TR(KP_ABORT)),
             0);
    prun(200u);
    tap(val(HITEC_PANEL_SERVO, 0, 0));            /* ABORT */
    reach(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_ABORTED);
    CHECK_EQ(strcmp(said(), TR(HP_ABORTED)), 0);
    tap(val(HITEC_PANEL_SERVO, 1, 1));
    reach(HITEC_PROG_HELD);
    CHECK_EQ(hitec_prog_message(model())->reg, 3u);
}

TEST_CASE(a_port_without_a_pin_names_the_port_alone)
{
    open_en();
    g_pin_known = false;
    prun(50u);
    show();
    CHECK(strstr(hitec_panel_port_line(0), "PROG PORT 1  ") != NULL);
    CHECK(strstr(hitec_panel_port_line(0), "GPIO") == NULL);
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    CHECK_EQ(msg(), HITEC_PROG_MSG_NO_PIN);
}

/* --------------------------------------------------------------- editors */

TEST_CASE(more_sets_the_voltage_and_current_on_the_keypad)
{
    char want[HITEC_PANEL_LINE];

    open_en();
    CHECK(hitec_panel_row_live(HITEC_PANEL_SERVO, 2, 0));
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    CHECK_EQ(hitec_panel_editor(), 2);
    snprintf(want, sizeof(want), TR(KP_SUPPLY_V), 5u, 0u);
    CHECK_EQ(strcmp(hitec_panel_choice_label(0), want), 0);
    snprintf(want, sizeof(want), TR(KP_SUPPLY_A), 1000u);
    CHECK_EQ(strcmp(hitec_panel_choice_label(1), want), 0);
    /* 5.5V. */
    tap(hitec_panel_choice_rect(0));
    CHECK_EQ(hitec_panel_editor(), 1);
    key(UI_KEY_CLR);
    key(UI_KEY_5);
    key(UI_KEY_DOT);
    key(UI_KEY_5);
    key(UI_KEY_OK);
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_EQ(hitec_prog_mv(model()), 5500u);
    CHECK(settings_get(SET_SERVO_PROG_V) > 5.49f
          && settings_get(SET_SERVO_PROG_V) < 5.51f);
    CHECK(strstr(hitec_panel_port_line(1), "5.5V") != NULL);
    /* 500mA. */
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    tap(hitec_panel_choice_rect(1));
    CHECK_EQ(hitec_panel_editor(), 1);
    key(UI_KEY_CLR);
    key(UI_KEY_5);
    key(UI_KEY_0);
    key(UI_KEY_0);
    key(UI_KEY_OK);
    CHECK_EQ(hitec_prog_ma(model()), 500u);
    CHECK(strstr(hitec_panel_port_line(1), "500mA") != NULL);
    /* The limits: 6.0V and 4.8V take; CANCEL keeps the value. */
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    tap(hitec_panel_choice_rect(0));
    key(UI_KEY_CLR);
    key(UI_KEY_6);
    key(UI_KEY_OK);
    CHECK_EQ(hitec_prog_mv(model()), 6000u);
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    tap(hitec_panel_choice_rect(0));
    key(UI_KEY_CLR);
    key(UI_KEY_4);
    key(UI_KEY_DOT);
    key(UI_KEY_8);
    key(UI_KEY_OK);
    CHECK_EQ(hitec_prog_mv(model()), 4800u);
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    tap(hitec_panel_choice_rect(0));
    key(UI_KEY_CLR);
    key(UI_KEY_5);
    key(UI_KEY_CANCEL);
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_EQ(hitec_prog_mv(model()), 4800u);
    /* A connect takes the set points the keypad left. */
    servo_d485();
    tap_connect();
    CHECK_EQ(g_hw.v.set_mv, 4800u);
    CHECK_EQ(g_hw.v.set_ma, 500u);
}

TEST_CASE(the_home_tag_closes_an_editor_then_the_overlay)
{
    open_en();
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    CHECK_EQ(hitec_panel_editor(), 2);
    hitec_panel_back();
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK(hitec_panel_is_open());
    hitec_panel_back();
    CHECK(!hitec_panel_is_open());
    /* Over another session's port: RELEASE PORT's page, and the overlay
     * stays. */
    open_en();
    (void)open_as(CH, HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    reach(HITEC_PROG_OTHER);
    hitec_panel_back();
    CHECK(hitec_panel_is_open());
    CHECK_EQ(hitec_panel_editor(), 3);
    CHECK_EQ(strcmp(hitec_panel_page_line(0), TR(HP_RELEASE)), 0);
    hitec_panel_back();
    CHECK_EQ(hitec_panel_editor(), 0);
    tap(hitec_panel_close_rect());
    CHECK_EQ(hitec_panel_editor(), 3);
}

TEST_CASE(a_hold_the_finger_leaves_does_nothing)
{
    const gfx_rect_t r = hitec_panel_apply_rect();

    open_en();
    (void)open_as(CH, HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    reach(HITEC_PROG_OTHER);
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    CHECK_EQ(hitec_panel_editor(), 3);
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    CHECK(hitec_panel_has_press(1));
    CHECK(!hitec_panel_has_press(2));
    for (int i = 0; i < 25; ++i) {
        hitec_panel_tick(0.02f);
    }
    ev(cx(r), r.y - 40, TOUCH_EVENT_MOVE);
    CHECK(!hitec_panel_has_press(1));
    for (int i = 0; i < 150; ++i) {
        hitec_panel_tick(0.02f);
        prun(20u);
    }
    ev(cx(r), r.y - 40, TOUCH_EVENT_UP);
    CHECK_EQ(pstate(), HITEC_PROG_OTHER);
    CHECK_EQ(hitec_panel_editor(), 3);
    /* Touch events lost: the press goes. */
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    hitec_panel_cancel();
    CHECK(!hitec_panel_has_press(1));
}

/* ---------------------------------------------------------------- values */

/* The D485HW with @p id set to @p value, on the line. */
static void d485_with(uint8_t id, int32_t value)
{
    hitec_image_t img;

    d485(&img);
    CHECK_EQ(hitec_param_set(&img, HITEC_LAYOUT_D, id, value), HITEC_SET_OK);
    for (unsigned a = 0; a < 256u; a += 2u) {
        g_drv.dreg[a] = hitec_image_reg(&img, (uint8_t)a);
    }
}

TEST_CASE(fail_safe_overload_and_a_raw_value_show_as_read)
{
    /* A fail-safe position. */
    open_en();
    d485_with(HITEC_P_D_FAILSAFE, 1500);
    tap_connect();
    tap(hitec_panel_tab_rect(HITEC_PANEL_PROTECT));
    CHECK(strstr(hitec_panel_value(HITEC_PANEL_PROTECT, 0, 0), "1500us")
          != NULL);
    /* LIMP. */
    open_en();
    d485_with(HITEC_P_D_FAILSAFE, HITEC_FS_LIMP);
    tap_connect();
    tap(hitec_panel_tab_rect(HITEC_PANEL_PROTECT));
    CHECK(strstr(hitec_panel_value(HITEC_PANEL_PROTECT, 0, 0), TR(HP_FS_LIMP))
          != NULL);
    /* Overload protection at 30%. */
    open_en();
    d485_with(HITEC_P_D_OVERLOAD, 30);
    tap_connect();
    tap(hitec_panel_tab_rect(HITEC_PANEL_PROTECT));
    CHECK(strstr(hitec_panel_value(HITEC_PANEL_PROTECT, 1, 0), "30%") != NULL);
    /* A speed that fits no encoding: RAW. */
    open_en();
    servo_d485();
    g_drv.dreg[0x54] = 0xFFFFu;
    tap_connect();
    tap(hitec_panel_tab_rect(HITEC_PANEL_MOTION));
    CHECK_EQ(strncmp(hitec_panel_value(HITEC_PANEL_MOTION, 0, 0), "RAW ", 4),
             0);
}

TEST_CASE(a_classic_resolution_and_an_unknown_name_show_as_read)
{
    hitec_image_t img;

    /* An HS-7955TG at resolution HIGH. */
    open_en();
    hs7955(&img);
    for (unsigned c = 0; c < 256u; ++c) {
        g_drv.eeprom[c] = hitec_image_cell(&img, (uint8_t)c);
    }
    /* Cell 0x22: 01 is High (read only, so set on the servo itself). */
    g_drv.eeprom[0x22] = 0x01u;
    g_drv.c_entry = true;
    g_drv.boot_byte = HITEC_C_ACK;
    g_drv.boot_ns = 59000000u;
    g_drv.version = 0x26u;
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(1));
    tap_connect();
    CHECK_EQ(hitec_prog_model(model())->model, HITEC_MODEL_HS7955TG);
    tap(hitec_panel_tab_rect(HITEC_PANEL_MOTION));
    CHECK(strstr(hitec_panel_value(HITEC_PANEL_MOTION, 2, 0), TR(HP_RES_HIGH))
          != NULL);

    /* A classic servo whose name no model has: the name as read, and the
     * layout it is read in said. */
    open_en();
    servo_hs5645();
    memcpy(&g_drv.eeprom[0x60], "XQ1234\0\0\0\0\0\0\0\0\0\0", 16);
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(1));
    tap_connect();
    CHECK_EQ(strcmp(hitec_panel_label(HITEC_PANEL_SERVO, 1, 1), "XQ1234"), 0);
    tap(hitec_panel_tab_rect(HITEC_PANEL_INFO));
    CHECK_EQ(strcmp(hitec_panel_value(HITEC_PANEL_INFO, 0, 0), "XQ1234"), 0);
}

TEST_CASE(a_d_series_servo_no_model_has_is_named_by_its_number)
{
    bool guess = false;
    char want[HITEC_PANEL_LINE];

    open_en();
    servo_d485();
    g_drv.dreg[2u * HITEC_D_REG_MODEL] = 12345u;
    tap_connect();
    CHECK_EQ(hitec_prog_model(model())->number, 12345u);
    CHECK_EQ(strcmp(hitec_panel_label(HITEC_PANEL_SERVO, 1, 1),
                    "SERVO 12345"),
             0);
    snprintf(want, sizeof(want), TR(HP_LAYOUT_GUESS), "D485HW");
    for (int i = 0; hitec_panel_note(i) != NULL; ++i) {
        guess |= strcmp(hitec_panel_note(i), want) == 0;
    }
    CHECK(guess);
}

TEST_CASE(a_classic_checksum_that_does_not_balance_is_said)
{
    bool sum_bad = false;

    open_en();
    servo_hs5645();
    g_drv.eeprom[HITEC_C_SUM_LAST] ^= 0x01u;
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(1));
    tap_connect();
    for (int i = 0; hitec_panel_note(i) != NULL; ++i) {
        sum_bad |= strcmp(hitec_panel_note(i), TR(HP_SUM_BAD)) == 0;
    }
    CHECK(sum_bad);
}

TEST_CASE(a_voltage_above_6v_is_said_capped_and_a_classic_name_waits)
{
    bool capped = false;

    open_en();
    settings_set(SET_SERVO_PROG_V, 7.4f);
    hitec_panel_init(AREA);
    hitec_panel_open();
    prun(20u);
    show();
    CHECK(hitec_prog_mv_capped(model()));
    for (int i = 0; hitec_panel_note(i) != NULL; ++i) {
        capped |= strcmp(hitec_panel_note(i), TR(HP_V_CAPPED)) == 0;
    }
    CHECK(capped);
    /* The classic name row before a read. */
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(1));
    tap(hitec_panel_tab_rect(HITEC_PANEL_INFO));
    CHECK_EQ(strcmp(hitec_panel_value(HITEC_PANEL_INFO, 0, 0), "--"), 0);
}

TEST_CASE(a_message_the_state_line_says_is_said_once)
{
    int times = 0;

    open_en();
    (void)open_as(CH, HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
    until(HITEC_PROG_OTHER);
    CHECK_EQ(hitec_prog_connect(model()), HITEC_PROG_MSG_HELD_OTHER);
    show();
    for (int i = 0; hitec_panel_note(i) != NULL; ++i) {
        times += strcmp(hitec_panel_note(i), TR(HP_HELD_OTHER)) == 0;
    }
    CHECK_EQ(times, 1);
}

TEST_CASE(the_registry_hooks_count_what_a_render_shows)
{
    uint32_t rev;
    uint32_t hold_rev;

    open_en();
    servo_d485();
    /* Never a safe spot: programming and an armed bench are exclusive. */
    CHECK(!hitec_panel_safe_at(cx(AREA), cy(AREA)));
    /* No event, or the overlay closed: nothing changes. */
    rev = hitec_panel_revision();
    hitec_panel_event(NULL);
    CHECK_EQ(hitec_panel_revision(), rev);
    hitec_panel_leave();
    rev = hitec_panel_revision();
    ev(cx(AREA), cy(AREA), TOUCH_EVENT_DOWN);
    CHECK_EQ(hitec_panel_revision(), rev);
    hitec_panel_open();
    /* A press that moves on its button still acts where it lifts. */
    rev = hitec_panel_revision();
    ev(cx(val(HITEC_PANEL_SERVO, 1, 0)), cy(val(HITEC_PANEL_SERVO, 1, 0)),
       TOUCH_EVENT_DOWN);
    ev(cx(val(HITEC_PANEL_SERVO, 1, 0)) + 2,
       cy(val(HITEC_PANEL_SERVO, 1, 0)), TOUCH_EVENT_MOVE);
    CHECK_EQ(hitec_panel_editor(), 0);
    ev(cx(val(HITEC_PANEL_SERVO, 1, 0)) + 2,
       cy(val(HITEC_PANEL_SERVO, 1, 0)), TOUCH_EVENT_UP);
    CHECK_EQ(hitec_panel_editor(), 2);
    CHECK(hitec_panel_revision() != rev);
    tap(hitec_panel_choice_cancel_rect());
    /* A hold counts on its own revision. */
    tap_connect();
    tap(hitec_panel_close_rect());
    CHECK(hitec_panel_page_line(-1) == NULL);
    CHECK(hitec_panel_page_line(99) == NULL);
    hold_rev = hitec_panel_hold_revision();
    hold(0.5f);
    CHECK(hitec_panel_hold_revision() != hold_rev);
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
}

TEST_CASE(the_state_line_names_a_d_series_leave)
{
    gfx_rect_t r;

    open_en();
    servo_d485();
    tap_connect();
    tap(hitec_panel_close_rect());
    r = hitec_panel_apply_rect();
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    for (int i = 0; i < 150 && pstate() == HITEC_PROG_HELD; ++i) {
        hitec_panel_tick(0.02f);
        if (pstate() == HITEC_PROG_HELD) {
            prun(20u);
        }
    }
    ev(cx(r), cy(r), TOUCH_EVENT_UP);
    show();
    CHECK_EQ(pstate(), HITEC_PROG_LEAVING);
    CHECK_EQ(strcmp(line(), TR(HP_ST_LEAVE_D)), 0);
    until(HITEC_PROG_PWM);
}

TEST_CASE(a_note_that_does_not_fit_above_the_message_is_not_drawn)
{
    /* An area 200px short of the overlay's: the notes meet the message. */
    const gfx_rect_t shorter = { AREA.x, AREA.y, AREA.w,
                                 (int16_t)(AREA.h - 200) };
    int n = 0;
    bool unsafe = false;
    gfx_rect_t d;

    open_en();
    hitec_panel_init(shorter);
    hitec_panel_open();
    rig_at(SUPPLY_ON, true);
    servo_d485();
    tap_connect();
    disarm();
    prun(200u);
    g_safe = false;
    (void)hitec_prog_clear(model());
    until(HITEC_PROG_HELD);
    show();
    while (hitec_panel_note(n) != NULL) {
        unsafe |= strcmp(hitec_panel_note(n), TR(HP_UNSAFE)) == 0;
        ++n;
    }
    /* The message stays; a note that does not fit above it gives way. */
    CHECK(unsafe);
    d = hitec_panel_notes_drawn();
    CHECK(d.y + d.h <= shorter.y + shorter.h);
}

int main(void)
{
    RUN(the_state_line_follows_the_link);
    RUN(the_state_line_names_each_entry_and_the_read);
    RUN(the_state_line_names_clear_leave_and_release);
    RUN(an_abort_in_the_entry_leaves_the_servo_out_of_programming);
    RUN(read_all_and_abort_by_their_buttons);
    RUN(a_port_without_a_pin_names_the_port_alone);
    RUN(more_sets_the_voltage_and_current_on_the_keypad);
    RUN(the_home_tag_closes_an_editor_then_the_overlay);
    RUN(a_hold_the_finger_leaves_does_nothing);
    RUN(fail_safe_overload_and_a_raw_value_show_as_read);
    RUN(a_classic_resolution_and_an_unknown_name_show_as_read);
    RUN(a_d_series_servo_no_model_has_is_named_by_its_number);
    RUN(a_classic_checksum_that_does_not_balance_is_said);
    RUN(a_voltage_above_6v_is_said_capped_and_a_classic_name_waits);
    RUN(a_message_the_state_line_says_is_said_once);
    RUN(the_registry_hooks_count_what_a_render_shows);
    RUN(the_state_line_names_a_d_series_leave);
    RUN(a_note_that_does_not_fit_above_the_message_is_not_drawn);
    free(fb);
    return test_summary("hitec_panel_states");
}
