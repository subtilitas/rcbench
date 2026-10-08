/*
 * The panel's half of the TONE page (protocol 4.8) against a small model of
 * the coprocessor's half that follows link_pages.h, and against a
 * coprocessor that speaks protocol 4.7.
 *
 * Under test: nothing sent to a coprocessor older than 4.8, and the
 * operator told when the tap is enabled for one; the page read before
 * anything is written and only what differs written; the two frames of the
 * set-up in the order that leaves the page a valid set-up; a refused frame
 * not written again until the set-up changes, a pin refused for a binding
 * offered again until it is free; the status read at 20 Hz; beeps taken by
 * number across the wrap from 65535 to 1, with replies lost, with the
 * panel more than the ring behind, and with the numbering restarted; the
 * FLAGS states the screen shows; and the events a read raises, each once.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "tone_link.h"

/* ----------------------------------------------------- the coprocessor */

typedef struct {
    uint16_t seq;
    uint32_t start_ms;
    uint16_t len_dms, freq_dhz, bursts, carrier_hhz, flags;
} fake_beep_t;

typedef struct {
    uint16_t cfg[LINK_TN_CONFIG_COUNT];
    uint16_t sel;
    uint16_t head;
    uint16_t flags, window, win_freq, win_periods, lost, glitches;
    fake_beep_t ring[LINK_TN_RING];
    int      bad_pin;           /**< a pin an enabled tap is refused on */
    unsigned frames_taken;      /**< writes of regs 0 to 7 that landed   */
    uint16_t order[8];          /**< the first register of each         */
} fake_t;

static fake_t       fk;
static tone_link_t  tl;
static uint32_t     now;
static uint16_t     minor;
static unsigned     writes, reads, to_tone;
static bool         lose_next;      /**< the next reply is lost          */
static bool         land_lost;      /**< a write whose reply is lost lands */

static void fake_init(void)
{
    memset(&fk, 0, sizeof(fk));
    fk.cfg[LINK_TN_PIN]         = LINK_TN_DEFAULT_PIN;
    fk.cfg[LINK_TN_F_MIN_HZ]    = LINK_TN_DEFAULT_F_MIN;
    fk.cfg[LINK_TN_F_MAX_HZ]    = LINK_TN_DEFAULT_F_MAX;
    fk.cfg[LINK_TN_SPLIT_PCT]   = LINK_TN_DEFAULT_SPLIT;
    fk.cfg[LINK_TN_GAP_MS]      = LINK_TN_DEFAULT_GAP_MS;
    fk.cfg[LINK_TN_MIN_PERIODS] = LINK_TN_DEFAULT_PERIODS;
    fk.bad_pin = -1;
}

/* The set-up the page would hold after a write: whether it takes it. */
static bool fake_valid(const uint16_t *c)
{
    if (c[LINK_TN_ENABLE] > 1u) {
        return false;
    }
    if (c[LINK_TN_ENABLE] != 0u && (int)c[LINK_TN_PIN] == fk.bad_pin) {
        return false;
    }
    return c[LINK_TN_F_MIN_HZ] >= LINK_TN_F_MIN_LO
           && c[LINK_TN_F_MIN_HZ] <= LINK_TN_F_MIN_HI
           && c[LINK_TN_F_MAX_HZ] > c[LINK_TN_F_MIN_HZ]
           && c[LINK_TN_F_MAX_HZ] <= LINK_TN_F_MAX_HI
           && c[LINK_TN_SPLIT_PCT] <= LINK_TN_SPLIT_MAX
           && c[LINK_TN_GAP_MS] >= LINK_TN_GAP_MS_MIN
           && c[LINK_TN_GAP_MS] <= LINK_TN_GAP_MS_MAX
           && (uint32_t)c[LINK_TN_GAP_MS] * 1000u
                  >= 1000000u / c[LINK_TN_F_MIN_HZ]
           && c[LINK_TN_MIN_PERIODS] >= LINK_TN_PERIODS_MIN
           && c[LINK_TN_MIN_PERIODS] <= LINK_TN_PERIODS_MAX;
}

static int fake_write(uint8_t off, uint8_t n, const uint16_t *regs)
{
    if (off == LINK_TN_EVT_SEL && n == 1u) {
        fk.sel = regs[0];
        return TONE_LINK_ACK;
    }
    if ((off != 0u && off != 4u) || n != 4u) {
        return LINK_NACK_BAD_RANGE;
    }
    uint16_t next[LINK_TN_CONFIG_COUNT];
    memcpy(next, fk.cfg, sizeof(next));
    for (unsigned i = 0u; i < n; ++i) {
        if (off + i < LINK_TN_CONFIG_COUNT) {
            next[off + i] = regs[i];
        } else if (regs[i] != 0u) {
            return LINK_NACK_BAD_VALUE;     /* register 7 takes 0 */
        }
    }
    if (!fake_valid(next)) {
        return LINK_NACK_BAD_VALUE;
    }
    memcpy(fk.cfg, next, sizeof(next));
    if (fk.frames_taken < sizeof(fk.order) / sizeof(fk.order[0])) {
        fk.order[fk.frames_taken] = off;
    }
    ++fk.frames_taken;
    fk.flags = (fk.cfg[LINK_TN_ENABLE] != 0u)
                   ? (uint16_t)(fk.flags | LINK_TN_RUNNING)
                   : 0u;
    return TONE_LINK_ACK;
}

