/*
 * The servo tester's programming port after the IOmcu restarts
 * (shared/outputs/servo_link.c with shared/bench/prog_link.c).
 *
 * PROG's TARGET and PWM are not kept: an IOmcu that starts again holds the
 * standard servo outputs as its target and no pulse bit.  The panel keeps
 * what the SERVO screen wants and writes it back on a bench at rest, within
 * the page's read cadence (PROG_LINK_POLL_MS, 500ms) of the link coming up.
 *
 * The far end here is a model of the PROG page's TARGET, PWM and port 0's
 * CMD: each write lands in it and is acknowledged unless a case makes it
 * silent.  The panel's read of the page is taken from the model.  A pass is
 * what the panel's control task does on each poll: the page read when due,
 * servo_link_prog_service() and servo_link_service().  The clock moves 10ms
 * a pass.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_host.h"
#include "link_pages.h"
#include "link_port.h"
#include "prog_link.h"
#include "servo_link.h"

/* ------------------------------------------------------------ the far end */

/** @brief The far end's PROG page and what reached it. */
static struct {
    bool     quiet;          /* nothing answers                          */
    uint16_t target;         /* PROG TARGET                              */
    uint16_t pwm;            /* PROG PWM                                 */
    uint16_t cmd;            /* port 0's CMD                             */
    bool     armed;          /* the bank is ARMED for the surfaces       */
    unsigned target_writes;  /* TARGET writes acknowledged               */
    unsigned pin_writes;     /* PIN writes of port 0 acknowledged        */
    unsigned pulses;         /* passes on which port 0 rendered a pulse  */
    uint16_t func0;          /* port 0's FUNC                            */
    unsigned func_writes;    /* FUNC writes of port 0 acknowledged       */
    unsigned sweep_stops;    /* SERVO page sweep stops acknowledged      */
} far;

static uint32_t s_now;

/**
 * @brief The port's clock.
 *
 * @param ctx Unused.
 * @return The rig's clock, in ms.
 */
static uint32_t port_now(void *ctx)
{
    (void)ctx;
    return s_now;
}

/**
 * @brief One exchange with the far end: a write lands in the model and is
 *        acknowledged, unless the far end is silent.
 *
 * @param ctx   Unused.
 * @param h     The link host.
 * @param req   The request.
 * @param reply Receives the answer.
 * @return true when the far end answered.
 */
static bool port_exchange(void *ctx, link_host_t *h, const link_msg_t *req,
                          link_msg_t *reply)
{
    (void)ctx;
    if (far.quiet) {
        link_host_abandon(h);
        return false;
    }
    if (req->page == LINK_PAGE_PROG && req->count == 1u) {
        if (req->offset == LINK_PG_TARGET) {
            far.target = req->regs[0];
            ++far.target_writes;
        } else if (req->offset == LINK_PG_PWM) {
            far.pwm = req->regs[0];
        } else if (req->offset == LINK_PG_REG(0u, LINK_PG_CMD)) {
            far.cmd = req->regs[0];
        } else if (req->offset == LINK_PG_REG(0u, LINK_PG_PIN)) {
            ++far.pin_writes;
        } else if (req->offset == LINK_PG_FUNC(0u)) {
            far.func0 = req->regs[0];
            ++far.func_writes;
        }
    }
    if (req->page == LINK_PAGE_SERVO && req->offset == LINK_SV_SWEEP
        && req->count == 1u && req->regs[0] == 0u) {
        ++far.sweep_stops;
    }
    link_msg_t part;
    memset(&part, 0, sizeof(part));
    part.page   = req->page;
    part.offset = req->offset;
    part.op     = LINK_OP_ACK;
    part.count  = req->count;
    memcpy(part.regs, req->regs, (size_t)req->count * sizeof(req->regs[0]));
    return link_host_accept(h, &part, s_now, reply);
}

/* -------------------------------------------------------------- the panel */

static struct {
    bool     armed;
    uint32_t stops;
    unsigned arms;
} panel;

static bool fake_armed(void *ctx)
{
    (void)ctx;
    return panel.armed;
}

static bool fake_stopped(void *ctx)
{
    (void)ctx;
    return false;
}

static void fake_alert(void *ctx, servo_link_alert_t a)
{
    (void)ctx;
    (void)a;
}

static void fake_disarm(void *ctx, bool link_up)
{
    (void)ctx;
    (void)link_up;
    panel.armed = false;
}

