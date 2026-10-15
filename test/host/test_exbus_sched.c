/*
 * The master's cycle on an EX Bus line (protocols/exbus/exbus_sched.c),
 * against a pin driver this suite controls.
 *
 * The fake driver keeps what the scheduler sends and the window it asks
 * for, ends a transaction when the case says so with the bytes the case
 * gives, refuses a start on request and counts aborts.  Time is the now_us
 * the case passes.
 *
 * Under test:
 *  - the configuration: the default, each refusal, the time a cycle needs
 *    at both rates with 1 to 24 channels;
 *  - the cycle: channels with header 0x3E 0x03 and a request with the same
 *    Packet_ID in one transmission, byte for byte; header 0x3E 0x01 without
 *    a request; a request every nth cycle; 24 channels; the Packet_ID's
 *    count and wrap; the period, a late step and the restart of the
 *    schedule;
 *  - the reply: the specification's reply, an empty one, no reply, one
 *    with a wrong CRC, a truncated one, one for another Packet_ID, one
 *    that starts after the window, one with a framing error, bytes after a
 *    cycle without a request, an overflowed capture;
 *  - silence: nothing before a start and without channel values, stop
 *    during a request (the transaction cut, its reply dropped, nothing
 *    more sent), channel values older than the hold time, a driver that
 *    never ends a transaction, a driver that refuses a start;
 *  - the channel values: the count, the clamp, the wrap of now_us.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "exbus_sched.h"
#include "exbus_wire.h"

/* ------------------------------------------------------------ the driver */

typedef struct {
    uint8_t tx[EXBUS_TX_MAX];
    unsigned n;
    uint32_t window_us;
    unsigned starts;
    unsigned polls;
    unsigned aborts;
    bool refuse;
    bool busy;
    bool done;
    exbus_capture_t cap;
} fake_t;

static bool fake_start(void *ctx, const uint8_t *tx, unsigned n,
                       uint32_t window_us)
{
    fake_t *f = (fake_t *)ctx;

    if (f->refuse || f->busy || n > sizeof(f->tx)) {
        return false;
    }
    memcpy(f->tx, tx, n);
    f->n = n;
    f->window_us = window_us;
    f->starts++;
    f->busy = true;
    f->done = false;
    memset(&f->cap, 0, sizeof(f->cap));
    return true;
}

static bool fake_poll(void *ctx, exbus_capture_t *out)
{
    fake_t *f = (fake_t *)ctx;

    f->polls++;
    if (!f->busy || !f->done) {
        return false;
    }
    *out = f->cap;
    f->busy = false;
    return true;
}

static void fake_abort(void *ctx)
{
    fake_t *f = (fake_t *)ctx;

    f->aborts++;
    f->busy = false;
}

static fake_t fake;
static exbus_sched_t s;

static exbus_driver_t driver(void)
{
    exbus_driver_t d;

    d.ctx = &fake;
    d.start = fake_start;
    d.poll = fake_poll;
    d.abort = fake_abort;
    return d;
}

/* ---------------------------------------------------------------- helpers */

static unsigned hex_to_bytes(const char *hex, uint8_t *out, unsigned room)
{
    unsigned n = 0;
    unsigned v = 0;
    unsigned digits = 0;

    for (const char *p = hex; *p != '\0'; ++p) {
        const char c = *p;
        unsigned d;

        if (c >= '0' && c <= '9') {
            d = (unsigned)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            d = (unsigned)(c - 'A' + 10);
        } else {
            continue;
        }
        v = (v << 4) | d;
        if (++digits == 2u) {
            if (n < room) {
                out[n] = (uint8_t)v;
            }
            ++n;
            v = 0;
            digits = 0;
        }
    }
    return n;
}

static bool sent_is(const char *want)
{
    uint8_t w[128];
    const unsigned n = hex_to_bytes(want, w, sizeof(w));

    if (n != fake.n) {
        printf("    sent %u bytes, want %u\n", fake.n, n);
        return false;
    }
    for (unsigned i = 0; i < n; ++i) {
        if (fake.tx[i] != w[i]) {
            printf("    byte %u: %02X, want %02X\n", i, fake.tx[i], w[i]);
            return false;
        }
    }
    return true;
}

/* The running transaction ends with the bytes of @p hex received, the
 * first @p at_us after the release and one every 80 us (125 kbaud). */
