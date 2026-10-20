/*
 * The SENSE_LIMIT page (sense_limit.h, protocol 4.27): a current limit per
 * channel and the windows that passed it.
 *
 * Under test: the layout and the minor; every register range of a write
 * and a limit at 0, 32767 and 32768; a refused write storing nothing; a
 * window's highest current at limit - 1, limit and limit + 1 mA and across
 * the rounding to mA; a clipped sample against a range top below, at and
 * above the limit, the bottom end, a negative current, a channel off and
 * no limit, a part offline at the window's close; each window judged once, again after a set-up; OVER sticky
 * until a read covers it, the peek and a read off the page leaving it;
 * OVER_COUNT wrapping at 65536; the derived map's shunt; what is kept and
 * its restore at boot.
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

static outputs_t    o;
static sense_page_t pg;

/* A bank with nothing bound; the page fresh. */
static void fresh(void)
{
    outputs_init(&o, 0u);
    sense_page_init(&pg);
}

/* Parts @p fit in force, every channel of theirs on 1000 (0.1 Ohm, a
 * range top of 1638 mA). */
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
 * and a highest current of 0 mA. */
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

TEST_CASE(a_window_is_over_above_its_limit_and_not_at_it)
{
    fresh();
    map_all(0x01u);
    CHECK_EQ(limit(0u, 100u), 0u);
    static const struct { int32_t ua; bool over; } k[] = {
        { 99000, false },  { 100000, false }, { 101000, true },
        { 100499, false }, { 100500, true },  { -150000, false },
    };
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        sense_snap_t s = snapshot((uint16_t)(10u + i));
        s.win[0].i_max_ua = k[i].ua;
        sense_page_publish(&pg, &s, 0u);
        CHECK_EQ(take_over(), k[i].over ? 0x001u : 0u);
    }
    CHECK_EQ(peek(LINK_SL_OVER_COUNT), 2u);
    /* No limit, and a channel with no samples, are never over. */
    CHECK_EQ(limit(0u, 0u), 0u);
    CHECK_EQ(limit(1u, 1u), 0u);
    sense_snap_t s = snapshot(20u);
    s.win[0].i_max_ua = 30000000;
    s.win[1].n_i = 0u;
    s.win[1].i_max_ua = 0;
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(take_over(), 0u);
    /* Limit 1 mA, 2 mA read: over; every channel at once. */
    for (unsigned c = 0; c < LINK_SM_CHANNELS; ++c) {
        CHECK_EQ(limit(c, 1u), 0u);
    }
    s = snapshot(21u);
    for (unsigned c = 0; c < LINK_SM_CHANNELS; ++c) {
        s.win[c].i_max_ua = 2000;
    }
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(take_over(), 0x0FFFu);
    CHECK_EQ(peek(LINK_SL_OVER_COUNT), 2u + 12u);
    /* A part offline at the window's close: its samples from before are
     * not judged, as SENSE_WIN shows them 0. */
    s.win[0].number = 22u;
    s.i3221[1] = SENSE_PART_OFFLINE;
    s.i3221[3] = SENSE_PART_WRONG_ID;
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(take_over(), 0x1C7u);
}

TEST_CASE(a_clipped_sample_is_over_when_the_range_top_is)
{
    fresh();
    map_all(0x01u);
    /* 0.1 Ohm: the top of the range is 1638 mA. */
    CHECK_EQ(sense_i3221_full_scale_ma(1000u), 1638u);
    static const struct { uint16_t lim; bool over; } k[] = {
        { 1637u, true }, { 1638u, false }, { 1639u, false },
    };
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        CHECK_EQ(limit(2u, k[i].lim), 0u);
        sense_snap_t s = snapshot((uint16_t)(40u + i));
        s.win[2].n_i = 0u;              /* every sample clipped */
        s.win[2].clip_hi = true;
        sense_page_publish(&pg, &s, 0u);
        CHECK_EQ(take_over(), k[i].over ? 0x004u : 0u);
    }
    /* The bottom end is a negative current: never over. */
    CHECK_EQ(limit(2u, 1u), 0u);
    sense_snap_t s = snapshot(50u);
    s.win[2].clip_lo = true;
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(take_over(), 0u);
    /* A clipped sample on a channel with no shunt has no top. */
    s = snapshot(51u);
    s.win[5].clip_hi = true;
    CHECK_EQ(limit(5u, 1u), 0u);
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(take_over(), 0u);
}

