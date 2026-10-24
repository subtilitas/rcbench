/*
 * The SENSE_LIMIT page (sense_limit.h, protocol 4.27): a current limit per
 * channel and the windows that passed it.
 *
 * Under test: the layout and the minor; every register range of a write
 * and a limit at 0, 32767 and 32768; a refused write storing nothing; a
 * window's highest current at limit - 1, limit and limit + 1mA, across
 * the rounding to mA and at the highest limit (sense_window_over()); a
 * clipped sample against a range top below, at and above the limit,
 * unrounded (163.8mA on 1Ohm), the bottom end, a negative current, no
 * samples and no limit; core 1's counts into OVER and OVER_COUNT, several
 * windows in one snapshot, a stale set-up's snapshot, a set-up, the count's
 * wrap; the limits in core 1's order; OVER sticky until a read covers it,
 * the peek and a read off the page leaving it; OVER_COUNT wrapping at
 * 65536; what is kept and its restore at boot.  Core 1's side, each
 * window judged as it closes and a part offline, is in test_sense_svc.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "outputs.h"
#include "sense_limit.h"
#include "sense_map.h"
#include "sense_page.h"
#include "sense_svc.h"

static outputs_t    o;
static sense_page_t pg;

/* A bank with nothing bound; the page fresh. */
static void fresh(void)
{
    outputs_init(&o, 0u);
    sense_page_init(&pg);
}

/* Parts @p fit in force, every channel of theirs on 1000 (0.1Ohm, a
 * range top of 1638mA). */
static void map_all(uint16_t fit)
{
    uint16_t m[LINK_SM_KEPT];
    memset(m, 0, sizeof(m));
    m[LINK_SM_ENABLE] = fit;
    m[LINK_SM_FAST] = 0u;
    for (unsigned c = 0; c < LINK_SM_CHANNELS; ++c) {
        if ((fit & (1u << (c / 3u))) != 0u) {
            m[LINK_SM_SHUNT(c)] = 1000u;
        }
    }
    CHECK_EQ(sense_map_write(&pg, 0u, (uint8_t)LINK_SM_KEPT, m, &o, 0u),
             0u);
}

/* Window @p number under the set-up in force, every channel with samples
 * and a highest current of 0mA. */
static sense_snap_t snapshot(uint16_t number)
{
    sense_snap_t s;
    memset(&s, 0, sizeof(s));
    s.cfg_gen = pg.cfg_gen;
    s.open = true;
    s.have_win = true;
    for (unsigned a = 0; a < SENSE_I3221_PARTS; ++a) {
        s.i3221[a] = SENSE_PART_ONLINE;
    }
    for (unsigned c = 0; c < SENSE_SRC_COUNT; ++c) {
        s.win[c].number = number;
        s.win[c].n_i = 5u;
    }
    return s;
}

static uint8_t limit(unsigned c, uint16_t ma)
{
    return sense_limit_write(&pg, (uint8_t)c, 1u, &ma);
}

static uint16_t peek(unsigned r)
{
    uint16_t v = 0xFFFFu;
    sense_limit_peek(&pg, (uint8_t)r, 1u, &v);
    return v;
}

/* OVER read over the link, which clears it. */
static uint16_t take_over(void)
{
    uint16_t v = 0xFFFFu;
    sense_limit_read(&pg, (uint8_t)LINK_SL_OVER, 1u, &v);
    return v;
}

TEST_CASE(the_page_is_where_protocol_4_27_puts_it)
{
    CHECK_EQ(LINK_PAGE_SENSE_LIMIT, 0x36);
    CHECK_EQ(LINK_MINOR_SENSE_LIMIT, 27u);
    CHECK(LINK_PROTOCOL_MINOR >= LINK_MINOR_SENSE_LIMIT);
    CHECK_EQ(LINK_SL_KEPT, 12);
    CHECK_EQ(LINK_SL_OVER, 12);
    CHECK_EQ(LINK_SL_OVER_COUNT, 13);
    CHECK_EQ(LINK_SL_COUNT, 16);
    CHECK_EQ(LINK_SL_MA_MAX, 32767u);
    fresh();
    for (unsigned r = 0; r < LINK_SL_COUNT; ++r) {
        CHECK_EQ(peek(r), 0u);
    }
}

