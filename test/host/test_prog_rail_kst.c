/*
 * A KST session on the remote servo node's common rail: the port of
 * shared/outputs/kst_port.c against the simulated servo of kst_sim.h,
 * whose supply is the PD mini through the session's port switch
 * (prog_rail.h), with the port switches (FITTED 0x1FF) and without them
 * (FITTED 0).  The servo is powered while the session's view reads on; it
 * enters its programming mode only on the first sync after a power-up.
 *
 * Wired as firmware/iomcu/src/iomcu_rsn.c and main.c wire it each pass:
 * the port's step, the CONNECT rules into the port's other-mask
 * (ports_exclusive()), the rail and the SUPPLY page told of the hold, the
 * frame and the write (ports_tell_supply()), the SUPPLY page's step.  A
 * pass is 100us on the wire's clock, every tenth 1ms on the port's.
 *
 * Under test: CONNECT and a REBOOT's cycle acting on RAIL_EN without the
 * switches (every port off for KST_PORT_RAIL_OFF_MS) and on PORT_EN k only
 * with them (the other ports on throughout); CONNECT refused with another
 * channel active without the switches, nothing switched, and taken with
 * the switches; CONNECT refused with a BEC found; a session at another
 * voltage switching the other ports off until it lets go; the panel's
 * error during a write to the servo's memory: every other port off at
 * once, the session's port and the PD mini on until the write's tail
 * ends, then off, the servo holding the whole target, the session back in
 * PWM with nothing switched on again; a lost heartbeat during a write:
 * everything off in that pass, the session aborted to PWM with the servo
 * in no mode; ENTER and REBOOT in one batch of frames with a PROG write
 * that makes another channel active, in both orders, never cutting a
 * live channel (the CONNECT rules judged at each command, as kst_write()
 * calls ports_exclusive() before the port's write).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "kst_plan.h"
#include "kst_port.h"
#include "kst_reg.h"
#include "kst_sim.h"
#include "prog_rail_rig.h"

#define CH    0u        /* the programming port */
#define PIN   20u
#define K     4         /* its output: S5 */
#define K_BIT 0x0010u
#define KEY   LINK_KST_WRITE_KEY

static sim_t      g_sim;
static kst_port_t g_port;
static uint32_t   g_ms;
static unsigned   g_sub;
static unsigned   g_k_ons;     /* rising edges of port k */
static bool       g_k_was;

/* ------------------------------------------------------------- the hardware */

static uint8_t hw_pin(void *ctx, uint8_t port)
{
    (void)ctx;
    return port == CH ? PIN : 0xFFu;
}

static bool hw_take(void *ctx, uint8_t port, uint8_t pin)
{
    (void)ctx;
    (void)port;
    (void)pin;
    return true;
}

static void hw_give(void *ctx)
{
    (void)ctx;
}

static void hw_supply(void *ctx, supply_port_view_t *out)
{
    (void)ctx;
    rail_view(out);
}

static bool hw_supply_set(void *ctx, bool on, uint16_t mv, uint16_t ma)
{
    (void)ctx;
    return rail_set(on, mv, ma);
}

/* ----------------------------------------------------------------- the page */

static uint16_t reg(unsigned i)
{
    uint16_t v = 0xDEADu;

    kst_port_read(&g_port, (uint8_t)i, 1, &v);
    return v;
}

static link_kst_state_t state(void)
{
    return (link_kst_state_t)LINK_KS_STATE_OF(reg(LINK_KS_STATE));
}

static bool busy(void)
{
    return (reg(LINK_KS_STATE) & LINK_KS_F_BUSY) != 0u;
}

static unsigned result(void)
{
    return reg(LINK_KS_RESULT) & 0xFFu;
}

static unsigned refusal(void)
{
    return reg(LINK_KS_RESULT) >> 8;
}

/* The rail told of the port's hold and write: ports_tell_supply(). */
static void tell(void)
{
    const bool held = kst_port_state(&g_port) != LINK_KST_PWM;

    g_rnow = g_ms;
    rail_tell(held, kst_port_writing(&g_port));
    supply_page_hold(&g_sp, held);
    supply_page_port_writes(&g_sp, kst_port_writing(&g_port));
}

