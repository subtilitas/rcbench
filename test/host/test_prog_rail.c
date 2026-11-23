/*
 * Programming on the remote servo node's common rail
 * (shared/outputs/prog_rail.c over power_page.c and the SUPPLY page), with
 * the port switches (FITTED 0x1FF) and without them (FITTED 0).
 *
 * Under test: the session's view of its supply through port k's switch;
 * its OFF acting on RAIL without the switches and on PORT_WANT bit k only
 * with them, and taken without a heartbeat or a port; its ON refused in
 * the order of the rules (no port, the panel's OFF standing, no heartbeat,
 * a chip before A4, the critical limits, a throttle port, a BEC found, and
 * without the switches another active channel) with nothing switched;
 * CONNECT's rules alone; another voltage than the rail's switching every
 * other port off with the switches and keeping them off (a new PORT_WANT
 * bit refused) until the session lets go, the same voltage leaving them
 * on; the PD mini set off and on again only while port k is off, a raise
 * under a powered port refused; an ON from RAIL 0 leaving the other ports
 * off; the panel's write that would switch the session's port on refused,
 * a RAIL 0 in it taken; the panel's OFF during a write to servo memory
 * taken at once for every other port and held for port k until the write
 * ends, to the millisecond at SUPPLY_PAGE_WRITE_WAIT_MS and across the
 * 2^32ms wrap, after which the session's ON is refused until it lets go;
 * a lost heartbeat not held; NULL arguments.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "prog_rail_rig.h"

#define K     4          /* the session's port, S5 */
#define K_BIT 0x0010u

/* With the switches: the panel's ports S1 and S2 on at RAIL_MV. */
static void two_ports_on(void)
{
    CHECK_EQ(pw_write(LINK_PW_RAIL, 1u, 0u), 0u);
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, 0x0003u, 0u), 0u);
    CHECK_EQ(ports_on(), 0x0003u);
}

/* The session holds the port and writes when @p writing. */
static void hold(bool writing, uint32_t now)
{
    g_rnow = now;
    rail_tell(true, writing);
}

TEST_CASE(the_view_is_the_pd_mini_through_the_ports_switch)
{
    supply_port_view_t v;

    rig_start(true, K, true);
    rail_view(&v);
    CHECK(v.online);
    CHECK(!v.asked_on);
    CHECK(!v.on);
    CHECK(v.off);                       /* the port is off: PD mini or not */
    CHECK_EQ(v.set_mv, RAIL_MV);
    two_ports_on();
    rail_view(&v);
    CHECK(!v.asked_on);                 /* S1 and S2 are not the session's */
    CHECK(rail_on( RAIL_MV, 1000u));
    CHECK_EQ(ports_on(), 0x0003u | K_BIT);
    sp_pass(true);
    rail_view(&v);
    CHECK(v.asked_on);
    CHECK(v.on);
    CHECK(!v.off);
    /* Without a port the session sees nothing on. */
    prog_rail_port(&g_rail, 9);
    rail_view(&v);
    CHECK(!v.asked_on);
    CHECK(v.off);
}

TEST_CASE(an_off_cycles_the_rail_without_the_switches_and_port_k_with_them)
{
    rig_start(false, K, true);
    CHECK(rail_on( RAIL_MV, 1000u));
    CHECK(power_rail_driven(&g_pw));
    CHECK_EQ(ports_on(), 0x01FFu);
    CHECK(rail_set(false, 0u, 0u));
    CHECK(!power_rail_driven(&g_pw));
    CHECK_EQ(ports_on(), 0u);           /* every servo on the rail cycles */
    CHECK(pd_wants_on());               /* the PD mini stays as it is */

    rig_start(true, K, true);
    two_ports_on();
    CHECK(rail_on( RAIL_MV, 1000u));
    CHECK(rail_set(false, 0u, 0u));
    CHECK(power_rail_driven(&g_pw));
    CHECK_EQ(ports_on(), 0x0003u);      /* S5 only */
    CHECK_EQ(pw_reg(LINK_PW_PORT_WANT), 0x0003u);
    /* An OFF is taken without a heartbeat and without a port. */
    CHECK(rail_on( RAIL_MV, 1000u));
    g_rctx.power.beat_alive = false;
    CHECK(rail_set(false, 0u, 0u));
    CHECK_EQ(ports_on(), 0x0003u);
    prog_rail_port(&g_rail, -1);
    CHECK(rail_set(false, 0u, 0u));
    CHECK(!rail_set(true, RAIL_MV, 1000u));
}

