/*
 * A Hitec session on the remote servo node's common rail: the HITEC page
 * of shared/outputs/hitec_port.c against the servo of hitec_sim.h, whose
 * supply is the PD mini through the session's port switch (prog_rail.h),
 * with the port switches (FITTED 0x1FF) and without them (FITTED 0).  The
 * servo is powered while the session's view reads on.
 *
 * Wired as firmware/iomcu/src/iomcu_rsn.c, iomcu_hitec.c and main.c wire
 * it each pass and after each command: the port's step, the CONNECT rules
 * into the port's other-mask, the rail and the SUPPLY page told of the
 * hold and the write, the SUPPLY page's step.  A pass is 1ms.
 *
 * Under test: the classic entry's POWER cycle of 500ms acting on RAIL_EN
 * without the switches (every port off) and on PORT_EN k only with them
 * (the other ports on throughout), the listen from the ON; a POWER at
 * another voltage than the rail's switching the other ports off with the
 * switches until CLOSE; OPEN refused with another channel active without
 * the switches, and with a BEC found; the panel's error between the two
 * steps of a classic write: every other port off at once, the session's
 * port and the PD mini on until the write's tail ends, then off, the servo
 * holding both cells, and a POWER after it switching nothing on; OPEN in
 * one batch of frames with a PROG write that makes another channel
 * active, in both orders, never cutting a live channel (hitec_write()
 * calls ports_exclusive() before the port's write); a POWER with OFF_MS 0
 * in a write's tail whose ON waits, and the panel's SUPPLY OFF then: port
 * k never switched on.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_port_rig.h"

#include "prog_rail_rig.h"

#define K     4          /* the session's output: S5 */
#define K_BIT 0x0010u

/* --------------------------------------------------------------- the rig */

static void rs_supply(void *ctx, supply_port_view_t *out)
{
    (void)ctx;
    rail_view(out);
}

/* The servo powers up when its port reads on: its boot byte comes
 * (hitec_sim.h). */
static bool rs_supply_set(void *ctx, bool on, uint16_t mv, uint16_t ma)
{
    (void)ctx;
    const bool taken = rail_set(on, mv, ma);
    const bool was = g_hw.v.on;
    rail_view(&g_hw.v);
    if (g_hw.v.on && !was) {
        g_drv.boot_pending = true;      /* powered up here */
    }
    return taken;
}

/* Bit q: another page's session holds programming port q. */
static uint8_t g_other_held;
/* The driver's clock of the first pass after port k came on. */
static uint32_t g_on_us;

/* The CONNECT rules into the port's other-mask: ports_exclusive(). */
static void exclusive(void)
{
    static const int k_of[2] = { K, 6 };

    hitec_port_other_mask(&g_port,
                          prog_rail_connect_mask(&g_pw, k_of, 2u, g_other_held,
                                                 &g_rctx));
}

/* The rail and the SUPPLY page told of the hold and the write
 * (ports_tell_supply()), and the CONNECT rules. */
static void tell(void)
{
    const bool held = hitec_port_supply_hold(&g_port);

    g_rnow = g_ms;
    rail_tell(held, hitec_port_writing(&g_port));
    supply_page_hold(&g_sp, held);
    supply_page_port_writes(&g_sp, hitec_port_writing(&g_port));
    exclusive();
}

/* One pass: the port, the rail, the SUPPLY page and the module; the servo
 * powered as the session's view reads. */
static void pass_as(bool safe)
{
    g_rnow = g_ms;
    hitec_port_step(&g_port, safe, false, g_ms);
    tell();
    supply_page_port_frame(&g_sp, hitec_port_frame_out(&g_port, g_ms), g_ms);
    sp_pass(safe);
    const bool was = g_hw.v.on;
    rail_view(&g_hw.v);
    if (g_hw.v.on && !was) {
        g_drv.boot_pending = true;      /* powered up in this pass */
        g_on_us = g_drv.us + 1000u;     /* the next pass's clock */
    }
    g_ms++;
    g_drv.us += 1000u;
}

static void pass(void)
{
    pass_as(true);
}

static void passes(unsigned ms)
{
    for (unsigned i = 0; i < ms; ++i) {
        pass();
    }
}

static uint8_t hcmd_raw(uint16_t word, uint16_t key, uint16_t crc)
{
    exclusive();                /* hitec_write(): the rules at the command */
    const uint8_t nack = cmd_raw(word, key, crc);

    tell();
    return nack;
}

