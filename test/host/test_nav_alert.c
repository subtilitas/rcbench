/**
 * @file test_nav_alert.c
 * @brief The shell's alerts: the band's two rows, the 30s clear, slots taken
 *        back or replaced, the tap that clears one, and what the band shows.
 *
 * Cases:
 * - an_alert_survives_navigation
 * - a_two_row_alert_fits_two_rows_of_the_band
 * - a_long_alert_draws_on_two_rows
 * - an_alert_clears_after_30_s_and_a_new_one_starts_again
 * - the_frame_an_alert_arrives_in_does_not_count
 * - an_alert_is_withdrawn_only_while_it_still_shows
 * - two_causes_over_in_one_frame_clear_the_alert_shown
 * - a_slot_alert_taken_back_before_a_frame_never_shows
 * - a_slot_alert_replaced_in_the_band_is_left_there
 * - a_slot_number_is_never_0
 * - a_second_contact_cannot_take_the_alert
 * - a_held_alert_returns_after_a_passing_one
 * - a_tap_on_the_alert_clears_it_and_reaches_no_screen
 * - a_lift_clears_only_the_alert_it_was_pressed_on
 * - a_press_on_an_alert_that_expires_stays_off_the_screen
 * - cancelling_gestures_lets_go_of_the_alert
 * - a_held_alert_stays_and_lets_taps_through
 * - a_cleared_alert_leaves_no_red_behind
 * - the_band_shows_what_is_wrong
 * - the_band_names_the_bench_or_the_model_not_the_link
 *
 * SPDX-License-Identifier: MIT
 */

#include "nav_rig.h"

/* The alert survives a change of screen, because it describes the bench and
 * not the screen. */
TEST_CASE(an_alert_survives_navigation)
{
    fresh();
    to_overview();
    ui_router_set_alert("touch controller stopped answering");
    ui_router_goto(SCREEN_LOGS);
    CHECK(ui_router_alert() != NULL);
    ui_router_set_alert(NULL);
    CHECK(ui_router_alert() == NULL);
}

/* An alert given more than the one-row budget of 94 cells (a row of the
 * band holds 97) fits the band's two rows in each language, its %s filled with a version of 8 characters and
 * its %u with 65535, the longest each takes. */
TEST_CASE(a_two_row_alert_fits_two_rows_of_the_band)
{
    int seen = 0;
    for (int id = 0; id < (int)TX_COUNT; ++id) {
        if (strncmp(ui_text_name((ui_text_id_t)id), "ALERT_", 6) != 0
            || ui_text_cells((ui_text_id_t)id) <= 94) {
            continue;
        }
        ++seen;
        for (int l = 0; l < (int)UI_LANG_COUNT; ++l) {
            const char *f = ui_tr_in((ui_lang_t)l, (ui_text_id_t)id);
            char text[UI_ALERT_MAX * 2];
            size_t n = 0;
            for (; *f != '\0' && n + 9u < sizeof(text); ++f) {
                if (f[0] == '%' && f[1] == 's') {
                    n += (size_t)snprintf(text + n, sizeof(text) - n, "10.10.10");
                    ++f;
                } else if (f[0] == '%' && f[1] == 'u') {
                    n += (size_t)snprintf(text + n, sizeof(text) - n, "65535");
                    ++f;
                } else {
                    text[n++] = *f;
                }
            }
            text[n] = '\0';
            const int rows = ui_router_alert_rows(text);
            if (rows < 1 || rows > 2) {
                T_FAIL("%s in language %d takes %d rows: \"%s\"",
                       ui_text_name((ui_text_id_t)id), l, rows, text);
            }
        }
    }
    CHECK(seen > 0);
}

/** @brief The alert band's taps: 4px under its top edge, over the menu's
 *         lower tiles, which end 10px above the panel's foot. */
#define ALERT_TAP_Y (UI_ALERT_Y + 4)

/* A point on the alert band, 4px under its top edge, that, with no alert
 * there, opens a screen from the overview: the tap the band must keep from
 * the screen beneath.  The menu's tiles end 10px above the panel's foot.
 * -1 when the overview has nothing there. */
