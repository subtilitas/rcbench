/**
 * @file test_servo_meter.c
 * @brief What the servo screen repaints when the meter changes or a window comes:
 * the row, the line, the plot, the link loss and the point of each window.
 *
 * Cases:
 * - a_change_of_meter_repaints_the_row_the_line_and_the_plot
 * - a_window_is_shown_under_the_meter_of_its_poll
 * - link_loss_clears_the_current_row
 * - the_current_row_repaints_on_a_changed_digit_and_alone
 * - the_current_reading_paints_on_live_only
 * - the_plot_keeps_one_point_for_each_window
 * - a_run_reads_the_supply_while_the_meter_changes
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

/* A pixel no part of the screen draws: one that is still there after a
 * render was not painted over. */
#define MARK_COLOUR ((gfx_color_t)0xF81F)

static void mark(int x, int y) { fb[(size_t)y * W + x] = MARK_COLOUR; }

static bool marked(int x, int y)
{
    return fb[(size_t)y * W + x] == MARK_COLOUR;
}

/* Marks in LIVE's CURRENT reading, the line, the plot, and around them on
 * both cards: the MEASURED reading, the tag, the SET line, SPEED's row and
 * the left card away from the grip. */
enum { MK_ROW = 0, MK_LINE, MK_PLOT, MK_MEASURED, MK_TAG, MK_SET, MK_SPEED,
       MK_LEFT, MK_COUNT };

static const struct { int x, y; } k_mark[MK_COUNT] = {
    { 516, 496 }, { 950, 80 }, { 950, 130 }, { 318, 496 }, { 700, 40 },
    { 700, 198 }, { 700, 240 }, { 40, 300 },
};

static void mark_all(void)
{
    for (int k = 0; k < MK_COUNT; ++k) {
        mark(k_mark[k].x, k_mark[k].y);
    }
}

static bool mk(int k) { return marked(k_mark[k].x, k_mark[k].y); }

/* Every mark outside the row, the line and the plot is still there. */
static bool card_kept(void)
{
    return mk(MK_MEASURED) && mk(MK_TAG) && mk(MK_SET) && mk(MK_SPEED)
           && mk(MK_LEFT);
}

/* Every change of meter, with the screen open: the row, the line, its name
 * and the plot are the new meter's in both buffers, as a full redraw draws
 * them.  The trace starts again when the INA3221 becomes or stops being the
 * meter; between the PD mini and the model it is the supply's and stays. */
