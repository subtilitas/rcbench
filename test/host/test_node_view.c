/*
 * The display node's screen without hardware
 * (firmware/node/main/node_view.c).
 *
 * The failures this is about: a value written past its field or with a
 * space before its unit, a rounding that shows -0.0A, a highest or lowest
 * that misses a window or keeps a reset value, a tap taken for the hold or
 * the hold for a tap, a hold that resets twice, a tap target that covers
 * the live value, German texts without a link, and a frame that redraws
 * what did not change.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "gfx_damage.h"
#include "node_view.h"
#include "rcnode.h"

/** @brief The frame the render cases draw into. */
static gfx_color_t s_fb[NV_W * NV_H];

/**
 * @brief nv_fmt() into a buffer of 12.
 * @param q NV_Q_*.
 * @param v The value.
 * @return The text, in a static buffer.
 */
static const char *fmt(int q, int32_t v)
{
    static char b[12];
    if (nv_fmt(q, v, b, sizeof b) == 0u) {
        (void)strcpy(b, "?");
    }
    return b;
}

/**
 * @brief nv_fmt_unit() into a buffer of 12.
 * @param q NV_Q_*.
 * @param v The value.
 * @return The text, in a static buffer.
 */
static const char *fmt_unit(int q, int32_t v)
{
    static char b[12];
    if (nv_fmt_unit(q, v, b, sizeof b) == 0u) {
        (void)strcpy(b, "?");
    }
    return b;
}

TEST_CASE(voltage_shows_0_00v_to_60_00v_and_clamps_at_99_99)
{
    CHECK_STR_EQ(fmt(NV_Q_V, 0), "0.00");
    CHECK_STR_EQ(fmt(NV_Q_V, 60000), "60.00");
    CHECK_STR_EQ(fmt(NV_Q_V, 12604), "12.60");
    CHECK_STR_EQ(fmt(NV_Q_V, 12605), "12.61");
    CHECK_STR_EQ(fmt(NV_Q_V, 99999), "99.99");
    CHECK_STR_EQ(fmt(NV_Q_V, 200000), "99.99");
    CHECK_STR_EQ(fmt(NV_Q_V, -40), "0.00");
    CHECK_STR_EQ(fmt_unit(NV_Q_V, 12600), "12.60V");
}

TEST_CASE(current_shows_0_0a_to_300_0a_half_away_and_no_minus_zero)
{
    CHECK_STR_EQ(fmt(NV_Q_A, 0), "0.0");
    CHECK_STR_EQ(fmt(NV_Q_A, 300000), "300.0");
    CHECK_STR_EQ(fmt(NV_Q_A, 35249), "35.2");
    CHECK_STR_EQ(fmt(NV_Q_A, 35250), "35.3");
    CHECK_STR_EQ(fmt(NV_Q_A, -35250), "-35.3");
    CHECK_STR_EQ(fmt(NV_Q_A, -49), "0.0");
    CHECK_STR_EQ(fmt(NV_Q_A, -50), "-0.1");
    CHECK_STR_EQ(fmt(NV_Q_A, 2000000), "999.9");
    CHECK_STR_EQ(fmt(NV_Q_A, -2000000), "-999.9");
    CHECK_STR_EQ(fmt(NV_Q_A, INT32_MIN), "-999.9");
    CHECK_STR_EQ(fmt_unit(NV_Q_A, 35200), "35.2A");
}