static void fake_read(uint8_t off, uint8_t n, uint16_t *regs)
{
    uint16_t all[LINK_TN_COUNT];
    memset(all, 0, sizeof(all));
    memcpy(all, fk.cfg, sizeof(fk.cfg));
    all[LINK_TN_FLAGS]        = fk.flags;
    all[LINK_TN_WINDOW]       = fk.window;
    all[LINK_TN_WIN_FREQ_DHZ] = fk.win_freq;
    all[LINK_TN_WIN_PERIODS]  = fk.win_periods;
    all[LINK_TN_BEEP_HEAD]    = fk.head;
    all[LINK_TN_EVT_SEL]      = fk.sel;
    all[LINK_TN_LOST]         = fk.lost;
    all[LINK_TN_GLITCHES]     = fk.glitches;
    if (fk.sel != 0u) {
        const fake_beep_t *b = &fk.ring[(fk.sel - 1u) % LINK_TN_RING];
        if (b->seq == fk.sel) {
            all[LINK_TN_EVT_SEQ]        = b->seq;
            all[LINK_TN_EVT_START_LO]   = (uint16_t)(b->start_ms & 0xFFFFu);
            all[LINK_TN_EVT_START_HI]   = (uint16_t)(b->start_ms >> 16);
            all[LINK_TN_EVT_LEN_DMS]    = b->len_dms;
            all[LINK_TN_EVT_FREQ_DHZ]   = b->freq_dhz;
            all[LINK_TN_EVT_BURSTS]     = b->bursts;
            all[LINK_TN_EVT_CARRIER_HHZ] = b->carrier_hhz;
            all[LINK_TN_EVT_FLAGS]      = b->flags;
        }
    }
    memset(regs, 0, LINK_MAX_REGS * sizeof(uint16_t));
    memcpy(regs, &all[off], n * sizeof(uint16_t));
}

/* One beep, numbered as the coprocessor numbers them: 1 to 65535, then 1.
 * Its length and pitch follow its number, so a test reads them back. */
static void fake_beep(void)
{
    fk.head = (fk.head >= 65535u) ? 1u : (uint16_t)(fk.head + 1u);
    fake_beep_t *b = &fk.ring[(fk.head - 1u) % LINK_TN_RING];
    b->seq         = fk.head;
    b->start_ms    = 70000u + 100u * (uint32_t)fk.head;
    b->len_dms     = (uint16_t)(1000u + fk.head % 1000u);
    b->freq_dhz    = (uint16_t)(20000u + fk.head % 1000u);
    b->bursts      = (uint16_t)(fk.head % 7u);
    b->carrier_hhz = (fk.head % 2u != 0u) ? 240u : 0u;
    b->flags       = (uint16_t)(fk.head % 4u);
}

static void fake_beeps(unsigned n)
{
    while (n-- > 0u) {
        fake_beep();
    }
}

/* The ring as it is when the newest beep is number @p n. */
static void fake_head(uint16_t n)
{
    fk.head = (uint16_t)(n - 1u);
    fake_beeps(1);
}

/* One exchange as the coprocessor answers it.  A 4.7 coprocessor has no
 * TONE page and refuses it with BAD_PAGE. */
static int far_exchange(const tone_link_op_t *op, uint16_t *regs)
{
    if (op->page != LINK_PAGE_TONE) {
        return LINK_NACK_BAD_PAGE;
    }
    ++to_tone;
    if (minor < TONE_LINK_MINOR) {
        return LINK_NACK_BAD_PAGE;
    }
    if (op->write) {
        ++writes;
        if (lose_next && !land_lost) {
            lose_next = false;
            return TONE_LINK_NO_ANSWER;
        }
        const int r = fake_write(op->off, op->n, op->regs);
        if (lose_next) {
            lose_next = false;
            return TONE_LINK_NO_ANSWER;
        }
        return r;
    }
    ++reads;
    fake_read(op->off, op->n, regs);
    if (lose_next) {
        lose_next = false;
        return TONE_LINK_NO_ANSWER;
    }
    return TONE_LINK_ACK;
}

/* The setup SETUP starts with: the tap off, the page's own defaults. */
static tone_setup_t setup_default(void)
{
    const tone_setup_t w = {
        .enable = false, .pin = LINK_TN_DEFAULT_PIN,
        .f_min_hz = LINK_TN_DEFAULT_F_MIN, .f_max_hz = LINK_TN_DEFAULT_F_MAX,
        .split_pct = LINK_TN_DEFAULT_SPLIT, .gap_ms = LINK_TN_DEFAULT_GAP_MS,
        .min_periods = LINK_TN_DEFAULT_PERIODS,
    };
    return w;
}

static void want(const tone_setup_t *w)
{
    tone_link_want(&tl, w, now);
}

static void fresh(uint16_t m)
{
    fake_init();
    tone_link_init(&tl);
    now = 10000u;
    minor = m;
    writes = reads = to_tone = 0u;
    lose_next = false;
    land_lost = false;
    const tone_setup_t w = setup_default();
    want(&w);
    tone_link_came_up(&tl, minor, now);
}

/* One poll of the control task: every exchange owed, then 50 ms on. */
static void poll_once(void)
{
    for (int k = 0; k < 8; ++k) {
        tone_link_op_t op;
        if (!tone_link_next(&tl, now, &op)) {
            break;
        }
        uint16_t regs[LINK_MAX_REGS];
        const int result = far_exchange(&op, regs);
        tone_link_done(&tl, result, op.write ? NULL : regs, now);
    }
    now += 50u;
}

static void polls(int n)
{
    for (int i = 0; i < n; ++i) {
        poll_once();
    }
}

/* Every exchange owed, without the poll's cap and without the clock. */
static void drain(void)
{
    for (int k = 0; k < 400; ++k) {
        tone_link_op_t op;
        if (!tone_link_next(&tl, now, &op)) {
            return;
        }
        uint16_t regs[LINK_MAX_REGS];
        const int result = far_exchange(&op, regs);
        tone_link_done(&tl, result, op.write ? NULL : regs, now);
    }
}

