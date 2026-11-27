/*
 * The servo programmers' frame (sprog.c): the presses, the row holds and
 * their due, the step keys' repeat, STOP from any contact, the tabs and
 * the editors' results, on the fake brand of sprog_rig.h.
 *
 * Every press kind: a tap acts on its release inside the control, by the
 * view at the release; a hold is due on the tick after it completes and a
 * touch loss before that takes it back; a stop acts on the DOWN, from a
 * second contact too, and a row's press held by another contact loses its
 * tap; an off control takes nothing, one with a why shows it and returns
 * REFUSED; a step steps on the press and repeats while held.
 *
 * SPDX-License-Identifier: MIT
 */
#include "sprog_rig.h"

/* A tap acts on the release inside the control, by the view at the
 * release; a release elsewhere, or one that is no tap, acts on nothing. */
TEST_CASE(a_tap_acts_on_its_release_inside_the_control)
{
    rig_start();
    sprog_tab_set(&rig.f, 1);
    rig_view();
    const gfx_rect_t v = sprog_value_rect(&rig.f, R_SPEED);

    CHECK_EQ(rig_down(v, 1).kind, SPROG_R_NONE);
    CHECK(sprog_has_press(&rig.f, 1));
    CHECK(!sprog_has_press(&rig.f, 2));
    sprog_result_t r = rig_up(v, 1);
    CHECK_EQ(r.kind, SPROG_R_TAP);
    CHECK_EQ(r.row, R_SPEED);
    CHECK_EQ(r.key, 0);
    CHECK(!sprog_has_press(&rig.f, 1));
    /* Released off the control. */
    (void)rig_down(v, 1);
    CHECK_EQ(rig_ev(TOUCH_EVENT_UP, v.x - 30, rig_cy(v), 1).kind,
             SPROG_R_NONE);
    /* A release that is no tap: a lost touch. */
    (void)rig_down(v, 1);
    touch_event_t e = { TOUCH_EVENT_UP, { 1, (int16_t)rig_cx(v),
                                          (int16_t)rig_cy(v), 0 },
                        TOUCH_FLAG_NO_TAP };
    CHECK_EQ(sprog_event(&rig.f, &e).kind, SPROG_R_NONE);
    /* The control went off between the press and the release. */
    (void)rig_down(v, 1);
    rig_cell(R_SPEED, SPROG_K_VALUE, SPROG_P_OFF);
    CHECK_EQ(rig_up(v, 1).kind, SPROG_R_NONE);
    /* An off control takes no press at all. */
    CHECK_EQ(rig_down(v, 1).kind, SPROG_R_NONE);
    CHECK(!sprog_has_press(&rig.f, 1));
}

/* A control with a why refuses at the press and takes nothing, whatever
 * its kind; an off one with a why as well. */
TEST_CASE(a_why_refuses_at_the_press)
{
    rig_start();
    rig.cell[R_WRITE].why = "Tap DISARM first.";
    sprog_result_t r = rig_down(sprog_value_rect(&rig.f, R_WRITE), 1);
    CHECK_EQ(r.kind, SPROG_R_REFUSED);
    CHECK_EQ(r.row, R_WRITE);
    CHECK(!rig.f.have);
    CHECK(!rig.f.hold.down);
    rig_cell(R_READ, SPROG_K_ACT, SPROG_P_OFF)->why = "Not read.";
    CHECK_EQ(rig_down(sprog_value_rect(&rig.f, R_READ), 1).kind,
             SPROG_R_REFUSED);
    /* A step key and a pair key with a why. */
    sprog_tab_set(&rig.f, 1);
    rig_cell(R_DEAD, SPROG_K_STEP, SPROG_P_TAP)->why = "Locked.";
    r = rig_down(sprog_step_rect(&rig.f, R_DEAD, true), 1);
    CHECK_EQ(r.kind, SPROG_R_REFUSED);
    CHECK_EQ(r.key, 1);
    rig_cell(R_SPEED, SPROG_K_PAIR, SPROG_P_TAP)->why = "Off.";
    rig_view();
    r = rig_down(sprog_pair_rect(&rig.f, R_SPEED, 1), 1);
    CHECK_EQ(r.kind, SPROG_R_REFUSED);
    CHECK_EQ(r.key, 1);
    CHECK(!rig.f.have);
}

