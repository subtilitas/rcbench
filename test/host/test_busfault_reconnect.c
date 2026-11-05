/**
 * @file test_busfault_reconnect.c
 * @brief The bus-fault screen once the link answers again: RECONNECT after
 *        BUSFAULT_BACK_MS of answers, withdrawn by a drop, its hold and its
 *        hint, the counters that move, and a bench left as it was.
 *
 * Cases:
 * - a_link_answering_for_1000ms_offers_reconnect_and_999ms_does_not
 * - a_silent_link_offers_no_reconnect_and_the_hold_acknowledges
 * - the_reconnect_hold_acts_at_2s_and_not_before
 * - a_drop_withdraws_reconnect_and_the_hold_acknowledges_again
 * - a_drop_during_the_reconnect_hold_abandons_it
 * - the_offer_arriving_during_the_acknowledgement_abandons_it
 * - the_offer_counts_across_the_32_bit_ms_wrap
 * - a_new_report_withdraws_the_offer
 * - a_short_press_shows_what_the_hold_does_for_4s
 * - a_tap_on_the_hint_closes_it
 * - a_press_that_slides_off_shows_no_hint
 * - the_link_column_moves_with_its_counters
 * - the_start_up_verdict_does_not_take_live_numbers
 * - the_start_up_verdict_offers_reconnect_the_same_way
 * - the_link_back_state_renders_whole_in_both_languages
 * - reconnect_leaves_the_bench_safe_with_every_latch_kept
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "guard_canvas.h"

#include "busfault_screen.h"
#include "overview_screen.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#include "busfault_spots.h"

#define SCREEN_W UI_PANEL_W
#define SCREEN_H UI_PANEL_H

/** @brief One hold step: the per-frame credit cap, 0.25s, exact in binary. */
#define STEP_S UI_HOLD_MAX_CREDIT_S
/** @brief The steps that make UI_HOLD_S: 8. */
#define HOLD_STEPS ((int)(UI_HOLD_S / STEP_S))

static gfx_color_t  s_px[SCREEN_W * SCREEN_H];
static gfx_canvas_t s_c;

/** @brief The screen under test. */
static const ui_screen_t *scr(void) { return busfault_screen(); }

/** @brief A link that stopped, the controller on the bus, the hold clear. */
static void fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    scr()->reset();
    gfx_canvas_init(&s_c, s_px, SCREEN_W, SCREEN_H, SCREEN_W);
    const busfault_report_t r = {
        .kind = BUSFAULT_LINK_LOST, .bus = BUSFAULT_BUS_RUNNING,
        .down_s = 12, .polls = 300, .timeouts = 12,
    };
    busfault_screen_set(&r);
    (void)busfault_screen_take_exit();
}

/** @brief A touch event of contact 1 at (x, y). */
static void touch(touch_event_type_t type, int x, int y)
{
    touch_event_t e = { .type = type,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    scr()->event(&e);
}

static int ack_cx(void) { return BF_ACK_X + 300; }
static int ack_cy(void) { return BF_ACK_Y + BF_ACK_H / 2; }

/** @brief @p n hold steps of STEP_S. */
static void steps(int n)
{
    for (int i = 0; i < n; ++i) {
        scr()->tick(STEP_S);
    }
}

/** @brief The link answering from @p t0 for @p ms, told every 50ms and at
 *         @p t0 + @p ms itself. */
static void answers(uint32_t t0, uint32_t ms)
{
    for (uint32_t t = 0u; t < ms; t += 50u) {
        busfault_screen_link(true, t0 + t);
    }
    busfault_screen_link(true, t0 + ms);
}

/** @brief The bar's colour at a point clear of its text. */
static gfx_color_t bar_face(void)
{
    scr()->render(&s_c, 0);
    return s_px[4 * SCREEN_W + SCREEN_W - 8];
}

TEST_CASE(a_link_answering_for_1000ms_offers_reconnect_and_999ms_does_not)
{
    fresh();
    answers(5000u, BUSFAULT_BACK_MS - 1u);
    CHECK(!busfault_screen_link_back());
    busfault_screen_link(true, 5000u + BUSFAULT_BACK_MS);
    CHECK(busfault_screen_link_back());
}

TEST_CASE(a_silent_link_offers_no_reconnect_and_the_hold_acknowledges)
{
    fresh();
    for (uint32_t t = 0u; t < 60000u; t += 50u) {
        busfault_screen_link(false, t);
    }
    CHECK(!busfault_screen_link_back());
    touch(TOUCH_EVENT_DOWN, ack_cx(), ack_cy());
    steps(HOLD_STEPS);
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_SIMULATION);
}

