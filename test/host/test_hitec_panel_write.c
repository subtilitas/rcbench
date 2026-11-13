/*
 * The Hitec programmer's overlay writing (shared/ui/hitec_panel_edit.c,
 * hitec_panel_page.c): a value edited by its list and its keypad, marked
 * `*`; CHECK THIS CHANGE held and cancelled; WRITE n VALUES by its 2s
 * hold; REPAIR CHECKSUM from MORE; RESTORE's
 * list after an interrupted write; the keypad's refusal with its reason;
 * every new page's lines inside the page in both languages; the
 * station's #612 finding: with STOP latched no line offers CLEAR; the card
 * half, the record notes, RESTORE's page, the refusals the pages say, the
 * keypads on a raw value, a pressed row, and every write message's lines.  The rig
 * is hitec_panel_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel_rig.h"
#include "hold_flash.h"

#include "hitec_card.h"

/* The row of a parameter on a tab, found by its label. */
static gfx_rect_t row_of(int page, const char *label)
{
    tap(hitec_panel_tab_rect(page));
    for (int row = 0; row < 6; ++row) {
        const char *l = hitec_panel_label(page, row, 0);

        if (l != NULL && strcmp(l, label) == 0) {
            return val(page, row, 0);
        }
    }
    return (gfx_rect_t){ 0, 0, 0, 0 };
}

/* The list's entry labelled @p label tapped. */
static void pick(const char *label)
{
    for (int i = 0; hitec_panel_choice_label(i) != NULL; ++i) {
        if (strcmp(hitec_panel_choice_label(i), label) == 0) {
            tap(hitec_panel_choice_rect(i));
            return;
        }
    }
    T_FAIL("no entry %s", label);
}

/* The keypad: @p digits typed, then OK. */
static void type(const char *digits)
{
    const ui_keypad_t *k = hitec_panel_keypad();

    static const ui_key_t k_digit[10] = {
        UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_4,
        UI_KEY_5, UI_KEY_6, UI_KEY_7, UI_KEY_8, UI_KEY_9,
    };

    for (const char *d = digits; *d != '\0'; ++d) {
        tap(ui_keypad_key_rect(k, *d == '.' ? UI_KEY_DOT
                                            : k_digit[*d - '0']));
    }
    tap(ui_keypad_key_rect(k, UI_KEY_OK));
}

/* Whether a line of the open page is @p s. */
static bool page_has(const char *s)
{
    for (int i = 1; hitec_panel_page_line(i) != NULL; ++i) {
        if (strcmp(hitec_panel_page_line(i), s) == 0) {
            return true;
        }
    }
    return false;
}

/* Whether the page counts the values it has no line for. */
static bool page_has_more(void)
{
    for (int i = 1; hitec_panel_page_line(i) != NULL; ++i) {
        if (strncmp(hitec_panel_page_line(i), "and ", 4) == 0
            || strncmp(hitec_panel_page_line(i), "und ", 4) == 0) {
            return true;
        }
    }
    return false;
}

/* Whether a note or the message of the tab shown is @p s. */
static bool note_has(const char *s)
{
    for (int i = 0; hitec_panel_note(i) != NULL; ++i) {
        if (strcmp(hitec_panel_note(i), s) == 0) {
            return true;
        }
    }
    return false;
}

/* How many notes and message lines of the tab shown are @p s. */
static int lines_saying(const char *s)
{
    int n = 0;

    for (int i = 0; hitec_panel_note(i) != NULL; ++i) {
        n += strcmp(hitec_panel_note(i), s) == 0;
    }
    return n;
}

/* Line @p id with the rig's GPIO in it. */
static const char *with_pin(ui_text_id_t id)
{
    static char t[HITEC_PANEL_LINE];

    snprintf(t, sizeof(t), ui_tr(id), (unsigned)PIN);
    return t;
}

/* SOFT START of a D485HW to 60% by its list. */
static void soft_start_60(void)
{
    tap(row_of(HITEC_PANEL_MOTION, TR(HP_SOFTSTART)));
    CHECK_EQ(hitec_panel_editor(), 2);
    pick("60%");
    CHECK_EQ(hitec_panel_editor(), 0);
}

TEST_CASE(a_value_edited_by_its_list_is_marked_and_counted)
{
    open_en();
    servo_d485();
    tap_connect();
    /* Five steps of soft start: a list, the servo's 20% chosen. */
    tap(row_of(HITEC_PANEL_MOTION, TR(HP_SOFTSTART)));
    CHECK_EQ(hitec_panel_editor(), 2);
    CHECK_STR_EQ(hitec_panel_choice_label(0), "20%");
    CHECK_STR_EQ(hitec_panel_choice_label(4), "100%");
    CHECK(hitec_panel_choice_label(5) == NULL);
    pick("60%");
    CHECK_STR_EQ(hitec_panel_value(HITEC_PANEL_MOTION, 2, 0), "*60% (6)");
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    CHECK_STR_EQ(hitec_panel_value(HITEC_PANEL_SERVO, 3, 1), "WRITE 1 VALUE");
    CHECK(hitec_panel_row_live(HITEC_PANEL_SERVO, 3, 1));
    /* MORE now offers DISCARD EDITS, which takes it back. */
    CHECK(hitec_panel_row_live(HITEC_PANEL_SERVO, 2, 0));
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    /* And no Hitec D receive: the IOmcu takes it at its next OPEN. */
    for (int e = 0; hitec_panel_choice_label(e) != NULL; ++e) {
        CHECK(strstr(hitec_panel_choice_label(e),
                     ui_setting_label(SET_HITEC_RX)) == NULL);
    }
    pick(TR(KP_DISCARD));
    CHECK_STR_EQ(hitec_panel_value(HITEC_PANEL_SERVO, 3, 1),
                 "WRITE 0 VALUES");
    CHECK(!hitec_panel_row_live(HITEC_PANEL_SERVO, 3, 1));
}