TEST_CASE(a_write_lies_within_the_limits_and_their_range)
{
    fresh();
    uint16_t v[LINK_SL_COUNT];
    memset(v, 0, sizeof(v));
    CHECK_EQ(sense_limit_write(&pg, 0u, (uint8_t)LINK_SL_KEPT, v), 0u);
    CHECK_EQ(sense_limit_write(&pg, 0u, 0u, v), 0u);
    CHECK_EQ(sense_limit_write(&pg, 0u, (uint8_t)LINK_SL_KEPT + 1u, v),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(sense_limit_write(&pg, (uint8_t)LINK_SL_OVER, 1u, v),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(sense_limit_write(&pg, 15u, 1u, v), LINK_NACK_READ_ONLY);
    CHECK_EQ(sense_limit_write(&pg, 15u, 2u, v), LINK_NACK_BAD_RANGE);
    CHECK_EQ(sense_limit_write(&pg, 0u, 17u, v), LINK_NACK_BAD_RANGE);
    CHECK_EQ(sense_limit_write(&pg, 0u, 1u, NULL), LINK_NACK_BAD_RANGE);
    CHECK_EQ(sense_limit_write(NULL, 0u, 1u, v), LINK_NACK_BAD_RANGE);
    CHECK_EQ(limit(11u, 32767u), 0u);
    CHECK_EQ(peek(11u), 32767u);
    CHECK_EQ(limit(11u, 32768u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(limit(11u, 0xFFFFu), LINK_NACK_BAD_VALUE);
    CHECK_EQ(peek(11u), 32767u);
    /* A write refused for one register stores none of them. */
    const uint16_t two[2] = { 500u, 40000u };
    CHECK_EQ(sense_limit_write(&pg, 3u, 2u, two), LINK_NACK_BAD_VALUE);
    CHECK_EQ(peek(3u), 0u);
    /* Taken while the bank drives: an alert stops nothing. */
    const out_slot_t pwm = { .driver = OUT_DRIVER_PWM, .first_channel = 0,
                             .channels = 1, .pin = 4, .rate_hz = 50 };
    CHECK(outputs_configure(&o, 0, &pwm));
    CHECK(outputs_set_role(&o, 0, OUT_ROLE_SURFACE));
    outputs_arm(&o, true, 0u);
    CHECK(outputs_driving(&o));
    CHECK_EQ(limit(0u, 100u), 0u);
    CHECK_EQ(peek(0u), 100u);
}

/* A window of 5 samples whose highest current is @p max_ua. */
static sense_window_t win_max(int32_t max_ua)
{
    sense_window_t w;
    memset(&w, 0, sizeof(w));
    w.n_i = 5u;
    w.i_max_ua = max_ua;
    return w;
}

TEST_CASE(a_window_is_over_above_its_limit_and_not_at_it)
{
    static const struct { int32_t ua; bool over; } k[] = {
        { 99000, false },  { 100000, false }, { 100499, false },
        { 100500, true },  { 101000, true },  { -150000, false },
        { INT32_MAX, true },
    };
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        const sense_window_t w = win_max(k[i].ua);
        CHECK_EQ(sense_window_over(&w, 1638000, 100u), k[i].over);
    }
    /* No limit, and no samples, are never over. */
    sense_window_t w = win_max(30000000);
    CHECK(!sense_window_over(&w, 1638000, 0u));
    w.n_i = 0u;
    CHECK(!sense_window_over(&w, 1638000, 1u));
    /* The highest limit. */
    w = win_max(32767499);
    CHECK(!sense_window_over(&w, 0, 32767u));
    w = win_max(32767500);
    CHECK(sense_window_over(&w, 0, 32767u));
}

TEST_CASE(a_clipped_sample_is_over_when_the_unrounded_top_is)
{
    /* Every sample clipped: no value, the top of the range only. */
    sense_window_t w;
    memset(&w, 0, sizeof(w));
    w.clip_hi = true;
    /* 0.1Ohm: 1638.000mA. */
    CHECK(sense_window_over(&w, 1638000, 1637u));
    CHECK(!sense_window_over(&w, 1638000, 1638u));
    CHECK(!sense_window_over(&w, 1638000, 1639u));
    /* 1Ohm: 163.800mA, above a limit of 163mA. */
    CHECK(sense_window_over(&w, 163800, 163u));
    CHECK(!sense_window_over(&w, 163800, 164u));
    /* A channel with no shunt has no top; the bottom end never counts. */
    CHECK(!sense_window_over(&w, 0, 1u));
    w.clip_hi = false;
    w.clip_lo = true;
    CHECK(!sense_window_over(&w, 1638000, 1u));
}

TEST_CASE(core_1s_counts_become_over_and_over_count)
{
    fresh();
    map_all(0x0Fu);
    sense_snap_t s = snapshot(10u);
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(peek(LINK_SL_OVER), 0u);
    /* One window over on channel 0, three on channel 11 -- windows that
     * closed between two snapshots count all the same. */
    s.over_n[0] = 1u;
    s.over_n[11] = 3u;
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(take_over(), 0x801u);
    CHECK_EQ(peek(LINK_SL_OVER_COUNT), 4u);
    /* The same counts again: nothing new. */
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(take_over(), 0u);
    CHECK_EQ(peek(LINK_SL_OVER_COUNT), 4u);
    /* A snapshot of the set-up before is not taken; its counts are, with
     * the next snapshot that is. */
    s.over_n[5] = 2u;
    s.cfg_gen = (uint16_t)(pg.cfg_gen - 1u);
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(peek(LINK_SL_OVER), 0u);
    s.cfg_gen = pg.cfg_gen;
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(take_over(), 0x020u);
    CHECK_EQ(peek(LINK_SL_OVER_COUNT), 6u);
    /* A set-up taken keeps OVER, OVER_COUNT and the counts seen: core 1's
     * counts run on across set-ups. */
    s.over_n[1] = 1u;
    sense_page_publish(&pg, &s, 0u);
    uint16_t sh = 1500u;
    CHECK_EQ(sense_map_write(&pg, (uint8_t)LINK_SM_SHUNT(2u), 1u, &sh, &o,
                             0u), 0u);
    CHECK_EQ(peek(LINK_SL_OVER), 0x002u);
    s.cfg_gen = pg.cfg_gen;
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(take_over(), 0x002u);
    CHECK_EQ(peek(LINK_SL_OVER_COUNT), 7u);
    /* Core 1's count wraps: 65535 to 1 is two windows. */
    pg.lim_seen[3] = 65535u;
    s.over_n[3] = 1u;
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(take_over(), 0x008u);
    CHECK_EQ(peek(LINK_SL_OVER_COUNT), 9u);
}

TEST_CASE(the_limits_go_to_core_1_in_its_order)
{
    fresh();
    for (unsigned c = 0; c < LINK_SL_KEPT; ++c) {
        CHECK_EQ(limit(c, (uint16_t)(c == 11u ? 32767u : 10u * c)), 0u);
    }
    sense_cmd_t cmd;
    memset(&cmd, 0xA5, sizeof(cmd));
    sense_page_cmd(&pg, &cmd);
    CHECK_EQ(cmd.limit_ma[0], 0u);
    CHECK_EQ(cmd.limit_ma[4], 40u);
    CHECK_EQ(cmd.limit_ma[11], 32767u);
}

TEST_CASE(over_holds_until_a_read_covers_it)
{
    fresh();
    map_all(0x03u);
    CHECK_EQ(limit(4u, 10u), 0u);
    sense_snap_t s = snapshot(1u);
    s.over_n[4] = 1u;
    sense_page_publish(&pg, &s, 0u);
    /* Snapshots after it with no window over leave the bit. */
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(peek(LINK_SL_OVER), 0x010u);
    /* The peek, a read of the limits, of OVER_COUNT alone and one off the
     * page leave it. */
    uint16_t r[LINK_SL_COUNT];
    sense_limit_read(&pg, 0u, (uint8_t)LINK_SL_KEPT, r);
    sense_limit_read(&pg, (uint8_t)LINK_SL_OVER_COUNT, 1u, r);
    sense_limit_read(&pg, (uint8_t)LINK_SL_OVER, 5u, r);
    sense_limit_read(&pg, (uint8_t)LINK_SL_OVER, 1u, NULL);
    sense_limit_read(NULL, (uint8_t)LINK_SL_OVER, 1u, r);
    CHECK_EQ(peek(LINK_SL_OVER), 0x010u);
    /* A read of the whole page gives it, then it is clear. */
    memset(r, 0xFF, sizeof(r));
    sense_limit_read(&pg, 0u, (uint8_t)LINK_SL_COUNT, r);
    CHECK_EQ(r[LINK_SL_OVER], 0x010u);
    CHECK_EQ(r[LINK_SL_OVER_COUNT], 1u);
    CHECK_EQ(r[4], 10u);
    CHECK_EQ(r[14], 0u);
    CHECK_EQ(r[15], 0u);
    CHECK_EQ(peek(LINK_SL_OVER), 0u);
    CHECK_EQ(peek(LINK_SL_OVER_COUNT), 1u);
    /* The peek off the page gives nothing. */
    uint16_t x = 0xABCDu;
    sense_limit_peek(&pg, 15u, 2u, &x);
    sense_limit_peek(NULL, 0u, 1u, &x);
    CHECK_EQ(x, 0xABCDu);
}

TEST_CASE(over_count_wraps_at_65536)
{
    fresh();
    pg.limit[LINK_SL_OVER_COUNT] = 65535u;
    sense_snap_t s = snapshot(3u);
    s.over_n[0] = 1u;
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(peek(LINK_SL_OVER_COUNT), 0u);
    CHECK_EQ(take_over(), 0x001u);
}

TEST_CASE(the_limits_are_kept_and_come_back_at_boot)
{
    fresh();
    for (unsigned c = 0; c < LINK_SL_KEPT; ++c) {
        CHECK_EQ(limit(c, (uint16_t)(100u * (c + 1u))), 0u);
    }
    uint16_t kept[LINK_SL_KEPT];
    sense_limit_kept(&pg, kept);
    CHECK_EQ(kept[0], 100u);
    CHECK_EQ(kept[11], 1200u);
    fresh();
    CHECK_EQ(sense_limit_restore(&pg, kept), 0u);
    CHECK_EQ(peek(11u), 1200u);
    CHECK_EQ(peek(LINK_SL_OVER), 0u);
    CHECK_EQ(peek(LINK_SL_OVER_COUNT), 0u);
    /* A kept value out of range: refused, every limit stays 0. */
    kept[6] = 40000u;
    fresh();
    CHECK_EQ(sense_limit_restore(&pg, kept), LINK_NACK_BAD_VALUE);
    for (unsigned c = 0; c < LINK_SL_KEPT; ++c) {
        CHECK_EQ(peek(c), 0u);
    }
}

int main(void)
{
    RUN(the_page_is_where_protocol_4_27_puts_it);
    RUN(a_write_lies_within_the_limits_and_their_range);
    RUN(a_window_is_over_above_its_limit_and_not_at_it);
    RUN(a_clipped_sample_is_over_when_the_unrounded_top_is);
    RUN(core_1s_counts_become_over_and_over_count);
    RUN(the_limits_go_to_core_1_in_its_order);
    RUN(over_holds_until_a_read_covers_it);
    RUN(over_count_wraps_at_65536);
    RUN(the_limits_are_kept_and_come_back_at_boot);
    return test_summary("sense_limit");
}