static int alert_band_x_over_a_tile(void)
{
    for (int x = 10; x < W; x += 20) {
        to_overview();
        tap(x, ALERT_TAP_Y);
        if (ui_router_current() != SCREEN_OVERVIEW) {
            return x;
        }
    }
    return -1;
}

/* The frame the alert arrives in, then 30s. */
static void expire_alert(void)
{
    ui_router_tick(0.0f);
    ui_router_tick(UI_ALERT_SHOW_S);
}

TEST_CASE(an_alert_clears_after_30_s_and_a_new_one_starts_again)
{
    fresh();
    to_overview();
    ui_router_set_alert("PD mini output would not switch");
    ui_router_tick(0.0f);
    ui_router_tick(UI_ALERT_SHOW_S - 0.5f);
    CHECK(ui_router_alert() != NULL);
    ui_router_set_alert("PD mini output would not switch");   /* again */
    ui_router_tick(0.0f);
    ui_router_tick(UI_ALERT_SHOW_S - 0.5f);
    CHECK(ui_router_alert() != NULL);
    ui_router_tick(1.0f);
    CHECK(ui_router_alert() == NULL);
}

/* An alert whose cause is over goes from the band when its number is
 * taken back -- and only while it is the alert showing: one cleared by
 * time or a tap, or replaced by another, is not touched, nor is a held
 * one. */
/* One frame of the render loop over the control task's slot (main.c). */
static bool slot_frame(ui_alert_slot_t *slot, ui_alert_band_t *band)
{
    char text[UI_ALERT_MAX];
    const bool have = ui_alert_slot_take(slot, text, sizeof(text));
    return ui_alert_band_update(band, have ? text : NULL, slot->taken,
                                slot->withdrawn);
}

TEST_CASE(two_causes_over_in_one_frame_clear_the_alert_shown)
{
    fresh();
    to_overview();
    /* The order of the two withdrawals does not matter: the shown alert's
     * is kept whichever comes last. */
    for (unsigned order = 0u; order < 2u; ++order) {
        ui_alert_slot_t slot = {0};
        ui_alert_band_t band = {0};
        const uint32_t sense = ui_alert_slot_post(&slot,
            "INA228 and INA3221 both use 0x40.");
        CHECK(!slot_frame(&slot, &band));
        const uint32_t poles = ui_alert_slot_post(&slot,
            "Pole count rejected by the IOmcu.");
        CHECK(!slot_frame(&slot, &band));
        CHECK_STR_EQ(ui_router_alert(), "Pole count rejected by the IOmcu.");
        if (order == 0u) {
            ui_alert_slot_withdraw(&slot, poles);
            ui_alert_slot_withdraw(&slot, sense);
        } else {
            ui_alert_slot_withdraw(&slot, sense);
            ui_alert_slot_withdraw(&slot, poles);
        }
        CHECK(slot_frame(&slot, &band));
        CHECK(ui_router_alert() == NULL);
        CHECK(!slot_frame(&slot, &band));               /* once */
    }
}

TEST_CASE(a_slot_alert_taken_back_before_a_frame_never_shows)
{
    fresh();
    to_overview();
    ui_alert_slot_t slot = {0};
    ui_alert_band_t band = {0};
    const uint32_t n = ui_alert_slot_post(&slot,
        "PD mini pins rejected. Check SETUP > INTERFACES.");
    ui_alert_slot_withdraw(&slot, n);
    CHECK(!slot.pending);
    CHECK(!slot_frame(&slot, &band));
    CHECK(ui_router_alert() == NULL);
    CHECK_EQ(slot.withdrawn, 0u);

    /* 0 is no alert, and takes nothing back. */
    const uint32_t m = ui_alert_slot_post(&slot, "IOmcu disarmed");
    ui_alert_slot_withdraw(&slot, 0u);
    CHECK(slot.pending);
    CHECK(!slot_frame(&slot, &band));
    CHECK_STR_EQ(ui_router_alert(), "IOmcu disarmed");
    CHECK_EQ(slot.taken, m);
}