TEST_CASE(each_window_is_judged_once_and_again_after_a_set_up)
{
    fresh();
    map_all(0x01u);
    CHECK_EQ(limit(0u, 100u), 0u);
    sense_snap_t s = snapshot(7u);
    s.win[0].i_max_ua = 200000;
    sense_page_publish(&pg, &s, 0u);
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(peek(LINK_SL_OVER_COUNT), 1u);
    s.win[0].number = 8u;
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(peek(LINK_SL_OVER_COUNT), 2u);
    /* No window closed yet: nothing judged. */
    s.have_win = false;
    s.win[0].number = 9u;
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(peek(LINK_SL_OVER_COUNT), 2u);
    /* A set-up taken: its windows count from 0 again, and a window
     * number seen under the set-up before is a new window. */
    uint16_t sh = 1500u;
    CHECK_EQ(sense_map_write(&pg, (uint8_t)LINK_SM_SHUNT(1u), 1u, &sh, &o,
                             0u), 0u);
    s = snapshot(8u);
    s.win[0].i_max_ua = 200000;
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(peek(LINK_SL_OVER_COUNT), 3u);
    /* A snapshot of the set-up before is not judged. */
    s.cfg_gen = (uint16_t)(pg.cfg_gen - 1u);
    s.win[0].number = 9u;
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(peek(LINK_SL_OVER_COUNT), 3u);
    /* The window number wraps: 65535 then 0 are two windows. */
    s = snapshot(65535u);
    s.win[0].i_max_ua = 200000;
    sense_page_publish(&pg, &s, 0u);
    s.win[0].number = 0u;
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(peek(LINK_SL_OVER_COUNT), 5u);
}

TEST_CASE(over_holds_until_a_read_covers_it)
{
    fresh();
    map_all(0x03u);
    CHECK_EQ(limit(4u, 10u), 0u);
    sense_snap_t s = snapshot(1u);
    s.win[4].i_max_ua = 50000;
    sense_page_publish(&pg, &s, 0u);
    /* Windows after it under the limit leave the bit. */
    s.win[4].i_max_ua = 0;
    s.win[4].number = 2u;
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
    map_all(0x01u);
    CHECK_EQ(limit(0u, 1u), 0u);
    pg.limit[LINK_SL_OVER_COUNT] = 65535u;
    sense_snap_t s = snapshot(3u);
    s.win[0].i_max_ua = 5000;
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(peek(LINK_SL_OVER_COUNT), 0u);
    CHECK_EQ(take_over(), 0x001u);
}

TEST_CASE(the_derived_map_gives_senses_shunt)
{
    fresh();
    /* SENSE's one INA3221 at 0x41 on 0.05 Ohm, CH1 to CH3: channels 3 to
     * 5, range top 3276 mA. */
    const uint16_t f[4] = { 0x41u, 500u, 0x07u, 0u };
    CHECK_EQ(sense_page_write(&pg, LINK_SN_I3221_ADDR, 4u, f, &o, 0u), 0u);
    const uint16_t en = LINK_SN_EN_I3221;
    CHECK_EQ(sense_page_write(&pg, LINK_SN_ENABLE, 1u, &en, &o, 0u), 0u);
    CHECK(!pg.map_set);
    CHECK_EQ(limit(3u, 3275u), 0u);
    CHECK_EQ(limit(0u, 1u), 0u);
    sense_snap_t s = snapshot(5u);
    s.win[3].n_i = 0u;
    s.win[3].clip_hi = true;
    s.win[0].n_i = 0u;              /* 0x40 not fitted: no samples */
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(take_over(), 0x008u);
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
    RUN(a_clipped_sample_is_over_when_the_range_top_is);
    RUN(each_window_is_judged_once_and_again_after_a_set_up);
    RUN(over_holds_until_a_read_covers_it);
    RUN(over_count_wraps_at_65536);
    RUN(the_derived_map_gives_senses_shunt);
    RUN(the_limits_are_kept_and_come_back_at_boot);
    return test_summary("sense_limit");
}
