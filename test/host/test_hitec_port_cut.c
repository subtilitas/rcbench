/*
 * An error while the HITEC page holds a port (protocol 4.31): the panel's
 * OFF of the supply, which the SUPPLY page holds back from the PD mini
 * while the port writes to the servo's memory: while a transaction runs,
 * or while the last command taken started its step with
 * LINK_HT_W_CMD_MORE (hitec_port_writing(), supply_page_port_writes()).
 *
 * The port is shared/outputs/hitec_port.c against the servo of
 * hitec_sim.h, whose memory takes a write only while the supply is on; the
 * SUPPLY page is the real one over a PD mini that reads back what it is
 * asked in the same pass, wired as firmware/iomcu/src/main.c and
 * iomcu_hitec.c wire them.  The steps are the writer's: a classic write of
 * a cell and its balance (0x1C), a D series write, its save and its
 * reboot, each but the last with the bit.  The rig is hitec_port_rig.h.
 *
 * Under test: an OFF between two steps waits for the write's last step and
 * reaches the module in the pass its transaction ends, the servo holding
 * every write; an OFF during the last step the same; an OFF with no write
 * at once; a step whose bit stands and no step after it holds the OFF to
 * SUPPLY_PAGE_WRITE_WAIT_MS to the millisecond, across the wrap at
 * 2^32ms; the bit cleared by any other command, a refused one, a stop and
 * CLOSE, refused on OPEN, CLOSE, ABORT and CLEAR, and shown in
 * LINK_HT_F_MORE; after the panel's OFF a POWER's ON is refused until
 * CLOSE; the heartbeat lost during a held OFF switches it off at once.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_port_rig.h"

#include "link_msg.h"
#include "outputs.h"
#include "pdmini.h"
#include "supply_page.h"

static supply_page_t g_sp;
static outputs_t     g_out;
static pdmini_t      g_pd;

/* --------------------------------------------------------------- the rig */

/* As firmware/iomcu/src/iomcu_kst.c: the SUPPLY page's view and request. */
static void sp_supply(void *ctx, supply_port_view_t *out)
{
    (void)ctx;
    supply_page_port_view(&g_sp, out);
}

static bool sp_supply_set(void *ctx, bool on, uint16_t mv, uint16_t ma)
{
    (void)ctx;
    return supply_page_port_set(&g_sp, on, mv, ma, true);
}

/* One pass of the IOmcu: the port, the page told of its hold, frame and
 * write, the page's step, the module; the servo powered as it reads. */
static void pass_as(bool safe, bool stop)
{
    hitec_port_step(&g_port, safe, stop, g_ms);
    supply_page_hold(&g_sp, hitec_port_supply_hold(&g_port));
    supply_page_port_frame(&g_sp, hitec_port_frame_out(&g_port, g_ms), g_ms);
    supply_page_port_writes(&g_sp, hitec_port_writing(&g_port));
    supply_page_step(&g_sp, safe, &g_pd);
    g_pd.st.output = g_pd.want_output;
    g_pd.st.set_mv = g_pd.want_mv;
    g_pd.st.set_ma = g_pd.want_ma;
    supply_page_port_view(&g_sp, &g_hw.v);
    g_ms++;
    g_drv.us += 1000u;
}

static void pass(void)
{
    pass_as(true, false);
}

static void passes(unsigned ms)
{
    for (unsigned i = 0; i < ms; ++i) {
        pass();
    }
}

/* A command of the page as iomcu_hitec.c takes it: the SUPPLY page told of
 * the hold in the same frame. */
static uint8_t hcmd_raw(uint16_t word, uint16_t key, uint16_t crc)
{
    const uint8_t nack = cmd_raw(word, key, crc);

    supply_page_hold(&g_sp, hitec_port_supply_hold(&g_port));
    return nack;
}

/* The panel's OFF, as error_act() has supply_link_cut() write it. */
static void panel_off(void)
{
    const uint16_t off = 0u;

    CHECK_EQ(supply_page_write(&g_sp, LINK_SP_OUTPUT, 1u, &off, &g_out,
                               true), 0u);
}

/* The port at @p t0, the page wired and on from the panel, the line of
 * @p family open. */