/* The tap on and running, the page read and the first status taken. */
static void tap_on(void)
{
    tone_setup_t w = setup_default();
    w.enable = true;
    want(&w);
    polls(14);
}

/* ---------------------------------------------------------- the version */

TEST_CASE(nothing_is_sent_to_a_4_7_coprocessor)
{
    fresh(7u);
    polls(20);
    CHECK_EQ(to_tone, 0u);
    /* The tap off: nothing to say either. */
    CHECK_EQ(tone_link_event(&tl, now), 0u);

    tone_setup_t w = setup_default();
    w.enable = true;
    want(&w);
    /* Enabled for a coprocessor that cannot read it: said at once. */
    CHECK_EQ(tone_link_event(&tl, now), TONE_LINK_EV_NO_PAGE);
    polls(40);
    CHECK_EQ(to_tone, 0u);
    CHECK_EQ(tone_link_event(&tl, now + 100000u), 0u);

    tone_readout_t r;
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.state, TONE_STATE_NO_PAGE);
    CHECK(tone_link_settled(&tl));
}

TEST_CASE(a_tap_enabled_before_the_link_is_said_at_link_up)
{
    fake_init();
    tone_link_init(&tl);
    now = 5000u;
    minor = 7u;
    tone_setup_t w = setup_default();
    w.enable = true;
    want(&w);
    CHECK_EQ(tone_link_event(&tl, now), 0u);    /* no coprocessor yet */
    tone_link_came_up(&tl, 7u, now);
    CHECK_EQ(tone_link_event(&tl, now), TONE_LINK_EV_NO_PAGE);
    /* Disabled again, the event that waited goes. */
    fresh(7u);
    w.enable = true;
    want(&w);
    w.enable = false;
    want(&w);
    CHECK_EQ(tone_link_event(&tl, now), 0u);
}

TEST_CASE(a_coprocessor_that_names_4_8_and_refuses_the_page_is_left_alone)
{
    fresh(8u);
    tone_setup_t w = setup_default();
    w.enable = true;
    want(&w);
    /* The identity says 4.8; the page is refused all the same. */
    minor = 7u;
    polls(2);
    CHECK_EQ(to_tone, 1u);                      /* the read of the set-up */
    CHECK_EQ(tone_link_event(&tl, now), TONE_LINK_EV_NO_PAGE);
    polls(40);
    CHECK_EQ(to_tone, 1u);
}

TEST_CASE(a_4_8_coprocessor_is_read_and_written)
{
    fresh(8u);
    tap_on();
    CHECK(to_tone > 2u);
    CHECK_EQ(fk.cfg[LINK_TN_ENABLE], LINK_TN_EN_TAP);
    tone_readout_t r;
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.state, TONE_STATE_RUNNING);
}

/* ------------------------------------------------------------ the set-up */

TEST_CASE(the_page_is_read_first_and_only_what_differs_is_written)
{
    fresh(8u);
    polls(30);
    /* One read of registers 0 to 6, nothing written, nothing polled: the
     * tap is off on the page. */
    CHECK_EQ(reads, 1u);
    CHECK_EQ(writes, 0u);
    CHECK(tone_link_settled(&tl));

    /* A set-up the page holds already is not written. */
    fresh(8u);
    fk.cfg[LINK_TN_PIN] = 23u;
    tone_setup_t w = setup_default();
    w.pin = 23u;
    want(&w);
    polls(30);
    CHECK_EQ(writes, 0u);
}

TEST_CASE(a_zeroed_setup_writes_nothing)
{
    fresh(8u);
    tone_setup_t zero;
    memset(&zero, 0, sizeof(zero));
    tone_link_want(&tl, &zero, now);
    polls(30);
    CHECK_EQ(writes, 0u);
    /* Out of the page's range in one value: also none. */
    tone_setup_t w = setup_default();
    w.f_min_hz = 49u;
    want(&w);
    w.f_min_hz = LINK_TN_F_MIN_HI + 1u;
    want(&w);
    w = setup_default();
    w.f_max_hz = LINK_TN_F_MAX_HI + 1u;
    want(&w);
    w = setup_default();
    w.split_pct = LINK_TN_SPLIT_MAX + 1u;
    want(&w);
    w = setup_default();
    w.gap_ms = 0u;
    want(&w);
    w.gap_ms = LINK_TN_GAP_MS_MAX + 1u;
    want(&w);
    w = setup_default();
    w.min_periods = 0u;
    want(&w);
    w.min_periods = LINK_TN_PERIODS_MAX + 1u;
    want(&w);
    polls(30);
    CHECK_EQ(writes, 0u);
}

TEST_CASE(enabling_the_tap_writes_the_first_frame_and_polls_the_status)
{
    fresh(8u);
    polls(2);
    tone_setup_t w = setup_default();
    w.enable = true;
    want(&w);
    /* A write waits for the set-up to rest. */
    polls(5);
    CHECK_EQ(writes, 0u);
    polls(6);
    CHECK_EQ(writes, 1u);
    CHECK_EQ(fk.cfg[LINK_TN_ENABLE], LINK_TN_EN_TAP);
    CHECK_EQ(fk.cfg[LINK_TN_PIN], LINK_TN_DEFAULT_PIN);
    CHECK(tone_link_settled(&tl));

    /* Registers 8 to 23 once per 50 ms poll, which is 20 Hz. */
    const unsigned before = reads;
    polls(20);
    CHECK_EQ(reads - before, 20u);
}