static void reply_with(const char *hex, uint32_t at_us)
{
    uint8_t b[EXBUS_RX_MAX];
    const unsigned n = hex_to_bytes(hex, b, sizeof(b));

    memset(&fake.cap, 0, sizeof(fake.cap));
    for (unsigned i = 0; i < n && i < EXBUS_RX_MAX; ++i) {
        fake.cap.bytes[i].value = b[i];
        fake.cap.bytes[i].at_us = (uint16_t)(at_us + 80u * i);
    }
    fake.cap.n = (uint8_t)(n > EXBUS_RX_MAX ? EXBUS_RX_MAX : n);
    fake.cap.release_us = (uint16_t)(4000u + 80u * n);
    fake.done = true;
}

/* The running transaction ends without a byte. */
static void silence(void)
{
    memset(&fake.cap, 0, sizeof(fake.cap));
    fake.cap.release_us = 4000;
    fake.done = true;
}

static uint16_t ch[EXBUS_MAX_CHANNELS];

/* A scheduler on the fake with @p channels at 1500 us, set at @p now and
 * started there.  @p every: request_every. */
static void setup(uint8_t channels, uint8_t every, uint32_t now)
{
    exbus_cfg_t cfg;
    const exbus_driver_t d = driver();

    memset(&fake, 0, sizeof(fake));
    exbus_cfg_default(&cfg);
    cfg.channels = channels;
    cfg.request_every = every;
    CHECK_EQ(exbus_sched_init(&s, &d, &cfg), EXBUS_CFG_OK);
    for (unsigned i = 0; i < EXBUS_MAX_CHANNELS; ++i) {
        ch[i] = 12000;
    }
    CHECK(exbus_sched_set_channels(&s, ch, channels, now));
    CHECK(exbus_sched_start(&s, now));
}

/* A cycle at @p t with fresh channel values that ends without a byte;
 * returns the events of its start. */
static unsigned quiet_cycle(uint32_t t)
{
    unsigned ev;

    CHECK(exbus_sched_set_channels(&s, ch, s.cfg.channels, t));
    ev = exbus_sched_step(&s, t);
    silence();
    (void)exbus_sched_step(&s, t + 9000u);
    return ev;
}

/* The specification's reply to Packet_ID 8. */
#define SPEC_REPLY                                                           \
    "3B 01 20 08 3A 18 9F 56 00 A4 51 55 EE 11 30 20 21 00 40 34 A3 28 00"   \
    " 41 00 00 51 18 00 09 91 D6"

/* ------------------------------------------------------------------ cases */

TEST_CASE(the_default_is_125_kbaud_10_ms_16_channels_and_a_request_a_cycle)
{
    exbus_cfg_t c;

    exbus_cfg_default(&c);
    exbus_cfg_default(NULL);
    CHECK_EQ(c.baud, 125000u);
    CHECK_EQ(c.period_us, 10000u);
    CHECK_EQ(c.window_us, 4000u);
    CHECK_EQ(c.hold_us, 100000u);
    CHECK_EQ(c.channels, 16);
    CHECK_EQ(c.request_every, 1);
    CHECK_EQ(exbus_cfg_check(&c), EXBUS_CFG_OK);
    /* 40 + 8 bytes at 80 us, and the 4 ms window. */
    CHECK_EQ(exbus_cycle_us(&c), 7840u);
    c.baud = 250000;
    CHECK_EQ(exbus_cycle_us(&c), 5920u);
    c.channels = 24;
    CHECK_EQ(exbus_cycle_us(&c), 6560u);
    c.baud = 125000;
    CHECK_EQ(exbus_cycle_us(&c), 9120u);
    CHECK_EQ(exbus_cfg_check(&c), EXBUS_CFG_OK);
    c.request_every = 0;
    CHECK_EQ(exbus_cycle_us(&c), 8480u);
    c.channels = 0;
    CHECK_EQ(exbus_cycle_us(&c), 0u);
    CHECK_EQ(exbus_cycle_us(NULL), 0u);
}

