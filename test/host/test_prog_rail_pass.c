/*
 * The pass-through beside a programming session on the remote servo node's
 * common rail: the PASS page (shared/outputs/pass_page.c) and the rail
 * (prog_rail.c) wired as firmware/iomcu/src/iomcu_rsn.c and iomcu_pass.c
 * wire them.  The rail's active channels are the slots' and programming
 * ports' ORed with pass_page_active(); the ports prog_rail_set() newly
 * drops go to pass_page_force_off(); the capture's window is flagged as
 * thru_service() flags it.
 *
 * Under test: without the port switches (FITTED 0) CONNECT and the
 * session's ON refused with OTHERS while a channel is on PASS-THROUGH, and
 * taken once it is OFF or BENCH, the session's own channel not counted;
 * with the switches (FITTED 0x1FF) a session at another voltage dropping a
 * powered PASS-THROUGH port, whose SOURCE then reads OFF and whose pad
 * MIRROR no longer drives, a BENCH channel a slot holds kept on BENCH, and
 * a session at the rail's voltage dropping nothing; a lapped ring during a
 * session publishing FLAGS bits 0 and 1 in that window, bit 1 alone in the
 * next and neither after the session.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "prog_rail_rig.h"

#include "pass_decode.h"
#include "pass_page.h"
#include "pass_ring.h"

#define K     4          /* the session's port, S5 */
#define K_BIT 0x0010u

static pass_page_t g_pa;
static pass_dec_t  g_dec;

static const pass_ctx_t SAFE = { false, 0u };

/* A SOURCE write while SAFE. */
static uint8_t src(unsigned k, uint16_t v)
{
    return pass_page_write(&g_pa, (uint8_t)LINK_PA_SOURCE(k), 1u, &v, &SAFE);
}

static uint16_t pa_reg(unsigned r)
{
    uint16_t v = 0xDEADu;
    pass_page_read(&g_pa, (uint8_t)r, 1u, &v);
    return v;
}

/* active_ports(): the slots' and ports' channels (@p bound) and
 * PASS-THROUGH's. */
static void active(uint16_t bound)
{
    g_rctx.active = (uint16_t)(bound | pass_page_active(&g_pa));
}

/* rsn_prog_set(): the session's request, then the ports it newly dropped
 * to OFF; @p busy the channels whose pin a slot holds. */
static bool prog_set(bool on, uint16_t mv, uint16_t ma, uint16_t busy)
{
    const uint16_t was = g_rail.dropped;
    const bool taken = rail_set(on, mv, ma);
    (void)pass_page_force_off(&g_pa, (uint16_t)(g_rail.dropped & ~was), busy);
    return taken;
}

/* prog_set() ON and the passes until port k is on, 400ms at most. */
static bool prog_on(uint16_t mv, uint16_t ma, uint16_t busy)
{
    if (!prog_set(true, mv, ma, busy)) {
        return false;
    }
    for (unsigned i = 0; i < 400u && (ports_on() & K_BIT) == 0u; ++i) {
        rail_pass();
    }
    return true;
}

/* The session holds the port, no write to the servo's memory. */
static void hold(uint32_t now)
{
    g_rnow = now;
    rail_tell(true, false);
}

static void start(bool switches)
{
    rig_start(switches, K, true);
    pass_page_init(&g_pa, true, 0u);
    pass_dec_init(&g_dec, 0u);
}