TEST_CASE(a_held_button_writes_the_set_up_once)
{
    fresh(8u);
    tone_setup_t w = setup_default();
    w.enable = true;
    polls(2);
    /* A value stepped every 33 ms for a second. */
    for (int i = 0; i < 30; ++i) {
        w.f_min_hz = (uint16_t)(400u + 10u * (unsigned)i);
        want(&w);
        drain();
        now += 33u;
    }
    CHECK_EQ(writes, 0u);
    polls(20);
    CHECK_EQ(fk.frames_taken, 1u);
    CHECK_EQ(fk.cfg[LINK_TN_F_MIN_HZ], 690u);
}

TEST_CASE(disabling_the_tap_writes_it_and_stops_the_reads)
{
    fresh(8u);
    tap_on();
    tone_setup_t w = setup_default();
    want(&w);
    polls(14);
    CHECK_EQ(fk.cfg[LINK_TN_ENABLE], 0u);
    const unsigned before = reads;
    polls(20);
    CHECK_EQ(reads, before);
    tone_readout_t r;
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.state, TONE_STATE_OFF);
}

TEST_CASE(frames_go_in_the_order_that_leaves_a_valid_set_up)
{
    /* 400 Hz and 3 ms to 50 Hz and 20 ms: the range alone would leave 50 Hz
     * with a 3 ms gap, which the page refuses; the gap goes first. */
    fresh(8u);
    polls(2);
    tone_setup_t w = setup_default();
    w.f_min_hz = 50u;
    w.gap_ms = 20u;
    want(&w);
    polls(20);
    CHECK_EQ(fk.frames_taken, 2u);
    CHECK_EQ(fk.order[0], 4u);
    CHECK_EQ(fk.order[1], 0u);
    CHECK_EQ(fk.cfg[LINK_TN_F_MIN_HZ], 50u);
    CHECK_EQ(fk.cfg[LINK_TN_GAP_MS], 20u);
    CHECK_EQ(tone_link_event(&tl, now), 0u);

    /* And back: the range first, so the gap is never short of its tone. */
    w = setup_default();
    want(&w);
    polls(20);
    CHECK_EQ(fk.frames_taken, 4u);
    CHECK_EQ(fk.order[2], 0u);
    CHECK_EQ(fk.order[3], 4u);
    CHECK_EQ(fk.cfg[LINK_TN_F_MIN_HZ], 400u);
    CHECK_EQ(fk.cfg[LINK_TN_GAP_MS], 3u);
    CHECK_EQ(tone_link_event(&tl, now), 0u);
}

TEST_CASE(a_refused_frame_is_said_once_and_not_written_again)
{
    fresh(8u);
    polls(2);
    /* A high tone below the low one. */
    tone_setup_t w = setup_default();
    w.f_min_hz = 1000u;
    w.f_max_hz = 900u;
    want(&w);
    polls(20);
    CHECK_EQ(writes, 1u);
    CHECK_EQ(tone_link_event(&tl, now), TONE_LINK_EV_PIN_REFUSED);
    CHECK_EQ(fk.cfg[LINK_TN_F_MIN_HZ], 400u);
    CHECK(tone_link_settled(&tl));
    /* Not again, however long. */
    polls(100);
    CHECK_EQ(writes, 2u);                   /* the one retry of the first frame */
    CHECK_EQ(tone_link_event(&tl, now + 100000u), 0u);

    /* The next edit asks again, and a good value goes through. */
    w = setup_default();
    w.f_min_hz = 600u;
    want(&w);
    polls(20);
    CHECK_EQ(fk.cfg[LINK_TN_F_MIN_HZ], 600u);
}

TEST_CASE(a_refused_second_frame_is_its_own_event)
{
    fresh(8u);
    polls(2);
    /* 1 ms is under the period of 400 Hz. */
    tone_setup_t w = setup_default();
    w.gap_ms = 1u;
    want(&w);
    polls(20);
    CHECK_EQ(writes, 1u);
    CHECK_EQ(tone_link_event(&tl, now), TONE_LINK_EV_SETUP_REFUSED);
    polls(100);
    CHECK_EQ(writes, 1u);
    CHECK_EQ(fk.cfg[LINK_TN_GAP_MS], 3u);
    CHECK(tone_link_settled(&tl));
}

TEST_CASE(a_pin_held_by_something_else_goes_through_once_it_is_free)
{
    fresh(8u);
    fk.bad_pin = 22;
    polls(2);
    tone_setup_t w = setup_default();
    w.enable = true;
    want(&w);
    polls(20);
    CHECK_EQ(writes, 1u);
    CHECK_EQ(tone_link_event(&tl, now), TONE_LINK_EV_PIN_REFUSED);
    tone_readout_t r;
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.state, TONE_STATE_STOPPED);

    /* Offered again every 5 s, quietly. */
    polls(110);
    CHECK(writes >= 2u);
    CHECK_EQ(tone_link_event(&tl, now + 100000u), 0u);

    fk.bad_pin = -1;
    polls(110);
    CHECK_EQ(fk.cfg[LINK_TN_ENABLE], LINK_TN_EN_TAP);
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.state, TONE_STATE_RUNNING);
    CHECK_EQ(tone_link_event(&tl, now + 100000u), 0u);
}

TEST_CASE(an_unanswered_write_is_written_again)
{
    for (int landed = 0; landed < 2; ++landed) {
        fresh(8u);
        polls(2);
        tone_setup_t w = setup_default();
        w.enable = true;
        want(&w);
        /* Nothing is exchanged until the set-up has rested: the write is
         * the next exchange, and its reply is the one lost. */
        lose_next = true;
        land_lost = landed != 0;
        polls(14);
        CHECK_EQ(fk.cfg[LINK_TN_ENABLE], LINK_TN_EN_TAP);
        CHECK_EQ(fk.frames_taken, (unsigned)(landed ? 2 : 1));
        CHECK_EQ(writes, 2u);
        CHECK(tone_link_settled(&tl));
        CHECK_EQ(tone_link_event(&tl, now), 0u);
    }
}