TEST_CASE(power_has_a_decimal_under_100w_and_reaches_18000w)
{
    CHECK_STR_EQ(fmt(NV_Q_W, 0), "0.0");
    CHECK_STR_EQ(fmt(NV_Q_W, 12600), "12.6");
    CHECK_STR_EQ(fmt(NV_Q_W, 99949), "99.9");
    CHECK_STR_EQ(fmt(NV_Q_W, 99950), "100");
    CHECK_STR_EQ(fmt(NV_Q_W, 443000), "443");
    CHECK_STR_EQ(fmt(NV_Q_W, 18000000), "18000");
    CHECK_STR_EQ(fmt(NV_Q_W, -5000000), "-5000");
    CHECK_STR_EQ(fmt(NV_Q_W, -40), "0.0");
    CHECK_STR_EQ(fmt(NV_Q_W, INT32_MAX), "99999");
    CHECK_STR_EQ(fmt_unit(NV_Q_W, 443000), "443W");
    CHECK_STR_EQ(fmt_unit(NV_Q_W, 18000000), "18000W");
}

TEST_CASE(a_format_refuses_an_unknown_quantity_and_a_small_buffer)
{
    char b[12];
    CHECK_EQ(nv_fmt(NV_Q_N, 1, b, sizeof b), 0u);
    CHECK_EQ(nv_fmt(NV_Q_V, 1, b, 7u), 0u);
    CHECK_EQ(nv_fmt(NV_Q_V, 1, NULL, sizeof b), 0u);
    CHECK_EQ(nv_fmt_unit(NV_Q_A, -2000000, b, 8u), 7u);   /* "-999.9A" and NUL */
    CHECK_EQ(nv_fmt_unit(NV_Q_A, -2000000, b, 7u), 0u);
}

TEST_CASE(the_widest_texts_fit_their_fields)
{
    /* The live value's digits from the left edge to NV_VAL_RIGHT. */
    const int32_t wide[NV_Q_N] = { 99999, -2000000, -200000000 };
    for (int q = 0; q < NV_Q_N; ++q) {
        CHECK(gfx_text_width(&gfx_font_num_32x44, fmt(q, wide[q]), 1)
              <= NV_VAL_RIGHT);
        CHECK(gfx_text_width(&gfx_font_10x20, fmt_unit(q, wide[q]), 1)
              <= NV_W - NV_EXT_TEXT_X);
    }
    /* The band's texts in both languages, linked and not. */
    for (uint8_t lang = 0u; lang <= RCNODE_LANG_DE; ++lang) {
        for (int linked = 0; linked < 2; ++linked) {
            CHECK(gfx_text_width(&gfx_font_10x20,
                                 nv_text_link(lang, linked != 0), 1)
                  <= NV_BAND_SPLIT - NV_BAND_PAD);
            nv_model_t m;
            memset(&m, 0, sizeof m);
            m.lang = lang;
            m.linked = linked != 0;
            m.hold_pm = 500u;
            CHECK(gfx_text_width(&gfx_font_10x20, nv_text_right(&m), 1)
                  <= NV_W - NV_BAND_SPLIT - NV_BAND_PAD);
            m.hold_pm = 0u;
            CHECK(gfx_text_width(&gfx_font_10x20, nv_text_right(&m), 1)
                  <= NV_W - NV_BAND_SPLIT - NV_BAND_PAD);
        }
    }
}

TEST_CASE(the_extremes_are_the_single_windows_and_the_live_value_their_mean)
{
    nv_stats_t s;
    nv_stats_init(&s);
    nv_stats_add(&s, 25000, 10000000);   /* 25V 10A */
    nv_stats_add(&s, 24000, 30000000);   /* 24V 30A */
    nv_stats_add(&s, 24500, -2000000);   /* 24.5V -2A */
    nv_frame_t f;
    nv_stats_frame(&s, &f);
    CHECK(f.ok);
    CHECK_EQ(f.val[NV_Q_V], 24500);
    CHECK_EQ(f.val[NV_Q_A], 12667);
    CHECK_EQ(f.val[NV_Q_W], (250000 + 720000 - 49000) / 3 + 0);
    CHECK_EQ(f.ext[NV_Q_V].lo, 24000);
    CHECK_EQ(f.ext[NV_Q_V].hi, 25000);
    CHECK_EQ(f.ext[NV_Q_A].lo, -2000);
    CHECK_EQ(f.ext[NV_Q_A].hi, 30000);
    CHECK_EQ(f.ext[NV_Q_W].lo, -49000);
    CHECK_EQ(f.ext[NV_Q_W].hi, 720000);
    /* No window since: no live value, the extremes stay. */
    nv_stats_frame(&s, &f);
    CHECK(!f.ok);
    CHECK(f.ext[NV_Q_A].have);
    CHECK_EQ(f.ext[NV_Q_A].hi, 30000);
}