/* WRITE n VALUES has no switch before it and no page after it: a tap
 * writes nothing, a release before 2s writes nothing, 2s of contact
 * writes, and the switch the hold turned on is off again after. */
TEST_CASE(write_takes_a_2s_hold_and_writes_with_no_page)
{
    open_en();
    servo_d485();
    tap_connect();
    soft_start_60();
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    CHECK_STR_EQ(hitec_panel_label(HITEC_PANEL_SERVO, 3, 0), TR(KP_RESTORE));
    tap(val(HITEC_PANEL_SERVO, 3, 1));
    CHECK_EQ(hitec_panel_editor(), 0);
    prun(500u);
    CHECK_EQ(g_drv.d_writes, 0u);
    CHECK(!hitec_prog_write_on(model()));
    press_write(1.9f);
    CHECK_EQ(hitec_panel_editor(), 0);
    prun(500u);
    CHECK_EQ(g_drv.d_writes, 0u);
    CHECK_EQ(hitec_prog_changes(model()), 1u);
    hold_write();
    CHECK_EQ(hitec_panel_editor(), 0);
    until(HITEC_PROG_HELD);
    show();
    CHECK_EQ(g_drv.dreg[0x60], 6u);
    CHECK_STR_EQ(said(), TR(KP_M_WRITTEN_ONE));
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    CHECK(!hitec_prog_write_on(model()));
    /* A second write takes the same hold. */
    tap(row_of(HITEC_PANEL_MOTION, TR(HP_SOFTSTART)));
    pick("80%");
    hold_write();
    until(HITEC_PROG_HELD);
    show();
    CHECK_EQ(g_drv.dreg[0x60], 8u);
    CHECK(!hitec_prog_write_on(model()));
}

/* The footer's completed hold (hitec_panel_write_go()) sends on the next
 * tick: a cancel of the touch stream in between sends nothing and keeps
 * the edit; with no cancel the next tick sends. */
TEST_CASE(the_footers_write_waits_a_tick_and_a_cancel_drops_it)
{
    open_en();
    servo_d485();
    tap_connect();
    soft_start_60();
    CHECK(hitec_panel_write_go());
    CHECK(!hitec_prog_write_on(model()));
    hitec_panel_cancel();
    for (int i = 0; i < 25; ++i) {
        hitec_panel_tick(0.02f);
        prun(20u);
    }
    CHECK_EQ(g_drv.d_writes, 0u);
    CHECK_EQ(hitec_prog_changes(model()), 1u);
    CHECK(!hitec_prog_write_on(model()));
    CHECK(hitec_panel_write_go());
    CHECK(!hitec_prog_write_on(model()));
    hitec_panel_tick(0.02f);
    CHECK(hitec_prog_state(model()) != HITEC_PROG_HELD);
    until(HITEC_PROG_HELD);
    show();
    CHECK_EQ(g_drv.dreg[0x60], 6u);
    CHECK_STR_EQ(said(), TR(KP_M_WRITTEN_ONE));
}

/* A write the session refuses says why at the press and starts no hold. */
TEST_CASE(a_write_the_session_refuses_says_why_at_the_press)
{
    open_en();
    servo_d485();
    tap_connect();
    soft_start_60();
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    g_stopped = true;
    prun(20u);
    {
        const gfx_rect_t r = val(HITEC_PANEL_SERVO, 3, 1);

        ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
        CHECK(!hitec_panel_pressed());
        CHECK(said()[0] != '\0');
        ev(cx(r), cy(r), TOUCH_EVENT_UP);
    }
    prun(500u);
    CHECK_EQ(g_drv.d_writes, 0u);
    CHECK(!hitec_prog_write_on(model()));
    g_stopped = false;
}

/* The hold's fill moves hold_revision alone, and hold_rect names WRITE. */
TEST_CASE(writes_hold_fills_through_its_own_revision)
{
    open_en();
    servo_d485();
    tap_connect();
    soft_start_60();
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    const gfx_rect_t w = val(HITEC_PANEL_SERVO, 3, 1);
    const uint32_t hold_rev = hitec_panel_hold_revision();

    ev(cx(w), cy(w), TOUCH_EVENT_DOWN);
    const uint32_t rev = hitec_panel_revision();
    hitec_panel_tick(0.1f);
    hitec_panel_tick(0.1f);
    CHECK(hitec_panel_hold_revision() != hold_rev);
    CHECK_EQ(hitec_panel_revision(), rev);
    const gfx_rect_t h = hitec_panel_pages.hold_rect();
    CHECK(h.x == w.x && h.y == w.y && h.w == w.w && h.h == w.h);
    ev(cx(w), cy(w), TOUCH_EVENT_UP);
    prun(500u);
    CHECK_EQ(g_drv.d_writes, 0u);
}

