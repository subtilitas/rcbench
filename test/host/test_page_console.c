/*
 * The bench console (shared/outputs/page_console.c) on a page table with a
 * read-write page, a read-only page and the HITEC page of a
 * shared/outputs/hitec_port.c whose driver hands back a set capture: the
 * raw read and write, the refusals as the link gives them, the parsing of
 * numbers and lines, the hitec conveniences down to the registers they
 * stage, the report when a transaction ends, the output taken in whole
 * lines or a long line in pieces with lines said from outside after it,
 * and a console that does not hold the link's failsafe off.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "hitec_port.h"
#include "hitec_pre.h"
#include "hitec_wire.h"
#include "link_dev.h"
#include "page_console.h"

#define PAGE_RW 0x10u
#define PAGE_RO 0x11u
#define PIN 13u

static uint16_t g_rw[8];
static uint16_t g_ro[4] = { 0x1111u, 0x2222u, 0x3333u, 0x4444u };
static hitec_port_t g_port;
static link_dev_t g_dev;
static page_console_t g_con;
static uint32_t g_ms;

/* The driver: open after the take, a transaction ends at the next poll
 * with the set capture, or not while it hangs. */
static struct {
    bool open, busy, hang;
    uint8_t family;
    uint8_t rx;
    hitec_xfer_t x;
    hitec_capture_t cap;
    unsigned starts;
    supply_port_view_t v;
} g_drv;

static void rw_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    memcpy(out, &g_rw[off], (size_t)n * sizeof(uint16_t));
}

static uint8_t rw_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    for (unsigned i = 0; i < n; ++i) {
        if (in[i] == 0xBADu) {
            return LINK_NACK_BAD_VALUE;
        }
    }
    memcpy(&g_rw[off], in, (size_t)n * sizeof(uint16_t));
    /* Stored clamped: the echo shows it. */
    if (g_rw[off] > 1000u) {
        g_rw[off] = 1000u;
    }
    return 0u;
}

static void ro_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    memcpy(out, &g_ro[off], (size_t)n * sizeof(uint16_t));
}

static void ht_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    hitec_port_read(&g_port, off, n, out);
}

static uint8_t ht_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    return hitec_port_write(&g_port, off, n, in, g_ms);
}

static const link_page_t k_pages[] = {
    { PAGE_RW, 8u, rw_read, rw_write },
    { PAGE_RO, 4u, ro_read, NULL },
    { LINK_PAGE_HITEC, LINK_HT_COUNT, ht_read, ht_write },
};

static uint8_t hw_pin(void *ctx, uint8_t port)
{
    (void)ctx;
    return port == 0u ? PIN : 0xFFu;
}

static bool hw_take(void *ctx, uint8_t port, uint8_t pin, uint8_t family,
                    uint8_t drive, uint8_t rx)
{
    (void)ctx;
    (void)port;
    (void)pin;
    (void)drive;
    g_drv.open = true;
    g_drv.family = family;
    g_drv.rx = rx;
    return true;
}

static void hw_give(void *ctx)
{
    (void)ctx;
    g_drv.open = false;
}

static void hw_abort(void *ctx)
{
    (void)ctx;
}

static void hw_supply(void *ctx, supply_port_view_t *out)
{
    (void)ctx;
    *out = g_drv.v;
}

static bool hw_supply_set(void *ctx, bool on, uint16_t mv, uint16_t ma)
{
    (void)ctx;
    g_drv.v.asked_on = on;
    g_drv.v.on = on;
    g_drv.v.off = !on;
    if (on) {
        g_drv.v.set_mv = mv;
        g_drv.v.set_ma = ma;
    }
    return true;
}

static bool drv_start(void *ctx, const hitec_xfer_t *x)
{
    (void)ctx;
    if (!g_drv.open || g_drv.busy || !hitec_xfer_valid(x)) {
        return false;
    }
    g_drv.x = *x;
    g_drv.busy = true;
    g_drv.starts++;
    return true;
}

static bool drv_poll(void *ctx, hitec_capture_t *out)
{
    (void)ctx;
    if (!g_drv.busy || g_drv.hang) {
        return false;
    }
    g_drv.busy = false;
    *out = g_drv.cap;
    out->family = g_drv.family;
    return true;
}

static uint32_t drv_now(void *ctx)
{
    (void)ctx;
    return g_ms * 1000u;
}

static void fresh(void)
{
    hitec_port_hw_t hw;

    memset(g_rw, 0, sizeof(g_rw));
    memset(&g_drv, 0, sizeof(g_drv));
    g_drv.v.online = true;
    g_drv.v.off = true;
    g_drv.v.vin_mv = 20000u;
    memset(&hw, 0, sizeof(hw));
    hw.ports = 1u;
    hw.pin = hw_pin;
    hw.take = hw_take;
    hw.give = hw_give;
    hw.abort = hw_abort;
    hw.supply = hw_supply;
    hw.supply_set = hw_supply_set;
    hw.driver.start = drv_start;
    hw.driver.poll = drv_poll;
    hw.driver.now_us = drv_now;
    CHECK(hitec_port_init(&g_port, &hw));
    g_ms = 1000u;
    hitec_port_step(&g_port, true, false, g_ms);
    link_dev_init(&g_dev, k_pages, 3u, NULL, g_ms);
    page_console_init(&g_con, &g_dev);
}

