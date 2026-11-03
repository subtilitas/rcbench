/*
 * The bench console's `esc` command, shared/outputs/esc_console.c, on the ESC_WIRE
 * page: each operation reaches the page in the frame a panel sends, its limits
 * hold, and no command of it writes to an ESC.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdarg.h>
#include <stdio.h>

#include "esc_console.h"
#include "esc_port_rig.h"

static char g_said[256];
static uint8_t g_nack;

static void io_read(uint8_t off, uint8_t n, uint16_t *out)
{
    esc_port_read(&g_p, off, n, out);
}

static uint8_t io_write(uint8_t off, uint8_t n, const uint16_t *in)
{
    return g_nack != 0u ? g_nack : esc_port_write(&g_p, off, n, in, esc.now / 1000u);
}

static void io_say(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(g_said, sizeof(g_said), fmt, ap);
    va_end(ap);
}

static const esc_console_io_t k_io = { io_read, io_write, io_say };

/* A line, split at its spaces. */
static void line(const char *s)
{
    static char buf[96];
    char *tok[12];
    unsigned n = 0u;

    (void)snprintf(buf, sizeof(buf), "%s", s);
    for (char *t = strtok(buf, " "); t != NULL && n < 12u; t = strtok(NULL, " ")) {
        tok[n++] = t;
    }
    CHECK(esc_console_line(&k_io, tok, n));
}

TEST_CASE(a_session_from_the_console)
{
    up_am_rig();
    pass();
    line("esc");
    CHECK(strncmp(g_said, "esc state=0 ", 12u) == 0);
    line("esc open slot 2");
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_STOPPING);
    CHECK(strncmp(g_said, "esc state=1 op=1 ", 17u) == 0);
    run_while(LINK_ESC_STOPPING, 200u);
    CHECK_EQ(g.last_pin, 5u);
    line("esc enter mv=5000 ma=500 hold=500 off=0");
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_POWER_OFF);
    run_while(LINK_ESC_POWER_OFF, 5000u);
    run_while(LINK_ESC_LEAD, 200u);
    CHECK_EQ(g.on_mv, 5000u);
    CHECK_EQ(g.on_ma, 500u);
    run_while(LINK_ESC_RUN, 20000u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_READY);
    line("esc read");
    run_while(LINK_ESC_RUN, 20000u);
    CHECK_EQ(reg(LINK_EW_READS), 1u);
    line("esc stop");
    CHECK(flag(LINK_EW_F_STOPPED));
    line("esc clear");
    CHECK(!flag(LINK_EW_F_STOPPED));
    line("esc close");
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
    CHECK_EQ(g.gives, 1);
}

TEST_CASE(by_hand_and_a_port)
{
    up_am_rig();
    pass();
    line("esc open port 0");
    run_while(LINK_ESC_STOPPING, 200u);
    CHECK_EQ(g.last_pin, 13u);
    line("esc enter hand hold=500");
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_HAND_WAIT);
    line("esc done");
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_RUN);
    line("esc abort");
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_OPEN);
    line("esc leave");
    CHECK_EQ(refusal(), LINK_ESC_REF_NOT_OPEN);
}

TEST_CASE(what_it_does_not_take)
{
    up_am_rig();
    pass();
    static const char *const k_bad[] = {
        "esc open", "esc open slot", "esc open tank 1", "esc open slot x",
        "esc open slot 256", "esc enter mv=", "esc enter mv=x",
        "esc enter mv=70000", "esc enter ma=70000", "esc enter off=70000",
        "esc enter wrong=1", "esc enter", "esc enter mv=5000",
        "esc enter ma=500", "esc enter hold=500", "esc write", "esc read 1",
    };

    for (size_t i = 0u; i < sizeof(k_bad) / sizeof(k_bad[0]); ++i) {
        g_said[0] = '\0';
        line(k_bad[i]);
        CHECK(strncmp(g_said, "err usage", 9u) == 0);
    }
    CHECK_EQ(reg(LINK_EW_SEQ), 0u);
    g_nack = LINK_NACK_BUSY;
    line("esc open slot 2");
    CHECK(strncmp(g_said, "nack ", 5u) == 0);
    line("esc stop");
    CHECK(strncmp(g_said, "esc state=0", 11u) == 0);
    g_nack = 0u;
}

int main(void)
{
    RUN(a_session_from_the_console);
    RUN(by_hand_and_a_port);
    RUN(what_it_does_not_take);
    return test_summary("esc_console");
}