TEST_CASE(an_on_is_refused_in_the_order_of_the_rules)
{
    rig_start(false, K, true);
    prog_rail_port(&g_rail, -1);
    CHECK_EQ(prog_rail_judge(&g_rail, &g_pw, &g_rctx), PROG_RAIL_PORT);
    prog_rail_port(&g_rail, K);
    g_rail.sticky = true;
    CHECK_EQ(prog_rail_judge(&g_rail, &g_pw, &g_rctx), PROG_RAIL_OFF_STANDS);
    g_rail.sticky = false;
    g_rctx.power.beat_alive = false;
    CHECK_EQ(prog_rail_judge(&g_rail, &g_pw, &g_rctx), PROG_RAIL_UNSAFE);
    g_rctx.power.beat_alive = true;
    g_pw.chip_ok = false;
    CHECK_EQ(prog_rail_judge(&g_rail, &g_pw, &g_rctx), PROG_RAIL_UNSAFE);
    g_pw.chip_ok = true;
    g_rctx.power.limits_held = false;
    CHECK_EQ(prog_rail_judge(&g_rail, &g_pw, &g_rctx), PROG_RAIL_LIMITS);
    g_rctx.power.limits_held = true;
    g_rctx.power.throttle = K_BIT;
    CHECK_EQ(prog_rail_judge(&g_rail, &g_pw, &g_rctx), PROG_RAIL_THROTTLE);
    g_rctx.power.throttle = 0x0001u;    /* another port's: not this rule */
    g_pw.bec = 0x0100u;
    CHECK_EQ(prog_rail_judge(&g_rail, &g_pw, &g_rctx), PROG_RAIL_BEC);
    g_pw.bec = 0u;
    g_rctx.active = 0x0001u;
    CHECK_EQ(prog_rail_judge(&g_rail, &g_pw, &g_rctx), PROG_RAIL_OTHERS);
    /* Each refusal switches nothing. */
    CHECK(!rail_set(true, RAIL_MV, 1000u));
    CHECK(!power_rail_driven(&g_pw));
    g_rctx.active = K_BIT;              /* its own channel is no other */
    CHECK_EQ(prog_rail_judge(&g_rail, &g_pw, &g_rctx), PROG_RAIL_OK);
    CHECK(rail_on( RAIL_MV, 1000u));
    CHECK(power_rail_driven(&g_pw));
    CHECK_EQ(prog_rail_judge(NULL, &g_pw, &g_rctx), PROG_RAIL_PORT);
    CHECK_EQ(prog_rail_judge(&g_rail, NULL, &g_rctx), PROG_RAIL_PORT);
    CHECK_EQ(prog_rail_judge(&g_rail, &g_pw, NULL), PROG_RAIL_PORT);
}

TEST_CASE(connect_is_refused_with_another_channel_active_without_switches)
{
    rig_start(false, K, true);
    for (unsigned j = 0; j < LINK_PW_PORTS; ++j) {
        g_rctx.active = (uint16_t)(1u << j);
        CHECK_EQ(prog_rail_connect(&g_pw, K, &g_rctx),
                 j == (unsigned)K ? PROG_RAIL_OK : PROG_RAIL_OTHERS);
    }
    g_rctx.active = 0x01FFu & ~K_BIT;
    CHECK_EQ(prog_rail_connect(&g_pw, K, &g_rctx), PROG_RAIL_OTHERS);
    /* With the switches the other channels may stay active. */
    rig_start(true, K, true);
    g_rctx.active = 0x01FFu;
    CHECK_EQ(prog_rail_connect(&g_pw, K, &g_rctx), PROG_RAIL_OK);
    CHECK(rail_on( RAIL_MV, 1000u));
    CHECK_EQ(prog_rail_connect(NULL, K, &g_rctx), PROG_RAIL_PORT);
    CHECK_EQ(prog_rail_connect(&g_pw, K, NULL), PROG_RAIL_PORT);
}

TEST_CASE(connect_is_refused_with_a_bec_found_at_500mv_not_490mv)
{
    for (unsigned sw = 0; sw < 2u; ++sw) {
        rig_start(sw != 0u, K, true);
        rig_window(1u, 490);
        CHECK_EQ(pw_reg(LINK_PW_BEC), 0u);
        CHECK_EQ(prog_rail_connect(&g_pw, K, &g_rctx), PROG_RAIL_OK);
        rig_window(2u, 500);
        CHECK_EQ(pw_reg(LINK_PW_BEC), 0x01FFu);
        CHECK_EQ(prog_rail_connect(&g_pw, K, &g_rctx), PROG_RAIL_BEC);
        CHECK(!rail_set(true, RAIL_MV, 1000u));
        CHECK(!power_rail_driven(&g_pw));
        rig_window(3u, 0);              /* the + link opened */
        CHECK_EQ(prog_rail_connect(&g_pw, K, &g_rctx), PROG_RAIL_OK);
        CHECK(rail_on( RAIL_MV, 1000u));
    }
}