TEST_CASE(the_reconnect_hold_acts_at_2s_and_not_before)
{
    fresh();
    answers(0u, BUSFAULT_BACK_MS);
    CHECK(busfault_screen_link_back());
    touch(TOUCH_EVENT_DOWN, ack_cx(), ack_cy());
    steps(HOLD_STEPS - 1);          /* 1.75s */
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_NONE);
    steps(1);                       /* 2.00s */
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_RECONNECT);
    /* Handed over once. */
    steps(HOLD_STEPS);
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_NONE);
}

TEST_CASE(a_drop_withdraws_reconnect_and_the_hold_acknowledges_again)
{
    fresh();
    answers(0u, BUSFAULT_BACK_MS);
    CHECK(busfault_screen_link_back());
    busfault_screen_link(false, BUSFAULT_BACK_MS + 50u);
    CHECK(!busfault_screen_link_back());
    touch(TOUCH_EVENT_DOWN, ack_cx(), ack_cy());
    steps(HOLD_STEPS);
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_SIMULATION);

    /* And the count starts again from the next answer. */
    touch(TOUCH_EVENT_UP, ack_cx(), ack_cy());
    answers(3000u, BUSFAULT_BACK_MS - 1u);
    CHECK(!busfault_screen_link_back());
    busfault_screen_link(true, 3000u + BUSFAULT_BACK_MS);
    CHECK(busfault_screen_link_back());
}

TEST_CASE(a_drop_during_the_reconnect_hold_abandons_it)
{
    fresh();
    answers(0u, BUSFAULT_BACK_MS);
    touch(TOUCH_EVENT_DOWN, ack_cx(), ack_cy());
    steps(HOLD_STEPS - 1);
    busfault_screen_link(false, 2000u);
    /* The finger stays on: neither exit follows. */
    steps(HOLD_STEPS * 2);
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_NONE);
    touch(TOUCH_EVENT_UP, ack_cx(), ack_cy());
    /* A new press is the acknowledgement. */
    touch(TOUCH_EVENT_DOWN, ack_cx(), ack_cy());
    steps(HOLD_STEPS);
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_SIMULATION);
}

TEST_CASE(the_offer_arriving_during_the_acknowledgement_abandons_it)
{
    fresh();
    touch(TOUCH_EVENT_DOWN, ack_cx(), ack_cy());
    steps(HOLD_STEPS - 1);
    answers(0u, BUSFAULT_BACK_MS);
    steps(HOLD_STEPS * 2);
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_NONE);
    touch(TOUCH_EVENT_UP, ack_cx(), ack_cy());
    touch(TOUCH_EVENT_DOWN, ack_cx(), ack_cy());
    steps(HOLD_STEPS);
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_RECONNECT);
}

TEST_CASE(the_offer_counts_across_the_32_bit_ms_wrap)
{
    fresh();
    const uint32_t t0 = 0xFFFFFFFFu - 400u;  /* wraps 401ms in */
    answers(t0, BUSFAULT_BACK_MS - 1u);
    CHECK(!busfault_screen_link_back());
    busfault_screen_link(true, t0 + BUSFAULT_BACK_MS);
    CHECK(busfault_screen_link_back());
}