TEST_CASE(a_change_of_meter_repaints_the_row_the_line_and_the_plot)
{
    static const servo_source_id_t k_src[3] = {
        SERVO_SOURCE_PDMINI, SERVO_SOURCE_INA3221, SERVO_SOURCE_MODEL,
    };
    for (int from = 0; from < 3; ++from) {
        for (int to = 0; to < 3; ++to) {
            if (from == to) {
                continue;
            }
            fresh();
            two_buffers();
            meter(k_src[from]);
            const bool from_ina = (k_src[from] == SERVO_SOURCE_INA3221);
            const bool to_ina   = (k_src[to] == SERVO_SOURCE_INA3221);
            /* Three readings of each kind: three points of the meter's. */
            for (int n = 0; n < 3; ++n) {
                rail_supply(6.0f, 0.4f);
                rail_window(1200, 5000u);
            }
            CHECK_EQ(servo_screen_power_points(), 3);
            CHECK(both_whole());
            if (from_ina) {
                CHECK(row_reads("1.20 A", UI_C_TEXT));
                CHECK(line_ina("5.00 V", "1.20 A", "6.0 W"));
            } else {
                CHECK(row_reads("0.40 A", UI_C_TEXT));
                CHECK(line_supply("6.00 V", "0.40 A", "2.4 W"));
            }

            meter(k_src[to]);
            CHECK(both_whole());
            /* At once, from the reading the new meter last gave. */
            if (to_ina) {
                CHECK(row_reads("1.20 A", UI_C_TEXT));
                CHECK(line_ina("5.00 V", "1.20 A", "6.0 W"));
            } else {
                CHECK(row_reads("0.40 A", UI_C_TEXT));
                CHECK(line_supply("6.00 V", "0.40 A", "2.4 W"));
            }
            CHECK_EQ(servo_screen_power_points(),
                     (from_ina != to_ina) ? 0 : 3);

            /* And from then on the new meter's readings only. */
            rail_supply(6.0f, 0.5f);
            rail_window(1300, 5000u);
            CHECK_EQ(servo_screen_power_points(),
                     (from_ina != to_ina) ? 1 : 4);
            CHECK(both_whole());
            if (to_ina) {
                CHECK(row_reads("1.30 A", UI_C_TEXT));
                CHECK_NEAR(servo_screen_power_sample(1, 0), 1.3f, 1e-6f);
            } else {
                CHECK(row_reads("0.50 A", UI_C_TEXT));
                CHECK_NEAR(servo_screen_power_sample(1, 0), 0.5f, 1e-6f);
            }
            /* The same meter said again changes nothing. */
            meter(k_src[to]);
            CHECK_EQ(servo_screen_power_points(),
                     (from_ina != to_ina) ? 1 : 4);
        }
    }
    /* With the settings open over the left card the output card's line
     * follows the meter all the same, and LIVE's CURRENT reading shows the
     * meter in force once LIVE is back. */
    fresh();
    two_buffers();
    open_settings();
    rail_supply(6.0f, 0.4f);
    CHECK(both_whole());
    meter(SERVO_SOURCE_INA3221);
    rail_window(1200, 5000u);
    CHECK(both_whole());
    CHECK(line_ina("5.00 V", "1.20 A", "6.0 W"));
    meter(SERVO_SOURCE_PDMINI);
    CHECK(both_whole());
    CHECK(line_supply("6.00 V", "0.40 A", "2.4 W"));
    close_settings();
    CHECK(row_reads("0.40 A", UI_C_TEXT));
}

/*
 * The meter comes from the snapshot and the windows from a queue, so either
 * can be the older.  A window taken under a meter that has been replaced is
 * dropped; one taken under a later meter brings that meter with it; an
 * answer older than the meter in force changes nothing.
 */
