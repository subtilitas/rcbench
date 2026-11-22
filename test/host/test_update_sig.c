/**
 * @file test_update_sig.c
 * @brief What the WI-FI question and PANEL UPDATE say about the release
 *        signature, and the second hold of a build that enforces it.
 *
 * The failures under test: a line that calls an image signed when the
 * release key did not vouch for it, a line wider than the field it is drawn
 * in, a warning that takes the single INSTALL hold away (the question asks
 * the same in both policies), and INSTALL ANYWAY firing from a release
 * before 2s, a press that slid off, a state that changed under the press or
 * a screen in any state other than UNSIGNED.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "fw_sig.h"
#include "gfx.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "wifi_screen.h"
#include "wifi_spots.h"

/** @brief One frame, s. */
#define TICK_S     (1.0f / 39.0f)
/** @brief Frames past a full hold. */
#define HOLD_TICKS ((int)(UI_HOLD_S / TICK_S) + 5)
/** @brief INSTALL ANYWAY's center: U_HOLD of update_screen.c. */
#define GO_X  (UI_PANEL_W - 24 - 240)
/** @brief The buttons' center line on PANEL UPDATE. */
#define BTN_Y (UI_PANEL_H - 18 - 28)
/** @brief CANCEL's center. */
#define CANCEL_X 144

static gfx_color_t  s_px[UI_PANEL_W * UI_PANEL_H]; /**< the frame */
static gfx_canvas_t s_c;                           /**< over s_px */

/** @brief Every verdict there is. */
static const fw_sig_t k_all[] = {
    FW_SIG_NONE, FW_SIG_OK, FW_SIG_FORM, FW_SIG_KEY, FW_SIG_BAD,
};

/** @brief One touch event of contact 1 to @p s at (@p x, @p y). */
static void send(const ui_screen_t *s, touch_event_type_t t, int x, int y)
{
    touch_event_t e = { .type = t,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    s->event(&e);
}

/** @brief @p n frames of @p s. */
static void ticks(const ui_screen_t *s, int n)
{
    for (int i = 0; i < n; ++i) {
        s->tick(TICK_S);
    }
}

/** @brief A hash of the frame @p s draws now into buffer @p buf. */
static uint32_t hash_of(const ui_screen_t *s, int buf, int h)
{
    memset(s_px, 0, sizeof(s_px));
    gfx_canvas_init(&s_c, s_px, UI_PANEL_W, h, UI_PANEL_W);
    s->render(&s_c, buf);
    uint32_t x = 2166136261u;
    for (int i = 0; i < UI_PANEL_W * h; ++i) {
        x = (x ^ (uint32_t)s_px[i]) * 16777619u;
    }
    return x;
}

/** @brief PANEL UPDATE, fresh, showing @p st with verdict @p sig. */
static const ui_screen_t *update_with(update_state_t st, fw_sig_t sig)
{
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    const ui_screen_t *u = update_screen();
    u->reset();
    update_view_t w = { .state = st, .written = 0u, .total = 1900000u,
                        .sig = sig };
    strcpy(w.version, "0.24.0");
    update_screen_set(&w);
    (void)update_screen_take_go();
    (void)update_screen_take_close();
    return u;
}

TEST_CASE(the_line_says_signed_only_for_a_valid_signature)
{
    char t[160];
    ui_text_set_language(UI_LANG_EN);
    update_sig_text(FW_SIG_OK, t, sizeof(t));
    CHECK_STR_EQ(t, "SIGNED BY THE RCBENCH RELEASE KEY");
    update_sig_text(FW_SIG_NONE, t, sizeof(t));
    CHECK_STR_EQ(t, "NOT SIGNED BY THE RCBENCH RELEASE KEY. The image comes "
                    "with no signature.");
    static const fw_sig_t bad[] = { FW_SIG_FORM, FW_SIG_KEY, FW_SIG_BAD };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        update_sig_text(bad[i], t, sizeof(t));
        CHECK_STR_EQ(t, "NOT SIGNED BY THE RCBENCH RELEASE KEY. The "
                        "signature does not match.");
    }
    ui_text_set_language(UI_LANG_DE);
    update_sig_text(FW_SIG_OK, t, sizeof(t));
    CHECK_STR_EQ(t, "VOM RCBENCH-RELEASE-SCHL\xc3\x9c" "SSEL SIGNIERT");
    update_sig_text(FW_SIG_BAD, t, sizeof(t));
    CHECK(strstr(t, "NICHT VOM") == t);
    ui_text_set_language(UI_LANG_EN);
    /* A buffer too short is cut, terminated; none is left alone. */
    update_sig_text(FW_SIG_NONE, t, 8u);
    CHECK_STR_EQ(t, "NOT SIG");
    update_sig_text(FW_SIG_NONE, NULL, 8u);
    t[0] = 'x';
    update_sig_text(FW_SIG_NONE, t, 0u);
    CHECK_EQ(t[0], 'x');
}

