/*
 * The PD mini's codec and driver, against a modelled module.
 *
 * Under test: the CRC against every value the vendor's sheet prints; reply
 * framing; a driver that identifies the module before saying anything to
 * it, keeps the pins at rest between transactions, confirms every write by
 * reading it back, learns which OUTPUT_EN argument means on, never switches
 * on an output an OFF was asked for, and notices a module that goes quiet.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "pdmini.h"

/* ------------------------------------------------------------- the module */

typedef struct {
    bool     powered;
    bool     attached;        /* the driver's UART on the pins           */
    uint8_t  on_arg;          /* OUTPUT_EN's argument for on             */
    bool     output;
    bool     pending;         /* an OUTPUT_EN not yet in effect          */
    bool     target;
    uint32_t settle_ms, en_at;
    int      slot;
    uint16_t mv[5], ma[5];
    uint16_t v_mv, i_ma;
    bool     input_ok;        /* PD negotiated: the output can switch on */
    bool     bad_crc;         /* answer every read with a broken CRC     */
    bool     whoami_crc;      /* end WHO_AM_I with a CRC, not 0x0A       */
    uint8_t  junk;            /* a byte the handover leaves, 0 for none  */

    /* The reply on its way: one byte a millisecond. */
    uint8_t  out[80];
    size_t   out_n, out_i;
    uint32_t out_at;

    unsigned en_writes, sends, sends_detached;
    bool     switched_on_by_off;   /* an OFF request turned it on      */
    unsigned reads[256];      /* requests by command                     */
    unsigned data_writes;
    bool     ignore_data;     /* OUTPUT_DATA taken and not applied       */
    uint32_t display_delay;   /* extra ms before a display reply         */
    uint16_t on_at_mv;        /* the active slot's mV when it came on    */
    bool     mute;            /* takes writes, answers nothing           */
    uint32_t on_since;        /* when the output last came on            */
    int      id_says;         /* READ_ID answers this, -1 the slot       */
    bool     other_slot;      /* READ_DATA answers for the next slot     */
    bool     no_input;        /* firmware without READ_INPUT_STATE       */
    bool     no_state;        /* READ_OUTPUT_STATE unanswered             */
    const char *who;          /* WHO_AM_I's text, NULL the module's      */
    unsigned ignore_en;       /* OUTPUT_EN writes taken and not applied  */
    uint32_t byte_gap;        /* ms between reply bytes, 0 for 1         */
    uint32_t self_off_ms;     /* goes off by itself this long after any
                                 OUTPUT_EN that left it on, 0 never      */
    uint32_t self_on_ms;      /* comes on by itself this long after an
                                 OUTPUT_EN, its AUTO OUT or button; 0 never */
    uint32_t last_en_ms;
    uint32_t attach_at, attach_max;
    int      switch_to;       /* the buttons choose this slot, -1 none,  */
    uint16_t switch_mv;       /* once a READ_DATA answers with this      */
    uint32_t on_ms;           /* how long it was on, all told            */
} module_t;

static module_t m;
static pdmini_t d;
static uint32_t now;

static void m_attach(void *ctx)
{
    (void)ctx;
    m.attached  = true;
    m.attach_at = now;
    if (m.junk != 0u && m.powered) {
        m.out[0] = m.junk;        /* a framing error from the handover */
        m.out_n = 1u;
        m.out_i = 0u;
        m.out_at = now;
    }
}

static void m_detach(void *ctx)
{
    (void)ctx;
    if (m.attached && (uint32_t)(now - m.attach_at) > m.attach_max) {
        m.attach_max = (uint32_t)(now - m.attach_at);
    }
    m.attached = false;
    m.out_n = m.out_i = 0u;       /* whatever was on its way is lost */
}

static void reply(const uint8_t *p, size_t n, bool crc)
{
    memcpy(m.out, p, n);
    m.out[n] = crc ? pdmini_crc8(p, n) : 0x0Au;
    if (m.bad_crc && crc) {
        m.out[n] ^= 0x5Au;
    }
    m.out_n = n + 1u;
    m.out_i = 0u;
    m.out_at = now + 2u;
}

static void m_send(void *ctx, const uint8_t *p, size_t n)
{
    (void)ctx;
    ++m.sends;
    if (!m.attached) {
        ++m.sends_detached;
        return;
    }
    if (!m.powered || n < 2u || pdmini_crc8(p, n - 1u) != p[n - 1u]) {
        return;
    }
    ++m.reads[p[0]];
    if (m.no_state && p[0] == PDMINI_READ_STATE) {
        return;
    }
    if (m.no_input && p[0] == PDMINI_READ_INPUT) {
        return;
    }
    if (m.mute && p[0] != PDMINI_OUTPUT_EN && p[0] != PDMINI_OUTPUT_DATA) {
        return;   /* its transmit line is gone, its receiver is not */
    }
    switch (p[0]) {
    case PDMINI_WHO_AM_I: {
        const char *who = (m.who != NULL) ? m.who
                                          : "WeAct Studio PD Power Mini V1 BUCK";
        const size_t len = strlen(who);
        uint8_t r[64];
        r[0] = PDMINI_WHO_AM_I;
        r[1] = (uint8_t)len;
        memcpy(&r[2], who, len);
        reply(r, 2u + len, m.whoami_crc);
        break;
    }
    case PDMINI_READ_STATE: {
        const uint8_t r[2] = { PDMINI_READ_STATE, (uint8_t)(m.output ? 1u : 0u) };
        reply(r, 2u, true);
        break;
    }
    case PDMINI_READ_ID: {
        const uint8_t r[2] = { PDMINI_READ_ID,
                               (uint8_t)(m.id_says >= 0 ? m.id_says : m.slot) };
        reply(r, 2u, true);
        break;
    }
    case PDMINI_READ_DATA: {
        const uint8_t s = p[1];
        const uint8_t r[6] = { PDMINI_READ_DATA,
                               (uint8_t)(m.other_slot ? (s + 1u) % 5u : s),
                               (uint8_t)(m.mv[s] & 0xFFu), (uint8_t)(m.mv[s] >> 8),
                               (uint8_t)(m.ma[s] & 0xFFu), (uint8_t)(m.ma[s] >> 8) };
        reply(r, 6u, true);
        if (m.switch_to >= 0 && m.mv[s] == m.switch_mv) {
            m.slot      = m.switch_to;
            m.switch_to = -1;
        }
        break;
    }
    case PDMINI_READ_DISPLAY: {
        const uint8_t r[5] = { PDMINI_READ_DISPLAY,
                               (uint8_t)(m.v_mv & 0xFFu), (uint8_t)(m.v_mv >> 8),
                               (uint8_t)(m.i_ma & 0xFFu), (uint8_t)(m.i_ma >> 8) };
        reply(r, 5u, true);
        m.out_at += m.display_delay;
        break;
    }
    case PDMINI_READ_INPUT: {
        const uint8_t r[6] = { PDMINI_READ_INPUT, m.input_ok ? 5u : 1u,
                               0xC0u, 0x5Du, 200u, 0u };   /* 24000 mV, 20.0 V */
        reply(r, 6u, true);
        break;
    }
    case PDMINI_OUTPUT_EN: {
        ++m.en_writes;
        m.last_en_ms = now;
        if (m.ignore_en > 0u) {
            --m.ignore_en;
            break;
        }
        /* The later write wins; one for what is already on its way
         * changes nothing. */
        const bool target = (p[1] == m.on_arg) && m.input_ok;
        if (!m.pending || target != m.target) {
            m.pending = true;
            m.target  = target;
            m.en_at   = now;
        }
        break;
    }
    case PDMINI_OUTPUT_DATA:
        ++m.data_writes;
        if (p[1] <= 4u && !m.ignore_data) {
            m.mv[p[1]] = (uint16_t)(p[2] | (p[3] << 8));
            m.ma[p[1]] = (uint16_t)(p[4] | (p[5] << 8));
        }
        break;
    default:
        break;
    }
}