static char g_out[8192];

/* Type @p line and take what the console says. */
static const char *type(const char *line)
{
    size_t at = 0;

    for (const char *p = line; *p != '\0'; ++p) {
        page_console_key(&g_con, (unsigned char)*p);
    }
    page_console_key(&g_con, '\n');
    for (;;) {
        const size_t n = page_console_take(&g_con, g_out + at,
                                           sizeof(g_out) - 1u - at);
        if (n == 0u) {
            break;
        }
        at += n;
    }
    g_out[at] = '\0';
    return g_out;
}

/* Passes of the port and the console until the console reports. */
static const char *passes(unsigned ms)
{
    size_t at = 0;

    for (unsigned i = 0; i < ms; ++i) {
        hitec_port_step(&g_port, true, false, g_ms);
        page_console_poll(&g_con);
        g_ms++;
        at += page_console_take(&g_con, g_out + at, sizeof(g_out) - 1u - at);
    }
    g_out[at] = '\0';
    return g_out;
}

static bool has(const char *text, const char *want)
{
    if (strstr(text, want) == NULL) {
        printf("      missing \"%s\" in:\n%s", want, text);
        return false;
    }
    return true;
}

/* ===================================================================== cases */

TEST_CASE(pages_lists_every_page)
{
    fresh();
    const char *o = type("pages");
    CHECK(has(o, "page 0x10 regs 8 rw\n"));
    CHECK(has(o, "page 0x11 regs 4 ro\n"));
    CHECK(has(o, "page 0x34 regs 32 rw\n"));
    CHECK(has(type("help"), "hitec power <port>"));
}

TEST_CASE(a_read_answers_data_and_a_refusal_is_a_nack)
{
    fresh();
    CHECK_STR_EQ(type("r 0x11 1 2"),
                 "data page=0x11 off=1 n=2: 0x2222 0x3333\n");
    CHECK_STR_EQ(type("r 17 0"), "data page=0x11 off=0 n=1: 0x1111\n");
    CHECK_STR_EQ(type("r 0x11 3 2"), "nack page=0x11 off=3: BAD_RANGE (2)\n");
    CHECK_STR_EQ(type("r 0x7F 0"), "nack page=0x7F off=0: BAD_PAGE (1)\n");
    CHECK(has(type("r 0x11 0 33"), "err usage: r"));
    CHECK(has(type("r 0x11 0 0"), "err usage: r"));
    CHECK(has(type("r 0x11"), "err usage: r"));
    CHECK(has(type("r 0x100 0"), "err usage: r"));
}

TEST_CASE(a_write_echoes_what_was_stored)
{
    fresh();
    CHECK_STR_EQ(type("w 0x10 2 5 0x0006"),
                 "ack page=0x10 off=2 n=2: 0x0005 0x0006\n");
    CHECK_EQ(g_rw[2], 5);
    CHECK_EQ(g_rw[3], 6);
    /* Clamped by the page: the echo says so. */
    CHECK_STR_EQ(type("w 0x10 0 5000"), "ack page=0x10 off=0 n=1: 0x03E8\n");
    CHECK_STR_EQ(type("w 0x10 0 0xBAD"),
                 "nack page=0x10 off=0: BAD_VALUE (4)\n");
    CHECK_STR_EQ(type("w 0x11 0 1"), "nack page=0x11 off=0: READ_ONLY (3)\n");
    CHECK_STR_EQ(type("w 0x10 7 1 2"), "nack page=0x10 off=7: BAD_RANGE (2)\n");
    CHECK(has(type("w 0x10 0 0x10000"), "err not a 16-bit value: 0x10000"));
    CHECK(has(type("w 0x10 0 12z"), "err not a 16-bit value: 12z"));
    CHECK(has(type("w 0x10 0"), "err usage: w"));
    CHECK(has(type("w 0x10 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 "
                   "20 21 22 23 24 25 26 27 28 29 30 31 32 33"),
              "err usage: w"));
    CHECK(has(type("frob"), "err unknown command: frob"));
    CHECK_STR_EQ(type(""), "");
    CHECK_STR_EQ(type("   "), "");
}

