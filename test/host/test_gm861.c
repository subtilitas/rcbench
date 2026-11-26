/**
 * @file
 * @brief The GM861 command protocol and its sequencer (gm861.h) against a
 *        simulated module.
 *
 * The failures this is about: a CRC that differs from the module's, so
 * every frame goes unanswered; a module at another rate never found; a
 * module written to flash at every start; a setting that does not stick
 * taken as set; a lost answer or a lost heartbeat that leaves the bench
 * waiting for ever.  The simulated module below answers the way the
 * manual's chapter 10 states, with its own frame code; the expected frames
 * are the manual's printed bytes.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include <string.h>

#include "gm861.h"
#include "scan_line.h"

/** @brief The manual's CRC routine (10.1), as printed. */
static uint16_t ref_crc(const uint8_t *p, size_t n)
{
    unsigned crc = 0u;
    while (n-- != 0u) {
        for (unsigned i = 0x80u; i != 0u; i /= 2u) {
            crc *= 2u;
            if ((crc & 0x10000u) != 0u) {
                crc ^= 0x11021u;
            }
            if ((*p & i) != 0u) {
                crc ^= 0x1021u;
            }
        }
        ++p;
    }
    return (uint16_t)crc;
}

/* --- the simulated module ---------------------------------------------- */

/** @brief A GM861 on the other end of the UART. */
typedef struct {
    uint8_t  ram[256];
    uint8_t  flash[256];
    uint32_t baud;          /**< the rate it listens and talks at */
    bool     mute;          /**< answers nothing */
    bool     nak;           /**< answers a write with data 01 */
    uint16_t stuck;         /**< a zone that ignores writes; 0xFFFF none */
    uint16_t flaky;         /**< a zone that ignores its first flaky_n writes; 0xFFFF none */
    unsigned flaky_n;       /**< writes to @c flaky still to be ignored */
    unsigned drop_from;     /**< the first request whose answer is lost (1 = first) */
    unsigned drop_count;    /**< answers lost from drop_from on */
    unsigned drop_beats;    /**< heartbeat answers still to be lost */
    unsigned requests, reads, writes, saves, beats, resets;
    uint16_t written[64];   /**< zones written, in order */
    uint8_t  out[256];      /**< bytes on their way to the bench */
    size_t   out_n;
} sim_t;

/** @brief Symbology zones (bit 0) of the stock module that read codes. */
static const uint8_t STOCK_ON[] = { 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x55 };

/** @brief A module as it may come: USB keyboard, continuous, tail CR, Data Matrix on. */
static void sim_stock(sim_t *m)
{
    memset(m->ram, 0, sizeof(m->ram));
    m->ram[0x00] = 0xD6u;   /* LED, sound, light, continuous (appendix B) */
    m->ram[0x03] = 0x01u;   /* setup code content sent */
    m->ram[0x0D] = 0x31u;   /* UTF-8 in, GBK out, USB keyboard */
    m->ram[0x13] = 0x85u;   /* same code again after 0.5s */
    m->ram[0x2A] = 0x39u;   /* 9600 baud */
    m->ram[0x2B] = 0x01u;
    m->ram[0x3F] = 0x01u;   /* QR on */
    for (size_t i = 0; i < sizeof(STOCK_ON); ++i) {
        m->ram[STOCK_ON[i]] = 0x01u;
    }
    m->ram[0x54] = 0x01u;   /* Data Matrix on */
    m->ram[0x60] = 0x01u;   /* tail CR */
    m->ram[0xB0] = 0x02u;   /* end part cut out */
    memcpy(m->flash, m->ram, sizeof(m->ram));
}

static void sim_init(sim_t *m, uint32_t baud)
{
    memset(m, 0, sizeof(*m));
    m->baud = baud;
    m->stuck = 0xFFFFu;
    m->flaky = 0xFFFFu;
    sim_stock(m);
}

/** @brief Queue an answer 02 (or 03) 00 00 len data CRC. */
static void sim_answer(sim_t *m, uint8_t head, const uint8_t *data, size_t n)
{
    uint8_t *o = &m->out[m->out_n];
    o[0] = head;
    o[1] = 0x00u;
    o[2] = 0x00u;
    o[3] = (uint8_t)n;
    memcpy(&o[4], data, n);
    const uint16_t crc = ref_crc(&o[2], n + 2u);
    o[4u + n] = (uint8_t)(crc >> 8);
    o[5u + n] = (uint8_t)(crc & 0xFFu);
    m->out_n += n + 6u;
}

/** @brief The module takes the bench's bytes at the bench's @p baud. */
static void sim_rx(sim_t *m, const uint8_t *b, size_t n, uint32_t baud)
{
    const uint8_t zero = 0u;
    if (baud != m->baud || n != 9u || b[0] != 0x7Eu || b[1] != 0x00u
        || ref_crc(&b[2], 5u) != (uint16_t)((b[7] << 8) | b[8])) {
        return;
    }
    ++m->requests;
    const uint16_t addr = (uint16_t)((b[4] << 8) | b[5]);
    const bool beat = b[2] == 0x0Au;
    bool lost = m->mute
                || (m->requests >= m->drop_from
                    && m->requests < m->drop_from + m->drop_count);
    if (beat && m->drop_beats > 0u) {
        --m->drop_beats;
        lost = true;
    }
    switch (b[2]) {
    case 0x07u:
        ++m->reads;
        if (!lost) {
            sim_answer(m, 0x02u, &m->ram[addr], b[6]);
        }
        return;
    case 0x08u:
        ++m->writes;
        if (m->writes <= 64u) {
            m->written[m->writes - 1u] = addr;
        }
        if (addr == 0x00D9u && b[6] == 0x50u) {
            ++m->resets;
            sim_stock(m);
        } else if (addr == m->flaky && m->flaky_n > 0u) {
            --m->flaky_n;
        } else if (addr != m->stuck) {
            m->ram[addr] = b[6];
        }
        break;
    case 0x09u:
        ++m->saves;
        memcpy(m->flash, m->ram, sizeof(m->ram));
        break;
    case 0x0Au:
        ++m->beats;
        break;
    default:
        return;
    }
    if (!lost) {
        const uint8_t one = 1u;
        sim_answer(m, beat ? 0x03u : 0x02u,
                   m->nak && b[2] == 0x08u ? &one : &zero, 1u);
    }
}