static void until(link_hitec_state_t s)
{
    for (unsigned i = 0; i < 20000u && state() != s; ++i) {
        pass();
    }
    CHECK_EQ(state(), s);
}

/* The node with or without the switches, the PD mini on at RAIL_MV; the
 * panel's 2s hold of port k (and of S1 and S2 with the switches). */
static void bench(bool switches)
{
    fresh_at(0u, SUPPLY_ON);
    g_port.hw.supply = rs_supply;
    g_port.hw.supply_set = rs_supply_set;
    g_port.hw.on_read = true;           /* as main.c on board 3 */
    rig_start(switches, K, true);
    g_other_held = 0u;
    CHECK_EQ(pw_write(LINK_PW_RAIL, 1u, g_ms), 0u);
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, 0x0003u | K_BIT, g_ms), 0u);
    pass();
    pass();
    CHECK(g_hw.v.on);
}

static uint8_t open_port(void)
{
    line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    return hcmd_raw(LINK_HT_W_CMD_OF(LINK_HITEC_OP_OPEN, CH), 0u, stage());
}

static void open_ok(void)
{
    CHECK_EQ(open_port(), 0u);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    until(LINK_HITEC_OPEN);
}

/* POWER at @p mv with @p off_ms off, and a listen from the ON. */
static void power(uint16_t mv, uint16_t off_ms)
{
    stage_listen(100000u, 1u, false, 0u, LINK_HITEC_DEC_LISTEN, 0x5Au);
    SST(LINK_HT_W_SUPPLY_MV) = mv;
    SST(LINK_HT_W_SUPPLY_MA) = MA;
    SST(LINK_HT_W_OFF_MS) = off_ms;
    CHECK_EQ(hcmd_raw(LINK_HT_W_CMD_OF(LINK_HITEC_OP_POWER, CH), 0u, stage()),
             0u);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
}

/* The classic entry's cycle: port k (or the rail) off for 500ms and more,
 * the others as @p others_on, then on with the listen from the ON. */
static void entry_cycle(bool switches, uint16_t others_on)
{
    uint32_t off = 0u;

    power(MV, 500u);
    CHECK_EQ(state(), LINK_HITEC_POWER_OFF);
    while (state() == LINK_HITEC_POWER_OFF) {
        if ((ports_on() & K_BIT) == 0u) {
            ++off;
            CHECK(!g_hw.v.on);
            CHECK_EQ(ports_on(), others_on);
            CHECK_EQ(power_rail_driven(&g_pw), switches);
        }
        pass();
    }
    CHECK(off >= 500u);
    CHECK(off < 600u);
    CHECK((ports_on() & K_BIT) != 0u);
    until(LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
    CHECK_EQ(cap_byte(0), 0x5A);        /* the boot byte, from the ON */
}

TEST_CASE(without_the_switches_the_entry_cycle_acts_on_rail_en)
{
    bench(false);
    open_ok();
    entry_cycle(false, 0u);
    CHECK_EQ(ports_on(), 0x01FFu);
}

TEST_CASE(with_the_switches_the_entry_cycle_acts_on_port_en_k)
{
    bench(true);
    open_ok();
    entry_cycle(true, 0x0003u);
    CHECK_EQ(ports_on(), 0x0003u | K_BIT);
}

TEST_CASE(a_power_at_another_voltage_switches_the_other_ports_off)
{
    bench(true);
    open_ok();
    power(5500u, 500u);
    until(LINK_HITEC_OPEN);
    /* The PD mini moved: port k on PROG_RAIL_SETTLE_MS after it reads
     * 5500mV, after the 100ms listen of this POWER. */
    for (unsigned i = 0; i < 300u && (ports_on() & K_BIT) == 0u; ++i) {
        pass();
    }
    CHECK_EQ(ports_on(), K_BIT);
    CHECK_EQ(g_pd.want_mv, 5500u);
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, K_BIT | 0x0001u, g_ms),
             (uint8_t)LINK_NACK_BUSY);
    /* CLOSE lets go: the panel's next hold brings them back. */
    CHECK_EQ(hcmd_raw(LINK_HT_W_CMD_OF(LINK_HITEC_OP_CLOSE, CH), 0u, 0u), 0u);
    pass();
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, K_BIT | 0x0003u, g_ms), 0u);
    CHECK_EQ(ports_on(), K_BIT | 0x0003u);
}