/* Bit q: another page's session holds programming port q. */
static uint8_t g_other_held;

/* The CONNECT rules into the port's other-mask: ports_exclusive(). */
static void exclusive(void)
{
    static const int k_of[2] = { K, 6 };

    kst_port_other_mask(&g_port,
                        prog_rail_connect_mask(&g_pw, k_of, 2u, g_other_held,
                                               &g_rctx));
}

static uint8_t cmd(link_kst_op_t o, uint16_t key, uint16_t crc)
{
    const uint16_t in[4] = { LINK_KS_W_CMD_OF(o, CH),
                             (uint16_t)(reg(LINK_KS_SEQ) + 1u), key, crc };

    exclusive();                /* kst_write(): the rules at the command */
    const uint8_t nack = kst_port_write(&g_port, 0, 4, in, g_ms);

    tell();
    exclusive();
    return nack;
}

/* The CRC of the image the page shows: a WRITE's start. */
static uint16_t shown_crc(void)
{
    uint16_t r[LINK_KS_IMAGE_REGS];

    kst_port_read(&g_port, LINK_KS_IMAGE, LINK_KS_IMAGE_REGS, r);
    return link_kst_crc(r, LINK_KS_IMAGE_REGS);
}

/* Stage @p img over the image shown, nothing unlocked; their CRC. */
static uint16_t stage(const kst_image_t *img)
{
    uint16_t s[LINK_KS_W_STAGED];

    memset(s, 0, sizeof(s));
    for (unsigned k = 0; k < LINK_KS_IMAGE_REGS; ++k) {
        s[k] = (uint16_t)(img->r[2u * k] | (img->r[2u * k + 1u] << 8));
    }
    s[LINK_KS_W_START_CRC - LINK_KS_W_IMAGE] = shown_crc();
    for (unsigned at = 0; at < LINK_KS_W_STAGED; at += 4u) {
        const unsigned n = at + 4u <= LINK_KS_W_STAGED
                               ? 4u : LINK_KS_W_STAGED - at;

        CHECK_EQ(kst_port_write(&g_port, (uint8_t)(LINK_KS_W_IMAGE + at),
                                (uint8_t)n, &s[at], g_ms), 0);
    }
    return link_kst_crc(s, LINK_KS_W_STAGED);
}

static kst_image_t page_image(void)
{
    uint16_t r[LINK_KS_IMAGE_REGS];
    kst_image_t img;

    kst_port_read(&g_port, LINK_KS_IMAGE, LINK_KS_IMAGE_REGS, r);
    for (unsigned k = 0; k < LINK_KS_IMAGE_REGS; ++k) {
        img.r[2u * k] = (uint8_t)(r[k] & 0xFFu);
        img.r[2u * k + 1u] = (uint8_t)(r[k] >> 8);
    }
    return img;
}

/* ---------------------------------------------------------------- the clock */

static void tick_as(bool safe)
{
    supply_port_view_t v;

    g_rnow = g_ms;
    kst_port_step(&g_port, safe, false, g_ms);
    exclusive();
    tell();
    supply_page_port_frame(&g_sp, kst_port_frame_out(&g_port), g_ms);
    sp_pass(safe);
    rail_view(&v);
    sim_power(&g_sim, v.on);
    const bool k_on = (ports_on() & K_BIT) != 0u;
    g_k_ons += (k_on && !g_k_was) ? 1u : 0u;
    g_k_was = k_on;
    g_sim.now_us += 100u;
    if (++g_sub == 10u) {
        g_sub = 0;
        g_ms++;
    }
}

static void tick(void)
{
    tick_as(true);
}

static void run_ms(unsigned ms)
{
    for (unsigned i = 0; i < 10u * ms; ++i) {
        tick();
    }
}

static void settle(void)
{
    for (unsigned i = 0; i < 200000u && busy(); ++i) {
        tick();
    }
    CHECK(!busy());
}

static void until_state(link_kst_state_t s)
{
    for (unsigned i = 0; i < 200000u && state() != s; ++i) {
        tick();
    }
    CHECK_EQ(state(), s);
}

/* ---------------------------------------------------------------- the bench */

/* The node with or without the switches, the PD mini on at RAIL_MV, the
 * rail off, the servo without power; the session's voltage @p mv. */
