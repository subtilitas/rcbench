/*
 * The wait after a REBOOT's supply cycle (link protocol 4.30): the KST page
 * on a programming port (shared/outputs/kst_port.c) takes
 * LINK_KS_W_CMD_LEAVE on a REBOOT from PROGRAMMING and waits in LEAVING for
 * LINK_KS_W_LEAVE_MS after RAIL_WAIT, and the panel's link module
 * (shared/bench/kst_link.c) sends the bit and the wait to an IOmcu of 4.30
 * only.
 *
 * Under test at the port: the wait at 0, 1, 10,000 and 10,001ms, the bit on
 * every other operation and from PWM, the reserved bit 4, a REBOOT without
 * the bit after a wait was written (an older panel), ABORT, a stop, an
 * unsafe bench and the supply off in the wait, ABORT in RAIL_OFF and
 * RAIL_WAIT with the bit, a write of register 28 in the wait, every other
 * command in it, and the wait across the 2^32ms wrap.  At the module: the
 * frames for 4.30 and for 4.29, and the requests it refuses.
 *
 * The clock: a pass is 100us on the wire's clock, and every tenth pass is
 * 1ms on the port's.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "kst_link.h"
#include "kst_port.h"
#include "kst_sim.h"

#define CH    0u     /* the port under test */
#define PIN   13u
#define MV    5000u  /* the staged supply */
#define MA    1000u
#define LAG_MS 20u   /* the simulated PD mini reads back this late */
#define WAIT_MS 300u /* the wait most cases stage */

static sim_t g_sim;
static kst_port_t g_port;

static struct {
    bool     taken;          /* between take() and give() */
    unsigned gives;
    supply_port_view_t v;    /* the PD mini */
    bool     moving;         /* a command the reading has not followed */
    uint32_t asked_ms;
    unsigned ons, offs;
} g_hw;

static uint32_t g_ms;
static unsigned g_sub;

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
    g_hw.taken = true;
    return true;
}

static void hw_give(void *ctx)
{
    (void)ctx;
    g_hw.gives++;
    g_hw.taken = false;
}

static void hw_supply(void *ctx, supply_port_view_t *out)
{
    (void)ctx;
    *out = g_hw.v;
}

static bool hw_supply_set(void *ctx, bool on, uint16_t mv, uint16_t ma)
{
    (void)ctx;
    if (on) {
        g_hw.ons++;
        g_hw.v.set_mv = mv;
        g_hw.v.set_ma = ma;
    } else {
        g_hw.offs++;
    }
    g_hw.v.asked_on = on;
    g_hw.v.on = false;
    g_hw.v.off = false;
    g_hw.moving = true;
    g_hw.asked_ms = g_ms;
    return true;
}

/* The SUPPLY page switches the output off: the panel's OFF. */
static void supply_asked_off(void)
{
    g_hw.v.asked_on = false;
    g_hw.v.on = false;
    g_hw.v.off = false;
    g_hw.moving = true;
    g_hw.asked_ms = g_ms;
}

/* ------------------------------------------------------------------ the page */

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

static bool flag(uint16_t f)
{
    return (reg(LINK_KS_STATE) & f) != 0u;
}

static unsigned result(void)
{
    return reg(LINK_KS_RESULT) & 0xFFu;
}

static unsigned refusal(void)
{
    return reg(LINK_KS_RESULT) >> 8;
}

/* A command frame with the next SEQ: the NACK, or 0. */
static uint8_t cmd_raw(uint16_t word)
{
    const uint16_t in[4] = { word, (uint16_t)(reg(LINK_KS_SEQ) + 1u), 0u,
                             0u };

    return kst_port_write(&g_port, 0, 4, in, g_ms);
}

static uint8_t cmd(link_kst_op_t o)
{
    return cmd_raw(LINK_KS_W_CMD_OF(o, CH));
}

/* REBOOT with the bit: the cycle ends in LEAVING. */
static uint8_t reboot_leave(void)
{
    return cmd_raw(LINK_KS_W_CMD_OF(LINK_KST_OP_REBOOT, CH)
                   | LINK_KS_W_CMD_LEAVE);
}

static void leave_ms(uint16_t ms)
{
    CHECK_EQ(kst_port_write(&g_port, LINK_KS_W_LEAVE_MS, 1, &ms, g_ms), 0);
}

/* ----------------------------------------------------------------- the clock */