TEST_CASE(a_line_is_edited_and_an_overlong_one_refused)
{
    char longline[PAGE_CONSOLE_LINE_MAX + 10u];

    fresh();
    page_console_key(&g_con, 'x');
    page_console_key(&g_con, 0x7F);
    page_console_key(&g_con, 0x01);
    CHECK_STR_EQ(type("r 0x11 0"), "data page=0x11 off=0 n=1: 0x1111\n");
    memset(longline, 'a', sizeof(longline) - 1u);
    longline[sizeof(longline) - 1u] = '\0';
    CHECK(has(type(longline), "err line longer than 160 characters"));
    /* The line after it runs; CR ends a line as LF does. */
    for (const char *p = "r 0x11 3\r"; *p != '\0'; ++p) {
        page_console_key(&g_con, *p);
    }
    CHECK_EQ(page_console_take(&g_con, g_out, sizeof(g_out)), 33);
    page_console_key(NULL, 'x');
    page_console_poll(NULL);
    CHECK_EQ(page_console_take(NULL, g_out, 10u), 0);
}

TEST_CASE(output_goes_out_in_whole_lines_and_overflow_is_counted)
{
    char buf[64];
    size_t n;

    fresh();
    for (const char *p = "r 0x11 0 4\n"; *p != '\0'; ++p) {
        page_console_key(&g_con, *p);
    }
    /* The line is 54 bytes: 53 do not take it. */
    CHECK_EQ(page_console_take(&g_con, buf, 53u), 0);
    n = page_console_take(&g_con, buf, 64u);
    CHECK_EQ(n, 54);
    CHECK(memcmp(buf, "data page=0x11 off=0 n=4:", 25u) == 0);
    CHECK_EQ(buf[n - 1u], '\n');
    /* Typed without a reader: the buffer fills, the rest is counted. */
    for (unsigned i = 0; i < 100u; ++i) {
        for (const char *p = "r 0x11 0 4\n"; *p != '\0'; ++p) {
            page_console_key(&g_con, *p);
        }
    }
    CHECK(g_con.dropped > 0u);
    {
        size_t at = 0, got;

        while ((got = page_console_take(&g_con, g_out + at,
                                        sizeof(g_out) - 1u - at)) > 0u) {
            at += got;
        }
        g_out[at] = '\0';
        CHECK(has(g_out, "err output full, "));
        CHECK_EQ(g_con.used, 0);
    }
}

/* A text of @p len bytes with its LF, the LF left to page_console_say():
 * a tag, then letters.  @p buf holds at least @p len bytes. */
static void line_of(char *buf, size_t len, char tag)
{
    for (size_t i = 0; i + 1u < len; ++i) {
        buf[i] = (char)('a' + (i % 26u));
    }
    buf[0] = tag;
    buf[len - 1u] = '\0';
}

static char g_want[8192];
static size_t g_want_n;

/* Said through page_console_say(), and expected in the stream. */
static void say_and_expect(const char *text)
{
    page_console_say(&g_con, "%s", text);
    const size_t n = strlen(text);
    memcpy(g_want + g_want_n, text, n);
    g_want_n += n;
    if (n == 0u || text[n - 1u] != '\n') {
        g_want[g_want_n++] = '\n';
    }
}

/* Passes of a transmit buffer with @p room free each: what they took,
 * appended to g_out from @p at.  Each take within its room, and a pass
 * with output waiting and room free moves it. */
static size_t drain(size_t at, size_t room, unsigned *passes_taken)
{
    unsigned k = 0;

    while (g_con.used > 0u && k < 1000u) {
        const uint16_t before = g_con.used;
        const size_t n = page_console_take_part(&g_con, g_out + at, room);
        CHECK(n <= room);
        CHECK(n > 0u || room == 0u);
        /* Less by what was taken, unless the note of a loss went in. */
        CHECK(g_con.used == before - n || g_con.dropped == 0u);
        at += n;
        k++;
        if (room == 0u) {
            break;
        }
    }
    if (passes_taken != NULL) {
        *passes_taken = k;
    }
    return at;
}

/* The IOmcu console build hands the console the free bytes of a 64-byte
 * USB transmit buffer: a line of more than 64 bytes with its LF has no
 * whole-line take there, and took the console's output down with it. */
TEST_CASE(a_line_longer_than_the_room_leaves_in_pieces)
{
    static const size_t lens[] = { 63u, 64u, 65u, 87u, PAGE_CONSOLE_OUT_LINE };
    char text[PAGE_CONSOLE_OUT_LINE + 64u];
    size_t at;
    unsigned passes_taken = 0;

    fresh();
    g_want_n = 0;
    for (size_t i = 0; i < sizeof(lens) / sizeof(lens[0]); ++i) {
        line_of(text, lens[i], (char)('A' + i));
        say_and_expect(text);
    }
    /* Whole lines only: the lines of 63 and 64 bytes, then nothing. */
    CHECK_EQ(page_console_take(&g_con, g_out, 64u), 63);
    CHECK_EQ(page_console_take(&g_con, g_out + 63, 64u), 64);
    CHECK_EQ(page_console_take(&g_con, g_out + 127, 64u), 0);
    /* In parts: 64 bytes a pass, lines cut where the room ends. */
    at = drain(127u, 64u, &passes_taken);
    CHECK_EQ(at, 63u + 64u + 65u + 87u + PAGE_CONSOLE_OUT_LINE);
    CHECK_EQ(at, g_want_n);
    CHECK(memcmp(g_out, g_want, at) == 0);
    /* 352 bytes after the first two lines: 6 passes of 64. */
    CHECK_EQ(passes_taken, 6);
    CHECK_EQ(g_con.dropped, 0);
    CHECK_EQ(page_console_take_part(&g_con, g_out, 64u), 0);
}

