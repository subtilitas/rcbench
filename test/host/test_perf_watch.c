/*
 * The statistics behind the panel's console lines: the frame window, the
 * table of lows and the line schedule (perf_watch.h).
 *
 * At the limits: an empty window, one frame, a screen change in the middle,
 * a mean that rounds, sums past 2^32us; a name at the cut length and one
 * past it, a figure that holds, rises and falls, a full table, a row due
 * twice, the last row of a full table; the schedule at frame 0, 1, 2, 299,
 * 300, 301 and across the wrap of 2^32.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "perf_watch.h"

TEST_CASE(the_window_is_300_frames_and_a_task_name_is_16_bytes)
{
    CHECK_EQ(PERF_WINDOW_FRAMES, 300);
    CHECK_EQ(PERF_LOWS_NAME, 16);
}

TEST_CASE(an_empty_window_reports_zero_everywhere)
{
    perf_window_t w;
    perf_window_start(&w, 7);
    CHECK_EQ(w.screen, 7);
    CHECK_EQ(w.frames, 0);
    CHECK_EQ(w.draw.min_us, 0);
    CHECK_EQ(w.draw.max_us, 0);
    CHECK_EQ(perf_span_mean_us(&w.draw, w.frames), 0);
    CHECK_EQ(perf_span_mean_us(&w.wait, w.frames), 0);
}

TEST_CASE(one_frame_is_its_own_minimum_mean_and_maximum)
{
    perf_window_t w;
    perf_window_start(&w, 1);
    perf_window_add(&w, 1, 3800u, 12000u, 0u);
    CHECK_EQ(w.frames, 1);
    CHECK_EQ(w.draw.min_us, 3800);
    CHECK_EQ(w.draw.max_us, 3800);
    CHECK_EQ(w.wait.min_us, 12000);
    CHECK_EQ(w.wait.max_us, 12000);
    CHECK_EQ(perf_span_mean_us(&w.draw, w.frames), 3800);
}

TEST_CASE(a_zero_draw_after_others_is_the_minimum)
{
    perf_window_t w;
    perf_window_start(&w, 1);
    perf_window_add(&w, 1, 5000u, 9000u, 0u);
    perf_window_add(&w, 1, 0u, 0u, 0u);
    CHECK_EQ(w.draw.min_us, 0);
    CHECK_EQ(w.wait.min_us, 0);
    CHECK_EQ(w.draw.max_us, 5000);
    CHECK_EQ(w.wait.max_us, 9000);
}

TEST_CASE(the_mean_rounds_to_the_nearest_microsecond)
{
    perf_window_t w;
    perf_window_start(&w, 2);
    perf_window_add(&w, 2, 1u, 2u, 0u);
    perf_window_add(&w, 2, 2u, 2u, 0u);     /* draw 1.5 -> 2, wait 2 */
    CHECK_EQ(perf_span_mean_us(&w.draw, w.frames), 2);
    perf_window_add(&w, 2, 1u, 3u, 0u);     /* draw 4/3 -> 1, wait 7/3 -> 2 */
    CHECK_EQ(perf_span_mean_us(&w.draw, w.frames), 1);
    CHECK_EQ(perf_span_mean_us(&w.wait, w.frames), 2);
}

TEST_CASE(a_frame_on_another_screen_starts_the_window_again)
{
    perf_window_t w;
    perf_window_start(&w, 1);
    for (int i = 0; i < 120; ++i) {
        perf_window_add(&w, 1, 9000u, 7000u, 0u);
    }
    perf_window_add(&w, 4, 2000u, 14000u, 0u);
    CHECK_EQ(w.screen, 4);
    CHECK_EQ(w.frames, 1);
    CHECK_EQ(w.draw.max_us, 2000);
    CHECK_EQ(w.draw.min_us, 2000);
    CHECK_EQ(perf_span_mean_us(&w.wait, w.frames), 14000);
}

TEST_CASE(a_full_window_of_long_frames_sums_past_32_bits)
{
    perf_window_t w;
    perf_window_start(&w, 3);
    for (uint32_t i = 0; i < PERF_WINDOW_FRAMES; ++i) {
        perf_window_add(&w, 3, 20000000u, 0u, 0u);      /* 20s each */
    }
    CHECK(w.draw.sum_us > UINT32_MAX);
    CHECK_EQ(perf_span_mean_us(&w.draw, w.frames), 20000000);
    CHECK_EQ(w.frames, PERF_WINDOW_FRAMES);
}

TEST_CASE(a_new_name_is_reported_and_kept)
{
    perf_lows_t t;
    perf_lows_init(&t);
    CHECK(perf_lows_note(&t, "runlog", 2100u));
    CHECK_EQ(t.count, 1);
    CHECK_STR_EQ(t.rows[0].name, "runlog");
    CHECK_EQ(t.rows[0].least, 2100);
}

TEST_CASE(a_figure_that_holds_or_rises_is_not_reported)
{
    perf_lows_t t;
    perf_lows_init(&t);
    (void)perf_lows_note(&t, "control", 1500u);
    CHECK(!perf_lows_note(&t, "control", 1500u));
    CHECK(!perf_lows_note(&t, "control", 1800u));
    CHECK_EQ(t.rows[0].least, 1500);
}