TEST_CASE(open_is_refused_with_another_channel_active_without_switches)
{
    bench(false);
    g_rctx.active = 0x0100u;            /* S9's channel is not OFF */
    tell();
    CHECK_EQ(open_port(), 0u);
    CHECK_EQ(refusal(), LINK_HITEC_REF_HELD);
    passes(100u);
    CHECK_EQ(state(), LINK_HITEC_FREE);
    g_rctx.active = 0u;
    tell();
    open_ok();
    /* With the switches S9 may stay active. */
    bench(true);
    g_rctx.active = 0x0100u;
    tell();
    open_ok();
}

TEST_CASE(open_is_refused_with_a_bec_found)
{
    for (unsigned sw = 0; sw < 2u; ++sw) {
        int32_t v[LINK_PW_PORTS] = { 0 };

        bench(sw != 0u);
        CHECK_EQ(pw_write(LINK_PW_RAIL, 0u, g_ms), 0u);
        v[8] = 1200;                    /* an ESC's BEC on S9 */
        (void)power_window(&g_pw, 0u, 1u, v, LINK_PW_PORT_ALL, RAIL_MV);
        (void)power_window(&g_pw, 0u, 3u, v, LINK_PW_PORT_ALL, RAIL_MV);
        CHECK_EQ(pw_reg(LINK_PW_BEC), 0x0100u);
        tell();
        CHECK_EQ(open_port(), 0u);
        CHECK_EQ(refusal(), LINK_HITEC_REF_HELD);
        v[8] = 0;
        (void)power_window(&g_pw, 0u, 4u, v, LINK_PW_PORT_ALL, RAIL_MV);
        tell();
        open_ok();
    }
}

/* A SEND of a classic write of @p addr, LINK_HT_W_CMD_MORE when @p more. */
static void classic_write(uint8_t addr, uint8_t value, bool more)
{
    hitec_xfer_t x;

    hitec_c_xfer(HITEC_C_WRITE_EEPROM, addr, value, &x);
    stage_send(&x, LINK_HITEC_DEC_C, HITEC_C_WRITE_EEPROM);
    CHECK_EQ(hcmd_raw((uint16_t)(LINK_HT_W_CMD_OF(LINK_HITEC_OP_SEND, CH)
                                 | (more ? LINK_HT_W_CMD_MORE : 0u)),
                      KEY, stage()),
             0u);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
}

static void step_ends(void)
{
    while (state() == LINK_HITEC_BUSY) {
        CHECK((ports_on() & K_BIT) != 0u);
        pass();
    }
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
}

static void error_between_two_writes(bool switches)
{
    bench(switches);
    open_ok();
    classic_write(0x10u, 0x55u, true);
    step_ends();
    CHECK(hitec_port_writing(&g_port));
    /* The panel's error: SUPPLY OFF, RAIL 0, PORT_WANT 0. */
    sp_panel_off();
    CHECK_EQ(pw_write(LINK_PW_RAIL, 0u, g_ms), 0u);
    CHECK_EQ(pw_write(LINK_PW_PORT_WANT, 0u, g_ms), 0u);
    CHECK_EQ(ports_on(), switches ? K_BIT : 0x01FFu);
    passes(32u);
    CHECK(g_hw.v.on);
    CHECK(g_pd.want_output);
    classic_write(0x1Cu, 0xAAu, false);
    step_ends();
    while (hitec_port_writing(&g_port)) {
        CHECK((ports_on() & K_BIT) != 0u);
        CHECK(g_pd.want_output);
        pass();
    }
    CHECK_EQ(ports_on(), 0u);
    CHECK(!power_rail_driven(&g_pw));
    CHECK(!g_hw.v.asked_on);
    passes(SUPPLY_PAGE_OFF_WAIT_MS + 1u);
    CHECK(!g_pd.want_output);
    CHECK_EQ(g_drv.c_writes, 2u);
    CHECK_EQ(g_drv.eeprom[0x10], 0x55u);
    CHECK_EQ(g_drv.eeprom[0x1C], 0xAAu);
    /* A POWER after the error switches nothing on until CLOSE. */
    power(MV, 100u);
    for (unsigned i = 0; i < 2000u && state() != LINK_HITEC_OPEN; ++i) {
        pass();
        CHECK_EQ(ports_on(), 0u);
    }
    CHECK_EQ(result(), LINK_HITEC_RESULT_SUPPLY);
}

