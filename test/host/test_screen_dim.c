/**
 * @file test_screen_dim.c
 * @brief Dim after (screen_dim.h): the backlight dims after the minutes
 *        without a touch on an idle bench and never on a busy one; a touch
 *        wakes it; the press that wakes it reaches no screen, except a press
 *        on STOP, which stops the bench.  The rule alone, and in front of
 *        the router as the panel's render loop puts it.
 *
 * Cases:
 * - zero_minutes_never_dims
 * - it_dims_at_the_minutes_and_not_a_millisecond_before
 * - a_busy_bench_never_dims_and_wakes_at_once
 * - the_link_reports_card_line_never_keeps_it_lit
 * - fault_01_alone_lets_it_dim
 * - a_press_while_a_card_line_is_written_only_wakes_it
 * - an_armed_bench_or_an_output_on_keeps_it_lit
 * - a_touch_wakes_it_and_starts_the_minutes_again
 * - the_press_that_wakes_it_reaches_no_screen
 * - a_press_on_stop_wakes_it_and_stops_the_bench
 * - a_busy_frame_never_swallows_a_press
 * - a_second_finger_after_the_wake_is_the_screens
 * - a_contact_down_before_the_dim_goes_on
 * - a_down_reusing_a_held_id_is_a_new_press
 * - lost_releases_never_let_a_waking_press_through
 * - the_clock_wraps
 * - null_is_refused
 * - the_line_names_what_started_the_minutes
 * - the_line_fits_its_buffer
 * - the_dials_input_wakes_it_and_starts_the_minutes_again
 *
 * SPDX-License-Identifier: MIT
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "greatest.h"

#include "gfx.h"
#include "link_pages.h"
#include "overview_screen.h"
#include "screen_dim.h"
#include "settings.h"
#include "shot_gate.h"
#include "splash_screen.h"
#include "touch_merge.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"

/** @brief One minute, ms. */
#define MIN_MS SCREEN_DIM_MS_PER_MIN

/** @brief Every fact of the screenshot gate idle. */
static shot_facts_t idle_facts(void)
{
    shot_facts_t f;
    memset(&f, 1, sizeof(f));
    return f;
}

/** @brief Every fact idle, for the touch calls. */
static const shot_facts_t *idle_p(void)
{
    static shot_facts_t f;
    f = idle_facts();
    return &f;
}

/** @brief One event of contact @p id at (@p x, @p y). */
static touch_event_t ev(touch_event_type_t type, uint8_t id, int x, int y)
{
    touch_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.point.id = id;
    e.point.x = (int16_t)x;
    e.point.y = (int16_t)y;
    e.point.strength = 40;
    return e;
}

/** @brief The middle of STOP. */
static int sx(void) { return ui_band_stop_rect().x + ui_band_stop_rect().w / 2; }
/** @brief The middle of STOP. */
static int sy(void) { return ui_band_stop_rect().y + ui_band_stop_rect().h / 2; }

/** @brief The rule dimmed by 5 minutes on an idle bench, from 0ms. */
static void dimmed(screen_dim_t *d)
{
    const shot_facts_t f = idle_facts();
    screen_dim_init(d, 0u);
    CHECK_EQ(screen_dim_step(d, 5u * MIN_MS, 5, &f), SCREEN_DIM_PCT);
    CHECK(screen_dim_is_dim(d));
}

/** @brief The router on the menu, the bench SAFE and linked. */
static void router_on_menu(void)
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
    const ui_bench_status_t st = { .link_up = true };
    ui_router_set_status(&st);
    ui_router_set_alert(NULL);
    (void)ui_router_take_stop();
}

/**
 * @brief An event through the rule and, if it lets it, the router: the
 *        render loop's drain in a frame with @p facts.
 * @return whether the router took it
 */
static bool feed_in(screen_dim_t *d, touch_event_t e,
                    const shot_facts_t *facts, uint32_t now)
{
    if (!screen_dim_touch(d, &e, ui_router_stop_live(), facts, now)) {
        return false;
    }
    ui_router_event(&e);
    return true;
}

/**
 * @brief feed_in() in a frame that finds the bench idle.
 * @return whether the router took it
 */