TEST_CASE(every_line_fits_one_line_of_the_question_and_panel_update)
{
    /* The question's paragraphs are 80 cells; PANEL UPDATE's lines 81. */
    for (int l = 0; l < 2; ++l) {
        ui_text_set_language(l == 0 ? UI_LANG_EN : UI_LANG_DE);
        for (size_t i = 0; i < sizeof(k_all) / sizeof(k_all[0]); ++i) {
            char t[160];
            update_sig_text(k_all[i], t, sizeof(t));
            if (gfx_text_cells(t) > 80) {
                T_FAIL("\"%s\" is %d cells", t, gfx_text_cells(t));
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(the_question_shows_the_verdict_and_keeps_one_hold)
{
    for (size_t i = 0; i < sizeof(k_all) / sizeof(k_all[0]); ++i) {
        ui_theme_set(UI_THEME_DARK);
        ui_text_set_language(UI_LANG_EN);
        const ui_screen_t *w = wifi_screen();
        w->reset();
        w->enter();
        wifi_view_t v;
        memset(&v, 0, sizeof(v));
        v.up = WIFI_UP_STAGED;
        v.offer = WIFI_OFFER_NEWER;
        v.gate = WIFI_GATE_OPEN;
        v.sig = k_all[i];
        strcpy(v.new_ver, "0.24.0");
        strcpy(v.run_ver, "0.23.16");
        wifi_screen_set(&v);
        (void)wifi_screen_take();
        /* Signed and not signed draw apart. */
        const uint32_t h = hash_of(w, 0, UI_BODY_H);
        v.sig = (k_all[i] == FW_SIG_OK) ? FW_SIG_NONE : FW_SIG_OK;
        wifi_screen_set(&v);
        CHECK(hash_of(w, 0, UI_BODY_H) != h);
        v.sig = k_all[i];
        wifi_screen_set(&v);
        /* The one 2s hold asks for the install whatever the verdict: the
         * application decides what follows (fw_sig_needs_ack()). */
        send(w, TOUCH_EVENT_DOWN, WF_INSTALL_CX, WF_Q_CY);
        ticks(w, HOLD_TICKS);
        CHECK_EQ(wifi_screen_take(), WIFI_REQ_INSTALL);
        send(w, TOUCH_EVENT_UP, WF_INSTALL_CX, WF_Q_CY);
        w->leave();
    }
}

TEST_CASE(panel_update_names_the_verdict_while_it_writes)
{
    const ui_screen_t *u = update_with(UPDATE_WRITING, FW_SIG_OK);
    const uint32_t ok = hash_of(u, 0, UI_PANEL_H);
    (void)update_with(UPDATE_WRITING, FW_SIG_NONE);
    const uint32_t none = hash_of(u, 0, UI_PANEL_H);
    (void)update_with(UPDATE_WRITING, FW_SIG_BAD);
    const uint32_t bad = hash_of(u, 0, UI_PANEL_H);
    CHECK(ok != none);
    CHECK(none != bad);
    /* No hold while it writes: a press where INSTALL ANYWAY would be does
     * nothing. */
    send(u, TOUCH_EVENT_DOWN, GO_X, BTN_Y);
    ticks(u, HOLD_TICKS);
    send(u, TOUCH_EVENT_UP, GO_X, BTN_Y);
    CHECK(!update_screen_take_go());
    CHECK(!update_screen_take_close());
}

TEST_CASE(install_anyway_is_a_2_s_hold_on_an_unsigned_image_only)
{
    const ui_screen_t *u = update_with(UPDATE_UNSIGNED, FW_SIG_NONE);
    CHECK(hash_of(u, 0, UI_PANEL_H) != 0u);
    /* A release before 2s asks nothing. */
    send(u, TOUCH_EVENT_DOWN, GO_X, BTN_Y);
    ticks(u, HOLD_TICKS / 2);
    send(u, TOUCH_EVENT_UP, GO_X, BTN_Y);
    ticks(u, HOLD_TICKS);
    CHECK(!update_screen_take_go());
    /* The full hold asks once, and holding on asks no more. */
    send(u, TOUCH_EVENT_DOWN, GO_X, BTN_Y);
    ticks(u, HOLD_TICKS);
    CHECK(update_screen_take_go());
    ticks(u, HOLD_TICKS);
    CHECK(!update_screen_take_go());
    send(u, TOUCH_EVENT_UP, GO_X, BTN_Y);
    CHECK(!update_screen_take_go());
    CHECK(!update_screen_take_close());
    /* Drawn through the hold and its flash. */
    CHECK(hash_of(u, 1, UI_PANEL_H) != 0u);
}

TEST_CASE(sliding_off_a_state_change_or_a_lost_touch_ends_the_hold)
{
    /* Sliding off; sliding back does not resume it. */
    const ui_screen_t *u = update_with(UPDATE_UNSIGNED, FW_SIG_BAD);
    send(u, TOUCH_EVENT_DOWN, GO_X, BTN_Y);
    ticks(u, HOLD_TICKS / 2);
    send(u, TOUCH_EVENT_MOVE, GO_X, BTN_Y - 200);
    send(u, TOUCH_EVENT_MOVE, GO_X, BTN_Y);
    ticks(u, HOLD_TICKS);
    send(u, TOUCH_EVENT_UP, GO_X, BTN_Y);
    CHECK(!update_screen_take_go());
    /* The image dropped under the hold (STOP, ARM): the state is no longer
     * UNSIGNED and the hold ends unfired. */
    u = update_with(UPDATE_UNSIGNED, FW_SIG_NONE);
    send(u, TOUCH_EVENT_DOWN, GO_X, BTN_Y);
    ticks(u, HOLD_TICKS / 2);
    update_view_t f = { .state = UPDATE_FAILED };
    update_screen_set(&f);
    ticks(u, HOLD_TICKS);
    CHECK(!update_screen_take_go());
    send(u, TOUCH_EVENT_UP, GO_X, BTN_Y);
    /* A lost touch. */
    u = update_with(UPDATE_UNSIGNED, FW_SIG_NONE);
    send(u, TOUCH_EVENT_DOWN, GO_X, BTN_Y);
    ticks(u, HOLD_TICKS / 2);
    u->cancel();
    ticks(u, HOLD_TICKS);
    CHECK(!update_screen_take_go());
}

TEST_CASE(cancel_on_an_unsigned_image_closes_without_the_hold)
{
    const ui_screen_t *u = update_with(UPDATE_UNSIGNED, FW_SIG_NONE);
    send(u, TOUCH_EVENT_DOWN, CANCEL_X, BTN_Y);
    send(u, TOUCH_EVENT_UP, CANCEL_X, BTN_Y);
    CHECK(update_screen_take_close());
    CHECK(!update_screen_take_close());
    CHECK(!update_screen_take_go());
    CHECK(!update_screen_take_cancel());
}

int main(void)
{
    RUN(the_line_says_signed_only_for_a_valid_signature);
    RUN(every_line_fits_one_line_of_the_question_and_panel_update);
    RUN(the_question_shows_the_verdict_and_keeps_one_hold);
    RUN(panel_update_names_the_verdict_while_it_writes);
    RUN(install_anyway_is_a_2_s_hold_on_an_unsigned_image_only);
    RUN(sliding_off_a_state_change_or_a_lost_touch_ends_the_hold);
    RUN(cancel_on_an_unsigned_image_closes_without_the_hold);
    return test_summary("update_sig");
}
