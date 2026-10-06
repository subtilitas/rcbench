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
} module_t;

static module_t m;
static pdmini_t d;
static uint32_t now;

static void m_attach(void *ctx)
{
    (void)ctx;
    m.attached = true;
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
    switch (p[0]) {
    case PDMINI_WHO_AM_I: {
        static const char who[] = "WeAct Studio PD Power Mini V1 BUCK";
        uint8_t r[64];
        r[0] = PDMINI_WHO_AM_I;
        r[1] = (uint8_t)(sizeof(who) - 1u);
        memcpy(&r[2], who, sizeof(who) - 1u);
        reply(r, 2u + sizeof(who) - 1u, m.whoami_crc);
        break;
    }
    case PDMINI_READ_STATE: {
        const uint8_t r[2] = { PDMINI_READ_STATE, (uint8_t)(m.output ? 1u : 0u) };
        reply(r, 2u, true);
        break;
    }
    case PDMINI_READ_ID: {
        const uint8_t r[2] = { PDMINI_READ_ID, (uint8_t)m.slot };
        reply(r, 2u, true);
        break;
    }
    case PDMINI_READ_DATA: {
        const uint8_t s = p[1];
        const uint8_t r[6] = { PDMINI_READ_DATA, s,
                               (uint8_t)(m.mv[s] & 0xFFu), (uint8_t)(m.mv[s] >> 8),
                               (uint8_t)(m.ma[s] & 0xFFu), (uint8_t)(m.ma[s] >> 8) };
        reply(r, 6u, true);
        break;
    }
    case PDMINI_READ_DISPLAY: {
        const uint8_t r[5] = { PDMINI_READ_DISPLAY,
                               (uint8_t)(m.v_mv & 0xFFu), (uint8_t)(m.v_mv >> 8),
                               (uint8_t)(m.i_ma & 0xFFu), (uint8_t)(m.i_ma >> 8) };
        reply(r, 5u, true);
        break;
    }
    case PDMINI_READ_INPUT: {
        const uint8_t r[6] = { PDMINI_READ_INPUT, m.input_ok ? 5u : 1u,
                               0xC0u, 0x5Du, 200u, 0u };   /* 24000 mV, 20.0 V */
        reply(r, 6u, true);
        break;
    }
    case PDMINI_OUTPUT_EN:
        ++m.en_writes;
        m.pending = true;
        m.target  = (p[1] == m.on_arg) && m.input_ok;
        m.en_at   = now;
        break;
    case PDMINI_OUTPUT_DATA:
        if (p[1] <= 4u) {
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
            m.output = m.target;
        }
        if (m.attached && m.out_i < m.out_n
            && (int32_t)(now - m.out_at) >= 0) {
            pdmini_rx(&d, m.out[m.out_i++], now);
            m.out_at = now + 1u;
        }
        pdmini_step(&d, now);
    }
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
    return test_summary("pdmini");
}