/* --- the rig: sequencer, framer and module on one clock ------------------ */

typedef struct {
    gm861_t     g;
    scan_line_t f;
    sim_t       m;
    uint32_t    t;
    bool        unplugged;
    unsigned    sent;
    uint8_t     last[GM861_REQ_LEN];
    uint32_t    bauds[16];   /**< the rate of each of the first 16 sends */
    bool        saw_usb;
} rig_t;

static void rig_init(rig_t *r, uint32_t baud, uint32_t t0)
{
    memset(r, 0, sizeof(*r));
    gm861_init(&r->g, GM861_MODE_SENSING);
    scan_line_init(&r->f);
    sim_init(&r->m, baud);
    r->t = t0;
}

/** @brief One 20ms tick of the panel's scan task. */
static void tick(rig_t *r)
{
    uint8_t tx[16];
    const size_t n = gm861_step(&r->g, r->t, !r->unplugged, tx, sizeof(tx));
    if (n > 0u) {
        CHECK_EQ(n, GM861_REQ_LEN);
        memcpy(r->last, tx, GM861_REQ_LEN);
        if (r->sent < 16u) {
            r->bauds[r->sent] = gm861_baud(&r->g);
        }
        ++r->sent;
        if (!r->unplugged) {
            sim_rx(&r->m, tx, n, gm861_baud(&r->g));
        }
    }
    for (size_t i = 0; i < r->m.out_n; ++i) {
        if (scan_line_feed(&r->f, r->m.out[i], r->t) == SCAN_OUT_FRAME) {
            size_t len = 0u;
            const uint8_t *fr = scan_line_frame(&r->f, &len);
            gm861_answer(&r->g, fr, len);
        }
    }
    r->m.out_n = 0u;
    if (gm861_state(&r->g) == GM861_USB_OUTPUT) {
        r->saw_usb = true;
    }
    r->t += 20u;
}

static void run_ms(rig_t *r, uint32_t ms)
{
    for (uint32_t i = 0; i < ms / 20u; ++i) {
        tick(r);
    }
}

/** @brief Tick until @p state; return the ms it took, or UINT32_MAX. */
static uint32_t run_until(rig_t *r, gm861_state_t state, uint32_t max_ms)
{
    for (uint32_t ms = 0u; ms <= max_ms; ms += 20u) {
        if (gm861_state(&r->g) == state) {
            return ms;
        }
        tick(r);
    }
    return UINT32_MAX;
}

/** @brief The stock module's zones as the bench must leave them. */
static void check_set(const uint8_t *z, bool sensing)
{
    CHECK_EQ(z[0x00], sensing ? 0xD7u : 0xD6u);
    CHECK_EQ(z[0x03], 0x00u);
    CHECK_EQ(z[0x0D], 0x3Cu);
    CHECK_EQ(z[0x13], 0x9Eu);
    CHECK_EQ(z[0x3F], 0x01u);
    CHECK_EQ(z[0x54], 0x00u);
    CHECK_EQ(z[0x60], 0x21u);
    CHECK_EQ(z[0xB0], 0x00u);
    for (size_t i = 0; i < sizeof(STOCK_ON); ++i) {
        CHECK_EQ(z[STOCK_ON[i]], 0x00u);
    }
    CHECK_EQ(z[0x2A], 0x39u);   /* the rate is left as found */
    CHECK_EQ(z[0x2B], 0x01u);
}

/* --- frames and CRC ---------------------------------------------------- */

TEST_CASE(the_crc_reproduces_the_manuals_examples)
{
    /* The 5 frames chapter 10 prints with a CRC: read zone 0x000A, its
     * answer, write zone 0x000A, save, factory reset; then the write
     * answer, the heartbeat, appendix B's rate frames. */
    const uint8_t read_a[] = { 0x07, 0x01, 0x00, 0x0A, 0x01 };
    const uint8_t read_ans[] = { 0x00, 0x01, 0x3E };
    const uint8_t write_a[] = { 0x08, 0x01, 0x00, 0x0A, 0x3E };
    const uint8_t save[] = { 0x09, 0x01, 0x00, 0x00, 0x00 };
    const uint8_t reset[] = { 0x08, 0x01, 0x00, 0xD9, 0x50 };
    const uint8_t ack[] = { 0x00, 0x01, 0x00 };
    const uint8_t beat[] = { 0x0A, 0x01, 0x00, 0x00, 0x00 };
    const uint8_t rate[] = { 0x08, 0x02, 0x00, 0x2A, 0x39, 0x01 };
    const uint8_t find[] = { 0x07, 0x01, 0x00, 0x2A, 0x02 };
    CHECK_EQ(gm861_crc(read_a, sizeof(read_a)), 0xEE8Au);
    CHECK_EQ(gm861_crc(read_ans, sizeof(read_ans)), 0xE4ACu);
    CHECK_EQ(gm861_crc(write_a, sizeof(write_a)), 0x4CCFu);
    CHECK_EQ(gm861_crc(save, sizeof(save)), 0xDEC8u);
    CHECK_EQ(gm861_crc(reset, sizeof(reset)), 0x81D3u);
    CHECK_EQ(gm861_crc(ack, sizeof(ack)), 0x3331u);
    CHECK_EQ(gm861_crc(beat, sizeof(beat)), 0x301Au);
    CHECK_EQ(gm861_crc(rate, sizeof(rate)), 0xA7EAu);
    CHECK_EQ(gm861_crc(find, sizeof(find)), 0xD80Fu);
    CHECK_EQ(gm861_crc(NULL, 0u), 0u);
}