TEST_CASE(connect_is_refused_without_switches_while_a_channel_passes_through)
{
    start(false);
    active(0u);
    CHECK_EQ(prog_rail_connect(&g_pw, K, &g_rctx), PROG_RAIL_OK);
    /* BENCH is no activity of its own: every channel starts on it. */
    CHECK_EQ(pa_reg(LINK_PA_SOURCE(2)), LINK_PA_SRC_BENCH);
    CHECK_EQ(src(2, LINK_PA_SRC_PASS), 0u);
    active(0u);
    CHECK_EQ(g_rctx.active, 1u << 2);
    CHECK_EQ(prog_rail_connect(&g_pw, K, &g_rctx), PROG_RAIL_OTHERS);
    const int k_of[2] = { K, PROG_RAIL_NO_PORT };
    CHECK_EQ(prog_rail_connect_mask(&g_pw, k_of, 2u, 0u, &g_rctx), 0x01u);
    CHECK(!prog_rail_open_ok(&g_pw, K, 0u, &g_rctx));
    /* The session's ON is refused the same, with nothing switched. */
    hold(0u);
    CHECK(!prog_set(true, RAIL_MV, 1000u, 0u));
    CHECK_EQ(ports_on(), 0u);
    CHECK(!power_rail_driven(&g_pw));
    CHECK_EQ(pa_reg(LINK_PA_SOURCE(2)), LINK_PA_SRC_PASS);
    /* All 9 channels on PASS-THROUGH but the session's own: refused. */
    for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
        CHECK_EQ(src(k, (k == (unsigned)K) ? LINK_PA_SRC_OFF
                                           : LINK_PA_SRC_PASS), 0u);
    }
    active(0u);
    CHECK_EQ(prog_rail_connect(&g_pw, K, &g_rctx), PROG_RAIL_OTHERS);
    /* Only the session's own channel on PASS-THROUGH: not counted. */
    for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
        CHECK_EQ(src(k, (k == (unsigned)K) ? LINK_PA_SRC_PASS
                                           : LINK_PA_SRC_OFF), 0u);
    }
    active(0u);
    CHECK_EQ(prog_rail_connect(&g_pw, K, &g_rctx), PROG_RAIL_OK);
    /* OFF everywhere: CONNECT and the ON taken. */
    CHECK_EQ(src(K, LINK_PA_SRC_OFF), 0u);
    active(0u);
    CHECK_EQ(g_rctx.active, 0u);
    CHECK_EQ(prog_rail_connect(&g_pw, K, &g_rctx), PROG_RAIL_OK);
    CHECK(prog_on(RAIL_MV, 1000u, 0u));
    CHECK_EQ(ports_on(), 0x01FFu);
    /* Without the switches nothing is dropped: the rail cycles whole. */
    CHECK_EQ(g_rail.dropped, 0u);
}

TEST_CASE(another_voltage_drops_a_pass_through_port_to_off)
{
    start(true);
    CHECK_EQ(pw_write(LINK_PW_RAIL, 1u, 0u), 0u);
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, 0x0007u, 0u), 0u);
    CHECK_EQ(ports_on(), 0x0007u);
    /* S1 passes through, S2 is BENCH with a slot on its pin, S3 BENCH
     * without one; S9 passes through on a port that is off. */
    CHECK_EQ(src(0, LINK_PA_SRC_PASS), 0u);
    CHECK_EQ(src(8, LINK_PA_SRC_PASS), 0u);
    CHECK_EQ(pass_page_mirror(&g_pa, true), (1u << 0) | (1u << 8));
    active(1u << 1);
    /* With the switches an active channel does not refuse CONNECT. */
    CHECK_EQ(prog_rail_connect(&g_pw, K, &g_rctx), PROG_RAIL_OK);
    hold(0u);
    CHECK(prog_on(6000u, 1000u, 1u << 1));
    CHECK_EQ(ports_on(), K_BIT);
    CHECK_EQ(g_rail.dropped, 0x0007u);
    CHECK_EQ(pa_reg(LINK_PA_SOURCE(0)), LINK_PA_SRC_OFF);
    CHECK_EQ(pa_reg(LINK_PA_SOURCE(1)), LINK_PA_SRC_BENCH);
    CHECK_EQ(pa_reg(LINK_PA_SOURCE(2)), LINK_PA_SRC_OFF);
    /* A port the session did not switch off keeps its source. */
    CHECK_EQ(pa_reg(LINK_PA_SOURCE(8)), LINK_PA_SRC_PASS);
    CHECK_EQ(pass_page_mirror(&g_pa, true), 1u << 8);
    CHECK_EQ(pass_page_held(&g_pa) & 0x0007u, (1u << 0) | (1u << 2));
    /* A second request of the session drops nothing new and leaves a
     * source written since as it is. */
    CHECK_EQ(src(0, LINK_PA_SRC_PASS), 0u);
    CHECK(prog_set(true, 6000u, 1000u, 1u << 1));
    CHECK_EQ(pa_reg(LINK_PA_SOURCE(0)), LINK_PA_SRC_PASS);
}