TEST_CASE(a_window_is_shown_under_the_meter_of_its_poll)
{
    const sense_link_win_t w1 = rail_win(1100, 5000u);
    const sense_link_win_t w2 = rail_win(1200, 5000u);
    const sense_link_win_t w3 = rail_win(1300, 5000u);

    /* The snapshot ahead of the queue, PD mini to INA3221: the windows
     * from before the change are not the INA3221's points. */
    fresh();
    servo_screen_source(SERVO_SOURCE_PDMINI, 1u);
    rail_supply(6.0f, 0.4f);
    servo_screen_source(SERVO_SOURCE_INA3221, 2u);
    servo_screen_window(&w1, SERVO_SOURCE_PDMINI, 1u);
    CHECK_EQ(servo_screen_power_points(), 0);
    CHECK(row_reads("---", UI_C_TEXT));
    servo_screen_window(&w2, SERVO_SOURCE_INA3221, 2u);
    CHECK_EQ(servo_screen_power_points(), 1);
    CHECK(row_reads("1.20 A", UI_C_TEXT));

    /* The queue ahead of the snapshot: the window brings its meter, and
     * the snapshot that catches up changes nothing. */
    fresh();
    servo_screen_source(SERVO_SOURCE_PDMINI, 1u);
    rail_supply(6.0f, 0.4f);
    servo_screen_window(&w1, SERVO_SOURCE_PDMINI, 1u);
    CHECK_EQ(servo_screen_power_points(), 1);       /* the supply's */
    servo_screen_window(&w2, SERVO_SOURCE_INA3221, 2u);
    servo_screen_window(&w3, SERVO_SOURCE_INA3221, 2u);
    CHECK_EQ(servo_screen_power_points(), 2);
    CHECK_NEAR(servo_screen_power_sample(1, 1), 1.2f, 1e-6f);
    CHECK_NEAR(servo_screen_power_sample(1, 0), 1.3f, 1e-6f);
    CHECK(line_ina("5.00 V", "1.30 A", "6.5 W"));
    /* The next frame's snapshot, read before that window's poll. */
    servo_screen_source(SERVO_SOURCE_PDMINI, 1u);
    CHECK_EQ(servo_screen_power_points(), 2);
    CHECK(row_reads("1.30 A", UI_C_TEXT));
    servo_screen_source(SERVO_SOURCE_INA3221, 2u);
    CHECK_EQ(servo_screen_power_points(), 2);

    /* INA3221 to PD mini with windows still queued: they are dropped, the
     * meter stays the PD mini and the plot the supply's. */
    servo_screen_source(SERVO_SOURCE_PDMINI, 3u);
    CHECK_EQ(servo_screen_power_points(), 0);
    servo_screen_window(&w1, SERVO_SOURCE_INA3221, 2u);
    CHECK_EQ(servo_screen_power_points(), 0);
    CHECK(row_reads("0.40 A", UI_C_TEXT));
    CHECK(line_supply("6.00 V", "0.40 A", "2.4 W"));
    /* Back at count 4: the window dropped at count 2 is not the reading. */
    servo_screen_source(SERVO_SOURCE_INA3221, 4u);
    CHECK(row_reads("1.30 A", UI_C_TEXT));          /* w3, taken at 2 */
    servo_screen_window(&w2, SERVO_SOURCE_INA3221, 4u);
    CHECK(row_reads("1.20 A", UI_C_TEXT));
    CHECK_EQ(servo_screen_power_points(), 1);

    /* The count across 2^32: 0 is after 0xFFFFFFFF, and 0xFFFFFFFF
     * before 0; 2^31 - 1 ahead is the last count that is later. */
    static const uint32_t k_at[] = { 0xFFFFFFFFu, 0x7FFFFFFFu, 0u, 5u };
    for (size_t i = 0u; i < sizeof(k_at) / sizeof(k_at[0]); ++i) {
        const uint32_t at = k_at[i];
        fresh();
        servo_screen_source(SERVO_SOURCE_PDMINI, at);
        rail_supply(6.0f, 0.4f);
        servo_screen_source(SERVO_SOURCE_INA3221, at + 1u);
        servo_screen_window(&w1, SERVO_SOURCE_INA3221, at + 1u);
        CHECK(row_reads("1.10 A", UI_C_TEXT));
        servo_screen_source(SERVO_SOURCE_PDMINI, at);          /* older */
        CHECK(row_reads("1.10 A", UI_C_TEXT));
        servo_screen_window(&w2, SERVO_SOURCE_PDMINI, at);     /* older */
        CHECK(row_reads("1.10 A", UI_C_TEXT));
        CHECK_EQ(servo_screen_power_points(), 1);
        /* 2^31 - 1 ahead of at + 1 is the last count that is later; so far
         * ahead, the window from before it is forgotten. */
        servo_screen_source(SERVO_SOURCE_PDMINI, at + 0x80000000u);
        CHECK(row_reads("0.40 A", UI_C_TEXT));
        servo_screen_source(SERVO_SOURCE_INA3221, at + 0x80000001u);
        CHECK(row_reads("---", UI_C_TEXT));
        servo_screen_window(&w1, SERVO_SOURCE_INA3221, at + 0x80000001u);
        CHECK(row_reads("1.10 A", UI_C_TEXT));
        /* 2^31 ahead reads as behind. */
        servo_screen_source(SERVO_SOURCE_PDMINI, at + 1u);
        CHECK(row_reads("1.10 A", UI_C_TEXT));
    }

    /* Changes that went by unseen: a count two or more ahead empties the
     * plot whatever the meter is, since another may have been the rail's
     * in between, and the INA3221's window from before them is not shown. */
    static const uint32_t k_from[] = { 2u, 0xFFFFFFFEu, 0xFFFFFFFFu };
    for (size_t i = 0u; i < sizeof(k_from) / sizeof(k_from[0]); ++i) {
        const uint32_t n = k_from[i];
        fresh();
        servo_screen_source(SERVO_SOURCE_INA3221, n);
        servo_screen_window(&w1, SERVO_SOURCE_INA3221, n);
        servo_screen_window(&w2, SERVO_SOURCE_INA3221, n);
        CHECK_EQ(servo_screen_power_points(), 2);
        servo_screen_source(SERVO_SOURCE_INA3221, n);       /* the same */
        CHECK_EQ(servo_screen_power_points(), 2);
        /* INA3221, PD mini, INA3221 between two snapshots. */
        servo_screen_source(SERVO_SOURCE_INA3221, n + 2u);
        CHECK_EQ(servo_screen_power_points(), 0);
        CHECK(row_reads("---", UI_C_TEXT));
        CHECK(line_ina("-- V", "-- A", "-- W"));
        /* The PD mini's interval, arriving behind it, shows nothing. */
        servo_screen_window(&w3, SERVO_SOURCE_PDMINI, n + 1u);
        CHECK(row_reads("---", UI_C_TEXT));
        servo_screen_window(&w3, SERVO_SOURCE_INA3221, n + 2u);
        CHECK_EQ(servo_screen_power_points(), 1);
        CHECK(row_reads("1.30 A", UI_C_TEXT));
        /* A window that is itself two ahead: the same, then its point. */
        servo_screen_window(&w1, SERVO_SOURCE_INA3221, n + 4u);
        CHECK_EQ(servo_screen_power_points(), 1);
        CHECK(row_reads("1.10 A", UI_C_TEXT));

        /* The supply's trace likewise: PD mini, INA3221, PD mini; and PD
         * mini, INA3221, model, where the name does not change either. */
        for (int to = 0; to < 2; ++to) {
            fresh();
            servo_screen_source(SERVO_SOURCE_PDMINI, n);
            rail_supply(6.0f, 0.4f);
            rail_supply(6.0f, 0.4f);
            CHECK_EQ(servo_screen_power_points(), 2);
            servo_screen_source(to == 0 ? SERVO_SOURCE_PDMINI
                                        : SERVO_SOURCE_MODEL, n + 2u);
            CHECK_EQ(servo_screen_power_points(), 0);
            CHECK(row_reads("0.40 A", UI_C_TEXT));   /* its last reading */
            rail_supply(6.0f, 0.5f);
            CHECK_EQ(servo_screen_power_points(), 1);
        }
        /* One ahead between the PD mini and the model: the supply's trace
         * goes on. */
        fresh();
        servo_screen_source(SERVO_SOURCE_PDMINI, n);
        rail_supply(6.0f, 0.4f);
        servo_screen_source(SERVO_SOURCE_MODEL, n + 1u);
        rail_supply(6.0f, 0.4f);
        CHECK_EQ(servo_screen_power_points(), 2);
    }

    /* The supply's samples wait on a queue as well.  One taken under a
     * meter replaced since is the supply's last reading and no point; one
     * ahead of the snapshot brings its meter. */
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.v = 6.0f;
    fresh();
    servo_screen_source(SERVO_SOURCE_INA3221, 2u);
    servo_screen_window(&w1, SERVO_SOURCE_INA3221, 2u);
    servo_screen_source(SERVO_SOURCE_PDMINI, 3u);       /* the snapshot */
    CHECK_EQ(servo_screen_power_points(), 0);
    st.i = 0.5f; st.p = 3.0f;
    servo_screen_supply_at(&st, SERVO_SOURCE_INA3221, 2u);
    CHECK_EQ(servo_screen_power_points(), 0);
    CHECK(row_reads("0.50 A", UI_C_TEXT));
    CHECK(line_supply("6.00 V", "0.50 A", "3.0 W"));
    st.i = 0.6f; st.p = 3.6f;
    servo_screen_supply_at(&st, SERVO_SOURCE_PDMINI, 3u);
    CHECK_EQ(servo_screen_power_points(), 1);
    CHECK_NEAR(servo_screen_power_sample(1, 0), 0.6f, 1e-6f);
    CHECK(row_reads("0.60 A", UI_C_TEXT));
    /* Ahead of the snapshot, to the INA3221 and back. */
    st.i = 0.7f; st.p = 4.2f;
    servo_screen_supply_at(&st, SERVO_SOURCE_INA3221, 4u);
    CHECK_EQ(servo_screen_power_points(), 0);
    CHECK(line_ina("5.00 V", "1.10 A", "5.5 W"));       /* w1, kept */
    servo_screen_source(SERVO_SOURCE_PDMINI, 3u);        /* older */
    CHECK(line_ina("5.00 V", "1.10 A", "5.5 W"));
    st.i = 0.8f; st.p = 4.8f;
    servo_screen_supply_at(&st, SERVO_SOURCE_PDMINI, 5u);
    CHECK_EQ(servo_screen_power_points(), 1);
    CHECK(row_reads("0.80 A", UI_C_TEXT));
    /* A window of count 4 behind it is dropped; the supply's trace stays. */
    servo_screen_window(&w2, SERVO_SOURCE_INA3221, 4u);
    CHECK_EQ(servo_screen_power_points(), 1);
    CHECK(row_reads("0.80 A", UI_C_TEXT));
    /* A sample with no tag is one of the meter in force. */
    st.i = 0.9f; st.p = 5.4f;
    servo_screen_supply(&st);
    CHECK_EQ(servo_screen_power_points(), 2);
    servo_screen_supply_at(NULL, SERVO_SOURCE_INA3221, 9u);
    CHECK_EQ(servo_screen_power_points(), 2);
    CHECK(row_reads("0.90 A", UI_C_TEXT));
    /* Across 2^32. */
    fresh();
    servo_screen_source(SERVO_SOURCE_PDMINI, 0u);
    servo_screen_supply_at(&st, SERVO_SOURCE_INA3221, 0xFFFFFFFFu);
    CHECK_EQ(servo_screen_power_points(), 0);            /* behind 0 */
    CHECK(line_supply("6.00 V", "0.90 A", "5.4 W"));
    servo_screen_supply_at(&st, SERVO_SOURCE_PDMINI, 0u);
    CHECK_EQ(servo_screen_power_points(), 1);

    /* A window while the link is down is dropped with its meter. */
    fresh();
    servo_screen_source(SERVO_SOURCE_PDMINI, 1u);
    servo_screen_set_link(false);
    servo_screen_window(&w1, SERVO_SOURCE_INA3221, 2u);
    servo_screen_set_link(true);
    CHECK(line_supply("-- V", "-- A", "-- W"));
    servo_screen_window(NULL, SERVO_SOURCE_INA3221, 2u);
    CHECK(line_supply("-- V", "-- A", "-- W"));
}

