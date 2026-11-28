/**
 * @file test_dk518_enc.c
 * @brief The Guition JC3636K518C's encoder decode (dk518_enc.h) over
 *        recorded pair sequences: a clockwise and an anticlockwise
 *        detent of 4 edges, a bounce, a half turn that goes back, an
 *        encoder of 2 edges a detent, a fast turn, a missed edge, the
 *        home rest, and the trace the firmware prints.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "dk518_enc.h"

/** @brief The clock, ms, near its wrap. */
static uint32_t s_now;

/**
 * @brief Start a decode at rest on @p home: the first pair, then quiet.
 * @param e    the decode
 * @param home the pair the lines rest at
 */
static void rest_at(dk518_enc_t *e, uint8_t home)
{
    s_now = 0xFFFFFF00u;
    dk518_enc_init(e);
    CHECK_EQ(0, dk518_enc_edge(e, home, s_now));
    s_now += DK518_REST_MS;
    CHECK_EQ(0, dk518_enc_poll(e, s_now));
}

/**
 * @brief Feed pairs 2ms apart, polling every edge.
 * @param e     the decode
 * @param pairs the pairs as a string of '0' to '3'
 * @return the detents they counted
 */
static int32_t feed(dk518_enc_t *e, const char *pairs)
{
    int32_t d = 0;
    for (const char *p = pairs; *p != '\0'; ++p) {
        s_now += 2u;
        d += dk518_enc_edge(e, (uint8_t)(*p - '0'), s_now);
        d += dk518_enc_poll(e, s_now);
    }
    return d;
}

/**
 * @brief Let the lines stay quiet past DK518_REST_MS, polling every 5ms.
 * @param e the decode
 * @return the detents the quiet counted
 */
static int32_t settle(dk518_enc_t *e)
{
    int32_t d = 0;
    for (unsigned t = 0; t <= DK518_REST_MS; t += 5u) {
        s_now += 5u;
        d += dk518_enc_poll(e, s_now);
    }
    return d;
}

TEST_CASE(a_clockwise_detent_of_four_edges_counts_one)
{
    dk518_enc_t e;
    rest_at(&e, 3u);
    /* A falls first: 11 > 01 > 00 > 10 > 11. */
    CHECK_EQ(0, feed(&e, "100"));
    CHECK_EQ(1, feed(&e, "23"));
    CHECK_EQ(0, settle(&e));
    CHECK_EQ(4, feed(&e, "1023102310231023"));
}

TEST_CASE(an_anticlockwise_detent_of_four_edges_counts_minus_one)
{
    dk518_enc_t e;
    rest_at(&e, 3u);
    /* B falls first: 11 > 10 > 00 > 01 > 11. */
    CHECK_EQ(-1, feed(&e, "2013"));
    CHECK_EQ(0, settle(&e));
    CHECK_EQ(-3, feed(&e, "201320132013"));
}

TEST_CASE(the_old_two_line_reading_of_one_detent_is_one_detent)
{
    /* The sequence the bench showed: one detent pulses A, then B.  Read
     * as two lines it gave +1 and -1; as quadrature it is one detent. */
    dk518_enc_t e;
    rest_at(&e, 3u);
    CHECK_EQ(1, feed(&e, "1023"));
    CHECK_EQ(0, settle(&e));
}

TEST_CASE(a_bounce_counts_nothing)
{
    dk518_enc_t e;
    rest_at(&e, 3u);
    /* A chatters at the start of a detent, then the detent completes. */
    CHECK_EQ(0, feed(&e, "131313"));
    CHECK_EQ(0, settle(&e));
    CHECK_EQ(1, feed(&e, "1310102323"));
    /* B chatters mid-way and the detent still counts once. */
    CHECK_EQ(1, feed(&e, "101010023"));
    CHECK_EQ(0, settle(&e));
}

TEST_CASE(a_half_turn_that_goes_back_counts_nothing)
{
    dk518_enc_t e;
    rest_at(&e, 3u);
    CHECK_EQ(0, feed(&e, "1013"));
    CHECK_EQ(0, settle(&e));
    CHECK_EQ(0, feed(&e, "201023"));
    CHECK_EQ(0, settle(&e));
}