/* LEAVE with an unwritten edit: the page names it under the line that
 * says it is lost, HOLD 2s TO LEAVE WITHOUT WRITING and BACK; BACK keeps
 * the edit, and the hold leaves without writing and drops it.  On CLASSIC
 * the hold cycles the supply as before. */
TEST_CASE(leave_with_edits_lists_them_back_keeps_them_the_hold_drops_them)
{
    for (int is_classic = 0; is_classic < 2; ++is_classic) {
        open_en();
        if (is_classic != 0) {
            servo_hs5645();
            tap(val(HITEC_PANEL_SERVO, 1, 0));
            tap(hitec_panel_choice_rect(1));
        } else {
            servo_d485();
        }
        tap_connect();
        if (is_classic != 0) {
            CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
                     HITEC_PROG_EDIT_OK);
        } else {
            soft_start_60();
        }
        CHECK_EQ(hitec_prog_changes(model()), 1u);
        tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
        press_port();
        CHECK_EQ(hitec_panel_editor(), 3);
        CHECK_STR_EQ(hitec_panel_page_line(0), TR(KP_LEAVE_TITLE));
        CHECK_STR_EQ(hitec_panel_page_line(1), TR(KP_LEAVE_CHANGES));
        if (is_classic == 0) {
            CHECK_STR_EQ(hitec_panel_page_line(2), "SOFT START: 20% -> 60%");
            CHECK(page_has(TR(HP_LEAVE_D)));
        } else {
            CHECK(strstr(hitec_panel_page_line(2), " -> ") != NULL);
            CHECK(page_has(TR(HP_LEAVE_C)));
        }
        CHECK(page_has(TR(KP_LEAVE_2)));
        CHECK_STR_EQ(hitec_panel_apply_label(), TR(KP_HOLD_LEAVE_DROP));
        CHECK_STR_EQ(hitec_panel_cancel_label(), TR(KP_BACK));
        /* Short of the hold nothing happens; BACK keeps the edit. */
        hold(1.9f);
        CHECK_EQ(hitec_panel_editor(), 3);
        CHECK_EQ(pstate(), HITEC_PROG_HELD);
        tap(hitec_panel_cancel_rect());
        CHECK_EQ(hitec_panel_editor(), 0);
        CHECK_EQ(pstate(), HITEC_PROG_HELD);
        CHECK_EQ(hitec_prog_changes(model()), 1u);
        /* The hold: no write, the edit dropped, the port back in PWM. */
        hitec_panel_back();
        CHECK_STR_EQ(hitec_panel_apply_label(), TR(KP_HOLD_LEAVE_DROP));
        hold(2.1f);
        CHECK_EQ(hitec_prog_changes(model()), 0u);
        until(HITEC_PROG_PWM);
        show();
        CHECK_STR_EQ(said(), TR(KP_M_LEFT));
        CHECK(!hitec_panel_holds_port());
        CHECK_EQ(g_drv.d_writes, 0u);
        CHECK_EQ(g_drv.c_writes, 0u);
    }
}

/* LEAVE without edits: the page it was, HOLD 2s TO LEAVE and CANCEL. */
TEST_CASE(leave_without_edits_is_the_page_it_was)
{
    open_en();
    servo_d485();
    tap_connect();
    press_port();
    CHECK_EQ(hitec_panel_editor(), 3);
    CHECK_STR_EQ(hitec_panel_page_line(1), TR(KP_LEAVE_1));
    CHECK(!page_has(TR(KP_LEAVE_CHANGES)));
    CHECK_STR_EQ(hitec_panel_apply_label(), TR(KP_HOLD_LEAVE));
    CHECK_STR_EQ(hitec_panel_cancel_label(), TR(CANCEL));
    CHECK_EQ(hitec_panel_apply_rect().w, 300);
}

TEST_CASE(check_this_change_takes_the_edit_on_its_hold_and_drops_it_on_cancel)
{
    open_en();
    servo_d485();
    tap_connect();
    /* SPEED is a keypad of 10% to 100%: 20% breaks the slow-speed rule. */
    tap(row_of(HITEC_PANEL_MOTION, TR(HP_SPEED)));
    CHECK_EQ(hitec_panel_editor(), 1);
    type("20");
    CHECK_EQ(hitec_panel_editor(), 3);
    CHECK_STR_EQ(hitec_panel_page_line(0), TR(KP_SOFT_TITLE));
    CHECK(page_has("SPEED: 100% -> 20%"));
    CHECK(page_has(TR(KP_W_SPD_SLOW)));
    tap(hitec_panel_cancel_rect());
    CHECK_EQ(hitec_prog_pending(model(), NULL, NULL), 0u);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    /* The home tag over it is its CANCEL too. */
    tap(row_of(HITEC_PANEL_MOTION, TR(HP_SPEED)));
    type("20");
    hitec_panel_back();
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    tap(row_of(HITEC_PANEL_MOTION, TR(HP_SPEED)));
    type("20");
    hold(2.1f);
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_EQ(hitec_prog_changes(model()), 1u);
    CHECK_STR_EQ(hitec_panel_value(HITEC_PANEL_MOTION, 0, 0), "*20% (4)");
}