TEST_CASE(sixty_volts_at_300a_is_18000w_from_a_window)
{
    nv_stats_t s;
    nv_stats_init(&s);
    rcnode_pwr_win_t w;
    memset(&w, 0, sizeof w);
    w.n = 18u;
    w.v_mean = 30000u;              /* 2mV steps: 60.000V */
    w.i_mean = 300000000;           /* 300A */
    w.i_max = 300000000;
    nv_stats_window(&s, &w);
    nv_frame_t f;
    nv_stats_frame(&s, &f);
    CHECK(f.ok);
    CHECK_EQ(f.val[NV_Q_V], 60000);
    CHECK_EQ(f.val[NV_Q_A], 300000);
    CHECK_EQ(f.val[NV_Q_W], 18000000);
    CHECK_STR_EQ(fmt_unit(NV_Q_W, f.val[NV_Q_W]), "18000W");
}

TEST_CASE(a_window_without_a_sample_or_a_current_counts_for_nothing)
{
    nv_stats_t s;
    nv_stats_init(&s);
    rcnode_pwr_win_t w;
    memset(&w, 0, sizeof w);
    w.n = 0u;
    w.i_max = RCNODE_I_NONE;
    w.v_min = RCNODE_V_NONE;
    nv_stats_window(&s, &w);
    w.n = 18u;                      /* every current sample clipped */
    w.v_mean = 11100u;
    nv_stats_window(&s, &w);
    nv_stats_window(&s, NULL);
    nv_stats_window(NULL, &w);
    nv_frame_t f;
    nv_stats_frame(&s, &f);
    CHECK(!f.ok);
    CHECK(!f.ext[NV_Q_V].have);
    nv_stats_frame(NULL, &f);
    CHECK(!f.ok);
}

TEST_CASE(a_reset_clears_one_row_or_all_and_the_next_window_sets_them)
{
    nv_stats_t s;
    nv_stats_init(&s);
    nv_stats_add(&s, 25000, 10000000);
    nv_stats_add(&s, 20000, 50000000);
    nv_stats_reset(&s, NV_Q_A);
    CHECK(s.ext[NV_Q_V].have);
    CHECK(!s.ext[NV_Q_A].have);
    CHECK(s.ext[NV_Q_W].have);
    nv_stats_add(&s, 22000, 5000000);
    CHECK_EQ(s.ext[NV_Q_A].lo, 5000);
    CHECK_EQ(s.ext[NV_Q_A].hi, 5000);
    CHECK_EQ(s.ext[NV_Q_V].lo, 20000);
    nv_stats_reset(&s, NV_Q_N);
    for (int q = 0; q < NV_Q_N; ++q) {
        CHECK(!s.ext[q].have);
    }
    nv_stats_add(&s, 12000, 1000000);
    CHECK_EQ(s.ext[NV_Q_V].lo, 12000);
    CHECK_EQ(s.ext[NV_Q_V].hi, 12000);
    CHECK_EQ(s.ext[NV_Q_W].hi, 12000);
}

