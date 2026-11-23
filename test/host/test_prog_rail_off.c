/*
 * Every OFF that stands ends a session's pending ON on the remote servo
 * node's common rail (shared/outputs/prog_rail.c over power_page.c and the
 * SUPPLY page), with the port switches (FITTED 0x1FF) and without them
 * (FITTED 0).
 *
 * The session holds the port and the SUPPLY page (ports_tell_supply()),
 * its ON at 5000mV waits for a PD mini at 8400mV to come down, with and
 * without a write to the servo's memory running.  Then one OFF: the
 * panel's SUPPLY OFF (with a write running the PD mini stays on for it,
 * supply_page_t.off_held), RAIL 0, PORT_WANT without bit k (with the
 * switches), a trip (POWER's TRIPPED alone, and with rsn_pass()'s
 * prog_rail_off()), a lost heartbeat (the context alone, and with
 * rsn_off()), and the session's own OFF.
 *
 * Under test: the pending ON ends, port k is not switched on while the PD
 * mini reads the voltage for PROG_RAIL_WAIT_MS and more, and the
 * session's next ON is refused while the OFF stands.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "prog_rail_rig.h"

#define K     4          /* the session's port, S5 */
#define K_BIT 0x0010u

/** @brief The OFFs, each against a pending ON. */
typedef enum {
    OFF_SUPPLY = 0,      /* the panel's SUPPLY OFF                      */
    OFF_RAIL_0,          /* the panel's RAIL 0                          */
    OFF_WANT,            /* the panel's PORT_WANT without bit k         */
    OFF_TRIPPED,         /* POWER's TRIPPED, judged alone               */
    OFF_TRIP,            /* a trip with rsn_pass()'s prog_rail_off()    */
    OFF_BEAT,            /* a lost heartbeat, judged alone              */
    OFF_BEAT_EDGE,       /* a lost heartbeat with rsn_off()             */
    OFF_SESSION,         /* the session's own OFF                       */
    OFF_KINDS
} off_kind_t;

/* The session holds the port and the SUPPLY page, writing when
 * @p writing: ports_tell_supply(). */
static void hold(bool writing)
{
    rail_tell(true, writing);
    supply_page_hold(&g_sp, true);
    supply_page_port_writes(&g_sp, writing);
}

/* The PD mini by hand at 8400mV, the panel's S1 and S2 on with the
 * switches, the session's ON at 5000mV waiting for it. */
static void waiting(bool switches, bool writing)
{
    const uint16_t on[3] = { 1u, 8400u, RAIL_MA };

    rig_start(switches, K, false);
    CHECK_EQ(supply_page_write(&g_sp, LINK_SP_OUTPUT, 3u, on, &g_sout, true),
             0u);
    sp_pass(true);
    sp_pass(true);
    if (switches) {
        CHECK_EQ(pw_write(LINK_PW_RAIL, 1u, 0u), 0u);
        CHECK_EQ(pw_write(LINK_PW_PORT_WANT, 0x0003u, 0u), 0u);
    }
    g_pd_by_hand = true;
    g_pd.st.v_mv = 8400u;
    g_rnow = 0u;
    hold(writing);
    CHECK(rail_set(true, 5000u, 1000u));
    CHECK(g_rail.pending);
    for (unsigned i = 0; i < 20u; ++i) {
        rail_pass();
    }
    CHECK(g_rail.pending);
    CHECK_EQ(ports_on() & K_BIT, 0u);
}

/* The OFF @p kind, as the firmware applies it. */
static void apply(off_kind_t kind)
{
    switch (kind) {
    case OFF_SUPPLY:
        sp_panel_off();
        break;
    case OFF_RAIL_0:
        CHECK_EQ(pw_write(LINK_PW_RAIL, 0u, g_rnow), 0u);
        break;
    case OFF_WANT:
        /* The others stay off for the session's other voltage. */
        CHECK_EQ(pw_write(LINK_PW_PORT_WANT, 0u, g_rnow), 0u);
        break;
    case OFF_TRIPPED:
        power_trip(&g_pw, LINK_PW_TRIP_THERMAL);
        break;
    case OFF_TRIP:
        power_trip(&g_pw, LINK_PW_TRIP_THERMAL);
        prog_rail_off(&g_rail);
        break;
    case OFF_BEAT:
        g_rctx.power.beat_alive = false;
        break;
    case OFF_BEAT_EDGE:
        g_rctx.power.beat_alive = false;
        power_off(&g_pw);
        prog_rail_off(&g_rail);
        break;
    case OFF_SESSION:
    case OFF_KINDS:
    default:
        CHECK(rail_set(false, 0u, 0u));
        break;
    }
}

/* @p kind against a pending ON: the ON ends, port k stays off with the PD
 * mini at the voltage, and the session's next ON is refused while it
 * stands. */
static void off_ends_the_wait(bool switches, bool writing, off_kind_t kind)
{
    waiting(switches, writing);
    apply(kind);
    g_pd.st.v_mv = 5000u;               /* the PD mini there after all */
    for (unsigned i = 0; i < PROG_RAIL_WAIT_MS + 500u; ++i) {
        rail_pass();
        CHECK_EQ(ports_on() & K_BIT, 0u);
        CHECK(!g_rail.pending);
    }
    if (kind != OFF_SESSION) {
        CHECK(prog_rail_judge(&g_rail, &g_pw, &g_sp, &g_rctx)
              != PROG_RAIL_OK);
        CHECK(!rail_set(true, 5000u, 1000u));
        CHECK(!g_rail.pending);
        CHECK_EQ(ports_on() & K_BIT, 0u);
    }
}

TEST_CASE(every_off_ends_a_pending_on)
{
    for (unsigned sw = 0; sw < 2u; ++sw) {
        for (unsigned wr = 0; wr < 2u; ++wr) {
            for (unsigned kind = 0; kind < (unsigned)OFF_KINDS; ++kind) {
                if (kind == (unsigned)OFF_WANT && sw == 0u) {
                    continue;           /* PORT_WANT needs the switches */
                }
                off_ends_the_wait(sw != 0u, wr != 0u, (off_kind_t)kind);
            }
        }
    }
}

TEST_CASE(a_supply_off_held_for_a_write_keeps_the_pd_mini_on_not_port_k)
{
    for (unsigned sw = 0; sw < 2u; ++sw) {
        waiting(sw != 0u, true);
        sp_panel_off();
        CHECK(supply_page_off_held(&g_sp));
        CHECK(g_sp.sticky);
        g_pd.st.v_mv = 5000u;
        rail_pass();
        CHECK(!g_rail.pending);
        CHECK(pd_wants_on());           /* the write's PD mini stays on */
        CHECK_EQ(prog_rail_judge(&g_rail, &g_pw, &g_sp, &g_rctx),
                 PROG_RAIL_OFF_STANDS);
        for (unsigned i = 0; i < 1000u; ++i) {
            rail_pass();
            CHECK_EQ(ports_on() & K_BIT, 0u);
        }
        /* The session lets go: the SUPPLY page's OFF ends with it. */
        rail_tell(false, false);
        supply_page_hold(&g_sp, false);
        CHECK(!g_sp.sticky);
        CHECK_EQ(prog_rail_judge(&g_rail, &g_pw, &g_sp, &g_rctx),
                 PROG_RAIL_OK);
    }
}

int main(void)
{
    RUN(every_off_ends_a_pending_on);
    RUN(a_supply_off_held_for_a_write_keeps_the_pd_mini_on_not_port_k);
    return test_summary("prog_rail_off");
}