static uint32_t fake_stop_count(void *ctx)
{
    (void)ctx;
    return panel.stops;
}

static void fake_arm_surfaces(void *ctx)
{
    (void)ctx;
    ++panel.arms;
}

static link_host_t host;
static prog_link_t prog;
static const link_port_t k_port = { port_exchange, port_now, NULL, NULL };
static const servo_link_ctx_t k_ctx = {
    .host         = &host,
    .port         = &k_port,
    .prog         = &prog,
    .ctx          = NULL,
    .armed        = fake_armed,
    .stopped      = fake_stopped,
    .alert        = fake_alert,
    .disarm       = fake_disarm,
    .stop_count   = fake_stop_count,
    .arm_surfaces = fake_arm_surfaces,
};

/** @brief The standard outputs' surfaces in the binding the panel reads. */
static uint8_t s_channels;
/** @brief The port the SERVO screen wants, 1 + n; 0 the standard outputs. */
static uint8_t s_want = 1u;

/**
 * @brief The panel reads the page from the model: port 0 on GPIO 13, SERVO
 *        PWM and bound, the other three ports without a pin.
 */
static void read_far(void)
{
    uint16_t page[LINK_PG_REGS];
    memset(page, 0, sizeof(page));
    page[LINK_PG_COUNT]  = 4u;
    page[LINK_PG_FLAGS]  = far.func0 == (uint16_t)LINK_PG_FN_SERVO_PWM
                               ? LINK_PG_F_BOUND(0u)
                               : LINK_PG_F_DSHOT(0u);
    page[LINK_PG_PWM]    = far.pwm;
    page[LINK_PG_TARGET] = far.target;
    for (unsigned n = 0; n < LINK_PG_PORTS_MAX; ++n) {
        page[LINK_PG_REG(n, LINK_PG_PIN)] =
            (n == 0u) ? (uint16_t)13u : (uint16_t)LINK_PG_PIN_NONE;
        page[LINK_PG_FUNC(n)] = (n == 0u) ? far.func0 : (uint16_t)1u;
    }
    prog_link_read(&prog, far.quiet ? NULL : page, s_now);
}

/**
 * @brief One pass of the panel's control task, 10ms of clock: the screen's
 *        want (PROG PORT 1, its pulse on), the page read when due, the
 *        target service and the servo refresh.
 */
static void pass(void)
{
    prog_link_want(&prog, s_want, s_want != 0u);
    if (prog_link_read_due(&prog, s_now)) {
        read_far();
    }
    servo_link_prog_service();
    servo_link_service(true);
    /* The IOmcu renders port 0 while its bank is ARMED for the surfaces,
     * port 0 is the target and its pulse bit is set. */
    if (far.armed && far.target == 1u && (far.pwm & 1u) != 0u) {
        ++far.pulses;
    }
    s_now += 10u;
}

/**
 * @brief Passes for @p ms of clock.
 *
 * @param ms The clock to run, in ms.
 */
static void run_ms(uint32_t ms)
{
    const uint32_t until = s_now + ms;
    while ((int32_t)(s_now - until) < 0) {
        pass();
    }
}

/**
 * @brief The link comes up at protocol 4.28 with @p channels standard
 *        surfaces bound, as panel_poll.c's link-up does.
 *
 * @param channels The standard outputs' surfaces.
 */
static void link_up(uint8_t channels)
{
    s_channels = channels;
    servo_link_came_up(LINK_MINOR_PROG_DSHOT);
    servo_link_binding(s_channels, true);
    prog_link_came_up(&prog, LINK_MINOR_PROG_DSHOT);
}

/**
 * @brief The link goes down, as panel_poll.c's link-lost edge does: a bank
 *        that was ARMED is disarmed by the STOP the loss latches.
 */
static void link_down(void)
{
    if (panel.armed) {
        panel.armed = false;
        ++panel.stops;
    }
    servo_link_lost();
    prog_link_lost(&prog);
}

/** @brief The IOmcu starts again: PWM and TARGET read 0, CMD at rest. */
static void far_restart(void)
{
    far.target = 0u;
    far.pwm    = 0u;
    far.cmd    = 0u;
    far.armed  = false;
}

/**
 * @brief A bench at rest with port 0 the target and pulsing, the standard
 *        outputs with @p channels surfaces, a position of 1700us written to
 *        the port and held there.
 *
 * @param channels The standard outputs' surfaces.
 */