TEST_CASE(a_figure_that_falls_by_one_byte_is_reported)
{
    perf_lows_t t;
    perf_lows_init(&t);
    (void)perf_lows_note(&t, "control", 1500u);
    CHECK(perf_lows_note(&t, "control", 1499u));
    CHECK_EQ(t.rows[0].least, 1499);
    CHECK(perf_lows_note(&t, "control", 0u));
    CHECK_EQ(t.rows[0].least, 0);
}

TEST_CASE(two_tasks_of_one_name_keep_the_lower_figure)
{
    perf_lows_t t;
    perf_lows_init(&t);
    CHECK(perf_lows_note(&t, "ipc", 900u));
    CHECK(perf_lows_note(&t, "ipc", 700u));
    CHECK(!perf_lows_note(&t, "ipc", 900u));
    CHECK_EQ(t.count, 1);
    CHECK_EQ(t.rows[0].least, 700);
}

TEST_CASE(a_name_is_cut_to_fifteen_bytes)
{
    perf_lows_t t;
    perf_lows_init(&t);
    CHECK(perf_lows_note(&t, "abcdefghijklmno", 10u));      /* 15: whole */
    CHECK_STR_EQ(t.rows[0].name, "abcdefghijklmno");
    CHECK(perf_lows_note(&t, "0123456789abcdefXYZ", 10u));  /* 19: cut */
    CHECK_STR_EQ(t.rows[1].name, "0123456789abcde");
    /* The same name past the cut is the same row. */
    CHECK(!perf_lows_note(&t, "0123456789abcdeQ", 10u));
    CHECK_EQ(t.count, 2);
}

TEST_CASE(no_name_is_noted_as_the_empty_name)
{
    perf_lows_t t;
    perf_lows_init(&t);
    CHECK(perf_lows_note(&t, NULL, 5u));
    CHECK_STR_EQ(t.rows[0].name, "");
    CHECK(!perf_lows_note(&t, "", 5u));
}

TEST_CASE(a_full_table_counts_a_new_name_as_dropped)
{
    perf_lows_t t;
    perf_lows_init(&t);
    char name[PERF_LOWS_NAME];
    for (uint32_t i = 0; i < PERF_LOWS_MAX; ++i) {
        (void)snprintf(name, sizeof(name), "task%u", (unsigned)i);
        CHECK(perf_lows_note(&t, name, 100u));
    }
    CHECK_EQ(t.count, PERF_LOWS_MAX);
    CHECK(!perf_lows_note(&t, "one too many", 1u));
    CHECK_EQ(t.dropped, 1);
    /* A name already in the table still reports a fall. */
    CHECK(perf_lows_note(&t, "task0", 99u));
    CHECK_EQ(t.dropped, 1);
}

TEST_CASE(turn_is_spanned_like_draw_and_wait)
{
    perf_window_t w;
    perf_window_start(&w, 5);
    perf_window_add(&w, 5, 15000u, 18000u, 4800u);
    perf_window_add(&w, 5, 15000u, 18000u, 3800u);
    perf_window_add(&w, 5, 15000u, 18000u, 4301u);
    CHECK_EQ(w.turn.min_us, 3800);
    CHECK_EQ(w.turn.max_us, 4800);
    CHECK_EQ(perf_span_mean_us(&w.turn, w.frames), 4300);
    /* NORMAL: every TURN 0. */
    perf_window_start(&w, 5);
    perf_window_add(&w, 5, 4000u, 12000u, 0u);
    CHECK_EQ(w.turn.min_us, 0);
    CHECK_EQ(w.turn.max_us, 0);
    CHECK_EQ(perf_span_mean_us(&w.turn, w.frames), 0);
}

TEST_CASE(a_mean_over_no_frames_is_zero)
{
    const perf_span_t s = {.min_us = 7u, .max_us = 9u, .sum_us = 16u};
    CHECK_EQ(perf_span_mean_us(&s, 0u), 0);
    CHECK_EQ(perf_span_mean_us(&s, 2u), 8);
}

TEST_CASE(a_noted_row_is_due_once)
{
    perf_lows_t t;
    perf_lows_init(&t);
    CHECK_EQ(perf_lows_take(&t), -1);
    (void)perf_lows_note(&t, "main", 3000u);
    (void)perf_lows_note(&t, "control", 1500u);
    /* A fall before the line is taken leaves one line due, at the new
     * figure. */
    (void)perf_lows_note(&t, "main", 2900u);
    CHECK_EQ(perf_lows_take(&t), 0);
    CHECK_EQ(t.rows[0].least, 2900);
    CHECK_EQ(perf_lows_take(&t), 1);
    CHECK_EQ(perf_lows_take(&t), -1);
    /* A figure that holds is not due again; one that falls is. */
    (void)perf_lows_note(&t, "control", 1500u);
    CHECK_EQ(perf_lows_take(&t), -1);
    (void)perf_lows_note(&t, "control", 1400u);
    CHECK_EQ(perf_lows_take(&t), 1);
    CHECK_EQ(t.due, 0u);
}