TEST_CASE(a_new_report_withdraws_the_offer)
{
    fresh();
    answers(0u, BUSFAULT_BACK_MS);
    CHECK(busfault_screen_link_back());
    const busfault_report_t r = { .kind = BUSFAULT_LINK_LOST,
                                  .bus = BUSFAULT_BUS_OFF };
    busfault_screen_set(&r);
    CHECK(!busfault_screen_link_back());
    /* The first answer after it starts the count. */
    busfault_screen_link(true, BUSFAULT_BACK_MS + 10u);
    CHECK(!busfault_screen_link_back());
}

TEST_CASE(a_short_press_shows_what_the_hold_does_for_4s)
{
    fresh();
    const gfx_color_t danger = ui_theme_color(UI_C_DANGER);
    const gfx_color_t warn   = ui_theme_color(UI_C_WARN);
    const gfx_color_t ok     = ui_theme_color(UI_C_OK);
    CHECK_EQ(bar_face(), danger);

    /* Silent: the acknowledgement's hint. */
    touch(TOUCH_EVENT_DOWN, ack_cx(), ack_cy());
    steps(1);
    touch(TOUCH_EVENT_UP, ack_cx(), ack_cy());
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_NONE);
    CHECK_EQ(bar_face(), warn);
    /* 4s: still at 3.9s, gone at 4.0s. */
    for (int i = 0; i < 39; ++i) {
        scr()->tick(0.1f);
    }
    CHECK_EQ(bar_face(), warn);
    scr()->tick(0.11f);
    CHECK_EQ(bar_face(), danger);

    /* Back: RECONNECT's hint over the green bar. */
    answers(0u, BUSFAULT_BACK_MS);
    CHECK_EQ(bar_face(), ok);
    touch(TOUCH_EVENT_DOWN, ack_cx(), ack_cy());
    touch(TOUCH_EVENT_UP, ack_cx(), ack_cy());
    CHECK_EQ(bar_face(), warn);

    /* The two hints are different words. */
    scr()->render(&s_c, 0);
    unsigned long reconnect = 0;
    for (int y = 10; y < 50; ++y) {
        for (int x = 24; x < 400; ++x) {
            reconnect += s_px[y * SCREEN_W + x];
        }
    }
    busfault_screen_link(false, 2000u);  /* the drop closes the hint */
    CHECK_EQ(bar_face(), danger);
    touch(TOUCH_EVENT_DOWN, ack_cx(), ack_cy());
    touch(TOUCH_EVENT_UP, ack_cx(), ack_cy());
    scr()->render(&s_c, 0);
    unsigned long ack = 0;
    for (int y = 10; y < 50; ++y) {
        for (int x = 24; x < 400; ++x) {
            ack += s_px[y * SCREEN_W + x];
        }
    }
    CHECK(ack != reconnect);
}

TEST_CASE(a_tap_on_the_hint_closes_it)
{
    fresh();
    touch(TOUCH_EVENT_DOWN, ack_cx(), ack_cy());
    touch(TOUCH_EVENT_UP, ack_cx(), ack_cy());
    CHECK_EQ(bar_face(), ui_theme_color(UI_C_WARN));
    touch(TOUCH_EVENT_DOWN, 500, 30);
    touch(TOUCH_EVENT_UP, 500, 30);
    CHECK_EQ(bar_face(), ui_theme_color(UI_C_DANGER));
}

TEST_CASE(a_press_that_slides_off_shows_no_hint)
{
    fresh();
    touch(TOUCH_EVENT_DOWN, ack_cx(), ack_cy());
    touch(TOUCH_EVENT_MOVE, ack_cx(), BF_ACK_Y - 60);
    touch(TOUCH_EVENT_UP, ack_cx(), BF_ACK_Y - 60);
    CHECK_EQ(bar_face(), ui_theme_color(UI_C_DANGER));
    /* Nor does a hold that completed. */
    touch(TOUCH_EVENT_DOWN, ack_cx(), ack_cy());
    steps(HOLD_STEPS);
    touch(TOUCH_EVENT_UP, ack_cx(), ack_cy());
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_SIMULATION);
    CHECK(bar_face() != ui_theme_color(UI_C_WARN));
}