static void at_port_with_position(uint8_t channels)
{
    memset(&far, 0, sizeof(far));
    memset(&panel, 0, sizeof(panel));
    s_want = 1u;
    s_now = 1000u;
    link_host_init(&host, s_now);
    prog_link_init(&prog);
    servo_link_init(&k_ctx);
    link_up(channels);
    run_ms(100u);
    servo_cmd_t c;
    memset(&c, 0, sizeof(c));
    c.kind     = SERVO_CMD_POSITION;
    c.value_us = 1700u;
    c.min_us   = 1000u;
    c.max_us   = 2000u;
    servo_link_apply(c, true, panel.stops);
}

/** @brief Port 0's CMD at the centre of its range. */
#define CMD_CENTRE ((uint16_t)(LINK_CH_SPAN / 2u))

/* ------------------------------------------------------------------ cases */

TEST_CASE(the_port_is_the_target_with_a_position_before_any_restart)
{
    at_port_with_position(0x00u);
    CHECK_EQ(far.target, 1u);
    CHECK_EQ(far.pwm, 1u);
    CHECK_EQ(far.cmd, 700u);
    CHECK_EQ(prog_link_drive(&prog).target, 1u);
}

/*
 * The station's case: no standard output is a surface, the port held a
 * position, the IOmcu restarts while SAFE.  The release owed for the port
 * goes to the standard outputs the restarted IOmcu names, where there is
 * nothing to centre; the port's TARGET and pulse come back within one read
 * cadence.
 */
TEST_CASE(a_restart_while_safe_gives_the_port_back_within_a_read)
{
    at_port_with_position(0x00u);
    link_down();
    far_restart();
    link_up(0x00u);
    run_ms(PROG_LINK_POLL_MS);
    CHECK_EQ(far.target, 1u);
    CHECK_EQ(far.pwm, 1u);
    CHECK(!servo_link_release_owed());
    CHECK_EQ(prog_link_drive(&prog).target, 1u);
    CHECK_EQ(prog_link_drive(&prog).pin, 13u);
    /* Nothing armed, so nothing rendered. */
    CHECK_EQ(panel.arms, 0u);
    CHECK_EQ(far.pulses, 0u);
}

/* With a standard surface bound the release centres it, as before. */
TEST_CASE(a_restart_with_a_standard_surface_gives_the_port_back_too)
{
    at_port_with_position(0x01u);
    link_down();
    far_restart();
    link_up(0x01u);
    run_ms(PROG_LINK_POLL_MS);
    CHECK_EQ(far.target, 1u);
    CHECK_EQ(far.pwm, 1u);
    CHECK(!servo_link_release_owed());
}

/*
 * ARMED when the IOmcu restarts: the target is written only once the bank
 * is SAFE, nothing arms it again, and the port renders no pulse until the
 * operator arms.
 */
TEST_CASE(a_restart_while_armed_writes_nothing_until_safe_and_arms_nothing)
{
    at_port_with_position(0x00u);
    panel.armed = true;
    far.armed   = true;
    run_ms(50u);
    const unsigned before = far.pulses;
    CHECK(before > 0u);
    /* The IOmcu restarts; the panel has not seen the loss yet and still
     * counts the bank ARMED: no TARGET and no pulse bit is written. */
    far_restart();
    servo_link_lost();
    prog_link_lost(&prog);
    link_up(0x00u);
    const unsigned writes = far.target_writes;
    run_ms(PROG_LINK_POLL_MS + 10u);
    CHECK_EQ(far.target_writes, writes);
    CHECK_EQ(far.target, 0u);
    CHECK_EQ(far.pwm, 0u);
    CHECK_EQ(far.pulses, before);
    /* The panel's STOP disarms it: SAFE.  The target comes back within a
     * read cadence, with no arm asked and no pulse rendered. */
    link_down();
    link_up(0x00u);
    run_ms(PROG_LINK_POLL_MS);
    CHECK_EQ(far.target, 1u);
    CHECK_EQ(far.pwm, 1u);
    CHECK_EQ(panel.arms, 0u);
    CHECK_EQ(far.pulses, before);
    /* The operator arms: the port renders from its centre, not the 1700us
     * written before the restart. */
    servo_cmd_t arm;
    memset(&arm, 0, sizeof(arm));
    arm.kind = SERVO_CMD_ARM;
    servo_link_apply(arm, true, panel.stops);
    CHECK_EQ(panel.arms, 1u);
    CHECK_EQ(far.cmd, CMD_CENTRE);
}