TEST_CASE(the_crc_matches_the_manuals_routine_on_every_short_input)
{
    uint8_t b[3];
    for (unsigned v = 0u; v < 0x10000u; v += 7u) {
        b[0] = (uint8_t)(v >> 8);
        b[1] = (uint8_t)v;
        b[2] = (uint8_t)(v * 13u);
        CHECK_EQ(gm861_crc(b, 3u), ref_crc(b, 3u));
        CHECK_EQ(gm861_crc(b, 1u), ref_crc(b, 1u));
    }
}

/** @brief gm861_request() gives exactly @p want. */
static void check_request(uint8_t type, uint16_t addr, uint8_t data,
                          const uint8_t want[GM861_REQ_LEN])
{
    uint8_t out[GM861_REQ_LEN];
    CHECK_EQ(gm861_request(out, type, addr, data), GM861_REQ_LEN);
    CHECK(memcmp(out, want, GM861_REQ_LEN) == 0);
}

TEST_CASE(each_request_frame_is_the_manuals)
{
    const uint8_t read_a[] = { 0x7E, 0x00, 0x07, 0x01, 0x00, 0x0A, 0x01, 0xEE, 0x8A };
    const uint8_t write_a[] = { 0x7E, 0x00, 0x08, 0x01, 0x00, 0x0A, 0x3E, 0x4C, 0xCF };
    const uint8_t save[] = { 0x7E, 0x00, 0x09, 0x01, 0x00, 0x00, 0x00, 0xDE, 0xC8 };
    const uint8_t beat[] = { 0x7E, 0x00, 0x0A, 0x01, 0x00, 0x00, 0x00, 0x30, 0x1A };
    const uint8_t reset[] = { 0x7E, 0x00, 0x08, 0x01, 0x00, 0xD9, 0x50, 0x81, 0xD3 };
    const uint8_t probe[] = { 0x7E, 0x00, 0x07, 0x01, 0x00, 0x00, 0x01, 0x01, 0x41 };
    const uint8_t serial[] = { 0x7E, 0x00, 0x08, 0x01, 0x00, 0x0D, 0x3C, 0xF5, 0x1A };
    const uint8_t tail[] = { 0x7E, 0x00, 0x08, 0x01, 0x00, 0x60, 0x21, 0x4B, 0xF0 };
    check_request(GM861_T_READ, 0x000Au, 0x01u, read_a);
    check_request(GM861_T_WRITE, 0x000Au, 0x3Eu, write_a);
    check_request(GM861_T_SAVE, 0x0000u, 0x00u, save);
    check_request(GM861_T_HEARTBEAT, 0x0000u, 0x00u, beat);
    check_request(GM861_T_WRITE, 0x00D9u, 0x50u, reset);
    check_request(GM861_T_READ, 0x0000u, 0x01u, probe);
    check_request(GM861_T_WRITE, 0x000Du, 0x3Cu, serial);
    check_request(GM861_T_WRITE, 0x0060u, 0x21u, tail);
}

TEST_CASE(an_answer_is_checked_for_head_type_length_and_crc)
{
    const uint8_t *data = NULL;
    uint8_t a[] = { 0x02, 0x00, 0x00, 0x01, 0x3E, 0xE4, 0xAC };
    CHECK_EQ(gm861_answer_data(a, sizeof(a), &data), 1);
    CHECK_EQ(data[0], 0x3Eu);
    const uint8_t b[] = { 0x03, 0x00, 0x00, 0x01, 0x00, 0x33, 0x31 };
    CHECK_EQ(gm861_answer_data(b, sizeof(b), NULL), 1);
    CHECK_EQ(gm861_answer_data(a, sizeof(a) - 1u, NULL), -1);
    CHECK_EQ(gm861_answer_data(NULL, 7u, NULL), -1);
    for (size_t i = 0; i < sizeof(a); ++i) {
        uint8_t c[sizeof(a)];
        memcpy(c, a, sizeof(a));
        c[i] ^= 0x04u;
        CHECK_EQ(gm861_answer_data(c, sizeof(c), NULL), -1);
    }
}