static void open_at(uint32_t t0, uint8_t family)
{
    static const uint16_t k_wiring[4] = { 1u, 8u, 9u, 1u };
    const uint16_t on[3] = { 1u, MV, MA };

    fresh_at(t0, SUPPLY_ON);
    g_port.hw.supply = sp_supply;
    g_port.hw.supply_set = sp_supply_set;
    outputs_init(&g_out, 0u);
    outputs_reserve_pins(&g_out, (uint64_t)1u << 3);
    supply_page_init(&g_sp);
    pdmini_init(&g_pd, NULL, 0u);
    g_pd.answered = true;
    g_pd.state_known = true;
    g_pd.st.online = true;
    CHECK_EQ(supply_page_write(&g_sp, LINK_SP_ENABLE, 4u, k_wiring, &g_out,
                               true), 0u);
    CHECK_EQ(supply_page_write(&g_sp, LINK_SP_OUTPUT, 3u, on, &g_out, true),
             0u);
    pass();
    pass();
    CHECK(g_hw.v.on);
    line(family, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(hcmd_raw(LINK_HT_W_CMD_OF(LINK_HITEC_OP_OPEN, CH), 0u, stage()),
             0u);
    for (unsigned i = 0; i < 1000u && state() != LINK_HITEC_OPEN; ++i) {
        pass();
    }
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    CHECK(g_pd.want_output);
}

/* Step @p x as a SEND with the key, LINK_HT_W_CMD_MORE when @p more. */
static void step(const hitec_xfer_t *x, uint8_t dec, uint8_t arg, bool more)
{
    stage_send(x, dec, arg);
    CHECK_EQ(hcmd_raw((uint16_t)(LINK_HT_W_CMD_OF(LINK_HITEC_OP_SEND, CH)
                                 | (more ? LINK_HT_W_CMD_MORE : 0u)),
                      KEY, stage()),
             0u);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK_EQ(state(), LINK_HITEC_BUSY);
    CHECK_EQ(flag(LINK_HT_F_MORE), more);
}

/* Passes while the step runs; @p held: the module stays on through them. */
static void step_ends(bool held)
{
    while (state() == LINK_HITEC_BUSY) {
        CHECK(!held || g_pd.want_output);
        pass();
    }
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
}

/* The classic write of cell 0x10 and its balance, the first step with the
 * bit. */
static void classic_cell(bool more)
{
    hitec_xfer_t x;

    hitec_c_xfer(HITEC_C_WRITE_EEPROM, 0x10u, 0x55u, &x);
    step(&x, LINK_HITEC_DEC_C, HITEC_C_WRITE_EEPROM, more);
}

static void classic_balance(void)
{
    hitec_xfer_t x;

    hitec_c_xfer(HITEC_C_WRITE_EEPROM, 0x1Cu, 0xAAu, &x);
    step(&x, LINK_HITEC_DEC_C, HITEC_C_WRITE_EEPROM, false);
}

/* ---------------------------------------------------------------- the cases */

TEST_CASE(an_off_between_classic_steps_waits_for_the_balance)
{
    open_at(0u, HITEC_CLASSIC);
    classic_cell(true);
    step_ends(false);
    CHECK(flag(LINK_HT_F_MORE));
    CHECK(hitec_port_writing(&g_port));
    panel_off();
    CHECK(supply_page_off_held(&g_sp));
    /* The gap before the next step: the module and the servo stay on. */
    passes(32u);
    CHECK(g_pd.want_output);
    CHECK(g_hw.v.asked_on);
    classic_balance();
    step_ends(true);
    /* The balance ended in the last pass: the write is whole, then off. */
    CHECK(!hitec_port_writing(&g_port));
    CHECK(!g_pd.want_output);
    CHECK_EQ(g_drv.c_writes, 2u);
    CHECK_EQ(g_drv.eeprom[0x10], 0x55u);
    CHECK_EQ(g_drv.eeprom[0x1C], 0xAAu);
    passes(100u);
    CHECK(!g_pd.want_output);
}

TEST_CASE(an_off_between_d_series_steps_waits_for_the_reboot)
{
    static const uint8_t k_reg[3] = { 0x30u, HITEC_D_REG_SAVE,
                                      HITEC_D_REG_REBOOT };
    static const uint16_t k_wait[3] = { 20u, 100u, 0u };

    open_at(0u, HITEC_D);
    for (unsigned k = 0; k < 3u; ++k) {
        hitec_xfer_t x;

        hitec_d_write_xfer(0u, k_reg[k], k == 0u ? 0x0123u : 1u, &x);
        step(&x, LINK_HITEC_DEC_NONE, 0u, k < 2u);
        step_ends(k > 0u);
        if (k == 0u) {
            panel_off();
        }
        if (k < 2u) {
            /* The plan's wait after the write and after the save. */
            passes(k_wait[k]);
            CHECK(g_pd.want_output);
        }
    }
    CHECK(!g_pd.want_output);
    CHECK_EQ(g_drv.d_writes, 1u);
    CHECK_EQ(g_drv.dreg[0x30], 0x0123u);
    CHECK_EQ(g_drv.d_saves, 1u);
    CHECK_EQ(g_drv.d_reboots, 1u);
}

TEST_CASE(an_off_during_the_last_step_cuts_at_its_end)
{
    open_at(0u, HITEC_CLASSIC);
    classic_cell(true);
    step_ends(false);
    classic_balance();
    pass();
    CHECK_EQ(state(), LINK_HITEC_BUSY);
    panel_off();
    step_ends(true);
    CHECK(!g_pd.want_output);
    CHECK_EQ(g_drv.c_writes, 2u);
}

TEST_CASE(an_off_with_no_write_is_at_once)
{
    /* A step without the bit has ended: nothing runs, nothing stands. */
    open_at(0u, HITEC_CLASSIC);
    classic_balance();
    step_ends(false);
    CHECK(!hitec_port_writing(&g_port));
    panel_off();
    CHECK(!supply_page_off_held(&g_sp));
    pass();
    CHECK(!g_pd.want_output);
    CHECK(!g_hw.v.asked_on);
}

/* The bit stands and no step follows: the OFF waits to the bound, from the
 * clock of the pass before it.  The second start puts the clock's wrap at
 * 2^32ms inside the wait. */
TEST_CASE(a_bit_left_standing_holds_the_off_to_the_bound)
{
    static const uint32_t k_t0[2] = { 0u, 0xFFFFFFFFu - 3000u };

    for (unsigned w = 0; w < 2u; ++w) {
        uint32_t off_at;

        open_at(k_t0[w], HITEC_CLASSIC);
        classic_cell(true);
        step_ends(false);
        off_at = g_ms - 1u;
        panel_off();
        while ((uint32_t)(g_ms - off_at) < SUPPLY_PAGE_WRITE_WAIT_MS) {
            pass();
            CHECK(g_pd.want_output);
        }
        pass();
        CHECK(!g_pd.want_output);
        CHECK(!g_hw.v.asked_on);
        CHECK(flag(LINK_HT_F_MORE));
    }
}

TEST_CASE(the_bit_ends_with_any_other_command_a_refusal_a_stop_or_close)
{
    open_at(0u, HITEC_CLASSIC);
    /* Not on the commands that are no step. */
    for (unsigned o = LINK_HITEC_OP_OPEN; o <= LINK_HITEC_OP_CLEAR; ++o) {
        if (o == LINK_HITEC_OP_OPEN || o == LINK_HITEC_OP_CLOSE
            || o == LINK_HITEC_OP_ABORT || o == LINK_HITEC_OP_CLEAR) {
            CHECK_EQ(hcmd_raw((uint16_t)(LINK_HT_W_CMD_OF(o, CH)
                                         | LINK_HT_W_CMD_MORE),
                              0u, stage()),
                     LINK_NACK_BAD_VALUE);
        }
    }
    CHECK_EQ(state(), LINK_HITEC_OPEN);
    /* An ABORT after a step with the bit. */
    classic_cell(true);
    step_ends(false);
    CHECK_EQ(hcmd_raw(LINK_HT_W_CMD_OF(LINK_HITEC_OP_ABORT, CH), 0u, 0u), 0u);
    CHECK(!flag(LINK_HT_F_MORE));
    CHECK(!hitec_port_writing(&g_port));
    /* A command the page refuses: a SEND without the key. */
    classic_cell(true);
    step_ends(false);
    stage_classic_read(0x10u);
    CHECK_EQ(hcmd_raw((uint16_t)(LINK_HT_W_CMD_OF(LINK_HITEC_OP_SEND, CH)
                                 | LINK_HT_W_CMD_MORE),
                      0u, stage()),
             0u);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NO_KEY);
    CHECK(!flag(LINK_HT_F_MORE));
    /* A LISTEN and a LEVEL carry it. */
    memset(g_st + 1, 0, sizeof(g_st) - sizeof(g_st[0]));
    stage_listen(1000u, 1u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(hcmd_raw((uint16_t)(LINK_HT_W_CMD_OF(LINK_HITEC_OP_LISTEN, CH)
                                 | LINK_HT_W_CMD_MORE),
                      0u, stage()),
             0u);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK(flag(LINK_HT_F_MORE));
    step_ends(false);
    memset(g_st + 1, 0, sizeof(g_st) - sizeof(g_st[0]));
    SST(LINK_HT_W_LEVEL_LO) = 100u;
    CHECK_EQ(hcmd_raw((uint16_t)(LINK_HT_W_CMD_OF(LINK_HITEC_OP_LEVEL, CH)
                                 | LINK_HT_W_CMD_MORE),
                      0u, stage()),
             0u);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    CHECK(flag(LINK_HT_F_MORE));
    step_ends(false);
    /* A stop. */
    pass_as(true, true);
    CHECK(!flag(LINK_HT_F_MORE));
    CHECK(!hitec_port_writing(&g_port));
    /* CLOSE. */
    CHECK_EQ(hcmd_raw(LINK_HT_W_CMD_OF(LINK_HITEC_OP_CLEAR, CH), 0u, 0u), 0u);
    classic_cell(true);
    step_ends(false);
    CHECK_EQ(hcmd_raw(LINK_HT_W_CMD_OF(LINK_HITEC_OP_CLOSE, CH), 0u, 0u), 0u);
    CHECK_EQ(state(), LINK_HITEC_FREE);
    CHECK(!hitec_port_writing(&g_port));
    CHECK(!hitec_port_writing(NULL));
}

/* After the panel's OFF a POWER switches the supply off as ever, and its ON
 * is refused: the servo stays without power until CLOSE. */
TEST_CASE(an_off_refuses_a_powers_on_until_close)
{
    open_at(0u, HITEC_CLASSIC);
    panel_off();
    pass();
    CHECK(!g_pd.want_output);
    SST(LINK_HT_W_SUPPLY_MV) = MV;
    SST(LINK_HT_W_SUPPLY_MA) = MA;
    SST(LINK_HT_W_OFF_MS) = 100u;
    stage_listen(1000u, 1u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(hcmd_raw(LINK_HT_W_CMD_OF(LINK_HITEC_OP_POWER, CH), 0u, stage()),
             0u);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    for (unsigned i = 0; i < 2000u && state() != LINK_HITEC_OPEN; ++i) {
        pass();
        CHECK(!g_pd.want_output);
    }
    CHECK_EQ(result(), LINK_HITEC_RESULT_SUPPLY);
    passes(500u);
    CHECK(!g_pd.want_output);
    /* CLOSE lets go; a new session's POWER switches it on. */
    CHECK_EQ(hcmd_raw(LINK_HT_W_CMD_OF(LINK_HITEC_OP_CLOSE, CH), 0u, 0u), 0u);
    pass();
    line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(hcmd_raw(LINK_HT_W_CMD_OF(LINK_HITEC_OP_OPEN, CH), 0u, stage()),
             0u);
    for (unsigned i = 0; i < 1000u && state() != LINK_HITEC_OPEN; ++i) {
        pass();
    }
    SST(LINK_HT_W_SUPPLY_MV) = MV;
    SST(LINK_HT_W_SUPPLY_MA) = MA;
    SST(LINK_HT_W_OFF_MS) = 100u;
    stage_listen(1000u, 1u, false, 0u, LINK_HITEC_DEC_NONE, 0u);
    CHECK_EQ(hcmd_raw(LINK_HT_W_CMD_OF(LINK_HITEC_OP_POWER, CH), 0u, stage()),
             0u);
    for (unsigned i = 0; i < 2000u && !g_pd.want_output; ++i) {
        pass();
    }
    CHECK(g_pd.want_output);
}

TEST_CASE(a_lost_heartbeat_during_a_held_off_cuts_at_once)
{
    open_at(0u, HITEC_CLASSIC);
    classic_cell(true);
    step_ends(false);
    panel_off();
    passes(10u);
    CHECK(g_pd.want_output);
    pass_as(false, false);
    CHECK(!g_pd.want_output);
    CHECK(!supply_page_off_held(&g_sp));
    pass();
    CHECK(!g_pd.want_output);
}

int main(void)
{
    RUN(an_off_between_classic_steps_waits_for_the_balance);
    RUN(an_off_between_d_series_steps_waits_for_the_reboot);
    RUN(an_off_during_the_last_step_cuts_at_its_end);
    RUN(an_off_with_no_write_is_at_once);
    RUN(a_bit_left_standing_holds_the_off_to_the_bound);
    RUN(the_bit_ends_with_any_other_command_a_refusal_a_stop_or_close);
    RUN(an_off_refuses_a_powers_on_until_close);
    RUN(a_lost_heartbeat_during_a_held_off_cuts_at_once);
    return test_summary("hitec_port_cut");
}