TEST_CASE(a_lost_link_reads_the_page_again)
{
    fresh(8u);
    tap_on();
    const unsigned r0 = reads;
    tone_link_lost(&tl);
    polls(10);
    CHECK_EQ(reads, r0);                        /* nothing while down */
    tone_readout_t r;
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.state, TONE_STATE_WAITING);

    tone_link_came_up(&tl, 8u, now);
    /* The coprocessor keeps its set-up: the read finds it as wanted and
     * nothing is written. */
    const unsigned w0 = writes;
    polls(14);
    CHECK_EQ(writes, w0);
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.state, TONE_STATE_RUNNING);
}

/* ---------------------------------------------------------------- beeps */

static void check_beep(const tone_beep_t *b, uint16_t seq)
{
    if (b->seq != seq) {
        T_FAIL("beep %u read as %u", (unsigned)seq, (unsigned)b->seq);
        return;
    }
    CHECK_EQ(b->start_ms, 70000u + 100u * (uint32_t)seq);
    CHECK_EQ(b->len_dms, 1000u + seq % 1000u);
    CHECK_EQ(b->freq_dhz, 20000u + seq % 1000u);
    CHECK_EQ(b->bursts, seq % 7u);
    CHECK_EQ(b->carrier_hhz, (seq % 2u != 0u) ? 240u : 0u);
    CHECK_EQ(b->flags, seq % 4u);
}

TEST_CASE(beeps_are_read_one_by_one_by_number)
{
    fresh(8u);
    tap_on();
    CHECK_EQ(tone_link_read_count(&tl), 0u);
    fake_beeps(3);
    polls(4);
    CHECK_EQ(tone_link_read_count(&tl), 3u);
    CHECK_EQ(tone_link_missed(&tl), 0u);

    tone_beep_t b[8];
    CHECK_EQ(tone_link_beeps(&tl, b, 8), 3u);
    check_beep(&b[0], 3u);
    check_beep(&b[1], 2u);
    check_beep(&b[2], 1u);
    CHECK_EQ(tone_link_beeps(&tl, b, 2), 2u);

    /* Nothing new, nothing asked beyond the status. */
    const unsigned w0 = writes;
    polls(10);
    CHECK_EQ(writes, w0);
    CHECK_EQ(tone_link_read_count(&tl), 3u);
}

TEST_CASE(a_tap_that_starts_with_beeps_in_the_ring_reads_the_newest)
{
    fresh(8u);
    fake_head(20);
    tap_on();
    polls(4);
    CHECK_EQ(tone_link_read_count(&tl), 1u);
    CHECK_EQ(tone_link_missed(&tl), 0u);
    tone_beep_t b[2];
    CHECK_EQ(tone_link_beeps(&tl, b, 2), 1u);
    check_beep(&b[0], 20u);
}

TEST_CASE(the_numbering_runs_across_the_wrap_and_skips_zero)
{
    fresh(8u);
    fake_head(65532u);
    tap_on();
    fake_beeps(6);                              /* 65533, 65534, 65535, 1, 2, 3 */
    polls(10);
    tone_beep_t b[8];
    CHECK_EQ(tone_link_beeps(&tl, b, 8), 7u);
    const uint16_t want_seq[7] = { 3, 2, 1, 65535, 65534, 65533, 65532 };
    for (unsigned i = 0u; i < 7u; ++i) {
        check_beep(&b[i], want_seq[i]);
    }
    CHECK_EQ(tone_link_missed(&tl), 0u);
    CHECK_EQ(fk.sel, 3u);
}

TEST_CASE(a_beep_number_65535_is_taken_like_any_other)
{
    fresh(8u);
    fake_head(65534u);
    tap_on();
    fake_beeps(1);                              /* 65535 */
    polls(4);
    tone_beep_t b[2];
    CHECK_EQ(tone_link_beeps(&tl, b, 2), 2u);
    check_beep(&b[0], 65535u);
    check_beep(&b[1], 65534u);
    fake_beeps(1);                              /* 1 */
    polls(4);
    CHECK_EQ(tone_link_beeps(&tl, b, 1), 1u);
    check_beep(&b[0], 1u);
    CHECK_EQ(tone_link_missed(&tl), 0u);
}

TEST_CASE(a_lost_reply_loses_no_beep)
{
    /* Each exchange of a beep, lost in turn: the status, the select and the
     * read, with the select landing or not. */
    for (int which = 0; which < 6; ++which) {
        fresh(8u);
        tap_on();
        fake_beeps(2);
        /* Exchanges before the lost one go through; the count of them
         * names it: 0 the status, 1 the first select, 2 the first read. */
        int n = 0;
        for (int k = 0; k < 60; ++k) {
            tone_link_op_t op;
            if (!tone_link_next(&tl, now, &op)) {
                now += 50u;
                if (tone_link_read_count(&tl) >= 2u) {
                    break;
                }
                continue;
            }
            land_lost = (which % 2) != 0;
            lose_next = (n == which / 2);
            ++n;
            uint16_t regs[LINK_MAX_REGS];
            const int result = far_exchange(&op, regs);
            tone_link_done(&tl, result, op.write ? NULL : regs, now);
        }
        CHECK_EQ(tone_link_read_count(&tl), 2u);
        CHECK_EQ(tone_link_missed(&tl), 0u);
        tone_beep_t b[4];
        CHECK_EQ(tone_link_beeps(&tl, b, 4), 2u);
        check_beep(&b[0], 2u);
        check_beep(&b[1], 1u);
    }
}