static bool feed(screen_dim_t *d, touch_event_t e, uint32_t now)
{
    return feed_in(d, e, idle_p(), now);
}

TEST_CASE(zero_minutes_never_dims)
{
    screen_dim_t d;
    const shot_facts_t f = idle_facts();
    screen_dim_init(&d, 0u);
    for (uint32_t t = 0u; t <= 120u * MIN_MS; t += MIN_MS) {
        CHECK_EQ(screen_dim_step(&d, t, 0, &f), SCREEN_FULL_PCT);
    }
    CHECK_EQ(screen_dim_step(&d, 200u * MIN_MS, -5, &f), SCREEN_FULL_PCT);
    CHECK(!screen_dim_is_dim(&d));
}

TEST_CASE(it_dims_at_the_minutes_and_not_a_millisecond_before)
{
    static const int k_min[] = { 5, 30, 60 };
    for (size_t i = 0u; i < sizeof(k_min) / sizeof(k_min[0]); ++i) {
        screen_dim_t d;
        const shot_facts_t f = idle_facts();
        const uint32_t at = (uint32_t)k_min[i] * MIN_MS;
        screen_dim_init(&d, 1000u);
        CHECK_EQ(screen_dim_step(&d, 1000u + at - 1u, k_min[i], &f),
                 SCREEN_FULL_PCT);
        CHECK_EQ(screen_dim_step(&d, 1000u + at, k_min[i], &f),
                 SCREEN_DIM_PCT);
        /* And it stays dim without a touch. */
        CHECK_EQ(screen_dim_step(&d, 1000u + 3u * at, k_min[i], &f),
                 SCREEN_DIM_PCT);
    }
    /* The level is a number the docs state. */
    CHECK_EQ(SCREEN_DIM_PCT, 20u);
    CHECK_EQ(SCREEN_FULL_PCT, 100u);
}

/*
 * Each fact of the screenshot gate on its own: one that is not idle keeps
 * the backlight at 100%, wakes a dim one in the same frame, and starts the
 * minutes again.
 */
TEST_CASE(a_busy_bench_never_dims_and_wakes_at_once)
{
    for (size_t off = 0u; off < sizeof(shot_facts_t); ++off) {
        if (off == offsetof(shot_facts_t, no_card_fault)
            || off == offsetof(shot_facts_t, no_lit_fault)) {
            continue;   /* the_link_reports_card_line_never_keeps_it_lit,
                           fault_01_alone_lets_it_dim */
        }
        screen_dim_t d;
        shot_facts_t busy = idle_facts();
        *((bool *)&busy + off) = false;
        /* A FAULT bit other than 01. */
        busy.no_lit_fault = busy.no_lit_fault
                            && off != offsetof(shot_facts_t, no_fault);
        const shot_facts_t idle = idle_facts();

        screen_dim_init(&d, 0u);
        CHECK_EQ(screen_dim_step(&d, 10u * MIN_MS, 5, &busy),
                 SCREEN_FULL_PCT);

        dimmed(&d);
        CHECK_EQ(screen_dim_step(&d, 6u * MIN_MS, 5, &busy),
                 SCREEN_FULL_PCT);
        CHECK(!screen_dim_is_dim(&d));
        /* Idle again: the whole 5 minutes from the last busy frame. */
        CHECK_EQ(screen_dim_step(&d, 11u * MIN_MS - 1u, 5, &idle),
                 SCREEN_FULL_PCT);
        CHECK_EQ(screen_dim_step(&d, 11u * MIN_MS, 5, &idle),
                 SCREEN_DIM_PCT);
    }
    /* No facts: not idle. */
    screen_dim_t d;
    dimmed(&d);
    CHECK_EQ(screen_dim_step(&d, 6u * MIN_MS, 5, NULL), SCREEN_FULL_PCT);
}

/*
 * The panel's link report hands the logger a line for the card's fault log
 * every 60s while the link is up (panel_control.c, link_report()), and
 * log_fault_busy() is true from the hand-over until the line is on the
 * card.  The screenshot's gate waits for it; Dim after does not, or a
 * bench that sits untouched never reaches 5 minutes.  As the panel reads
 * it: a frame every 60s whose bench has the line out, the frames between
 * without.
 */