TEST_CASE(a_value_the_keypad_takes_and_the_model_refuses_keeps_the_keypad)
{
    open_en();
    servo_d485();
    tap_connect();
    /* 35% is no step of SPEED. */
    tap(row_of(HITEC_PANEL_MOTION, TR(HP_SPEED)));
    type("35");
    CHECK_EQ(hitec_panel_editor(), 1);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
    CHECK_STR_EQ(said(), TR(HP_E_RANGE));
    /* LEFT END above NEUTRAL: refused for the order. */
    hitec_panel_back();
    tap(row_of(HITEC_PANEL_TRAVEL, TR(HP_END_L_N)));
    type("9000");
    CHECK_EQ(hitec_panel_editor(), 1);
    CHECK_STR_EQ(said(), TR(HP_E_ORDER));
}

TEST_CASE(the_d_failsafe_list_leads_to_its_position_keypad)
{
    open_en();
    servo_d485();
    tap_connect();
    tap(row_of(HITEC_PANEL_PROTECT, TR(HP_FAILSAFE)));
    CHECK_EQ(hitec_panel_editor(), 2);
    pick(TR(HP_POSITION));
    CHECK_EQ(hitec_panel_editor(), 1);
    type("1600");
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_EQ(hitec_image_reg(hitec_prog_draft(model()), 0x4C), 1600u);
    /* 1900us asks: far from the center. */
    tap(row_of(HITEC_PANEL_PROTECT, TR(HP_FAILSAFE)));
    pick(TR(HP_POSITION));
    type("1900");
    CHECK_EQ(hitec_panel_editor(), 3);
    CHECK(page_has(TR(KP_W_UNCONT_POS_FAR)));
}

TEST_CASE(repair_checksum_comes_from_more_on_a_wrong_sum)
{
    open_en();
    servo_hs5645();
    g_drv.eeprom[HITEC_C_SUM_LAST] ^= 0x01u;
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(1));
    tap_connect();
    CHECK(note_has(TR(HP_SUM_BAD)));
    CHECK(note_has(TR(HP_SUM_HINT)));
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    pick(TR(HP_MORE_REPAIR));
    /* Its own page, no switch's before it. */
    CHECK_STR_EQ(hitec_panel_page_line(0), TR(HP_REPAIR));
    hold(2.1f);
    until(HITEC_PROG_HELD);
    show();
    CHECK_STR_EQ(said(), TR(HP_REPAIRED));
    CHECK(!note_has(TR(HP_SUM_BAD)));
    CHECK_EQ(g_drv.c_writes, 1u);
}

TEST_CASE(restore_offers_the_record_after_an_interrupted_write)
{
    open_en();
    servo_d485();
    g_drv.d_drop = true;
    tap_connect();
    soft_start_60();
    hold_write();
    until(HITEC_PROG_HELD);
    show();
    CHECK_STR_EQ(said(), TR(HP_VERIFY_FAIL));
    CHECK(note_has(TR(KP_M_JOURNAL)));
    /* RESTORE: the record stands; READ ALL comes before its restore. */
    CHECK(hitec_panel_row_live(HITEC_PANEL_SERVO, 3, 0));
    tap(val(HITEC_PANEL_SERVO, 3, 0));
    CHECK_STR_EQ(hitec_panel_choice_label(0), TR(KP_FORGET));
    hitec_panel_back();
    tap(val(HITEC_PANEL_SERVO, 1, 1));
    until(HITEC_PROG_HELD);
    show();
    CHECK(note_has(TR(HP_RECORD_HELD)));
    tap(val(HITEC_PANEL_SERVO, 3, 0));
    CHECK_STR_EQ(hitec_panel_choice_label(0), TR(KP_FROM_JOURNAL));
    pick(TR(KP_FROM_JOURNAL));
    /* RESTORE's own page: its hold writes. */
    CHECK_STR_EQ(hitec_panel_page_line(0), TR(KP_RESTORE_TITLE));
    CHECK(!hitec_prog_write_on(model()));
    hitec_panel_back();
    tap(val(HITEC_PANEL_SERVO, 3, 0));
    pick(TR(KP_FORGET));
    show();
    CHECK_STR_EQ(said(), TR(KP_M_FORGOTTEN));
    CHECK(!hitec_panel_row_live(HITEC_PANEL_SERVO, 3, 0));
}

/* The station's finding on #612: with the STOP latched the foot said "Tap
 * CLEAR to go on" beside the state line's latched path. */
TEST_CASE(a_latched_stop_offers_no_clear_anywhere)
{
    open_en();
    servo_d485();
    tap_connect();
    tap(val(HITEC_PANEL_SERVO, 1, 1));   /* READ ALL */
    prun(30u);
    press_stop();
    until(HITEC_PROG_HELD);
    prun(50u);
    show();
    CHECK(hitec_prog_stopped(model()));
    CHECK_EQ(msg(), HITEC_PROG_MSG_STOPPED);
    CHECK(note_has(TR(HP_ST_STOP_LATCHED)));
    CHECK(!note_has(TR(HP_ST_STOPPED)));
    for (int i = 0; hitec_panel_note(i) != NULL; ++i) {
        CHECK(strstr(hitec_panel_note(i), "CLEAR") == NULL);
    }
    CHECK(!hitec_panel_row_live(HITEC_PANEL_SERVO, 2, 1));
    /* A switch to SAFE alone still offers CLEAR. */
    open_en();
    servo_d485();
    tap_connect();
    tap(val(HITEC_PANEL_SERVO, 1, 1));
    prun(30u);
    disarm();
    until(HITEC_PROG_HELD);
    prun(50u);
    show();
    CHECK(note_has(TR(HP_ST_STOPPED)));
    CHECK(hitec_panel_row_live(HITEC_PANEL_SERVO, 2, 1));
}