TEST_CASE(another_voltage_switches_the_other_ports_off_with_the_switches)
{
    rig_start(true, K, true);
    two_ports_on();
    hold(false, 0u);
    CHECK(rail_on( 6000u, 1000u));
    CHECK_EQ(ports_on(), K_BIT);
    CHECK_EQ(pw_reg(LINK_PW_PORT_WANT), K_BIT);
    CHECK_EQ(g_rail.dropped, 0x0003u);
    CHECK(g_rail.alone);
    sp_pass(true);
    CHECK_EQ(g_pd.want_mv, 6000u);
    /* For the session's duration a new bit is refused, RAIL 0 taken. */
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, K_BIT | 0x0001u, 10u),
             (uint8_t)LINK_NACK_BUSY);
    CHECK_EQ(ports_on(), K_BIT);
    /* A write starting with RAIL 0 and a new bit: refused, RAIL 0 taken. */
    const uint16_t four[4] = { 0u, 0u, 0u, K_BIT | 0x0001u };
    CHECK_EQ(prog_rail_write(&g_rail, &g_pw, 0u, 4u, four, &g_rctx, 10u),
             (uint8_t)LINK_NACK_BUSY);
    CHECK_EQ(ports_on(), 0u);
    CHECK_EQ(pw_reg(LINK_PW_PORT_WANT), K_BIT);
    /* The session lets go: a new hold brings them back. */
    CHECK(rail_set(false, 0u, 0u));
    g_rnow = 20u;
    rail_tell(false, false);
    CHECK(!g_rail.alone);
    CHECK_EQ(g_rail.dropped, 0u);
    CHECK_EQ(ports_on(), 0u);
    CHECK_EQ(pw_write(LINK_PW_RAIL, 1u, 30u), 0u);
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, 0x0003u, 30u), 0u);
    CHECK_EQ(ports_on(), 0x0003u);
}

TEST_CASE(the_same_voltage_leaves_the_other_ports_on)
{
    rig_start(true, K, true);
    two_ports_on();
    hold(false, 0u);
    CHECK(rail_on( RAIL_MV, 500u));
    CHECK_EQ(ports_on(), 0x0003u | K_BIT);
    CHECK(!g_rail.alone);
    CHECK_EQ(g_rail.dropped, 0u);
    sp_pass(true);
    CHECK_EQ(g_pd.want_ma, 500u);       /* the lower limit, for all */
    /* A cycle of port k only. */
    CHECK(rail_set(false, 0u, 0u));
    CHECK_EQ(ports_on(), 0x0003u);
    CHECK(rail_on( RAIL_MV, 500u));
    CHECK_EQ(ports_on(), 0x0003u | K_BIT);
}

TEST_CASE(the_pd_mini_moves_only_while_port_k_is_off)
{
    /* Without the switches, the rail off: off and on again at 6000mV. */
    rig_start(false, K, true);
    hold(false, 0u);
    CHECK(rail_on( 6000u, 1000u));
    sp_pass(true);
    CHECK(pd_wants_on());
    CHECK_EQ(g_pd.want_mv, 6000u);
    /* With the rail on, a raise under the servo is refused, nothing moves;
     * a lower voltage is taken. */
    CHECK(!rail_set(true, 7000u, 1000u));
    sp_pass(true);
    CHECK_EQ(g_pd.want_mv, 6000u);
    CHECK(power_rail_driven(&g_pw));
    CHECK(rail_on( 5000u, 1000u));
    sp_pass(true);
    CHECK_EQ(g_pd.want_mv, 5000u);
    /* The PD mini off: switched on at the session's voltage. */
    rig_start(true, K, false);
    CHECK(!pd_wants_on());
    CHECK(rail_on( 5500u, 800u));
    sp_pass(true);
    CHECK(pd_wants_on());
    CHECK_EQ(g_pd.want_mv, 5500u);
    CHECK_EQ(g_pd.want_ma, 800u);
    /* A PD mini that refuses: nothing switched on. */
    rig_start(true, K, false);
    g_rctx.power.beat_alive = false;
    CHECK(!rail_set(true, 5500u, 800u));
    CHECK_EQ(ports_on(), 0u);
}