static const pdmini_io_t k_io = { m_attach, m_detach, m_send, NULL };

static void fresh(void)
{
    memset(&m, 0, sizeof(m));
    m.powered  = true;
    m.on_arg   = 1u;
    m.input_ok = true;
    m.slot     = 0;
    m.mv[0] = 5000u;
    m.ma[0] = 1000u;
    m.switch_to = -1;
    m.id_says   = -1;
    now = 1000u;
    pdmini_init(&d, &k_io, now);
}

/* @p ms milliseconds: the module settles and talks, the driver steps. */
static void run(uint32_t ms, bool want_off_seen_on_check)
{
    for (uint32_t k = 0; k < ms; ++k) {
        ++now;
        if (m.pending && (uint32_t)(now - m.en_at) >= m.settle_ms) {
            m.pending = false;
            if (want_off_seen_on_check && !m.output && m.target) {
                m.switched_on_by_off = true;
            }
            if (!m.output && m.target) {
                m.on_at_mv = m.mv[m.slot];
            }
            m.output = m.target;
        }
        if (m.self_off_ms != 0u && m.output && m.en_writes > 0u
            && (uint32_t)(now - m.last_en_ms) == m.self_off_ms) {
            m.output = false;                  /* its overcurrent protection */
        }
        if (m.self_on_ms != 0u && !m.output && m.en_writes > 0u
            && (uint32_t)(now - m.last_en_ms) == m.self_on_ms) {
            m.output = true;
            m.self_on_ms = 0u;
        }
        if (m.output) {
            ++m.on_ms;
        }
        if (m.attached && m.out_i < m.out_n
            && (int32_t)(now - m.out_at) >= 0) {
            pdmini_rx(&d, m.out[m.out_i++], now);
            m.out_at = now + ((m.byte_gap != 0u) ? m.byte_gap : 1u);
        }
        pdmini_step(&d, now);
    }
}

/* ON, OFF and ON again: the argument for on shown twice and relied on,
 * the output left on. */