/* Status lines said between the passes that carry a long line out come
 * after it whole, never inside it; console replies keep their place. */
TEST_CASE(status_lines_said_meanwhile_leave_after_the_line_in_pieces)
{
    static const size_t rooms[] = { 64u, 1u, 17u, 0u, 63u, 64u, 5u };
    char text[PAGE_CONSOLE_OUT_LINE + 64u];
    char status[48];
    size_t at = 0;
    unsigned said = 0;

    fresh();
    g_want_n = 0;
    line_of(text, 87u, 'L');
    say_and_expect(text);
    for (const char *p = "r 0x11 0 4\n"; *p != '\0'; ++p) {
        page_console_key(&g_con, *p);
    }
    {
        const char *reply = "data page=0x11 off=0 n=4: 0x1111 0x2222 0x3333 "
                            "0x4444\n";
        memcpy(g_want + g_want_n, reply, strlen(reply));
        g_want_n += strlen(reply);
    }
    line_of(text, PAGE_CONSOLE_OUT_LINE, 'M');
    say_and_expect(text);
    /* Passes of changing room, a status line said every second pass. */
    for (unsigned pass = 0; pass < 200u && (g_con.used > 0u || said < 8u);
         ++pass) {
        const size_t room = rooms[pass % (sizeof(rooms) / sizeof(rooms[0]))];
        const size_t n = page_console_take_part(&g_con, g_out + at, room);

        CHECK(n <= room);
        at += n;
        if ((pass % 2u) == 1u && said < 8u) {
            /* With its own LF, as the IOmcu's printf texts have it. */
            (void)snprintf(status, sizeof(status),
                           "rcbench-iomcu: status %u\n", said++);
            say_and_expect(status);
        }
    }
    CHECK_EQ(g_con.used, 0);
    CHECK_EQ(at, g_want_n);
    CHECK(memcmp(g_out, g_want, at) == 0);
    CHECK_EQ(g_con.dropped, 0);
}

/* The text of a line said from outside: the LF added once, a longer text
 * cut to PAGE_CONSOLE_OUT_LINE with its LF, and the guards. */
TEST_CASE(a_said_line_gets_one_lf_and_is_cut_at_the_longest)
{
    char text[PAGE_CONSOLE_OUT_LINE + 64u];

    fresh();
    page_console_say(&g_con, "%s", "");
    page_console_say(&g_con, "plain");
    page_console_say(&g_con, "ends %d\n", 7);
    CHECK_EQ(page_console_take(&g_con, g_out, sizeof(g_out)), 14);
    CHECK(memcmp(g_out, "\nplain\nends 7\n", 14u) == 0);
    /* 199 characters and the LF fit; 200 and more are cut to that. */
    line_of(text, PAGE_CONSOLE_OUT_LINE, 'X');
    page_console_say(&g_con, "%s", text);
    CHECK_EQ(page_console_take(&g_con, g_out, sizeof(g_out)),
             PAGE_CONSOLE_OUT_LINE);
    line_of(text, PAGE_CONSOLE_OUT_LINE + 1u, 'Y');
    page_console_say(&g_con, "%s", text);
    line_of(text, sizeof(text), 'Z');
    page_console_say(&g_con, "%s\n", text);
    CHECK_EQ(page_console_take(&g_con, g_out, sizeof(g_out)),
             2u * PAGE_CONSOLE_OUT_LINE);
    CHECK_EQ(g_out[PAGE_CONSOLE_OUT_LINE - 1u], '\n');
    CHECK_EQ(g_out[PAGE_CONSOLE_OUT_LINE - 2u],
             (char)('a' + (PAGE_CONSOLE_OUT_LINE - 2u) % 26u));
    CHECK_EQ(g_out[PAGE_CONSOLE_OUT_LINE], 'Z');
    CHECK_EQ(g_out[2u * PAGE_CONSOLE_OUT_LINE - 1u], '\n');
    page_console_say(NULL, "x");
    page_console_say(&g_con, NULL);
    CHECK_EQ(g_con.used, 0);
    CHECK_EQ(page_console_take_part(NULL, g_out, 64u), 0);
    CHECK_EQ(page_console_take_part(&g_con, NULL, 64u), 0);
}

/* A buffer that overflows while a line is out in part: the rest of that
 * line first, then the note of the loss on a line of its own. */