/** @brief The sum of the link column's pixels, x 760 to 1000, y 124 to 432. */
static unsigned long column_sum(int buffer)
{
    scr()->render(&s_c, buffer);
    unsigned long sum = 0;
    for (int y = 124; y < BF_NOTE_Y; ++y) {
        for (int x = BF_NUM_X; x < BF_NUM_X + 240; ++x) {
            sum += s_px[y * SCREEN_W + x];
        }
    }
    return sum;
}

TEST_CASE(the_link_column_moves_with_its_counters)
{
    fresh();
    const unsigned long before = column_sum(0);
    busfault_report_t r = *busfault_screen_report();
    r.down_s = 13;
    r.polls  = 301;
    busfault_screen_refresh(&r);
    const unsigned long after = column_sum(0);
    CHECK(after != before);
    CHECK_EQ(busfault_screen_report()->polls, 301u);
    /* The same numbers draw the same column off the cached half. */
    CHECK_EQ(column_sum(0), after);

    /* Back, the column counts the seconds the link has answered. */
    answers(10000u, BUSFAULT_BACK_MS);
    const unsigned long one = column_sum(0);
    busfault_screen_link(true, 10000u + 2000u);
    CHECK(column_sum(0) != one);
}

TEST_CASE(the_start_up_verdict_does_not_take_live_numbers)
{
    fresh();
    const busfault_report_t t = { .kind = BUSFAULT_SELFTEST,
                                  .verdict = CAN_SELFTEST_SILENT,
                                  .sent = 1310, .lost = 1310 };
    busfault_screen_set(&t);
    const busfault_report_t live = { .kind = BUSFAULT_LINK_LOST,
                                     .polls = 99 };
    busfault_screen_refresh(&live);
    CHECK_EQ(busfault_screen_report()->kind, BUSFAULT_SELFTEST);
    CHECK_EQ(busfault_screen_report()->sent, 1310u);
    busfault_screen_refresh(NULL);
    CHECK_EQ(busfault_screen_report()->sent, 1310u);
}

TEST_CASE(the_start_up_verdict_offers_reconnect_the_same_way)
{
    fresh();
    const busfault_report_t t = { .kind = BUSFAULT_SELFTEST,
                                  .verdict = CAN_SELFTEST_CORRUPT,
                                  .sent = 1284, .echoed = 1197 };
    busfault_screen_set(&t);
    answers(0u, BUSFAULT_BACK_MS - 1u);
    CHECK(!busfault_screen_link_back());
    busfault_screen_link(true, BUSFAULT_BACK_MS);
    CHECK(busfault_screen_link_back());
    touch(TOUCH_EVENT_DOWN, ack_cx(), ack_cy());
    steps(HOLD_STEPS);
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_RECONNECT);
}

/* The guarded canvas of the case below. */
static gfx_color_t    s_gpx[GUARD_CANVAS_PIXELS(SCREEN_W, SCREEN_H)];
static guard_canvas_t s_g;
static gfx_canvas_t   s_gc;