static void bench(bool switches, uint16_t mv)
{
    const uint16_t sup[2] = { mv, 1000u };
    kst_port_hw_t hw;

    rig_start(switches, K, true);
    g_other_held = 0u;
    sim_init(&g_sim);
    g_sim.needs_power_up = true;
    sim_power(&g_sim, false);
    memset(&hw, 0, sizeof(hw));
    hw.ports = 1u;
    hw.pin = hw_pin;
    hw.take = hw_take;
    hw.give = hw_give;
    hw.supply = hw_supply;
    hw.supply_set = hw_supply_set;
    hw.driver = sim_driver(&g_sim);
    CHECK(kst_port_init(&g_port, &hw));
    g_ms = 0u;
    g_sub = 0u;
    g_k_ons = 0u;
    g_k_was = false;
    tick();
    CHECK_EQ(kst_port_write(&g_port, LINK_KS_W_SUPPLY_MV, 2, sup, g_ms), 0);
}

/* CONNECT: ENTER taken and the servo entered and read. */
static void connect(void)
{
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, 0, 0), 0);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
}

/* With the switches: the panel's ports S1 and S2 on at RAIL_MV. */
static void two_ports_on(void)
{
    CHECK_EQ(pw_write(LINK_PW_RAIL, 1u, g_ms), 0u);
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, 0x0003u, g_ms), 0u);
}

/* A REBOOT: the switch off for KST_PORT_RAIL_OFF_MS at least, every pass of
 * it checked with @p check, then on again. */
static void reboot_cycles(bool switches)
{
    const unsigned ons = g_k_ons;
    uint32_t off_ms = 0u;

    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    until_state(LINK_KST_RAIL_OFF);
    tick();
    const uint32_t t0 = g_ms;
    while (g_k_ons == ons) {
        if (switches) {
            CHECK(power_rail_driven(&g_pw));
            CHECK_EQ(ports_on(), 0x0003u);  /* S1 and S2 stay on */
        } else {
            CHECK(!power_rail_driven(&g_pw));
            CHECK_EQ(ports_on(), 0u);       /* every servo cycles */
        }
        CHECK(!g_sim.powered);
        off_ms = g_ms - t0;
        tick();
    }
    CHECK(off_ms >= KST_PORT_RAIL_OFF_MS);
    CHECK(off_ms < KST_PORT_RAIL_OFF_MS + 50u);
    CHECK_EQ(ports_on(), switches ? (0x0003u | K_BIT) : 0x01FFu);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(g_sim.power_ups, 2u);
}

TEST_CASE(without_the_switches_the_cycles_act_on_rail_en)
{
    bench(false, RAIL_MV);
    CHECK(!power_rail_driven(&g_pw));
    connect();
    CHECK(power_rail_driven(&g_pw));
    CHECK_EQ(ports_on(), 0x01FFu);
    CHECK_EQ(g_sim.power_ups, 1u);
    reboot_cycles(false);
}

TEST_CASE(with_the_switches_the_cycles_act_on_port_en_k)
{
    bench(true, RAIL_MV);
    two_ports_on();
    connect();
    CHECK_EQ(ports_on(), 0x0003u | K_BIT);
    CHECK_EQ(g_sim.power_ups, 1u);
    reboot_cycles(true);
}

TEST_CASE(connect_is_refused_with_another_channel_active_without_switches)
{
    bench(false, RAIL_MV);
    g_rctx.active = 0x0001u;            /* S1's channel is not OFF */
    exclusive();
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    run_ms(KST_PORT_RAIL_OFF_MS);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK(!power_rail_driven(&g_pw));
    CHECK_EQ(g_sim.power_ups, 0u);
    /* S1 OFF: taken. */
    g_rctx.active = K_BIT;
    exclusive();
    connect();
    /* With the switches the other channels may stay active. */
    bench(true, RAIL_MV);
    two_ports_on();
    g_rctx.active = 0x0003u;
    exclusive();
    connect();
    CHECK_EQ(ports_on(), 0x0003u | K_BIT);
}