/* A link that goes takes the INA3221's last window with it: the dashes,
 * until a window arrives over a link that is up. */
TEST_CASE(link_loss_clears_the_current_row)
{
    fresh();
    two_buffers();
    meter(SERVO_SOURCE_INA3221);
    rail_window(1200, 5000u);
    CHECK(row_reads("1.20 A", UI_C_TEXT));
    servo_screen_set_link(false);
    CHECK(both_whole());
    CHECK(row_reads("---", UI_C_TEXT));
    CHECK(line_ina("-- V", "-- A", "-- W"));
    /* A window that was on its way is dropped: no reading, no point. */
    const int points = servo_screen_power_points();
    rail_window(1300, 5000u);
    CHECK(row_reads("---", UI_C_TEXT));
    CHECK_EQ(servo_screen_power_points(), points);
    /* Said again, nothing changes. */
    servo_screen_set_link(false);
    CHECK(row_reads("---", UI_C_TEXT));
    /* The link back: still no reading until one arrives. */
    servo_screen_set_link(true);
    CHECK(row_reads("---", UI_C_TEXT));
    CHECK(line_ina("-- V", "-- A", "-- W"));
    rail_window(1300, 5000u);
    CHECK(row_reads("1.30 A", UI_C_TEXT));
    CHECK(both_whole());

    /* The meter drops to the model with the link, in either order: the
     * row is the supply's then, and the window is gone when the INA3221
     * is the meter again. */
    for (int order = 0; order < 2; ++order) {
        fresh();
        meter(SERVO_SOURCE_INA3221);
        rail_supply(6.0f, 0.4f);
        rail_window(1200, 5000u);
        if (order == 0) {
            servo_screen_set_link(false);
            meter(SERVO_SOURCE_MODEL);
        } else {
            meter(SERVO_SOURCE_MODEL);
            servo_screen_set_link(false);
        }
        CHECK(row_reads("0.40 A", UI_C_TEXT));
        CHECK(line_supply("6.00 V", "0.40 A", "2.4 W"));
        servo_screen_set_link(true);
        meter(SERVO_SOURCE_INA3221);
        CHECK(row_reads("---", UI_C_TEXT));
    }

    /* The supply's reading goes when its sample says it does not answer,
     * which is what the PD mini's samples say once the link is down. */
    fresh();
    meter(SERVO_SOURCE_PDMINI);
    rail_supply(6.0f, 0.4f);
    servo_screen_set_link(false);
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    servo_screen_supply(&st);
    CHECK(row_reads("---", UI_C_TEXT));
    CHECK(line_supply("-- V", "-- A", "-- W"));
}