TEST_CASE(the_link_back_state_renders_whole_in_both_languages)
{
    /* Both kinds, both languages, silent and back: each paints the whole
     * canvas and nothing beside it, and back is a picture of its own. */
    static const busfault_kind_t k_kind[] = { BUSFAULT_SELFTEST,
                                              BUSFAULT_LINK_LOST };
    for (int l = 0; l < 2; ++l) {
        for (size_t k = 0; k < 2; ++k) {
            fresh();
            ui_text_set_language((ui_lang_t)l);
            guard_canvas_init(&s_g, &s_gc, s_gpx, SCREEN_W, SCREEN_H);
            const busfault_report_t r = {
                .kind = k_kind[k], .verdict = CAN_SELFTEST_SILENT,
                .bus = BUSFAULT_BUS_RUNNING, .down_s = 12, .sent = 1310,
            };
            busfault_screen_set(&r);
            scr()->render(&s_gc, 0);
            CHECK_EQ(guard_canvas_touched(&s_g), 0u);
            CHECK_EQ(guard_canvas_unpainted(&s_g), 0u);
            const uint32_t silent = guard_canvas_sum(&s_g);
            answers(0u, BUSFAULT_BACK_MS);
            scr()->render(&s_gc, 0);
            CHECK_EQ(guard_canvas_touched(&s_g), 0u);
            CHECK_EQ(guard_canvas_unpainted(&s_g), 0u);
            CHECK(guard_canvas_sum(&s_g) != silent);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(reconnect_leaves_the_bench_safe_with_every_latch_kept)
{
    /*
     * The bench as the application hands it over: SAFE, a STOP standing,
     * FAULT 01 (the IOmcu's failsafe) on the chip, the link answering.
     * RECONNECT and the way to the menu change none of it; the band says
     * STOP and FAULT 01 after it.
     */
    ui_router_init();
    fresh();
    const ui_bench_status_t st = {
        .link_up = true, .armed = false, .stop_latched = true,
        .faults = 0x01u, .mode = ui_band_mode(true),
    };
    ui_router_set_status(&st);
    ui_router_goto(SCREEN_BUSFAULT);
    CHECK_EQ(ui_router_current(), SCREEN_BUSFAULT);
    answers(0u, BUSFAULT_BACK_MS);

    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)ack_cx(),
                                   .y = (int16_t)ack_cy(), .strength = 40 } };
    ui_router_event(&e);
    for (int i = 0; i < HOLD_STEPS; ++i) {
        ui_router_tick(STEP_S);
    }
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_RECONNECT);
    ui_router_goto(SCREEN_OVERVIEW);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    const ui_bench_status_t *now = ui_router_status();
    CHECK(!now->armed);
    CHECK(now->stop_latched);
    CHECK_EQ(now->faults, 0x01u);
    CHECK(now->link_up);
    /* The band the menu draws carries the FAULT chip, and the same band
     * as for the status handed in before the screen. */
    CHECK(ui_band_chip_rect(now, UI_BAND_CHIP_FAULT, NULL));
    CHECK_EQ(ui_band_key("MENU", false, now, NULL, false,
                         UI_BAND_GUIDE_NONE),
             ui_band_key("MENU", false, &st, NULL, false,
                         UI_BAND_GUIDE_NONE));
}

int main(void)
{
    RUN(a_link_answering_for_1000ms_offers_reconnect_and_999ms_does_not);
    RUN(a_silent_link_offers_no_reconnect_and_the_hold_acknowledges);
    RUN(the_reconnect_hold_acts_at_2s_and_not_before);
    RUN(a_drop_withdraws_reconnect_and_the_hold_acknowledges_again);
    RUN(a_drop_during_the_reconnect_hold_abandons_it);
    RUN(the_offer_arriving_during_the_acknowledgement_abandons_it);
    RUN(the_offer_counts_across_the_32_bit_ms_wrap);
    RUN(a_new_report_withdraws_the_offer);
    RUN(a_short_press_shows_what_the_hold_does_for_4s);
    RUN(a_tap_on_the_hint_closes_it);
    RUN(a_press_that_slides_off_shows_no_hint);
    RUN(the_link_column_moves_with_its_counters);
    RUN(the_start_up_verdict_does_not_take_live_numbers);
    RUN(the_start_up_verdict_offers_reconnect_the_same_way);
    RUN(the_link_back_state_renders_whole_in_both_languages);
    RUN(reconnect_leaves_the_bench_safe_with_every_latch_kept);
    return test_summary("busfault_reconnect");
}