TEST_CASE(connect_is_refused_with_a_bec_found)
{
    for (unsigned sw = 0; sw < 2u; ++sw) {
        bench(sw != 0u, RAIL_MV);
        rig_window(1u, 0);
        g_pw.bec = 0u;
        int32_t v[LINK_PW_PORTS] = { 0 };
        v[2] = 500;                     /* an ESC's BEC on S3 */
        (void)power_window(&g_pw, 0u, 2u, v, LINK_PW_PORT_ALL, RAIL_MV);
        CHECK_EQ(pw_reg(LINK_PW_BEC), 0x0004u);
        exclusive();
        CHECK_EQ(cmd(LINK_KST_OP_ENTER, 0, 0), 0);
        CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
        run_ms(200u);
        CHECK_EQ(ports_on(), 0u);
        CHECK_EQ(g_sim.power_ups, 0u);
        v[2] = 0;                       /* its + link opened */
        (void)power_window(&g_pw, 0u, 3u, v, LINK_PW_PORT_ALL, RAIL_MV);
        exclusive();
        connect();
    }
}

TEST_CASE(another_voltage_switches_the_other_ports_off_until_the_end)
{
    bench(true, 6000u);
    two_ports_on();
    connect();
    CHECK_EQ(ports_on(), K_BIT);
    CHECK_EQ(g_pd.want_mv, 6000u);
    CHECK_EQ(g_rail.dropped, 0x0003u);
    /* No new port for the session's duration. */
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, K_BIT | 0x0001u, g_ms),
             (uint8_t)LINK_NACK_BUSY);
    /* The session ends: LEAVE without a cycle lets go of the port. */
    CHECK_EQ(cmd(LINK_KST_OP_LEAVE, 0, 0), 0);
    settle();
    until_state(LINK_KST_PWM);
    tick();
    CHECK(!g_rail.alone);
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, K_BIT | 0x0003u, g_ms), 0u);
    CHECK_EQ(ports_on(), K_BIT | 0x0003u);
}

/* The panel's error as error_act() writes it to the node: SUPPLY OFF, then
 * POWER's RAIL 0 and PORT_WANT 0. */
static void panel_error(void)
{
    sp_panel_off();
    CHECK_EQ(pw_write(LINK_PW_RAIL, 0u, g_ms), 0u);
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, 0u, g_ms), 0u);
}