TEST_CASE(each_configuration_error_has_its_code)
{
    exbus_cfg_t c;
    exbus_cfg_t d;

    exbus_cfg_default(&d);
    CHECK_EQ(exbus_cfg_check(NULL), EXBUS_CFG_ERR_ARG);
    c = d;
    c.baud = 115200;
    CHECK_EQ(exbus_cfg_check(&c), EXBUS_CFG_ERR_BAUD);
    c = d;
    c.channels = 0;
    CHECK_EQ(exbus_cfg_check(&c), EXBUS_CFG_ERR_CHANNELS);
    c.channels = 25;
    CHECK_EQ(exbus_cfg_check(&c), EXBUS_CFG_ERR_CHANNELS);
    c = d;
    c.window_us = 3999;
    CHECK_EQ(exbus_cfg_check(&c), EXBUS_CFG_ERR_WINDOW);
    c.window_us = 20001;
    CHECK_EQ(exbus_cfg_check(&c), EXBUS_CFG_ERR_WINDOW);
    c = d;
    c.period_us = 4999;
    CHECK_EQ(exbus_cfg_check(&c), EXBUS_CFG_ERR_PERIOD);
    c.period_us = 30001;
    CHECK_EQ(exbus_cfg_check(&c), EXBUS_CFG_ERR_PERIOD);
    /* 16 channels and a request at 125 kbaud need 7840 us and 500 us of
     * guard: 8340 fits, 8339 does not. */
    c = d;
    c.period_us = 8340;
    CHECK_EQ(exbus_cfg_check(&c), EXBUS_CFG_OK);
    c.period_us = 8339;
    CHECK_EQ(exbus_cfg_check(&c), EXBUS_CFG_ERR_FIT);
    /* 5 ms fits 1 channel at 250 kbaud without requests. */
    c.period_us = 5000;
    c.baud = 250000;
    c.channels = 1;
    c.request_every = 0;
    CHECK_EQ(exbus_cfg_check(&c), EXBUS_CFG_OK);
    /* 30 ms and a window of 20 ms. */
    c = d;
    c.period_us = 30000;
    c.window_us = 20000;
    CHECK_EQ(exbus_cfg_check(&c), EXBUS_CFG_OK);
    c = d;
    c.hold_us = 9999;
    CHECK_EQ(exbus_cfg_check(&c), EXBUS_CFG_ERR_HOLD);
    c.hold_us = 0;
    CHECK_EQ(exbus_cfg_check(&c), EXBUS_CFG_OK);
}

TEST_CASE(init_takes_nothing_it_refuses)
{
    exbus_cfg_t c;
    exbus_driver_t d = driver();

    exbus_cfg_default(&c);
    CHECK_EQ(exbus_sched_init(NULL, &d, &c), EXBUS_CFG_ERR_ARG);
    CHECK_EQ(exbus_sched_init(&s, NULL, &c), EXBUS_CFG_ERR_ARG);
    CHECK_EQ(exbus_sched_init(&s, &d, NULL), EXBUS_CFG_ERR_ARG);
    d.abort = NULL;
    CHECK_EQ(exbus_sched_init(&s, &d, &c), EXBUS_CFG_ERR_ARG);
    d = driver();
    c.baud = 0;
    CHECK_EQ(exbus_sched_init(&s, &d, &c), EXBUS_CFG_ERR_BAUD);
}

TEST_CASE(nothing_is_sent_before_a_start_or_without_channel_values)
{
    exbus_cfg_t c;
    const exbus_driver_t d = driver();

    memset(&fake, 0, sizeof(fake));
    exbus_cfg_default(&c);
    c.channels = 8;
    CHECK_EQ(exbus_sched_init(&s, &d, &c), EXBUS_CFG_OK);
    CHECK_EQ(exbus_sched_step(&s, 0), 0u);
    CHECK(!exbus_sched_start(&s, 0));
    CHECK(!exbus_sched_running(&s));
    CHECK_EQ(exbus_sched_step(&s, 100000), 0u);
    /* The count must be the configured one. */
    CHECK(!exbus_sched_set_channels(&s, ch, 7, 0));
    CHECK(!exbus_sched_set_channels(&s, ch, 9, 0));
    CHECK(!exbus_sched_set_channels(&s, NULL, 8, 0));
    CHECK(!exbus_sched_start(&s, 0));
    CHECK_EQ(fake.starts, 0u);
    CHECK_EQ(fake.polls, 0u);
    CHECK_EQ(exbus_sched_last_rx(&s), EXBUS_RX_NONE);
    CHECK(exbus_sched_reply(&s) == NULL);
    /* Safe on NULL. */
    CHECK_EQ(exbus_sched_step(NULL, 0), 0u);
    exbus_sched_stop(NULL);
    CHECK(!exbus_sched_running(NULL));
    CHECK(exbus_sched_capture(NULL) == NULL);
    CHECK(exbus_sched_reply(NULL) == NULL);
    CHECK_EQ(exbus_sched_last_rx(NULL), EXBUS_RX_NONE);
    CHECK(!exbus_sched_start(NULL, 0));
}