TEST_CASE(a_slot_alert_replaced_in_the_band_is_left_there)
{
    fresh();
    to_overview();
    ui_alert_slot_t slot = {0};
    ui_alert_band_t band = {0};
    const uint32_t n = ui_alert_slot_post(&slot,
        "Phase tap split, gap or periods rejected.");
    CHECK(!slot_frame(&slot, &band));
    /* Another caller's alert replaces it in the band. */
    ui_router_set_alert("touch stopped answering");
    ui_alert_slot_withdraw(&slot, n);
    CHECK(!slot_frame(&slot, &band));
    CHECK_STR_EQ(ui_router_alert(), "touch stopped answering");

    /* An older alert of the slot taken back leaves the newer one. */
    const uint32_t old = ui_alert_slot_post(&slot, "IOmcu disarmed");
    CHECK(!slot_frame(&slot, &band));
    (void)ui_alert_slot_post(&slot, "Pole count rejected by the IOmcu.");
    CHECK(!slot_frame(&slot, &band));
    ui_alert_slot_withdraw(&slot, old);
    CHECK(!slot_frame(&slot, &band));
    CHECK_STR_EQ(ui_router_alert(), "Pole count rejected by the IOmcu.");

    /* A new alert taken in the frame the old one is taken back shows. */
    const uint32_t shown = slot.taken;
    (void)ui_alert_slot_post(&slot, "IOmcu disarmed");
    ui_alert_slot_withdraw(&slot, shown);
    CHECK(!slot_frame(&slot, &band));
    CHECK_STR_EQ(ui_router_alert(), "IOmcu disarmed");
}

TEST_CASE(a_slot_number_is_never_0)
{
    ui_alert_slot_t slot = {0};
    slot.posted = UINT32_MAX;
    CHECK_EQ(ui_alert_slot_post(&slot, "a"), 1u);
    CHECK_EQ(ui_alert_slot_post(&slot, "b"), 2u);
    /* Text past the slot is cut, terminated. */
    char longer[UI_ALERT_MAX + 16];
    memset(longer, 'x', sizeof(longer) - 1u);
    longer[sizeof(longer) - 1u] = '\0';
    (void)ui_alert_slot_post(&slot, longer);
    char out[8];
    CHECK(ui_alert_slot_take(&slot, out, sizeof(out)));
    CHECK_EQ(strlen(out), sizeof(out) - 1u);
    CHECK_EQ(strlen(slot.text), (size_t)UI_ALERT_MAX - 1u);
    CHECK(!ui_alert_slot_take(&slot, out, sizeof(out)));
}

TEST_CASE(an_alert_is_withdrawn_only_while_it_still_shows)
{
    fresh();
    to_overview();
    CHECK_EQ(ui_router_alert_number(), 0u);
    CHECK(!ui_router_withdraw_alert(0u));
    ui_router_set_alert("PD mini pins rejected. Check SETUP > INTERFACES.");
    const uint32_t first = ui_router_alert_number();
    CHECK(first != 0u);
    CHECK(ui_router_withdraw_alert(first));
    CHECK(ui_router_alert() == NULL);
    CHECK_EQ(ui_router_alert_number(), 0u);
    CHECK(!ui_router_withdraw_alert(first));        /* once */

    /* The same text set again is a new alert with a new number. */
    ui_router_set_alert("PD mini pins rejected. Check SETUP > INTERFACES.");
    const uint32_t second = ui_router_alert_number();
    CHECK(second != first);
    CHECK(!ui_router_withdraw_alert(first));
    CHECK(ui_router_alert() != NULL);

    /* Replaced: the newer one stays. */
    ui_router_set_alert("IOmcu disarmed");
    CHECK(!ui_router_withdraw_alert(second));
    CHECK_STR_EQ(ui_router_alert(), "IOmcu disarmed");

    /* Cleared by its time, or by a tap, before it is taken back. */
    const uint32_t third = ui_router_alert_number();
    expire_alert();
    CHECK_EQ(ui_router_alert_number(), 0u);
    CHECK(!ui_router_withdraw_alert(third));
    ui_router_set_alert("IOmcu disarmed");
    const uint32_t fourth = ui_router_alert_number();
    tap(400, ALERT_TAP_Y);
    CHECK(ui_router_alert() == NULL);
    CHECK(!ui_router_withdraw_alert(fourth));

    /* A held alert is not one to take back, and shows again beneath. */
    ui_router_hold_alert("touch did not answer -- the bench will not arm");
    ui_router_set_alert("Phase tap split, gap or periods rejected.");
    CHECK(ui_router_withdraw_alert(ui_router_alert_number()));
    CHECK_STR_EQ(ui_router_alert(),
                 "touch did not answer -- the bench will not arm");
    CHECK_EQ(ui_router_alert_number(), 0u);
    ui_router_hold_alert(NULL);
}