TEST_CASE(the_wanted_zones_are_plan_bs_table)
{
    CHECK_EQ(gm861_zone_want(0x0000u, 0x00u, GM861_MODE_SENSING), 0x83u);
    CHECK_EQ(gm861_zone_want(0x0000u, 0x7Cu, GM861_MODE_CONTINUOUS), 0xFEu);
    CHECK_EQ(gm861_zone_want(0x0003u, 0xFFu, GM861_MODE_SENSING), 0xFEu);
    CHECK_EQ(gm861_zone_want(0x000Du, 0xF3u, GM861_MODE_SENSING), 0xFCu);
    CHECK_EQ(gm861_zone_want(0x0013u, 0x00u, GM861_MODE_SENSING), 0x9Eu);
    CHECK_EQ(gm861_zone_want(0x003Fu, 0x00u, GM861_MODE_SENSING), 0x01u);
    CHECK_EQ(gm861_zone_want(0x0054u, 0xFFu, GM861_MODE_SENSING), 0xFCu);
    CHECK_EQ(gm861_zone_want(0x0060u, 0xFFu, GM861_MODE_SENSING), 0x21u);
    CHECK_EQ(gm861_zone_want(0x00B0u, 0x03u, GM861_MODE_SENSING), 0x00u);
    const uint16_t off[] = { 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x36, 0x39,
                             0x3C, 0x40, 0x43, 0x46, 0x49, 0x4C, 0x4F, 0x50,
                             0x51, 0x55, 0x56, 0x57, 0x5C, 0x5F, 0x26, 0x29 };
    unsigned held = 0u;
    for (size_t i = 0; i < sizeof(off) / sizeof(off[0]); ++i) {
        CHECK_EQ(gm861_zone_want(off[i], 0xFFu, GM861_MODE_SENSING), 0xFEu);
    }
    for (uint16_t a = 0u; a < 0x100u; ++a) {
        if (gm861_zone_want(a, 0x5Au, GM861_MODE_SENSING) != 0x5Au) {
            ++held;
        }
    }
    /* 0x5A has bits 0 and 1 clear and bit 3 set: 0x0000, 0x000D, 0x0013,
     * 0x003F, 0x0054, 0x0060 and 0x00B0 change it; 0x0003 and the 24
     * symbologies clear bit 0 only. */
    CHECK_EQ(held, 7u);
    CHECK_EQ(gm861_zone_want(0x002Au, 0x39u, GM861_MODE_SENSING), 0x39u);
    CHECK_EQ(gm861_rates()[0], 9600u);
    CHECK_EQ(gm861_rates()[GM861_RATES - 1u], 1200u);
}

/* --- the sequence ------------------------------------------------------ */

TEST_CASE(the_module_is_found_at_each_of_the_8_rates_and_set)
{
    for (unsigned i = 0u; i < GM861_RATES; ++i) {
        rig_t r;
        const uint32_t baud = gm861_rates()[i];
        rig_init(&r, baud, 1000u);
        CHECK(run_until(&r, GM861_READY, 6000u) != UINT32_MAX);
        CHECK_EQ(gm861_baud(&r.g), baud);
        /* One probe at each rate before it, in the table's order. */
        for (unsigned k = 0u; k <= i; ++k) {
            CHECK_EQ(r.bauds[k], gm861_rates()[k]);
        }
        check_set(r.m.ram, true);
        check_set(r.m.flash, true);
        CHECK_EQ(r.m.saves, 1u);
        CHECK_EQ(r.m.writes, 13u);
        CHECK_EQ(gm861_bad_zone(&r.g), GM861_NO_ZONE);
        uint8_t cut = 0u;
        const uint8_t *z = gm861_found(&r.g, &cut);
        CHECK_EQ(z[0x0D], 0x31u);           /* as read before the writes */
        CHECK_EQ(cut, 0x02u);
    }
}

TEST_CASE(nothing_is_sent_before_300ms_and_rx_low_is_not_connected)
{
    rig_t r;
    rig_init(&r, 9600u, 0u);
    r.unplugged = true;
    run_ms(&r, 300u);
    CHECK_EQ(r.sent, 0u);
    CHECK_EQ(gm861_state(&r.g), GM861_NOT_CONNECTED);
    run_ms(&r, 20000u);
    CHECK_EQ(r.sent, 0u);
    CHECK_EQ(gm861_state(&r.g), GM861_NOT_CONNECTED);

    /* Plugged in: found at the next try, within 5s. */
    r.unplugged = false;
    CHECK(run_until(&r, GM861_READY, GM861_RETRY_MS + 1000u) != UINT32_MAX);
}

TEST_CASE(rx_high_and_a_mute_module_is_no_answer_and_tried_again_after_5s)
{
    rig_t r;
    rig_init(&r, 9600u, 0u);
    r.m.mute = true;
    const uint32_t ms = run_until(&r, GM861_NO_ANSWER, 6000u);
    CHECK(ms != UINT32_MAX);
    CHECK(ms >= 300u + GM861_RATES * GM861_ANSWER_MS);
    CHECK_EQ(r.sent, GM861_RATES);
    for (unsigned k = 0u; k < GM861_RATES; ++k) {
        CHECK_EQ(r.bauds[k], gm861_rates()[k]);
    }
    run_ms(&r, GM861_RETRY_MS - 100u);
    CHECK_EQ(r.sent, GM861_RATES);          /* nothing during the wait */
    run_ms(&r, 200u);
    CHECK_EQ(r.sent, GM861_RATES + 1u);     /* the next try started */
    CHECK_EQ(gm861_state(&r.g), GM861_NO_ANSWER);

    r.m.mute = false;
    CHECK(run_until(&r, GM861_READY, 10000u) != UINT32_MAX);
}

TEST_CASE(usb_output_at_01_and_11_is_shown_and_set_to_serial)
{
    const uint8_t usb[2] = { 0x31u, 0x33u };
    for (unsigned i = 0u; i < 2u; ++i) {
        rig_t r;
        rig_init(&r, 9600u, 0u);
        r.m.ram[0x0D] = usb[i];
        CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
        CHECK(r.saw_usb);
        CHECK_EQ(r.m.ram[0x0D] & 0x03u, 0x00u);
        CHECK_EQ(r.m.flash[0x0D] & 0x03u, 0x00u);
    }
    /* Already serial: no USB OUTPUT on the way. */
    rig_t r;
    rig_init(&r, 9600u, 0u);
    r.m.ram[0x0D] = 0x30u;
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    CHECK(!r.saw_usb);
}