TEST_CASE(the_link_reports_card_line_never_keeps_it_lit)
{
    const shot_bench_t quiet = { .read = true, .link_up = true };
    shot_bench_t noting = quiet;
    noting.fault_log_out = true;
    shot_facts_t idle;
    shot_facts_t note;
    shot_facts_read(&quiet, &idle);
    shot_facts_read(&noting, &note);
    /* The screenshot's gate still waits for the line. */
    CHECK_EQ(shot_gate(&note), SHOT_WAIT_CARD_FAULT);
    CHECK(screen_dim_idle(&note));
    CHECK_EQ(screen_dim_wait(&note), SHOT_IDLE);

    screen_dim_t d;
    screen_dim_init(&d, 0u);
    unsigned pct = SCREEN_FULL_PCT;
    /* Frames 20ms apart for 5 minutes; a report every 60s, its line out
     * for 3 frames. */
    for (uint32_t t = 20u; t <= 5u * MIN_MS; t += 20u) {
        const bool out = (t % MIN_MS) < 60u;
        pct = screen_dim_step(&d, t, 5, out ? &note : &idle);
        if (t < 5u * MIN_MS) {
            CHECK_EQ(pct, SCREEN_FULL_PCT);
        }
    }
    CHECK_EQ(pct, SCREEN_DIM_PCT);
    /* A line out on a dim screen leaves it dim. */
    CHECK_EQ(screen_dim_step(&d, 6u * MIN_MS, 5, &note), SCREEN_DIM_PCT);
    CHECK(screen_dim_is_dim(&d));
}

/*
 * FAULT 01, LINK_FAULT_LINK_SILENT, alone: the IOmcu reports it after a
 * restart of the panel until the next ARM.  The screen dims at the minutes
 * as on a bench without a FAULT bit; the screenshot's gate still waits for
 * it and the band's FAULT chip still shows.  Bit 01 with any other bit,
 * and every other bit alone, keep the screen lit.
 */
TEST_CASE(fault_01_alone_lets_it_dim)
{
    shot_bench_t b = { .read = true, .link_up = true,
                       .faults = (uint16_t)LINK_FAULT_LINK_SILENT };
    shot_facts_t f;
    screen_dim_t d;

    shot_facts_read(&b, &f);
    CHECK_EQ(shot_gate(&f), SHOT_WAIT_FAULT);
    CHECK(screen_dim_idle(&f));
    screen_dim_init(&d, 0u);
    CHECK_EQ(screen_dim_step(&d, 5u * MIN_MS - 1u, 5, &f), SCREEN_FULL_PCT);
    CHECK_EQ(screen_dim_step(&d, 5u * MIN_MS, 5, &f), SCREEN_DIM_PCT);
    {
        const ui_bench_status_t st = { .link_up = true,
                                       .faults = b.faults };

        CHECK(ui_band_chip_rect(&st, UI_BAND_CHIP_FAULT, NULL));
    }
    for (unsigned bit = 1u; bit < 16u; ++bit) {
        for (int with01 = 0; with01 < 2; ++with01) {
            b.faults = (uint16_t)((1u << bit)
                                  | (with01 != 0 ? 1u : 0u));
            shot_facts_read(&b, &f);
            CHECK_EQ(screen_dim_wait(&f), SHOT_WAIT_FAULT);
            dimmed(&d);
            CHECK_EQ(screen_dim_step(&d, 6u * MIN_MS, 5, &f),
                     SCREEN_FULL_PCT);
            screen_dim_init(&d, 0u);
            CHECK_EQ(screen_dim_step(&d, 10u * MIN_MS, 5, &f),
                     SCREEN_FULL_PCT);
        }
    }
}

/* A press on a dim screen while the line is written is a press on an idle
 * bench: it only wakes the screen. */