/* The frame's interval was measured before the alert was set: a stall that
 * long must not clear the alert before it is drawn. */
TEST_CASE(the_frame_an_alert_arrives_in_does_not_count)
{
    fresh();
    to_overview();
    ui_router_set_alert("the card did not keep up -- run not recorded");
    ui_router_tick(2.0f * UI_ALERT_SHOW_S);
    CHECK(ui_router_alert() != NULL);
    ui_router_tick(UI_ALERT_SHOW_S);
    CHECK(ui_router_alert() == NULL);
}

/* A second finger on the band takes nothing from the first: the first one's
 * lift still clears, and the second's events reach no screen. */
TEST_CASE(a_second_contact_cannot_take_the_alert)
{
    fresh();
    const int x = alert_band_x_over_a_tile();
    CHECK(x >= 0);
    to_overview();
    ui_router_set_alert("IOmcu refused the pulse range");
    touch(400, ALERT_TAP_Y, TOUCH_EVENT_DOWN, 1);
    touch(x, ALERT_TAP_Y, TOUCH_EVENT_DOWN, 2);
    touch(x, 200, TOUCH_EVENT_MOVE, 2);
    touch(x, 200, TOUCH_EVENT_UP, 2);           /* slid off: clears nothing */
    CHECK(ui_router_alert() != NULL);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    touch(400, ALERT_TAP_Y, TOUCH_EVENT_UP, 1);
    CHECK(ui_router_alert() == NULL);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

TEST_CASE(a_tap_on_the_alert_clears_it_and_reaches_no_screen)
{
    fresh();
    const int x = alert_band_x_over_a_tile();
    CHECK(x >= 0);
    to_overview();
    ui_router_set_alert("supply not answering -- output off");
    tap(x, ALERT_TAP_Y);
    CHECK(ui_router_alert() == NULL);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    /* Pressed on the band and lifted off it: a slip, not a tap. */
    ui_router_set_alert("supply not answering -- output off");
    touch(x, ALERT_TAP_Y, TOUCH_EVENT_DOWN, 1);
    touch(x, 200, TOUCH_EVENT_MOVE, 1);
    touch(x, 200, TOUCH_EVENT_UP, 1);
    CHECK(ui_router_alert() != NULL);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    ui_router_set_alert(NULL);
}

/* An alert that arrives under a finger has not been read; the lift that
 * was meant for the one before does not clear it. */
TEST_CASE(a_lift_clears_only_the_alert_it_was_pressed_on)
{
    fresh();
    to_overview();
    ui_router_set_alert("IOmcu refused to arm");
    touch(400, ALERT_TAP_Y, TOUCH_EVENT_DOWN, 1);
    ui_router_set_alert("IOmcu disarmed -- arm again");
    touch(400, ALERT_TAP_Y, TOUCH_EVENT_UP, 1);
    CHECK_STR_EQ(ui_router_alert(), "IOmcu disarmed -- arm again");
    ui_router_set_alert(NULL);
}

/* A press on the band that the alert's expiry overtakes is still the band's:
 * the screen never saw it begin, so it does not see it end. */
TEST_CASE(a_press_on_an_alert_that_expires_stays_off_the_screen)
{
    fresh();
    const int x = alert_band_x_over_a_tile();
    CHECK(x >= 0);
    to_overview();
    ui_router_set_alert("card full or unwritable -- run not recorded");
    touch(x, ALERT_TAP_Y, TOUCH_EVENT_DOWN, 1);
    expire_alert();
    CHECK(ui_router_alert() == NULL);
    touch(x, ALERT_TAP_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/* Cancelled, the band lets go of its press; a release that arrives anyway
 * clears nothing. */
TEST_CASE(cancelling_gestures_lets_go_of_the_alert)
{
    fresh();
    to_overview();
    ui_router_set_alert("touch lost while arming -- arm again");
    touch(400, ALERT_TAP_Y, TOUCH_EVENT_DOWN, 1);
    ui_router_cancel_gestures();
    touch(400, ALERT_TAP_Y, TOUCH_EVENT_UP, 1);
    CHECK(ui_router_alert() != NULL);
    ui_router_set_alert(NULL);
}

/* The start-up fault that leaves no touch: neither time nor a tap clears
 * it, and a tap goes to the screen as if the band were not there. */
TEST_CASE(a_held_alert_stays_and_lets_taps_through)
{
    fresh();
    const int x = alert_band_x_over_a_tile();
    CHECK(x >= 0);
    to_overview();
    ui_router_hold_alert("touch did not answer -- the bench will not arm");
    ui_router_tick(10.0f * UI_ALERT_SHOW_S);
    CHECK(ui_router_alert() != NULL);
    tap(x, ALERT_TAP_Y);
    CHECK(ui_router_alert() != NULL);
    CHECK(ui_router_current() != SCREEN_OVERVIEW);
    ui_router_set_alert(NULL);                  /* not the held one */
    CHECK(ui_router_alert() != NULL);
    ui_router_hold_alert(NULL);
    CHECK(ui_router_alert() == NULL);
}

/* A passing alert shows over the held one and, cleared by time or by a tap,
 * gives way to it again. */
TEST_CASE(a_held_alert_returns_after_a_passing_one)
{
    fresh();
    to_overview();
    ui_router_hold_alert("touch did not answer -- the bench will not arm");
    ui_router_set_alert("protocol mismatch -- will not arm");
    CHECK_STR_EQ(ui_router_alert(), "protocol mismatch -- will not arm");
    expire_alert();
    CHECK_STR_EQ(ui_router_alert(),
                 "touch did not answer -- the bench will not arm");
    ui_router_set_alert("protocol mismatch -- will not arm");
    tap(400, ALERT_TAP_Y);
    CHECK_STR_EQ(ui_router_alert(),
                 "touch did not answer -- the bench will not arm");
    ui_router_hold_alert(NULL);
}

/* The screens cache what they drew per framebuffer, and the band was drawn
 * over that; cleared, it has to be painted over, not left in red. */
TEST_CASE(a_cleared_alert_leaves_no_red_behind)
{
    fresh();
    to_overview();
    ui_router_set_alert("PD mini set points would not take");
    ui_router_render(&cv, 0);
    expire_alert();
    CHECK(ui_router_alert() == NULL);
    ui_router_render(&cv, 0);
    int danger_bottom = 0;
    for (int y = H - UI_ALERT_H; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (fb[(size_t)y * W + x] == ui_theme_color(UI_C_DANGER)) {
                ++danger_bottom;
            }
        }
    }
    CHECK_EQ(danger_bottom, 0);
}

/* The text pixels of the alert band's rows from @p y0 to @p y1 (panel y),
 * left of the "x". */
static int alert_ink(int y0, int y1)
{
    int ink = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = 0; x < W - 40; ++x) {
            if (fb[(size_t)y * W + x] == ui_theme_color(UI_C_TEXT)) {
                ++ink;
            }
        }
    }
    return ink;
}