TEST_CASE(a_module_with_every_zone_as_wanted_takes_no_write_and_no_save)
{
    rig_t r;
    rig_init(&r, 9600u, 0u);
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    /* A restart of the panel: the module keeps its flash. */
    sim_t kept = r.m;
    rig_init(&r, 9600u, 50000u);
    memcpy(r.m.ram, kept.flash, sizeof(r.m.ram));
    memcpy(r.m.flash, kept.flash, sizeof(r.m.flash));
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    CHECK_EQ(r.m.writes, 0u);
    CHECK_EQ(r.m.saves, 0u);
    CHECK_EQ(r.m.reads, 3u);                /* probe, the block, 0x00B0 */
}

TEST_CASE(a_write_that_does_not_stick_is_named_and_not_saved)
{
    rig_t r;
    rig_init(&r, 9600u, 0u);
    r.m.stuck = 0x0060u;
    CHECK(run_until(&r, GM861_NO_ANSWER, 3000u) != UINT32_MAX);
    CHECK_EQ(gm861_bad_zone(&r.g), 0x0060u);
    CHECK_EQ(r.m.saves, 0u);
    unsigned tail = 0u;
    for (unsigned i = 0u; i < r.m.writes; ++i) {
        tail += r.m.written[i] == 0x0060u ? 1u : 0u;
    }
    CHECK_EQ(tail, 2u);
    CHECK_EQ(r.m.writes, 14u);              /* 13, then 0x0060 again */
    /* Held: no flash write and no frame every few seconds. */
    const unsigned sent = r.sent;
    run_ms(&r, 60000u);
    CHECK_EQ(r.sent, sent);
    CHECK_EQ(gm861_state(&r.g), GM861_NO_ANSWER);

    r.m.stuck = 0xFFFFu;
    gm861_recheck(&r.g);
    CHECK_EQ(gm861_bad_zone(&r.g), GM861_NO_ZONE);
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    CHECK_EQ(r.m.saves, 1u);
    check_set(r.m.flash, true);
}

TEST_CASE(an_answer_lost_mid_sequence_is_asked_again)
{
    /* Requests 2 to 19: the block, 0x00B0, 13 writes, the read back of
     * both, the save.  Request 1 is the probe, whose miss moves to the next
     * rate. */
    for (unsigned at = 2u; at <= 19u; ++at) {
        rig_t r;
        rig_init(&r, 9600u, 0u);
        r.m.drop_from = at;
        r.m.drop_count = 1u;
        CHECK(run_until(&r, GM861_READY, 4000u) != UINT32_MAX);
        check_set(r.m.flash, true);
        /* A lost save answer sends the save once more. */
        CHECK_EQ(r.m.saves, at == 19u ? 2u : 1u);
        CHECK_EQ(r.m.writes, at >= 4u && at <= 16u ? 14u : 13u);
    }
}

TEST_CASE(two_answers_lost_in_a_row_end_the_pass_and_it_is_tried_again)
{
    rig_t r;
    rig_init(&r, 9600u, 0u);
    r.m.drop_from = 5u;                     /* the second write */
    r.m.drop_count = 2u;
    CHECK(run_until(&r, GM861_NO_ANSWER, 3000u) != UINT32_MAX);
    CHECK_EQ(r.m.saves, 0u);
    CHECK(run_until(&r, GM861_READY, GM861_RETRY_MS + 2000u) != UINT32_MAX);
    check_set(r.m.flash, true);
}

TEST_CASE(a_last_write_applied_with_both_answers_lost_is_saved_on_the_retry)
{
    /* Request 16 is the last of the 13 writes (zone 0x00B0), 17 its
     * resend.  The module applies both; both answers are lost.  The retry
     * reads every zone as wanted, and the save must still go. */
    rig_t r;
    rig_init(&r, 9600u, 0u);
    r.m.drop_from = 16u;
    r.m.drop_count = 2u;
    CHECK(run_until(&r, GM861_NO_ANSWER, 3000u) != UINT32_MAX);
    CHECK_EQ(r.m.saves, 0u);
    CHECK_EQ(r.m.ram[0xB0], 0x00u);         /* applied in RAM */
    CHECK_EQ(r.m.flash[0xB0], 0x02u);       /* not in flash yet */
    CHECK(run_until(&r, GM861_READY, GM861_RETRY_MS + 2000u) != UINT32_MAX);
    CHECK_EQ(r.m.writes, 14u);              /* no write on the retry */
    CHECK_EQ(r.m.saves, 1u);
    check_set(r.m.flash, true);
}

TEST_CASE(a_save_whose_answers_are_lost_is_sent_again_on_the_retry)
{
    /* Request 19 is the save, 20 its resend: both answers lost. */
    rig_t r;
    rig_init(&r, 9600u, 0u);
    r.m.drop_from = 19u;
    r.m.drop_count = 2u;
    CHECK(run_until(&r, GM861_NO_ANSWER, 3000u) != UINT32_MAX);
    CHECK_EQ(r.m.saves, 2u);
    CHECK(run_until(&r, GM861_READY, GM861_RETRY_MS + 2000u) != UINT32_MAX);
    CHECK_EQ(r.m.saves, 3u);
    /* Answered: the next recheck with nothing changed saves nothing. */
    gm861_recheck(&r.g);
    run_ms(&r, 100u);
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    CHECK_EQ(r.m.saves, 3u);
}

