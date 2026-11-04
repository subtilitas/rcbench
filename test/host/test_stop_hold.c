/**
 * @file test_stop_hold.c
 * @brief The band's STOP as an emergency stop on the render side: a touch on
 * an unlatched STOP is a stop and nothing more, a hold of 2s on a latched
 * one asks for the reset once, an early release shows the hint and asks
 * nothing, and the latched button reads STOPPED - HOLD 2s TO RESET, red in
 * a yellow frame, with the armed chip reading STOP.  Through the router,
 * as the panel feeds it.
 *
 * Cases:
 * - a_touch_on_an_unlatched_stop_starts_no_hold
 * - a_press_the_status_shows_latched_but_that_latched_starts_no_hold
 * - a_second_contact_that_takes_the_band_ends_the_hold
 * - a_stop_or_a_loss_before_the_request_is_taken_drops_it
 * - a_hold_of_1999ms_asks_nothing_and_2000ms_asks_once
 * - an_early_release_shows_the_hint_and_asks_nothing
 * - a_press_that_slides_off_stop_asks_nothing
 * - a_touch_loss_during_the_hold_asks_nothing
 * - a_screen_change_during_the_hold_asks_nothing
 * - a_touch_controller_failure_during_the_hold_asks_nothing
 * - a_reset_done_under_the_finger_flashes_it
 * - the_latched_stop_is_red_in_a_yellow_frame
 * - the_hold_fades_the_fill_to_the_frame
 * - the_armed_chip_reads_stop_while_latched
 * - the_latched_labels_fit_the_button
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "gfx.h"
#include "settings.h"
#include "splash_screen.h"
#include "stop_hold.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/** @brief The most a frame credits a hold, s; 8 of them are 2s exactly. */
#define Q 0.25f

static gfx_color_t *fb;
static gfx_canvas_t cv;

/** @brief The router on the overview, the stop latched or not. */
static void router_on(bool latched)
{
    ui_theme_set(UI_THEME_DARK);
    settings_set_store(NULL);
    settings_init();
    ui_text_set_language(UI_LANG_EN);
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    const ui_bench_status_t st = { .link_up = true,
                                   .stop_latched = latched };
    ui_router_set_status(&st);
    ui_router_set_alert(NULL);
    (void)ui_router_take_stop();
    (void)stop_hold_take_reset();
}

/** @brief The latch as the control task publishes it. */
static void latch(bool latched)
{
    const ui_bench_status_t st = { .link_up = true,
                                   .stop_latched = latched };
    ui_router_set_status(&st);
}

/** @brief One event of contact 1 at (@p x, @p y). */
static void touch(int x, int y, touch_event_type_t type, uint8_t flags)
{
    touch_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.flags = flags;
    e.point.id = 1u;
    e.point.x = (int16_t)x;
    e.point.y = (int16_t)y;
    e.point.strength = 40;
    ui_router_event(&e);
}

/** @brief A DOWN on STOP the control task marked: a stop latched before it. */
static void press_latched(void);

/** @brief The middle of STOP. */
static int sx(void) { return ui_band_stop_rect().x + ui_band_stop_rect().w / 2; }
static int sy(void) { return ui_band_stop_rect().y + ui_band_stop_rect().h / 2; }

static void press_latched(void)
{
    touch(sx(), sy(), TOUCH_EVENT_DOWN, TOUCH_FLAG_STOP_LATCHED);
}

/** @brief A hold of 7 frames of 0.25s and 0.249s more: 1,999ms. */
static void hold_1999(void)
{
    for (int i = 0; i < 7; ++i) {
        ui_router_tick(Q);
    }
    ui_router_tick(0.249f);
}

/** @brief The band drawn into the canvas. */
static void draw(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)UI_PANEL_W * UI_PANEL_H, sizeof(gfx_color_t));
    }
    gfx_canvas_init(&cv, fb, UI_PANEL_W, UI_PANEL_H, UI_PANEL_W);
    ui_router_invalidate();
    ui_router_render(&cv, 0);
}

/** @brief The pixel at (@p x, @p y) of the canvas. */
static gfx_color_t px(int x, int y)
{
    return fb[(size_t)y * UI_PANEL_W + (size_t)x];
}