TEST_CASE(a_panel_far_behind_skips_to_the_ring_and_counts_what_it_missed)
{
    fresh(8u);
    fake_head(10u);
    tap_on();                                   /* takes 10 */
    CHECK_EQ(tone_link_read_count(&tl), 1u);
    fake_beeps(100);                            /* 11 to 110 */
    polls(80);
    /* The ring holds 47 to 110: 64 beeps, 36 gone. */
    CHECK_EQ(tone_link_missed(&tl), 36u);
    CHECK_EQ(tone_link_read_count(&tl), 1u + 64u);
    tone_beep_t b[4];
    CHECK_EQ(tone_link_beeps(&tl, b, 4), 4u);
    check_beep(&b[0], 110u);
    check_beep(&b[3], 107u);

    /* Exactly the ring behind is not behind. */
    fake_beeps(64);
    polls(80);
    CHECK_EQ(tone_link_missed(&tl), 36u);
    CHECK_EQ(tone_link_read_count(&tl), 1u + 64u + 64u);
}

TEST_CASE(one_beep_past_the_ring_is_one_missed)
{
    fresh(8u);
    fake_head(10u);
    tap_on();
    fake_beeps(65);
    polls(80);
    CHECK_EQ(tone_link_missed(&tl), 1u);
    CHECK_EQ(tone_link_read_count(&tl), 1u + 64u);
}

TEST_CASE(a_beep_that_leaves_the_ring_between_the_status_and_the_read_is_counted)
{
    fresh(8u);
    fake_head(10u);
    tap_on();
    const uint32_t taken = tone_link_read_count(&tl);
    fake_beeps(3);                              /* 11 to 13 */
    /* The status read ... */
    tone_link_op_t op;
    uint16_t regs[LINK_MAX_REGS];
    now += 50u;
    CHECK(tone_link_next(&tl, now, &op));
    CHECK_EQ(op.kind, TONE_LINK_OP_STATUS);
    int result = far_exchange(&op, regs);
    tone_link_done(&tl, result, regs, now);
    /* ... and 70 beeps before the first of its three is asked for. */
    fake_beeps(70);
    drain();
    polls(80);
    /* Every beep after 10 is either read or counted: none is neither. */
    const uint32_t read = tone_link_read_count(&tl) - taken;
    CHECK_EQ(read + tone_link_missed(&tl), 73u);
    CHECK(tone_link_missed(&tl) >= 9u);
    tone_beep_t b[1];
    CHECK_EQ(tone_link_beeps(&tl, b, 1), 1u);
    check_beep(&b[0], 83u);
}

TEST_CASE(every_beep_is_read_or_counted_whatever_the_pace)
{
    fresh(8u);
    tap_on();
    uint32_t added = 0u;
    uint32_t seed = 12345u;
    for (int step = 0; step < 400; ++step) {
        seed = seed * 1103515245u + 12345u;
        const unsigned burst = ((seed >> 16) % 11u == 0u)
                                   ? 70u + (seed >> 20) % 40u
                                   : (seed >> 16) % 4u;
        fake_beeps(burst);
        added += burst;
        /* A reply lost now and then. */
        lose_next = ((seed >> 8) % 9u) == 0u;
        poll_once();
    }
    lose_next = false;
    polls(120);
    CHECK_EQ(tone_link_read_count(&tl) + tone_link_missed(&tl), added);
    CHECK(tone_link_read_count(&tl) > 0u);
    CHECK(tone_link_missed(&tl) > 0u);
}

TEST_CASE(a_restarted_numbering_is_not_a_miss)
{
    fresh(8u);
    fake_head(200u);
    tap_on();
    fake_beeps(3);
    polls(6);
    CHECK_EQ(tone_link_missed(&tl), 0u);
    /* Someone disabled and enabled the tap: the numbering began again. */
    memset(fk.ring, 0, sizeof(fk.ring));
    fk.head = 0u;
    polls(4);
    fake_beeps(2);
    polls(6);
    CHECK_EQ(tone_link_missed(&tl), 0u);
    tone_beep_t b[2];
    CHECK_EQ(tone_link_beeps(&tl, b, 2), 2u);
    check_beep(&b[0], 2u);
    check_beep(&b[1], 1u);

}

TEST_CASE(a_restart_the_panel_never_saw_is_not_a_miss)
{
    fresh(8u);
    fake_head(40000u);
    tap_on();
    fake_beeps(3);
    polls(6);
    CHECK_EQ(tone_link_read_count(&tl), 4u);
    /* The numbering began again, and 5 beeps came before the panel looked:
     * the head is behind it by 25 thousand, which is not a count of
     * anything. */
    memset(fk.ring, 0, sizeof(fk.ring));
    fk.head = 0u;
    fake_beeps(5);
    polls(6);
    CHECK_EQ(tone_link_missed(&tl), 0u);
    tone_beep_t b[1];
    CHECK_EQ(tone_link_beeps(&tl, b, 1), 1u);
    check_beep(&b[0], 5u);
    CHECK_EQ(tone_link_read_count(&tl), 5u);
}

TEST_CASE(the_history_keeps_the_newest_eight)
{
    fresh(8u);
    tap_on();
    for (int i = 0; i < 20; ++i) {
        fake_beeps(1);
        polls(2);
    }
    tone_beep_t b[TONE_LINK_HISTORY + 4];
    CHECK_EQ(tone_link_beeps(&tl, b, TONE_LINK_HISTORY + 4),
             (unsigned)TONE_LINK_HISTORY);
    for (unsigned i = 0u; i < TONE_LINK_HISTORY; ++i) {
        check_beep(&b[i], (uint16_t)(20u - i));
    }
    tone_readout_t r;
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.n, TONE_LINK_SHOWN);
    check_beep(&r.beeps[0], 20u);
    check_beep(&r.beeps[TONE_LINK_SHOWN - 1u], 17u);
}