TEST_CASE(an_overflow_during_a_line_in_pieces_is_noted_after_it)
{
    char text[PAGE_CONSOLE_OUT_LINE];
    size_t at;
    const char *note;

    fresh();
    line_of(text, 87u, 'P');
    page_console_say(&g_con, "%s", text);
    CHECK_EQ(page_console_take_part(&g_con, g_out, 10u), 10);
    line_of(text, 100u, 'Q');
    for (unsigned i = 0; i < 40u; ++i) {
        page_console_say(&g_con, "%s", text);
    }
    CHECK(g_con.dropped > 0u);
    at = drain(10u, 64u, NULL);
    g_out[at] = '\0';
    CHECK(memcmp(g_out, "Pbcdefghij", 10u) == 0);
    CHECK_EQ(g_out[86], '\n');
    CHECK_EQ(g_out[87], 'Q');
    note = strstr(g_out, "err output full, ");
    CHECK(note != NULL);
    CHECK_EQ(note[-1], '\n');
    CHECK_EQ(g_out[at - 1u], '\n');
}

TEST_CASE(hitec_open_and_a_classic_command_report_the_reply)
{
    const char *o;

    fresh();
    o = type("hitec open 0 c");
    CHECK(has(o, "hitec OPEN port=0 started\n"));
    o = passes(60u);
    CHECK(has(o, "hitec done state=OPEN port=0"));
    CHECK(has(o, "op=OPEN"));
    CHECK(has(o, "hitec line pin=13"));
    /* The servo's reply: 0x2E and `+`, at 51us and 572us. */
    g_drv.cap.n = 2u;
    g_drv.cap.full = 1u;
    g_drv.cap.byte[0].value = 0x2Eu;
    g_drv.cap.byte[0].at_ns = 51302u;
    g_drv.cap.byte[1].value = HITEC_C_ACK;
    g_drv.cap.byte[1].at_ns = 51302u + hitec_bits_ns(HITEC_CLASSIC, 10u);
    o = type("hitec c 0 0x61 0x12 0");
    CHECK(has(o, "hitec SEND port=0 started\n"));
    CHECK_EQ(g_drv.starts, 1);
    CHECK_EQ(g_drv.x.n_tx, 4);
    CHECK_EQ(g_drv.x.tx[0], 0x61);
    CHECK_EQ(g_drv.x.tx[3], hitec_c_sum(0x61u, 0x12u, 0u));
    CHECK_EQ(g_drv.x.window_us, HITEC_C_WINDOW_US);
    o = passes(3u);
    CHECK(has(o, "hitec done state=OPEN port=0 flags=RAIL,CAPTURE,KEY seq=2 "
                 "op=SEND count=1 result=DONE refusal=NONE\n"));
    CHECK(has(o, "hitec cap n=2 full=1 answered=0 overflow=0 aborted=0 "
                 "dec=C rx=OK id=0x00 value=0x002E delay_ns=520052"));
    CHECK(has(o, "hitec byte 0 0x2E at_ns=51302 framing=0\n"));
    CHECK(has(o, "hitec byte 1 0x2B at_ns=572135 framing=0\n"));
    /* The view the report borrowed is given back. */
    CHECK_EQ(g_port.view, LINK_HITEC_VIEW_TIMES_LO);
    /* Reported once. */
    CHECK_STR_EQ(passes(5u), "");
}

/* `more` on a step: LINK_HT_W_CMD_MORE in its command, the page's latch
 * and a write running until a step without it; refused on the rest. */
TEST_CASE(a_step_marked_more_carries_the_bit)
{
    static const char *const k_more[] = {
        "hitec c 0 0x62 0x10 0x55 more",
        "hitec send 0 lead=100 1 2 more",
        "hitec listen 0 window=1000 rx=1 more",
        "hitec level 0 0 1500 more",
    };

    fresh();
    (void)type("hitec open 0 c");
    (void)passes(60u);
    for (unsigned i = 0; i < sizeof(k_more) / sizeof(k_more[0]); ++i) {
        CHECK(has(type(k_more[i]), "started"));
        CHECK_EQ(g_port.last_cmd[LINK_HT_W_CMD] & LINK_HT_W_CMD_MORE,
                 LINK_HT_W_CMD_MORE);
        CHECK(has(passes(3u), "flags=RAIL,CAPTURE,"));
        CHECK(g_port.more != 0u);
        CHECK(hitec_port_writing(&g_port));
    }
    /* The last step: none, and nothing runs once it has ended. */
    CHECK(has(type("hitec c 0 0x62 0x1C 0xAA"), "started"));
    CHECK_EQ(g_port.last_cmd[LINK_HT_W_CMD] & LINK_HT_W_CMD_MORE, 0u);
    CHECK_EQ(g_port.more, 0u);
    (void)passes(3u);
    CHECK(!hitec_port_writing(&g_port));
    /* The mark is the line's: the next line goes without it. */
    CHECK(has(type("hitec c 0 0x61 0x12 0"), "started"));
    CHECK_EQ(g_port.last_cmd[LINK_HT_W_CMD] & LINK_HT_W_CMD_MORE, 0u);
    (void)passes(3u);
    /* A D series write and a power. */
    CHECK(has(type("hitec close 0"), "state=FREE"));
    (void)type("hitec open 0 d");
    (void)passes(60u);
    CHECK(has(type("hitec d 0 write 0 0x30 0x0123 more"), "started"));
    CHECK(g_port.more != 0u);
    (void)passes(3u);
    CHECK(has(type("hitec power 0 mv=5000 ma=800 off=100 window=1000 rx=1 "
                   "more"), "started"));
    CHECK(g_port.more != 0u);
    CHECK(has(passes(200u), "flags=RAIL,CAPTURE,MORE"));
    /* Not a step: the word is no argument there. */
    CHECK(has(type("hitec open 0 c more"), "err usage: hitec open"));
    CHECK(has(type("hitec d 0 read 0 0x32 more x"), "err usage: hitec d"));
    CHECK(has(type("help"), "more, last on send"));
}