TEST_CASE(a_cycle_is_channels_with_0x03_and_a_request_with_the_same_id)
{
    setup(8, 1, 0);
    CHECK_EQ(exbus_sched_step(&s, 0), EXBUS_EV_SENT);
    CHECK(sent_is("3E 03 18 00 31 10 E0 2E E0 2E E0 2E E0 2E E0 2E E0 2E"
                  " E0 2E E0 2E 59 6D"
                  " 3D 01 08 00 3A 00 41 57"));
    CHECK_EQ(fake.window_us, 4000u);
    /* Nothing more while it runs, and nothing before the period. */
    CHECK_EQ(exbus_sched_step(&s, 100), 0u);
    silence();
    CHECK_EQ(exbus_sched_step(&s, 8000), EXBUS_EV_NO_REPLY);
    CHECK_EQ(exbus_sched_step(&s, 9999), 0u);
    CHECK_EQ(fake.starts, 1u);
    CHECK_EQ(exbus_sched_step(&s, 10000), EXBUS_EV_SENT);
    CHECK(sent_is("3E 03 18 01 31 10 E0 2E E0 2E E0 2E E0 2E E0 2E E0 2E"
                  " E0 2E E0 2E 10 FE"
                  " 3D 01 08 01 3A 00 9D 0D"));
    CHECK_EQ(s.stats.requests, 2u);
}

TEST_CASE(without_requests_the_channel_packet_releases_with_0x01)
{
    setup(8, 0, 0);
    CHECK_EQ(exbus_sched_step(&s, 0), EXBUS_EV_SENT);
    CHECK_EQ(fake.n, 24u);
    CHECK_EQ(fake.tx[1], EXBUS_HEAD_RELEASE);
    CHECK_EQ(exbus_crc16(fake.tx, fake.n), 0);
    silence();
    CHECK_EQ(exbus_sched_step(&s, 5000), 0u);
    CHECK_EQ(s.stats.requests, 0u);
    CHECK_EQ(s.stats.no_reply, 0u);
    /* Bytes after such a cycle are not a reply. */
    CHECK_EQ(exbus_sched_step(&s, 10000), EXBUS_EV_SENT);
    reply_with("3B 01 08 01 3A 00", 200);
    CHECK_EQ(exbus_sched_step(&s, 15000), EXBUS_EV_UNSOLICITED);
    CHECK_EQ(s.stats.unsolicited, 1u);
    CHECK(exbus_sched_reply(&s) == NULL);
}

TEST_CASE(one_request_every_third_cycle)
{
    static const bool want[7] = { true, false, false, true, false, false,
                                  true };

    setup(4, 3, 0);
    for (unsigned i = 0; i < 7u; ++i) {
        CHECK_EQ(exbus_sched_step(&s, i * 10000u), EXBUS_EV_SENT);
        CHECK_EQ(fake.n, want[i] ? 24u : 16u);
        CHECK_EQ(fake.tx[1], want[i] ? EXBUS_HEAD_HOLD : EXBUS_HEAD_RELEASE);
        CHECK_EQ(fake.tx[3], i);
        silence();
        (void)exbus_sched_step(&s, i * 10000u + 9000u);
    }
    CHECK_EQ(s.stats.requests, 3u);
    CHECK_EQ(s.stats.no_reply, 3u);
    CHECK_EQ(s.stats.cycles, 7u);
}

TEST_CASE(twenty_four_channels_and_a_request_are_64_bytes)
{
    setup(24, 1, 0);
    CHECK_EQ(exbus_sched_step(&s, 0), EXBUS_EV_SENT);
    CHECK_EQ(fake.n, 64u);
    CHECK_EQ(fake.tx[2], 56);
    CHECK_EQ(exbus_crc16(fake.tx, 56), 0);
    CHECK_EQ(exbus_crc16(fake.tx + 56, 8), 0);
}