TEST_CASE(a_press_under_2s_is_a_tap_at_its_start_60ms_after_the_last_touch)
{
    nv_touch_t t;
    memset(&t, 0, sizeof t);
    CHECK_EQ(nv_touch_step(&t, true, 250, 60, 1000u), NV_TOUCH_NONE);
    CHECK_EQ(nv_touch_step(&t, true, 260, 70, 1020u), NV_TOUCH_NONE);
    /* A read without the finger inside the press: still the press. */
    CHECK_EQ(nv_touch_step(&t, false, 0, 0, 1040u), NV_TOUCH_NONE);
    CHECK_EQ(nv_touch_step(&t, true, 262, 72, 1060u), NV_TOUCH_NONE);
    CHECK_EQ(nv_touch_step(&t, false, 0, 0, 1080u), NV_TOUCH_NONE);
    CHECK_EQ(nv_touch_step(&t, false, 0, 0, 1100u), NV_TOUCH_NONE);
    CHECK_EQ(nv_touch_step(&t, false, 0, 0, 1120u), NV_TOUCH_TAP);
    CHECK_EQ(t.x, 250);
    CHECK_EQ(t.y, 60);
    CHECK_EQ(nv_touch_step(&t, false, 0, 0, 1140u), NV_TOUCH_NONE);
}

TEST_CASE(a_hold_fires_once_at_2s_and_its_release_is_no_tap)
{
    nv_touch_t t;
    memset(&t, 0, sizeof t);
    uint32_t now = 0xFFFFF000u;     /* across the clock's wrap */
    CHECK_EQ(nv_touch_step(&t, true, 10, 100, now), NV_TOUCH_NONE);
    unsigned holds = 0u;
    unsigned taps = 0u;
    for (uint32_t k = 20u; k <= 3000u; k += 20u) {
        const nv_touch_ev_t e = nv_touch_step(&t, true, 10, 100, now + k);
        holds += e == NV_TOUCH_HOLD ? 1u : 0u;
        if (e == NV_TOUCH_HOLD) {
            CHECK_EQ(k, NV_HOLD_MS);
        }
    }
    for (uint32_t k = 3020u; k <= 3200u; k += 20u) {
        taps += nv_touch_step(&t, false, 0, 0, now + k) == NV_TOUCH_TAP ? 1u : 0u;
    }
    CHECK_EQ(holds, 1u);
    CHECK_EQ(taps, 0u);
    /* 1999ms is still a tap. */
    memset(&t, 0, sizeof t);
    CHECK_EQ(nv_touch_step(&t, true, 10, 100, 0u), NV_TOUCH_NONE);
    CHECK_EQ(nv_touch_step(&t, true, 10, 100, NV_HOLD_MS - 1u), NV_TOUCH_NONE);
    CHECK_EQ(nv_touch_step(&t, false, 0, 0, NV_HOLD_MS + NV_UP_MS),
             NV_TOUCH_TAP);
    CHECK_EQ(nv_touch_step(NULL, true, 0, 0, 0u), NV_TOUCH_NONE);
}

TEST_CASE(the_hold_bar_shows_from_300ms_to_the_hold)
{
    nv_touch_t t;
    memset(&t, 0, sizeof t);
    CHECK_EQ(nv_touch_progress(&t, 0u), 0u);
    (void)nv_touch_step(&t, true, 10, 100, 100u);
    CHECK_EQ(nv_touch_progress(&t, 100u + NV_HINT_MS - 1u), 0u);
    CHECK_EQ(nv_touch_progress(&t, 100u + NV_HINT_MS), 150u);
    CHECK_EQ(nv_touch_progress(&t, 1100u), 500u);
    CHECK_EQ(nv_touch_progress(&t, 100u + NV_HOLD_MS + 500u), 1000u);
    (void)nv_touch_step(&t, true, 10, 100, 100u + NV_HOLD_MS);
    CHECK_EQ(nv_touch_progress(&t, 100u + NV_HOLD_MS), 0u);
    CHECK_EQ(nv_touch_progress(NULL, 0u), 0u);
}

