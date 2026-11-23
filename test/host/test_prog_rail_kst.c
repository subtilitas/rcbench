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
 * in no mode.
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

    prog_rail_tell(&g_rail, &g_pw, held, kst_port_writing(&g_port), g_ms);
    supply_page_hold(&g_sp, held);
    supply_page_port_writes(&g_sp, kst_port_writing(&g_port));
}

/* The CONNECT rules into the port's other-mask: ports_exclusive(). */
static void exclusive(void)
{
    kst_port_other_mask(&g_port,
                        prog_rail_connect(&g_pw, K, &g_rctx) != PROG_RAIL_OK
                            ? (uint8_t)(1u << CH) : 0u);
}

static uint8_t cmd(link_kst_op_t o, uint16_t key, uint16_t crc)
{
    const uint16_t in[4] = { LINK_KS_W_CMD_OF(o, CH),
                             (uint16_t)(reg(LINK_KS_SEQ) + 1u), key, crc };
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
    return test_summary("prog_rail_kst");
}