/* The tabs: a tap on another tab shows it and says so; a mask that hides
 * the tab shown goes back to the first, closes the editors and drops the
 * press. */
TEST_CASE(the_tabs_follow_taps_and_the_mask)
{
    rig_start();
    CHECK_EQ(rig.f.tabs.count, 2);
    sprog_result_t r = rig_tap(sprog_tab_rect(&rig.f, 1));
    CHECK_EQ(r.kind, SPROG_R_TAB);
    CHECK_EQ(r.key, 1);
    CHECK_EQ(sprog_tab(&rig.f), 1);
    /* The same tab again changes nothing. */
    CHECK_EQ(rig_tap(sprog_tab_rect(&rig.f, 1)).kind, SPROG_R_NONE);
    sprog_list_open(&rig.f, 3, "TYPE", false);
    (void)sprog_list_add(&rig.f, "V6", NULL, 6);
    rig.mask = M_SLOT;
    rig_view();
    CHECK_EQ(rig.f.tabs.count, 1);
    CHECK_EQ(sprog_tab(&rig.f), 0);
    CHECK(!rig.f.list.open);
    CHECK_EQ(sprog_tab_rect(&rig.f, 1).w, 0);
    /* A tab not shown is not set. */
    sprog_tab_set(&rig.f, 1);
    CHECK_EQ(sprog_tab(&rig.f), 0);
    /* Back again: the tab shown stays where it is. */
    rig.mask = M_ALL;
    rig_view();
    CHECK_EQ(rig.f.tabs.count, 2);
    CHECK_EQ(sprog_tab(&rig.f), 0);
}

/* A hold: the press returns HOLD_BEGIN, 2s complete it, and it is due on
 * the tick after the one it completes on. */
TEST_CASE(a_hold_is_due_on_the_tick_after_it_completes)
{
    rig_start();
    const gfx_rect_t w = sprog_value_rect(&rig.f, R_WRITE);
    sprog_result_t r = rig_down(w, 1);
    CHECK_EQ(r.kind, SPROG_R_HOLD_BEGIN);
    CHECK_EQ(r.row, R_WRITE);
    CHECK(rig.f.hold.down);
    CHECK_EQ(rig.f.hold.hint, TX_HOLD_HINT_WRITE);
    const uint32_t hr = sprog_hold_revision(&rig.f);
    CHECK_EQ(rig_run(1.0f).kind, SPROG_R_NONE);
    CHECK(sprog_hold_revision(&rig.f) > hr);
    CHECK_EQ(sprog_hold_rect(&rig.f).x, w.x);
    int ticks = 0;
    while (!rig.f.hold_due && ticks < 100) {
        CHECK_EQ(rig_tick(0.05f).kind, SPROG_R_NONE);
        ++ticks;
    }
    CHECK(rig.f.hold_due);
    CHECK(ui_hold_flashing(&rig.f.hold));
    r = rig_tick(0.05f);
    CHECK_EQ(r.kind, SPROG_R_HOLD_DUE);
    CHECK_EQ(r.row, R_WRITE);
    CHECK_EQ(rig_tick(0.05f).kind, SPROG_R_NONE);
    /* The release of the press that fired shows no hint and taps nothing. */
    CHECK_EQ(rig_up(w, 1).kind, SPROG_R_NONE);
    CHECK(!rig.f.have);
    /* The flash ends on its own. */
    (void)rig_run(1.0f);
    CHECK(!ui_hold_flashing(&rig.f.hold));
    CHECK_EQ(sprog_hold_rect(&rig.f).x, sprog_apply_rect(&rig.f).x);
}

/* A touch loss between the completion and the due takes the hold back. */
TEST_CASE(a_touch_loss_before_the_due_cancels_the_hold)
{
    rig_start();
    (void)rig_down(sprog_value_rect(&rig.f, R_HEAD), 1);
    int ticks = 0;
    while (!rig.f.hold_due && ticks < 100) {
        (void)rig_tick(0.05f);
        ++ticks;
    }
    CHECK(rig.f.hold_due);
    sprog_cancel(&rig.f);
    CHECK(!rig.f.have);
    CHECK_EQ(rig_run(0.5f).kind, SPROG_R_NONE);
}