static void tick_as(bool safe, bool stop)
{
    if (g_hw.moving && (uint32_t)(g_ms - g_hw.asked_ms) >= LAG_MS) {
        g_hw.v.on = g_hw.v.asked_on;
        g_hw.v.off = !g_hw.v.asked_on;
        g_hw.moving = false;
    }
    kst_port_step(&g_port, safe, stop, g_ms);
    g_sim.now_us += 100u;
    if (++g_sub == 10u) {
        g_sub = 0;
        g_ms++;
    }
}

static void run_ms(unsigned ms)
{
    for (unsigned i = 0; i < 10u * ms; ++i) {
        tick_as(true, false);
    }
}

/* Pass until no operation runs; 20s is the bound. */
static void settle(void)
{
    for (unsigned i = 0; i < 200000u && flag(LINK_KS_F_BUSY); ++i) {
        tick_as(true, false);
    }
    CHECK(!flag(LINK_KS_F_BUSY));
}

static void until_state(link_kst_state_t s)
{
    for (unsigned i = 0; i < 200000u && state() != s; ++i) {
        tick_as(true, false);
    }
    CHECK_EQ(state(), s);
}

/* ------------------------------------------------------------------ set-ups */

/* In PROGRAMMING from @p t0, the PD mini on at MV, the servo read. */
static void up_at(uint32_t t0)
{
    const uint16_t sup[2] = { MV, MA };
    kst_port_hw_t hw;

    sim_init(&g_sim);
    memset(&g_hw, 0, sizeof(g_hw));
    g_hw.v.online = true;
    g_hw.v.asked_on = true;
    g_hw.v.on = true;
    g_hw.v.set_mv = MV;
    g_hw.v.set_ma = MA;
    g_hw.v.vin_mv = 20000u;
    memset(&hw, 0, sizeof(hw));
    hw.ports = 1u;
    hw.pin = hw_pin;
    hw.take = hw_take;
    hw.give = hw_give;
    hw.supply = hw_supply;
    hw.supply_set = hw_supply_set;
    hw.driver = sim_driver(&g_sim);
    CHECK(kst_port_init(&g_port, &hw));
    g_ms = t0;
    g_sub = 0;
    kst_port_step(&g_port, true, false, t0);
    CHECK_EQ(kst_port_write(&g_port, LINK_KS_W_SUPPLY_MV, 2, sup, t0), 0);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER), 0);
    settle();
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL), 0);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    CHECK(flag(LINK_KS_F_IN_MODE));
}

/*
 * From @p t0, a REBOOT with the bit and the wait @p ms, through the cycle;
 * the port's clock as RAIL_WAIT ended is returned.  With a wait of 0 the
 * port is in PWM then, else in LEAVING.
 */
static uint32_t cycled_at(uint32_t t0, uint16_t ms)
{
    up_at(t0);
    leave_ms(ms);
    CHECK_EQ(reboot_leave(), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    until_state(LINK_KST_RAIL_WAIT);
    for (unsigned i = 0; i < 200000u && state() == LINK_KST_RAIL_WAIT; ++i) {
        /* The pin is the driver's through the cycle. */
        CHECK(g_hw.taken);
        tick_as(true, false);
    }
    CHECK_EQ(g_hw.offs, 1u);
    CHECK_EQ(g_hw.ons, 1u);
    return g_port.t0_ms;
}

static uint32_t cycled(uint16_t ms)
{
    return cycled_at(0u, ms);
}

/* Passes until PWM; the pin held until then.  The port's clock then. */
static uint32_t until_pwm(void)
{
    uint32_t at = g_ms;

    for (unsigned i = 0; i < 200000u && state() != LINK_KST_PWM; ++i) {
        CHECK(g_hw.taken);
        CHECK(kst_port_quiet_mask(&g_port) != 0u);
        at = g_ms;
        tick_as(true, false);
    }
    CHECK_EQ(state(), LINK_KST_PWM);
    return at;
}

/* ---------------------------------------------------------------- the port */

TEST_CASE(the_wait_after_the_cycle_is_register_28_from_1_to_10_000_ms)
{
    const uint16_t waits[] = { 1u, WAIT_MS, LINK_KST_LEAVE_MS_MAX };

    for (unsigned i = 0; i < sizeof(waits) / sizeof(waits[0]); ++i) {
        const uint32_t t0 = cycled(waits[i]);

        CHECK_EQ(state(), LINK_KST_LEAVING);
        CHECK(flag(LINK_KS_F_BUSY));
        CHECK_EQ(result(), KST_SES_BUSY);
        CHECK_EQ(LINK_KS_OP_OF(reg(LINK_KS_OP)), LINK_KST_OP_REBOOT);
        CHECK_EQ((uint32_t)(until_pwm() - t0), (uint32_t)waits[i]);
        CHECK_EQ(result(), KST_SES_OK);
        CHECK(!flag(LINK_KS_F_IN_MODE | LINK_KS_F_MUST_READ | LINK_KS_F_BUSY));
        CHECK(!g_hw.taken);
        CHECK_EQ(g_hw.gives, 1u);
        /* The supply stays on at what it had. */
        CHECK(g_hw.v.asked_on);
        CHECK_EQ(g_hw.v.set_mv, MV);
        CHECK_EQ(g_hw.offs, 1u);
    }
}

TEST_CASE(a_wait_of_0_ends_the_cycle_in_pwm)
{
    cycled(0u);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK(!g_hw.taken);
}

TEST_CASE(a_reboot_without_the_bit_ignores_the_wait_written)
{
    /* An older panel, or a CONNECT's cycle: register 28 holds a LEAVE's
     * wait, and the REBOOT ends in PWM at the end of RAIL_WAIT. */
    up_at(0u);
    leave_ms(5000u);
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT), 0);
    until_state(LINK_KST_RAIL_WAIT);
    for (unsigned i = 0; i < 200000u && state() == LINK_KST_RAIL_WAIT; ++i) {
        tick_as(true, false);
    }
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_OK);
}