/*
 * An ask waiting when the IOmcu restarts is for the IOmcu it was made to:
 * it is lost, not written, and the target still comes back.
 */
TEST_CASE(a_restart_during_an_ask_loses_the_ask_and_gives_the_port_back)
{
    at_port_with_position(0x00u);
    CHECK(prog_link_ask(&prog, 0u, PROG_LINK_ASK_PIN, 14u));
    link_down();
    far_restart();
    link_up(0x00u);
    run_ms(PROG_LINK_POLL_MS);
    CHECK_EQ(prog_link_ask_state(&prog, NULL, NULL, NULL), PROG_LINK_ASK_LOST);
    CHECK_EQ(far.pin_writes, 0u);
    CHECK_EQ(far.target, 1u);
    CHECK_EQ(far.pwm, 1u);
}

/*
 * The link lost without a restart: the IOmcu still holds the target and
 * the pulse.  No TARGET is written, and the position held survives the
 * loss and is said again to the port (servo_cmd_survives_link_loss()).
 */
TEST_CASE(a_link_lost_without_a_restart_keeps_the_target_and_the_position)
{
    at_port_with_position(0x00u);
    const unsigned writes = far.target_writes;
    link_down();
    far.cmd = 0u;
    link_up(0x00u);
    run_ms(PROG_LINK_POLL_MS);
    CHECK_EQ(far.target_writes, writes);
    CHECK_EQ(far.target, 1u);
    CHECK_EQ(far.pwm, 1u);
    CHECK_EQ(far.cmd, 700u);
    /* Let go, the release goes to the port the IOmcu still names. */
    servo_link_let_go();
    run_ms(10u);
    CHECK_EQ(far.cmd, CMD_CENTRE);
    CHECK(!servo_link_release_owed());
}

/*
 * Two restarts in a row: the second before the first had its target back,
 * the third after it.  Each ends with the port the target within a read.
 */
TEST_CASE(two_restarts_in_a_row_each_give_the_port_back)
{
    at_port_with_position(0x00u);
    link_down();
    far_restart();
    link_up(0x00u);
    pass();
    link_down();
    far_restart();
    link_up(0x00u);
    run_ms(PROG_LINK_POLL_MS);
    CHECK_EQ(far.target, 1u);
    CHECK_EQ(far.pwm, 1u);
    link_down();
    far_restart();
    link_up(0x00u);
    run_ms(PROG_LINK_POLL_MS);
    CHECK_EQ(far.target, 1u);
    CHECK_EQ(far.pwm, 1u);
    CHECK(!servo_link_release_owed());
}

/*
 * A restarted IOmcu that does not answer the release keeps the debt and
 * the target waits; it comes back within a read of the far end answering.
 */
TEST_CASE(a_silent_far_end_after_a_restart_holds_the_target_back)
{
    at_port_with_position(0x00u);
    link_down();
    far_restart();
    link_up(0x00u);
    far.quiet = true;
    run_ms(PROG_LINK_POLL_MS);
    CHECK_EQ(far.target, 0u);
    far.quiet = false;
    run_ms(PROG_LINK_POLL_MS);
    CHECK_EQ(far.target, 1u);
    CHECK_EQ(far.pwm, 1u);
}

/*
 * A restart the panel does not see as a link loss, the link answering
 * again before a poll times out: the next read shows TARGET 0, and the
 * port comes back within that read and the writes after it.
 */
TEST_CASE(a_restart_the_panel_does_not_see_gives_the_port_back_at_the_next_read)
{
    at_port_with_position(0x00u);
    far_restart();
    run_ms(PROG_LINK_POLL_MS + 20u);
    CHECK_EQ(far.target, 1u);
    CHECK_EQ(far.pwm, 1u);
    CHECK(!servo_link_release_owed());
    CHECK_EQ(far.pulses, 0u);
}

/**
 * @brief A panel or an IOmcu just started, no position written since, port 0
 *        on @p func and the screen wanting @p want.
 *
 * @param func The port's function.
 * @param want The port the screen wants.
 */
static void started_with(link_pg_func_t func, uint8_t want)
{
    memset(&far, 0, sizeof(far));
    memset(&panel, 0, sizeof(panel));
    far.func0 = (uint16_t)func;
    s_want = want;
    s_now = 1000u;
    link_host_init(&host, s_now);
    prog_link_init(&prog);
    servo_link_init(&k_ctx);
    link_up(0x00u);
    run_ms(100u);
}