TEST_CASE(an_encoder_of_two_edges_a_detent_counts_each_rest)
{
    /* Rests at 11 and at 00, each a detent away. */
    dk518_enc_t e;
    rest_at(&e, 3u);
    CHECK_EQ(0, feed(&e, "10"));
    CHECK_EQ(1, settle(&e));
    CHECK_EQ(1, feed(&e, "23"));
    CHECK_EQ(0, settle(&e));
    CHECK_EQ(0, feed(&e, "20"));
    CHECK_EQ(-1, settle(&e));
    CHECK_EQ(-1, feed(&e, "13"));
}

TEST_CASE(a_fast_turn_counts_every_full_cycle)
{
    /* Three detents without a pause: each passes home and counts. */
    dk518_enc_t e;
    rest_at(&e, 3u);
    dk518_enc_t f = e;
    CHECK_EQ(3, feed(&e, "102310231023"));
    /* Edges lost to a missed interrupt: the pair jumps, no step moves. */
    CHECK_EQ(1, feed(&f, "10" "3"));
    CHECK_EQ(0, settle(&f));
    CHECK_EQ(0, feed(&f, "0"));
    CHECK_EQ(0, settle(&f));
}

TEST_CASE(the_home_rest_is_the_first_quiet_pair)
{
    dk518_enc_t e;
    s_now = 100u;
    dk518_enc_init(&e);
    /* Edges before the first rest count there. */
    CHECK_EQ(0, dk518_enc_edge(&e, 0u, s_now));
    CHECK_EQ(0, dk518_enc_poll(&e, s_now + DK518_REST_MS - 1u));
    s_now += DK518_REST_MS;
    CHECK_EQ(0, dk518_enc_poll(&e, s_now));
    /* Home is 00: a detent from it counts on the return. */
    CHECK_EQ(1, feed(&e, "2310"));
    CHECK_EQ(0, settle(&e));
    /* Bits above the pair are ignored. */
    CHECK_EQ(1, feed(&e, "\x3a\x3b\x39\x38"));
}

TEST_CASE(the_trace_names_each_pair_once_and_its_edges)
{
    dk518_enc_t e;
    dk518_trace_t t;
    char text[96];
    rest_at(&e, 3u);
    CHECK(!dk518_enc_last(&e, &t));
    CHECK_EQ(1, feed(&e, "1023"));
    CHECK(dk518_enc_last(&e, &t));
    CHECK(!dk518_enc_last(&e, &t));
    CHECK_EQ(1, t.detents);
    CHECK_EQ(4u, t.edges);
    CHECK_EQ(5u, t.n);
    CHECK_EQ(6u, t.ms);
    dk518_enc_text(&t, text, sizeof text);
    CHECK_STR_EQ(text, "CW 1, 4 edges, 6ms: 11>01>00>10>11");
    /* A long chatter keeps the first pairs and counts every edge. */
    CHECK_EQ(-1, feed(&e, "2323232323232323232013"));
    CHECK(dk518_enc_last(&e, &t));
    CHECK_EQ(-1, t.detents);
    CHECK_EQ(22u, t.edges);
    CHECK_EQ(DK518_TRACE_MAX, t.n);
    dk518_enc_text(&t, text, 20u);
    CHECK_EQ(19u, (unsigned)strlen(text));
}

int main(void)
{
    RUN(a_clockwise_detent_of_four_edges_counts_one);
    RUN(an_anticlockwise_detent_of_four_edges_counts_minus_one);
    RUN(the_old_two_line_reading_of_one_detent_is_one_detent);
    RUN(a_bounce_counts_nothing);
    RUN(a_half_turn_that_goes_back_counts_nothing);
    RUN(an_encoder_of_two_edges_a_detent_counts_each_rest);
    RUN(a_fast_turn_counts_every_full_cycle);
    RUN(the_home_rest_is_the_first_quiet_pair);
    RUN(the_trace_names_each_pair_once_and_its_edges);
    return test_summary("dk518_enc");
}
