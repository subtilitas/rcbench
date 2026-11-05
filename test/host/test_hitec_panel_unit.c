/*
 * RESTORE of the session's backup in the Hitec programmer's overlay
 * (shared/ui/hitec_panel_file.c, hitec_panel_page.c) for a servo the bench
 * does not tie to the backup: a D series servo and a classic servo without
 * a serial.  The page says the bench cannot check the unit, names the
 * model with its ID 0x32 or its missing serial and the backup's age, and
 * its 2s hold confirms the unit:
 *
 *  - the hold writes the backup back;
 *  - a short press shows the hold's hint and writes nothing;
 *  - CANCEL leaves without writing; a tap elsewhere on the page does
 *    nothing;
 *  - a finger off the button, a cancelled touch and STOP during the hold
 *    write nothing;
 *  - the age in seconds to 119s and in minutes from 120s;
 *  - the page in both languages inside the card.
 *
 * The classic servo with the backup's serial keeps the page without these
 * lines (test_hitec_panel_file.c).  The rig is hitec_panel_rig.h.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos these tests model.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel_rig.h"

#include "hitec_card.h"
#include "splash_screen.h"
#include "ui_screen.h"

/* ---------------------------------------------------------------- helpers */

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

/* Whether every line of the open page lies above its button. */
static bool page_fits(void)
{
    int y = AREA.y + 64;   /* HP_PAGE_TOP */

    for (int i = 1; hitec_panel_page_line(i) != NULL; ++i) {
        y += hitec_panel_text_rows(hitec_panel_page_line(i)) * 20 + 8;
    }
    return y - 8 <= hitec_panel_apply_rect().y - 4
           && gfx_text_cells(hitec_panel_page_line(0)) * 20 + 24 <= AREA.w;
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

/* A D485HW read in @p lang, a card in with @p card, SOFT START 60%
 * written from its page: register 0x60 = 6. */
static void d_written_card(ui_lang_t lang, bool card)
{
    open_at(SUPPLY_ON, lang);
    servo_d485();
    tap_connect();
    g_card = card;
    prun(20u);
    if (card) {
        card_in();
    }
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SOFT_START, 60),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    tap(val(HITEC_PANEL_SERVO, 4, 1));
    hold(2.1f);
    until(HITEC_PROG_HELD);
    show();
    CHECK_EQ(g_drv.dreg[0x60], 6u);
}

/* As d_written_card(), a card in. */
static void d_written(ui_lang_t lang)
{
    d_written_card(lang, true);
}

/* RESTORE, then the session's backup: its page. */
static void restore_page(void)
{
    tap(val(HITEC_PANEL_SERVO, 4, 0));
    pick(TR(KP_FROM_SESSION));
    CHECK_EQ(hitec_panel_editor(), 3);
    CHECK_STR_EQ(hitec_panel_page_line(0), TR(KP_RESTORE_TITLE));
}

/* The page's line of the backup's age for @p n in @p unit. */
static const char *age_line(unsigned n, const char *unit)
{
    static char t[HITEC_PANEL_LINE];

    snprintf(t, sizeof(t), TR(HP_UNIT_AGE), n, unit);
    return t;
}

/* Nothing was written since @p writes, and the page's write is not run:
 * register 0x60 holds the written 6. */
static void nothing_written(unsigned writes)
{
    prun(200u);
    CHECK_EQ(g_drv.d_writes, writes);
    CHECK_EQ(g_drv.dreg[0x60], 6u);
    CHECK(hitec_prog_backup_differs(model()));
}

/* The apply button pressed and held @p secs, the bench running, not let
 * go. */
static void press_for(float secs)
{
    const gfx_rect_t r = hitec_panel_apply_rect();
    const int steps = (int)(secs * 50.0f + 0.5f);

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    for (int i = 0; i < steps; ++i) {
        hitec_panel_tick(0.02f);
        prun(20u);
    }
}

/* ------------------------------------------------------------- the page */

