/*
 * The panel's half of the SERVO screen on the link (shared/outputs/
 * servo_link.c), against a far end that answers each frame, refuses it or
 * stays silent, over a link_port_t.
 *
 * The far end is a script, not an IOmcu: every frame is logged with its
 * page, offset and registers, and acknowledged unless a case says otherwise.
 * The clock is the port's and moves only where a case moves it, or by
 * bus.exchange_ms an exchange.  The panel's callbacks are fakes that record
 * what was asked of them.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_can.h"
#include "link_host.h"
#include "link_pages.h"
#include "link_port.h"
#include "outputs.h"
#include "prog_link.h"
#include "servo_link.h"

/* ------------------------------------------------------------ the far end */

#define LOG_MAX 64u

typedef struct {
    uint8_t  page;
    uint8_t  offset;
    uint8_t  count;
    uint16_t regs[LINK_CAN_REGS_PER_FRAME];
} frame_t;

static struct {
    frame_t  log[LOG_MAX];
    unsigned n;
    uint32_t exchange_ms;   /* the clock an exchange takes                 */
    bool     quiet;         /* nothing answers                             */
    bool     quiet_rate;    /* nothing answers a FRAME_HZ write            */
    bool     quiet_channels;/* nothing answers a CHANNELS write            */
    bool     nack_rate;     /* a FRAME_HZ write of a rate is refused       */
    bool     nack_resume;   /* a SWEEP write of LINK_SV_RESUME is refused  */
    /* Called after the frame numbered @p n (from 1) is logged. */
    void (*after)(unsigned n);
} bus;

static uint32_t s_now;

static uint32_t port_now(void *ctx)
{
    (void)ctx;
    return s_now;
}

static bool is_rate(const link_msg_t *req)
{
    return req->page == LINK_PAGE_SERVO && req->offset == LINK_SV_FRAME_HZ;
}

static bool port_exchange(void *ctx, link_host_t *h, const link_msg_t *req,
                          link_msg_t *reply)
{
    (void)ctx;
    frame_t *f = &bus.log[bus.n < LOG_MAX ? bus.n : LOG_MAX - 1u];
    memset(f, 0, sizeof(*f));
    f->page   = req->page;
    f->offset = req->offset;
    f->count  = req->count;
    memcpy(f->regs, req->regs,
           (size_t)(req->count < LINK_CAN_REGS_PER_FRAME
                        ? req->count : LINK_CAN_REGS_PER_FRAME)
               * sizeof(req->regs[0]));
    ++bus.n;
    s_now += bus.exchange_ms;
    if (bus.after != NULL) {
        bus.after(bus.n);
    }
    if (bus.quiet || (bus.quiet_rate && is_rate(req))
        || (bus.quiet_channels && req->page == LINK_PAGE_CHANNELS)) {
        link_host_abandon(h);
        return false;
    }
    link_msg_t part;
    memset(&part, 0, sizeof(part));
    part.page   = req->page;
    part.offset = req->offset;
    const bool refuse =
        (bus.nack_rate && is_rate(req) && req->regs[0] != 0u)
        || (bus.nack_resume && req->page == LINK_PAGE_SERVO
            && req->offset == LINK_SV_SWEEP && req->count == 1u
            && req->regs[0] == LINK_SV_RESUME);
    if (refuse) {
        part.op      = LINK_OP_NACK;
        part.count   = 1u;
        part.regs[0] = 1u;
    } else {
        part.op    = LINK_OP_ACK;
        part.count = req->count;
        memcpy(part.regs, req->regs,
               (size_t)req->count * sizeof(req->regs[0]));
    }
    return link_host_accept(h, &part, s_now, reply);
}

/* Frames to @p page at @p offset since the log was cleared. */
static unsigned frames_to(uint8_t page, uint8_t offset)
{
    unsigned k = 0u;
    for (unsigned i = 0; i < bus.n && i < LOG_MAX; ++i) {
        if (bus.log[i].page == page && bus.log[i].offset == offset) {
            ++k;
        }
    }
    return k;
}

/* Whether a one-register SWEEP write of @p value was sent. */
static bool sweep_reg_sent(uint16_t value)
{
    for (unsigned i = 0; i < bus.n && i < LOG_MAX; ++i) {
        const frame_t *f = &bus.log[i];
        if (f->page == LINK_PAGE_SERVO && f->offset == LINK_SV_SWEEP
            && f->count == 1u && f->regs[0] == value) {
            return true;
        }
    }
    return false;
}

static const frame_t *last_to(uint8_t page)
{
    const frame_t *found = NULL;
    for (unsigned i = 0; i < bus.n && i < LOG_MAX; ++i) {
        if (bus.log[i].page == page) {
            found = &bus.log[i];
        }
    }
    return found;
}

static void clear_log(void)
{
    bus.n = 0u;
}

/* -------------------------------------------------------------- the panel */

static struct {
    bool     armed;
    bool     stopped;
    uint32_t stops;
    unsigned alerts;
    servo_link_alert_t alert;
    unsigned disarms;
    bool     disarm_link;
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
    return panel.stopped;
}

static void fake_alert(void *ctx, servo_link_alert_t a)
{
    (void)ctx;
    ++panel.alerts;
    panel.alert = a;
}