TEST_CASE(an_on_from_rail_0_leaves_the_other_ports_off)
{
    rig_start(true, K, true);
    two_ports_on();
    CHECK_EQ(pw_write(LINK_PW_RAIL, 0u, 0u), 0u);
    CHECK_EQ(pw_reg(LINK_PW_PORT_WANT), 0x0003u);  /* RAIL 0 keeps them */
    CHECK(rail_on( RAIL_MV, 1000u));
    CHECK_EQ(ports_on(), K_BIT);
    CHECK_EQ(pw_reg(LINK_PW_PORT_WANT), K_BIT);
    CHECK_EQ(g_rail.dropped, 0x0003u);
}

TEST_CASE(the_panel_cannot_switch_the_sessions_port_on)
{
    rig_start(false, K, true);
    hold(false, 0u);
    CHECK(rail_on( RAIL_MV, 1000u));
    CHECK(rail_set(false, 0u, 0u));     /* the cycle's off time */
    CHECK_EQ(pw_write(LINK_PW_RAIL, 1u, 0u), (uint8_t)LINK_NACK_BUSY);
    CHECK(!power_rail_driven(&g_pw));
    rig_start(true, K, true);
    two_ports_on();
    hold(false, 0u);
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, 0x0003u | K_BIT, 0u),
             (uint8_t)LINK_NACK_BUSY);
    CHECK_EQ(ports_on(), 0x0003u);
    /* RAIL 0 is taken. */
    const uint16_t two[4] = { 0u, 0u, 0u, 0x0003u | K_BIT };
    CHECK_EQ(prog_rail_write(&g_rail, &g_pw, 0u, 1u, two, &g_rctx, 0u), 0u);
    CHECK_EQ(ports_on(), 0u);
    /* Without the session the panel's rules alone. */
    g_rnow = 0u;
    rail_tell(false, false);
    CHECK_EQ(pw_write(LINK_PW_RAIL, 1u, 0u), 0u);
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, K_BIT, 0u), 0u);
    CHECK_EQ(ports_on(), K_BIT);
    CHECK_EQ(prog_rail_write(NULL, &g_pw, 0u, 1u, two, &g_rctx, 0u),
             (uint8_t)LINK_NACK_BAD_RANGE);
    CHECK_EQ(prog_rail_write(&g_rail, &g_pw, 0u, 1u, NULL, &g_rctx, 0u),
             (uint8_t)LINK_NACK_BAD_RANGE);
}

/* An OFF during a write from @p t0: S1 off at once, port k held until the
 * write ends at @p t0 + 700ms. */
static void off_held_to_the_writes_end(bool switches, uint32_t t0)
{
    rig_start(switches, K, true);
    if (switches) {
        two_ports_on();
    }
    hold(false, t0);
    CHECK(rail_on( RAIL_MV, 1000u));
    hold(true, t0);
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, 0u, t0), 0u);
    CHECK_EQ(pw_write(LINK_PW_RAIL, 0u, t0), 0u);
    CHECK_EQ(pw_reg(LINK_PW_RAIL), 0u);
    CHECK_EQ(pw_reg(LINK_PW_PORT_WANT), 0u);
    CHECK_EQ(ports_on(), switches ? K_BIT : 0x01FFu);
    CHECK(power_rail_driven(&g_pw));
    for (uint32_t t = t0; t != t0 + 700u; ++t) {
        hold(true, t);
        CHECK((ports_on() & K_BIT) != 0u);
    }
    hold(false, t0 + 700u);             /* the write and its tail ended */
    hold(true, t0 + 700u);              /* the session still holds */
    CHECK_EQ(ports_on(), 0u);
    CHECK(!power_rail_driven(&g_pw));
    /* The panel's OFF stands for the session until it lets go. */
    CHECK(!rail_set(true, RAIL_MV, 1000u));
    CHECK_EQ(prog_rail_judge(&g_rail, &g_pw, &g_rctx), PROG_RAIL_OFF_STANDS);
    g_rnow = t0 + 800u;
    rail_tell(false, false);
    CHECK(rail_on( RAIL_MV, 1000u));
}

TEST_CASE(the_panels_off_waits_for_a_write_with_the_switches)
{
    off_held_to_the_writes_end(true, 1000u);
}

TEST_CASE(the_panels_off_waits_for_a_write_without_the_switches)
{
    off_held_to_the_writes_end(false, 1000u);
}