/* The row is repainted when the digits it shows change, and then the row
 * alone: not the card around it.  The same digits paint nothing there. */
TEST_CASE(the_current_row_repaints_on_a_changed_digit_and_alone)
{
    fresh_armed();
    meter(SERVO_SOURCE_INA3221);
    rail_window(412, 5980u);
    scr->render(&cv, 0);

    /* 0.412A to 0.414A: the plot takes its point, the row is left. */
    mark_all();
    rail_window(414, 5980u);
    scr->render(&cv, 0);
    CHECK(mk(MK_ROW));
    CHECK(!mk(MK_LINE));
    CHECK(!mk(MK_PLOT));
    CHECK(card_kept());

    /* 0.414A to 0.415A is 0.41 to 0.42: the row, and nothing around it
     * but the line and the plot the window also feeds. */
    mark_all();
    rail_window(415, 5980u);
    scr->render(&cv, 0);
    CHECK(!mk(MK_ROW));
    CHECK(card_kept());
    CHECK(row_reads("0.42 A", UI_C_TEXT));

    /* No window in a frame: nothing is painted at all. */
    mark_all();
    scr->render(&cv, 0);
    CHECK(mk(MK_ROW));
    CHECK(mk(MK_LINE));
    CHECK(mk(MK_PLOT));
    CHECK(card_kept());

    /* The other buffer owes the row once and takes it once. */
    settle_drawn();                 /* ARM's flash after the arm is over */
    rail_window(500, 5980u);
    CHECK(both_whole_in(SV_RCARD_X, 6, SV_RCARD_W, 400));
    CHECK(row_reads("0.50 A", UI_C_TEXT));
    scr->render(&cv1, 1);
    mark_all();
    scr->render(&cv, 0);
    CHECK(mk(MK_ROW));
    CHECK(mk(MK_PLOT));
    CHECK(card_kept());

    /* The supply as the meter: the same rule. */
    fresh_armed();
    meter(SERVO_SOURCE_PDMINI);
    rail_supply(6.0f, 0.402f);
    scr->render(&cv, 0);
    mark_all();
    rail_supply(6.0f, 0.404f);
    scr->render(&cv, 0);
    CHECK(mk(MK_ROW));
    CHECK(!mk(MK_PLOT));
    CHECK(card_kept());
    mark_all();
    rail_supply(6.0f, 0.43f);
    scr->render(&cv, 0);
    CHECK(!mk(MK_ROW));
    CHECK(card_kept());

    /* A feedback whose current alone moved paints nothing: neither the
     * row nor the card. */
    servo_screen_feedback(1500u, 0.2f, true);
    scr->render(&cv, 0);
    mark_all();
    servo_screen_feedback(1500u, 0.9f, true);
    scr->render(&cv, 0);
    CHECK(mk(MK_ROW));
    CHECK(mk(MK_PLOT));
    CHECK(card_kept());
}