TEST_CASE(the_last_row_of_a_full_table_is_due)
{
    perf_lows_t t;
    perf_lows_init(&t);
    char name[PERF_LOWS_NAME];
    for (uint32_t i = 0; i < PERF_LOWS_MAX; ++i) {
        (void)snprintf(name, sizeof(name), "task%u", (unsigned)i);
        (void)perf_lows_note(&t, name, 100u);
    }
    for (uint32_t i = 0; i < PERF_LOWS_MAX; ++i) {
        CHECK_EQ(perf_lows_take(&t), (int)i);
    }
    CHECK_EQ(perf_lows_take(&t), -1);
    (void)snprintf(name, sizeof(name), "task%u", (unsigned)(PERF_LOWS_MAX - 1u));
    (void)perf_lows_note(&t, name, 1u);
    CHECK_EQ(perf_lows_take(&t), (int)(PERF_LOWS_MAX - 1u));
    /* A dropped name is never due. */
    (void)perf_lows_note(&t, "one too many", 1u);
    CHECK_EQ(perf_lows_take(&t), -1);
}

TEST_CASE(the_rate_line_closes_each_window_and_the_window_line_follows)
{
    CHECK_EQ(perf_line_due(0u), PERF_LINE_RATE);
    CHECK_EQ(perf_line_due(1u), PERF_LINE_WINDOW);
    CHECK_EQ(perf_line_due(2u), PERF_LINE_LOW);
    CHECK_EQ(perf_line_due(299u), PERF_LINE_LOW);
    CHECK_EQ(perf_line_due(300u), PERF_LINE_RATE);
    CHECK_EQ(perf_line_due(301u), PERF_LINE_WINDOW);
    CHECK_EQ(perf_line_due(302u), PERF_LINE_LOW);
    CHECK_EQ(perf_line_due(600u), PERF_LINE_RATE);
}

TEST_CASE(the_window_across_the_wrap_is_196_frames)
{
    /* The last rate line before the wrap, then the one at frame 0. */
    const uint32_t last = UINT32_MAX - (UINT32_MAX % PERF_WINDOW_FRAMES);
    CHECK_EQ(perf_line_due(last), PERF_LINE_RATE);
    /* The 195 frames after it, up to UINT32_MAX, carry none; frame 0,
     * the 196th, closes the window. */
    uint32_t rates = 0u;
    for (uint32_t i = 1u; i < 196u; ++i) {
        rates += (perf_line_due(last + i) == PERF_LINE_RATE) ? 1u : 0u;
    }
    CHECK_EQ(rates, 0);
    CHECK_EQ((uint32_t)(last + 196u), 0u);
    CHECK_EQ(perf_line_due(0u), PERF_LINE_RATE);
    CHECK_EQ(perf_line_due(UINT32_MAX), PERF_LINE_LOW);
}

TEST_CASE(a_name_copy_reads_no_byte_past_fifteen_or_the_terminator)
{
    char dst[PERF_LOWS_NAME];
    /* 16 bytes and no terminator: the 16th is never read. */
    const char raw[16] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
                          'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p'};
    perf_name_copy(dst, raw);
    CHECK_STR_EQ(dst, "abcdefghijklmno");
    perf_name_copy(dst, "ipc0");
    CHECK_STR_EQ(dst, "ipc0");
    perf_name_copy(dst, "");
    CHECK_STR_EQ(dst, "");
    perf_name_copy(dst, NULL);
    CHECK_STR_EQ(dst, "");
}

int main(void)
{
    RUN(the_window_is_300_frames_and_a_task_name_is_16_bytes);
    RUN(an_empty_window_reports_zero_everywhere);
    RUN(one_frame_is_its_own_minimum_mean_and_maximum);
    RUN(a_zero_draw_after_others_is_the_minimum);
    RUN(the_mean_rounds_to_the_nearest_microsecond);
    RUN(a_frame_on_another_screen_starts_the_window_again);
    RUN(a_full_window_of_long_frames_sums_past_32_bits);
    RUN(a_new_name_is_reported_and_kept);
    RUN(a_figure_that_holds_or_rises_is_not_reported);
    RUN(a_figure_that_falls_by_one_byte_is_reported);
    RUN(two_tasks_of_one_name_keep_the_lower_figure);
    RUN(a_name_is_cut_to_fifteen_bytes);
    RUN(no_name_is_noted_as_the_empty_name);
    RUN(a_full_table_counts_a_new_name_as_dropped);
    RUN(turn_is_spanned_like_draw_and_wait);
    RUN(a_mean_over_no_frames_is_zero);
    RUN(a_noted_row_is_due_once);
    RUN(the_last_row_of_a_full_table_is_due);
    RUN(the_rate_line_closes_each_window_and_the_window_line_follows);
    RUN(the_window_across_the_wrap_is_196_frames);
    RUN(a_name_copy_reads_no_byte_past_fifteen_or_the_terminator);
    return test_summary("perf_watch");
}
