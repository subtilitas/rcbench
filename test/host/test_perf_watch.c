/*
 * The statistics behind the panel's console lines: the frame window and the
 * table of lows (perf_watch.h).
 *
 * At the limits: an empty window, one frame, a screen change in the middle,
 * a mean that rounds, sums past 2^32 us; a name at the cut length and one
 * past it, a figure that holds, rises and falls, a full table.
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
    CHECK_EQ(w.draw_min_us, 0);
    CHECK_EQ(w.draw_max_us, 0);
    CHECK_EQ(perf_window_draw_mean_us(&w), 0);
    CHECK_EQ(perf_window_wait_mean_us(&w), 0);
}

TEST_CASE(one_frame_is_its_own_minimum_mean_and_maximum)
{
    perf_window_t w;
    perf_window_start(&w, 1);
    perf_window_add(&w, 1, 3800u, 12000u);
    CHECK_EQ(w.frames, 1);
    CHECK_EQ(w.draw_min_us, 3800);
    CHECK_EQ(w.draw_max_us, 3800);
    CHECK_EQ(w.wait_min_us, 12000);
    CHECK_EQ(w.wait_max_us, 12000);
    CHECK_EQ(perf_window_draw_mean_us(&w), 3800);
}

TEST_CASE(a_zero_draw_after_others_is_the_minimum)
{
    perf_window_t w;
    perf_window_start(&w, 1);
    perf_window_add(&w, 1, 5000u, 9000u);
    perf_window_add(&w, 1, 0u, 0u);
    CHECK_EQ(w.draw_min_us, 0);
    CHECK_EQ(w.wait_min_us, 0);
    CHECK_EQ(w.draw_max_us, 5000);
    CHECK_EQ(w.wait_max_us, 9000);
}

TEST_CASE(the_mean_rounds_to_the_nearest_microsecond)
{
    perf_window_t w;
    perf_window_start(&w, 2);
    perf_window_add(&w, 2, 1u, 2u);
    perf_window_add(&w, 2, 2u, 2u);     /* draw 1.5 -> 2, wait 2 */
    CHECK_EQ(perf_window_draw_mean_us(&w), 2);
    perf_window_add(&w, 2, 1u, 3u);     /* draw 4/3 -> 1, wait 7/3 -> 2 */
    CHECK_EQ(perf_window_draw_mean_us(&w), 1);
    CHECK_EQ(perf_window_wait_mean_us(&w), 2);
}

TEST_CASE(a_frame_on_another_screen_starts_the_window_again)
{
    perf_window_t w;
    perf_window_start(&w, 1);
    for (int i = 0; i < 120; ++i) {
        perf_window_add(&w, 1, 9000u, 7000u);
    }
    perf_window_add(&w, 4, 2000u, 14000u);
    CHECK_EQ(w.screen, 4);
    CHECK_EQ(w.frames, 1);
    CHECK_EQ(w.draw_max_us, 2000);
    CHECK_EQ(w.draw_min_us, 2000);
    CHECK_EQ(perf_window_wait_mean_us(&w), 14000);
}

TEST_CASE(a_full_window_of_long_frames_sums_past_32_bits)
{
    perf_window_t w;
    perf_window_start(&w, 3);
    for (uint32_t i = 0; i < PERF_WINDOW_FRAMES; ++i) {
        perf_window_add(&w, 3, 20000000u, 0u);      /* 20 s each */
    }
    CHECK(w.draw_sum_us > UINT32_MAX);
    CHECK_EQ(perf_window_draw_mean_us(&w), 20000000);
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
    return test_summary("perf_watch");
}