TEST_CASE(a_write_unsaved_through_mute_retries_is_saved_when_the_module_answers)
{
    /* The last write and its resend lose their answers, then the module
     * goes mute through the retries; when it answers again the save goes. */
    rig_t r;
    rig_init(&r, 9600u, 0u);
    r.m.drop_from = 16u;
    r.m.drop_count = 2u;
    CHECK(run_until(&r, GM861_NO_ANSWER, 3000u) != UINT32_MAX);
    r.m.mute = true;
    run_ms(&r, 2u * GM861_RETRY_MS + 6000u);
    CHECK_EQ(r.m.saves, 0u);
    r.m.mute = false;
    CHECK(run_until(&r, GM861_READY, GM861_RETRY_MS + 4000u) != UINT32_MAX);
    CHECK_EQ(r.m.saves, 1u);
    check_set(r.m.flash, true);
}

TEST_CASE(a_heartbeat_every_10s_three_lost_is_lost_then_back)
{
    rig_t r;
    rig_init(&r, 9600u, 0u);
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    const unsigned reads = r.m.reads;
    run_ms(&r, 10020u);
    CHECK_EQ(r.m.beats, 1u);
    const uint8_t beat[] = { 0x7E, 0x00, 0x0A, 0x01, 0x00, 0x00, 0x00, 0x30, 0x1A };
    CHECK(memcmp(r.last, beat, sizeof(beat)) == 0);
    run_ms(&r, 20000u);
    CHECK_EQ(r.m.beats, 3u);

    /* Two lost: still READY. */
    r.m.drop_beats = 2u;
    run_ms(&r, 30000u);
    CHECK_EQ(gm861_state(&r.g), GM861_READY);
    CHECK_EQ(r.m.beats, 6u);

    /* Three lost in a row: LOST at the third, then found and read again. */
    r.m.drop_beats = 3u;
    run_ms(&r, 29000u);
    CHECK_EQ(gm861_state(&r.g), GM861_READY);
    CHECK(run_until(&r, GM861_LOST, 1500u) != UINT32_MAX);
    CHECK(run_until(&r, GM861_READY, 2000u) != UINT32_MAX);
    CHECK_EQ(r.m.reads, reads + 3u);
    CHECK_EQ(r.m.saves, 1u);                /* nothing changed: no save */
}

TEST_CASE(a_module_gone_after_ready_stays_lost_until_it_answers)
{
    rig_t r;
    rig_init(&r, 115200u, 0u);
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    r.m.mute = true;
    CHECK(run_until(&r, GM861_LOST, 32000u) != UINT32_MAX);
    run_ms(&r, 20000u);
    CHECK_EQ(gm861_state(&r.g), GM861_LOST);
    r.m.mute = false;
    CHECK(run_until(&r, GM861_READY, GM861_RETRY_MS + 4000u) != UINT32_MAX);
    CHECK_EQ(gm861_baud(&r.g), 115200u);
}

TEST_CASE(a_zone_changed_while_ready_is_put_back_at_the_next_recheck)
{
    rig_t r;
    rig_init(&r, 9600u, 0u);
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    r.m.ram[0x0060] = 0x01u;                /* a setup code in view: tail CR */
    const unsigned writes = r.m.writes;
    run_ms(&r, 30000u);
    CHECK_EQ(r.m.writes, writes);
    gm861_recheck(&r.g);
    CHECK(run_until(&r, GM861_SETTING, 100u) != UINT32_MAX);
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    CHECK_EQ(r.m.ram[0x0060], 0x21u);
    CHECK_EQ(r.m.writes, writes + 1u);
    CHECK_EQ(r.m.saves, 2u);
}

TEST_CASE(a_new_read_mode_is_written_at_once)
{
    rig_t r;
    rig_init(&r, 9600u, 0u);
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    gm861_set_mode(&r.g, GM861_MODE_CONTINUOUS);
    run_ms(&r, 40u);
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    check_set(r.m.flash, false);
    /* Set while not READY: taken at the next pass, no recheck. */
    gm861_stop(&r.g);
    gm861_set_mode(&r.g, GM861_MODE_SENSING);
    CHECK_EQ(gm861_state(&r.g), GM861_OFF);
}

/** @brief Tick until the last frame sent is of @p type (and zone @p addr, unless 0xFFFF). */
static bool run_to_send(rig_t *r, uint8_t type, uint16_t addr, uint32_t max_ms)
{
    for (uint32_t ms = 0u; ms <= max_ms; ms += 20u) {
        const unsigned before = r->sent;
        tick(r);
        if (r->sent != before && r->last[2] == type
            && (addr == 0xFFFFu
                || (uint16_t)((r->last[4] << 8) | r->last[5]) == addr)) {
            return true;
        }
    }
    return false;
}

TEST_CASE(a_mode_changed_while_the_save_is_out_is_written_and_saved)
{
    rig_t r;
    rig_init(&r, 9600u, 0u);
    CHECK(run_to_send(&r, GM861_T_SAVE, 0xFFFFu, 3000u));
    gm861_set_mode(&r.g, GM861_MODE_CONTINUOUS);
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    check_set(r.m.ram, false);
    check_set(r.m.flash, false);
    CHECK_EQ(r.m.saves, 2u);
    run_ms(&r, 40000u);                     /* heartbeats change nothing */
    CHECK_EQ(gm861_state(&r.g), GM861_READY);
    check_set(r.m.flash, false);
}