TEST_CASE(a_beep_costs_a_select_and_a_read)
{
    fresh(8u);
    tap_on();
    const unsigned w0 = writes, r0 = reads;
    fake_beeps(1);
    polls(1);
    /* The status, the select, the read. */
    CHECK_EQ(writes - w0, 1u);
    CHECK_EQ(reads - r0, 2u);
}

/* ---------------------------------------------------------------- flags */

TEST_CASE(the_readout_follows_the_flags)
{
    fresh(8u);
    tone_readout_t r;
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.state, TONE_STATE_OFF);
    CHECK_EQ(r.n, 0u);

    tone_setup_t w = setup_default();
    w.enable = true;
    want(&w);
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.state, TONE_STATE_WAITING);      /* nothing read yet */

    polls(14);
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.state, TONE_STATE_RUNNING);
    CHECK(!r.tone);
    CHECK(!r.beep);
    CHECK(!r.overrun);
    CHECK_EQ(r.win_freq_dhz, 0u);

    fk.flags |= LINK_TN_TONE | LINK_TN_BEEP;
    fk.win_freq = 15234u;
    fk.win_periods = 12u;
    fk.lost = 3u;
    fk.glitches = 65535u;
    polls(2);
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.state, TONE_STATE_RUNNING);
    CHECK(r.tone);
    CHECK(r.beep);
    CHECK_EQ(r.win_freq_dhz, 15234u);
    CHECK_EQ(r.win_periods, 12u);
    CHECK_EQ(r.lost, 3u);
    CHECK_EQ(r.glitches, 65535u);
    CHECK_EQ(tone_link_flags(&tl) & LINK_TN_TONE, LINK_TN_TONE);

    /* The counters wrap in 16 bits. */
    fk.lost = 0u;
    polls(2);
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.lost, 0u);

    /* The pin is not free: the capture does not run. */
    fk.flags = LINK_TN_PIN_REFUSED;
    polls(2);
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.state, TONE_STATE_REFUSED);

    /* Enabled, neither running nor refused. */
    fk.flags = 0u;
    polls(2);
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.state, TONE_STATE_STOPPED);

    fk.flags = LINK_TN_RUNNING | LINK_TN_OVERRUN;
    polls(2);
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.state, TONE_STATE_RUNNING);
    CHECK(r.overrun);
}

TEST_CASE(a_status_that_stops_coming_is_no_longer_running)
{
    fresh(8u);
    tap_on();
    tone_readout_t r;
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.state, TONE_STATE_RUNNING);
    tone_link_readout(&tl, now + TONE_LINK_STALE_MS + 100u, &r);
    CHECK_EQ(r.state, TONE_STATE_WAITING);
}

TEST_CASE(a_pin_not_free_and_an_overrun_are_said_on_their_edges)
{
    fresh(8u);
    tap_on();
    CHECK_EQ(tone_link_event(&tl, now), 0u);

    fk.flags = LINK_TN_PIN_REFUSED;
    polls(3);
    CHECK_EQ(tone_link_event(&tl, now), TONE_LINK_EV_PIN_BUSY);
    polls(10);
    CHECK_EQ(tone_link_event(&tl, now + 100000u), 0u);   /* once */

    fk.flags = LINK_TN_RUNNING | LINK_TN_OVERRUN;
    polls(3);
    CHECK_EQ(tone_link_event(&tl, now + 100000u), TONE_LINK_EV_OVERRUN);
    polls(10);
    CHECK_EQ(tone_link_event(&tl, now + 200000u), 0u);

    /* Cleared and set again: said again. */
    fk.flags = LINK_TN_RUNNING;
    polls(3);
    fk.flags = LINK_TN_RUNNING | LINK_TN_OVERRUN;
    polls(3);
    CHECK_EQ(tone_link_event(&tl, now + 300000u), TONE_LINK_EV_OVERRUN);
}

TEST_CASE(events_are_handed_out_one_at_a_time_and_can_be_put_back)
{
    fresh(8u);
    tap_on();
    fk.flags = LINK_TN_RUNNING | LINK_TN_PIN_REFUSED | LINK_TN_OVERRUN;
    polls(3);
    const uint32_t t0 = now;
    /* The most pressing first. */
    CHECK_EQ(tone_link_event(&tl, t0), TONE_LINK_EV_PIN_BUSY);
    CHECK_EQ(tone_link_event(&tl, t0 + 100u), 0u);
    CHECK_EQ(tone_link_event(&tl, t0 + TONE_LINK_EVENT_GAP_MS),
             TONE_LINK_EV_OVERRUN);

    /* One the band never showed comes again after the gap. */
    tone_link_event_back(&tl, TONE_LINK_EV_OVERRUN);
    CHECK_EQ(tone_link_event(&tl, t0 + TONE_LINK_EVENT_GAP_MS + 10u), 0u);
    CHECK_EQ(tone_link_event(&tl, t0 + 3u * TONE_LINK_EVENT_GAP_MS),
             TONE_LINK_EV_OVERRUN);

    /* Not for a coprocessor that has gone. */
    tone_link_lost(&tl);
    tone_link_event_back(&tl, TONE_LINK_EV_OVERRUN);
    CHECK_EQ(tone_link_event(&tl, t0 + 10u * TONE_LINK_EVENT_GAP_MS), 0u);
}