TEST_CASE(a_held_off_waits_across_the_clock_wrap)
{
    off_held_to_the_writes_end(true, 0xFFFFFF00u);
}

/* A write that does not end: port k on to 9999ms after the OFF, off at
 * 10000ms. */
static void held_at_most_10000ms(uint32_t t0)
{
    rig_start(true, K, true);
    hold(false, t0);
    CHECK(rail_on( RAIL_MV, 1000u));
    hold(true, t0);
    CHECK_EQ(pw_write(LINK_PW_RAIL, 0u, t0), 0u);
    hold(true, t0 + SUPPLY_PAGE_WRITE_WAIT_MS - 1u);
    CHECK_EQ(ports_on(), K_BIT);
    hold(true, t0 + SUPPLY_PAGE_WRITE_WAIT_MS);
    CHECK_EQ(ports_on(), 0u);
    CHECK(!power_rail_driven(&g_pw));
    hold(true, t0 + SUPPLY_PAGE_WRITE_WAIT_MS + 1u);
    CHECK_EQ(ports_on(), 0u);
}

TEST_CASE(a_held_off_ends_at_10000ms)
{
    held_at_most_10000ms(5u);
    held_at_most_10000ms(0xFFFFFFFFu - 5000u);
}

TEST_CASE(an_off_without_a_write_is_at_once_and_stands)
{
    rig_start(true, K, true);
    hold(false, 0u);
    CHECK(rail_on( RAIL_MV, 1000u));
    hold(false, 0u);
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, 0u, 0u), 0u);
    CHECK_EQ(ports_on(), 0u);
    CHECK(g_rail.sticky);
    CHECK(!rail_set(true, RAIL_MV, 1000u));
    /* A PORT_WANT without bit k in the session's own off time is no OFF of
     * the session. */
    rig_start(true, K, true);
    two_ports_on();
    hold(false, 0u);
    CHECK(rail_on( RAIL_MV, 1000u));
    CHECK(rail_set(false, 0u, 0u));
    hold(false, 0u);
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, 0x0001u, 0u), 0u);
    CHECK(!g_rail.sticky);
    CHECK(rail_on( RAIL_MV, 1000u));
    CHECK_EQ(ports_on(), 0x0001u | K_BIT);
}

TEST_CASE(a_lost_heartbeat_is_not_held)
{
    rig_start(true, K, true);
    two_ports_on();
    hold(false, 0u);
    CHECK(rail_on( RAIL_MV, 1000u));
    hold(true, 0u);
    CHECK_EQ(pw_write(LINK_PW_RAIL, 0u, 0u), 0u);
    CHECK_EQ(ports_on(), K_BIT);
    /* iomcu_rsn.c's rsn_off(): power_off() and prog_rail_off(). */
    power_off(&g_pw);
    prog_rail_off(&g_rail);
    CHECK_EQ(ports_on(), 0u);
    CHECK(!power_rail_driven(&g_pw));
    hold(true, 1u);
    CHECK_EQ(ports_on(), 0u);
    CHECK(!rail_set(true, RAIL_MV, 1000u));
    /* A trip the same. */
    rig_start(false, K, true);
    hold(false, 0u);
    CHECK(rail_on( RAIL_MV, 1000u));
    hold(true, 0u);
    CHECK_EQ(pw_write(LINK_PW_RAIL, 0u, 0u), 0u);
    CHECK(power_rail_driven(&g_pw));
    power_trip(&g_pw, LINK_PW_TRIP_CRIT_N);
    CHECK(!power_rail_driven(&g_pw));
}

TEST_CASE(null_arguments_do_nothing)
{
    supply_port_view_t v;

    rig_start(true, K, true);
    prog_rail_init(NULL);
    prog_rail_port(NULL, 1);
    prog_rail_tell(NULL, &g_pw, &g_sp, true, true, &g_rctx, 0u);
    prog_rail_tell(&g_rail, NULL, &g_sp, true, true, &g_rctx, 0u);
    prog_rail_off(NULL);
    prog_rail_view(NULL, &g_pw, &v, &v);
    CHECK(!prog_rail_set(NULL, &g_pw, &g_sp, false, 0u, 0u, &g_rctx, 0u));
    CHECK(!prog_rail_set(&g_rail, &g_pw, NULL, false, 0u, 0u, &g_rctx, 0u));
    power_port_set(NULL, 0u, true);
    power_port_set(&g_pw, LINK_PW_PORTS, true);
    power_drop(NULL, 0u);
    power_keep(NULL, 0u);
    CHECK(!power_rail_driven(NULL));
    CHECK(!power_rail_driven(&g_pw));
}