/* Every page's lines inside it, above its buttons, in both languages. */
TEST_CASE(every_leave_page_fits_in_both_languages)
{
    static const ui_lang_t langs[] = { UI_LANG_EN, UI_LANG_DE };

    for (size_t k = 0; k < sizeof(langs) / sizeof(langs[0]); ++k) {
        int y;

        open_at(SUPPLY_ON, langs[k]);
        servo_d485();
        tap_connect();
        /* Many values: direction mirrors three, and four more. */
        CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_DIRECTION, HITEC_CCW),
                 HITEC_PROG_EDIT_CHECK);
        CHECK_EQ(hitec_prog_confirm(model()), HITEC_PROG_EDIT_OK);
        CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SOFT_START, 60),
                 HITEC_PROG_EDIT_OK);
        CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
                 HITEC_PROG_EDIT_OK);
        CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_OVERLOAD, 30),
                 HITEC_PROG_EDIT_OK);
        CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_ID, 3),
                 HITEC_PROG_EDIT_OK);
        CHECK(hitec_prog_changes(model()) > 6u);   /* HP_PAGE_VALUES */
        /* LEAVE lists them: the values that fit and the count of the
         * rest, HOLD 2s TO LEAVE WITHOUT WRITING and BACK. */
        tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
        tap(val(HITEC_PANEL_SERVO, 0, 0));
        CHECK_EQ(hitec_panel_editor(), 3);
        CHECK_STR_EQ(hitec_panel_page_line(1), TR(KP_LEAVE_CHANGES));
        CHECK_STR_EQ(hitec_panel_apply_label(), TR(KP_HOLD_LEAVE_DROP));
        CHECK_STR_EQ(hitec_panel_cancel_label(), TR(KP_BACK));
        CHECK((int)strlen(hitec_panel_apply_label()) * 10 + 20
              <= hitec_panel_apply_rect().w);
        CHECK_EQ(hitec_panel_apply_rect().x + hitec_panel_apply_rect().w
                     + 20,
                 hitec_panel_cancel_rect().x);
        /* As draw_hold() steps: each line's rows and 8px. */
        y = AREA.y + 64;   /* HP_PAGE_TOP */
        for (int i = 1; hitec_panel_page_line(i) != NULL; ++i) {
            y += hitec_panel_text_rows(hitec_panel_page_line(i)) * 20
                 + 8;
        }
        CHECK(page_has_more());
        CHECK(y - 8 <= hitec_panel_apply_rect().y - 4);
        CHECK(gfx_text_cells(hitec_panel_page_line(0)) * 20 + 24 <= AREA.w);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* The model's draft edited, for the cases below. */
static void edit_on(uint8_t id, int32_t v)
{
    CHECK_EQ(hitec_prog_edit(model(), id, v), HITEC_PROG_EDIT_OK);
}

/* The write by WRITE's hold, run to HELD. */
static void write_by_hold(void)
{
    press_write(2.1f);
    until(HITEC_PROG_HELD);
    show();
}

/* READ ALL, run to HELD. */
static void read_all(void)
{
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    show();
}

TEST_CASE(the_overlay_hands_the_record_to_the_card_and_says_each_phase)
{
    char text[HITEC_CARD_TEXT_MAX];
    size_t len;

    open_en();
    servo_d485();
    tap_connect();
    card_in();
    g_card_slow = true;   /* the rig's card half takes nothing */
    edit_on(HITEC_P_D_SOFT_START, 60);
    press_write(2.1f);
    /* The second read first, said as such, with ABORT on the PORT row. */
    CHECK_EQ(pstate(), HITEC_PROG_READING);
    CHECK(hitec_prog_rereading(model()));
    show();
    CHECK(strncmp(hitec_panel_note(0), "Second read before the write: ",
                  30) == 0);
    CHECK_STR_EQ(hitec_panel_value(HITEC_PANEL_SERVO, 0, 0), TR(KP_ABORT));
    reread();
    CHECK_EQ(pstate(), HITEC_PROG_RECORDING);
    CHECK(note_has(TR(HP_ST_RECORD)));
    len = hitec_panel_file_take(text, sizeof(text));
    CHECK(len > 0u);
    CHECK_EQ(hitec_panel_file_take(text, sizeof(text)), 0u);
    hitec_panel_file_done(true);
    CHECK_EQ(pstate(), HITEC_PROG_WRITING);
    show();
    CHECK(strncmp(hitec_panel_note(0), "Writing 1 of ", 13) == 0);
    /* The record on the card is this write's: no interrupted write. */
    CHECK(!note_has(TR(KP_M_JOURNAL)));
    until(HITEC_PROG_HELD);
    show();
    CHECK_STR_EQ(said(), TR(KP_M_WRITTEN_ONE));
    /* A text with no record loads as none. */
    len = hitec_card_text(NULL, text, sizeof(text));
    CHECK(hitec_panel_file_load(text, len));
    CHECK(!hitec_prog_record(model()));
}