TEST_CASE(hitec_commands_stage_name_the_crc_and_carry_the_key)
{
    fresh();
    (void)type("hitec open 0 d pp");
    (void)passes(60u);
    CHECK_EQ(g_port.family, HITEC_D);
    CHECK_EQ(g_port.drive, HITEC_DRIVE_PUSH_PULL);
    CHECK(has(type("hitec d 0 read 0 0x32"), "hitec SEND port=0 started"));
    CHECK_EQ(g_drv.x.n_tx, HITEC_D_READ_LEN);
    CHECK_EQ(g_drv.x.lead_us, HITEC_D_LEAD_US);
    CHECK_EQ(g_port.dec, LINK_HITEC_DEC_D);
    CHECK_EQ(g_port.dec_arg, 0x32);
    CHECK(g_port.keyed);
    (void)passes(2u);
    CHECK(has(type("hitec send 0 lead=100 gap=2 window=3000 rx=4 dec=l:0x2B "
                   "1 2 0x03"), "started"));
    CHECK_EQ(g_drv.x.lead_us, 100u);
    CHECK_EQ(g_drv.x.gap_bits, 2u);
    CHECK_EQ(g_drv.x.window_us, 3000u);
    CHECK_EQ(g_drv.x.max_rx, 4u);
    CHECK_EQ(g_drv.x.n_tx, 3u);
    CHECK_EQ(g_drv.x.tx[2], 3u);
    CHECK_EQ(g_port.dec, LINK_HITEC_DEC_LISTEN);
    (void)passes(2u);
    /* A listen without an answer carries no key; one with does. */
    CHECK(has(type("hitec listen 0 window=1000 rx=1"), "started"));
    CHECK(!g_port.keyed);
    (void)passes(2u);
    CHECK(has(type("hitec listen 0 window=1000 rx=1 answer=0x2B"),
              "started"));
    CHECK(g_port.keyed);
    CHECK_EQ(g_drv.x.answer_byte, 0x2B);
    (void)passes(2u);
    CHECK(has(type("hitec level 0 0 1500"), "started"));
    CHECK_EQ(g_drv.x.kind, HITEC_XFER_LEVEL);
    CHECK_EQ(g_drv.x.level_us, 1500u);
    (void)passes(2u);
    /* The page's refusals come back named. */
    CHECK(has(type("hitec send 0 window=1000 1"),
              "hitec SEND port=0 refused RANGE"));
    CHECK(has(type("hitec power 0 mv=6001 ma=500 off=100 window=1000 rx=1"),
              "hitec POWER port=0 refused SUPPLY_RANGE"));
    CHECK(has(type("hitec open 0 c"), "hitec OPEN port=0 refused BUSY"));
    /* And the console's own errors. */
    CHECK(has(type("hitec send 0"), "err usage: hitec send"));
    CHECK(has(type("hitec send 0 dec=x:1 1"), "err bad argument: dec=x:1"));
    CHECK(has(type("hitec send 0 1 2 3 4 5 6 7 8 9"),
              "err bad argument: 9"));
    CHECK(has(type("hitec listen 0 window=1000 rx=1 7"),
              "err usage: hitec listen"));
    CHECK(has(type("hitec open 0 x"), "err usage: hitec open"));
    CHECK(has(type("hitec level 0 2 100"), "err usage: hitec level"));
    CHECK(has(type("hitec d 0 peek 1 2"), "err usage: hitec d"));
    CHECK(has(type("hitec c 0 1 2"), "err usage: hitec c"));
    CHECK(has(type("hitec frob 0"), "err unknown: hitec frob"));
    CHECK(has(type("hitec close 16"), "err not a port: 16"));
    CHECK(has(type("hitec close"), "err usage: hitec <sub>"));
    /* CLOSE gives the port back. */
    CHECK(has(type("hitec close 0"), "hitec done state=FREE"));
    CHECK(!g_drv.open);
}