TEST_CASE(a_trip_latches_until_the_panels_rail_1)
{
    for (unsigned sw = 0; sw < 2u; ++sw) {
        rig_start(sw != 0u, K, true);
        power_trip(&g_pw, LINK_PW_TRIP_CRIT_N);
        CHECK_EQ(prog_rail_judge(&g_rail, &g_pw, &g_rctx), PROG_RAIL_TRIPPED);
        CHECK(!rail_set(true, RAIL_MV, 1000u));
        CHECK_EQ(ports_on(), 0u);
        CHECK(!power_rail_driven(&g_pw));
        /* The panel's RAIL 1 clears TRIPPED, as POWER's rule has it. */
        CHECK_EQ(pw_write(LINK_PW_RAIL, 1u, 0u), 0u);
        CHECK_EQ(pw_reg(LINK_PW_TRIPPED), 0u);
        CHECK(rail_on(RAIL_MV, 1000u));
        CHECK((ports_on() & K_BIT) != 0u);
    }
}

TEST_CASE(a_trip_while_port_k_waits_ends_the_wait)
{
    rig_start(true, K, false);
    hold(false, 0u);
    CHECK(rail_set(true, RAIL_MV, 1000u));
    CHECK(g_rail.pending);
    rail_pass();
    power_trip(&g_pw, LINK_PW_TRIP_THERMAL);
    prog_rail_off(&g_rail);             /* rsn_pass() on a trip */
    for (unsigned i = 0; i < 3000u; ++i) {
        rail_pass();
        CHECK_EQ(ports_on(), 0u);
    }
    CHECK(!g_rail.pending);
    /* A lost heartbeat the same. */
    rig_start(false, K, false);
    hold(false, 0u);
    CHECK(rail_set(true, RAIL_MV, 1000u));
    g_rctx.power.beat_alive = false;
    for (unsigned i = 0; i < 300u; ++i) {
        rail_pass();
        CHECK(!power_rail_driven(&g_pw));
    }
    CHECK(!g_rail.pending);
}

TEST_CASE(one_session_at_a_time_refuses_every_other_connect)
{
    const int k_of[4] = { K, 6, PROG_RAIL_NO_PORT, 0 };

    rig_start(true, K, true);
    CHECK_EQ(prog_rail_connect_mask(&g_pw, k_of, 4u, 0u, &g_rctx), 0u);
    CHECK_EQ(prog_rail_connect_mask(&g_pw, k_of, 4u, 0x02u, &g_rctx), 0x0Fu);
    CHECK_EQ(prog_rail_connect_mask(&g_pw, k_of, 4u, 0x01u, &g_rctx), 0x0Fu);
    CHECK_EQ(prog_rail_connect_mask(&g_pw, k_of, 4u, 0x10u, &g_rctx), 0u);
    g_pw.bec = 0x0001u;
    CHECK_EQ(prog_rail_connect_mask(&g_pw, k_of, 4u, 0u, &g_rctx), 0x0Bu);
    g_pw.bec = 0u;
    rig_start(false, K, true);
    g_rctx.active = 0x0040u;            /* port 1's own output, S7 */
    CHECK_EQ(prog_rail_connect_mask(&g_pw, k_of, 4u, 0u, &g_rctx), 0x09u);
    CHECK_EQ(prog_rail_connect_mask(NULL, k_of, 4u, 0u, &g_rctx), 0x0Fu);
    CHECK_EQ(prog_rail_connect_mask(&g_pw, k_of, 9u, 0x80u, &g_rctx), 0xFFu);
}

/* The PD mini by hand at @p from_mv, the panel's ports S1 and S2 on with
 * the switches; the session asks 5000mV with port k off. */
static void pd_by_hand_at(bool switches, uint16_t from_mv, uint32_t t0)
{
    const uint16_t on[3] = { 1u, from_mv, RAIL_MA };

    rig_start(switches, K, false);
    CHECK_EQ(supply_page_write(&g_sp, LINK_SP_OUTPUT, 3u, on, &g_sout, true),
             0u);
    sp_pass(true);
    sp_pass(true);
    if (switches) {
        two_ports_on();
    }
    g_pd_by_hand = true;
    g_pd.st.v_mv = from_mv;
    hold(false, t0);
    CHECK(rail_set(true, 5000u, 1000u));
    CHECK_EQ(ports_on(), 0u);           /* the others off, port k waits */
    CHECK(!power_rail_driven(&g_pw) || switches);
}

