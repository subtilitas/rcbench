/*
 * The bench console's `yge` command, shared/outputs/yge_console.c, on the
 * YGE_PROG page: each operation reaches the page in the frame a panel sends,
 * ENTER carries the key, its limits hold, and no command of it writes to an
 * ESC.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdarg.h>
#include <stdio.h>

#include "yge_console.h"
#include "yge_port_rig.h"

static char g_said[256];
static uint8_t g_nack;

static void io_read(uint8_t off, uint8_t n, uint16_t *out)
{
    yge_port_read(&g_p, off, n, out);
}

static uint8_t io_write(uint8_t off, uint8_t n, const uint16_t *in)
{
    return g_nack != 0u ? g_nack : yge_port_write(&g_p, off, n, in, now_ms());
}

static void io_say(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(g_said, sizeof(g_said), fmt, ap);
    va_end(ap);
}

static const yge_console_io_t k_io = { io_read, io_write, io_say };

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
    CHECK(yge_console_line(&k_io, tok, n));
}

TEST_CASE(a_session_from_the_console)
{
    rig_up();
    pass();
    line("yge");
    CHECK(strncmp(g_said, "yge state=0 ", 12u) == 0);
    line("yge open port 0");
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_STOPPING);
    CHECK(strncmp(g_said, "yge state=1 op=1 ", 17u) == 0);
    run_ms(100u);
    line("yge enter mv=7400 ma=1000 boot=1000");
    CHECK_EQ(refusal(), LINK_YGE_REF_NONE);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_POWER_OFF);
    run_while(LINK_YGE_POWER_OFF, 1000u);
    CHECK_EQ(g.on_mv, 7400u);
    CHECK_EQ(g.on_ma, 1000u);
    run_while(LINK_YGE_POWER_ON, 1000u);
    run_while(LINK_YGE_RUN, 10000u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_READY);
    line("yge read");
    run_while(LINK_YGE_RUN, 10000u);
    line("yge");
    CHECK(strstr(g_said, " entries=32 reads=1 type=5712 fw=103555 ") != NULL);
    line("yge leave");
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_OPEN);
    CHECK(!g.v.asked_on);
    line("yge stop");
    CHECK(flag(LINK_YP_F_STOPPED));
    line("yge clear");
    CHECK(!flag(LINK_YP_F_STOPPED));
    line("yge abort");
    line("yge close");
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    /* Not a write frame in all of it. */
    CHECK_EQ(sim_count(OPENYGE_FT_WRITE_PARAM_REQ, 0u), 0u);
}

TEST_CASE(by_hand_and_the_default_boot_wait)
{
    rig_up();
    pass();
    line("yge open port 2");
    run_ms(100u);
    line("yge enter hand");
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_HAND_WAIT);
    CHECK_EQ(g.ons + g.offs, 0);
    g.battery = true;
    line("yge done");
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_RUN);
    CHECK_EQ(g_p.boot_ms, OY_BOOT_MS);
    run_while(LINK_YGE_RUN, 20000u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_READY);
    CHECK_EQ(g.ons, 0);
}

TEST_CASE(what_it_does_not_take)
{
    rig_up();
    pass();
    line("yge open slot 0");
    CHECK(strncmp(g_said, "err usage: yge open", 19u) == 0);
    line("yge open port x");
    CHECK(strncmp(g_said, "err usage: yge open", 19u) == 0);
    line("yge open port 0");
    run_ms(100u);
    /* No voltage but the one asked for. */
    line("yge enter mv=7400");
    CHECK(strncmp(g_said, "err usage: yge enter", 20u) == 0);
    line("yge enter ma=1000");
    CHECK(strncmp(g_said, "err usage: yge enter", 20u) == 0);
    line("yge enter mv=7400 ma=1000 boot=");
    CHECK(strncmp(g_said, "err usage: yge enter", 20u) == 0);
    line("yge enter mv=70000 ma=1000");
    CHECK(strncmp(g_said, "err usage: yge enter", 20u) == 0);
    line("yge enter mv=7400 ma=1000 hold=3000");
    CHECK(strncmp(g_said, "err usage: yge enter", 20u) == 0);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_OPEN);
    /* A boot wait out of range: the page refuses it. */
    line("yge enter hand boot=999");
    CHECK_EQ(refusal(), LINK_YGE_REF_RANGE);
    /* There is no write. */
    line("yge write");
    CHECK(strncmp(g_said, "err usage: yge [", 16u) == 0);
    line("yge read now");
    CHECK(strncmp(g_said, "err usage: yge [", 16u) == 0);
    /* A page that refuses the window says so. */
    g_nack = LINK_NACK_BUSY;
    line("yge close");
    CHECK(strncmp(g_said, "yge state=", 10u) == 0);
    char want[16];

    (void)snprintf(want, sizeof(want), "nack %u\n", (unsigned)LINK_NACK_BUSY);
    line("yge open port 0");
    CHECK_STR_EQ(g_said, want);
    g_nack = 0u;
}

int main(void)
{
    RUN(a_session_from_the_console);
    RUN(by_hand_and_the_default_boot_wait);
    RUN(what_it_does_not_take);
    return test_summary("yge_console");
}