TEST_CASE(a_touch_on_an_unlatched_stop_starts_no_hold)
{
    router_on(false);
    touch(sx(), sy(), TOUCH_EVENT_DOWN, 0);
    CHECK(ui_router_take_stop());
    /* The control task latches it; the same press held on resets nothing. */
    latch(true);
    for (int i = 0; i < 20; ++i) {
        ui_router_tick(Q);
    }
    CHECK(!stop_hold_take_reset());
    CHECK(stop_hold_held_s() == 0.0f);
    touch(sx(), sy(), TOUCH_EVENT_UP, 0);
    CHECK(!stop_hold_take_reset());
}

TEST_CASE(a_press_the_status_shows_latched_but_that_latched_starts_no_hold)
{
    /* The snapshot can carry the press's own stop before the press is
     * dispatched: only the control task's mark makes a reset's hold. */
    router_on(true);
    touch(sx(), sy(), TOUCH_EVENT_DOWN, 0);
    CHECK(ui_router_take_stop());
    for (int i = 0; i < 20; ++i) {
        ui_router_tick(Q);
    }
    CHECK(!stop_hold_take_reset());
    CHECK(stop_hold_held_s() == 0.0f);
}

TEST_CASE(a_second_contact_that_takes_the_band_ends_the_hold)
{
    router_on(true);
    press_latched();
    ui_router_tick(1.0f);
    gfx_rect_t chip;
    const ui_bench_status_t st = { .link_up = true, .stop_latched = true };
    CHECK(ui_band_chip_rect(&st, UI_BAND_CHIP_ARMED, &chip));
    touch_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = TOUCH_EVENT_DOWN;
    e.point.id = 2u;
    e.point.x = (int16_t)(chip.x + chip.w / 2);
    e.point.y = (int16_t)(chip.y + chip.h / 2);
    ui_router_event(&e);
    touch(sx(), sy(), TOUCH_EVENT_UP, 0);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    for (int i = 0; i < 12; ++i) {
        ui_router_tick(Q);
    }
    CHECK(!stop_hold_take_reset());
}

TEST_CASE(a_stop_or_a_loss_before_the_request_is_taken_drops_it)
{
    /* The hold completes; a new press on STOP comes before the frame takes
     * the request: that press is a later STOP, and the request goes. */
    router_on(true);
    press_latched();
    for (int i = 0; i < 8; ++i) {
        ui_router_tick(Q);
    }
    touch(sx(), sy(), TOUCH_EVENT_UP, 0);
    press_latched();
    CHECK(!stop_hold_take_reset());
    touch(sx(), sy(), TOUCH_EVENT_UP, 0);
    /* The same with a touch loss in place of the press. */
    press_latched();
    for (int i = 0; i < 8; ++i) {
        ui_router_tick(Q);
    }
    ui_router_cancel_gestures();
    CHECK(!stop_hold_take_reset());
}

TEST_CASE(a_hold_of_1999ms_asks_nothing_and_2000ms_asks_once)
{
    router_on(true);
    press_latched();
    /* Still a STOP at the press. */
    CHECK(ui_router_take_stop());
    hold_1999();
    CHECK(!stop_hold_take_reset());
    ui_router_tick(0.001f);
    CHECK(stop_hold_take_reset());
    for (int i = 0; i < 20; ++i) {
        ui_router_tick(Q);
    }
    CHECK(!stop_hold_take_reset());
    touch(sx(), sy(), TOUCH_EVENT_UP, 0);
    CHECK(!stop_hold_take_reset());
    CHECK(ui_router_alert() == NULL);
}

TEST_CASE(an_early_release_shows_the_hint_and_asks_nothing)
{
    router_on(true);
    press_latched();
    hold_1999();
    touch(sx(), sy(), TOUCH_EVENT_UP, 0);
    CHECK(!stop_hold_take_reset());
    const char *a = ui_router_alert();
    CHECK(a != NULL && strcmp(a, TR(HOLD_HINT_STOP_RESET)) == 0);
    /* And the time does not carry over to the next press. */
    press_latched();
    ui_router_tick(Q);
    CHECK(!stop_hold_take_reset());
    CHECK(stop_hold_held_s() == Q);
}