TEST_CASE(only_a_rows_highest_and_lowest_are_tap_targets)
{
    CHECK_EQ(nv_hit(NV_SPLIT_X - 1, 60), -1);
    CHECK_EQ(nv_hit(NV_SPLIT_X, NV_BAND_H), NV_Q_V);
    CHECK_EQ(nv_hit(NV_W - 1, NV_BAND_H + NV_ROW_H - 1), NV_Q_V);
    CHECK_EQ(nv_hit(300, NV_BAND_H + NV_ROW_H), NV_Q_A);
    CHECK_EQ(nv_hit(300, NV_BAND_H + 2 * NV_ROW_H), NV_Q_W);
    CHECK_EQ(nv_hit(300, NV_H - 1), NV_Q_W);
    CHECK_EQ(nv_hit(300, NV_BAND_H - 1), -1);
    CHECK_EQ(nv_hit(NV_W, 60), -1);
    CHECK_EQ(nv_hit(300, NV_H), -1);
}

TEST_CASE(the_touch_map_turns_portrait_to_landscape_and_clamps)
{
    int x = -1;
    int y = -1;
    nv_touch_map(0, 0, &x, &y);
    CHECK_EQ(x, 0);
    CHECK_EQ(y, NV_H - 1);
    nv_touch_map(239, 319, &x, &y);
    CHECK_EQ(x, NV_W - 1);
    CHECK_EQ(y, 0);
    nv_touch_map(400, 900, &x, &y);
    CHECK_EQ(x, NV_W - 1);
    CHECK_EQ(y, 0);
    nv_touch_map(-5, -5, &x, &y);
    CHECK_EQ(x, 0);
    CHECK_EQ(y, NV_H - 1);
}

TEST_CASE(texts_are_english_without_a_link_and_german_only_with_one)
{
    CHECK_STR_EQ(nv_text_link(RCNODE_LANG_DE, false), "STANDALONE");
    CHECK_STR_EQ(nv_text_link(RCNODE_LANG_EN, false), "STANDALONE");
    CHECK_STR_EQ(nv_text_link(RCNODE_LANG_DE, true), "VERBUNDEN");
    CHECK_STR_EQ(nv_text_link(RCNODE_LANG_EN, true), "CONNECTED");
    CHECK_STR_EQ(nv_text_link(9u, true), "CONNECTED");
    nv_model_t m;
    memset(&m, 0, sizeof m);
    m.lang = RCNODE_LANG_DE;
    CHECK_STR_EQ(nv_text_right(&m), "NO SENSOR");
    m.linked = true;
    CHECK_STR_EQ(nv_text_right(&m), "KEIN SENSOR");
    m.hold_pm = 1u;
    CHECK_STR_EQ(nv_text_right(&m), "ALLE ZURÜCKSETZEN");
    m.linked = false;
    CHECK_STR_EQ(nv_text_right(&m), "RESET ALL");
    m.hold_pm = 0u;
    m.f.ok = true;
    CHECK_STR_EQ(nv_text_right(&m), "");
    CHECK_STR_EQ(nv_text_right(NULL), "");
}

/**
 * @brief A model with values: 22.20V, 35.2A, 781W, highest and lowest set.
 * @param m Receives it.
 */
static void model(nv_model_t *m)
{
    memset(m, 0, sizeof *m);
    nv_stats_t s;
    nv_stats_init(&s);
    nv_stats_add(&s, 22900, 2100000);
    nv_stats_add(&s, 21500, 68400000);
    nv_stats_add(&s, 22200, 35200000);
    nv_stats_frame(&s, &m->f);
}