/* An alert longer than a row's 97 cells takes two rows of 20px from 2px
 * under the band's top edge; one that fits a row is centred in the band. */
TEST_CASE(a_long_alert_draws_on_two_rows)
{
    fresh();
    to_overview();
    static const char k_long[] =
        "The IOmcu refused the binding written last: the pins it names are "
        "held by another protocol. Choose other pins on OUTPUTS.";
    CHECK_EQ(ui_router_alert_rows(k_long), 2);
    ui_router_set_alert(k_long);
    ui_router_render(&cv, 0);
    const int top = H - UI_ALERT_H;
    CHECK(alert_ink(top + 2, top + 22) > 0);
    CHECK(alert_ink(top + 22, top + 42) > 0);

    ui_router_set_alert("PD mini set points would not take");
    CHECK_EQ(ui_router_alert_rows(ui_router_alert()), 1);
    ui_router_render(&cv, 0);
    CHECK(alert_ink(top + 12, top + 32) > 0);
    CHECK_EQ(alert_ink(top, top + 12) + alert_ink(top + 32, H), 0);
}

/* The band's other states: armed, and a fault badge.  Rendered here rather
 * than only on a faulted bench. */
TEST_CASE(the_band_shows_what_is_wrong)
{
    fresh();
    to_overview();

    const ui_bench_status_t bad = {
        .link_up = false, .armed = true, .faults = 0x11,
        .run_seconds = 0, .mode = NULL,
    };
    ui_router_set_status(&bad);
    ui_router_invalidate();
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);

    /* Armed and faulted both paint in the danger and warning colours, and
     * neither is the background. */
    int danger = 0, warn = 0;
    for (int y = 0; y < UI_BAND_H; ++y) {
        for (int x = 0; x < W; ++x) {
            const gfx_color_t p = fb[(size_t)y * W + x];
            if (p == ui_theme_color(UI_C_DANGER)) { ++danger; }
            if (p == ui_theme_color(UI_C_WARN))   { ++warn; }
        }
    }
    CHECK(danger > 400);   /* STOP, and the ARMED badge */
    CHECK(warn > 100);     /* the fault chip */

    CHECK_EQ(ui_router_status()->faults, 0x11);
}