/* A short release, a finger that leaves the control, the control that
 * stops being a hold, an editor, and a why that comes up: no due. */
TEST_CASE(a_hold_ends_without_a_due)
{
    rig_start();
    const gfx_rect_t w = sprog_value_rect(&rig.f, R_WRITE);

    (void)rig_down(w, 1);
    (void)rig_run(0.5f);
    CHECK_EQ(rig_up(w, 1).kind, SPROG_R_NONE);
    CHECK_EQ(rig_run(2.5f).kind, SPROG_R_NONE);

    (void)rig_down(w, 1);
    (void)rig_ev(TOUCH_EVENT_MOVE, rig_cx(w), rig_cy(w), 1);
    CHECK(rig.f.have);
    (void)rig_ev(TOUCH_EVENT_MOVE, w.x - 20, rig_cy(w), 1);
    CHECK(!rig.f.have);
    CHECK_EQ(rig_run(2.5f).kind, SPROG_R_NONE);

    (void)rig_down(w, 1);
    rig.cell[R_WRITE].press = SPROG_P_TAP;
    (void)rig_tick(0.05f);
    CHECK(!rig.f.have);
    rig.cell[R_WRITE].press = SPROG_P_HOLD;
    CHECK_EQ(rig_run(2.5f).kind, SPROG_R_NONE);

    (void)rig_down(w, 1);
    sprog_keypad(&rig.f, 1, "X", "", 1.0f, 0.0f, 9.0f, 0);
    (void)rig_tick(0.05f);
    CHECK(!rig.f.have);
    sprog_close_editors(&rig.f);

    (void)rig_down(w, 1);
    rig.cell[R_WRITE].why = "Card pulled.";
    (void)rig_tick(0.05f);
    CHECK(!rig.f.have);
    CHECK(!rig.f.hold.down);
    rig.cell[R_WRITE].why = NULL;
    CHECK_EQ(rig_run(2.5f).kind, SPROG_R_NONE);

    /* The row gone from the tab shown. */
    (void)rig_down(w, 1);
    sprog_tab_set(&rig.f, 1);
    (void)rig_tick(0.05f);
    CHECK(!rig.f.have);
    CHECK_EQ(rig_run(2.5f).kind, SPROG_R_NONE);
}

/* A brand refuses HOLD_BEGIN: the press and the hold go before any of it
 * shows, and nothing is due. */
TEST_CASE(a_refused_hold_begin_starts_no_hold)
{
    rig_start();
    const gfx_rect_t w = sprog_value_rect(&rig.f, R_WRITE);

    CHECK_EQ(rig_down(w, 1).kind, SPROG_R_HOLD_BEGIN);
    sprog_refuse(&rig.f);
    CHECK(!rig.f.have);
    CHECK(!rig.f.hold.down);
    CHECK(rig.f.hold.held_s == 0.0f);
    CHECK_EQ(rig_run(2.5f).kind, SPROG_R_NONE);
    /* The contact's release finds no press. */
    CHECK_EQ(rig_up(w, 1).kind, SPROG_R_NONE);
}

/* sprog_due(): the footer's WRITE n VALUES is due on the next tick with no
 * press; a press or an editor refuses it; a touch loss takes it back. */
TEST_CASE(a_due_asked_from_outside_comes_on_the_next_tick)
{
    rig_start();
    CHECK(sprog_due(&rig.f, R_WRITE));
    sprog_result_t r = rig_tick(0.05f);
    CHECK_EQ(r.kind, SPROG_R_HOLD_DUE);
    CHECK_EQ(r.row, R_WRITE);
    CHECK(sprog_due(&rig.f, R_WRITE));
    sprog_cancel(&rig.f);
    CHECK_EQ(rig_tick(0.05f).kind, SPROG_R_NONE);
    (void)rig_down(sprog_value_rect(&rig.f, R_READ), 1);
    CHECK(!sprog_due(&rig.f, R_WRITE));
    (void)rig_up(sprog_value_rect(&rig.f, R_READ), 1);
    sprog_page_open(&rig.f, 1, false, "T", "HOLD", "CANCEL", -1, false);
    CHECK(!sprog_due(&rig.f, R_WRITE));
    sprog_close_editors(&rig.f);
    CHECK(!sprog_due(&rig.f, R_COUNT));
    rig.f.open = false;
    CHECK(!sprog_due(&rig.f, R_WRITE));
}