static void learn_on_twice(void)
{
    pdmini_want(&d, true, 5000u, 1000u);
    run(1500u, false);
    pdmini_want(&d, false, 5000u, 1000u);
    run(600u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    run(1500u, false);
}

/* -------------------------------------------------------------- the codec */

TEST_CASE(the_crc_matches_every_value_the_sheet_prints)
{
    static const uint8_t req[15] = { 0x81, 0x82, 0x83, 0x85, 0x86, 0x87, 0x88,
                                     0x8A, 0xC2, 0xC3, 0xC6, 0xC7, 0x40, 0x44,
                                     0x45 };
    static const uint8_t crc[15] = { 0xE7, 0xB4, 0x85, 0x23, 0x70, 0x41, 0x6F,
                                     0x0D, 0x89, 0xB8, 0x4D, 0x7C, 0x91, 0x55,
                                     0x64 };
    for (unsigned i = 0; i < 15u; ++i) {
        CHECK_EQ(pdmini_crc8(&req[i], 1u), crc[i]);
    }
}

TEST_CASE(a_reply_is_whole_only_with_its_length_and_its_check)
{
    uint8_t r[8] = { PDMINI_READ_DISPLAY, 0xB0, 0x36, 0xE8, 0x03, 0 };
    r[5] = pdmini_crc8(r, 5u);
    CHECK_EQ(pdmini_reply_frame(PDMINI_READ_DISPLAY, r, 0u), PDMINI_MORE);
    CHECK_EQ(pdmini_reply_frame(PDMINI_READ_DISPLAY, r, 5u), PDMINI_MORE);
    CHECK_EQ(pdmini_reply_frame(PDMINI_READ_DISPLAY, r, 6u), PDMINI_DONE);
    CHECK_EQ(pdmini_reply_frame(PDMINI_READ_STATE, r, 1u), PDMINI_BAD);
    r[5] ^= 1u;
    CHECK_EQ(pdmini_reply_frame(PDMINI_READ_DISPLAY, r, 6u), PDMINI_BAD);
    CHECK_EQ(pdmini_reply_frame(0x47, r, 1u), PDMINI_BAD);

    /* WHO_AM_I: command, length, text, and 0x0A or a CRC. */
    uint8_t w[8] = { PDMINI_WHO_AM_I, 3u, 'a', 'b', 'c', 0x0Au };
    CHECK_EQ(pdmini_reply_frame(PDMINI_WHO_AM_I, w, 1u), PDMINI_MORE);
    CHECK_EQ(pdmini_reply_frame(PDMINI_WHO_AM_I, w, 5u), PDMINI_MORE);
    CHECK_EQ(pdmini_reply_frame(PDMINI_WHO_AM_I, w, 6u), PDMINI_DONE);
    w[5] = pdmini_crc8(w, 5u);
    CHECK_EQ(pdmini_reply_frame(PDMINI_WHO_AM_I, w, 6u), PDMINI_DONE);
    w[5] = 0x0Bu;
    CHECK_EQ(pdmini_reply_frame(PDMINI_WHO_AM_I, w, 6u), PDMINI_BAD);
}

/* ------------------------------------------------------------- the driver */

/* Nothing is said to the module before it has said who it is, the pins are
 * at rest between transactions, and the readings come in. */
TEST_CASE(the_module_is_identified_then_read_with_the_pins_at_rest_between)
{
    fresh();
    m.v_mv = 12050u;
    m.i_ma = 340u;
    run(50u, false);
    CHECK(pdmini_status(&d)->online);
    run(1000u, false);
    const pdmini_status_t *st = pdmini_status(&d);
    CHECK_EQ(st->v_mv, 12050u);
    CHECK_EQ(st->i_ma, 340u);
    CHECK_EQ(st->in_state, 5u);
    CHECK_EQ(st->vin_mv, 24000u);
    CHECK(st->samples >= 8u);
    CHECK_EQ(st->errors, 0u);
    CHECK_EQ(m.sends_detached, 0u);
    CHECK_EQ(m.en_writes, 0u);                 /* nothing asked, nothing written */
    /* Between transactions the pins are at rest. */
    unsigned rest = 0u;
    for (int k = 0; k < 200; ++k) {
        run(1u, false);
        rest += (d.phase == PD_GAP || d.phase == PD_IDLE) && !m.attached;
    }
    CHECK(rest > 0u);
}

/* ON and the set points: written into the active slot, each read back. */
TEST_CASE(an_output_and_its_set_points_are_written_and_read_back)
{
    fresh();
    m.slot = 2;
    run(100u, false);
    pdmini_want(&d, true, 12000u, 1500u);
    run(1500u, false);
    const pdmini_status_t *st = pdmini_status(&d);
    CHECK(m.output);
    CHECK(st->output);
    CHECK_EQ(m.mv[2], 12000u);
    CHECK_EQ(m.ma[2], 1500u);
    CHECK_EQ(st->set_mv, 12000u);
    CHECK_EQ(st->set_ma, 1500u);
    CHECK_EQ(m.en_writes, 1u);
    CHECK(!st->stuck);

    pdmini_want(&d, false, 12000u, 1500u);
    run(600u, false);
    CHECK(!m.output);
    CHECK(!pdmini_status(&d)->output);
    CHECK_EQ(m.en_writes, 2u);
}

/* Set points outside the module's range are clamped to it. */
TEST_CASE(set_points_are_clamped_to_the_module)
{
    fresh();
    run(100u, false);
    pdmini_want(&d, false, 25000u, 10u);
    run(1000u, false);
    CHECK_EQ(m.mv[0], PDMINI_V_MAX_MV);
    CHECK_EQ(m.ma[0], PDMINI_I_MIN_MA);
}

/*
 * A module that reads OUTPUT_EN the way the sheet says: ON takes the other
 * argument after two that do not take, and is remembered, so the next OFF
 * is right first time.
 */
TEST_CASE(the_on_argument_is_learnt_from_the_module)
{
    fresh();
    m.on_arg = 0u;
    run(100u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    run(2000u, false);
    CHECK(m.output);
    CHECK_EQ(m.en_writes, 3u);                 /* 1, 1, then 0 */
    CHECK_EQ(d.on_value, 0u);
    pdmini_want(&d, false, 5000u, 1000u);
    run(600u, false);
    CHECK(!m.output);
    CHECK_EQ(m.en_writes, 4u);
}

/* An OFF writes nothing to an output that is already off, and cannot switch
 * on one that was off, whichever way the module reads the argument. */
TEST_CASE(an_off_never_switches_an_output_on)
{
    for (uint8_t arg = 0u; arg <= 1u; ++arg) {
        fresh();
        m.on_arg = arg;
        run(100u, false);
        pdmini_want(&d, false, 5000u, 1000u);
        run(2000u, true);
        CHECK(!m.output);
        CHECK_EQ(m.en_writes, 0u);
        CHECK(!m.switched_on_by_off);

        /* And one that is on goes off, the inverted module included. */
        fresh();
        m.on_arg = arg;
        m.output = true;
        run(100u, false);
        pdmini_want(&d, false, 5000u, 1000u);
        run(2000u, true);
        CHECK(!m.output);
        CHECK(!m.switched_on_by_off);
    }
}

/* The confirming read waits until the output has settled; a write that takes
 * 200 ms to show is confirmed with one write, not taken for a failure. */
TEST_CASE(the_confirming_read_waits_for_the_output_to_settle)
{
    fresh();
    m.settle_ms = 200u;
    run(100u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    run(1500u, false);
    CHECK(m.output);
    CHECK_EQ(m.en_writes, 1u);
}

/* An input that is not ready will not switch on: four writes and the output
 * is stuck; once the input is ready it is tried again. */
TEST_CASE(an_on_that_does_not_take_is_stuck_and_tried_again)
{
    fresh();
    m.input_ok = false;
    run(100u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    run(2000u, false);
    CHECK(pdmini_status(&d)->stuck);
    CHECK_EQ(m.en_writes, 4u);
    m.input_ok = true;
    run(3000u, false);
    CHECK(m.output);
    CHECK(!pdmini_status(&d)->stuck);
}

/* A module that is not there is asked who it is, once a second, and nothing
 * else; one that goes quiet is noticed after three failures and asked
 * again; one that answers garbage counts its errors. */
TEST_CASE(a_module_that_is_not_there_or_goes_quiet_is_noticed)
{
    fresh();
    m.powered = false;
    pdmini_want(&d, true, 5000u, 1000u);
    run(3500u, false);
    CHECK(!pdmini_status(&d)->online);
    CHECK_EQ(m.en_writes, 0u);
    CHECK(m.sends >= 3u && m.sends <= 5u);

    m.powered = true;
    run(1500u, false);
    CHECK(pdmini_status(&d)->online);

    m.powered = false;
    run(2000u, false);
    CHECK(!pdmini_status(&d)->online);
    CHECK(pdmini_status(&d)->errors >= 3u);

    fresh();
    m.bad_crc = true;
    m.whoami_crc = true;
    run(3000u, false);
    CHECK(!pdmini_status(&d)->online);
    CHECK(pdmini_status(&d)->errors >= 2u);
}

/* An ON waits for the set points: the output never comes on at what the
 * active slot held before. */
TEST_CASE(an_on_waits_for_the_set_points)
{
    fresh();
    m.mv[0] = 5000u;
    run(100u, false);
    pdmini_want(&d, true, 12000u, 1500u);
    run(1500u, false);
    CHECK(m.output);
    CHECK_EQ(m.on_at_mv, 12000u);
}

/* A module slow to answer the display reads does not starve the state and
 * the input. */
TEST_CASE(slow_display_reads_do_not_starve_the_others)
{
    fresh();
    m.display_delay = 120u;
    run(4000u, false);
    CHECK(m.reads[PDMINI_READ_DISPLAY] >= 10u);
    CHECK(m.reads[PDMINI_READ_STATE] >= 4u);
    CHECK(m.reads[PDMINI_READ_INPUT] >= 4u);
}

/* The active slot and what is in it are read again: a change made on the
 * module's own buttons is put back to what is asked. */
TEST_CASE(the_slot_is_read_again_and_put_back)
{
    fresh();
    run(100u, false);
    pdmini_want(&d, false, 9000u, 700u);
    run(1000u, false);
    CHECK_EQ(m.mv[0], 9000u);
    m.mv[0] = 3300u;                           /* turned on the module */
    run(2000u, false);
    CHECK_EQ(m.mv[0], 9000u);
    m.slot = 3;                                /* another slot chosen */
    run(2500u, false);
    CHECK_EQ(m.mv[3], 9000u);
    CHECK_EQ(m.ma[3], 700u);
}

/* Set points the module will not take: three writes and they are stuck,
 * tried again two seconds on; the readings go on meanwhile, and the ON
 * that waits on them does not come. */
TEST_CASE(set_points_that_do_not_take_are_stuck_and_bounded)
{
    fresh();
    m.ignore_data = true;
    run(100u, false);
    pdmini_want(&d, true, 12000u, 1500u);
    run(1000u, false);
    CHECK(pdmini_status(&d)->set_stuck);
    CHECK_EQ(m.data_writes, 3u);
    const uint32_t samples = pdmini_status(&d)->samples;
    run(1000u, false);
    CHECK(pdmini_status(&d)->samples > samples + 5u);
    run(3000u, false);
    CHECK(m.data_writes >= 6u && m.data_writes <= 9u);
    CHECK(!m.output);
    CHECK_EQ(m.en_writes, 0u);
}

/* Stuck clears whenever the output is seen where it was asked, however it
 * got there. */
TEST_CASE(stuck_clears_when_the_output_is_seen_as_asked)
{
    fresh();
    m.input_ok = false;
    run(100u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    run(1500u, false);
    CHECK(pdmini_status(&d)->stuck);
    m.output = true;                           /* switched on at the module */
    run(800u, false);
    CHECK(!pdmini_status(&d)->stuck);
    CHECK(pdmini_status(&d)->output);
}

/* An OFF asked for while an ON waits to be confirmed does not wait out the
 * 250 ms: the state is read at once, and the output switched off the
 * moment it reads on. */
TEST_CASE(an_off_does_not_wait_behind_an_on_being_confirmed)
{
    fresh();
    m.settle_ms = 60u;
    run(100u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    unsigned guard = 0u;
    while (m.en_writes == 0u && guard++ < 2000u) {
        run(1u, false);
    }
    run(10u, false);
    pdmini_want(&d, false, 5000u, 1000u);
    m.on_ms = 0u;
    run(600u, false);
    CHECK(!m.output);
    /* On for its 60 ms to come on, a read or two, and its 60 ms to go off:
     * waiting out the confirmation would be 250 ms and the same again. */
    CHECK(m.on_ms < 150u);
    CHECK_EQ(m.en_writes, 2u);

    /* An ON that never comes is not answered with an OFF. */
    fresh();
    m.input_ok = false;
    run(100u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    guard = 0u;
    while (m.en_writes == 0u && guard++ < 2000u) {
        run(1u, false);
    }
    pdmini_want(&d, false, 5000u, 1000u);
    run(1000u, true);
    CHECK(!m.output);
    CHECK_EQ(m.en_writes, 1u);
}

/* A module whose replies stop while its output is on still gets an OFF:
 * sent blind while one is asked for, with the argument a read-back showed
 * to mean on -- and none goes to a module whose polarity was never shown. */
TEST_CASE(an_off_reaches_a_module_that_stopped_answering)
{
    fresh();
    run(100u, false);
    learn_on_twice();
    CHECK(m.output);
    m.mute = true;
    run(2500u, false);
    CHECK(!pdmini_status(&d)->online);
    CHECK(m.output);                           /* nothing asked otherwise */
    pdmini_want(&d, false, 5000u, 1000u);
    run(2500u, true);
    CHECK(!m.output);
    CHECK(!m.switched_on_by_off);

    /* On from the module's own buttons: no read-back has shown which
     * argument means off, so nothing is sent blind. */
    fresh();
    m.output = true;
    run(1000u, false);
    m.mute = true;
    pdmini_want(&d, false, 5000u, 1000u);
    run(3000u, false);
    CHECK_EQ(m.en_writes, 1u);                 /* the one before it went quiet */
}

/* Run until the driver has written OUTPUT_EN @p n times, at most 3 s. */
static void run_to_en_write(unsigned n)
{
    for (unsigned k = 0u; m.en_writes < n && k < 3000u; ++k) {
        run(1u, false);
    }
}

/* With the argument for off known, an OFF asked for while an ON waits to be
 * confirmed is written at once, and an ON slower than the confirmation never
 * shows; with it not yet known, the ON is watched past its 250 ms. */
TEST_CASE(an_off_cancels_a_slow_on)
{
    fresh();
    run(100u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    run(1500u, false);
    learn_on_twice();
    pdmini_want(&d, false, 5000u, 1000u);
    run(600u, false);
    CHECK(!m.output);
    CHECK(d.on_confirmed);
    m.settle_ms = 300u;
    pdmini_want(&d, true, 5000u, 1000u);
    run_to_en_write(3u);
    run(10u, false);
    pdmini_want(&d, false, 5000u, 1000u);
    m.on_ms = 0u;
    run(1500u, false);
    CHECK(!m.output);
    CHECK_EQ(m.on_ms, 0u);
    CHECK_EQ(m.en_writes, 4u);

    fresh();
    m.settle_ms = 300u;
    run(100u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    run_to_en_write(1u);
    run(10u, false);
    pdmini_want(&d, false, 5000u, 1000u);
    run(1500u, false);
    CHECK(!m.output);
    /* Seen on as it came on, at 300 ms, and its 300 ms to go off. */
    CHECK(m.on_ms < 400u);
    CHECK(d.on_seen);
    CHECK_EQ(d.on_value, 1u);
}

/* A module that reads OUTPUT_EN the way the sheet says, its ON cancelled
 * while the third write is being confirmed: seen on, the argument is learnt
 * there, and the OFF is right first time. */
TEST_CASE(a_watched_on_teaches_the_argument)
{
    fresh();
    m.on_arg = 0u;
    m.settle_ms = 100u;
    run(100u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    run_to_en_write(3u);
    run(10u, false);
    pdmini_want(&d, false, 5000u, 1000u);
    m.on_ms = 0u;
    run(1500u, false);
    CHECK(!m.output);
    CHECK_EQ(d.on_value, 0u);
    CHECK_EQ(m.en_writes, 4u);
    CHECK(m.on_ms < 200u);
}

/* Once a read-back has shown which argument means off, an OFF slower than
 * two confirmations is written again with that argument only: the other
 * would be an ON, and undo it. */
TEST_CASE(a_known_argument_is_not_tried_the_other_way)
{
    fresh();
    run(100u, false);
    learn_on_twice();
    CHECK(d.on_confirmed);
    m.settle_ms = 600u;
    pdmini_want(&d, false, 5000u, 1000u);
    run(3000u, true);
    CHECK(!m.output);
    CHECK(!m.switched_on_by_off);
}

/* An ON whose pins are still being handed over when an OFF is asked for is
 * not sent. */
TEST_CASE(an_on_not_yet_sent_is_dropped_by_an_off)
{
    fresh();
    run(100u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    for (unsigned k = 0u; k < 3000u
         && !(d.phase == PD_ATTACH && d.cmd == PDMINI_OUTPUT_EN); ++k) {
        run(1u, false);
    }
    CHECK(d.phase == PD_ATTACH);
    pdmini_want(&d, false, 5000u, 1000u);
    run(1000u, true);
    CHECK(!m.output);
    CHECK_EQ(m.on_ms, 0u);
    CHECK_EQ(m.en_writes, 0u);
}

/* A slot chosen on the module's buttons once its set points read back right
 * is not the one switched on: the slot is read again straight before the
 * ON, and the new slot given the set points first. */
TEST_CASE(the_slot_is_read_again_straight_before_an_on)
{
    fresh();
    m.mv[3] = 20000u;
    m.ma[3] = 3000u;
    m.switch_to = 3;
    m.switch_mv = 12000u;
    run(100u, false);
    pdmini_want(&d, true, 12000u, 1500u);
    run(2000u, false);
    CHECK(m.output);
    CHECK_EQ(m.slot, 3);
    CHECK_EQ(m.on_at_mv, 12000u);
}

/* A module that goes quiet straight after an ON is owed the OFF however
 * many rounds of WHO_AM_I go unanswered before it is asked for. */
TEST_CASE(an_owed_off_outlasts_the_rounds_that_find_nothing)
{
    fresh();
    run(100u, false);
    learn_on_twice();
    pdmini_want(&d, false, 5000u, 1000u);
    run(600u, false);
    CHECK(d.on_confirmed);
    pdmini_want(&d, true, 5000u, 1000u);
    run_to_en_write(m.en_writes + 1u);
    m.mute = true;
    run(8000u, false);
    CHECK(!pdmini_status(&d)->online);
    CHECK(m.output);
    pdmini_want(&d, false, 5000u, 1000u);
    run(2500u, true);
    CHECK(!m.output);
    CHECK(!m.switched_on_by_off);
}

/* Another module, the other way round, in place of one that went quiet
 * with its output on: it answers who it is, and is read rather than sent
 * the old one's OFF, which would be its ON. */
TEST_CASE(no_blind_off_goes_to_a_module_that_answers)
{
    fresh();
    run(100u, false);
    learn_on_twice();
    CHECK(d.on_confirmed);
    m.mute = true;
    run(2500u, false);
    CHECK(!pdmini_status(&d)->online);
    for (unsigned k = 0u; k < 2000u && d.phase != PD_IDLE; ++k) {
        run(1u, false);                        /* changed between rounds */
    }
    m.mute   = false;                          /* the other module */
    m.output = false;
    m.on_arg = 0u;
    const unsigned writes = m.en_writes;
    pdmini_want(&d, false, 5000u, 1000u);
    run(3000u, true);
    CHECK(pdmini_status(&d)->online);
    CHECK(!m.output);
    CHECK(!m.switched_on_by_off);
    CHECK_EQ(m.en_writes, writes);
    /* And its argument for on is learnt afresh once the last one's does not
     * take four times. */
    pdmini_want(&d, true, 5000u, 1000u);
    run(6000u, false);
    CHECK(m.output);
    CHECK_EQ(d.on_value, 0u);
}

/* An OFF that does not take is written again without the pause an ON
 * waits out: four ignored, and the fifth goes straight on. */
TEST_CASE(an_off_is_not_paused_by_writes_that_did_not_take)
{
    fresh();
    run(100u, false);
    learn_on_twice();
    CHECK(d.on_confirmed);
    const unsigned writes = m.en_writes;
    m.ignore_en = 4u;
    pdmini_want(&d, false, 5000u, 1000u);
    run(1500u, false);
    CHECK(!m.output);
    CHECK_EQ(m.en_writes, writes + 5u);
    run(600u, false);
    CHECK(!pdmini_status(&d)->stuck);          /* seen off: not stuck */
}

/* A set point waiting for its pins when an OFF is asked for, with the
 * output on, is not sent: the OFF goes first. */
TEST_CASE(a_set_point_not_yet_sent_is_dropped_by_an_off)
{
    fresh();
    run(100u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    run(1500u, false);
    CHECK(m.output);
    pdmini_want(&d, true, 15000u, 1000u);
    for (unsigned k = 0u; k < 3000u
         && !(d.phase == PD_ATTACH && d.cmd == PDMINI_OUTPUT_DATA); ++k) {
        run(1u, false);
    }
    CHECK(d.phase == PD_ATTACH);
    pdmini_want(&d, false, 15000u, 1000u);
    const unsigned data = m.data_writes;
    run(5u, false);
    CHECK_EQ(m.data_writes, data);
    run(600u, false);
    CHECK(!m.output);
}

/* A slot number past the module's five is no slot: nothing is written to
 * a slot taken for it, and nothing switched on. */
TEST_CASE(a_slot_past_four_is_no_slot)
{
    fresh();
    m.slot    = 2;
    m.mv[2]   = 20000u;
    m.id_says = 7;
    run(100u, false);
    pdmini_want(&d, true, 12000u, 1500u);
    run(4000u, false);
    CHECK(!m.output);
    CHECK_EQ(m.en_writes, 0u);
    CHECK_EQ(m.data_writes, 0u);
    CHECK(pdmini_status(&d)->errors >= 3u);
}

/* Set points answered for another slot than asked fail, and do not hold
 * the driver asking for them again and again. */
TEST_CASE(set_points_for_another_slot_fail)
{
    fresh();
    m.other_slot = true;
    run(100u, false);
    pdmini_want(&d, false, 9000u, 700u);
    run(3000u, false);
    CHECK(pdmini_status(&d)->errors >= 3u);
    CHECK(m.reads[PDMINI_READ_DATA] < 20u);
    CHECK_EQ(m.data_writes, 0u);
}

/* Another device that frames a WHO_AM_I reply the same way is not taken
 * for the module, and nothing is written to it. */
TEST_CASE(another_device_is_not_the_module)
{
    fresh();
    m.who = "SOMETHING ELSE V2";
    run(100u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    run(3000u, false);
    CHECK(!pdmini_status(&d)->online);
    CHECK_EQ(m.en_writes, 0u);
    CHECK_EQ(m.data_writes, 0u);
    CHECK_EQ(m.reads[PDMINI_READ_STATE], 0u);
}

/* A module that answers who it is with another device's text, in place of
 * one that went quiet with its output on, is not silence: it is sent no
 * blind OFF, which may be its ON. */
TEST_CASE(no_blind_off_goes_to_another_device_that_answers)
{
    fresh();
    run(100u, false);
    learn_on_twice();
    CHECK(d.on_confirmed);
    m.mute = true;
    run(2500u, false);
    CHECK(!pdmini_status(&d)->online);
    for (unsigned k = 0u; k < 2000u && d.phase != PD_IDLE; ++k) {
        run(1u, false);
    }
    m.mute   = false;
    m.who    = "SOMETHING ELSE V2";
    m.output = false;
    m.on_arg = 0u;
    const unsigned writes = m.en_writes;
    pdmini_want(&d, false, 5000u, 1000u);
    run(3000u, true);
    CHECK(!m.output);
    CHECK_EQ(m.en_writes, writes);
}

/* A reply that drips is not waited for past the whole transaction's time,
 * and an OFF does not wait behind a slow read. */
TEST_CASE(a_slow_reply_holds_neither_the_pins_nor_an_off)
{
    fresh();
    m.byte_gap = 59u;                          /* a 36-byte WHO_AM_I: 2.1 s */
    run(3000u, false);
    CHECK(!pdmini_status(&d)->online);
    CHECK(m.attach_max <= PDMINI_ATTACH_MS + PDMINI_TXN_MS + 2u);

    fresh();
    run(100u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    run(1500u, false);
    CHECK(m.output);
    m.display_delay = 300u;
    for (unsigned k = 0u; k < 3000u
         && !(d.phase == PD_WAIT && d.cmd == PDMINI_READ_DISPLAY); ++k) {
        run(1u, false);
    }
    const unsigned writes = m.en_writes;
    pdmini_want(&d, false, 5000u, 1000u);
    run(30u, false);
    CHECK_EQ(m.en_writes, writes + 1u);        /* the OFF, not the reply */
    run(600u, false);
    CHECK(!m.output);

    /* And with an ON still being confirmed, the watch reads at once. */
    fresh();
    run(100u, false);
    m.settle_ms = 200u;
    pdmini_want(&d, true, 5000u, 1000u);
    run_to_en_write(1u);
    m.display_delay = 300u;
    for (unsigned k = 0u; k < 300u
         && !(d.phase == PD_WAIT && d.cmd == PDMINI_READ_DISPLAY); ++k) {
        run(1u, false);
    }
    CHECK(d.phase == PD_WAIT && d.cmd == PDMINI_READ_DISPLAY);
    const unsigned states = m.reads[PDMINI_READ_STATE];
    pdmini_want(&d, false, 5000u, 1000u);
    run(30u, false);
    CHECK(m.reads[PDMINI_READ_STATE] > states);
}

/* A module the other way round, on from its own buttons: the first OFF
 * guessed is its ON, and its overcurrent protection switches it off in the
 * meantime.  That is not taken to show which argument is which, so once
 * it is on again an OFF still tries both. */
TEST_CASE(an_output_that_went_off_by_itself_teaches_nothing)
{
    fresh();
    m.on_arg = 0u;
    m.output = true;
    m.self_off_ms = 200u;
    run(100u, false);
    pdmini_want(&d, false, 5000u, 1000u);
    run(1000u, false);
    CHECK(!m.output);
    CHECK(!d.on_confirmed);
    m.self_off_ms = 0u;
    m.output = true;                           /* on again from its buttons */
    run(3000u, true);
    CHECK(!m.output);
}

/* ON asked again while a slow OFF waits to be confirmed: the output still
 * reading on is not taken to show that the OFF's argument means on. */
TEST_CASE(an_on_asked_over_a_pending_off_teaches_nothing)
{
    fresh();
    run(100u, false);
    learn_on_twice();
    CHECK(d.on_confirmed);
    m.settle_ms = 300u;
    const unsigned writes = m.en_writes;
    pdmini_want(&d, false, 5000u, 1000u);
    run_to_en_write(writes + 1u);
    run(10u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    run(3000u, false);
    CHECK_EQ(d.on_value, 1u);
    CHECK(m.output);
}

/* A set point dropped for an OFF that also changes the set points leaves
 * nothing stuck: the new ones are written straight after the OFF. */
TEST_CASE(a_dropped_set_point_leaves_nothing_stuck)
{
    fresh();
    run(100u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    run(1500u, false);
    pdmini_want(&d, true, 15000u, 1000u);
    for (unsigned k = 0u; k < 3000u
         && !(d.phase == PD_ATTACH && d.cmd == PDMINI_OUTPUT_DATA); ++k) {
        run(1u, false);
    }
    CHECK(d.phase == PD_ATTACH);
    pdmini_want(&d, false, 9000u, 700u);
    run(600u, false);
    CHECK(!m.output);
    CHECK(!pdmini_status(&d)->set_stuck);
    CHECK_EQ(m.mv[0], 9000u);
}

/* Set points changed while an ON read back for the old ones waits for its
 * pins: that ON is not sent, and the output comes on at the new ones. */
TEST_CASE(an_on_is_dropped_when_its_set_points_change)
{
    fresh();
    run(100u, false);
    pdmini_want(&d, true, 20000u, 1000u);
    for (unsigned k = 0u; k < 3000u
         && !(d.phase == PD_ATTACH && d.cmd == PDMINI_OUTPUT_EN); ++k) {
        run(1u, false);
    }
    CHECK(d.phase == PD_ATTACH);
    pdmini_want(&d, true, 5000u, 1000u);
    run(2000u, false);
    CHECK(m.output);
    CHECK_EQ(m.on_at_mv, 5000u);
    CHECK_EQ(m.en_writes, 1u);
}

/* A module swapped between two reads for one that reads OUTPUT_EN the other
 * way round, and on: the learnt argument is put in doubt after four writes
 * that do not take, and the other one switches it off. */
TEST_CASE(a_learnt_argument_that_stops_working_is_doubted)
{
    fresh();
    run(100u, false);
    learn_on_twice();
    CHECK(d.on_confirmed);
    m.on_arg = 0u;                             /* swapped, and on */
    m.output = true;
    pdmini_want(&d, false, 5000u, 1000u);
    run(3000u, true);
    CHECK(!m.output);
}

/* Set points changed while the old ones wait for their pins, the output
 * on: the old ones are not sent. */
TEST_CASE(stale_set_points_are_not_sent)
{
    fresh();
    run(100u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    run(1500u, false);
    pdmini_want(&d, true, 20000u, 1000u);
    for (unsigned k = 0u; k < 3000u
         && !(d.phase == PD_ATTACH && d.cmd == PDMINI_OUTPUT_DATA); ++k) {
        run(1u, false);
    }
    CHECK(d.phase == PD_ATTACH);
    pdmini_want(&d, true, 6000u, 1000u);
    const unsigned data = m.data_writes;
    run(5u, false);
    CHECK_EQ(m.data_writes, data);
    run(1000u, false);
    CHECK_EQ(m.mv[0], 6000u);
}

/* Firmware that does not answer READ_INPUT_STATE is asked three times and
 * then left alone, so its 400 ms windows do not hold up the rest. */
TEST_CASE(an_unanswered_input_read_is_given_up)
{
    fresh();
    m.no_input = true;
    run(100u, false);
    run(5000u, false);
    const unsigned asked = m.reads[PDMINI_READ_INPUT];
    CHECK_EQ(asked, PDMINI_INPUT_MISSES);
    CHECK(pdmini_status(&d)->online);
    run(3000u, false);
    CHECK_EQ(m.reads[PDMINI_READ_INPUT], asked);
}

/* The same module, on, goes quiet, answers again and goes quiet again: the
 * argument it showed is kept, and an OFF still reaches it blind. */
TEST_CASE(an_argument_outlasts_a_module_that_comes_back)
{
    fresh();
    run(100u, false);
    learn_on_twice();
    CHECK(d.on_confirmed);
    m.mute = true;
    run(2500u, false);
    CHECK(!pdmini_status(&d)->online);
    m.mute = false;
    run(1500u, false);
    CHECK(pdmini_status(&d)->online);
    CHECK(d.on_confirmed);
    m.mute = true;
    run(2500u, false);
    pdmini_want(&d, false, 5000u, 1000u);
    run(2500u, true);
    CHECK(!m.output);
}

/* A module the other way round, whose output comes on by itself just after
 * the first ON guessed -- its OFF.  Seen once, the guess is not relied on:
 * the OFF that follows tries the other argument after two. */
TEST_CASE(an_output_that_came_on_by_itself_is_not_relied_on)
{
    fresh();
    m.on_arg = 0u;
    m.self_on_ms = 50u;
    run(100u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    run(1000u, false);
    CHECK(m.output);
    CHECK(!d.on_confirmed);
    pdmini_want(&d, false, 5000u, 1000u);
    m.on_ms = 0u;
    run(2000u, true);
    CHECK(!m.output);
    CHECK(m.on_ms < 900u);
}

/* An ON that went out, its OFF lost, and then nothing answers: the ON is
 * owed an OFF, sent blind. */
TEST_CASE(an_on_sent_and_unsettled_is_owed_an_off)
{
    fresh();
    run(100u, false);
    learn_on_twice();
    pdmini_want(&d, false, 5000u, 1000u);
    run(600u, false);
    CHECK(!m.output);
    m.settle_ms = 100u;
    pdmini_want(&d, true, 5000u, 1000u);
    run_to_en_write(m.en_writes + 1u);
    m.ignore_en = 1u;                          /* the OFF is lost */
    pdmini_want(&d, false, 5000u, 1000u);
    run_to_en_write(m.en_writes + 1u);
    m.mute = true;
    run(6000u, true);
    CHECK(!m.output);
}

/* Live set points the module will not take: the output goes off rather
 * than stay at the old ones, and stays off while ON is still asked, until
 * an OFF and a new ON. */
TEST_CASE(live_set_points_that_will_not_take_switch_the_output_off)
{
    fresh();
    run(100u, false);
    pdmini_want(&d, true, 20000u, 1000u);
    run(1500u, false);
    CHECK(m.output);
    m.ignore_data = true;
    for (int k = 0; k < 40; ++k) {             /* asked every pass, as the */
        pdmini_want(&d, true, 5000u, 1000u);   /* coprocessor does         */
        run(50u, false);
    }
    CHECK(!m.output);
    CHECK(pdmini_status(&d)->set_stuck);
    m.ignore_data = false;                     /* takes them now, but ON */
    for (int k = 0; k < 60; ++k) {             /* is still only repeated */
        pdmini_want(&d, true, 5000u, 1000u);
        run(50u, false);
    }
    CHECK(!m.output);
    pdmini_want(&d, false, 5000u, 1000u);
    run(100u, false);
    pdmini_want(&d, true, 5000u, 1000u);
    run(2000u, false);
    CHECK(m.output);
    CHECK_EQ(m.on_at_mv, 5000u);
}

/* Only the state reads go unanswered, the rest still do, after an ON went
 * out and its OFF was lost: the module is taken for gone, and the OFF it is
 * owed goes blind. */
TEST_CASE(state_reads_that_fail_are_not_hidden_by_the_rest)
{
    fresh();
    run(100u, false);
    learn_on_twice();
    pdmini_want(&d, false, 5000u, 1000u);
    run(600u, false);
    CHECK(!m.output);
    m.settle_ms = 100u;
    pdmini_want(&d, true, 5000u, 1000u);
    run_to_en_write(m.en_writes + 1u);
    m.ignore_en = 1u;                          /* the OFF is lost */
    m.no_state  = true;
    pdmini_want(&d, false, 5000u, 1000u);
    run(5000u, true);
    CHECK(!m.output);
    CHECK(pdmini_status(&d)->errors >= 3u);
}

/* Readings start on time when the millisecond count is past 2^31. */
TEST_CASE(readings_are_taken_past_half_the_clock)
{
    fresh();
    now = 0x80001000u;
    pdmini_init(&d, &k_io, now);
    m.v_mv = 12050u;
    run(1500u, false);
    CHECK(pdmini_status(&d)->online);
    CHECK(pdmini_status(&d)->samples >= 8u);
    CHECK_EQ(pdmini_status(&d)->v_mv, 12050u);
    CHECK_EQ(pdmini_status(&d)->in_state, 5u);
}

/* A byte left on the line by the pin handover is not taken for the start of
 * the reply. */
TEST_CASE(a_byte_from_the_handover_is_not_the_reply)
{
    fresh();
    m.junk = 0x55u;
    run(1000u, false);
    CHECK(pdmini_status(&d)->online);
    CHECK_EQ(pdmini_status(&d)->errors, 0u);
}

int main(void)
{
    RUN(the_crc_matches_every_value_the_sheet_prints);
    RUN(a_reply_is_whole_only_with_its_length_and_its_check);
    RUN(the_module_is_identified_then_read_with_the_pins_at_rest_between);
    RUN(an_output_and_its_set_points_are_written_and_read_back);
    RUN(set_points_are_clamped_to_the_module);
    RUN(the_on_argument_is_learnt_from_the_module);
    RUN(an_off_never_switches_an_output_on);
    RUN(the_confirming_read_waits_for_the_output_to_settle);
    RUN(an_on_that_does_not_take_is_stuck_and_tried_again);
    RUN(a_module_that_is_not_there_or_goes_quiet_is_noticed);
    RUN(a_byte_from_the_handover_is_not_the_reply);
    RUN(an_on_waits_for_the_set_points);
    RUN(slow_display_reads_do_not_starve_the_others);
    RUN(the_slot_is_read_again_and_put_back);
    RUN(set_points_that_do_not_take_are_stuck_and_bounded);
    RUN(stuck_clears_when_the_output_is_seen_as_asked);
    RUN(an_off_does_not_wait_behind_an_on_being_confirmed);
    RUN(an_off_reaches_a_module_that_stopped_answering);
    RUN(an_off_cancels_a_slow_on);
    RUN(a_watched_on_teaches_the_argument);
    RUN(a_known_argument_is_not_tried_the_other_way);
    RUN(an_on_not_yet_sent_is_dropped_by_an_off);
    RUN(the_slot_is_read_again_straight_before_an_on);
    RUN(an_owed_off_outlasts_the_rounds_that_find_nothing);
    RUN(no_blind_off_goes_to_a_module_that_answers);
    RUN(readings_are_taken_past_half_the_clock);
    RUN(an_off_is_not_paused_by_writes_that_did_not_take);
    RUN(a_set_point_not_yet_sent_is_dropped_by_an_off);
    RUN(a_slot_past_four_is_no_slot);
    RUN(set_points_for_another_slot_fail);
    RUN(another_device_is_not_the_module);
    RUN(no_blind_off_goes_to_another_device_that_answers);
    RUN(a_slow_reply_holds_neither_the_pins_nor_an_off);
    RUN(an_output_that_went_off_by_itself_teaches_nothing);
    RUN(an_on_asked_over_a_pending_off_teaches_nothing);
    RUN(a_dropped_set_point_leaves_nothing_stuck);
    RUN(an_on_is_dropped_when_its_set_points_change);
    RUN(a_learnt_argument_that_stops_working_is_doubted);
    RUN(stale_set_points_are_not_sent);
    RUN(an_unanswered_input_read_is_given_up);
    RUN(an_argument_outlasts_a_module_that_comes_back);
    RUN(an_output_that_came_on_by_itself_is_not_relied_on);
    RUN(an_on_sent_and_unsettled_is_owed_an_off);
    RUN(live_set_points_that_will_not_take_switch_the_output_off);
    RUN(state_reads_that_fail_are_not_hidden_by_the_rest);
    return test_summary("pdmini");
}