TEST_CASE(a_frame_draws_all_once_then_only_what_changed)
{
    gfx_canvas_t c;
    gfx_canvas_init(&c, s_fb, NV_W, NV_H, 0);
    nv_view_t v;
    nv_view_init(&v);
    nv_model_t m;
    model(&m);
    /* The background, 3 band parts, 3 rows of 3. */
    CHECK_EQ(nv_render(&v, &c, &m), 13);
    CHECK_EQ(nv_render(&v, &c, &m), 0);
    m.f.val[NV_Q_A] = 35300;
    CHECK_EQ(nv_render(&v, &c, &m), 1);
    m.f.val[NV_Q_A] = 35310;        /* the same text */
    CHECK_EQ(nv_render(&v, &c, &m), 0);
    m.hold_pm = 400u;               /* the bar and the right text */
    CHECK_EQ(nv_render(&v, &c, &m), 2);
    m.f.ext[NV_Q_V].have = false;   /* a row's tap: its two */
    CHECK_EQ(nv_render(&v, &c, &m), 2);
    CHECK_STR_EQ(v.hi[NV_Q_V], "--");
    CHECK_STR_EQ(v.val[NV_Q_A], "35.3");
    CHECK_STR_EQ(v.hi[NV_Q_A], "68.4A");
    CHECK_STR_EQ(v.lo[NV_Q_W], "48.1W");
    CHECK_EQ(nv_render(NULL, &c, &m), 0);
    CHECK_EQ(nv_render(&v, NULL, &m), 0);
    CHECK_EQ(nv_render(&v, &c, NULL), 0);
}

TEST_CASE(a_change_sends_only_its_own_rectangle)
{
    gfx_canvas_t c;
    gfx_canvas_init(&c, s_fb, NV_W, NV_H, 0);
    gfx_damage_t d;
    gfx_damage_init(&d, &c);
    nv_view_t v;
    nv_view_init(&v);
    nv_model_t m;
    model(&m);
    (void)nv_render(&v, &c, &m);
    m.f.val[NV_Q_W] = 999000;
    gfx_damage_reset(&d);
    gfx_damage_attach(&d);
    CHECK_EQ(nv_render(&v, &c, &m), 1);
    gfx_damage_attach(NULL);
    gfx_rect_t r[GFX_DAMAGE_MAX_RECTS];
    const int n = gfx_damage_rects(&d, r, GFX_DAMAGE_MAX_RECTS);
    CHECK(n >= 1);
    for (int i = 0; i < n; ++i) {
        /* Inside the power row's live value. */
        CHECK(r[i].x + r[i].w <= NV_SPLIT_X);
        CHECK(r[i].y >= NV_BAND_H + 2 * NV_ROW_H - GFX_DAMAGE_BAND_H);
    }
    /* The band keeps its colour, the rows theirs. */
    CHECK_EQ(gfx_pixel_get(&c, 1, 1), GFX_GREY(28));
    CHECK_EQ(gfx_pixel_get(&c, 1, NV_BAND_H + 2), GFX_BLACK);
}

int main(void)
{
    RUN(voltage_shows_0_00v_to_60_00v_and_clamps_at_99_99);
    RUN(current_shows_0_0a_to_300_0a_half_away_and_no_minus_zero);
    RUN(power_has_a_decimal_under_100w_and_reaches_18000w);
    RUN(a_format_refuses_an_unknown_quantity_and_a_small_buffer);
    RUN(the_widest_texts_fit_their_fields);
    RUN(the_extremes_are_the_single_windows_and_the_live_value_their_mean);
    RUN(sixty_volts_at_300a_is_18000w_from_a_window);
    RUN(a_window_without_a_sample_or_a_current_counts_for_nothing);
    RUN(a_reset_clears_one_row_or_all_and_the_next_window_sets_them);
    RUN(a_press_under_2s_is_a_tap_at_its_start_60ms_after_the_last_touch);
    RUN(a_hold_fires_once_at_2s_and_its_release_is_no_tap);
    RUN(the_hold_bar_shows_from_300ms_to_the_hold);
    RUN(only_a_rows_highest_and_lowest_are_tap_targets);
    RUN(the_touch_map_turns_portrait_to_landscape_and_clamps);
    RUN(texts_are_english_without_a_link_and_german_only_with_one);
    RUN(a_frame_draws_all_once_then_only_what_changed);
    RUN(a_change_sends_only_its_own_rectangle);
    return test_summary("node_view");
}