/* A step steps on the press and repeats while held, at ui_key_repeat()'s
 * rates; a finger off the key, a page and a refusal end the repeat. */
TEST_CASE(a_step_steps_on_the_press_and_repeats)
{
    rig_start();
    sprog_tab_set(&rig.f, 1);
    rig_view();
    const gfx_rect_t plus = sprog_step_rect(&rig.f, R_DEAD, true);
    const gfx_rect_t minus = sprog_step_rect(&rig.f, R_DEAD, false);
    sprog_result_t r = rig_down(plus, 1);
    CHECK_EQ(r.kind, SPROG_R_STEP);
    CHECK_EQ(r.row, R_DEAD);
    CHECK_EQ(r.key, 1);
    CHECK_EQ(r.value, 1);
    /* The repeats over 1.5s, against ui_key_repeat() itself. */
    float held = 0.0f;
    int want = 0, got = 0;
    for (int i = 0; i < 30; ++i) {
        want += ui_key_repeat(&held, 0.05f);
        r = rig_tick(0.05f);
        if (r.kind == SPROG_R_STEP) {
            CHECK_EQ(r.key, 1);
            got += r.value;
        }
    }
    CHECK(want >= 8);
    CHECK_EQ(got, want);
    (void)rig_up(plus, 1);
    CHECK_EQ(rig_run(1.0f).kind, SPROG_R_NONE);
    /* The minus key, and a finger that slides off it. */
    r = rig_down(minus, 1);
    CHECK_EQ(r.key, -1);
    (void)rig_ev(TOUCH_EVENT_MOVE, minus.x - 20, rig_cy(minus), 1);
    CHECK(!rig.f.have);
    CHECK_EQ(rig_run(1.0f).kind, SPROG_R_NONE);
    /* A page the step opened takes over. */
    (void)rig_down(plus, 1);
    sprog_page_open(&rig.f, 1, false, "CHECK", "HOLD", "CANCEL", -1, false);
    (void)rig_tick(0.05f);
    CHECK(!rig.f.have || rig.f.on != SPROG_ON_PLUS);
    sprog_close_editors(&rig.f);
    (void)rig_up(plus, 1);
    /* The brand refuses the step. */
    (void)rig_down(plus, 1);
    sprog_refuse(&rig.f);
    CHECK_EQ(rig_run(1.0f).kind, SPROG_R_NONE);
    /* A step row that goes off ends the repeat. */
    (void)rig_down(plus, 1);
    rig_cell(R_DEAD, SPROG_K_STEP, SPROG_P_OFF);
    (void)rig_tick(0.05f);
    CHECK(!rig.f.have);
    /* An off step takes no press. */
    CHECK_EQ(rig_down(plus, 1).kind, SPROG_R_NONE);
}

/* STOP acts on the DOWN: from the first contact its press taps nothing,
 * from a second contact a row's press loses its tap, a hold's stays. */