TEST_CASE(a_press_while_a_card_line_is_written_only_wakes_it)
{
    const gfx_rect_t tile = overview_screen_tile_rect(0);
    const int x = tile.x + tile.w / 2;
    const int y = UI_BAND_H + tile.y + tile.h / 2;
    const uint32_t t = 6u * MIN_MS;
    router_on_menu();
    shot_facts_t note = idle_facts();
    note.no_card_fault = false;
    screen_dim_t d;
    dimmed(&d);
    CHECK(!feed_in(&d, ev(TOUCH_EVENT_DOWN, 1u, x, y), &note, t));
    CHECK(!screen_dim_is_dim(&d));
    CHECK(!feed_in(&d, ev(TOUCH_EVENT_UP, 1u, x, y), &note, t + 80u));
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/* The bench as the panel reads it: ARMED, an arm waiting, the supply or the
 * ports' 5V on or coming. */
TEST_CASE(an_armed_bench_or_an_output_on_keeps_it_lit)
{
    static const size_t k_off[] = {
        offsetof(shot_bench_t, armed),
        offsetof(shot_bench_t, arm_wanted),
        offsetof(shot_bench_t, supply_on),
        offsetof(shot_bench_t, supply_sample),
        offsetof(shot_bench_t, supply_coming),
        offsetof(shot_bench_t, prog5v_on),
        offsetof(shot_bench_t, prog5v_coming),
    };
    router_on_menu();
    const shot_bench_t quiet = { .read = true, .link_up = true };
    shot_facts_t f;
    shot_facts_read(&quiet, &f);
    screen_dim_t d;
    screen_dim_init(&d, 0u);
    CHECK_EQ(screen_dim_step(&d, 5u * MIN_MS, 5, &f), SCREEN_DIM_PCT);
    for (size_t i = 0u; i < sizeof(k_off) / sizeof(k_off[0]); ++i) {
        shot_bench_t b = quiet;
        *((bool *)&b + k_off[i]) = true;
        shot_facts_read(&b, &f);
        screen_dim_init(&d, 0u);
        CHECK_EQ(screen_dim_step(&d, 60u * MIN_MS, 5, &f), SCREEN_FULL_PCT);
    }
}

TEST_CASE(a_touch_wakes_it_and_starts_the_minutes_again)
{
    screen_dim_t d;
    const shot_facts_t f = idle_facts();
    dimmed(&d);
    const uint32_t t = 7u * MIN_MS;
    touch_event_t e = ev(TOUCH_EVENT_DOWN, 1u, 500, 300);
    (void)screen_dim_touch(&d, &e, true, idle_p(), t);
    CHECK(!screen_dim_is_dim(&d));
    CHECK_EQ(screen_dim_step(&d, t, 5, &f), SCREEN_FULL_PCT);
    e.type = TOUCH_EVENT_UP;
    (void)screen_dim_touch(&d, &e, true, idle_p(), t + 100u);
    CHECK_EQ(screen_dim_step(&d, t + 100u + 5u * MIN_MS - 1u, 5, &f),
             SCREEN_FULL_PCT);
    CHECK_EQ(screen_dim_step(&d, t + 100u + 5u * MIN_MS, 5, &f),
             SCREEN_DIM_PCT);
    /* A MOVE counts as a touch too. */
    e.type = TOUCH_EVENT_MOVE;
    (void)screen_dim_touch(&d, &e, true, idle_p(), t + 20u * MIN_MS);
    CHECK(!screen_dim_is_dim(&d));
}

/* A tap on the menu's MOTOR & ESC tile on a dim screen wakes it and opens
 * nothing; the same tap after the wake opens MOTOR & ESC. */
TEST_CASE(the_press_that_wakes_it_reaches_no_screen)
{
    router_on_menu();
    screen_dim_t d;
    dimmed(&d);
    const gfx_rect_t tile = overview_screen_tile_rect(0);
    const int x = tile.x + tile.w / 2;
    const int y = UI_BAND_H + tile.y + tile.h / 2;
    const uint32_t handed = ui_router_dispatched();
    const uint32_t t = 6u * MIN_MS;
    CHECK(!feed(&d, ev(TOUCH_EVENT_DOWN, 1u, x, y), t));
    CHECK(!feed(&d, ev(TOUCH_EVENT_MOVE, 1u, x + 2, y), t + 20u));
    CHECK(!feed(&d, ev(TOUCH_EVENT_UP, 1u, x + 2, y), t + 80u));
    CHECK(!screen_dim_is_dim(&d));
    CHECK_EQ(ui_router_dispatched(), handed);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(!ui_router_take_stop());
    /* The next tap is the tile's. */
    CHECK(feed(&d, ev(TOUCH_EVENT_DOWN, 1u, x, y), t + 500u));
    CHECK(feed(&d, ev(TOUCH_EVENT_UP, 1u, x, y), t + 580u));
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
}

/* STOP always counts: the press goes on, the router latches it, and the
 * backlight is back at 100%. */
TEST_CASE(a_press_on_stop_wakes_it_and_stops_the_bench)
{
    router_on_menu();
    CHECK(ui_router_stop_live());
    screen_dim_t d;
    dimmed(&d);
    const shot_facts_t f = idle_facts();
    const uint32_t t = 6u * MIN_MS;
    CHECK(feed(&d, ev(TOUCH_EVENT_DOWN, 3u, sx(), sy()), t));
    CHECK(ui_router_take_stop());
    CHECK_EQ(screen_dim_step(&d, t, 5, &f), SCREEN_FULL_PCT);
    CHECK(feed(&d, ev(TOUCH_EVENT_UP, 3u, sx(), sy()), t + 60u));

    /* Where no STOP is drawn, the same corner is a plain press: held. */
    dimmed(&d);
    CHECK(!screen_dim_touch(&d, &(touch_event_t){ .type = TOUCH_EVENT_DOWN,
                                .point = { .id = 3u, .x = (int16_t)sx(),
                                           .y = (int16_t)sy() } },
                            false, idle_p(), t));
}

/*
 * The step dimmed the screen, and the next frame's facts say the bench is
 * no longer idle (an output reported on, a FAULT bit) before the step that
 * lights it.  A press in that frame acts at once: on the menu, the tile's
 * tap opens MOTOR & ESC.  Each busy fact alone, and no facts at all.
 */
TEST_CASE(a_busy_frame_never_swallows_a_press)
{
    const gfx_rect_t tile = overview_screen_tile_rect(0);
    const int x = tile.x + tile.w / 2;
    const int y = UI_BAND_H + tile.y + tile.h / 2;
    const uint32_t t = 6u * MIN_MS;
    for (size_t off = 0u; off <= sizeof(shot_facts_t); ++off) {
        if (off == offsetof(shot_facts_t, no_card_fault)
            || off == offsetof(shot_facts_t, no_lit_fault)) {
            continue;   /* a_press_while_a_card_line_is_written_only_wakes_it,
                           fault_01_alone_lets_it_dim */
        }
        router_on_menu();
        screen_dim_t d;
        dimmed(&d);
        shot_facts_t busy = idle_facts();
        const shot_facts_t *fp = &busy;
        if (off < sizeof(shot_facts_t)) {
            *((bool *)&busy + off) = false;
            /* A FAULT bit other than 01. */
            busy.no_lit_fault = busy.no_lit_fault
                                && off != offsetof(shot_facts_t, no_fault);
        } else {
            fp = NULL;
        }
        CHECK(feed_in(&d, ev(TOUCH_EVENT_DOWN, 1u, x, y), fp, t));
        CHECK(!screen_dim_is_dim(&d));
        CHECK(feed_in(&d, ev(TOUCH_EVENT_UP, 1u, x, y), fp, t + 80u));
        CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    }
}

/* The first finger wakes the screen and is held; a second one that comes
 * down while the first rests is the screen's, STOP included. */
TEST_CASE(a_second_finger_after_the_wake_is_the_screens)
{
    router_on_menu();
    screen_dim_t d;
    dimmed(&d);
    const uint32_t t = 6u * MIN_MS;
    CHECK(!feed(&d, ev(TOUCH_EVENT_DOWN, 1u, 400, 300), t));
    CHECK(feed(&d, ev(TOUCH_EVENT_DOWN, 2u, sx(), sy()), t + 30u));
    CHECK(ui_router_take_stop());
    CHECK(!feed(&d, ev(TOUCH_EVENT_MOVE, 1u, 410, 300), t + 40u));
    CHECK(feed(&d, ev(TOUCH_EVENT_UP, 2u, sx(), sy()), t + 50u));
    CHECK(!feed(&d, ev(TOUCH_EVENT_UP, 1u, 410, 300), t + 60u));
    /* Both gone: the next press of contact 1 goes on. */
    CHECK(feed(&d, ev(TOUCH_EVENT_DOWN, 1u, 400, 300), t + 90u));
}

/* A finger that rests through the minutes emits nothing; when it moves or
 * lifts, the screen that owns it gets the event. */
TEST_CASE(a_contact_down_before_the_dim_goes_on)
{
    screen_dim_t d;
    const shot_facts_t f = idle_facts();
    screen_dim_init(&d, 0u);
    touch_event_t e = ev(TOUCH_EVENT_DOWN, 4u, 300, 300);
    CHECK(screen_dim_touch(&d, &e, true, idle_p(), 0u));
    CHECK_EQ(screen_dim_step(&d, 5u * MIN_MS, 5, &f), SCREEN_DIM_PCT);
    e.type = TOUCH_EVENT_MOVE;
    CHECK(screen_dim_touch(&d, &e, true, idle_p(), 5u * MIN_MS + 10u));
    CHECK(!screen_dim_is_dim(&d));
    e.type = TOUCH_EVENT_UP;
    CHECK(screen_dim_touch(&d, &e, true, idle_p(), 5u * MIN_MS + 20u));
}

/* A release that went missing: the next DOWN of the same id is a new
 * contact on an awake screen and goes on. */
TEST_CASE(a_down_reusing_a_held_id_is_a_new_press)
{
    screen_dim_t d;
    dimmed(&d);
    touch_event_t e = ev(TOUCH_EVENT_DOWN, 1u, 300, 300);
    CHECK(!screen_dim_touch(&d, &e, true, idle_p(), 6u * MIN_MS));
    CHECK(screen_dim_touch(&d, &e, true, idle_p(), 6u * MIN_MS + 500u));
    e.type = TOUCH_EVENT_UP;
    CHECK(screen_dim_touch(&d, &e, true, idle_p(), 6u * MIN_MS + 600u));
}

/* Releases lost every time the screen woke: the held list fills, and a
 * press on a dim screen is still held. */
TEST_CASE(lost_releases_never_let_a_waking_press_through)
{
    screen_dim_t d;
    const shot_facts_t f = idle_facts();
    screen_dim_init(&d, 0u);
    uint32_t t = 0u;
    for (uint8_t id = 0u; id < 2u * TOUCH_MAX_POINTS; ++id) {
        t += 5u * MIN_MS;
        CHECK_EQ(screen_dim_step(&d, t, 5, &f), SCREEN_DIM_PCT);
        touch_event_t e = ev(TOUCH_EVENT_DOWN, id, 300, 300);
        CHECK(!screen_dim_touch(&d, &e, true, idle_p(), t));
        CHECK(d.held_n <= TOUCH_MAX_POINTS);
    }
    /* The newest is held: its release goes nowhere. */
    touch_event_t up = ev(TOUCH_EVENT_UP, 2u * TOUCH_MAX_POINTS - 1u, 300,
                          300);
    CHECK(!screen_dim_touch(&d, &up, true, idle_p(), t + 10u));
}

TEST_CASE(the_clock_wraps)
{
    screen_dim_t d;
    const shot_facts_t f = idle_facts();
    const uint32_t start = UINT32_MAX - 1000u;
    screen_dim_init(&d, start);
    CHECK_EQ(screen_dim_step(&d, start + 5u * MIN_MS - 1u, 5, &f),
             SCREEN_FULL_PCT);
    CHECK_EQ(screen_dim_step(&d, start + 5u * MIN_MS, 5, &f),
             SCREEN_DIM_PCT);
}

TEST_CASE(null_is_refused)
{
    screen_dim_t d;
    const shot_facts_t f = idle_facts();
    screen_dim_init(NULL, 0u);
    CHECK_EQ(screen_dim_step(NULL, 0u, 5, &f), SCREEN_FULL_PCT);
    const touch_event_t e = ev(TOUCH_EVENT_DOWN, 1u, 1, 1);
    CHECK(!screen_dim_touch(NULL, &e, true, idle_p(), 0u));
    dimmed(&d);
    CHECK(!screen_dim_touch(&d, NULL, true, idle_p(), 0u));
    CHECK(screen_dim_is_dim(&d));
    CHECK(!screen_dim_is_dim(NULL));
}

/** @brief screen_dim_line() of @p d at @p now with @p minutes, in @p buf. */
static const char *line_of(const screen_dim_t *d, uint32_t now, int minutes)
{
    static char buf[SCREEN_DIM_LINE_MAX];
    CHECK(screen_dim_line(d, now, minutes, buf, sizeof(buf)) > 0u);
    return buf;
}

/*
 * The console's line: the setting, dim or awake, the seconds since the
 * minutes started and what started them, each of the four causes.
 */
TEST_CASE(the_line_names_what_started_the_minutes)
{
    screen_dim_t d;
    const shot_facts_t f = idle_facts();
    screen_dim_init(&d, 1000u);
    CHECK_STR_EQ(line_of(&d, 38999u, 5), "dim: after 5min awake, 37s since start");

    touch_event_t e = ev(TOUCH_EVENT_UP, (uint8_t)(1u | TOUCH_MERGE_INJECT_ID),
                         10, 10);
    (void)screen_dim_touch(&d, &e, false, &f, 2000u);
    CHECK_STR_EQ(line_of(&d, 2000u + 37000u, 5),
                 "dim: after 5min awake, 37s since touch UP id 0x81 injector");
    e = ev(TOUCH_EVENT_DOWN, 0u, 10, 10);
    (void)screen_dim_touch(&d, &e, false, &f, 3000u);
    CHECK_STR_EQ(line_of(&d, 3000u, 5),
                 "dim: after 5min awake, 0s since touch DOWN id 0x00 glass");
    e = ev(TOUCH_EVENT_MOVE, 4u, 10, 10);
    (void)screen_dim_touch(&d, &e, false, &f, 3000u);
    CHECK_STR_EQ(line_of(&d, 3000u, 5),
                 "dim: after 5min awake, 0s since touch MOVE id 0x04 glass");

    /* A busy frame names the gate's first fact; the card's line is none. */
    shot_facts_t busy = idle_facts();
    busy.safe = false;
    busy.no_card_fault = false;
    (void)screen_dim_step(&d, 4000u, 5, &busy);
    CHECK_STR_EQ(line_of(&d, 64999u, 5),
                 "dim: after 5min awake, 60s since busy armed");
    busy = idle_facts();
    busy.no_recording = false;
    busy.no_card_fault = false;
    (void)screen_dim_step(&d, 4000u, 5, &busy);
    CHECK_STR_EQ(line_of(&d, 4000u, 5),
                 "dim: after 5min awake, 0s since busy recording");
    (void)screen_dim_step(&d, 4000u, 5, NULL);
    CHECK_STR_EQ(line_of(&d, 4000u, 5),
                 "dim: after 5min awake, 0s since busy link");

    /* 0 minutes. */
    (void)screen_dim_step(&d, 5000u, 0, &f);
    CHECK_STR_EQ(line_of(&d, 5000u, 0), "dim: after 0min awake, 0s since off");

    /* Dim: the cause stays the last one that started the minutes. */
    CHECK_EQ(screen_dim_step(&d, 5000u + 5u * MIN_MS, 5, &f), SCREEN_DIM_PCT);
    CHECK_STR_EQ(line_of(&d, 5000u + 6u * MIN_MS, 5),
                 "dim: after 5min dim, 360s since off");
}

/* The longest line fits SCREEN_DIM_LINE_MAX; a short buffer is cut and
 * terminated; NULL writes nothing. */
TEST_CASE(the_line_fits_its_buffer)
{
    screen_dim_t d;
    const shot_facts_t f = idle_facts();
    screen_dim_init(&d, 0u);
    touch_event_t e = ev(TOUCH_EVENT_MOVE, 0xFFu, 10, 10);
    (void)screen_dim_touch(&d, &e, false, &f, 0u);
    char buf[SCREEN_DIM_LINE_MAX];
    const size_t n = screen_dim_line(&d, UINT32_MAX, INT32_MIN, buf,
                                     sizeof(buf));
    CHECK_EQ(n, strlen(buf));
    CHECK(n < sizeof(buf));
    CHECK_STR_EQ(buf, "dim: after -2147483648min awake, 4294967s since "
                      "touch MOVE id 0xff injector");
    char small[8];
    CHECK_EQ(screen_dim_line(&d, 0u, 5, small, sizeof(small)), 7u);
    CHECK_STR_EQ(small, "dim: af");
    CHECK_EQ(screen_dim_line(NULL, 0u, 5, buf, sizeof(buf)), 0u);
    CHECK_EQ(screen_dim_line(&d, 0u, 5, NULL, 4u), 0u);
    CHECK_EQ(screen_dim_line(&d, 0u, 5, buf, 0u), 0u);
    /* Every fact of the gate has a name. */
    for (int w = 0; w < (int)SHOT_WAIT_COUNT; ++w) {
        CHECK(strcmp(shot_wait_name((shot_wait_t)w), "?") != 0);
    }
    CHECK_STR_EQ(shot_wait_name(SHOT_WAIT_COUNT), "?");
    CHECK_STR_EQ(shot_wait_name(SHOT_WAIT_CARD_FAULT), "card_fault");
    /* A cause outside the enum reads as the start. */
    d.why = 0xEEu;
    CHECK_STR_EQ(line_of(&d, 0u, 5), "dim: after 5min awake, 0s since start");
    d.why = (uint8_t)SCREEN_DIM_WHY_TOUCH;
    d.why_type = 9u;
    CHECK_STR_EQ(line_of(&d, 0u, 5),
                 "dim: after 5min awake, 0s since touch ? id 0xff injector");
}

/* The M5Stack Dial's turn, PRESS or HOLD: no touch, but it wakes the
 * backlight and starts the minutes again; the line names it. */
TEST_CASE(the_dials_input_wakes_it_and_starts_the_minutes_again)
{
    screen_dim_t d;
    screen_dim_init(&d, 0u);
    const shot_facts_t f = idle_facts();
    CHECK_EQ(screen_dim_step(&d, 5u * MIN_MS, 5, &f), SCREEN_DIM_PCT);
    CHECK(screen_dim_is_dim(&d));
    CHECK(screen_dim_wake(&d, 5u * MIN_MS + 10u));
    CHECK(!screen_dim_is_dim(&d));
    CHECK_EQ(screen_dim_step(&d, 5u * MIN_MS + 20u, 5, &f), SCREEN_FULL_PCT);
    char line[SCREEN_DIM_LINE_MAX];
    (void)screen_dim_line(&d, 5u * MIN_MS + 20u, 5, line, sizeof(line));
    CHECK(strstr(line, "since dial") != NULL);
    /* Awake: it reports no wake, and the minutes start from it. */
    CHECK(!screen_dim_wake(&d, 9u * MIN_MS));
    CHECK_EQ(screen_dim_step(&d, 14u * MIN_MS - 1u, 5, &f), SCREEN_FULL_PCT);
    CHECK_EQ(screen_dim_step(&d, 14u * MIN_MS, 5, &f), SCREEN_DIM_PCT);
    CHECK(!screen_dim_wake(NULL, 0u));
}

int main(void)
{
    RUN(zero_minutes_never_dims);
    RUN(it_dims_at_the_minutes_and_not_a_millisecond_before);
    RUN(a_busy_bench_never_dims_and_wakes_at_once);
    RUN(the_link_reports_card_line_never_keeps_it_lit);
    RUN(fault_01_alone_lets_it_dim);
    RUN(a_press_while_a_card_line_is_written_only_wakes_it);
    RUN(an_armed_bench_or_an_output_on_keeps_it_lit);
    RUN(a_touch_wakes_it_and_starts_the_minutes_again);
    RUN(the_press_that_wakes_it_reaches_no_screen);
    RUN(a_press_on_stop_wakes_it_and_stops_the_bench);
    RUN(a_busy_frame_never_swallows_a_press);
    RUN(a_second_finger_after_the_wake_is_the_screens);
    RUN(a_contact_down_before_the_dim_goes_on);
    RUN(a_down_reusing_a_held_id_is_a_new_press);
    RUN(lost_releases_never_let_a_waking_press_through);
    RUN(the_clock_wraps);
    RUN(null_is_refused);
    RUN(the_line_names_what_started_the_minutes);
    RUN(the_line_fits_its_buffer);
    RUN(the_dials_input_wakes_it_and_starts_the_minutes_again);
    return test_summary("screen_dim");
}