TEST_CASE(a_mode_changed_during_a_write_of_each_pass_ends_ready_with_the_last_mode)
{
    /* Continuous during pass 1 (after zone 0x0000 went out as sensing),
     * sensing again during pass 2's write of zone 0x0000: the second
     * change must not use up the 2 passes and hold zone 0x0000. */
    rig_t r;
    rig_init(&r, 9600u, 0u);
    CHECK(run_to_send(&r, GM861_T_WRITE, 0x0000u, 3000u));
    CHECK(run_to_send(&r, GM861_T_WRITE, 0x0003u, 200u));
    gm861_set_mode(&r.g, GM861_MODE_CONTINUOUS);
    CHECK(run_to_send(&r, GM861_T_WRITE, 0x0000u, 3000u));
    gm861_set_mode(&r.g, GM861_MODE_SENSING);
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    CHECK_EQ(gm861_bad_zone(&r.g), GM861_NO_ZONE);
    check_set(r.m.ram, true);
    check_set(r.m.flash, true);
}

TEST_CASE(a_mode_changed_during_the_probe_is_the_one_written)
{
    rig_t r;
    rig_init(&r, 9600u, 0u);
    r.m.baud = 57600u;                      /* the second rate tried */
    CHECK(run_to_send(&r, GM861_T_READ, 0x0000u, 1000u));
    CHECK_EQ(gm861_baud(&r.g), 9600u);
    gm861_set_mode(&r.g, GM861_MODE_CONTINUOUS);
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    check_set(r.m.flash, false);
    CHECK_EQ(r.m.saves, 1u);
}

TEST_CASE(a_mode_changed_during_the_last_write_of_pass_1_gets_2_fresh_passes)
{
    /* Zone 0x0060 takes its third write only.  The change at pass 1's
     * last write (zone 0x00B0) comes due in the verify read; the new mode
     * gets its own 2 passes, the second of which sets zone 0x0060. */
    rig_t r;
    rig_init(&r, 9600u, 0u);
    r.m.flaky = 0x0060u;
    r.m.flaky_n = 2u;
    CHECK(run_to_send(&r, GM861_T_WRITE, 0x00B0u, 3000u));
    gm861_set_mode(&r.g, GM861_MODE_CONTINUOUS);
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    CHECK_EQ(gm861_bad_zone(&r.g), GM861_NO_ZONE);
    check_set(r.m.ram, false);
    check_set(r.m.flash, false);
}

TEST_CASE(a_mode_changed_during_the_last_write_of_pass_2_ends_ready)
{
    /* Continuous at pass 1's last write (zone 0x00B0), sensing again at
     * pass 2's last and only write (zone 0x0000): both come due in a
     * verify read, and neither may use up the passes of the next mode. */
    rig_t r;
    rig_init(&r, 9600u, 0u);
    CHECK(run_to_send(&r, GM861_T_WRITE, 0x00B0u, 3000u));
    gm861_set_mode(&r.g, GM861_MODE_CONTINUOUS);
    CHECK(run_to_send(&r, GM861_T_WRITE, 0x0000u, 3000u));
    gm861_set_mode(&r.g, GM861_MODE_SENSING);
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    CHECK_EQ(gm861_bad_zone(&r.g), GM861_NO_ZONE);
    check_set(r.m.ram, true);
    check_set(r.m.flash, true);
}

TEST_CASE(a_mode_changed_while_a_zone_is_held_is_tried_again)
{
    rig_t r;
    rig_init(&r, 9600u, 0u);
    r.m.stuck = 0x0060u;
    CHECK(run_until(&r, GM861_NO_ANSWER, 3000u) != UINT32_MAX);
    r.m.stuck = 0xFFFFu;
    gm861_set_mode(&r.g, GM861_MODE_CONTINUOUS);
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    CHECK_EQ(gm861_bad_zone(&r.g), GM861_NO_ZONE);
    check_set(r.m.flash, false);
}

TEST_CASE(a_recheck_while_a_request_is_out_waits_for_its_end)
{
    rig_t r;
    rig_init(&r, 9600u, 0u);
    r.m.mute = true;
    CHECK(run_to_send(&r, GM861_T_READ, 0x0000u, 1000u));
    const unsigned sent = r.sent;
    gm861_recheck(&r.g);
    tick(&r);
    CHECK_EQ(r.sent, sent);                 /* the probe's wait runs out first */
    r.m.mute = false;
    CHECK(run_until(&r, GM861_READY, 6000u) != UINT32_MAX);
    check_set(r.m.flash, true);
}

TEST_CASE(a_factory_reset_is_sent_and_the_module_set_again)
{
    rig_t r;
    rig_init(&r, 9600u, 0u);
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    gm861_factory_reset(&r.g);
    tick(&r);
    const uint8_t reset[] = { 0x7E, 0x00, 0x08, 0x01, 0x00, 0xD9, 0x50, 0x81, 0xD3 };
    CHECK(memcmp(r.last, reset, sizeof(reset)) == 0);
    CHECK_EQ(r.m.resets, 1u);
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    check_set(r.m.flash, true);
    CHECK_EQ(r.m.saves, 2u);

    /* A reset with no answer starts the sequence all the same. */
    r.m.mute = true;
    gm861_factory_reset(&r.g);
    run_ms(&r, 1000u);
    CHECK_EQ(r.m.resets, 2u);
    r.m.mute = false;
    CHECK(run_until(&r, GM861_READY, GM861_RETRY_MS + 4000u) != UINT32_MAX);
}