TEST_CASE(a_stop_acts_on_the_press_from_any_contact)
{
    rig_start();
    rig_cell(R_HEAD, SPROG_K_HEAD, SPROG_P_STOP)->fill = SPROG_F_ACTIVE;
    rig.cell[R_HEAD].value = "ABORT";
    rig_view();
    const gfx_rect_t h = sprog_value_rect(&rig.f, R_HEAD);
    const gfx_rect_t rd = sprog_value_rect(&rig.f, R_READ);

    CHECK(sprog_safe_at(&rig.f, rig_cx(h), rig_cy(h)));
    CHECK(!sprog_safe_at(&rig.f, rig_cx(rd), rig_cy(rd)));
    sprog_result_t r = rig_down(h, 1);
    CHECK_EQ(r.kind, SPROG_R_STOP);
    CHECK_EQ(r.row, R_HEAD);
    CHECK(rig.f.have);
    CHECK_EQ(rig_up(h, 1).kind, SPROG_R_NONE);
    /* A second contact while a row's press is held. */
    (void)rig_down(rd, 1);
    r = rig_down(h, 2);
    CHECK_EQ(r.kind, SPROG_R_STOP);
    CHECK(!rig.f.have);
    CHECK_EQ(rig_up(rd, 1).kind, SPROG_R_NONE);
    CHECK_EQ(rig_up(h, 2).kind, SPROG_R_NONE);
    /* A second contact on anything else takes nothing. */
    (void)rig_down(rd, 1);
    CHECK_EQ(rig_down(sprog_value_rect(&rig.f, R_MORE), 2).kind,
             SPROG_R_NONE);
    CHECK(sprog_has_press(&rig.f, 1));
    CHECK_EQ(rig_up(rd, 1).kind, SPROG_R_TAP);
    /* A hold's press stays its contact's. */
    const gfx_rect_t w = sprog_value_rect(&rig.f, R_WRITE);
    (void)rig_down(w, 1);
    CHECK_EQ(rig_down(h, 2).kind, SPROG_R_STOP);
    CHECK(sprog_has_press(&rig.f, 1));
    CHECK_EQ(rig.f.on, SPROG_ON_HOLD);
    (void)rig_up(w, 1);
    /* An overlay covers the stop. */
    sprog_list_open(&rig.f, 1, "MORE", false);
    CHECK(!sprog_safe_at(&rig.f, rig_cx(h), rig_cy(h)));
    sprog_close_editors(&rig.f);
    rig.f.open = false;
    CHECK(!sprog_safe_at(&rig.f, rig_cx(h), rig_cy(h)));
    CHECK_EQ(rig_down(h, 1).kind, SPROG_R_NONE);
}

/* A pair's two keys, each with its own press: a tap, a stop, off. */
TEST_CASE(a_pairs_keys_take_their_own_presses)
{
    rig_start();
    sprog_tab_set(&rig.f, 1);
    rig_cell(R_SPEED, SPROG_K_PAIR, SPROG_P_TAP)->press2 = SPROG_P_STOP;
    rig.cell[R_SPEED].value = "LEFT";
    rig.cell[R_SPEED].value2 = "RIGHT";
    rig_view();
    const gfx_rect_t k0 = sprog_pair_rect(&rig.f, R_SPEED, 0);
    const gfx_rect_t k1 = sprog_pair_rect(&rig.f, R_SPEED, 1);

    CHECK(k0.x + k0.w < k1.x);
    CHECK_EQ(k1.x + k1.w, sprog_value_rect(&rig.f, R_SPEED).x
                              + sprog_value_rect(&rig.f, R_SPEED).w);
    sprog_result_t r = rig_tap(k0);
    CHECK_EQ(r.kind, SPROG_R_TAP);
    CHECK_EQ(r.key, 0);
    CHECK(sprog_safe_at(&rig.f, rig_cx(k1), rig_cy(k1)));
    r = rig_down(k1, 1);
    CHECK_EQ(r.kind, SPROG_R_STOP);
    CHECK_EQ(r.key, 1);
    (void)rig_up(k1, 1);
    /* Released on the other key: nothing. */
    (void)rig_down(k0, 1);
    CHECK_EQ(rig_up(k1, 1).kind, SPROG_R_NONE);
    /* An off key takes nothing. */
    rig.cell[R_SPEED].press = SPROG_P_OFF;
    CHECK_EQ(rig_down(k0, 1).kind, SPROG_R_NONE);
    CHECK(!rig.f.have);
    /* A tap key that turns off before the release. */
    rig.cell[R_SPEED].press2 = SPROG_P_TAP;
    (void)rig_down(k1, 1);
    rig.cell[R_SPEED].press2 = SPROG_P_OFF;
    CHECK_EQ(rig_up(k1, 1).kind, SPROG_R_NONE);
}

/* The head's text opens PORT where the view offers it. */
TEST_CASE(the_head_text_opens_the_port_list_where_offered)
{
    rig_start();
    const gfx_rect_t t = sprog_head_rect(&rig.f);

    CHECK(t.w > 0);
    CHECK_EQ(rig_tap(t).kind, SPROG_R_NONE);
    rig.port_pick = true;
    CHECK_EQ(rig_tap(t).kind, SPROG_R_PORT);
    (void)rig_down(t, 1);
    CHECK_EQ(rig_up(sprog_value_rect(&rig.f, R_READ), 1).kind, SPROG_R_NONE);
    /* No head on the tab shown. */
    sprog_tab_set(&rig.f, 1);
    rig_view();
    CHECK_EQ(sprog_head_rect(&rig.f).w, 0);
}