/* Passes at @p v_mv until port k is on, at most @p max: the passes. */
static unsigned passes_at(uint16_t v_mv, unsigned max)
{
    unsigned n = 0u;

    g_pd.st.v_mv = v_mv;
    while (n < max && (ports_on() & K_BIT) == 0u) {
        rail_pass();
        ++n;
    }
    return n;
}

TEST_CASE(port_k_waits_for_a_pd_mini_above_the_voltage_to_come_down)
{
    for (unsigned sw = 0; sw < 2u; ++sw) {
        supply_port_view_t v;

        pd_by_hand_at(sw != 0u, 8400u, 0u);
        rail_view(&v);
        CHECK(v.asked_on);
        CHECK(!v.on);
        CHECK(!v.off);
        CHECK_EQ(passes_at(8400u, 1000u), 1000u);
        CHECK_EQ(g_pd.want_mv, 5000u);
        CHECK_EQ(passes_at(7000u, 200u), 200u);
        CHECK_EQ(passes_at(5000u + PROG_RAIL_OVER_MV + 1u, 200u), 200u);
        CHECK(!power_rail_driven(&g_pw) || sw != 0u);
        /* In the band: PROG_RAIL_SETTLE_MS in a row, a dip starts anew. */
        CHECK_EQ(passes_at(5000u + PROG_RAIL_OVER_MV, 50u), 50u);
        CHECK_EQ(passes_at(5300u, 1u), 1u);
        CHECK_EQ(passes_at(5000u + PROG_RAIL_OVER_MV, 1000u),
                 PROG_RAIL_SETTLE_MS + 1u);
        CHECK_EQ(ports_on(), sw != 0u ? K_BIT : 0x01FFu);
        rail_view(&v);
        CHECK(v.on);
    }
}

TEST_CASE(port_k_waits_for_a_pd_mini_below_the_voltage_to_come_up)
{
    pd_by_hand_at(true, 4800u, 0u);
    CHECK_EQ(passes_at(5000u - PROG_RAIL_UNDER_MV - 1u, 500u), 500u);
    CHECK_EQ(passes_at(5000u - PROG_RAIL_UNDER_MV, 1000u),
             PROG_RAIL_SETTLE_MS + 1u);
    CHECK_EQ(ports_on(), K_BIT);
}

TEST_CASE(a_pd_mini_at_the_voltage_switches_port_k_at_once)
{
    rig_start(true, K, true);
    two_ports_on();
    g_pd_by_hand = true;
    g_pd.st.v_mv = RAIL_MV;
    sp_pass(true);
    hold(false, 0u);
    CHECK(rail_set(true, RAIL_MV, 1000u));
    CHECK_EQ(ports_on(), 0x0003u | K_BIT);
    /* At the set point but reading high: not at once. */
    CHECK(rail_set(false, 0u, 0u));
    g_pd.st.v_mv = RAIL_MV + PROG_RAIL_OVER_MV + 1u;
    sp_pass(true);
    CHECK(rail_set(true, RAIL_MV, 1000u));
    CHECK_EQ(ports_on(), 0x0003u);
    CHECK_EQ(passes_at(RAIL_MV, 10u), 1u);   /* not moved: no settle */
}

/* A PD mini that never comes down: refused at PROG_RAIL_WAIT_MS to the
 * millisecond, the PD mini off, port k off, the ON standing refused. */
static void never_there(uint32_t t0)
{
    supply_port_view_t v;

    pd_by_hand_at(true, 8400u, t0);
    CHECK_EQ(passes_at(8400u, PROG_RAIL_WAIT_MS - 1u),
             PROG_RAIL_WAIT_MS - 1u);
    CHECK(g_rail.pending);
    rail_pass();
    CHECK(!g_rail.pending);
    CHECK_EQ(ports_on(), 0u);
    sp_pass(true);
    CHECK(!pd_wants_on());
    rail_view(&v);
    CHECK(!v.asked_on);
    CHECK(!rail_set(true, 5000u, 1000u));
    CHECK_EQ(prog_rail_judge(&g_rail, &g_pw, &g_rctx), PROG_RAIL_OFF_STANDS);
    /* The session lets go: a new one may try again. */
    rail_tell(false, false);
    CHECK_EQ(prog_rail_judge(&g_rail, &g_pw, &g_rctx), PROG_RAIL_OK);
}

TEST_CASE(a_pd_mini_that_never_reaches_the_voltage_is_refused)
{
    never_there(0u);
    never_there(0xFFFFFFFFu - 1000u);
}