/*
 * The port left on DSHOT300 over a start: it is not bound, so the screen's
 * want gives no TARGET change, and the screen's taps send nothing.  Only the
 * ask has anything to write, and the sweep's state is settled for it.
 */
TEST_CASE(a_port_left_on_dshot_takes_its_ask_after_a_start)
{
    started_with(LINK_PG_FN_DSHOT300, 1u);
    CHECK_EQ(far.sweep_stops, 0u);
    CHECK(prog_link_ask(&prog, 0u, PROG_LINK_ASK_FUNC, LINK_PG_FN_SERVO_PWM));
    run_ms(100u);
    CHECK_EQ(far.func_writes, 1u);
    CHECK_EQ(far.func0, (uint16_t)LINK_PG_FN_SERVO_PWM);
    CHECK_EQ(far.sweep_stops, 1u);
    CHECK_EQ(prog_link_ask_state(&prog, NULL, NULL, NULL), PROG_LINK_ASK_DONE);
    CHECK_EQ(prog_link_ask_wait(&prog), PROG_LINK_WAIT_NONE);
    /* The page read again, the port bound: it is the target. */
    run_ms(PROG_LINK_POLL_MS + 20u);
    CHECK_EQ(far.target, 1u);
}

/* The same with the SERVO screen wanting nothing, on any function. */
TEST_CASE(an_ask_with_no_target_wanted_is_taken_after_a_start)
{
    started_with(LINK_PG_FN_SERVO_PWM, 0u);
    CHECK(prog_link_ask(&prog, 0u, PROG_LINK_ASK_FUNC, LINK_PG_FN_DSHOT300));
    run_ms(100u);
    CHECK_EQ(far.func0, (uint16_t)LINK_PG_FN_DSHOT300);
    CHECK_EQ(prog_link_ask_state(&prog, NULL, NULL, NULL), PROG_LINK_ASK_DONE);
}

/* An IOmcu restart with the ask waiting ends it as lost, and the next one lands. */
TEST_CASE(an_ask_after_an_iomcu_restart_is_taken)
{
    started_with(LINK_PG_FN_DSHOT300, 1u);
    link_down();
    far_restart();
    link_up(0x00u);
    run_ms(100u);
    CHECK(prog_link_ask(&prog, 0u, PROG_LINK_ASK_FUNC, LINK_PG_FN_SERVO_PWM));
    run_ms(100u);
    CHECK_EQ(far.func0, (uint16_t)LINK_PG_FN_SERVO_PWM);
}

/*
 * Whatever holds an ask is named, and each hold has its end: no state leaves an
 * ask waiting with no holder.
 */
TEST_CASE(an_ask_held_names_what_holds_it_and_lands_when_it_ends)
{
    started_with(LINK_PG_FN_DSHOT300, 1u);
    CHECK(prog_link_ask(&prog, 0u, PROG_LINK_ASK_FUNC, LINK_PG_FN_NONE));

    panel.armed = true;
    run_ms(50u);
    CHECK_EQ(prog_link_ask_wait(&prog), PROG_LINK_WAIT_SAFE);
    panel.armed = false;

    static const prog_link_wait_t k_holders[] = {
        PROG_LINK_WAIT_KST, PROG_LINK_WAIT_HITEC, PROG_LINK_WAIT_ESC,
    };
    for (size_t k = 0u; k < sizeof(k_holders) / sizeof(k_holders[0]); ++k) {
        prog_link_hold_by(&prog, k_holders[k]);
        run_ms(50u);
        CHECK_EQ(prog_link_ask_wait(&prog), k_holders[k]);
        CHECK_EQ(far.func_writes, 0u);
    }
    prog_link_hold_by(&prog, PROG_LINK_WAIT_NONE);

    prog_link_stale(&prog);
    CHECK_EQ(prog_link_ask_wait(&prog), PROG_LINK_WAIT_READ);

    run_ms(100u);
    CHECK_EQ(far.func0, (uint16_t)LINK_PG_FN_NONE);
    CHECK_EQ(prog_link_ask_state(&prog, NULL, NULL, NULL), PROG_LINK_ASK_DONE);
}