TEST_CASE(an_error_between_two_writes_waits_for_the_second_with_switches)
{
    error_between_two_writes(true);
}

TEST_CASE(an_error_between_two_writes_waits_without_the_switches)
{
    error_between_two_writes(false);
}

/* A trip @p at_ms into a POWER's off wait: nothing is switched on again,
 * the POWER ends with its error. */
static void trip_in_power(bool switches, uint32_t at_ms)
{
    bench(switches);
    open_ok();
    power(MV, 500u);
    while (state() == LINK_HITEC_POWER_OFF && (ports_on() & K_BIT) != 0u) {
        pass();
    }
    const uint32_t t0 = g_ms;
    while (g_ms - t0 < at_ms) {
        pass();
    }
    CHECK_EQ(state(), LINK_HITEC_POWER_OFF);
    power_trip(&g_pw, LINK_PW_TRIP_CRIT_N);
    prog_rail_off(&g_rail);
    for (unsigned i = 0; i < 4000u; ++i) {
        pass();
        CHECK_EQ(ports_on(), 0u);
        CHECK(!g_hw.v.on);
    }
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_SUPPLY);
}

TEST_CASE(a_trip_in_a_power_wait_keeps_the_rail_off)
{
    static const uint32_t k_at[4] = { 0u, 250u, 498u, 499u };

    for (unsigned sw = 0; sw < 2u; ++sw) {
        for (unsigned i = 0; i < 4u; ++i) {
            trip_in_power(sw != 0u, k_at[i]);
        }
    }
}

TEST_CASE(open_is_refused_while_another_session_holds_a_port)
{
    for (unsigned sw = 0; sw < 2u; ++sw) {
        bench(sw != 0u);
        g_other_held = 0x02u;           /* KST holds programming port 1 */
        tell();
        CHECK_EQ(open_port(), 0u);
        CHECK_EQ(refusal(), LINK_HITEC_REF_HELD);
        g_other_held = 0u;
        tell();
        open_ok();
    }
}

/* POWER at 5500mV with the PD mini by hand: it reads 5500mV @p settle_ms
 * after the ON (never for UINT32_MAX).  The listen starts in the pass port
 * k first reads on, and the classic entry's boot byte is heard. */
static void power_moved(bool switches, uint32_t settle_ms)
{
    bench(switches);
    open_ok();
    g_pd_by_hand = true;
    g_pd.st.v_mv = MV;
    const unsigned starts = g_drv.starts;
    power(5500u, 500u);
    while (state() == LINK_HITEC_POWER_OFF && !g_pd.want_output) {
        pass();
    }
    while (state() == LINK_HITEC_POWER_OFF && g_pd.want_mv != 5500u) {
        pass();
    }
    const uint32_t t_on = g_ms;
    while (state() == LINK_HITEC_POWER_OFF) {
        if (settle_ms != UINT32_MAX && g_ms - t_on >= settle_ms) {
            g_pd.st.v_mv = 5500u;
        }
        CHECK_EQ(g_drv.starts, starts);  /* no listen before the power */
        pass();
    }
    if (settle_ms == UINT32_MAX) {
        CHECK_EQ(state(), LINK_HITEC_OPEN);
        CHECK_EQ(result(), LINK_HITEC_RESULT_SUPPLY);
        CHECK_EQ(g_drv.starts, starts);
        CHECK(g_ms - t_on >= PROG_RAIL_WAIT_MS);
        CHECK(g_ms - t_on <= PROG_RAIL_WAIT_MS + 2u);
        passes(100u);
        CHECK_EQ(ports_on(), 0u);
        CHECK(!g_pd.want_output);
        return;
    }
    CHECK_EQ(g_drv.starts, starts + 1u);
    CHECK_EQ(g_drv.t_start, g_on_us);
    until(LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
    CHECK_EQ(cap_byte(0), 0x5A);
    CHECK_EQ(ports_on(), switches ? K_BIT : 0x01FFu);
}

TEST_CASE(a_power_at_a_moved_voltage_listens_from_the_servos_power)
{
    for (unsigned sw = 0; sw < 2u; ++sw) {
        power_moved(sw != 0u, 0u);
        power_moved(sw != 0u, 400u);
        power_moved(sw != 0u, 2000u);
        power_moved(sw != 0u, UINT32_MAX);
    }
}

TEST_CASE(a_stop_while_a_power_waits_for_its_on_asks_the_supply_off)
{
    bench(true);
    open_ok();
    g_pd_by_hand = true;
    g_pd.st.v_mv = MV;
    power(5500u, 100u);
    for (unsigned i = 0; i < 300u; ++i) {
        pass();
    }
    CHECK_EQ(state(), LINK_HITEC_POWER_OFF);
    CHECK(g_port.on_wait != 0u);
    pass_as(false);
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_CUT);
    CHECK(!g_rail.pending);
    pass();
    CHECK_EQ(ports_on(), 0u);
    CHECK(!g_pd.want_output);
}