TEST_CASE(the_packet_id_counts_up_and_wraps_and_the_spec_reply_passes)
{
    unsigned ev = 0;

    setup(16, 1, 0);
    for (unsigned i = 0; i < 8u; ++i) {
        CHECK_EQ(exbus_sched_step(&s, i * 10000u), EXBUS_EV_SENT);
        silence();
        CHECK_EQ(exbus_sched_step(&s, i * 10000u + 9000u), EXBUS_EV_NO_REPLY);
    }
    CHECK_EQ(s.stats.no_reply, 8u);
    /* Cycle 9 carries Packet_ID 8: the specification's reply fits it. */
    CHECK_EQ(exbus_sched_step(&s, 80000), EXBUS_EV_SENT);
    CHECK_EQ(fake.tx[3], 8);
    CHECK_EQ(fake.tx[40 + 3], 8);
    reply_with(SPEC_REPLY, 300);
    ev = exbus_sched_step(&s, 89000);
    CHECK_EQ(ev, EXBUS_EV_REPLY);
    CHECK_EQ(exbus_sched_last_rx(&s), EXBUS_RX_OK);
    CHECK(exbus_sched_reply(&s) != NULL);
    if (exbus_sched_reply(&s) != NULL) {
        CHECK_EQ(exbus_sched_reply(&s)->block_len, 24);
        CHECK_EQ(exbus_sched_reply(&s)->block[0], 0x9F);
    }
    CHECK_EQ(exbus_sched_capture(&s)->n, 32);
    CHECK_EQ(s.stats.replies, 1u);
    /* On to 255 and over to 0. */
    for (unsigned i = 9; i < 258u; ++i) {
        CHECK_EQ(quiet_cycle(i * 10000u), EXBUS_EV_SENT);
        CHECK_EQ(fake.tx[3], (uint8_t)i);
    }
    CHECK_EQ(fake.tx[3], 1);
    CHECK_EQ(s.stats.late, 0u);
}

TEST_CASE(an_empty_reply_passes_and_is_counted)
{
    setup(16, 1, 0);
    for (unsigned i = 0; i < 6u; ++i) {
        CHECK_EQ(exbus_sched_step(&s, i * 10000u), EXBUS_EV_SENT);
        silence();
        (void)exbus_sched_step(&s, i * 10000u + 9000u);
    }
    CHECK_EQ(exbus_sched_step(&s, 60000), EXBUS_EV_SENT);
    reply_with("3B 01 08 06 3A 00 62 99", 150);
    CHECK_EQ(exbus_sched_step(&s, 69000), EXBUS_EV_REPLY);
    CHECK_EQ(s.stats.empty, 1u);
    CHECK_EQ(exbus_sched_reply(&s)->block_len, 0);
}

/* Cycles to Packet_ID 8, then one that ends with @p hex at @p at_us. */
static unsigned reply_8(const char *hex, uint32_t at_us)
{
    setup(16, 1, 0);
    for (unsigned i = 0; i < 8u; ++i) {
        (void)exbus_sched_step(&s, i * 10000u);
        silence();
        (void)exbus_sched_step(&s, i * 10000u + 9000u);
    }
    CHECK_EQ(exbus_sched_step(&s, 80000), EXBUS_EV_SENT);
    reply_with(hex, at_us);
    return exbus_sched_step(&s, 89000);
}

TEST_CASE(a_bad_reply_is_refused_with_its_reason)
{
    /* One bit of the CRC. */
    CHECK_EQ(reply_8("3B 01 20 08 3A 18 9F 56 00 A4 51 55 EE 11 30 20 21 00"
                     " 40 34 A3 28 00 41 00 00 51 18 00 09 91 D7", 300),
             EXBUS_EV_BAD_REPLY);
    CHECK_EQ(exbus_sched_last_rx(&s), EXBUS_RX_CRC);
    CHECK_EQ(s.stats.bad[EXBUS_RX_CRC], 1u);
    CHECK(exbus_sched_reply(&s) == NULL);
    /* Cut after 20 bytes. */
    CHECK_EQ(reply_8("3B 01 20 08 3A 18 9F 56 00 A4 51 55 EE 11 30 20 21 00"
                     " 40 34", 300),
             EXBUS_EV_BAD_REPLY);
    CHECK_EQ(exbus_sched_last_rx(&s), EXBUS_RX_TRUNCATED);
    /* The reply to Packet_ID 6. */
    CHECK_EQ(reply_8("3B 01 08 06 3A 00 62 99", 300), EXBUS_EV_BAD_REPLY);
    CHECK_EQ(exbus_sched_last_rx(&s), EXBUS_RX_PACKET_ID);
    /* Noise only. */
    CHECK_EQ(reply_8("00 FF 00", 300), EXBUS_EV_BAD_REPLY);
    CHECK_EQ(exbus_sched_last_rx(&s), EXBUS_RX_HEADER);
}