TEST_CASE(hitec_power_stages_the_supply_and_reports_the_listen)
{
    const char *o;

    fresh();
    (void)type("hitec open 0 c");
    (void)passes(60u);
    g_drv.cap.n = 1u;
    g_drv.cap.byte[0].value = 0x5Au;
    g_drv.cap.byte[0].at_ns = 30000000u;
    o = type("hitec power 0 mv=4800 ma=700 off=10 window=100000 rx=1 "
             "dec=l:0x5A");
    CHECK(has(o, "hitec POWER port=0 started"));
    CHECK_EQ(g_port.mv, 4800);
    CHECK_EQ(g_port.ma, 700);
    CHECK_EQ(g_port.off_ms, 10);
    o = passes(20u);
    CHECK(has(o, "op=POWER count=1 result=DONE"));
    CHECK(has(o, "rx=OK"));
    CHECK(has(o, "hitec byte 0 0x5A at_ns=30000000"));
    CHECK_EQ(g_drv.v.set_mv, 4800);
    CHECK(has(type("hitec"), "hitec now state=OPEN"));
}

TEST_CASE(hitec_on_a_build_without_the_page_says_so)
{
    static const link_page_t one[] = {
        { PAGE_RO, 4u, ro_read, NULL },
    };

    fresh();
    link_dev_init(&g_dev, one, 1u, NULL, g_ms);
    CHECK(has(type("hitec"), "err the HITEC page does not answer"));
    CHECK(has(type("hitec abort 0"), "err the HITEC page does not answer"));
    CHECK(has(type("hitec open 0 c"), "nack page=0x34 off=4: BAD_PAGE (1)"));
}

/* A convenience of one family is refused on a line of the other: its
 * frame would go out at the wrong rate and polarity. */
TEST_CASE(the_family_commands_need_a_line_of_their_family)
{
    fresh();
    (void)type("hitec open 0 c");
    (void)passes(60u);
    CHECK(has(type("hitec d 0 read 0 0x32"),
              "err the line is classic; this command needs a D series "
              "line\n"));
    CHECK(has(type("hitec d 0 write 0 0x32 1"), "needs a D series line"));
    CHECK_EQ(g_drv.starts, 0);
    CHECK_EQ(g_port.refusal, LINK_HITEC_REF_NONE);
    (void)type("hitec close 0");
    (void)type("hitec open 0 d");
    (void)passes(60u);
    CHECK(has(type("hitec c 0 0x61 0x12 0"),
              "err the line is D series; this command needs a classic "
              "line\n"));
    CHECK_EQ(g_drv.starts, 0);
    CHECK(has(type("hitec d 0 read 0 0x32"), "hitec SEND port=0 started"));
    CHECK_EQ(g_drv.starts, 1);
}

/* pu and pre write the receive mode before the OPEN, the OPEN takes it,
 * and the report names it; without either the page's mode stays. */
TEST_CASE(hitec_open_writes_the_receive_mode_of_a_d_series_line)
{
    const char *o;

    fresh();
    o = type("hitec open 0 d pp pre");
    CHECK(has(o, "hitec OPEN port=0 started\n"));
    CHECK_EQ(g_port.rx_mode, LINK_HITEC_RX_PRECHARGE);
    o = passes(60u);
    CHECK_EQ(g_drv.rx, HITEC_RX_PRECHARGE);
    CHECK(has(o, "flags=RAIL,PRECHARGE "));
    CHECK(has(o, "hitec line pin=13 line=0x11 rx=pre "));
    (void)type("hitec close 0");
    /* No option: the mode the page holds. */
    (void)type("hitec open 0 d");
    (void)passes(60u);
    CHECK_EQ(g_drv.rx, HITEC_RX_PRECHARGE);
    (void)type("hitec close 0");
    /* pu, with and without the drive. */
    (void)type("hitec open 0 d pu");
    o = passes(60u);
    CHECK_EQ(g_port.rx_mode, LINK_HITEC_RX_PULL_UP);
    CHECK_EQ(g_drv.rx, HITEC_RX_PULL_UP);
    CHECK(has(o, "hitec line pin=13 line=0x01 rx=pu "));
    (void)type("hitec close 0");
    (void)type("hitec open 0 d od pre");
    (void)passes(60u);
    CHECK_EQ(g_drv.rx, HITEC_RX_PRECHARGE);
    (void)type("hitec close 0");
    /* Classic takes neither; the order is drive, then mode; one of each. */
    CHECK(has(type("hitec open 0 c pre"),
              "err pu and pre are for the D series only\n"));
    CHECK(has(type("hitec open 0 c od pu"),
              "err pu and pre are for the D series only\n"));
    CHECK(has(type("hitec open 0 d pre pp"), "err usage: hitec open"));
    CHECK(has(type("hitec open 0 d pre pu"), "err usage: hitec open"));
    CHECK(has(type("hitec open 0 d pp od"), "err usage: hitec open"));
    CHECK(has(type("hitec open 0 d pp pre x"), "err usage: hitec open"));
    CHECK_EQ(g_port.rx_mode, LINK_HITEC_RX_PRECHARGE);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_FREE);
    /* Classic with the page at PRECHARGE opens as classic always does. */
    (void)type("hitec open 0 c");
    o = passes(60u);
    CHECK_EQ(g_drv.rx, HITEC_RX_PULL_UP);
    CHECK(has(o, "rx=pu "));
}