/* LIVE's CURRENT reading is LIVE's: a changed current paints nothing at its
 * place on PROGRAM's page or TEST's, and LIVE shows it once it is back. */
TEST_CASE(the_current_reading_paints_on_live_only)
{
    for (int task = 0; task < 2; ++task) {
        fresh();
        meter(SERVO_SOURCE_PDMINI);
        rail_supply(6.0f, 0.402f);
        if (task == 0) {
            open_program();
        } else {
            open_test();
        }
        scr->render(&cv, 0);
        mark(k_mark[MK_ROW].x, k_mark[MK_ROW].y);
        rail_supply(6.0f, 0.73f);
        scr->render(&cv, 0);
        CHECK(mk(MK_ROW));
        close_settings();
        CHECK(row_reads("0.73 A", UI_C_TEXT));
    }
}

/* The plot keeps one point for each window, however many a frame hands
 * over, in their order; a frame with none adds none and repaints nothing. */
TEST_CASE(the_plot_keeps_one_point_for_each_window)
{
    for (int n = 0; n <= 4; ++n) {
        fresh();
        meter(SERVO_SOURCE_INA3221);
        rail_window(50, 4000u);
        scr->render(&cv, 0);
        CHECK_EQ(servo_screen_power_points(), 1);
        mark_all();
        for (int k = 0; k < n; ++k) {
            rail_window(100 * (k + 1), 5000u + (unsigned)k);
        }
        scr->render(&cv, 0);
        CHECK_EQ(servo_screen_power_points(), 1 + n);
        for (int k = 0; k < n; ++k) {
            const int back = n - 1 - k;
            const float a = 0.1f * (float)(k + 1);
            const float v = (float)(5000 + k) / 1000.0f;
            CHECK_NEAR(servo_screen_power_sample(0, back), v, 1e-5f);
            CHECK_NEAR(servo_screen_power_sample(1, back), a, 1e-6f);
            CHECK_NEAR(servo_screen_power_sample(2, back), v * a, 1e-5f);
        }
        CHECK_NEAR(servo_screen_power_sample(1, n), 0.05f, 1e-6f);
        CHECK_EQ(mk(MK_PLOT), n == 0);
        CHECK_EQ(mk(MK_LINE), n == 0);
        CHECK_EQ(mk(MK_ROW), n == 0);
        CHECK(card_kept());
        if (n > 0) {
            char want[16];
            snprintf(want, sizeof(want), "0.%d0 A", n);
            CHECK(row_reads(want, UI_C_TEXT));
        }
    }
    /* The supply's samples are the points while it is the meter, one each,
     * and the windows none. */
    fresh();
    meter(SERVO_SOURCE_PDMINI);
    for (int k = 0; k < 4; ++k) {
        rail_supply(6.0f, 0.1f * (float)(k + 1));
        rail_window(900, 5000u);
        rail_window(900, 5000u);
    }
    CHECK_EQ(servo_screen_power_points(), 4);
    CHECK_NEAR(servo_screen_power_sample(1, 0), 0.4f, 1e-6f);
    CHECK_NEAR(servo_screen_power_sample(1, 3), 0.1f, 1e-6f);
}