/* A WRITE of a new dead band in flight: its first write frame gone. */
static kst_image_t write_in_flight(void)
{
    kst_image_t target = page_image();
    const unsigned writes = g_sim.n_writes;

    CHECK(kst_field_edit(&target, KST_F_DEAD_BAND, 50));
    CHECK_EQ(cmd(LINK_KST_OP_WRITE, KEY, stage(&target)), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    for (unsigned i = 0; i < 200000u && g_sim.n_writes == writes; ++i) {
        tick();
    }
    CHECK(kst_port_writing(&g_port));
    return target;
}

static void error_during_a_write(bool switches)
{
    bench(switches, RAIL_MV);
    if (switches) {
        two_ports_on();
    }
    connect();
    const kst_image_t target = write_in_flight();
    panel_error();
    CHECK_EQ(pw_reg(LINK_PW_RAIL), 0u);
    CHECK_EQ(ports_on() & 0x0003u, switches ? 0u : 0x0003u);
    /* The session's port and the PD mini on until the tail's end. */
    while (kst_port_writing(&g_port)) {
        CHECK((ports_on() & K_BIT) != 0u);
        CHECK(pd_wants_on());
        CHECK(g_sim.powered);
        tick();
    }
    for (unsigned i = 0; i < 10u * (SUPPLY_PAGE_OFF_WAIT_MS + 1u)
                         && pd_wants_on();
         ++i) {
        tick();
    }
    CHECK_EQ(ports_on(), 0u);
    CHECK(!power_rail_driven(&g_pw));
    CHECK(!pd_wants_on());
    CHECK(!g_sim.powered);
    /* The servo holds the whole target. */
    CHECK(memcmp(g_sim.reg, target.r, sizeof(target.r)) == 0);
    /* The session ends in PWM and switches nothing on again. */
    const unsigned ons = g_k_ons;
    until_state(LINK_KST_PWM);
    run_ms(KST_PORT_RAIL_OFF_MS + KST_PORT_RAIL_WAIT_MS);
    CHECK_EQ(g_k_ons, ons);
    CHECK_EQ(ports_on(), 0u);
    CHECK(!pd_wants_on());
}

TEST_CASE(an_error_during_a_write_waits_for_it_with_the_switches)
{
    error_during_a_write(true);
}

TEST_CASE(an_error_during_a_write_waits_for_it_without_the_switches)
{
    error_during_a_write(false);
}

TEST_CASE(a_lost_heartbeat_during_a_write_cuts_everything_at_once)
{
    bench(true, RAIL_MV);
    two_ports_on();
    connect();
    (void)write_in_flight();
    /* main.c: the gate's off edge, rsn_off(): power_off(), prog_rail_off(). */
    power_off(&g_pw);
    prog_rail_off(&g_rail);
    tick_as(false);
    CHECK_EQ(ports_on(), 0u);
    CHECK(!power_rail_driven(&g_pw));
    CHECK(!g_sim.powered);
    for (unsigned i = 0; i < 10u * (SUPPLY_PAGE_OFF_WAIT_MS + 1u)
                         && pd_wants_on();
         ++i) {
        tick_as(false);
    }
    CHECK(!pd_wants_on());
    until_state(LINK_KST_PWM);
    CHECK(result() != KST_SES_OK);
    /* The servo was cut mid-write: without power and in no mode; the
     * next CONNECT reads what it holds before anything else. */
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK_EQ(reg(LINK_KS_STATE) & LINK_KS_F_IN_MODE, 0u);
    run_ms(KST_PORT_RAIL_OFF_MS);
    CHECK_EQ(ports_on(), 0u);
}

/* A trip (rsn_pass(): power_trip(), prog_rail_off()) @p at_ms into a
 * REBOOT's off time, or into its RAIL_WAIT when @p in_wait: nothing is
 * switched on again, the session ends with its error. */
static void trip_in_reboot(bool switches, uint32_t at_ms, bool in_wait)
{
    bench(switches, RAIL_MV);
    if (switches) {
        two_ports_on();
    }
    connect();
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, 0, 0), 0);
    until_state(in_wait ? LINK_KST_RAIL_WAIT : LINK_KST_RAIL_OFF);
    tick();
    const uint32_t t0 = g_ms;
    while (g_ms - t0 < at_ms) {
        tick();
    }
    power_trip(&g_pw, LINK_PW_TRIP_CRIT_N);
    prog_rail_off(&g_rail);
    const unsigned ons = g_k_ons;
    CHECK_EQ(ports_on(), 0u);
    until_state(LINK_KST_PWM);
    run_ms(KST_PORT_SUPPLY_MS);
    CHECK_EQ(g_k_ons, ons);
    CHECK_EQ(ports_on(), 0u);
    CHECK(!power_rail_driven(&g_pw));
    CHECK(!g_sim.powered);
    CHECK(result() != KST_SES_OK);
    /* Until the panel's RAIL 1 no session switches it on. */
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, 0, 0), 0);
    run_ms(KST_PORT_SUPPLY_MS + 100u);
    CHECK_EQ(g_k_ons, ons);
}

TEST_CASE(a_trip_in_a_reboot_keeps_the_rail_off)
{
    static const uint32_t k_at[4] = { 0u, 500u, KST_PORT_RAIL_OFF_MS - 2u,
                                      KST_PORT_RAIL_OFF_MS - 1u };

    for (unsigned sw = 0; sw < 2u; ++sw) {
        for (unsigned i = 0; i < 4u; ++i) {
            trip_in_reboot(sw != 0u, k_at[i], false);
        }
        trip_in_reboot(sw != 0u, 0u, true);
        trip_in_reboot(sw != 0u, KST_PORT_RAIL_WAIT_MS - 2u, true);
    }
}

TEST_CASE(connect_is_refused_while_another_session_holds_a_port)
{
    for (unsigned sw = 0; sw < 2u; ++sw) {
        bench(sw != 0u, RAIL_MV);
        g_other_held = 0x02u;           /* HITEC holds programming port 1 */
        exclusive();
        CHECK_EQ(cmd(LINK_KST_OP_ENTER, 0, 0), 0);
        CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
        run_ms(200u);
        CHECK_EQ(ports_on(), 0u);
        CHECK_EQ(g_sim.power_ups, 0u);
        g_other_held = 0u;
        exclusive();
        connect();
    }
}