/** @brief A tap on key @p key of the keypad. */
static sprog_result_t kp_key(ui_key_t key)
{
    return rig_tap(ui_keypad_key_rect(&rig.f.kp, key));
}

/* The keypad: ENTER returns the brand's code and the value x 10^decimals,
 * CANCEL its code. */
TEST_CASE(the_keypad_returns_its_code_and_value)
{
    rig_start();
    sprog_keypad(&rig.f, 42, "SPEED", "%", 50.0f, 0.0f, 100.0f, 1);
    CHECK(rig.f.kp.open);
    (void)kp_key(UI_KEY_1);
    (void)kp_key(UI_KEY_2);
    (void)kp_key(UI_KEY_DOT);
    (void)kp_key(UI_KEY_5);
    sprog_result_t r = kp_key(UI_KEY_OK);
    CHECK_EQ(r.kind, SPROG_R_KEYPAD);
    CHECK_EQ(r.what, 42);
    CHECK_EQ(r.value, 125);
    CHECK(!rig.f.kp.open);
    sprog_keypad(&rig.f, 43, "WAIT", "ms", 50.0f, 0.0f, 100.0f, 0);
    r = kp_key(UI_KEY_CANCEL);
    CHECK_EQ(r.kind, SPROG_R_KEYPAD_CANCEL);
    CHECK_EQ(r.what, 43);
    CHECK(sprog_revision(&rig.f) > 0u);
}

/* The keyboard: OK returns the name in f->name, CANCEL says so. */
TEST_CASE(the_keyboard_returns_the_name)
{
    rig_start();
    sprog_name(&rig.f, "NAME", "HAND");
    CHECK(rig.f.tk.open);
    sprog_result_t r = rig_tap(ui_textkey_key_rect(&rig.f.tk, UI_TK_OK));
    CHECK_EQ(r.kind, SPROG_R_NAME);
    CHECK_STR_EQ(rig.f.name, "HAND");
    sprog_name(&rig.f, "NAME", "X");
    r = rig_tap(ui_textkey_key_rect(&rig.f.tk, UI_TK_CANCEL));
    CHECK_EQ(r.kind, SPROG_R_NAME_CANCEL);
    CHECK(!rig.f.tk.open);
}

/* A list: an entry returns the list's code and its value and closes the
 * list; CANCEL its code; a release off the entry nothing. */
TEST_CASE(a_list_returns_the_entry_chosen)
{
    rig_start();
    sprog_list_open(&rig.f, 9, "TYPE", false);
    CHECK(sprog_list_add(&rig.f, "V6", NULL, 6));
    CHECK(sprog_list_add(&rig.f, "V8", NULL, 8));
    sprog_list_current(&rig.f, 8);
    (void)rig_down(sprog_list_rect(&rig.f, 0), 1);
    CHECK_EQ(rig_up(sprog_list_rect(&rig.f, 1), 1).kind, SPROG_R_NONE);
    CHECK(rig.f.list.open);
    sprog_result_t r = rig_tap(sprog_list_rect(&rig.f, 1));
    CHECK_EQ(r.kind, SPROG_R_LIST);
    CHECK_EQ(r.what, 9);
    CHECK_EQ(r.value, 8);
    CHECK(!rig.f.list.open);
    sprog_list_open(&rig.f, 10, "MORE", false);
    (void)sprog_list_add(&rig.f, "VERIFY", NULL, 0);
    /* A press outside every entry and CANCEL takes nothing. */
    CHECK_EQ(rig_ev(TOUCH_EVENT_DOWN, 300, 300, 1).kind, SPROG_R_NONE);
    CHECK(!rig.f.have);
    (void)rig_down(sprog_list_cancel_rect(&rig.f), 1);
    CHECK_EQ(rig_up(sprog_list_rect(&rig.f, 0), 1).kind, SPROG_R_NONE);
    r = rig_tap(sprog_list_cancel_rect(&rig.f));
    CHECK_EQ(r.kind, SPROG_R_LIST_CANCEL);
    CHECK_EQ(r.what, 10);
    CHECK(!rig.f.list.open);
}