TEST_CASE(a_d_series_restore_asks_for_the_unit_and_its_hold_writes)
{
    char unit[HITEC_PANEL_LINE];

    d_written(UI_LANG_EN);
    restore_page();
    CHECK(page_has(TR(KP_FROM_SESSION)));
    CHECK(page_has(TR(KP_RESTORE_UNCHECKED)));
    CHECK(page_has(TR(HP_UNIT_UNCHECKED)));
    snprintf(unit, sizeof(unit), TR(HP_UNIT_D), "D485HW", 0u);
    CHECK(page_has(unit));
    CHECK(page_has("SOFT START: 60% -> 20%"));
    CHECK(page_fits());
    hold(2.1f);
    until(HITEC_PROG_HELD);
    show();
    CHECK_STR_EQ(said(), TR(KP_M_RESTORED));
    CHECK_EQ(g_drv.dreg[0x60], fix_d485()[0x60 / 2]);
    CHECK(!hitec_panel_row_live(HITEC_PANEL_SERVO, 4, 0));
    CHECK(!model()->bk_unit_ok);
}

TEST_CASE(the_page_names_the_id_the_servo_holds)
{
    char unit[HITEC_PANEL_LINE];

    open_en();
    servo_d485();
    g_drv.dreg[0x32] = 17u;
    tap_connect();
    g_card = true;
    prun(20u);
    card_in();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_D_SOFT_START, 60),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    tap(val(HITEC_PANEL_SERVO, 4, 1));
    hold(2.1f);
    until(HITEC_PROG_HELD);
    show();
    restore_page();
    snprintf(unit, sizeof(unit), TR(HP_UNIT_D), "D485HW", 17u);
    CHECK(page_has(unit));
}

TEST_CASE(a_classic_servo_without_a_serial_is_asked_for_too)
{
    char unit[HITEC_PANEL_LINE];

    open_en();
    servo_hs5645();
    for (unsigned c = 0; c < HITEC_C_SERIAL_LEN; ++c) {
        g_drv.eeprom[HITEC_C_SERIAL + c] = 0xFFu;
    }
    tap(val(HITEC_PANEL_SERVO, 1, 0));
    tap(hitec_panel_choice_rect(1));
    tap_connect();
    g_card = true;
    prun(20u);
    card_in();
    CHECK_EQ(hitec_prog_edit(model(), HITEC_P_C_SPEED, 50),
             HITEC_PROG_EDIT_OK);
    CHECK_EQ(hitec_prog_write_enable(model(), true), HITEC_PROG_MSG_NONE);
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    tap(val(HITEC_PANEL_SERVO, 4, 1));
    hold(2.1f);
    until(HITEC_PROG_HELD);
    show();
    restore_page();
    CHECK(page_has(TR(HP_UNIT_UNCHECKED)));
    snprintf(unit, sizeof(unit), TR(HP_UNIT_C), "HS-5645MG");
    CHECK(page_has(unit));
    CHECK(page_fits());
    hold(2.1f);
    until(HITEC_PROG_HELD);
    show();
    CHECK_STR_EQ(said(), TR(KP_M_RESTORED));
    CHECK_EQ(g_drv.eeprom[0x06], fix_hs5645()[0x06]);
}

TEST_CASE(the_backup_age_reads_in_seconds_to_119s_then_in_minutes)
{
    d_written(UI_LANG_EN);
    model()->bk_ms = model()->f.now_ms - 119999u;
    restore_page();
    CHECK(page_has(age_line(119u, "s")));
    tap(hitec_panel_cancel_rect());
    model()->bk_ms = model()->f.now_ms - 120000u;
    restore_page();
    CHECK(page_has(age_line(2u, "min")));
    tap(hitec_panel_cancel_rect());
    model()->bk_ms = model()->f.now_ms - 3599999u;
    restore_page();
    CHECK(page_has(age_line(59u, "min")));
}

/* --------------------------------------------- what leaves without writing */

TEST_CASE(a_short_press_shows_the_hint_and_writes_nothing)
{
    /* The band is the router's: SUPPLY's, the splash done. */
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_SUPPLY);
    d_written(UI_LANG_EN);
    ui_router_set_alert(NULL);
    const unsigned writes = g_drv.d_writes;
    restore_page();
    hold(0.5f);
    CHECK_EQ(hitec_panel_editor(), 3);
    CHECK(ui_router_alert() != NULL);
    CHECK_STR_EQ(ui_router_alert(), TR(HOLD_HINT_RESTORE));
    nothing_written(writes);
}