TEST_CASE(a_press_that_slides_off_stop_asks_nothing)
{
    router_on(true);
    press_latched();
    ui_router_tick(1.0f);
    touch(ui_band_stop_rect().x - 20, sy(), TOUCH_EVENT_MOVE, 0);
    for (int i = 0; i < 8; ++i) {
        ui_router_tick(Q);
    }
    CHECK(!stop_hold_take_reset());
    /* Sliding back on does not resume it. */
    touch(sx(), sy(), TOUCH_EVENT_MOVE, 0);
    for (int i = 0; i < 8; ++i) {
        ui_router_tick(Q);
    }
    CHECK(!stop_hold_take_reset());
    touch(sx(), sy(), TOUCH_EVENT_UP, 0);
    CHECK(ui_router_alert() == NULL);
}

TEST_CASE(a_touch_loss_during_the_hold_asks_nothing)
{
    router_on(true);
    press_latched();
    ui_router_tick(1.5f);
    ui_router_cancel_gestures();
    for (int i = 0; i < 8; ++i) {
        ui_router_tick(Q);
    }
    CHECK(!stop_hold_take_reset());
    CHECK(stop_hold_held_s() == 0.0f);
}

TEST_CASE(a_screen_change_during_the_hold_asks_nothing)
{
    /* Navigation drops the router's press on STOP: the finger that stays
     * on the glass holds nothing, and the new screen's band starts no
     * hold of its own. */
    router_on(true);
    press_latched();
    ui_router_tick(1.0f);
    ui_router_goto(SCREEN_SETUP);
    for (int i = 0; i < 8; ++i) {
        ui_router_tick(Q);
    }
    CHECK(!stop_hold_take_reset());
    CHECK(stop_hold_held_s() == 0.0f);
    /* Back on the overview the press is still not the router's. */
    ui_router_goto(SCREEN_OVERVIEW);
    for (int i = 0; i < 8; ++i) {
        ui_router_tick(Q);
    }
    CHECK(!stop_hold_take_reset());
    touch(sx(), sy(), TOUCH_EVENT_UP, 0);
    CHECK(!stop_hold_take_reset());
}

TEST_CASE(a_touch_controller_failure_during_the_hold_asks_nothing)
{
    /* The control task counts a break when the controller goes unanswered
     * for 100ms; the frame answers it with stop_hold_cancel().  The finger
     * the controller last reported is not one the hold can count on. */
    router_on(true);
    press_latched();
    ui_router_tick(1.75f);
    stop_hold_cancel();
    for (int i = 0; i < 8; ++i) {
        ui_router_tick(Q);
    }
    CHECK(!stop_hold_take_reset());
    CHECK(stop_hold_held_s() == 0.0f);
}

TEST_CASE(a_reset_done_under_the_finger_flashes_it)
{
    router_on(true);
    press_latched();
    for (int i = 0; i < 8; ++i) {
        ui_router_tick(Q);
    }
    CHECK(stop_hold_take_reset());
    CHECK_EQ(stop_hold_flash_ms(), 0);
    /* The control task confirms and releases the latch. */
    latch(false);
    ui_router_tick(0.016f);
    CHECK(stop_hold_flash_ms() > 0);
    touch(sx(), sy(), TOUCH_EVENT_UP, 0);
    CHECK(ui_router_alert() == NULL);
}

TEST_CASE(the_latched_stop_is_red_in_a_yellow_frame)
{
    const gfx_rect_t r = ui_band_stop_rect();
    router_on(false);
    draw();
    CHECK(px(r.x + r.w / 2, r.y + 1) != ui_theme_color(UI_C_WARN));
    CHECK_EQ(px(r.x + 6, r.y + r.h / 2), ui_theme_color(UI_C_DANGER));
    latch(true);
    draw();
    /* The 3px frame on every side, the red inside it. */
    CHECK_EQ(px(r.x + r.w / 2, r.y + 1), ui_theme_color(UI_C_WARN));
    CHECK_EQ(px(r.x + r.w / 2, r.y + r.h - 2), ui_theme_color(UI_C_WARN));
    CHECK_EQ(px(r.x + 1, r.y + r.h / 2), ui_theme_color(UI_C_WARN));
    CHECK_EQ(px(r.x + r.w - 2, r.y + r.h / 2), ui_theme_color(UI_C_WARN));
    CHECK_EQ(px(r.x + 5, r.y + r.h / 2), ui_theme_color(UI_C_DANGER));
}