TEST_CASE(the_rails_voltage_drops_no_pass_through_port)
{
    start(true);
    CHECK_EQ(pw_write(LINK_PW_RAIL, 1u, 0u), 0u);
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, 0x0003u, 0u), 0u);
    CHECK_EQ(src(0, LINK_PA_SRC_PASS), 0u);
    active(0u);
    hold(0u);
    CHECK(prog_on(RAIL_MV, 1000u, 0u));
    CHECK_EQ(ports_on(), 0x0003u | K_BIT);
    CHECK_EQ(g_rail.dropped, 0u);
    CHECK_EQ(pa_reg(LINK_PA_SOURCE(0)), LINK_PA_SRC_PASS);
    CHECK_EQ(pass_page_mirror(&g_pa, true), 1u << 0);
}

/* thru_service(): one drain of the ring, the window's flags, the tick. */
static uint32_t     g_states[PASS_RING_EVENTS];
static uint32_t     g_stamps[PASS_RING_EVENTS];
static uint32_t     g_head;
static pass_ring_t  g_ring;

static void put(uint32_t state, uint32_t stamp)
{
    g_states[g_head & (PASS_RING_EVENTS - 1u)] = state;
    g_stamps[g_head & (PASS_RING_EVENTS - 1u)] = stamp;
    ++g_head;
}

static bool service(bool session, uint32_t now_ms)
{
    const pass_drain_t r = pass_ring_drain(&g_ring, g_states, g_stamps,
                                           g_head, now_ms * 1000u, &g_dec);
    uint16_t f = 0u;
    if (r.lapped) {
        f |= LINK_PA_F_LAPPED;
    }
    if (session) {
        f |= LINK_PA_F_SESSION;
    }
    pass_page_flag(&g_pa, f);
    return pass_page_tick(&g_pa, &g_dec, now_ms);
}

TEST_CASE(a_lapped_window_during_a_session_sets_flags_bits_0_and_1)
{
    start(true);
    memset(g_states, 0, sizeof(g_states));
    memset(g_stamps, 0, sizeof(g_stamps));
    g_head = 0u;
    pass_ring_init(&g_ring);
    put(0u, 10u);
    CHECK(!service(false, 1u));
    CHECK(service(false, 50u));
    CHECK_EQ(pa_reg(LINK_PA_FLAGS), 0u);
    /* A session holds the rail; a flash window lets the ring lap. */
    hold(50u);
    CHECK(prog_on(RAIL_MV, 1000u, 0u));
    for (uint32_t i = 0; i < PASS_RING_EVENTS + 5u; ++i) {
        put((i & 1u) << PASS_IN_BIT(0), 51000u + i);
    }
    CHECK(!service(g_rail.held, 99u));
    CHECK(service(g_rail.held, 100u));
    CHECK_EQ(pa_reg(LINK_PA_FLAGS), LINK_PA_F_LAPPED | LINK_PA_F_SESSION);
    /* The next window: the session only. */
    put(0u, 120000u);
    CHECK(service(g_rail.held, 150u));
    CHECK_EQ(pa_reg(LINK_PA_FLAGS), LINK_PA_F_SESSION);
    /* The session lets go: no flag. */
    CHECK(prog_set(false, 0u, 0u, 0u));
    g_rnow = 160u;
    rail_tell(false, false);
    CHECK(service(g_rail.held, 200u));
    CHECK_EQ(pa_reg(LINK_PA_FLAGS), 0u);
}

int main(void)
{
    RUN(connect_is_refused_without_switches_while_a_channel_passes_through);
    RUN(another_voltage_drops_a_pass_through_port_to_off);
    RUN(the_rails_voltage_drops_no_pass_through_port);
    RUN(a_lapped_window_during_a_session_sets_flags_bits_0_and_1);
    return test_summary("prog_rail_pass");
}