TEST_CASE(the_notes_name_a_read_owed_another_servo_and_a_mix)
{
    uint16_t dead_band;

    open_en();
    servo_d485();
    dead_band = g_drv.dreg[0x4E];
    g_drv.d_drop = true;
    tap_connect();
    /* A write the servo did not take, its record discarded: READ ALL is
     * still owed. */
    edit_on(HITEC_P_D_SOFT_START, 60);
    write_by_hold();
    CHECK_STR_EQ(said(), TR(HP_VERIFY_FAIL));
    CHECK_EQ(hitec_prog_forget(model()), HITEC_PROG_MSG_FORGOTTEN);
    show();
    CHECK(note_has(TR(HP_MUST_READ)));
    read_all();
    CHECK(!note_has(TR(HP_MUST_READ)));
    /* Another servo: a dead band the record knows neither way. */
    edit_on(HITEC_P_D_SOFT_START, 60);
    write_by_hold();
    g_drv.dreg[0x4E] = 4u;
    read_all();
    CHECK(note_has(TR(KP_M_JOURNAL_OTHER)));
    /* Review round 2 on #613: the note says the message's first line;
     * the message's GPIO line stays. */
    CHECK_EQ(lines_saying(TR(KP_M_JOURNAL_OTHER)), 1);
    CHECK(note_has(with_pin(TX_KP_M_JOURNAL_OTHER_2_PIN)));
    CHECK_EQ(hitec_prog_forget(model()), HITEC_PROG_MSG_FORGOTTEN);
    g_drv.dreg[0x4E] = dead_band;
    read_all();
    /* A mix: one of two values took. */
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    edit_on(HITEC_P_D_SOFT_START, 60);
    write_by_hold();
    g_drv.dreg[0x54] = 10u;
    read_all();
    CHECK(note_has(TR(KP_M_INCONSISTENT)));
    CHECK_EQ(lines_saying(TR(KP_M_INCONSISTENT)), 1);
    CHECK(note_has(TR(HP_REC_MIX_2)));
    /* With an edit standing: WRITE waits for the record, said at the
     * press, RESTORE for the edit. */
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SOFT_START, 80),
             HITEC_PROG_EDIT_OK);
    press_write(2.5f);
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_STR_EQ(said(), TR(HP_RECORD_HELD));
    CHECK(!hitec_prog_write_on(model()));
    tap(val(HITEC_PANEL_SERVO, 3, 0));
    pick(TR(KP_FROM_JOURNAL));
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_STR_EQ(said(), TR(HP_EDITS_PENDING));
    /* Without it: the RESTORE page, its hold, the values back. */
    hitec_prog_discard(model());
    g_drv.d_drop = false;
    tap(val(HITEC_PANEL_SERVO, 3, 0));
    pick(TR(KP_FROM_JOURNAL));
    CHECK_STR_EQ(hitec_panel_page_line(0), TR(KP_RESTORE_TITLE));
    CHECK(page_has(TR(KP_FROM_JOURNAL)));
    CHECK(page_has("SPEED: 50% -> 100%"));
    hold(2.1f);
    until(HITEC_PROG_HELD);
    show();
    CHECK_STR_EQ(said(), TR(HP_RESTORED));
    CHECK_EQ(g_drv.dreg[0x54], 4095u);
    CHECK(!hitec_prog_record(model()));
}

TEST_CASE(repair_waits_for_the_edits)
{
    open_en();
    servo_hs5645();
    g_drv.eeprom[HITEC_C_SUM_LAST] ^= 0x01u;
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(1));
    tap_connect();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    pick(TR(HP_MORE_REPAIR));
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_STR_EQ(said(), TR(HP_EDITS_PENDING));
    CHECK_EQ(g_drv.c_writes, 0u);
}

/* The hold flash walk's cases: WRITE flashes as its hold fires, and a hold
 * page, LEAVE's, closes as its hold fires and its button flashes where it
 * was. */
static void hp_frame(float dt_s)
{
    hitec_panel_tick(dt_s);
    prun(17u);
}

TEST_CASE(writes_hold_flashes_as_it_fires)
{
    open_en();
    servo_d485();
    tap_connect();
    soft_start_60();
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    const gfx_rect_t b = val(HITEC_PANEL_SERVO, 3, 1);
    const gfx_rect_t r = { (int16_t)(b.x + 12), (int16_t)(b.y + 4),
                           (int16_t)(b.w - 24), 6 };
    const hold_flash_rig_t frig = { hp_frame, show, fb, UI_PANEL_W };
    char seen[64];
    ev(cx(b), cy(b), TOUCH_EVENT_DOWN);
    if (!hold_flash_walk(&frig, r, 2.6f, seen, sizeof(seen))) {
        T_FAIL("WRITE's hold drew %s, no flash", seen);
    }
    ev(cx(b), cy(b), TOUCH_EVENT_UP);
    until(HITEC_PROG_HELD);
    show();
    CHECK_STR_EQ(said(), TR(KP_M_WRITTEN_ONE));
    CHECK_EQ(hold_flash_body(&frig, r), '.');
}