TEST_CASE(a_reply_that_starts_after_the_window_is_late)
{
    /* The window is 4000 us: a start at 4000 is in it, 4001 is not. */
    CHECK_EQ(reply_8(SPEC_REPLY, 4000), EXBUS_EV_REPLY);
    CHECK_EQ(reply_8(SPEC_REPLY, 4001), EXBUS_EV_BAD_REPLY);
    CHECK_EQ(exbus_sched_last_rx(&s), EXBUS_RX_LATE);
    CHECK_EQ(s.stats.bad[EXBUS_RX_LATE], 1u);
    CHECK(exbus_sched_reply(&s) == NULL);
    /* Bytes skipped in front of the header do not move the start. */
    CHECK_EQ(reply_8("00 " SPEC_REPLY, 3950), EXBUS_EV_BAD_REPLY);
    CHECK_EQ(exbus_sched_last_rx(&s), EXBUS_RX_LATE);
}

TEST_CASE(a_framing_error_names_a_refused_reply)
{
    /* A low stop bit in a reply whose CRC fails is the reason given. */
    setup(16, 1, 0);
    for (unsigned i = 0; i < 8u; ++i) {
        (void)exbus_sched_step(&s, i * 10000u);
        silence();
        (void)exbus_sched_step(&s, i * 10000u + 9000u);
    }
    (void)exbus_sched_step(&s, 80000);
    reply_with(SPEC_REPLY, 300);
    fake.cap.bytes[10].value ^= 0x04;
    fake.cap.bytes[10].flags = EXBUS_RXB_FRAMING;
    CHECK_EQ(exbus_sched_step(&s, 89000), EXBUS_EV_BAD_REPLY);
    CHECK_EQ(exbus_sched_last_rx(&s), EXBUS_RX_FRAMING);
    /* A reply that passes its CRC passes, whatever a stop bit said. */
    for (unsigned i = 9; i < 264u; ++i) {
        CHECK_EQ(quiet_cycle(i * 10000u), EXBUS_EV_SENT);
    }
    CHECK(exbus_sched_set_channels(&s, ch, 16, 2640000));
    CHECK_EQ(exbus_sched_step(&s, 2640000), EXBUS_EV_SENT);
    CHECK_EQ(fake.tx[3], 8);
    reply_with(SPEC_REPLY, 300);
    fake.cap.bytes[31].flags = EXBUS_RXB_FRAMING;
    CHECK_EQ(exbus_sched_step(&s, 2649000), EXBUS_EV_REPLY);
}

TEST_CASE(an_overflowed_capture_is_counted)
{
    CHECK_EQ(reply_8(SPEC_REPLY, 300), EXBUS_EV_REPLY);
    (void)exbus_sched_step(&s, 90000);
    silence();
    fake.cap.overflow = 1;
    CHECK_EQ(exbus_sched_step(&s, 99000), EXBUS_EV_NO_REPLY);
    CHECK_EQ(s.stats.overflow, 1u);
}

TEST_CASE(stop_during_a_request_cuts_it_and_sends_nothing_more)
{
    setup(16, 1, 0);
    CHECK_EQ(exbus_sched_step(&s, 0), EXBUS_EV_SENT);
    CHECK(fake.busy);
    exbus_sched_stop(&s);
    CHECK(!exbus_sched_running(&s));
    CHECK_EQ(fake.aborts, 1u);
    CHECK(!fake.busy);
    /* A stop on a stopped scheduler does nothing more. */
    exbus_sched_stop(&s);
    CHECK_EQ(fake.aborts, 1u);
    /* The reply that would have come is never read, and nothing is sent
     * however long the steps go on. */
    fake.polls = 0;
    for (uint32_t t = 1000; t < 200000u; t += 1000u) {
        CHECK_EQ(exbus_sched_step(&s, t), 0u);
    }
    CHECK_EQ(fake.starts, 1u);
    CHECK_EQ(fake.polls, 0u);
    CHECK_EQ(s.stats.no_reply, 0u);
    CHECK(exbus_sched_reply(&s) == NULL);
    /* A start needs fresh values; the stream goes on with Packet_ID 1. */
    CHECK(!exbus_sched_start(&s, 200000));
    CHECK(exbus_sched_set_channels(&s, ch, 16, 200000));
    CHECK(exbus_sched_start(&s, 200000));
    CHECK_EQ(exbus_sched_step(&s, 200000), EXBUS_EV_SENT);
    CHECK_EQ(fake.tx[3], 1);
    /* A second start while running changes nothing. */
    CHECK(exbus_sched_start(&s, 201000));
    CHECK_EQ(s.next_us, 210000u);
}