TEST_CASE(a_higher_rail_never_reaches_the_sessions_servo)
{
    const uint16_t hi[3] = { 1u, 8400u, RAIL_MA };

    static const uint32_t k_settle[4] = { 0u, 400u, 2000u, UINT32_MAX };

    for (unsigned c = 0; c < 4u; ++c) {
        const uint32_t settle_ms = k_settle[c];
        const bool settles = settle_ms != UINT32_MAX;
        bench(true, RAIL_MV);
        CHECK_EQ(supply_page_write(&g_sp, LINK_SP_OUTPUT, 3u, hi, &g_sout,
                                   true), 0u);
        tick();
        two_ports_on();
        g_pd_by_hand = true;
        g_pd.st.v_mv = 8400u;
        CHECK_EQ(cmd(LINK_KST_OP_ENTER, 0, 0), 0);
        CHECK_EQ(refusal(), LINK_KST_REF_NONE);
        /* Port k waits from the ON: the others go off then. */
        while (g_rail.pending == false && busy()) {
            tick();
        }
        CHECK(g_rail.pending);
        CHECK_EQ(ports_on(), 0u);
        const uint32_t t_on = g_ms;
        while (busy() && (!settles || g_ms - t_on < settle_ms)) {
            tick();
            CHECK(!g_sim.powered);
        }
        if (settles) {
            g_pd.st.v_mv = RAIL_MV;
        }
        settle();
        if (settles) {
            CHECK_EQ(result(), KST_SES_OK);
            CHECK(g_sim.powered);
            CHECK_EQ(ports_on(), K_BIT);
        } else {
            CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
            CHECK_EQ(ports_on(), 0u);
            CHECK(!g_sim.powered);
            CHECK(!pd_wants_on());
        }
    }
}

/* The panel's PORT_WANT without bit k (S1 and S2 kept) @p at_ms into a
 * REBOOT's off time: port k is never switched on again, the session ends
 * with SUPPLY. */
static void want_off_in_reboot(uint32_t at_ms)
{
    bench(true, RAIL_MV);
    two_ports_on();
    connect();
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, 0, 0), 0);
    until_state(LINK_KST_RAIL_OFF);
    tick();
    const uint32_t t0 = g_ms;
    while (g_ms - t0 < at_ms) {
        tick();
    }
    CHECK_EQ(prog_rail_claim(&g_rail), K_BIT);   /* reads wanted */
    const unsigned ons = g_k_ons;
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, 0x0003u, g_ms), 0u);
    until_state(LINK_KST_PWM);
    run_ms(KST_PORT_SUPPLY_MS);
    CHECK_EQ(g_k_ons, ons);
    CHECK_EQ(ports_on(), 0x0003u);
    CHECK(!g_sim.powered);
    CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
}

TEST_CASE(a_port_want_without_k_in_a_reboot_keeps_it_off)
{
    static const uint32_t k_at[4] = { 0u, 500u, KST_PORT_RAIL_OFF_MS - 2u,
                                      KST_PORT_RAIL_OFF_MS - 1u };

    for (unsigned i = 0; i < 4u; ++i) {
        want_off_in_reboot(k_at[i]);
    }
}

TEST_CASE(a_port_want_without_k_ends_a_waiting_on)
{
    const uint16_t hi[3] = { 1u, 8400u, RAIL_MA };

    bench(true, RAIL_MV);
    CHECK_EQ(supply_page_write(&g_sp, LINK_SP_OUTPUT, 3u, hi, &g_sout, true),
             0u);
    tick();
    two_ports_on();
    g_pd_by_hand = true;
    g_pd.st.v_mv = 8400u;
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, 0, 0), 0);
    while (!g_rail.pending && busy()) {
        tick();
    }
    CHECK(g_rail.pending);
    run_ms(50u);
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, 0u, g_ms), 0u);
    CHECK(!g_rail.pending);
    g_pd.st.v_mv = RAIL_MV;             /* the PD mini there after all */
    settle();
    CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
    CHECK_EQ(ports_on(), 0u);
    CHECK(!g_sim.powered);
}