TEST_CASE(a_hold_pages_button_flashes_as_it_fires)
{
    open_en();
    servo_d485();
    tap_connect();
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    CHECK_STR_EQ(hitec_panel_page_line(0), TR(KP_LEAVE_TITLE));
    const gfx_rect_t b = hitec_panel_apply_rect();
    const gfx_rect_t r = { (int16_t)(b.x + 12), (int16_t)(b.y + 4),
                           (int16_t)(b.w - 24), 6 };
    const hold_flash_rig_t frig = { hp_frame, show, fb, UI_PANEL_W };
    char seen[64];
    ev(cx(b), cy(b), TOUCH_EVENT_DOWN);
    if (!hold_flash_walk(&frig, r, 2.6f, seen, sizeof(seen))) {
        T_FAIL("LEAVE's hold drew %s, no flash", seen);
    }
    ev(cx(b), cy(b), TOUCH_EVENT_UP);
    CHECK_EQ(hitec_panel_editor(), 0);
    show();
    CHECK_EQ(hold_flash_body(&frig, r), '.');
}

TEST_CASE(each_keypad_opens_on_its_value_or_its_range)
{
    /* A raw speed: the keypad opens on the range's top. */
    open_en();
    servo_d485();
    g_drv.dreg[0x54] = 7u;
    g_drv.dreg[0x32] = 300u;   /* an ID above 255: raw */
    tap_connect();
    tap(row_of(HITEC_PANEL_MOTION, TR(HP_SPEED)));
    CHECK_EQ(hitec_panel_editor(), 1);
    type("50");
    CHECK_EQ(hitec_image_reg(hitec_prog_draft(model()), 0x54), 10u);
    /* LEAVE's page names the stored value the edit replaces. */
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    CHECK(page_has("SPEED: RAW 7 -> 50%"));
    tap(hitec_panel_cancel_rect());
    /* A raw ID: the keypad opens on the range's foot. */
    tap(hitec_panel_tab_rect(HITEC_PANEL_INFO));
    tap(val(HITEC_PANEL_INFO, 0, 1));
    CHECK_EQ(hitec_panel_editor(), 1);
    type("5");
    CHECK_EQ(hitec_image_reg(hitec_prog_draft(model()), 0x32), 5u);
    /* NEUTRAL: counts, on its value. */
    tap(row_of(HITEC_PANEL_TRAVEL, TR(HP_NEUTRAL_N)));
    CHECK_EQ(hitec_panel_editor(), 1);
    hitec_panel_back();
    /* A classic CENTER in quarter microseconds, read and raw. */
    open_en();
    servo_hs5645();
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(1));
    tap_connect();
    tap(hitec_panel_tab_rect(HITEC_PANEL_TRAVEL));
    tap(val(HITEC_PANEL_TRAVEL, 0, 0));
    CHECK_EQ(hitec_panel_editor(), 1);
    type("1500.25");
    CHECK_EQ(hitec_panel_editor(), 3);   /* the center moves: CHECK */
    hitec_panel_back();
    open_en();
    servo_hs5645();
    g_drv.eeprom[0x07] = 0u;   /* 2052 quarters: below 900us, raw */
    g_drv.eeprom[0x08] = 0u;
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(1));
    tap_connect();
    tap(hitec_panel_tab_rect(HITEC_PANEL_TRAVEL));
    tap(val(HITEC_PANEL_TRAVEL, 0, 0));
    CHECK_EQ(hitec_panel_editor(), 1);
}

TEST_CASE(a_value_row_under_the_finger_is_drawn_pressed)
{
    gfx_rect_t r;

    open_en();
    servo_d485();
    tap_connect();
    r = row_of(HITEC_PANEL_MOTION, TR(HP_SOFTSTART));
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    show();
    CHECK(fb[(size_t)cy(r) * UI_PANEL_W + (size_t)(r.x + 4)]
          == ui_theme_color(UI_C_PANEL));
    ev(cx(r), cy(r), TOUCH_EVENT_UP);
    CHECK_EQ(hitec_panel_editor(), 2);
}