/* One run of the automatic test with @p changing: the meter changed every
 * 300ms and two windows handed over every 100ms while it runs.  With
 * @p behind every supply sample of the run was taken under a meter
 * replaced since. */
static void a_short_run(bool changing, bool behind)
{
    static const servo_source_id_t k_src[4] = {
        SERVO_SOURCE_INA3221, SERVO_SOURCE_PDMINI, SERVO_SOURCE_INA3221,
        SERVO_SOURCE_MODEL,
    };
    bench_fresh();
    short_runs();
    supply_screen_put(5.5f, 1.5f);
    if (behind) {
        meter(SERVO_SOURCE_INA3221);
        meter(SERVO_SOURCE_PDMINI);
        bench.behind = true;
    }
    hold_start(2.3f);
    for (int i = 0; i < 600 && servo_screen_testing(); ++i) {
        if (changing) {
            meter(k_src[(i / 3) % 4]);
            rail_window(2000 + i, 9000u);
            rail_window(-2000 - i, 9000u);
        }
        bench_frames(100u);
        if (changing && (i % 7) == 0) {
            scr->render(&cv, 0);
        }
    }
    bench_frames(1000u);
}

/* A run reads the PD mini whatever the rail's meter is and however it
 * changes: its report and its files are those of a run under one meter,
 * and the screen draws the meter's reading as a full redraw would. */