/* OPEN behind a PROG write that makes S1's channel active in the same
 * batch of frames (no pass between), without the switches: refused, the
 * rail on.  And in the other order: OPEN taken, switching nothing, the
 * rail on. */
TEST_CASE(an_open_and_an_activation_in_one_batch_never_cut_a_live_channel)
{
    bench(false);
    g_rctx.active = 0x0001u;            /* prog_write(): S1 active */
    CHECK_EQ(open_port(), 0u);
    CHECK_EQ(refusal(), LINK_HITEC_REF_HELD);
    passes(1000u);
    CHECK_EQ(state(), LINK_HITEC_FREE);
    CHECK(power_rail_driven(&g_pw));
    CHECK_EQ(ports_on(), 0x01FFu);

    bench(false);
    CHECK_EQ(open_port(), 0u);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    g_rctx.active = 0x0001u;            /* prog_write() behind it */
    for (unsigned i = 0; i < 1000u; ++i) {
        pass();
        CHECK(power_rail_driven(&g_pw));
        CHECK_EQ(ports_on(), 0x01FFu);
    }
    CHECK_EQ(state(), LINK_HITEC_OPEN);
}

/* A POWER with OFF_MS 0 in a write's tail, its ON waiting for a moved PD
 * mini, and the panel's SUPPLY OFF then: the PD mini stays on for the
 * write, port k is never switched on. */
TEST_CASE(a_supply_off_in_a_writes_tail_ends_a_waiting_power)
{
    for (unsigned sw = 0; sw < 2u; ++sw) {
        bench(sw != 0u);
        open_ok();
        classic_write(0x10u, 0x55u, true);
        step_ends();
        CHECK(hitec_port_writing(&g_port));
        g_pd_by_hand = true;
        g_pd.st.v_mv = MV;
        power(5500u, 0u);
        for (unsigned i = 0; i < 400u && !g_rail.pending; ++i) {
            pass();
        }
        CHECK(g_rail.pending);
        const uint16_t was_on = ports_on();
        CHECK_EQ(was_on & K_BIT, 0u);
        sp_panel_off();
        g_pd.st.v_mv = 5500u;           /* the PD mini there after all */
        for (unsigned i = 0; i < PROG_RAIL_WAIT_MS + 500u; ++i) {
            pass();
            CHECK_EQ(ports_on() & K_BIT, 0u);
        }
        CHECK(!g_rail.pending);
        CHECK(!g_hw.v.on);
    }
}

int main(void)
{
    RUN(without_the_switches_the_entry_cycle_acts_on_rail_en);
    RUN(with_the_switches_the_entry_cycle_acts_on_port_en_k);
    RUN(a_power_at_another_voltage_switches_the_other_ports_off);
    RUN(open_is_refused_with_another_channel_active_without_switches);
    RUN(open_is_refused_with_a_bec_found);
    RUN(an_error_between_two_writes_waits_for_the_second_with_switches);
    RUN(an_error_between_two_writes_waits_without_the_switches);
    RUN(a_trip_in_a_power_wait_keeps_the_rail_off);
    RUN(open_is_refused_while_another_session_holds_a_port);
    RUN(a_power_at_a_moved_voltage_listens_from_the_servos_power);
    RUN(a_stop_while_a_power_waits_for_its_on_asks_the_supply_off);
    RUN(an_open_and_an_activation_in_one_batch_never_cut_a_live_channel);
    RUN(a_supply_off_in_a_writes_tail_ends_a_waiting_power);
    return test_summary("prog_rail_hitec");
}