TEST_CASE(the_sequence_holds_across_the_clock_wrap)
{
    rig_t r;
    rig_init(&r, 19200u, 0xFFFFFC00u);
    CHECK(run_until(&r, GM861_READY, 5000u) != UINT32_MAX);
    CHECK(r.t < 0x10000000u);               /* the clock wrapped */
    run_ms(&r, 40000u);
    CHECK_EQ(gm861_state(&r.g), GM861_READY);
    CHECK_EQ(r.m.beats, 4u);
}

TEST_CASE(off_sends_nothing_and_null_arguments_do_nothing)
{
    rig_t r;
    uint8_t tx[4];
    rig_init(&r, 9600u, 0u);
    gm861_stop(&r.g);
    run_ms(&r, 10000u);
    CHECK_EQ(r.sent, 0u);
    CHECK_EQ(gm861_state(&r.g), GM861_OFF);
    gm861_recheck(&r.g);
    gm861_factory_reset(&r.g);
    run_ms(&r, 1000u);
    CHECK_EQ(r.sent, 0u);

    gm861_init(&r.g, GM861_MODE_SENSING);
    CHECK_EQ(gm861_step(&r.g, 1000u, true, tx, sizeof(tx)), 0u);
    CHECK_EQ(gm861_step(&r.g, 1000u, true, NULL, 16u), 0u);
    CHECK_EQ(gm861_step(NULL, 1000u, true, tx, sizeof(tx)), 0u);
    gm861_init(NULL, GM861_MODE_SENSING);
    gm861_stop(NULL);
    gm861_recheck(NULL);
    gm861_set_mode(NULL, GM861_MODE_SENSING);
    gm861_factory_reset(NULL);
    gm861_answer(NULL, tx, 0u);
    CHECK_EQ(gm861_state(NULL), GM861_OFF);
    CHECK_EQ(gm861_baud(NULL), 9600u);
}

TEST_CASE(an_answer_nobody_asked_for_or_of_the_wrong_size_is_ignored)
{
    rig_t r;
    rig_init(&r, 9600u, 0u);
    const uint8_t ack[] = { 0x02, 0x00, 0x00, 0x01, 0x00, 0x33, 0x31 };
    gm861_answer(&r.g, ack, sizeof(ack));   /* nothing out yet */
    CHECK(run_until(&r, GM861_READY, 3000u) != UINT32_MAX);
    gm861_recheck(&r.g);
    tick(&r);                               /* the probe, answered */
    r.m.mute = true;
    tick(&r);                               /* the block read is out */
    gm861_answer(&r.g, ack, sizeof(ack));   /* 1 byte, not 97 */
    run_ms(&r, 500u);
    CHECK_EQ(r.m.reads, 8u);                /* 5, the probe, the block twice */
}

TEST_CASE(a_write_answered_with_data_other_than_00_is_not_taken)
{
    rig_t r;
    rig_init(&r, 9600u, 0u);
    r.m.nak = true;
    CHECK(run_until(&r, GM861_NO_ANSWER, 3000u) != UINT32_MAX);
    CHECK_EQ(r.m.writes, 2u);               /* the first write, twice */
    CHECK_EQ(r.m.saves, 0u);
}

int main(void)
{
    RUN(the_crc_reproduces_the_manuals_examples);
    RUN(the_crc_matches_the_manuals_routine_on_every_short_input);
    RUN(each_request_frame_is_the_manuals);
    RUN(an_answer_is_checked_for_head_type_length_and_crc);
    RUN(the_wanted_zones_are_plan_bs_table);
    RUN(the_module_is_found_at_each_of_the_8_rates_and_set);
    RUN(nothing_is_sent_before_300ms_and_rx_low_is_not_connected);
    RUN(rx_high_and_a_mute_module_is_no_answer_and_tried_again_after_5s);
    RUN(usb_output_at_01_and_11_is_shown_and_set_to_serial);
    RUN(a_module_with_every_zone_as_wanted_takes_no_write_and_no_save);
    RUN(a_write_that_does_not_stick_is_named_and_not_saved);
    RUN(an_answer_lost_mid_sequence_is_asked_again);
    RUN(two_answers_lost_in_a_row_end_the_pass_and_it_is_tried_again);
    RUN(a_last_write_applied_with_both_answers_lost_is_saved_on_the_retry);
    RUN(a_save_whose_answers_are_lost_is_sent_again_on_the_retry);
    RUN(a_write_unsaved_through_mute_retries_is_saved_when_the_module_answers);
    RUN(a_heartbeat_every_10s_three_lost_is_lost_then_back);
    RUN(a_module_gone_after_ready_stays_lost_until_it_answers);
    RUN(a_zone_changed_while_ready_is_put_back_at_the_next_recheck);
    RUN(a_new_read_mode_is_written_at_once);
    RUN(a_mode_changed_while_the_save_is_out_is_written_and_saved);
    RUN(a_mode_changed_during_a_write_of_each_pass_ends_ready_with_the_last_mode);
    RUN(a_mode_changed_during_the_probe_is_the_one_written);
    RUN(a_mode_changed_during_the_last_write_of_pass_1_gets_2_fresh_passes);
    RUN(a_mode_changed_during_the_last_write_of_pass_2_ends_ready);
    RUN(a_mode_changed_while_a_zone_is_held_is_tried_again);
    RUN(a_recheck_while_a_request_is_out_waits_for_its_end);
    RUN(a_factory_reset_is_sent_and_the_module_set_again);
    RUN(the_sequence_holds_across_the_clock_wrap);
    RUN(off_sends_nothing_and_null_arguments_do_nothing);
    RUN(an_answer_nobody_asked_for_or_of_the_wrong_size_is_ignored);
    RUN(a_write_answered_with_data_other_than_00_is_not_taken);
    return test_summary("gm861");
}