TEST_CASE(a_new_link_forgets_what_the_last_one_said)
{
    fresh(8u);
    tap_on();
    fk.flags = LINK_TN_RUNNING | LINK_TN_OVERRUN;
    polls(3);
    tone_link_lost(&tl);
    CHECK_EQ(tone_link_event(&tl, now + 100000u), 0u);
    tone_link_came_up(&tl, 8u, now);
    polls(14);
    /* Still overrun on the coprocessor: news again. */
    CHECK_EQ(tone_link_event(&tl, now + 100000u), TONE_LINK_EV_OVERRUN);
}

TEST_CASE(the_history_stays_across_a_lost_link)
{
    fresh(8u);
    tap_on();
    fake_beeps(2);
    polls(4);
    tone_link_lost(&tl);
    tone_beep_t b[2];
    CHECK_EQ(tone_link_beeps(&tl, b, 2), 2u);
    tone_readout_t r;
    tone_link_readout(&tl, now, &r);
    CHECK_EQ(r.n, 2u);
    CHECK_EQ(r.state, TONE_STATE_WAITING);
}

TEST_CASE(null_arguments_are_taken)
{
    tone_link_init(NULL);
    tone_link_lost(NULL);
    tone_link_came_up(NULL, 8u, 0u);
    tone_link_want(NULL, NULL, 0u);
    tone_link_done(NULL, 0, NULL, 0u);
    tone_link_event_back(NULL, 1u);
    tone_link_op_t op;
    CHECK(!tone_link_next(NULL, 0u, &op));
    CHECK_EQ(tone_link_event(NULL, 0u), 0u);
    CHECK(!tone_link_settled(NULL));
    CHECK_EQ(tone_link_beeps(NULL, NULL, 1), 0u);
    CHECK_EQ(tone_link_read_count(NULL), 0u);
    CHECK_EQ(tone_link_missed(NULL), 0u);
    CHECK_EQ(tone_link_flags(NULL), 0u);
    CHECK_EQ(tone_link_pin(NULL), 0u);
    CHECK_EQ(tone_link_f_min(NULL), 0u);
    CHECK_EQ(tone_link_f_max(NULL), 0u);
    tone_readout_t r;
    tone_link_readout(NULL, 0u, &r);
    CHECK_EQ(r.state, TONE_STATE_OFF);
    tone_link_readout(NULL, 0u, NULL);

    fresh(8u);
    CHECK(!tone_link_next(&tl, 0u, NULL));
    tone_setup_t w = setup_default();
    tone_link_want(&tl, NULL, 0u);
    want(&w);
    CHECK_EQ(tone_link_pin(&tl), (unsigned)LINK_TN_DEFAULT_PIN);
    CHECK_EQ(tone_link_f_min(&tl), (unsigned)LINK_TN_DEFAULT_F_MIN);
    CHECK_EQ(tone_link_f_max(&tl), (unsigned)LINK_TN_DEFAULT_F_MAX);
    /* An answer with nothing pending is ignored. */
    tone_link_done(&tl, TONE_LINK_ACK, NULL, now);
    /* A reply to a read that carries no registers is a refusal. */
    CHECK(tone_link_next(&tl, now, &op));
    tone_link_done(&tl, TONE_LINK_ACK, NULL, now);
    CHECK(!tone_link_next(&tl, now, &op));
}

int main(void)
{
    RUN(nothing_is_sent_to_a_4_7_coprocessor);
    RUN(a_tap_enabled_before_the_link_is_said_at_link_up);
    RUN(a_coprocessor_that_names_4_8_and_refuses_the_page_is_left_alone);
    RUN(a_4_8_coprocessor_is_read_and_written);
    RUN(the_page_is_read_first_and_only_what_differs_is_written);
    RUN(a_zeroed_setup_writes_nothing);
    RUN(enabling_the_tap_writes_the_first_frame_and_polls_the_status);
    RUN(a_held_button_writes_the_set_up_once);
    RUN(disabling_the_tap_writes_it_and_stops_the_reads);
    RUN(frames_go_in_the_order_that_leaves_a_valid_set_up);
    RUN(a_refused_frame_is_said_once_and_not_written_again);
    RUN(a_refused_second_frame_is_its_own_event);
    RUN(a_pin_held_by_something_else_goes_through_once_it_is_free);
    RUN(an_unanswered_write_is_written_again);
    RUN(a_lost_link_reads_the_page_again);
    RUN(beeps_are_read_one_by_one_by_number);
    RUN(a_tap_that_starts_with_beeps_in_the_ring_reads_the_newest);
    RUN(the_numbering_runs_across_the_wrap_and_skips_zero);
    RUN(a_beep_number_65535_is_taken_like_any_other);
    RUN(a_lost_reply_loses_no_beep);
    RUN(a_panel_far_behind_skips_to_the_ring_and_counts_what_it_missed);
    RUN(one_beep_past_the_ring_is_one_missed);
    RUN(a_beep_that_leaves_the_ring_between_the_status_and_the_read_is_counted);
    RUN(every_beep_is_read_or_counted_whatever_the_pace);
    RUN(a_restarted_numbering_is_not_a_miss);
    RUN(a_restart_the_panel_never_saw_is_not_a_miss);
    RUN(the_history_keeps_the_newest_eight);
    RUN(a_beep_costs_a_select_and_a_read);
    RUN(the_readout_follows_the_flags);
    RUN(a_status_that_stops_coming_is_no_longer_running);
    RUN(a_pin_not_free_and_an_overrun_are_said_on_their_edges);
    RUN(events_are_handed_out_one_at_a_time_and_can_be_put_back);
    RUN(a_new_link_forgets_what_the_last_one_said);
    RUN(the_history_stays_across_a_lost_link);
    RUN(null_arguments_are_taken);
    return test_summary("tone_link");
}