TEST_CASE(the_hold_fades_the_fill_to_the_frame)
{
    const gfx_rect_t r = ui_band_stop_rect();
    router_on(true);
    press_latched();
    ui_router_tick(Q);
    ui_router_tick(Q);
    ui_router_tick(Q);
    ui_router_tick(Q);
    draw();
    CHECK_EQ(px(r.x + 5, r.y + r.h / 2),
             ui_hold_fill(ui_theme_color(UI_C_DANGER),
                          ui_theme_color(UI_C_WARN), 1.0f));
}

TEST_CASE(the_armed_chip_reads_stop_while_latched)
{
    router_on(false);
    ui_bench_status_t st = { .link_up = true };
    const uint32_t safe_key = ui_band_key("X", false, &st, NULL, false,
                                          UI_BAND_GUIDE_NONE);
    gfx_color_t fill = 0;
    gfx_color_t ink = 0;
    CHECK(ui_band_chip_colors(&st, UI_BAND_CHIP_ARMED, &fill, &ink));
    CHECK(fill != ui_theme_color(UI_C_DANGER));
    st.stop_latched = true;
    CHECK(ui_band_key("X", false, &st, NULL, false, UI_BAND_GUIDE_NONE)
          != safe_key);
    CHECK(ui_band_chip_colors(&st, UI_BAND_CHIP_ARMED, &fill, &ink));
    CHECK_EQ(fill, ui_theme_color(UI_C_DANGER));
    /* Wider than SAFE's chip by the label's two cells. */
    gfx_rect_t stop_chip;
    gfx_rect_t safe_chip;
    CHECK(ui_band_chip_rect(&st, UI_BAND_CHIP_ARMED, &stop_chip));
    st.stop_latched = false;
    CHECK(ui_band_chip_rect(&st, UI_BAND_CHIP_ARMED, &safe_chip));
    CHECK_EQ(stop_chip.w - safe_chip.w,
             gfx_text_width(UI_FONT_BAND, "STOP", 1)
                 - gfx_text_width(UI_FONT_BAND, TR(BAND_SAFE), 1));
}

TEST_CASE(the_latched_labels_fit_the_button)
{
    const gfx_rect_t r = ui_band_stop_rect();
    /* Inside the 3px frame with 4px at each side. */
    const int room = r.w - 2 * 3 - 2 * 4;
    const ui_lang_t langs[2] = { UI_LANG_EN, UI_LANG_DE };
    for (int i = 0; i < 2; ++i) {
        ui_text_set_language(langs[i]);
        CHECK(gfx_text_width(UI_FONT_BAND, TR(STOP_LATCHED_1), 1) <= room);
        CHECK(gfx_text_width(UI_FONT_LABEL, TR(STOP_LATCHED_2), 1) <= room);
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(a_touch_on_an_unlatched_stop_starts_no_hold);
    RUN(a_press_the_status_shows_latched_but_that_latched_starts_no_hold);
    RUN(a_second_contact_that_takes_the_band_ends_the_hold);
    RUN(a_stop_or_a_loss_before_the_request_is_taken_drops_it);
    RUN(a_hold_of_1999ms_asks_nothing_and_2000ms_asks_once);
    RUN(an_early_release_shows_the_hint_and_asks_nothing);
    RUN(a_press_that_slides_off_stop_asks_nothing);
    RUN(a_touch_loss_during_the_hold_asks_nothing);
    RUN(a_screen_change_during_the_hold_asks_nothing);
    RUN(a_touch_controller_failure_during_the_hold_asks_nothing);
    RUN(a_reset_done_under_the_finger_flashes_it);
    RUN(the_latched_stop_is_red_in_a_yellow_frame);
    RUN(the_hold_fades_the_fill_to_the_frame);
    RUN(the_armed_chip_reads_stop_while_latched);
    RUN(the_latched_labels_fit_the_button);
    free(fb);
    return test_summary("stop_hold");
}