TEST_CASE(stop_between_cycles_sends_nothing_more)
{
    setup(16, 1, 0);
    CHECK_EQ(exbus_sched_step(&s, 0), EXBUS_EV_SENT);
    silence();
    CHECK_EQ(exbus_sched_step(&s, 9000), EXBUS_EV_NO_REPLY);
    exbus_sched_stop(&s);
    CHECK_EQ(fake.aborts, 0u);
    CHECK_EQ(exbus_sched_step(&s, 10000), 0u);
    CHECK_EQ(exbus_sched_step(&s, 50000), 0u);
    CHECK_EQ(fake.starts, 1u);
}

TEST_CASE(channel_values_older_than_the_hold_time_stop_the_stream)
{
    unsigned ev;

    setup(16, 0, 0);
    for (unsigned i = 0; i < 10u; ++i) {
        CHECK_EQ(exbus_sched_step(&s, i * 10000u), EXBUS_EV_SENT);
        silence();
        (void)exbus_sched_step(&s, i * 10000u + 9000u);
    }
    /* Set at 0, 100 ms of hold: the cycle due at 100000 is not sent. */
    ev = exbus_sched_step(&s, 100000);
    CHECK_EQ(ev, EXBUS_EV_STALE);
    CHECK(!exbus_sched_running(&s));
    CHECK_EQ(s.stats.stale, 1u);
    CHECK_EQ(exbus_sched_step(&s, 110000), 0u);
    CHECK_EQ(fake.starts, 10u);
    /* Fresh values every cycle keep it going. */
    CHECK(exbus_sched_set_channels(&s, ch, 16, 120000));
    CHECK(exbus_sched_start(&s, 120000));
    for (unsigned i = 0; i < 30u; ++i) {
        CHECK_EQ(quiet_cycle(120000u + i * 10000u), EXBUS_EV_SENT);
    }
    CHECK_EQ(s.stats.stale, 1u);
}

TEST_CASE(a_hold_time_of_0_never_stops_the_stream)
{
    exbus_cfg_t c;
    const exbus_driver_t d = driver();

    memset(&fake, 0, sizeof(fake));
    exbus_cfg_default(&c);
    c.hold_us = 0;
    CHECK_EQ(exbus_sched_init(&s, &d, &c), EXBUS_CFG_OK);
    CHECK(exbus_sched_set_channels(&s, ch, 16, 0));
    CHECK(exbus_sched_start(&s, 5000000));
    CHECK_EQ(exbus_sched_step(&s, 5000000), EXBUS_EV_SENT);
}

TEST_CASE(a_driver_that_never_ends_is_cut_and_the_stream_goes_on)
{
    setup(16, 1, 0);
    CHECK_EQ(exbus_sched_step(&s, 0), EXBUS_EV_SENT);
    /* 48 bytes out, 49 in with 2 byte times of quiet after each, the 4 ms
     * window: 15600 + 4000 us; 5 ms more. */
    CHECK_EQ(s.budget_us, 19600u);
    CHECK_EQ(exbus_sched_step(&s, 24599), 0u);
    CHECK_EQ(fake.aborts, 0u);
    CHECK_EQ(exbus_sched_step(&s, 24600), EXBUS_EV_CUT | EXBUS_EV_SENT);
    CHECK_EQ(fake.aborts, 1u);
    CHECK_EQ(s.stats.cut, 1u);
    CHECK_EQ(s.stats.overflow, 0u);
    CHECK_EQ(fake.starts, 2u);
    /* The cycle due at 10000 went out at 24600, more than a period late:
     * the schedule restarts from there. */
    CHECK_EQ(s.next_us, 34600u);
    CHECK_EQ(s.stats.late, 1u);
}

TEST_CASE(a_refused_start_is_tried_again_at_the_next_step)
{
    setup(16, 1, 0);
    fake.refuse = true;
    CHECK_EQ(exbus_sched_step(&s, 0), EXBUS_EV_REFUSED);
    CHECK_EQ(exbus_sched_step(&s, 500), EXBUS_EV_REFUSED);
    CHECK_EQ(s.stats.refused, 2u);
    CHECK_EQ(s.stats.cycles, 0u);
    fake.refuse = false;
    CHECK_EQ(exbus_sched_step(&s, 1000), EXBUS_EV_SENT);
    /* The refused cycles took no Packet_ID. */
    CHECK_EQ(fake.tx[3], 0);
    CHECK_EQ(s.next_us, 10000u);
}