TEST_CASE(a_wait_above_10_000_ms_is_refused_and_nothing_switches)
{
    up_at(0u);
    leave_ms(LINK_KST_LEAVE_MS_MAX + 1u);
    CHECK_EQ(reboot_leave(), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_LEAVE_MS);
    run_ms(100);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK_EQ(g_hw.offs, 0u);
    CHECK(g_sim.in_mode);
    /* The same REBOOT without the bit is taken. */
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
}

TEST_CASE(the_bit_is_refused_from_pwm_and_on_every_other_operation)
{
    up_at(0u);
    for (unsigned o = LINK_KST_OP_ENTER; o < LINK_KST_OP_COUNT; ++o) {
        if (o == LINK_KST_OP_REBOOT) {
            continue;
        }
        CHECK_EQ(cmd_raw(LINK_KS_W_CMD_OF(o, CH) | LINK_KS_W_CMD_LEAVE),
                 LINK_NACK_BAD_VALUE);
    }
    /* Bit 4 stays reserved. */
    CHECK_EQ(cmd_raw(LINK_KS_W_CMD_OF(LINK_KST_OP_REBOOT, CH) | 0x0010u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    /* From PWM: a cycle there is CONNECT's, and has no programming to
     * leave. */
    CHECK_EQ(cmd(LINK_KST_OP_LEAVE), 0);
    settle();
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(reboot_leave(), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NOT_PROGRAMMING);
    run_ms(100);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(g_hw.offs, 0u);
}

TEST_CASE(abort_in_the_wait_returns_to_programming_with_the_supply_on)
{
    cycled(WAIT_MS);
    run_ms(WAIT_MS / 2u);
    CHECK_EQ(cmd(LINK_KST_OP_ABORT), 0);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK(!flag(LINK_KS_F_BUSY | LINK_KS_F_MUST_READ));
    /* The cycle ended the backup; the image last read stays. */
    CHECK(!flag(LINK_KS_F_BACKUP));
    CHECK(flag(LINK_KS_F_IMAGE));
    run_ms(2u * WAIT_MS);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK(g_hw.taken);
    CHECK(g_hw.v.asked_on);
    /* A LEAVE is taken at once and waits its own time. */
    CHECK_EQ(cmd(LINK_KST_OP_LEAVE), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    settle();
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(g_hw.offs, 1u);
}

TEST_CASE(the_supply_off_after_an_abort_in_the_wait_ends_as_a_session)
{
    cycled(WAIT_MS);
    CHECK_EQ(cmd(LINK_KST_OP_ABORT), 0);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    supply_asked_off();
    until_state(LINK_KST_RAIL_OFF);
    until_state(LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK(!g_hw.v.asked_on);
}

TEST_CASE(a_stop_in_the_wait_releases_the_pin)
{
    cycled(WAIT_MS);
    run_ms(10);
    tick_as(true, true);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK(!g_hw.taken);
    CHECK(!flag(LINK_KS_F_BUSY | LINK_KS_F_IN_MODE));
    /* A disarm alone leaves the supply as it is. */
    CHECK(g_hw.v.asked_on);
    CHECK_EQ(g_hw.offs, 1u);
}

TEST_CASE(an_unsafe_bench_in_the_wait_releases_the_pin_and_the_supply)
{
    cycled(WAIT_MS);
    run_ms(10);
    tick_as(false, false);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK(!g_hw.taken);
    CHECK(!g_hw.v.asked_on);
    CHECK_EQ(g_hw.offs, 2u);
    /* Nothing is switched on while it stays so. */
    for (unsigned i = 0; i < 10000u; ++i) {
        tick_as(false, false);
    }
    CHECK_EQ(g_hw.ons, 1u);
}

TEST_CASE(the_supply_off_in_the_wait_ends_it_in_pwm_at_once)
{
    cycled(WAIT_MS);
    run_ms(10);
    supply_asked_off();
    tick_as(true, false);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK(!g_hw.taken);
    /* And one the PD mini's own protection switched off. */
    cycled(WAIT_MS);
    run_ms(10);
    g_hw.v.on = false;
    g_hw.v.off = true;
    tick_as(true, false);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
}

TEST_CASE(a_pd_mini_that_stops_answering_leaves_the_wait_alone)
{
    const uint32_t t0 = cycled(WAIT_MS);

    g_hw.v.online = false;
    CHECK_EQ((uint32_t)(until_pwm() - t0), WAIT_MS);
    CHECK_EQ(result(), KST_SES_OK);
}

TEST_CASE(every_command_but_abort_is_busy_in_the_wait)
{
    const uint32_t t0 = cycled(WAIT_MS);
    const link_kst_op_t ops[] = {
        LINK_KST_OP_ENTER, LINK_KST_OP_READ_ALL, LINK_KST_OP_VERIFY,
        LINK_KST_OP_POWER_CYCLED, LINK_KST_OP_REBOOT, LINK_KST_OP_LEAVE,
    };

    for (unsigned i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        CHECK_EQ(cmd(ops[i]), 0);
        CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    }
    CHECK_EQ(reboot_leave(), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    /* A wait written now is for the next one. */
    leave_ms(LINK_KST_LEAVE_MS_MAX);
    CHECK_EQ(state(), LINK_KST_LEAVING);
    CHECK_EQ((uint32_t)(until_pwm() - t0), WAIT_MS);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(g_hw.offs, 1u);
}

TEST_CASE(abort_in_the_cycle_with_the_bit_ends_it_off_and_waits_for_nothing)
{
    for (int in_wait = 0; in_wait < 2; ++in_wait) {
        up_at(0u);
        leave_ms(WAIT_MS);
        CHECK_EQ(reboot_leave(), 0);
        until_state(in_wait != 0 ? LINK_KST_RAIL_WAIT : LINK_KST_RAIL_OFF);
        CHECK_EQ(cmd(LINK_KST_OP_ABORT), 0);
        settle();
        CHECK_EQ(state(), LINK_KST_PWM);
        CHECK_EQ(result(), KST_SES_ERR_ABORTED);
        CHECK(!g_hw.v.asked_on);
        CHECK(!g_hw.taken);
    }
}

TEST_CASE(the_wait_after_the_cycle_holds_across_the_tick_wrap)
{
    /* The set-up and the cycle take as long at every start: the wait
     * starts 100ms before the wrap and ends 200ms after it. */
    const uint32_t lead = cycled_at(0u, WAIT_MS);
    const uint32_t t0 = cycled_at(0u - lead - 100u, WAIT_MS);

    CHECK_EQ(t0, 0u - 100u);
    run_ms(WAIT_MS - 2u);
    CHECK_EQ(state(), LINK_KST_LEAVING);
    CHECK(g_ms < 0x1000u);
    CHECK_EQ((uint32_t)(until_pwm() - t0), WAIT_MS);
    CHECK_EQ(result(), KST_SES_OK);
}

/* --------------------------------------------------------------- the module */

/* The module up at @p minor, the page read once: READY. */
static void link_at(kst_link_t *k, uint16_t minor)
{
    const uint16_t zero[LINK_KS_W_CMD_FRAME] = { 0 };
    kst_link_xfer_t x;

    kst_link_init(k);
    kst_link_came_up(k, minor);
    CHECK(kst_link_next(k, 0u, &x));
    CHECK(!x.write);
    kst_link_done(k, KST_LINK_ACK, zero);
    CHECK_EQ(k->phase, KST_LINK_READY);
}

static kst_link_request_t reboot_req(bool then_leave, uint16_t ms)
{
    kst_link_request_t r;

    memset(&r, 0, sizeof(r));
    r.op = LINK_KST_OP_REBOOT;
    r.port = CH;
    r.then_leave = then_leave;
    r.leave_ms = ms;
    return r;
}

TEST_CASE(the_module_sends_the_bit_and_the_wait_to_4_30_only)
{
    kst_link_t k;
    kst_link_xfer_t x;
    kst_link_request_t r = reboot_req(true, WAIT_MS);

    /* 4.30: register 28, then the command with the bit. */
    link_at(&k, LINK_MINOR_KST_CYCLE_WAIT);
    CHECK_EQ(kst_link_start(&k, &r), KST_LINK_START_OK);
    CHECK(kst_link_next(&k, 1u, &x));
    CHECK(x.write);
    CHECK_EQ(x.page, LINK_PAGE_KST);
    CHECK_EQ(x.off, LINK_KS_W_LEAVE_MS);
    CHECK_EQ(x.n, 1u);
    CHECK_EQ(x.regs[0], WAIT_MS);
    kst_link_done(&k, KST_LINK_ACK, NULL);
    CHECK(kst_link_next(&k, 2u, &x));
    CHECK(x.write);
    CHECK_EQ(x.off, LINK_KS_W_CMD);
    CHECK_EQ(x.regs[LINK_KS_W_CMD],
             LINK_KS_W_CMD_OF(LINK_KST_OP_REBOOT, CH) | LINK_KS_W_CMD_LEAVE);

    /* 4.29: nothing is sent. */
    link_at(&k, LINK_MINOR_KST_CYCLE_WAIT - 1u);
    CHECK_EQ(kst_link_start(&k, &r), KST_LINK_START_NO_OP);
    CHECK_EQ(k.phase, KST_LINK_READY);
    CHECK(!k.active);
    /* The REBOOT without the bit goes: no register 28, the plain word. */
    r = reboot_req(false, WAIT_MS);
    CHECK_EQ(kst_link_start(&k, &r), KST_LINK_START_OK);
    CHECK(kst_link_next(&k, 1u, &x));
    CHECK_EQ(x.off, LINK_KS_W_CMD);
    CHECK_EQ(x.regs[LINK_KS_W_CMD], LINK_KS_W_CMD_OF(LINK_KST_OP_REBOOT, CH));
}

TEST_CASE(the_module_refuses_the_bit_off_a_reboot_and_a_wait_out_of_range)
{
    kst_link_t k;
    kst_link_request_t r = reboot_req(true, LINK_KST_LEAVE_MS_MAX + 1u);

    link_at(&k, LINK_PROTOCOL_MINOR);
    CHECK_EQ(kst_link_start(&k, &r), KST_LINK_START_ARG);
    r.leave_ms = LINK_KST_LEAVE_MS_MAX;
    r.op = LINK_KST_OP_LEAVE;
    CHECK_EQ(kst_link_start(&k, &r), KST_LINK_START_ARG);
    r.op = LINK_KST_OP_ENTER;
    CHECK_EQ(kst_link_start(&k, &r), KST_LINK_START_ARG);
    CHECK(!k.active);
    r.op = LINK_KST_OP_REBOOT;
    CHECK_EQ(kst_link_start(&k, &r), KST_LINK_START_OK);
    CHECK(k.leave_due);
    CHECK_EQ(k.leave_ms, LINK_KST_LEAVE_MS_MAX);
}

int main(void)
{
    RUN(the_wait_after_the_cycle_is_register_28_from_1_to_10_000_ms);
    RUN(a_wait_of_0_ends_the_cycle_in_pwm);
    RUN(a_reboot_without_the_bit_ignores_the_wait_written);
    RUN(a_wait_above_10_000_ms_is_refused_and_nothing_switches);
    RUN(the_bit_is_refused_from_pwm_and_on_every_other_operation);
    RUN(abort_in_the_wait_returns_to_programming_with_the_supply_on);
    RUN(the_supply_off_after_an_abort_in_the_wait_ends_as_a_session);
    RUN(a_stop_in_the_wait_releases_the_pin);
    RUN(an_unsafe_bench_in_the_wait_releases_the_pin_and_the_supply);
    RUN(the_supply_off_in_the_wait_ends_it_in_pwm_at_once);
    RUN(a_pd_mini_that_stops_answering_leaves_the_wait_alone);
    RUN(every_command_but_abort_is_busy_in_the_wait);
    RUN(abort_in_the_cycle_with_the_bit_ends_it_off_and_waits_for_nothing);
    RUN(the_wait_after_the_cycle_holds_across_the_tick_wrap);
    RUN(the_module_sends_the_bit_and_the_wait_to_4_30_only);
    RUN(the_module_refuses_the_bit_off_a_reboot_and_a_wait_out_of_range);
    return test_summary("kst_cycle_wait");
}