TEST_CASE(a_run_reads_the_supply_while_the_meter_changes)
{
    a_short_run(false, false);
    CHECK(!servo_screen_testing());
    CHECK(strstr(bench.report, "Result:         PASS") != NULL);
    static char report[sizeof(bench.report)];
    memcpy(report, bench.report, sizeof(report));
    const unsigned csv = bench.csv, txt = bench.txt;
    CHECK(csv > 50u);

    a_short_run(true, false);
    CHECK(!servo_screen_testing());
    CHECK_EQ(bench.opens, 1u);
    CHECK_EQ(bench.ends, 1u);
    CHECK_EQ(bench.csv, csv);
    CHECK_EQ(bench.txt, txt);
    CHECK_STR_EQ(bench.report, report);
    CHECK(strstr(bench.report, "Supply:         PD mini") != NULL);
    CHECK(!bench.out);

    /* Samples taken under a meter replaced since are no points of the
     * plot, and the run reads every one of them. */
    a_short_run(false, true);
    CHECK(!servo_screen_testing());
    CHECK_EQ(bench.csv, csv);
    CHECK_EQ(bench.txt, txt);
    CHECK_STR_EQ(bench.report, report);
    CHECK_EQ(servo_screen_power_points(), 0);

    /* Mid-run, on each meter: the output card as a full redraw draws it,
     * and LIVE's CURRENT reading the meter's. */
    bench_fresh();
    short_runs();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    close_settings();                   /* LIVE: its CURRENT reading */
    bench_frames(2000u);
    settle_drawn();
    meter(SERVO_SOURCE_INA3221);
    rail_window(-1640, 5000u);
    bench_frames(100u);
    CHECK(servo_screen_testing());
    CHECK(both_whole_in(SV_RCARD_X, 6, SV_RCARD_W, 400));
    CHECK(row_reads("-1.64 A", UI_C_TEXT));
    meter(SERVO_SOURCE_PDMINI);
    bench_frames(200u);
    CHECK(servo_screen_testing());
    CHECK(both_whole_in(SV_RCARD_X, 6, SV_RCARD_W, 400));
    CHECK(!row_reads("-1.64 A", UI_C_TEXT));
    meter(SERVO_SOURCE_MODEL);
    bench_frames(200u);
    CHECK(servo_screen_testing());
    CHECK(both_whole_in(SV_RCARD_X, 6, SV_RCARD_W, 400));
    /* The link going ends the run and clears the INA3221's row. */
    meter(SERVO_SOURCE_INA3221);
    rail_window(700, 5000u);
    servo_screen_set_link(false);
    CHECK(!servo_screen_testing());
    CHECK(row_reads("---", UI_C_TEXT));
}

int main(void)
{
    RUN(a_change_of_meter_repaints_the_row_the_line_and_the_plot);
    RUN(a_window_is_shown_under_the_meter_of_its_poll);
    RUN(link_loss_clears_the_current_row);
    RUN(the_current_row_repaints_on_a_changed_digit_and_alone);
    RUN(the_current_reading_paints_on_live_only);
    RUN(the_plot_keeps_one_point_for_each_window);
    RUN(a_run_reads_the_supply_while_the_meter_changes);
    return test_summary("servo_meter");
}