/* A page's hold: PAGE_APPLY on the tick it completes, the page closed,
 * its flash running, the release tapping nothing; a short release
 * nothing; CANCEL its code. */
TEST_CASE(a_pages_hold_applies_and_cancel_cancels)
{
    rig_start();
    sprog_page_open(&rig.f, 5, true, "RESTORE", "HOLD 2s TO RESTORE",
                    "CANCEL", TX_HOLD_HINT_APPLY, false);
    const gfx_rect_t a = sprog_apply_rect(&rig.f);

    (void)rig_down(a, 1);
    CHECK(rig.f.page.down);
    (void)rig_run(0.5f);
    CHECK_EQ(rig_up(a, 1).kind, SPROG_R_NONE);
    CHECK(rig.f.page.open);
    CHECK(!rig.f.page.down);
    /* A finger that leaves the button abandons the hold. */
    (void)rig_down(a, 1);
    (void)rig_ev(TOUCH_EVENT_MOVE, a.x + a.w + 40, rig_cy(a), 1);
    CHECK(!rig.f.page.down);
    CHECK(!rig.f.have);
    (void)rig_down(a, 1);
    sprog_result_t got = { 0 };
    for (int i = 0; i < 60 && got.kind == SPROG_R_NONE; ++i) {
        got = rig_tick(0.05f);
    }
    CHECK_EQ(got.kind, SPROG_R_PAGE_APPLY);
    CHECK_EQ(got.what, 5);
    CHECK(!rig.f.page.open);
    CHECK(ui_hold_flashing(&rig.f.page.hold));
    CHECK_EQ(rig_up(a, 1).kind, SPROG_R_NONE);
    (void)rig_run(1.0f);
    CHECK(!ui_hold_flashing(&rig.f.page.hold));
    /* CANCEL. */
    sprog_page_open(&rig.f, 6, false, "LEAVE", "HOLD", "BACK", -1, true);
    CHECK_EQ(rig_ev(TOUCH_EVENT_DOWN, 300, 200, 1).kind, SPROG_R_NONE);
    CHECK(!rig.f.have);
    (void)rig_down(sprog_page_cancel_rect(&rig.f), 1);
    CHECK_EQ(rig_up(a, 1).kind, SPROG_R_NONE);
    got = rig_tap(sprog_page_cancel_rect(&rig.f));
    CHECK_EQ(got.kind, SPROG_R_PAGE_CANCEL);
    CHECK_EQ(got.what, 6);
    CHECK(!rig.f.page.open);
}

/* The revision counts every event and a change of the brand's signature;
 * a closed frame takes no event. */
TEST_CASE(the_revision_follows_events_and_the_signature)
{
    rig_start();
    uint32_t rev = sprog_revision(&rig.f);
    (void)rig_ev(TOUCH_EVENT_DOWN, 300, 450, 1);
    CHECK(sprog_revision(&rig.f) > rev);
    (void)rig_ev(TOUCH_EVENT_UP, 300, 450, 1);
    rev = sprog_revision(&rig.f);
    (void)rig_tick(0.05f);
    CHECK_EQ(sprog_revision(&rig.f), rev);
    rig.sig = 77u;
    (void)rig_tick(0.05f);
    CHECK(sprog_revision(&rig.f) > rev);
    rig.f.open = false;
    rev = sprog_revision(&rig.f);
    CHECK_EQ(rig_tap(sprog_value_rect(&rig.f, R_READ)).kind, SPROG_R_NONE);
    CHECK_EQ(sprog_revision(&rig.f), rev);
    CHECK_EQ(sprog_event(&rig.f, NULL).kind, SPROG_R_NONE);
}

/* A step key held while its cell takes a why: the repeat ends with the
 * refusal, as a fresh press on it is refused (review of #996). */