static void fake_disarm(void *ctx, bool link_up)
{
    (void)ctx;
    ++panel.disarms;
    panel.disarm_link = link_up;
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

/* A fresh module and far end at @p now; no link-up yet. */
static void reset_at(uint32_t now)
{
    memset(&bus, 0, sizeof(bus));
    memset(&panel, 0, sizeof(panel));
    s_now = now;
    link_host_init(&host, s_now);
    prog_link_init(&prog);
    servo_link_init(&k_ctx);
}

/* Up at protocol minor @p minor, surfaces on @p channels, log cleared. */
static void up(uint16_t minor, uint8_t channels)
{
    reset_at(1000u);
    servo_link_came_up(minor);
    servo_link_binding(channels, true);
    clear_log();
}

static servo_cmd_t pos(uint16_t us)
{
    servo_cmd_t c;
    memset(&c, 0, sizeof(c));
    c.kind     = SERVO_CMD_POSITION;
    c.value_us = us;
    c.min_us   = 1000u;
    c.max_us   = 2000u;
    return c;
}

static servo_cmd_t sweep(uint16_t seq, bool resume)
{
    servo_cmd_t c = pos(1500u);
    c.kind           = SERVO_CMD_SWEEP;
    c.sweep_kind     = 1u;
    c.sweep_mhz      = 500u;
    c.sweep_span     = 300u;
    c.sweep_dwell_ms = 0u;
    c.start_seq      = seq;
    c.resume         = resume;
    return c;
}

static servo_cmd_t hold(uint16_t pause)
{
    servo_cmd_t c;
    memset(&c, 0, sizeof(c));
    c.kind       = SERVO_CMD_HOLD;
    c.ends_sweep = true;
    c.pause_seq  = pause;
    return c;
}

static servo_cmd_t bare(servo_cmd_kind_t kind)
{
    servo_cmd_t c;
    memset(&c, 0, sizeof(c));
    c.kind = kind;
    return c;
}

/* A sweep started and then held at pause @p pause; log cleared. */
static void sweep_then_hold(uint16_t pause)
{
    servo_link_apply(sweep(1u, false), true, 0u);
    (void)servo_link_take_sweep_start();
    servo_link_apply(hold(pause), true, 0u);
    clear_log();
}

/* -------------------------------------------------- the sweep version gate */

TEST_CASE(a_coprocessor_before_4_2_has_no_sweep_and_a_hold_sends_nothing)
{
    up(1u, 0x01u);
    CHECK(!servo_link_sweep_able());
    servo_link_apply(hold(1u), true, 0u);
    CHECK_EQ(bus.n, 0u);
    CHECK(!servo_link_release_owed());

    up(2u, 0x01u);
    CHECK(servo_link_sweep_able());
}

TEST_CASE(the_first_command_after_link_up_writes_a_stop_first)
{
    up(2u, 0x01u);
    servo_link_apply(pos(1500u), true, 0u);
    CHECK(bus.n >= 1u);
    CHECK_EQ(bus.log[0].page, LINK_PAGE_SERVO);
    CHECK_EQ(bus.log[0].offset, LINK_SV_SWEEP);
    CHECK_EQ(bus.log[0].regs[0], 0u);

    /* The state is known after it: the next one writes no stop. */
    clear_log();
    s_now += 100u;
    servo_link_service(true);
    CHECK(!sweep_reg_sent(0u));
    CHECK_EQ(frames_to(LINK_PAGE_CHANNELS, 0u), 1u);
}

TEST_CASE(a_resume_before_4_6_starts_the_curve_over_and_says_so)
{
    up(5u, 0x01u);
    sweep_then_hold(7u);
    uint16_t seq = 0u;
    uint32_t kept = 0u;
    CHECK(servo_link_take_sweep_held(&seq, &kept));
    CHECK_EQ(seq, 7u);

    servo_link_apply(sweep(2u, true), true, 0u);
    CHECK(!sweep_reg_sent(LINK_SV_RESUME));
    CHECK(sweep_reg_sent(0u));
    CHECK_EQ(frames_to(LINK_PAGE_SERVO, LINK_SV_SWEEP_MOVES), 1u);
    CHECK_EQ(panel.alerts, 1u);
    CHECK_EQ(panel.alert, SERVO_LINK_ALERT_RESUME_OLD);
    CHECK(servo_link_take_sweep_start());
    const servo_link_start_t st = servo_link_sweep_start();
    CHECK_EQ(st.start_seq, 2u);
    CHECK_EQ(st.from, SERVO_SWEEP_FROM_REST);
}

TEST_CASE(a_resume_from_4_6_is_one_register_and_carries_the_phase_on)
{
    up(6u, 0x01u);
    bus.exchange_ms = 10u;
    servo_link_apply(sweep(1u, false), true, 0u);
    (void)servo_link_take_sweep_start();
    const uint32_t started = servo_link_sweep_start().start_ms;
    s_now += 300u;
    servo_link_apply(hold(7u), true, 0u);
    uint16_t seq = 0u;
    uint32_t kept = 0u;
    CHECK(servo_link_take_sweep_held(&seq, &kept));
    clear_log();

    s_now += 2000u;
    servo_link_apply(sweep(2u, true), true, 0u);
    /* The range, then RESUME alone. */
    CHECK_EQ(bus.n, 2u);
    CHECK_EQ(bus.log[0].page, LINK_PAGE_CHAN_CFG);
    CHECK(sweep_reg_sent(LINK_SV_RESUME));
    CHECK_EQ(frames_to(LINK_PAGE_SERVO, LINK_SV_SWEEP_MOVES), 0u);
    CHECK_EQ(panel.alerts, 0u);
    CHECK(servo_link_take_sweep_start());
    const servo_link_start_t st = servo_link_sweep_start();
    CHECK_EQ(st.start_seq, 2u);
    CHECK_EQ(st.from, SERVO_SWEEP_RESUMED);
    /* Phase 0 moved on by the hold: as old as the phase it kept. */
    CHECK_EQ(st.frozen_ms - st.start_ms, kept);
    CHECK(st.start_ms != started);
}

TEST_CASE(a_refused_resume_starts_the_curve_over_and_says_so)
{
    up(6u, 0x01u);
    sweep_then_hold(7u);
    bus.nack_resume = true;
    servo_link_apply(sweep(2u, true), true, 0u);
    CHECK(sweep_reg_sent(LINK_SV_RESUME));
    CHECK(sweep_reg_sent(0u));
    CHECK_EQ(frames_to(LINK_PAGE_SERVO, LINK_SV_SWEEP_MOVES), 1u);
    CHECK_EQ(panel.alerts, 1u);
    CHECK_EQ(panel.alert, SERVO_LINK_ALERT_RESUME_REFUSED);
    CHECK(servo_link_take_sweep_start());
    CHECK_EQ(servo_link_sweep_start().from, SERVO_SWEEP_FROM_REST);
}

/* ------------------------------------- the refusal while a release is owed */

TEST_CASE(a_sweep_waits_while_a_release_is_owed_and_cannot_be_paid)
{
    reset_at(1000u);
    servo_link_came_up(6u);   /* no binding read: unknown, not empty */
    clear_log();
    servo_link_ask_release();
    CHECK(servo_link_take_release());
    CHECK(!servo_link_take_release());
    CHECK(servo_link_release_owed());

    servo_link_apply(sweep(1u, false), true, 0u);
    /* The stop link-up owes, and no range, no curve, no centre. */
    CHECK_EQ(frames_to(LINK_PAGE_CHAN_CFG, 0u), 0u);
    CHECK_EQ(frames_to(LINK_PAGE_CHANNELS, 0u), 0u);
    CHECK_EQ(frames_to(LINK_PAGE_SERVO, LINK_SV_SWEEP_MOVES), 0u);
    CHECK(servo_link_release_owed());
    CHECK(!servo_link_take_sweep_start());

    /* A binding read: the refresh pays the release, then the sweep goes. */
    servo_link_binding(0x01u, true);
    clear_log();
    servo_link_service(true);
    CHECK(!servo_link_release_owed());
    const frame_t *centre = last_to(LINK_PAGE_CHANNELS);
    CHECK(centre != NULL);
    if (centre != NULL) {
        CHECK_EQ(centre->regs[0], LINK_CH_SPAN / 2u);
    }
    CHECK(!servo_link_take_sweep_start());
    s_now += 100u;
    servo_link_service(true);
    CHECK(servo_link_take_sweep_start());
    CHECK_EQ(servo_link_sweep_start().from, SERVO_SWEEP_FROM_REST);
}

TEST_CASE(a_sweep_waits_for_an_unpaid_release_over_a_known_binding)
{
    up(6u, 0x01u);
    servo_link_apply(pos(1800u), true, 0u);
    servo_link_let_go();
    clear_log();
    bus.quiet_channels = true;   /* the centre write goes unanswered */
    servo_link_apply(sweep(1u, false), true, 0u);
    CHECK(servo_link_release_owed());
    CHECK(!servo_link_take_sweep_start());
    CHECK_EQ(frames_to(LINK_PAGE_SERVO, LINK_SV_SWEEP_MOVES), 0u);
    CHECK_EQ(frames_to(LINK_PAGE_SERVO, LINK_SV_SWEEP), 0u);

    /* The far end answers again: the refresh pays the release first. */
    bus.quiet_channels = false;
    clear_log();
    s_now += 100u;
    servo_link_service(true);
    CHECK(!servo_link_release_owed());
    const frame_t *centre = last_to(LINK_PAGE_CHANNELS);
    CHECK(centre != NULL);
    if (centre != NULL) {
        CHECK_EQ(centre->regs[0], LINK_CH_SPAN / 2u);
    }
}

TEST_CASE(an_arm_onto_an_unknown_binding_leaves_the_release_owed)
{
    reset_at(1000u);
    servo_link_came_up(6u);
    clear_log();
    servo_link_apply(bare(SERVO_CMD_ARM), true, 0u);
    /* The arm is asked for; the policy refuses it on the debt. */
    CHECK_EQ(panel.arms, 1u);
    CHECK(servo_link_release_owed());
    CHECK_EQ(frames_to(LINK_PAGE_CHANNELS, 0u), 0u);
}

TEST_CASE(a_release_over_no_surface_is_paid_at_once)
{
    up(6u, 0x00u);
    servo_link_apply(bare(SERVO_CMD_RELEASE), true, 0u);
    CHECK(!servo_link_release_owed());
    CHECK_EQ(frames_to(LINK_PAGE_CHANNELS, 0u), 0u);
}

/* --------------------------------------------------------- the rate retry */

TEST_CASE(a_link_up_puts_each_slot_back_at_its_own_rate)
{
    reset_at(1000u);
    servo_link_came_up(6u);
    CHECK_EQ(frames_to(LINK_PAGE_SERVO, LINK_SV_FRAME_HZ), 1u);
    CHECK_EQ(bus.log[0].regs[0], 0u);
    CHECK_EQ(servo_link_hz_sent(), 0u);
    CHECK_EQ(servo_link_rate_shown(), (unsigned)SERVO_RATE_UNSENT << 16);

    reset_at(1000u);
    servo_link_came_up(0u);   /* 4.0: no SERVO page */
    CHECK_EQ(bus.n, 0u);
    CHECK_EQ(servo_link_hz_sent(), 0u);
    CHECK_EQ(servo_link_rate_shown(), (unsigned)SERVO_RATE_UNSUPPORTED << 16);
}

TEST_CASE(a_refused_rate_is_not_asked_again_until_a_reset)
{
    up(6u, 0x01u);
    bus.nack_rate = true;
    servo_cmd_t heli = pos(1500u);
    heli.frame_hz = 333u;
    servo_link_apply(heli, true, 0u);
    /* Faster than 50Hz: the pulse first, then the rate. */
    CHECK_EQ(frames_to(LINK_PAGE_CHANNELS, 0u), 1u);
    CHECK_EQ(frames_to(LINK_PAGE_SERVO, LINK_SV_FRAME_HZ), 1u);
    CHECK(!servo_link_release_owed());
    CHECK_EQ(servo_link_rate_shown(),
             ((unsigned)SERVO_RATE_REFUSED << 16) | 333u);
    CHECK_EQ(servo_link_hz_sent(), 0u);

    s_now += 100u;
    servo_link_service(true);
    CHECK_EQ(frames_to(LINK_PAGE_CHANNELS, 0u), 2u);
    CHECK_EQ(frames_to(LINK_PAGE_SERVO, LINK_SV_FRAME_HZ), 1u);

    /* A reset forgets the refusal, and the next hold asks again. */
    servo_link_rate_reset_note(true, true);
    bus.nack_rate = false;
    s_now += 100u;
    servo_link_service(true);
    CHECK_EQ(frames_to(LINK_PAGE_SERVO, LINK_SV_FRAME_HZ), 2u);
    CHECK_EQ(servo_link_hz_sent(), 333u);
    CHECK_EQ(servo_link_rate_shown(),
             ((unsigned)SERVO_RATE_IN_FORCE_SURFACES << 16) | 333u);
}

TEST_CASE(an_unanswered_rate_reset_is_tried_again_until_it_lands)
{
    reset_at(1000u);
    bus.quiet_rate = true;
    servo_link_came_up(6u);
    CHECK_EQ(servo_link_hz_sent(), SERVO_HZ_UNKNOWN);
    CHECK(!servo_link_rate_settled());
    CHECK_EQ(frames_to(LINK_PAGE_SERVO, LINK_SV_FRAME_HZ), 2u);

    /* A position waits for a rate not known: it counts as fast. */
    servo_link_binding(0x01u, true);
    servo_cmd_t std50 = pos(1500u);
    std50.frame_hz = 50u;
    servo_link_apply(std50, true, 0u);
    CHECK_EQ(frames_to(LINK_PAGE_CHANNELS, 0u), 0u);

    bus.quiet_rate = false;
    CHECK(servo_link_rate_settled());
    CHECK_EQ(servo_link_hz_sent(), 0u);
    const unsigned asked = frames_to(LINK_PAGE_SERVO, LINK_SV_FRAME_HZ);
    CHECK(servo_link_rate_settled());
    CHECK_EQ(frames_to(LINK_PAGE_SERVO, LINK_SV_FRAME_HZ), asked);
}

TEST_CASE(a_rate_not_known_is_settled_while_armed_before_the_hold)
{
    reset_at(1000u);
    bus.quiet_rate = true;
    servo_link_came_up(6u);
    servo_link_binding(0x01u, true);
    bus.quiet_rate = false;
    clear_log();
    /* Nothing owed, nothing held, not armed: nothing to settle. */
    servo_link_service(true);
    CHECK_EQ(bus.n, 0u);
    panel.armed = true;
    servo_link_service(true);
    CHECK_EQ(frames_to(LINK_PAGE_SERVO, LINK_SV_FRAME_HZ), 1u);
    CHECK_EQ(servo_link_hz_sent(), 0u);
    /* And nothing while the link is down. */
    reset_at(1000u);
    bus.quiet_rate = true;
    servo_link_came_up(6u);
    clear_log();
    panel.armed = true;
    servo_link_service(false);
    CHECK_EQ(bus.n, 0u);
}

TEST_CASE(a_target_change_the_page_reports_is_noted_for_the_rate)
{
    up(6u, 0x01u);
    prog.rate = (uint8_t)PROG_LINK_RATE_UNKNOWN;
    servo_link_prog_service();
    CHECK_EQ(bus.n, 0u);
    CHECK_EQ(servo_link_hz_sent(), SERVO_HZ_UNKNOWN);
    prog.rate = (uint8_t)PROG_LINK_RATE_RESET;
    servo_link_prog_service();
    CHECK_EQ(servo_link_hz_sent(), 0u);
}

/* ------------------------------------------------ limits, timeouts, aborts */

TEST_CASE(a_hold_unrepeated_for_more_than_500_ms_is_let_go)
{
    up(6u, 0x01u);
    sweep_then_hold(7u);
    const uint32_t held_at = s_now;

    s_now = held_at + OUT_DEFAULT_TIMEOUT_MS;   /* 500ms: still held */
    servo_link_apply(hold(7u), true, 0u);
    CHECK(sweep_reg_sent(LINK_SV_HOLD));
    uint16_t seq = 0u;
    CHECK(!servo_link_take_hold_lost(&seq));

    clear_log();
    s_now += OUT_DEFAULT_TIMEOUT_MS + 1u;       /* 501ms: let go */
    servo_link_service(true);                   /* the HOLD said again */
    CHECK_EQ(bus.n, 0u);
    CHECK(servo_link_take_hold_lost(&seq));
    CHECK_EQ(seq, 7u);
    CHECK(!servo_link_take_hold_lost(&seq));
    CHECK(servo_link_release_owed());

    /* A position derived from that pause is stale and not sent; the
     * first one from after it is. */
    servo_cmd_t stale = pos(1800u);
    stale.from_pause = 7u;
    servo_link_apply(stale, true, 0u);
    CHECK_EQ(bus.n, 0u);
    servo_link_apply(pos(1800u), true, 0u);
    CHECK_EQ(frames_to(LINK_PAGE_CHANNELS, 0u), 1u);
    CHECK(!servo_link_release_owed());
}

TEST_CASE(a_hold_asked_499_or_500_ms_after_the_last_is_held_and_owes_nothing)
{
    static const uint32_t gaps[] = { OUT_DEFAULT_TIMEOUT_MS - 1u,
                                     OUT_DEFAULT_TIMEOUT_MS };
    for (size_t i = 0; i < sizeof(gaps) / sizeof(gaps[0]); ++i) {
        up(6u, 0x01u);
        sweep_then_hold(7u);
        s_now += gaps[i];
        servo_link_apply(hold(7u), true, 0u);
        CHECK(sweep_reg_sent(LINK_SV_HOLD));
        uint16_t seq = 0u;
        CHECK(!servo_link_take_hold_lost(&seq));
        CHECK(!servo_link_release_owed());
    }
}

TEST_CASE(a_hold_asked_501_ms_after_the_last_owes_the_release_until_paid)
{
    up(6u, 0x01u);
    sweep_then_hold(7u);
    s_now += OUT_DEFAULT_TIMEOUT_MS + 1u;
    servo_link_apply(hold(7u), true, 0u);
    CHECK_EQ(bus.n, 0u);                        /* no HOLD, nothing paid */
    uint16_t seq = 0u;
    CHECK(servo_link_take_hold_lost(&seq));
    CHECK_EQ(seq, 7u);
    CHECK(servo_link_release_owed());

    /* The next refresh pays it: the range, then the centre. */
    servo_link_service(true);
    CHECK(!servo_link_release_owed());
    CHECK_EQ(frames_to(LINK_PAGE_CHAN_CFG, 0u), 1u);
    const frame_t *centre = last_to(LINK_PAGE_CHANNELS);
    CHECK(centre != NULL);
    if (centre != NULL) {
        CHECK_EQ(centre->regs[0], LINK_CH_SPAN / 2u);
    }
    clear_log();
    servo_link_service(true);
    CHECK_EQ(bus.n, 0u);                        /* paid once */
}

TEST_CASE(a_sweep_after_a_hold_lost_at_501_ms_starts_from_the_centre)
{
    up(6u, 0x01u);
    sweep_then_hold(7u);
    s_now += OUT_DEFAULT_TIMEOUT_MS + 1u;
    servo_link_apply(hold(7u), true, 0u);
    servo_link_apply(sweep(2u, false), true, 0u);
    /* The release first, then the curve. */
    unsigned centre_at = LOG_MAX;
    unsigned moves_at  = LOG_MAX;
    for (unsigned i = 0; i < bus.n && i < LOG_MAX; ++i) {
        if (bus.log[i].page == LINK_PAGE_CHANNELS && centre_at == LOG_MAX
            && bus.log[i].regs[0] == LINK_CH_SPAN / 2u) {
            centre_at = i;
        }
        if (bus.log[i].page == LINK_PAGE_SERVO
            && bus.log[i].offset == LINK_SV_SWEEP_MOVES) {
            moves_at = i;
        }
    }
    CHECK(centre_at < LOG_MAX);
    CHECK(moves_at < LOG_MAX);
    CHECK(centre_at < moves_at);
    CHECK(!servo_link_release_owed());
    CHECK(servo_link_take_sweep_start());
    CHECK_EQ(servo_link_sweep_start().from, SERVO_SWEEP_FROM_REST);
}

TEST_CASE(a_hold_lost_at_501_ms_sends_no_faster_rate)
{
    up(6u, 0x01u);
    sweep_then_hold(7u);
    s_now += OUT_DEFAULT_TIMEOUT_MS + 1u;
    servo_cmd_t fast = hold(7u);
    fast.frame_hz = 333u;                       /* faster than the 50Hz */
    servo_link_apply(fast, true, 0u);
    CHECK_EQ(frames_to(LINK_PAGE_SERVO, LINK_SV_FRAME_HZ), 0u);
    CHECK(servo_link_release_owed());
}

TEST_CASE(a_hold_answered_after_more_than_500_ms_ends_the_pause)
{
    up(6u, 0x01u);
    servo_link_apply(sweep(1u, false), true, 0u);
    bus.exchange_ms = OUT_DEFAULT_TIMEOUT_MS;   /* 500ms: taken */
    clear_log();
    servo_link_apply(hold(3u), true, 0u);
    uint16_t seq = 0u;
    uint32_t kept = 0u;
    CHECK(servo_link_take_sweep_held(&seq, &kept));
    CHECK(!servo_link_take_hold_lost(&seq));
    CHECK_EQ(bus.n, 1u);

    up(6u, 0x01u);
    servo_link_apply(sweep(1u, false), true, 0u);
    bus.exchange_ms = OUT_DEFAULT_TIMEOUT_MS + 1u;   /* 501ms: let go */
    clear_log();
    servo_link_apply(hold(3u), true, 0u);
    CHECK(sweep_reg_sent(LINK_SV_HOLD));
    CHECK(sweep_reg_sent(0u));     /* the live HOLD ended with a stop */
    CHECK(!servo_link_take_sweep_held(&seq, &kept));
    CHECK(servo_link_take_hold_lost(&seq));
    CHECK_EQ(seq, 3u);
    CHECK(servo_link_release_owed());
}

TEST_CASE(a_held_position_is_said_again_at_100_ms_and_not_before)
{
    static const uint32_t starts[] = { 1000u, 0xFFFFFFC0u };
    for (size_t i = 0; i < sizeof(starts) / sizeof(starts[0]); ++i) {
        up(1u, 0x01u);
        s_now = starts[i];   /* the second runs across the clock's wrap */
        servo_link_apply(pos(1500u), true, 0u);
        CHECK_EQ(frames_to(LINK_PAGE_CHANNELS, 0u), 1u);
        const uint32_t at = s_now;
        s_now = at + 50u;    /* across the wrap: next has wrapped, now not */
        servo_link_service(true);
        CHECK_EQ(frames_to(LINK_PAGE_CHANNELS, 0u), 1u);
        s_now = at + 99u;
        servo_link_service(true);
        CHECK_EQ(frames_to(LINK_PAGE_CHANNELS, 0u), 1u);
        s_now = at + 100u;
        servo_link_service(true);
        CHECK_EQ(frames_to(LINK_PAGE_CHANNELS, 0u), 2u);
        servo_link_service(true);
        CHECK_EQ(frames_to(LINK_PAGE_CHANNELS, 0u), 2u);
    }
}

static void stop_after_first(unsigned n)
{
    if (n == 1u) {
        panel.stopped = true;
    }
}

static void stop_after_second(unsigned n)
{
    if (n == 2u) {
        panel.stopped = true;
    }
}

static void release_after_first(unsigned n)
{
    if (n == 1u) {
        servo_link_ask_release();
    }
}

static void hold_after_first(unsigned n)
{
    if (n == 1u) {
        servo_link_ask_hold();
    }
}

TEST_CASE(a_stop_during_a_position_ends_it_between_frames)
{
    /* Channels 0 and 2: two runs, each a range and a pulse. */
    up(1u, 0x05u);
    bus.after = stop_after_first;
    servo_link_apply(pos(1500u), true, 0u);
    CHECK_EQ(bus.n, 1u);
    CHECK_EQ(bus.log[0].page, LINK_PAGE_CHAN_CFG);

    up(1u, 0x05u);
    bus.after = stop_after_second;
    servo_link_apply(pos(1500u), true, 0u);
    CHECK_EQ(bus.n, 2u);
    CHECK_EQ(bus.log[1].page, LINK_PAGE_CHANNELS);

    up(1u, 0x05u);
    servo_link_apply(pos(1500u), true, 0u);
    CHECK_EQ(bus.n, 4u);
    CHECK_EQ(bus.log[2].offset, 2u * LINK_CC_STRIDE);
    CHECK_EQ(bus.log[3].offset, 2u);
}

TEST_CASE(a_release_or_hold_asked_during_a_sweep_write_ends_it)
{
    up(6u, 0x01u);
    servo_link_apply(pos(1500u), true, 0u);   /* the stop link-up owes */
    clear_log();
    bus.after = release_after_first;
    servo_link_apply(sweep(1u, false), true, 0u);
    CHECK_EQ(bus.n, 1u);
    CHECK_EQ(frames_to(LINK_PAGE_SERVO, LINK_SV_SWEEP_MOVES), 0u);
    CHECK(!servo_link_take_sweep_start());
    CHECK(servo_link_take_release());

    up(6u, 0x01u);
    servo_link_apply(pos(1500u), true, 0u);
    clear_log();
    bus.after = hold_after_first;
    servo_link_apply(sweep(1u, false), true, 0u);
    CHECK_EQ(bus.n, 1u);
    CHECK(!servo_link_take_sweep_start());
    /* The next command clears the HOLD asked for. */
    bus.after = NULL;
    clear_log();
    servo_link_apply(sweep(2u, false), true, 0u);
    CHECK(servo_link_take_sweep_start());
}

TEST_CASE(an_arm_after_a_stop_in_between_is_not_asked_for)
{
    up(6u, 0x01u);
    panel.stops = 4u;
    servo_link_apply(bare(SERVO_CMD_ARM), true, 3u);
    CHECK_EQ(panel.arms, 0u);
    /* The surfaces are centred all the same. */
    CHECK_EQ(frames_to(LINK_PAGE_CHANNELS, 0u), 1u);
    CHECK(!servo_link_release_owed());

    servo_link_apply(bare(SERVO_CMD_ARM), true, 4u);
    CHECK_EQ(panel.arms, 1u);
}

TEST_CASE(a_disarm_is_acted_on_with_or_without_the_link)
{
    up(6u, 0x01u);
    servo_link_apply(bare(SERVO_CMD_DISARM), false, 0u);
    CHECK_EQ(panel.disarms, 1u);
    CHECK(!panel.disarm_link);
    CHECK_EQ(bus.n, 0u);
    CHECK(servo_link_release_owed());

    servo_link_apply(bare(SERVO_CMD_DISARM), true, 0u);
    CHECK_EQ(panel.disarms, 2u);
    CHECK(panel.disarm_link);
    CHECK(!servo_link_release_owed());
    CHECK_EQ(frames_to(LINK_PAGE_CHANNELS, 0u), 1u);
}

TEST_CASE(a_sweep_asked_with_the_link_down_is_dropped_and_owes_a_release)
{
    up(1u, 0x01u);
    servo_link_apply(pos(1500u), true, 0u);
    clear_log();
    servo_link_apply(sweep(1u, false), false, 0u);
    CHECK_EQ(bus.n, 0u);
    CHECK(servo_link_release_owed());
    /* The release is paid; neither the sweep nor the position is said
     * again. */
    servo_link_service(true);
    CHECK(!servo_link_release_owed());
    s_now += 1000u;
    clear_log();
    servo_link_service(true);
    CHECK_EQ(bus.n, 0u);
}

TEST_CASE(a_lost_link_drops_a_sweep_and_keeps_a_position)
{
    up(1u, 0x01u);
    servo_link_apply(pos(1500u), true, 0u);
    servo_link_lost();
    clear_log();
    s_now += 100u;
    servo_link_service(true);
    CHECK_EQ(frames_to(LINK_PAGE_CHANNELS, 0u), 1u);

    up(6u, 0x01u);
    servo_link_apply(sweep(1u, false), true, 0u);
    servo_link_lost();
    clear_log();
    s_now += 100u;
    servo_link_service(true);
    CHECK_EQ(bus.n, 0u);
}

TEST_CASE(a_sweep_with_no_surface_bound_is_refused_once)
{
    up(6u, 0x00u);
    CHECK(!servo_link_surfaces());
    servo_link_apply(sweep(1u, false), true, 0u);
    CHECK(servo_link_take_sweep_refused());
    CHECK(!servo_link_take_sweep_refused());
    CHECK(!servo_link_take_sweep_start());
    CHECK_EQ(frames_to(LINK_PAGE_SERVO, LINK_SV_SWEEP_MOVES), 0u);
    /* Not kept: a binding with a surface does not sweep it untapped. */
    servo_link_binding(0x01u, true);
    CHECK(servo_link_surfaces());
    clear_log();
    s_now += 100u;
    servo_link_service(true);
    CHECK_EQ(bus.n, 0u);
}

TEST_CASE(a_pulse_is_clamped_to_the_range_it_names)
{
    static const struct {
        uint16_t min_us, max_us, us, span, cfg_min, cfg_max;
    } k[] = {
        { 1000u, 2000u, 1500u, 500u, 1000u, 2000u },
        { 1000u, 2000u,  900u,   0u, 1000u, 2000u },   /* below: 0      */
        { 1000u, 2000u, 2100u, 1000u, 1000u, 2000u },  /* above: span   */
        { 1000u, 2000u, 1250u, 250u, 1000u, 2000u },
        {  660u,  860u,  760u, 500u,  660u,  860u },   /* narrow servo  */
        {    0u,    0u, 1750u, 750u, 1000u, 2000u },   /* none named    */
        {  300u, 2000u, 1500u, 500u, 1000u, 2000u },   /* below 400us   */
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        up(1u, 0x01u);
        servo_cmd_t c = pos(k[i].us);
        c.min_us = k[i].min_us;
        c.max_us = k[i].max_us;
        servo_link_apply(c, true, 0u);
        const frame_t *cfg = last_to(LINK_PAGE_CHAN_CFG);
        const frame_t *cmd = last_to(LINK_PAGE_CHANNELS);
        CHECK(cfg != NULL && cmd != NULL);
        if (cfg == NULL || cmd == NULL) {
            continue;
        }
        CHECK_EQ(cfg->regs[LINK_CC_ROLE], LINK_CC_ROLE_SURFACE);
        CHECK_EQ(cfg->regs[LINK_CC_MIN_US], k[i].cfg_min);
        CHECK_EQ(cfg->regs[LINK_CC_MAX_US], k[i].cfg_max);
        CHECK_EQ(cmd->regs[0], k[i].span);
    }
}

TEST_CASE(nothing_is_written_before_the_binding_is_read)
{
    reset_at(1000u);
    servo_link_came_up(1u);
    clear_log();
    CHECK(!servo_link_binding_known());
    servo_link_apply(pos(1500u), true, 0u);
    CHECK_EQ(bus.n, 0u);
    servo_link_binding(0x01u, true);
    CHECK(servo_link_binding_known());
    servo_link_forget_binding();
    CHECK(!servo_link_binding_known());
    servo_link_binding(0x00u, false);
    CHECK(!servo_link_surfaces());
}

TEST_CASE(a_silent_far_end_leaves_the_release_owed)
{
    up(1u, 0x01u);
    servo_link_apply(pos(1500u), true, 0u);
    bus.quiet = true;
    servo_link_let_go();
    CHECK(servo_link_release_owed());
    servo_link_service(true);
    CHECK(servo_link_release_owed());
    bus.quiet = false;
    servo_link_service(true);
    CHECK(!servo_link_release_owed());
    /* With nothing held or written, letting go owes nothing. */
    servo_link_let_go();
    CHECK(!servo_link_release_owed());
}

/* ------------------------------------------- the tester's port at rest */

/* An IOmcu page with port 0 on GP13, SERVO PWM and bound, TARGET @p t. */
static void prog_page(uint16_t t, bool bound)
{
    uint16_t page[LINK_PG_REGS];
    memset(page, 0, sizeof(page));
    page[LINK_PG_COUNT]  = 4u;
    page[LINK_PG_FLAGS]  = bound ? LINK_PG_F_BOUND(0u) : LINK_PG_F_REFUSED(0u);
    page[LINK_PG_TARGET] = t;
    for (unsigned n = 0; n < LINK_PG_PORTS_MAX; ++n) {
        page[LINK_PG_REG(n, LINK_PG_PIN)] =
            (n == 0u) ? (uint16_t)13u : (uint16_t)LINK_PG_PIN_NONE;
        page[LINK_PG_FUNC(n)] = (uint16_t)((n == 0u) ? 0u : 1u);
    }
    prog_link_read(&prog, page, s_now);
}

/* Whether a TARGET write of @p t went to the PROG page. */
static bool target_sent(uint16_t t)
{
    for (unsigned i = 0; i < bus.n && i < LOG_MAX; ++i) {
        const frame_t *f = &bus.log[i];
        if (f->page == LINK_PAGE_PROG && f->offset == LINK_PG_TARGET
            && f->regs[0] == t) {
            return true;
        }
    }
    return false;
}

/*
 * SAFE right after a link-up, as after an IOmcu update: the IOmcu starts
 * with the standard outputs as TARGET and the sweep state is not known.
 * The port the tester is set to is written back at rest, with the stop
 * that settles the sweep in front of it, and not only at the next ARM.
 */
TEST_CASE(the_tester_port_is_put_back_at_rest_after_a_link_up)
{
    up(LINK_MINOR_PROG_FUNC, 0x01u);
    prog_link_came_up(&prog, LINK_MINOR_PROG_FUNC);
    prog_page(0u, true);
    prog_link_want(&prog, 1u, true);
    panel.armed = false;
    servo_link_prog_service();
    CHECK(sweep_reg_sent(0u));
    CHECK(target_sent(1u));
    CHECK_EQ(prog_link_target(&prog), 1u);
    /* Armed, the sweep state not known: nothing is written for it. */
    up(LINK_MINOR_PROG_FUNC, 0x01u);
    prog_link_came_up(&prog, LINK_MINOR_PROG_FUNC);
    prog_page(0u, true);
    prog_link_want(&prog, 1u, true);
    panel.armed = true;
    servo_link_prog_service();
    CHECK(!sweep_reg_sent(0u));
    CHECK(!target_sent(1u));
    /* A stop that is not answered leaves it waiting. */
    panel.armed = false;
    bus.quiet = true;
    servo_link_prog_service();
    CHECK(!target_sent(1u));
    bus.quiet = false;
    servo_link_prog_service();
    CHECK(target_sent(1u));
}

/*
 * SAFE after a programming session gives the port back: the IOmcu took
 * the TARGET to the standard outputs while the port had no pin bound,
 * and the panel puts the port back once it is bound again.
 */
TEST_CASE(the_tester_port_is_put_back_at_rest_after_a_session)
{
    up(LINK_MINOR_PROG_FUNC, 0x01u);
    prog_link_came_up(&prog, LINK_MINOR_PROG_FUNC);
    prog_page(0u, true);
    prog_link_want(&prog, 1u, true);
    servo_link_prog_service();
    servo_link_prog_service();
    CHECK_EQ(prog_link_target(&prog), 1u);
    /* The session holds the port: nothing is written. */
    prog_link_hold(&prog, true);
    prog_page(0u, false);
    clear_log();
    servo_link_prog_service();
    CHECK_EQ(bus.n, 0u);
    /* Given back, read bound again: the target goes back at rest. */
    prog_link_hold(&prog, false);
    prog_page(0u, true);
    servo_link_prog_service();
    CHECK(target_sent(1u));
    CHECK_EQ(prog_link_target(&prog), 1u);
}

int main(void)
{
    RUN(a_coprocessor_before_4_2_has_no_sweep_and_a_hold_sends_nothing);
    RUN(the_first_command_after_link_up_writes_a_stop_first);
    RUN(a_resume_before_4_6_starts_the_curve_over_and_says_so);
    RUN(a_resume_from_4_6_is_one_register_and_carries_the_phase_on);
    RUN(a_refused_resume_starts_the_curve_over_and_says_so);
    RUN(a_sweep_waits_while_a_release_is_owed_and_cannot_be_paid);
    RUN(a_sweep_waits_for_an_unpaid_release_over_a_known_binding);
    RUN(an_arm_onto_an_unknown_binding_leaves_the_release_owed);
    RUN(a_release_over_no_surface_is_paid_at_once);
    RUN(a_link_up_puts_each_slot_back_at_its_own_rate);
    RUN(a_refused_rate_is_not_asked_again_until_a_reset);
    RUN(an_unanswered_rate_reset_is_tried_again_until_it_lands);
    RUN(a_rate_not_known_is_settled_while_armed_before_the_hold);
    RUN(a_target_change_the_page_reports_is_noted_for_the_rate);
    RUN(a_hold_unrepeated_for_more_than_500_ms_is_let_go);
    RUN(a_hold_asked_499_or_500_ms_after_the_last_is_held_and_owes_nothing);
    RUN(a_hold_asked_501_ms_after_the_last_owes_the_release_until_paid);
    RUN(a_sweep_after_a_hold_lost_at_501_ms_starts_from_the_centre);
    RUN(a_hold_lost_at_501_ms_sends_no_faster_rate);
    RUN(a_hold_answered_after_more_than_500_ms_ends_the_pause);
    RUN(a_held_position_is_said_again_at_100_ms_and_not_before);
    RUN(a_stop_during_a_position_ends_it_between_frames);
    RUN(a_release_or_hold_asked_during_a_sweep_write_ends_it);
    RUN(an_arm_after_a_stop_in_between_is_not_asked_for);
    RUN(a_disarm_is_acted_on_with_or_without_the_link);
    RUN(a_sweep_asked_with_the_link_down_is_dropped_and_owes_a_release);
    RUN(a_lost_link_drops_a_sweep_and_keeps_a_position);
    RUN(a_sweep_with_no_surface_bound_is_refused_once);
    RUN(a_pulse_is_clamped_to_the_range_it_names);
    RUN(nothing_is_written_before_the_binding_is_read);
    RUN(a_silent_far_end_leaves_the_release_owed);
    RUN(the_tester_port_is_put_back_at_rest_after_a_link_up);
    RUN(the_tester_port_is_put_back_at_rest_after_a_session);
    return test_summary("servo_link");
}