TEST_CASE(a_step_a_period_late_restarts_the_schedule)
{
    setup(16, 1, 0);
    CHECK_EQ(exbus_sched_step(&s, 0), EXBUS_EV_SENT);
    silence();
    (void)exbus_sched_step(&s, 9000);
    /* Due at 10000, stepped at 20000: one late cycle, then 30000. */
    CHECK(exbus_sched_set_channels(&s, ch, 16, 20000));
    CHECK_EQ(exbus_sched_step(&s, 20000), EXBUS_EV_SENT);
    CHECK_EQ(s.stats.late, 1u);
    CHECK_EQ(s.next_us, 30000u);
    silence();
    (void)exbus_sched_step(&s, 29000);
    /* Stepped 9999 us late: not a restart; the next one keeps the grid. */
    CHECK_EQ(exbus_sched_step(&s, 39999), EXBUS_EV_SENT);
    CHECK_EQ(s.stats.late, 1u);
    CHECK_EQ(s.next_us, 40000u);
}

TEST_CASE(channel_values_are_clamped_and_counted)
{
    const uint16_t v[4] = { 0, 12000, 30000, 3200 };

    setup(4, 0, 0);
    CHECK(exbus_sched_set_channels(&s, v, 4, 0));
    CHECK_EQ(s.stats.clamped, 2u);
    CHECK_EQ(exbus_sched_step(&s, 0), EXBUS_EV_SENT);
    CHECK_EQ(fake.tx[6] | (fake.tx[7] << 8), 3200);
    CHECK_EQ(fake.tx[8] | (fake.tx[9] << 8), 12000);
    CHECK_EQ(fake.tx[10] | (fake.tx[11] << 8), 20000);
    CHECK_EQ(fake.tx[12] | (fake.tx[13] << 8), 3200);
}

TEST_CASE(now_us_may_wrap)
{
    const uint32_t t0 = 0xFFFFF000u;

    setup(16, 1, t0);
    CHECK_EQ(exbus_sched_step(&s, t0), EXBUS_EV_SENT);
    silence();
    CHECK_EQ(exbus_sched_step(&s, t0 + 9000u), EXBUS_EV_NO_REPLY);
    CHECK_EQ(exbus_sched_step(&s, t0 + 9999u), 0u);
    CHECK_EQ(exbus_sched_step(&s, t0 + 10000u), EXBUS_EV_SENT);
    CHECK_EQ(s.stats.late, 0u);
    /* The cut across the wrap. */
    CHECK_EQ(exbus_sched_step(&s, t0 + 10000u + 24599u), 0u);
    CHECK_EQ(exbus_sched_step(&s, t0 + 10000u + 24600u),
             EXBUS_EV_CUT | EXBUS_EV_SENT);
}

int main(void)
{
    RUN(the_default_is_125_kbaud_10_ms_16_channels_and_a_request_a_cycle);
    RUN(each_configuration_error_has_its_code);
    RUN(init_takes_nothing_it_refuses);
    RUN(nothing_is_sent_before_a_start_or_without_channel_values);
    RUN(a_cycle_is_channels_with_0x03_and_a_request_with_the_same_id);
    RUN(without_requests_the_channel_packet_releases_with_0x01);
    RUN(one_request_every_third_cycle);
    RUN(twenty_four_channels_and_a_request_are_64_bytes);
    RUN(the_packet_id_counts_up_and_wraps_and_the_spec_reply_passes);
    RUN(an_empty_reply_passes_and_is_counted);
    RUN(a_bad_reply_is_refused_with_its_reason);
    RUN(a_reply_that_starts_after_the_window_is_late);
    RUN(a_framing_error_names_a_refused_reply);
    RUN(an_overflowed_capture_is_counted);
    RUN(stop_during_a_request_cuts_it_and_sends_nothing_more);
    RUN(stop_between_cycles_sends_nothing_more);
    RUN(channel_values_older_than_the_hold_time_stop_the_stream);
    RUN(a_hold_time_of_0_never_stops_the_stream);
    RUN(a_driver_that_never_ends_is_cut_and_the_stream_goes_on);
    RUN(a_refused_start_is_tried_again_at_the_next_step);
    RUN(a_step_a_period_late_restarts_the_schedule);
    RUN(channel_values_are_clamped_and_counted);
    RUN(now_us_may_wrap);
    return test_summary("exbus_sched");
}