/* The mode chip says what drives the outputs, and the link chip beside it
 * says whether the link is up: the mode never repeats the link's word.
 * Mode names, so the same in German. */
TEST_CASE(the_band_names_the_bench_or_the_model_not_the_link)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK_STR_EQ(ui_band_mode(true), "BENCH");
        CHECK_STR_EQ(ui_band_mode(false), "SIM");
        CHECK(strstr(ui_band_mode(true), "LINK") == NULL);
        CHECK(strstr(ui_band_mode(false), "LINK") == NULL);
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(an_alert_survives_navigation);
    RUN(a_two_row_alert_fits_two_rows_of_the_band);
    RUN(a_long_alert_draws_on_two_rows);
    RUN(an_alert_clears_after_30_s_and_a_new_one_starts_again);
    RUN(the_frame_an_alert_arrives_in_does_not_count);
    RUN(an_alert_is_withdrawn_only_while_it_still_shows);
    RUN(two_causes_over_in_one_frame_clear_the_alert_shown);
    RUN(a_slot_alert_taken_back_before_a_frame_never_shows);
    RUN(a_slot_alert_replaced_in_the_band_is_left_there);
    RUN(a_slot_number_is_never_0);
    RUN(a_second_contact_cannot_take_the_alert);
    RUN(a_held_alert_returns_after_a_passing_one);
    RUN(a_tap_on_the_alert_clears_it_and_reaches_no_screen);
    RUN(a_lift_clears_only_the_alert_it_was_pressed_on);
    RUN(a_press_on_an_alert_that_expires_stays_off_the_screen);
    RUN(cancelling_gestures_lets_go_of_the_alert);
    RUN(a_held_alert_stays_and_lets_taps_through);
    RUN(a_cleared_alert_leaves_no_red_behind);
    RUN(the_band_shows_what_is_wrong);
    RUN(the_band_names_the_bench_or_the_model_not_the_link);
    return test_summary("nav_alert");
}