/* The panel's RAIL 1 without the switches: every servo on the rail
 * powered, nothing active, a pass for the masks. */
static void rail_1_alone(void)
{
    bench(false, RAIL_MV);
    CHECK_EQ(pw_write(LINK_PW_RAIL, 1u, g_ms), 0u);
    tick();
    CHECK(power_rail_driven(&g_pw));
}

/* @p op from PWM behind a PROG write that makes S1's channel active in the
 * same batch of frames (no pass between): refused, the rail on throughout.
 * And in the other order: taken; a rail on at the PROG write stays on,
 * and an ON after it is refused. */
static void connect_in_one_batch(link_kst_op_t op)
{
    rail_1_alone();
    g_rctx.active = 0x0001u;            /* prog_write(): S1 active */
    CHECK_EQ(cmd(op, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    for (unsigned i = 0; i < 2u * KST_PORT_RAIL_OFF_MS; ++i) {
        run_ms(1u);
        CHECK(power_rail_driven(&g_pw));
        CHECK_EQ(ports_on(), 0x01FFu);
    }
    CHECK_EQ(state(), LINK_KST_PWM);

    for (unsigned same_ma = 0; same_ma < 2u; ++same_ma) {
        rail_1_alone();
        if (same_ma != 0u) {
            const uint16_t sup[2] = { RAIL_MV, RAIL_MA };
            CHECK_EQ(kst_port_write(&g_port, LINK_KS_W_SUPPLY_MV, 2, sup,
                                    g_ms), 0);
        }
        CHECK_EQ(cmd(op, 0, 0), 0);
        CHECK_EQ(refusal(), LINK_KST_REF_NONE);
        /* REBOOT's cycle starts at the command: the rail is off before the
         * PROG write behind it; ENTER's ON at the rail's own voltage cuts
         * nothing. */
        const bool live = power_rail_driven(&g_pw);
        CHECK_EQ(live, op != LINK_KST_OP_REBOOT);
        g_rctx.active = 0x0001u;        /* prog_write() behind it */
        while (busy()) {
            CHECK_EQ(power_rail_driven(&g_pw), live);
            tick();
        }
        if (live) {
            /* A lower current limit is an ON, refused; the same is none. */
            CHECK_EQ(result(), same_ma != 0u ? (unsigned)KST_SES_OK
                                             : (unsigned)LINK_KST_RESULT_SUPPLY);
            CHECK_EQ(ports_on(), 0x01FFu);
        } else {
            CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
            CHECK_EQ(state(), LINK_KST_PWM);
            CHECK_EQ(ports_on(), 0u);
            CHECK(!g_sim.powered);
            CHECK_EQ(g_sim.power_ups, 1u);  /* the panel's RAIL 1 only */
        }
    }
}

TEST_CASE(a_connect_and_an_activation_in_one_batch_never_cut_a_live_channel)
{
    connect_in_one_batch(LINK_KST_OP_ENTER);
    connect_in_one_batch(LINK_KST_OP_REBOOT);
}

int main(void)
{
    RUN(without_the_switches_the_cycles_act_on_rail_en);
    RUN(with_the_switches_the_cycles_act_on_port_en_k);
    RUN(connect_is_refused_with_another_channel_active_without_switches);
    RUN(connect_is_refused_with_a_bec_found);
    RUN(another_voltage_switches_the_other_ports_off_until_the_end);
    RUN(an_error_during_a_write_waits_for_it_with_the_switches);
    RUN(an_error_during_a_write_waits_for_it_without_the_switches);
    RUN(a_lost_heartbeat_during_a_write_cuts_everything_at_once);
    RUN(a_trip_in_a_reboot_keeps_the_rail_off);
    RUN(connect_is_refused_while_another_session_holds_a_port);
    RUN(a_higher_rail_never_reaches_the_sessions_servo);
    RUN(a_port_want_without_k_in_a_reboot_keeps_it_off);
    RUN(a_port_want_without_k_ends_a_waiting_on);
    RUN(a_connect_and_an_activation_in_one_batch_never_cut_a_live_channel);
    return test_summary("prog_rail_kst");
}