TEST_CASE(cancel_leaves_and_a_tap_elsewhere_stays_without_writing)
{
    d_written(UI_LANG_EN);
    const unsigned writes = g_drv.d_writes;
    restore_page();
    /* Inside the page, off both buttons. */
    ev(AREA.x + AREA.w / 2, AREA.y + 120, TOUCH_EVENT_DOWN);
    ev(AREA.x + AREA.w / 2, AREA.y + 120, TOUCH_EVENT_UP);
    show();
    CHECK_EQ(hitec_panel_editor(), 3);
    nothing_written(writes);
    tap(hitec_panel_cancel_rect());
    CHECK(hitec_panel_editor() != 3);
    nothing_written(writes);
    CHECK(!model()->bk_unit_ok);
}

TEST_CASE(a_finger_off_the_button_abandons_the_hold)
{
    d_written(UI_LANG_EN);
    const unsigned writes = g_drv.d_writes;
    restore_page();
    press_for(1.0f);
    ev(AREA.x + 10, AREA.y + 120, TOUCH_EVENT_MOVE);
    for (int i = 0; i < 75; ++i) {
        hitec_panel_tick(0.02f);
        prun(20u);
    }
    ev(AREA.x + 10, AREA.y + 120, TOUCH_EVENT_UP);
    show();
    nothing_written(writes);
}

TEST_CASE(a_cancelled_touch_during_the_hold_writes_nothing)
{
    d_written(UI_LANG_EN);
    const unsigned writes = g_drv.d_writes;
    restore_page();
    press_for(1.0f);
    hitec_panel_cancel();
    for (int i = 0; i < 75; ++i) {
        hitec_panel_tick(0.02f);
        prun(20u);
    }
    show();
    nothing_written(writes);
}

TEST_CASE(stop_during_the_hold_writes_nothing)
{
    d_written(UI_LANG_EN);
    const unsigned writes = g_drv.d_writes;
    restore_page();
    press_for(1.0f);
    press_stop();
    press_for(1.2f);
    {
        const gfx_rect_t r = hitec_panel_apply_rect();

        ev(cx(r), cy(r), TOUCH_EVENT_UP);
    }
    prun(200u);
    show();
    CHECK_EQ(g_drv.d_writes, writes);
    CHECK_EQ(g_drv.dreg[0x60], 6u);
    CHECK(!model()->bk_unit_ok);
    CHECK(hitec_prog_message(model())->id != HITEC_PROG_MSG_BACKUP_RESTORED);
}

/* ------------------------------------------------------------ the texts */

TEST_CASE(the_page_fits_in_both_languages_without_a_card)
{
    static const ui_lang_t langs[] = { UI_LANG_EN, UI_LANG_DE };

    for (size_t k = 0; k < 2u; ++k) {
        /* No card: the 3 lines of the backup in RAM as well. */
        d_written_card(langs[k], false);
        CHECK(hitec_prog_backup_ram_only(model()));
        restore_page();
        CHECK(page_has(TR(HP_UNIT_UNCHECKED)));
        CHECK(page_fits());
        tap(hitec_panel_cancel_rect());
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(a_d_series_restore_asks_for_the_unit_and_its_hold_writes);
    RUN(the_page_names_the_id_the_servo_holds);
    RUN(a_classic_servo_without_a_serial_is_asked_for_too);
    RUN(the_backup_age_reads_in_seconds_to_119s_then_in_minutes);
    RUN(a_short_press_shows_the_hint_and_writes_nothing);
    RUN(cancel_leaves_and_a_tap_elsewhere_stays_without_writing);
    RUN(a_finger_off_the_button_abandons_the_hold);
    RUN(a_cancelled_touch_during_the_hold_writes_nothing);
    RUN(stop_during_the_hold_writes_nothing);
    RUN(the_page_fits_in_both_languages_without_a_card);
    free(fb);
    return test_summary("hitec_panel_unit");
}