TEST_CASE(a_why_ends_a_held_steps_repeat)
{
    rig_start();
    sprog_tab_set(&rig.f, 1);
    rig_view();
    const gfx_rect_t plus = sprog_step_rect(&rig.f, R_DEAD, true);

    CHECK_EQ(rig_down(plus, 1).kind, SPROG_R_STEP);
    rig_cell(R_DEAD, SPROG_K_STEP, SPROG_P_TAP)->why = "Locked.";
    CHECK_EQ(rig_run(1.5f).kind, SPROG_R_NONE);
    CHECK(!rig.f.have);
}

/* A row's press held when the brand opens an overlay: its release taps
 * nothing under the page or the editor (review of #996). */
TEST_CASE(an_overlay_opened_under_a_press_takes_its_tap)
{
    rig_start();
    const gfx_rect_t rd = sprog_value_rect(&rig.f, R_READ);

    (void)rig_down(rd, 1);
    sprog_page_open(&rig.f, 1, false, "CHECK", "HOLD", "CANCEL", -1, false);
    CHECK_EQ(rig_up(rd, 1).kind, SPROG_R_NONE);
    sprog_close_editors(&rig.f);
    (void)rig_down(rd, 1);
    sprog_keypad(&rig.f, 1, "X", "", 1.0f, 0.0f, 9.0f, 0);
    CHECK_EQ(rig_up(rd, 1).kind, SPROG_R_NONE);
    sprog_close_editors(&rig.f);
    (void)rig_down(rd, 1);
    sprog_list_open(&rig.f, 1, "MORE", false);
    CHECK_EQ(rig_up(rd, 1).kind, SPROG_R_NONE);
    sprog_close_editors(&rig.f);
    /* The head's text and the notes as well. */
    rig.port_pick = true;
    (void)rig_down(sprog_head_rect(&rig.f), 1);
    sprog_name(&rig.f, "NAME", "X");
    CHECK_EQ(rig_up(sprog_head_rect(&rig.f), 1).kind, SPROG_R_NONE);
    sprog_close_editors(&rig.f);
    CHECK_EQ(rig_tap(rd).kind, SPROG_R_TAP);
}

/* The hold rectangle of a drop page that closed as its hold fired: the
 * 380px button its flash is drawn on (review of #996). */
TEST_CASE(a_drop_pages_flash_keeps_its_hold_rect)
{
    rig_start();
    sprog_page_open(&rig.f, 1, false, "LEAVE", "HOLD", "BACK", -1, true);
    rig_render();
    const gfx_rect_t a = sprog_apply_rect(&rig.f);
    CHECK_EQ(a.w, 380);
    (void)rig_down(a, 1);
    sprog_result_t got = { 0 };
    for (int i = 0; i < 60 && got.kind == SPROG_R_NONE; ++i) {
        got = rig_tick(0.05f);
    }
    CHECK_EQ(got.kind, SPROG_R_PAGE_APPLY);
    CHECK(ui_hold_flashing(&rig.f.page.hold));
    const gfx_rect_t h = sprog_hold_rect(&rig.f);
    CHECK_EQ(h.x, a.x);
    CHECK_EQ(h.w, 380);
}

int main(void)
{
    RUN(a_tap_acts_on_its_release_inside_the_control);
    RUN(a_why_refuses_at_the_press);
    RUN(the_tabs_follow_taps_and_the_mask);
    RUN(a_hold_is_due_on_the_tick_after_it_completes);
    RUN(a_touch_loss_before_the_due_cancels_the_hold);
    RUN(a_hold_ends_without_a_due);
    RUN(a_refused_hold_begin_starts_no_hold);
    RUN(a_due_asked_from_outside_comes_on_the_next_tick);
    RUN(a_step_steps_on_the_press_and_repeats);
    RUN(a_stop_acts_on_the_press_from_any_contact);
    RUN(a_pairs_keys_take_their_own_presses);
    RUN(the_head_text_opens_the_port_list_where_offered);
    RUN(the_keypad_returns_its_code_and_value);
    RUN(the_keyboard_returns_the_name);
    RUN(a_list_returns_the_entry_chosen);
    RUN(a_pages_hold_applies_and_cancel_cancels);
    RUN(the_revision_follows_events_and_the_signature);
    RUN(a_why_ends_a_held_steps_repeat);
    RUN(an_overlay_opened_under_a_press_takes_its_tap);
    RUN(a_drop_pages_flash_keeps_its_hold_rect);
    return test_summary("sprog");
}