TEST_CASE(an_off_ends_a_waiting_on)
{
    pd_by_hand_at(true, 8400u, 0u);
    CHECK(rail_set(false, 0u, 0u));
    CHECK(!g_rail.pending);
    CHECK_EQ(passes_at(5000u, 500u), 500u);
    CHECK_EQ(ports_on(), 0u);
}

/* The PD mini at 5000mV and @p pd_ma; the session's ON at 5000mV and
 * @p ma: the limit it ends with. */
static uint16_t limit_after(bool switches, bool k_on, bool others,
                            uint16_t pd_ma, uint16_t ma)
{
    const uint16_t on[3] = { 1u, RAIL_MV, pd_ma };

    rig_start(switches, K, false);
    CHECK_EQ(supply_page_write(&g_sp, LINK_SP_OUTPUT, 3u, on, &g_sout, true),
             0u);
    sp_pass(true);
    sp_pass(true);
    if (others) {
        two_ports_on();
    }
    hold(false, 0u);
    if (k_on) {
        CHECK(rail_on(RAIL_MV, pd_ma));
        CHECK((ports_on() & K_BIT) != 0u);
    }
    CHECK(rail_on(RAIL_MV, ma));
    CHECK((ports_on() & K_BIT) != 0u);
    if (others) {
        CHECK_EQ(ports_on() & 0x0003u, 0x0003u);   /* they stay on */
    }
    sp_pass(true);
    return g_pd.want_ma;
}

TEST_CASE(the_sessions_current_limit_is_lowered_never_raised_under_a_servo)
{
    /* Alone and powered at the same voltage: the lower limit is applied. */
    CHECK_EQ(limit_after(true, true, false, 2000u, 1000u), 1000u);
    CHECK_EQ(limit_after(false, true, false, 2000u, 1000u), 1000u);
    /* A higher one is not applied under the powered servo. */
    CHECK_EQ(limit_after(true, true, false, 1000u, 2000u), 1000u);
    CHECK_EQ(limit_after(false, true, false, 1000u, 2000u), 1000u);
    /* Shared with the switches: lowered for all, the others stay on. */
    CHECK_EQ(limit_after(true, false, true, 2000u, 800u), 800u);
    CHECK_EQ(limit_after(true, false, true, 800u, 2000u), 800u);
    /* Without the switches the rail is the one switch: as alone. */
    CHECK_EQ(limit_after(false, false, false, 2000u, 800u), 800u);
    /* Nothing powered: taken as asked, a raise included. */
    CHECK_EQ(limit_after(true, false, false, 800u, 2000u), 2000u);
}

int main(void)
{
    RUN(the_view_is_the_pd_mini_through_the_ports_switch);
    RUN(an_off_cycles_the_rail_without_the_switches_and_port_k_with_them);
    RUN(an_on_is_refused_in_the_order_of_the_rules);
    RUN(connect_is_refused_with_another_channel_active_without_switches);
    RUN(connect_is_refused_with_a_bec_found_at_500mv_not_490mv);
    RUN(another_voltage_switches_the_other_ports_off_with_the_switches);
    RUN(the_same_voltage_leaves_the_other_ports_on);
    RUN(the_pd_mini_moves_only_while_port_k_is_off);
    RUN(an_on_from_rail_0_leaves_the_other_ports_off);
    RUN(the_panel_cannot_switch_the_sessions_port_on);
    RUN(the_panels_off_waits_for_a_write_with_the_switches);
    RUN(the_panels_off_waits_for_a_write_without_the_switches);
    RUN(a_held_off_waits_across_the_clock_wrap);
    RUN(a_held_off_ends_at_10000ms);
    RUN(an_off_without_a_write_is_at_once_and_stands);
    RUN(a_lost_heartbeat_is_not_held);
    RUN(null_arguments_do_nothing);
    RUN(a_trip_latches_until_the_panels_rail_1);
    RUN(a_trip_while_port_k_waits_ends_the_wait);
    RUN(one_session_at_a_time_refuses_every_other_connect);
    RUN(port_k_waits_for_a_pd_mini_above_the_voltage_to_come_down);
    RUN(port_k_waits_for_a_pd_mini_below_the_voltage_to_come_up);
    RUN(a_pd_mini_at_the_voltage_switches_port_k_at_once);
    RUN(a_pd_mini_that_never_reaches_the_voltage_is_refused);
    RUN(an_off_ends_a_waiting_on);
    RUN(the_sessions_current_limit_is_lowered_never_raised_under_a_servo);
    return test_summary("prog_rail");
}