/* A position held waits as a test does; the screen letting go ends it. */
TEST_CASE(an_ask_on_another_port_waits_for_a_position_held)
{
    at_port_with_position(0x00u);
    run_ms(50u);
    CHECK(prog_link_ask(&prog, 1u, PROG_LINK_ASK_FUNC, LINK_PG_FN_NONE));
    run_ms(100u);
    CHECK_EQ(prog_link_ask_wait(&prog), PROG_LINK_WAIT_TEST);
    CHECK_EQ(prog_link_ask_state(&prog, NULL, NULL, NULL),
             PROG_LINK_ASK_WAITING);
    servo_cmd_t c;
    memset(&c, 0, sizeof(c));
    c.kind = SERVO_CMD_NONE;
    servo_link_apply(c, true, panel.stops);
    run_ms(100u);
    CHECK_EQ(prog_link_ask_state(&prog, NULL, NULL, NULL), PROG_LINK_ASK_DONE);
}

/*
 * A programming session that took the port: the IOmcu lets the TARGET go
 * while the port's pin is not the PWM generator's.  Given back, the port
 * is the target again within a read, with the position written before the
 * session owing nothing.
 */
TEST_CASE(a_session_that_took_the_target_gives_the_port_back)
{
    at_port_with_position(0x00u);
    prog_link_hold(&prog, true);
    far.target = 0u;
    far.pwm    = 0u;
    run_ms(PROG_LINK_POLL_MS);
    CHECK_EQ(far.target, 0u);
    prog_link_hold(&prog, false);
    run_ms(PROG_LINK_POLL_MS + 20u);
    CHECK_EQ(far.target, 1u);
    CHECK_EQ(far.pwm, 1u);
    CHECK(!servo_link_release_owed());
}

/*
 * The station's sequence: the auto test drives the port while ARMED, the
 * IOmcu drops (it stops answering and starts again), the test ends first
 * and its release goes unanswered, and the panel sees the link lost 760ms
 * later.  After the link comes back the port is the target again within a
 * read, with no arm asked and no pulse rendered.
 */
TEST_CASE(a_restart_during_a_running_test_gives_the_port_back)
{
    at_port_with_position(0x00u);
    panel.armed = true;
    far.armed   = true;
    servo_cmd_t step;
    memset(&step, 0, sizeof(step));
    step.kind     = SERVO_CMD_POSITION;
    step.value_us = 1200u;
    step.min_us   = 1000u;
    step.max_us   = 2000u;
    for (int k = 0; k < 5; ++k) {
        servo_link_apply(step, true, panel.stops);
        run_ms(100u);
    }
    const unsigned before = far.pulses;
    /* The IOmcu drops: silent, and started again when it answers. */
    far.quiet = true;
    far_restart();
    servo_cmd_t rel;
    memset(&rel, 0, sizeof(rel));
    rel.kind = SERVO_CMD_RELEASE;
    servo_link_apply(rel, true, panel.stops);
    CHECK(servo_link_release_owed());
    run_ms(760u);
    link_down();
    far.quiet = false;
    link_up(0x00u);
    run_ms(PROG_LINK_POLL_MS);
    CHECK_EQ(far.target, 1u);
    CHECK_EQ(far.pwm, 1u);
    CHECK(!servo_link_release_owed());
    CHECK_EQ(prog_link_drive(&prog).target, 1u);
    CHECK_EQ(panel.arms, 0u);
    CHECK_EQ(far.pulses, before);
}

int main(void)
{
    RUN(the_port_is_the_target_with_a_position_before_any_restart);
    RUN(a_restart_while_safe_gives_the_port_back_within_a_read);
    RUN(a_restart_with_a_standard_surface_gives_the_port_back_too);
    RUN(a_restart_while_armed_writes_nothing_until_safe_and_arms_nothing);
    RUN(a_restart_during_an_ask_loses_the_ask_and_gives_the_port_back);
    RUN(a_link_lost_without_a_restart_keeps_the_target_and_the_position);
    RUN(two_restarts_in_a_row_each_give_the_port_back);
    RUN(a_silent_far_end_after_a_restart_holds_the_target_back);
    RUN(a_restart_the_panel_does_not_see_gives_the_port_back_at_the_next_read);
    RUN(a_session_that_took_the_target_gives_the_port_back);
    RUN(a_port_left_on_dshot_takes_its_ask_after_a_start);
    RUN(an_ask_with_no_target_wanted_is_taken_after_a_start);
    RUN(an_ask_after_an_iomcu_restart_is_taken);
    RUN(an_ask_held_names_what_holds_it_and_lands_when_it_ends);
    RUN(an_ask_on_another_port_waits_for_a_position_held);
    RUN(a_restart_during_a_running_test_gives_the_port_back);
    return test_summary("servo_link_restart");
}