TEST_CASE(every_write_message_names_its_step_and_its_servo)
{
    static const struct {
        uint8_t id;
        uint16_t reg;
        uint16_t arg;
        const char *first;
        int lines;
    } k_case[] = {
        { HITEC_PROG_MSG_WRITE_CUT, HITEC_STEP_C_CELL, 0x06,
          "Writing was interrupted at stored value 0x06. Tap READ ALL.", 2 },
        { HITEC_PROG_MSG_WRITE_CUT, HITEC_STEP_C_SUM, 0x06,
          "Stored value 0x06 is written, its checksum in 0x1C is not. "
          "Tap READ ALL.", 2 },
        { HITEC_PROG_MSG_WRITE_CUT, HITEC_STEP_D_WRITE, 0x54,
          "Writing was interrupted at register 0x54. Tap READ ALL.", 1 },
        { HITEC_PROG_MSG_WRITE_CUT, HITEC_STEP_D_SAVE, 0x70,
          "Writing was interrupted before the servo saved. Tap READ ALL.",
          1 },
        { HITEC_PROG_MSG_WRITE_CUT, HITEC_STEP_D_REBOOT, 0x46,
          "Writing was interrupted after the save, before the restart. "
          "Tap READ ALL.", 1 },
        { HITEC_PROG_MSG_WRITE_CUT, HITEC_PROG_CUT_CHECK, 0,
          "All values were sent, but verification was interrupted. "
          "Tap READ ALL.", 1 },
        { HITEC_PROG_MSG_RECORD, 0, 13,
          "Writing was interrupted. The settings may be incomplete.", 2 },
        { HITEC_PROG_MSG_RECORD, 2, 0xFF,
          "Writing was interrupted. The settings may be incomplete.", 2 },
        { HITEC_PROG_MSG_REC_SAME, 0, 13,
          "Is this the servo whose write was interrupted on GPIO 13?", 2 },
        { HITEC_PROG_MSG_REC_DONE, 2, 0xFF,
          "Is this the servo whose write was interrupted on port 3?", 2 },
        { HITEC_PROG_MSG_REC_OTHER, 0, 13,
          "The interrupted write affected a different servo.", 2 },
        { HITEC_PROG_MSG_REC_OTHER, 2, 0xFF,
          "The interrupted write affected a different servo.", 2 },
        { HITEC_PROG_MSG_EDIT_REFUSED, 0, 200,
          "The value is outside this field's allowed range.", 1 },
    };
    static const char *const k_second[] = {
        "Do not use the servo. Connect it to GPIO 13 and tap READ ALL.",
        "Do not use the servo. Connect it to port 3 and tap READ ALL.",
        "It was connected to GPIO 13. Discard the record under RESTORE.",
        "It was connected to port 3. Discard the record under RESTORE.",
    };
    char lines[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE];

    ui_text_set_language(UI_LANG_EN);
    for (size_t i = 0; i < sizeof(k_case) / sizeof(k_case[0]); ++i) {
        const hitec_prog_msg_t m = { .id = k_case[i].id,
                                     .reg = k_case[i].reg,
                                     .arg = k_case[i].arg };

        CHECK_EQ(hitec_panel_message_lines(&m, lines), k_case[i].lines);
        CHECK_STR_EQ(lines[0], k_case[i].first);
        if (i == 0u) {
            CHECK_STR_EQ(lines[1], TR(HP_CUT_SUM_2));
        } else if (i >= 6u && i <= 7u) {
            CHECK_STR_EQ(lines[1], k_second[i - 6u]);
        } else if (i >= 10u && i <= 11u) {
            CHECK_STR_EQ(lines[1], k_second[i - 8u]);
        }
    }
}

TEST_CASE(a_record_loaded_at_the_start_keeps_its_gpio_line)
{
    hitec_card_record_t r;
    uint8_t addrs[HITEC_C_BLOCK_LEN];
    char text[HITEC_CARD_TEXT_MAX];
    size_t len;

    open_en();
    CHECK_EQ(hitec_card_addrs(HITEC_D, addrs), 21u);
    memset(&r, 0, sizeof(r));
    r.series = HITEC_D;
    r.port = CH;
    r.pin = PIN;
    hitec_image_init(&r.before, HITEC_D);
    hitec_image_init(&r.target, HITEC_D);
    for (unsigned i = 0; i < 21u; ++i) {
        (void)hitec_image_put_reg(&r.before, addrs[i], 1u);
        (void)hitec_image_put_reg(&r.target, addrs[i], 2u);
    }
    len = hitec_card_text(&r, text, sizeof(text));
    CHECK(hitec_panel_file_load(text, len));
    show();
    CHECK_EQ(msg(), HITEC_PROG_MSG_RECORD);
    CHECK_EQ(lines_saying(TR(KP_M_JOURNAL)), 1);
    CHECK(note_has(with_pin(TX_KP_M_JOURNAL_2_PIN)));
}

int main(void)
{
    RUN(a_value_edited_by_its_list_is_marked_and_counted);
    RUN(write_takes_a_2s_hold_and_writes_with_no_page);
    RUN(the_footers_write_waits_a_tick_and_a_cancel_drops_it);
    RUN(a_write_the_session_refuses_says_why_at_the_press);
    RUN(writes_hold_fills_through_its_own_revision);
    RUN(leave_with_edits_lists_them_back_keeps_them_the_hold_drops_them);
    RUN(leave_without_edits_is_the_page_it_was);
    RUN(check_this_change_takes_the_edit_on_its_hold_and_drops_it_on_cancel);
    RUN(a_value_the_keypad_takes_and_the_model_refuses_keeps_the_keypad);
    RUN(the_d_failsafe_list_leads_to_its_position_keypad);
    RUN(repair_checksum_comes_from_more_on_a_wrong_sum);
    RUN(restore_offers_the_record_after_an_interrupted_write);
    RUN(a_latched_stop_offers_no_clear_anywhere);
    RUN(every_leave_page_fits_in_both_languages);
    RUN(the_overlay_hands_the_record_to_the_card_and_says_each_phase);
    RUN(the_notes_name_a_read_owed_another_servo_and_a_mix);
    RUN(repair_waits_for_the_edits);
    RUN(each_keypad_opens_on_its_value_or_its_range);
    RUN(a_value_row_under_the_finger_is_drawn_pressed);
    RUN(every_write_message_names_its_step_and_its_servo);
    RUN(a_record_loaded_at_the_start_keeps_its_gpio_line);
    RUN(writes_hold_flashes_as_it_fires);
    RUN(a_hold_pages_button_flashes_as_it_fires);
    free(fb);
    return test_summary("hitec_panel_write");
}