/* A CLOSE while a transaction is watched ends both in its frame: one
 * report, not a second when the next pass finds the port free. */
TEST_CASE(a_close_of_a_watched_transaction_reports_once)
{
    const char *o;

    fresh();
    (void)type("hitec open 0 c");
    (void)passes(60u);
    g_drv.hang = true;
    CHECK(has(type("hitec listen 0 window=100000 rx=1"), "started"));
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_BUSY);
    CHECK_STR_EQ(passes(3u), "");
    o = type("hitec close 0");
    CHECK(has(o, "hitec done state=FREE"));
    CHECK(strstr(strstr(o, "hitec done") + 1, "hitec done") == NULL);
    CHECK_STR_EQ(passes(60u), "");
}

/* The raw example of docs/LinkHitec.md, line by line. */
TEST_CASE(the_raw_example_of_the_docs_runs)
{
    fresh();
    CHECK(has(type("w 0x34 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"),
              "ack page=0x34 off=4 n=18:"));
    CHECK(has(type("w 0x34 0 0x0001 1 0 0x8135"), "ack page=0x34 off=0"));
    CHECK_EQ(g_port.refusal, LINK_HITEC_REF_NONE);
    (void)passes(60u);
    CHECK(has(type("r 0x34 0 4"), "data page=0x34 off=0 n=4: 0x0012 0x0001"));
    g_drv.cap.n = 2u;
    g_drv.cap.byte[0].value = 0x2Eu;
    g_drv.cap.byte[0].at_ns = 51302u;
    g_drv.cap.byte[1].value = HITEC_C_ACK;
    g_drv.cap.byte[1].at_ns = 572135u;
    (void)type("w 0x34 4 0 0x0104 0x1261 0x8D00 0 0 0 0 0x05DC 0 2 0 0 0 "
               "0x6101 0 0 0");
    CHECK(has(type("w 0x34 0 0x0003 2 0x48A5 0xE56A"), "ack page=0x34 off=0"));
    CHECK_EQ(g_port.refusal, LINK_HITEC_REF_NONE);
    CHECK_EQ(g_drv.starts, 1);
    CHECK_EQ(g_drv.x.tx[3], 0x8D);
    (void)passes(2u);
    CHECK(has(type("r 0x34 0 16"),
              "data page=0x34 off=0 n=16: 0x0072 0x0002 0x3001 0x0002 "
              "0x1002 0x0000 0x002E 0x0000 0x2B2E"));
}

/* The console's requests are not the panel's: the link goes silent 200ms
 * after the panel's last request whatever the console does. */
TEST_CASE(the_console_does_not_hold_the_failsafe_off)
{
    link_msg_t req = { 0 };
    link_msg_t reply;

    fresh();
    req.op = LINK_OP_READ;
    req.page = PAGE_RO;
    req.count = 1;
    CHECK(link_dev_dispatch(&g_dev, &req, &reply, g_ms));
    for (unsigned t = 0; t < LINK_DEV_SILENCE_MS; t += 20u) {
        (void)type("r 0x11 0");
        (void)type("w 0x10 0 1");
        CHECK(!link_dev_tick(&g_dev, g_ms + t));
    }
    CHECK(link_dev_tick(&g_dev, g_ms + LINK_DEV_SILENCE_MS));
    CHECK(g_dev.failsafe);
}

int main(void)
{
    RUN(pages_lists_every_page);
    RUN(a_read_answers_data_and_a_refusal_is_a_nack);
    RUN(a_write_echoes_what_was_stored);
    RUN(a_line_is_edited_and_an_overlong_one_refused);
    RUN(output_goes_out_in_whole_lines_and_overflow_is_counted);
    RUN(a_line_longer_than_the_room_leaves_in_pieces);
    RUN(status_lines_said_meanwhile_leave_after_the_line_in_pieces);
    RUN(a_said_line_gets_one_lf_and_is_cut_at_the_longest);
    RUN(an_overflow_during_a_line_in_pieces_is_noted_after_it);
    RUN(hitec_open_and_a_classic_command_report_the_reply);
    RUN(hitec_commands_stage_name_the_crc_and_carry_the_key);
    RUN(hitec_power_stages_the_supply_and_reports_the_listen);
    RUN(hitec_on_a_build_without_the_page_says_so);
    RUN(the_family_commands_need_a_line_of_their_family);
    RUN(hitec_open_writes_the_receive_mode_of_a_d_series_line);
    RUN(a_close_of_a_watched_transaction_reports_once);
    RUN(the_raw_example_of_the_docs_runs);
    RUN(a_step_marked_more_carries_the_bit);
    RUN(the_console_does_not_hold_the_failsafe_off);
    return test_summary("page_console");
}
