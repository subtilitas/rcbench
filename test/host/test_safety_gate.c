/*
 * The coprocessor's decision to arm and the panel's answer to a link that
 * goes or comes, each on its own and then the two ends against each other.
 *
 * The second half is a model of both main loops around the real shared
 * code -- link_dev.c, link_control.c, safety_gate.c, heartbeat.c, arming.c
 * and outputs.c -- on a 1 ms clock:
 *
 *   coprocessor  one pass per millisecond: a frame served, then
 *                safety_gate_step(), then outputs_arm() with its answer.  A
 *                restart is link_dev_init() and heartbeat_mon_init() again,
 *                with a throttle channel bound as the flash would restore
 *                it and no command.
 *   panel        a pass, then a wait of 5 ms plus the pass's own work.  A
 *                pass pumps the heartbeat, serves the arming policy, drains
 *                the operator's commands and polls the far end every 50 ms
 *                while the link is up and every 1000 ms while it is down.
 *   the wire     an exchange costs 1 ms.  A request sent while the
 *                coprocessor is down is lost after LINK_HOST_TIMEOUT_MS
 *                (the controller chip acknowledged it), fails at once (the
 *                panel's controller is bus-off), or is retransmitted until
 *                the coprocessor answers.
 *
 * Nothing here has run on hardware; the model's numbers are the firmware's
 * constants and not measurements.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include <stdbool.h>
#include <stdint.h>

#include "arming.h"
#include "heartbeat.h"
#include "link_control.h"
#include "link_dev.h"
#include "link_host.h"
#include "link_msg.h"
#include "link_pages.h"
#include "outputs.h"
#include "safety_gate.h"

/* ------------------------------------------------------- the gate alone */

static uint16_t        g_control[LINK_CT_COUNT];
static link_dev_t      g_dev;
static heartbeat_mon_t g_beat;

static void gate_init(uint32_t now)
{
    memset(g_control, 0, sizeof(g_control));
    link_dev_init(&g_dev, NULL, 0, NULL, now);
    heartbeat_mon_init(&g_beat);
}

/* Passes of 1 ms from @p from to @p to, an edge every 20 ms, a request
 * every pass so the link stays out of silence. */
static safety_gate_t gate_run(uint32_t from, uint32_t to)
{
    safety_gate_t g = { false, false };
    for (uint32_t t = from; t != to; ++t) {
        g_dev.last_request_ms = t;
        g_dev.heard = true;
        g = safety_gate_step(&g_beat, (t % 20u) == 0u, &g_dev, g_control, t);
    }
    return g;
}

static uint8_t gate_write(uint8_t off, uint16_t value)
{
    return safety_gate_control_write(g_control, off, 1u, &value, &g_beat,
                                     &g_dev);
}

TEST_CASE(a_start_is_latched_and_reports_no_fault)
{
    gate_init(0);
    CHECK(link_dev_arm_latched(&g_dev));
    CHECK(!g_dev.failsafe);

    /* Before the line is trusted: failsafe state, the heartbeat bit alone. */
    uint16_t state = 0xFFFFu, faults = 0xFFFFu;
    safety_gate_status(&g_beat, &g_dev, g_control, &state, &faults);
    CHECK_EQ(state, LINK_STATE_FAILSAFE);
    CHECK_EQ(faults, LINK_FAULT_HEARTBEAT);
    CHECK(!safety_gate_supply_ok(&g_beat, &g_dev));

    /* With the line trusted: idle, no fault bit, the supply's ON taken --
     * and still no arm. */
    (void)gate_run(1, 200);
    CHECK(g_beat.alive);
    safety_gate_status(&g_beat, &g_dev, g_control, &state, &faults);
    CHECK_EQ(state, LINK_STATE_IDLE);
    CHECK_EQ(faults, 0);
    CHECK(safety_gate_supply_ok(&g_beat, &g_dev));
    CHECK(safety_gate_line_trusted(faults));
    CHECK(!safety_gate_may_arm(&g_beat, &g_dev));
    CHECK_EQ(gate_write(LINK_CT_ARM, 1u), LINK_NACK_NOT_ARMED);
    CHECK_EQ(g_control[LINK_CT_ARM], 0);
    CHECK(!gate_run(200, 260).arm);
}

TEST_CASE(clear_releases_the_latch_and_the_arm_is_taken)
{
    gate_init(0);
    (void)gate_run(1, 200);
    CHECK_EQ(gate_write(LINK_CT_CLEAR, LINK_CLEAR_MAGIC), 0);
    CHECK(!link_dev_arm_latched(&g_dev));
    CHECK_EQ(gate_write(LINK_CT_ARM, 1u), 0);
    const safety_gate_t g = gate_run(200, 260);
    CHECK(g.arm);
    CHECK(!g.off);

    uint16_t state = 0, faults = 0xFFFFu;
    safety_gate_status(&g_beat, &g_dev, g_control, &state, &faults);
    CHECK_EQ(state, LINK_STATE_ARMED);
    CHECK_EQ(faults, 0);

    /* A CLEAR with the wrong value releases nothing. */
    gate_init(0);
    (void)gate_run(1, 200);
    CHECK_EQ(gate_write(LINK_CT_CLEAR, 0x1234u), LINK_NACK_BAD_VALUE);
    CHECK(link_dev_arm_latched(&g_dev));
}

TEST_CASE(a_distrusted_heartbeat_latches_until_clear)
{
    gate_init(0);
    (void)gate_run(1, 200);
    (void)gate_write(LINK_CT_CLEAR, LINK_CLEAR_MAGIC);
    (void)gate_write(LINK_CT_ARM, 1u);
    g_control[LINK_CT_THROTTLE] = 7000u;
    CHECK(gate_run(200, 301).arm);

    /* One edge 2 ms after a real one: under HEARTBEAT_MIN_GAP_MS. */
    g_dev.last_request_ms = 302u;
    safety_gate_t g = safety_gate_step(&g_beat, true, &g_dev, g_control, 302u);
    CHECK(g.off);
    CHECK(!g.arm);
    CHECK(link_dev_arm_latched(&g_dev));
    CHECK(!g_dev.failsafe);                  /* the link was never silent */
    CHECK_EQ(g_control[LINK_CT_ARM], 0);
    CHECK_EQ(g_control[LINK_CT_THROTTLE], 0);

    /* The line is trusted again after four good intervals, and the ARM the
     * panel goes on writing is refused all the same. */
    g = gate_run(303, 600);
    CHECK(g_beat.alive);
    CHECK(!g.off);                           /* the edge, once */
    CHECK_EQ(gate_write(LINK_CT_ARM, 1u), LINK_NACK_NOT_ARMED);
    CHECK(!gate_run(600, 700).arm);
    /* The supply's gate and STATUS do not carry the latch. */
    CHECK(safety_gate_supply_ok(&g_beat, &g_dev));
    uint16_t state = 0xFFFFu, faults = 0xFFFFu;
    safety_gate_status(&g_beat, &g_dev, g_control, &state, &faults);
    CHECK_EQ(state, LINK_STATE_IDLE);
    CHECK_EQ(faults, 0);

    CHECK_EQ(gate_write(LINK_CT_CLEAR, LINK_CLEAR_MAGIC), 0);
    CHECK_EQ(gate_write(LINK_CT_ARM, 1u), 0);
    CHECK(gate_run(700, 760).arm);
}

TEST_CASE(a_silent_line_latches_at_the_ceiling_and_not_before)
{
    /* The last edge at 300 ms: trusted for 149 ms of silence, and not at
     * HEARTBEAT_MAX_GAP_MS (150 ms). */
    for (uint32_t base = 0u; base < 2u; ++base) {
        const uint32_t t0 = base == 0u ? 0u : 0xFFFFFF00u;   /* and the wrap */
        gate_init(t0);
        for (uint32_t i = 1u; i <= 300u; ++i) {
            g_dev.last_request_ms = t0 + i;
            g_dev.heard = true;
            (void)safety_gate_step(&g_beat, (i % 20u) == 0u, &g_dev,
                                   g_control, t0 + i);
        }
        (void)gate_write(LINK_CT_CLEAR, LINK_CLEAR_MAGIC);
        (void)gate_write(LINK_CT_ARM, 1u);
        safety_gate_t g = { false, false };
        for (uint32_t i = 301u; i < 300u + HEARTBEAT_MAX_GAP_MS; ++i) {
            g_dev.last_request_ms = t0 + i;
            g = safety_gate_step(&g_beat, false, &g_dev, g_control, t0 + i);
        }
        CHECK(g.arm);                        /* 149 ms: still inside */
        CHECK(!g.off);
        CHECK(!link_dev_arm_latched(&g_dev));
        g_dev.last_request_ms = t0 + 300u + HEARTBEAT_MAX_GAP_MS;
        g = safety_gate_step(&g_beat, false, &g_dev, g_control,
                             t0 + 300u + HEARTBEAT_MAX_GAP_MS);
        CHECK(g.off);                        /* 150 ms */
        CHECK(!g.arm);
        CHECK(link_dev_arm_latched(&g_dev));
        g_dev.last_request_ms = t0 + 301u + HEARTBEAT_MAX_GAP_MS;
        g = safety_gate_step(&g_beat, false, &g_dev, g_control,
                             t0 + 301u + HEARTBEAT_MAX_GAP_MS);
        CHECK(!g.off);                       /* the edge, once */
    }
}

TEST_CASE(link_silence_latches_at_200_ms_across_the_wrap)
{
    for (uint32_t base = 0u; base < 2u; ++base) {
        const uint32_t t0 = base == 0u ? 1000u : 0xFFFFFFA0u;
        gate_init(t0);
        /* The line beats throughout; requests stop at t0 + 300. */
        safety_gate_t g = { false, false };
        bool armed_at_199 = false, off_at_200 = false;
        for (uint32_t i = 1u; i <= 600u; ++i) {
            if (i <= 300u) {
                g_dev.last_request_ms = t0 + i;
                g_dev.heard = true;
            }
            if (i == 250u) {
                (void)gate_write(LINK_CT_CLEAR, LINK_CLEAR_MAGIC);
                (void)gate_write(LINK_CT_ARM, 1u);
            }
            g = safety_gate_step(&g_beat, (i % 20u) == 0u, &g_dev, g_control,
                                 t0 + i);
            if (i == 300u + LINK_DEV_SILENCE_MS - 1u) {
                armed_at_199 = g.arm && !g.off;
            }
            if (i == 300u + LINK_DEV_SILENCE_MS) {
                off_at_200 = g.off && !g.arm;
            }
        }
        CHECK(armed_at_199);
        CHECK(off_at_200);
        CHECK(g_dev.failsafe);
        CHECK(link_dev_arm_latched(&g_dev));
        CHECK(g_beat.alive);

        /* Traffic returns: ARM refused, and the supply's ON with it, until
         * CLEAR. */
        g_dev.last_request_ms = t0 + 600u;
        CHECK_EQ(gate_write(LINK_CT_ARM, 1u), LINK_NACK_NOT_ARMED);
        CHECK(!safety_gate_supply_ok(&g_beat, &g_dev));
        uint16_t state = 0, faults = 0;
        safety_gate_status(&g_beat, &g_dev, g_control, &state, &faults);
        CHECK_EQ(state, LINK_STATE_FAILSAFE);
        CHECK_EQ(faults, LINK_FAULT_LINK_SILENT);
        CHECK_EQ(gate_write(LINK_CT_CLEAR, LINK_CLEAR_MAGIC), 0);
        CHECK(safety_gate_supply_ok(&g_beat, &g_dev));
        CHECK_EQ(gate_write(LINK_CT_ARM, 1u), 0);
    }
}

TEST_CASE(a_refused_arm_leaves_the_latch_set)
{
    /* CLEAR acknowledged while the line is not trusted yet, then the frame
     * that arms: refused, and the line becoming trusted afterwards does not
     * leave this end open to the next ARM. */
    gate_init(0);
    (void)gate_run(1, 50);                   /* two edges: not trusted */
    CHECK(!g_beat.alive);
    CHECK_EQ(gate_write(LINK_CT_CLEAR, LINK_CLEAR_MAGIC), 0);
    CHECK(!link_dev_arm_latched(&g_dev));
    CHECK_EQ(gate_write(LINK_CT_ARM, 1u), LINK_NACK_NOT_ARMED);
    CHECK(link_dev_arm_latched(&g_dev));
    (void)gate_run(50, 300);
    CHECK(g_beat.alive);
    CHECK_EQ(gate_write(LINK_CT_ARM, 1u), LINK_NACK_NOT_ARMED);
    CHECK(!gate_run(300, 320).arm);

    /* A frame carrying CLEAR and ARM together is refused whole and releases
     * nothing: ARM is judged before the CLEAR is acted on. */
    const uint16_t both[4] = { 1u, 0u, 0u, LINK_CLEAR_MAGIC };
    CHECK_EQ(safety_gate_control_write(g_control, 0u, 4u, both, &g_beat,
                                       &g_dev),
             LINK_NACK_NOT_ARMED);
    CHECK(link_dev_arm_latched(&g_dev));

    /* ARM = 0 is taken latched or not, and latches nothing. */
    CHECK_EQ(gate_write(LINK_CT_CLEAR, LINK_CLEAR_MAGIC), 0);
    CHECK_EQ(gate_write(LINK_CT_ARM, 0u), 0);
    CHECK(!link_dev_arm_latched(&g_dev));
    /* Nor does a refusal for another reason. */
    CHECK_EQ(gate_write(LINK_CT_THROTTLE, LINK_THROTTLE_MAX + 1u),
             LINK_NACK_BAD_VALUE);
    CHECK(!link_dev_arm_latched(&g_dev));
}

TEST_CASE(null_arguments_arm_nothing)
{
    gate_init(0);
    const uint16_t one = 1u;
    CHECK(!safety_gate_may_arm(NULL, &g_dev));
    CHECK(!safety_gate_may_arm(&g_beat, NULL));
    CHECK(!safety_gate_supply_ok(NULL, NULL));
    CHECK(link_dev_arm_latched(NULL));
    link_dev_latch_arm(NULL);
    CHECK(!safety_gate_step(NULL, false, &g_dev, g_control, 0).arm);
    CHECK(!safety_gate_step(&g_beat, false, NULL, g_control, 0).arm);
    CHECK(!safety_gate_step(&g_beat, false, &g_dev, NULL, 0).arm);
    CHECK_EQ(safety_gate_control_write(NULL, 0, 1, &one, &g_beat, &g_dev),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(safety_gate_control_write(g_control, 0, 1, NULL, &g_beat, &g_dev),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(safety_gate_control_write(g_control, 0, 1, &one, &g_beat, NULL),
             LINK_NACK_BAD_RANGE);
    uint16_t state = 0, faults = 0;
    safety_gate_status(NULL, NULL, NULL, &state, &faults);
    CHECK_EQ(state, LINK_STATE_FAILSAFE);
    CHECK_EQ(faults, LINK_FAULT_LINK_SILENT | LINK_FAULT_HEARTBEAT);
    safety_gate_status(&g_beat, &g_dev, g_control, NULL, NULL);
    CHECK(!safety_gate_line_trusted(LINK_FAULT_HEARTBEAT));
    CHECK(safety_gate_line_trusted(LINK_FAULT_LINK_SILENT));
}

/* --------------------------------------------------------- the two ends */

#define D_CH_THROTTLE LINK_OUT_CHANNELS   /* the coprocessor's own throttle */
#define P_CH_THROTTLE 0u

static uint32_t T;                /* the clock, 1 ms                       */
static uint32_t T0;               /* where a scenario starts it            */
static bool     w_line;           /* the level of the panel's safety line  */
static uint32_t w_glitch_at;      /* 0: none.  The line reads inverted for
                                     this one millisecond                  */
typedef enum { WIRE_LOST, WIRE_BUS_OFF, WIRE_RETX } wire_t;
static wire_t   w_wire;
static uint32_t w_unanswered;     /* exchanges that ended with no answer   */

/* A panel or a coprocessor without this change's rules, to show what each
 * end holds on its own. */
static bool w_panel_ignores_link_edges;
static bool w_dev_starts_unlatched;

/* -- coprocessor */
static bool            d_up;
static uint16_t        d_control[LINK_CT_COUNT];
static link_dev_t      d_dev;
static heartbeat_mon_t d_beat;
static bool            d_beat_level;
static outputs_t       d_bank;
static uint32_t        d_now;
static bool            d_was_driving;
static unsigned        d_arm_edges;      /* times the bank began driving    */
static unsigned        d_beat_disarms;   /* disarms taken for the heartbeat */
static unsigned        d_arm_frames;     /* writes carrying ARM != 0        */

static bool d_line(void)
{
    return (w_glitch_at != 0u && T == w_glitch_at) ? !w_line : w_line;
}

static void d_control_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = d_control[off + i];
    }
}

static uint8_t d_control_write(void *ctx, uint8_t off, uint8_t n,
                               const uint16_t *in)
{
    (void)ctx;
    if (off == LINK_CT_ARM && in[0] != 0u) {
        ++d_arm_frames;
    }
    const uint8_t nack = safety_gate_control_write(d_control, off, n, in,
                                                   &d_beat, &d_dev);
    if (nack != 0u) {
        return nack;
    }
    const uint16_t thr = (uint16_t)(((uint32_t)d_control[LINK_CT_THROTTLE]
                                     * OUT_SPAN) / LINK_THROTTLE_MAX);
    (void)outputs_set(&d_bank, D_CH_THROTTLE, thr, d_now);
    (void)outputs_set_role_channels(&d_bank, OUT_ROLE_THROTTLE,
                                    (uint8_t)LINK_OUT_CHANNELS, thr, d_now);
    return 0u;
}

/* STATE and FAULTS as the firmware fills them at a read. */
static void d_status_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    uint16_t st[LINK_ST_COUNT] = { 0 };
    safety_gate_status(&d_beat, &d_dev, d_control, &st[LINK_ST_STATE],
                       &st[LINK_ST_FAULTS]);
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = st[off + i];
    }
}

static const link_page_t k_pages[] = {
    { LINK_PAGE_STATUS,  LINK_ST_COUNT, d_status_read,  NULL },
    { LINK_PAGE_CONTROL, LINK_CT_COUNT, d_control_read, d_control_write },
};

/* The start: the saved binding has channel 0 as a motor; commands are not
 * restored. */
static void d_boot(void)
{
    memset(d_control, 0, sizeof(d_control));
    d_now = T;
    outputs_init(&d_bank, T);
    (void)outputs_set_role(&d_bank, D_CH_THROTTLE, OUT_ROLE_THROTTLE);
    (void)outputs_set_role(&d_bank, 0, OUT_ROLE_THROTTLE);
    link_dev_init(&d_dev, k_pages, 2, NULL, T);
    if (w_dev_starts_unlatched) {
        d_dev.arm_latched = false;
    }
    heartbeat_mon_init(&d_beat);
    d_beat_level = d_line();
    d_up = true;
    d_was_driving = false;
}

static void d_reset(void)
{
    d_up = false;
    d_was_driving = false;
}

/* One pass.  @p req is a frame the pass found waiting, or NULL. */
static bool d_pass(const link_msg_t *req, link_msg_t *reply)
{
    bool answered = false;
    d_now = T;
    if (req != NULL) {
        answered = link_dev_dispatch(&d_dev, req, reply, T);
    }
    const bool was_beating = d_beat.alive;
    const bool level = d_line();
    const bool edge = level != d_beat_level;
    d_beat_level = level;
    const bool drove = outputs_driving(&d_bank);
    const safety_gate_t g = safety_gate_step(&d_beat, edge, &d_dev,
                                             d_control, T);
    if (g.off) {
        outputs_arm(&d_bank, false, T);
        if (drove && was_beating && !d_beat.alive) {
            ++d_beat_disarms;
        }
    }
    outputs_arm(&d_bank, g.arm, T);
    outputs_step(&d_bank, T);

    const bool driving = outputs_driving(&d_bank);
    if (driving && !d_was_driving) {
        ++d_arm_edges;
    }
    d_was_driving = driving;
    return answered;
}

/* -- panel */
static heartbeat_gen_t p_gen;
static arming_t        p_arm;
static outputs_t       p_out;
static uint16_t        p_throttle;
static bool            p_link_up, p_endpoints_hold;
static uint32_t        p_last_poll, p_stops_served;
static unsigned        p_far_end_stops, p_arm_refused, p_arm_acked,
                       p_arm_gave_up, p_clear_sent, p_arm_unanswered;
/* Which exchange of an arm went unanswered: the servo release, CLEAR, the
 * frame that arms. */
static unsigned        p_quiet_release, p_quiet_clear, p_quiet_frame;
/* When a poll's write of ARM and THROTTLE first went unanswered, and when
 * the panel first finished a far-end stop. */
static uint32_t        p_write_quiet_at, p_stop_at;
static bool            p_write_quiet, p_stop_seen;
static uint32_t        p_extra_at, p_extra_ms;    /* one long pass, pumping */
static uint32_t        p_pass_work_ms;            /* work in every pass     */
static uint32_t        p_touch_out_at, p_touch_out_ms;
static uint32_t        p_keepalive_last, p_keepalive_gap;

static uint32_t e_reset_at, e_boot_at;
static bool     e_reset_on, e_boot_on;

static void events(void)
{
    if (e_reset_on && T == e_reset_at) {
        d_reset();
    }
    if (e_boot_on && T == e_boot_at) {
        d_boot();
    }
}

/* What every pass and every 5 ms of an exchange's wait does. */
static void p_pump(void)
{
    const bool touch_out = p_touch_out_ms != 0u
                           && (uint32_t)(T - p_touch_out_at) < p_touch_out_ms;
    if (!touch_out) {
        arming_touch_seen(&p_arm, T);
    }
    arming_touch_poll(&p_arm, T);
    w_line = heartbeat_gen_step(&p_gen, T, arming_heartbeat(&p_arm, T));
}

static bool step_ms(const link_msg_t *req, link_msg_t *reply)
{
    ++T;
    events();
    return d_up ? d_pass(req, reply) : false;
}

static void wait_pumping(uint32_t ms)
{
    for (uint32_t i = 1; i <= ms; ++i) {
        (void)step_ms(NULL, NULL);
        if (i % 5u == 0u) {
            p_pump();
        }
    }
}

static void sleep_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; ++i) {
        (void)step_ms(NULL, NULL);
    }
}

static bool xchg(uint8_t op, uint8_t page, uint8_t off, uint8_t n,
                 const uint16_t *regs, link_msg_t *reply)
{
    link_msg_t req;
    memset(&req, 0, sizeof(req));
    memset(reply, 0, sizeof(*reply));
    req.op = op;
    req.page = page;
    req.offset = off;
    req.count = n;
    for (uint8_t i = 0; regs != NULL && i < n; ++i) {
        req.regs[i] = regs[i];
    }
    if (!d_up) {
        if (w_wire == WIRE_BUS_OFF) {
            ++w_unanswered;
            return false;
        }
        if (w_wire == WIRE_LOST) {
            wait_pumping(LINK_HOST_TIMEOUT_MS);
            ++w_unanswered;
            return false;
        }
        for (uint32_t i = 1; i <= LINK_HOST_TIMEOUT_MS && !d_up; ++i) {
            (void)step_ms(NULL, NULL);
            if (i % 5u == 0u) {
                p_pump();
            }
            if (i == LINK_HOST_TIMEOUT_MS) {
                ++w_unanswered;
                return false;
            }
        }
    }
    if (!step_ms(&req, reply)) {
        ++w_unanswered;         /* it went down under this very frame */
        return false;
    }
    return true;
}

static bool p_control_write(bool armed, link_msg_t *ack)
{
    const uint16_t regs[2] = { armed ? 1u : 0u, p_throttle };
    if (armed) {
        if (p_keepalive_last != 0u
            && (uint32_t)(T - p_keepalive_last) > p_keepalive_gap) {
            p_keepalive_gap = (uint32_t)(T - p_keepalive_last);
        }
        p_keepalive_last = T;
    }
    return xchg(LINK_OP_WRITE, LINK_PAGE_CONTROL, LINK_CT_ARM, 2, regs, ack)
           && ack->op == LINK_OP_ACK;
}

/* far_end_stop_here() */
static void p_stop_here(void)
{
    outputs_arm(&p_out, false, T);
    p_throttle = 0;
    ++p_far_end_stops;
    if (!p_stop_seen) {
        p_stop_seen = true;
        p_stop_at = T;
    }
}

/* far_line_trusted() */
static bool p_line_trusted(bool *answered)
{
    link_msg_t st;
    *answered = xchg(LINK_OP_READ, LINK_PAGE_STATUS, LINK_ST_FAULTS, 1, NULL,
                     &st);
    return *answered && st.op == LINK_OP_DATA
           && safety_gate_line_trusted(st.regs[0]);
}

/* arm_write_failed(): refused, or the link quiet under the arm. */
static void p_arm_write_failed(uint32_t quiet_before)
{
    if (arming_write_failed(&p_arm, w_unanswered == quiet_before)) {
        ++p_arm_unanswered;
        p_stop_here();
    } else {
        ++p_arm_refused;
    }
}

/* service_arming() */
static void p_service_arming(void)
{
    const uint32_t stops = arming_stop_count(&p_arm);
    if (stops != p_stops_served) {
        p_stops_served = stops;
        p_throttle = 0;
    }
    if (!p_link_up) {
        arming_line_nobody(&p_arm);
    } else if (arming_line_wanted(&p_arm, T)) {
        bool answered = true;
        const bool trusted = p_line_trusted(&answered);
        if (answered) {
            arming_line_report(&p_arm, trusted);
        } else if (arming_link_lost(&p_arm, outputs_armed(&p_out))) {
            p_stop_here();
        }
    }
    switch (arming_step(&p_arm, T)) {
    case ARMING_ACT_DISARM:
        outputs_arm(&p_out, false, T);
        p_throttle = 0;
        if (p_link_up) {
            link_msg_t a;
            (void)p_control_write(false, &a);
        }
        break;
    case ARMING_ACT_ARM: {
        link_msg_t ack;
        const uint32_t quiet = w_unanswered;
        p_throttle = 0;
        if (!p_link_up) {
            outputs_arm(&p_out, true, T);   /* the simulator */
            break;
        }
        /* servo_service(): the release of a held position, one exchange. */
        if (!xchg(LINK_OP_READ, LINK_PAGE_CONTROL, 0, 1, NULL, &ack)) {
            ++p_quiet_release;
            p_arm_write_failed(quiet);
            break;
        }
        const uint16_t magic = LINK_CLEAR_MAGIC;
        ++p_clear_sent;
        if (!(xchg(LINK_OP_WRITE, LINK_PAGE_CONTROL, LINK_CT_CLEAR, 1, &magic,
                   &ack)
              && ack.op == LINK_OP_ACK)) {
            p_quiet_clear += w_unanswered != quiet;
            p_arm_write_failed(quiet);
            break;
        }
        const uint16_t regs[3] = { 1u, p_throttle, 0u };
        if (!(xchg(LINK_OP_WRITE, LINK_PAGE_CONTROL, LINK_CT_ARM, 3, regs,
                   &ack)
              && ack.op == LINK_OP_ACK)) {
            p_quiet_frame += w_unanswered != quiet;
            p_arm_write_failed(quiet);
        } else {
            outputs_arm(&p_out, true, T);
            ++p_arm_acked;
        }
        break;
    }
    case ARMING_ACT_GIVE_UP:
        ++p_arm_gave_up;
        break;
    default:
        break;
    }
}

/* poll_far_end() with poll_bench() */
static void p_poll_far_end(void)
{
    if ((uint32_t)(T - p_last_poll) < (p_link_up ? 50u : 1000u)) {
        return;
    }
    p_last_poll = T;
    link_msg_t r;
    bool answered;
    if (p_link_up) {
        answered = xchg(LINK_OP_READ, LINK_PAGE_CONTROL, 0, LINK_CT_COUNT,
                        NULL, &r);
        if (answered) {
            p_pump();
            const bool armed = outputs_armed(&p_out) && !p_endpoints_hold
                               && !arming_stopped(&p_arm);
            link_msg_t ack;
            const bool written = p_control_write(armed, &ack);
            if (!written && ack.op != LINK_OP_NACK) {
                if (!p_write_quiet) {
                    p_write_quiet = true;
                    p_write_quiet_at = T;
                }
                answered = false;       /* the poll has failed */
            }
            if (answered && p_endpoints_hold && written && !armed) {
                link_msg_t cc;
                if (xchg(LINK_OP_READ, LINK_PAGE_CONTROL, 0, LINK_CT_COUNT,
                         NULL, &cc)) {
                    p_endpoints_hold = false;
                }
            }
            if (!written && armed && ack.op == LINK_OP_NACK) {
                arming_stop_from_far_end(&p_arm);
                p_stop_here();
            }
        }
    } else {
        answered = xchg(LINK_OP_READ, LINK_PAGE_CONTROL, 0, LINK_CT_COUNT,
                        NULL, &r);
        if (answered) {
            if (!w_panel_ignores_link_edges
                && arming_link_found(&p_arm, outputs_armed(&p_out))) {
                p_stop_here();
            }
            /* link_came_up(): about ten exchanges, then the hold. */
            for (int i = 0; i < 10; ++i) {
                (void)xchg(LINK_OP_READ, LINK_PAGE_CONTROL, 0, LINK_CT_COUNT,
                           NULL, &r);
            }
            p_endpoints_hold = true;
        }
    }
    if (!answered && p_link_up && !w_panel_ignores_link_edges
        && arming_link_lost(&p_arm, outputs_armed(&p_out))) {
        p_stop_here();
    }
    p_link_up = answered;
}

/* The operator. */
typedef struct {
    uint32_t at;
    int      kind;
    uint16_t value;
    bool     done;
} op_t;
enum { OP_ARM = 1, OP_STOP, OP_THROTTLE };
static op_t w_ops[8];

static void op_at(unsigned i, uint32_t at, int kind, uint16_t value)
{
    w_ops[i] = (op_t){ T0 + at, kind, value, false };
}

static void p_drain_commands(void)
{
    for (unsigned i = 0; i < 8; ++i) {
        op_t *o = &w_ops[i];
        if (o->kind == 0 || o->done || (int32_t)(T - o->at) < 0) {
            continue;
        }
        o->done = true;
        if (o->kind == OP_ARM) {
            p_pump();
            arming_request_arm(&p_arm, T);
        } else if (o->kind == OP_STOP) {
            arming_stop_pressed(&p_arm);
            outputs_arm(&p_out, false, T);
            p_throttle = 0;
            if (p_link_up) {
                link_msg_t a;
                (void)p_control_write(false, &a);
            }
        } else {
            p_throttle = o->value;
        }
    }
}

static void world_init(uint32_t t0)
{
    T0 = t0;
    T = t0;
    w_line = false;
    w_glitch_at = 0;
    w_wire = WIRE_LOST;
    e_reset_on = e_boot_on = false;
    d_up = false;
    d_arm_edges = d_beat_disarms = d_arm_frames = 0;
    heartbeat_gen_init(&p_gen);
    arming_init(&p_arm, t0, HEARTBEAT_SETTLE_MS);
    arming_set_line_wait(&p_arm, ARMING_LINE_WAIT_MS);
    outputs_init(&p_out, t0);
    (void)outputs_set_role(&p_out, P_CH_THROTTLE, OUT_ROLE_THROTTLE);
    p_throttle = 0;
    p_link_up = false;
    p_endpoints_hold = false;
    p_last_poll = t0;
    p_stops_served = 0;
    p_far_end_stops = p_arm_refused = p_arm_acked = p_arm_gave_up = 0;
    p_arm_unanswered = 0;
    p_quiet_release = p_quiet_clear = p_quiet_frame = 0;
    p_write_quiet = p_stop_seen = false;
    p_clear_sent = 0;
    p_extra_at = p_extra_ms = 0;
    p_touch_out_at = p_touch_out_ms = 0;
    p_keepalive_last = p_keepalive_gap = 0;
    memset(w_ops, 0, sizeof(w_ops));
}

static void run_until(uint32_t end)
{
    end += T0;
    while ((int32_t)(T - end) < 0) {
        p_pump();
        p_service_arming();
        p_drain_commands();
        if (p_extra_ms != 0u && (int32_t)(T - p_extra_at) >= 0) {
            const uint32_t ms = p_extra_ms;
            p_extra_ms = 0;
            wait_pumping(ms);
        }
        p_poll_far_end();
        sleep_ms(5u + p_pass_work_ms);
    }
}

/* Armed at 1 s, 70 % at 1.5 s: the run every scenario below starts from. */
static void armed_at_70(uint32_t t0, wire_t wire)
{
    world_init(t0);
    w_wire = wire;
    d_boot();
    op_at(0, 1000, OP_ARM, 0);
    op_at(1, 1500, OP_THROTTLE, 7000);
}

static bool d_drives_70(void)
{
    return outputs_driving(&d_bank) && outputs_actual(&d_bank, 0) == 700u;
}

/* The coprocessor restarts at @p reset_at for @p down_ms.  Returns true
 * when it began driving a second time. */
static bool restart_rearms(uint32_t t0, uint32_t reset_at, uint32_t down_ms,
                           wire_t wire)
{
    armed_at_70(t0, wire);
    e_reset_at = t0 + reset_at;
    e_boot_at  = t0 + reset_at + down_ms;
    e_reset_on = e_boot_on = true;
    /* Looked at 100 ms ahead: a pass can run a few milliseconds past the
     * time it is asked to stop at. */
    run_until(reset_at - 100u);
    const bool ran = d_drives_70() && d_arm_edges == 1u;
    run_until(reset_at + down_ms + 6000u);
    return !ran || d_arm_edges != 1u || outputs_driving(&d_bank);
}

static const uint32_t k_downs[] = { 5, 30, 50, 60, 300, 1000, 5000 };
#define N_DOWNS (sizeof(k_downs) / sizeof(k_downs[0]))

TEST_CASE(the_power_up_arm_is_taken_on_the_first_hold)
{
    for (p_pass_work_ms = 0; p_pass_work_ms <= 4u; ++p_pass_work_ms) {
        armed_at_70(0, WIRE_LOST);
        run_until(900);
        /* Before any arm: the supply's ON is taken, STATUS is clean. */
        CHECK(safety_gate_supply_ok(&d_beat, &d_dev));
        uint16_t state = 0xFFFFu, faults = 0xFFFFu;
        safety_gate_status(&d_beat, &d_dev, d_control, &state, &faults);
        CHECK_EQ(state, LINK_STATE_IDLE);
        CHECK_EQ(faults, 0);
        CHECK(link_dev_arm_latched(&d_dev));
        run_until(3000);
        CHECK(d_drives_70());
        CHECK_EQ(d_arm_edges, 1);
        CHECK_EQ(p_clear_sent, 1);
        CHECK_EQ(p_arm_acked, 1);
        CHECK_EQ(p_arm_refused, 0);
        CHECK_EQ(p_arm_gave_up, 0);
        CHECK_EQ(p_far_end_stops, 0);
    }
    p_pass_work_ms = 0;
}

TEST_CASE(a_restarted_coprocessor_is_not_armed_again)
{
    static const wire_t wires[] = { WIRE_LOST, WIRE_BUS_OFF, WIRE_RETX };
    for (unsigned w = 0; w < 3u; ++w) {
        for (unsigned i = 0; i < N_DOWNS; ++i) {
            unsigned rearmed = 0, unlatched = 0;
            for (uint32_t ph = 0; ph < 55u; ++ph) {
                rearmed += restart_rearms(0, 3000u + ph, k_downs[i], wires[w]);
                /* And the panel holds a stop with its command at zero. */
                unlatched += !(arming_stopped(&p_arm)
                               && !outputs_armed(&p_out) && p_throttle == 0u
                               && p_far_end_stops == 1u && p_clear_sent == 1u);
            }
            if (rearmed != 0u || unlatched != 0u) {
                T_FAIL("wire %u, %u ms down: re-armed in %u of 55 reset "
                       "phases, panel not stopped in %u",
                       w, (unsigned)k_downs[i], rearmed, unlatched);
            }
        }
    }
}

TEST_CASE(a_restart_across_the_tick_wrap_is_not_armed_again)
{
    /* The reset at 3000 ms after a start 2800 ms before the wrap: the
     * outage, the panel's 1000 ms timeout and its 1 Hz probe all span it. */
    for (unsigned i = 0; i < N_DOWNS; ++i) {
        unsigned rearmed = 0;
        for (uint32_t ph = 0; ph < 55u; ph += 6u) {
            rearmed += restart_rearms(0u - 2800u, 3000u + ph, k_downs[i],
                                      WIRE_LOST);
            rearmed += restart_rearms(0u - 3400u, 3000u + ph, k_downs[i],
                                      WIRE_RETX);
        }
        CHECK_EQ(rearmed, 0);
    }
}

TEST_CASE(either_end_alone_holds_a_restart_it_can_see)
{
    /* A panel without the link-edge rule: the coprocessor's start latch
     * refuses the ARM written again, at every outage and wire. */
    w_panel_ignores_link_edges = true;
    unsigned rearmed = 0;
    for (unsigned i = 0; i < N_DOWNS; ++i) {
        for (uint32_t ph = 0; ph < 55u; ph += 2u) {
            rearmed += restart_rearms(0, 3000u + ph, k_downs[i], WIRE_LOST);
            rearmed += restart_rearms(0, 3000u + ph, k_downs[i], WIRE_RETX);
        }
    }
    CHECK_EQ(rearmed, 0);
    w_panel_ignores_link_edges = false;

    /* A coprocessor that starts unlatched: the panel's stop on the
     * link-down edge holds every outage the panel notices -- 60 ms and
     * longer with a lost or failed request. */
    w_dev_starts_unlatched = true;
    rearmed = 0;
    for (unsigned i = 3; i < N_DOWNS; ++i) {
        for (uint32_t ph = 0; ph < 55u; ph += 2u) {
            rearmed += restart_rearms(0, 3000u + ph, k_downs[i], WIRE_LOST);
            rearmed += restart_rearms(0, 3000u + ph, k_downs[i], WIRE_BUS_OFF);
        }
    }
    CHECK_EQ(rearmed, 0);
    w_dev_starts_unlatched = false;
}

/* Armed with no link at 1 s and at 70 % from 1.5 s; a coprocessor is
 * powered at 4 s. */
static void sim_then_power(void)
{
    world_init(0);
    w_wire = WIRE_BUS_OFF;
    op_at(0, 1000, OP_ARM, 0);
    op_at(1, 1500, OP_THROTTLE, 7000);
    e_boot_at = 4000;
    e_boot_on = true;
    run_until(3900);
}

TEST_CASE(a_bank_armed_with_no_link_arms_no_far_end_that_appears)
{
    sim_then_power();
    CHECK(outputs_armed(&p_out));            /* the simulator runs */
    CHECK_EQ(p_throttle, 7000);
    run_until(9000);
    CHECK(p_link_up);
    CHECK_EQ(d_arm_edges, 0);
    CHECK_EQ(d_arm_frames, 0);               /* never even asked */
    CHECK_EQ(p_clear_sent, 0);
    CHECK(!outputs_armed(&p_out));
    CHECK_EQ(p_throttle, 0);
    CHECK(arming_stopped(&p_arm));
    CHECK_EQ(p_far_end_stops, 1);

    /* The panel's rule off: asked once, refused by the start latch. */
    w_panel_ignores_link_edges = true;
    sim_then_power();
    run_until(9000);
    w_panel_ignores_link_edges = false;
    CHECK_EQ(d_arm_edges, 0);
    CHECK_EQ(d_arm_frames, 1);
    CHECK(arming_stopped(&p_arm));
    CHECK(!outputs_armed(&p_out));

    /* The coprocessor's latch off: the panel's rule alone. */
    w_dev_starts_unlatched = true;
    sim_then_power();
    run_until(9000);
    w_dev_starts_unlatched = false;
    CHECK_EQ(d_arm_edges, 0);
    CHECK_EQ(d_arm_frames, 0);

    /* And an operator's arm afterwards is taken: CLEAR, then ARM. */
    sim_then_power();
    op_at(2, 9000, OP_ARM, 0);
    run_until(10000);
    CHECK_EQ(d_arm_edges, 1);
    CHECK_EQ(p_clear_sent, 1);
    CHECK(outputs_driving(&d_bank));
    CHECK_EQ(outputs_actual(&d_bank, 0), 0);  /* from nothing */
}

TEST_CASE(a_pulled_cable_stops_the_panel_and_arms_nothing_on_return)
{
    /* The coprocessor keeps running; the panel's sends fail for 300 ms. */
    armed_at_70(0, WIRE_BUS_OFF);
    run_until(3000);
    CHECK(d_drives_70());
    d_up = false;                            /* frames stop arriving... */
    uint32_t until = T + 300u;
    while ((int32_t)(T - until) < 0) {
        p_pump();
        p_service_arming();
        p_poll_far_end();
        for (unsigned i = 0; i < 5u; ++i) {  /* ...and its passes go on */
            ++T;
            (void)d_pass(NULL, NULL);
        }
    }
    CHECK(d_dev.failsafe);
    CHECK(!outputs_driving(&d_bank));
    CHECK(arming_stopped(&p_arm));           /* on the link-down edge */
    CHECK(!outputs_armed(&p_out));
    CHECK_EQ(p_throttle, 0);
    CHECK(!arming_heartbeat(&p_arm, T));
    d_up = true;
    const unsigned frames = d_arm_frames;
    run_until(8000);
    CHECK(p_link_up);
    CHECK_EQ(d_arm_edges, 1);
    CHECK_EQ(d_arm_frames, frames);          /* no ARM = 1 after the return */
    CHECK_EQ(p_far_end_stops, 1);
}

TEST_CASE(a_heartbeat_glitch_stays_disarmed_until_clear)
{
    /* One panel pass at 3000 ms made longer, which stretches the interval
     * between two writes of ARM = 1 to 52, 98 and 123 ms. */
    static const uint32_t extra[]    = { 0, 76, 101 };
    static const uint32_t interval[] = { 52, 98, 123 };
    for (unsigned i = 0; i < 3u; ++i) {
        unsigned rearmed = 0, disarmed = 0, unlatched = 0;
        uint32_t gap = 0;
        for (uint32_t at = 2900u; at < 3100u; ++at) {
            armed_at_70(0, WIRE_LOST);
            w_glitch_at = at;
            p_extra_at = 3000u;
            p_extra_ms = extra[i];
            run_until(2800);
            const bool ran = d_drives_70();
            run_until(5000);
            if (p_keepalive_gap > gap) {
                gap = p_keepalive_gap;
            }
            if (d_beat_disarms == 0u) {
                /* The inverted sample closed two good intervals. */
                rearmed += !(ran && d_drives_70() && d_arm_edges == 1u);
                continue;
            }
            ++disarmed;
            rearmed += !ran || d_arm_edges != 1u || outputs_driving(&d_bank);
            unlatched += !(arming_stopped(&p_arm) && !outputs_armed(&p_out)
                           && p_throttle == 0u && p_clear_sent == 1u);
        }
        CHECK_EQ(gap, interval[i]);
        CHECK(disarmed >= 150u);
        CHECK_EQ(rearmed, 0);
        CHECK_EQ(unlatched, 0);
    }

    /* And the operator's next arm is taken. */
    armed_at_70(0, WIRE_LOST);
    w_glitch_at = 3012u;
    p_extra_at = 3000u;
    p_extra_ms = 101u;
    op_at(2, 6000, OP_ARM, 0);
    run_until(5900);
    CHECK_EQ(d_beat_disarms, 1);
    CHECK(!outputs_driving(&d_bank));
    CHECK(link_dev_arm_latched(&d_dev));
    run_until(7000);
    CHECK_EQ(d_arm_edges, 2);
    CHECK_EQ(p_clear_sent, 2);
    CHECK_EQ(p_arm_refused, 0);
    CHECK(outputs_driving(&d_bank));
    CHECK_EQ(outputs_actual(&d_bank, 0), 0);
}

TEST_CASE(the_first_arm_after_a_stop_is_taken)
{
    /* A pass of 5 to 9 ms, the hold completing at each of 110 phases. */
    for (p_pass_work_ms = 0; p_pass_work_ms <= 4u; ++p_pass_work_ms) {
        unsigned refused = 0, not_armed = 0;
        uint32_t slowest = 0;
        for (uint32_t ph = 0; ph < 110u; ++ph) {
            armed_at_70(0, WIRE_LOST);
            op_at(2, 3000, OP_STOP, 0);
            op_at(3, 6000u + ph, OP_ARM, 0);
            run_until(5990);
            const bool stopped = !outputs_driving(&d_bank)
                                 && link_dev_arm_latched(&d_dev);
            uint32_t took = 0;
            while (!outputs_driving(&d_bank) && took < 1000u) {
                run_until(6000u + ph + took);
                took += 5u;
            }
            if (took > slowest) {
                slowest = took;
            }
            refused += p_arm_refused + p_arm_gave_up;
            not_armed += !(stopped && outputs_driving(&d_bank)
                           && d_arm_edges == 2u && p_arm_acked == 2u
                           && p_clear_sent == 2u);
        }
        if (refused != 0u || not_armed != 0u) {
            T_FAIL("pass %u ms: refused %u, not armed %u of 110",
                   (unsigned)(5u + p_pass_work_ms), refused, not_armed);
        }
        /* Inside the bound, with room: settle plus wait is 300 ms. */
        CHECK(slowest <= 200u);
    }
    p_pass_work_ms = 0;
}

TEST_CASE(a_link_that_goes_while_an_arm_waits_is_a_stop)
{
    /* STOP at 3 s, the hold completes at 6 s, and the coprocessor goes down
     * for 3 s somewhere between 100 ms before the hold and the end of the
     * wait: every 5 ms, and every millisecond from 6090 to 6150 ms, where
     * the question about the line, the CLEAR and the frame that arms are
     * on the wire.  An exchange nobody answers has waited 1000 ms, past
     * the bound: that is a link lost, not an arm given up or refused. */
    static const wire_t wires[] = { WIRE_LOST, WIRE_BUS_OFF, WIRE_RETX };
    for (unsigned w = 0; w < 3u; ++w) {
        unsigned gave_up = 0, not_stopped = 0, armed = 0, unanswered = 0;
        unsigned release = 0, clear = 0, frame = 0;
        for (uint32_t at = 5900u; at <= 6300u;
             at += (at >= 6090u && at < 6150u) ? 1u : 5u) {
            armed_at_70(0, wires[w]);
            op_at(2, 3000, OP_STOP, 0);
            op_at(3, 6000, OP_ARM, 0);
            e_reset_at = at;
            e_boot_at = at + 3000u;
            e_reset_on = e_boot_on = true;
            run_until(at + 2500u);
            gave_up += p_arm_gave_up;
            /*
             * Two ends, by which pass found the link gone.  The poll,
             * before the hold completed: the panel arms its own bank with
             * no link, the simulated bench.  Anything later -- the question
             * about the line, the CLEAR, the frame that arms, the poll
             * after the arm was taken: a stop, the heartbeat withheld.
             * The coprocessor refuses nothing here, so no arm ends refused.
             */
            const bool idle = !outputs_armed(&p_out) && !p_arm.arming
                              && !p_arm.armed;
            const bool sim = outputs_armed(&p_out) && !p_link_up
                             && p_clear_sent == 1u;
            const bool stopped = idle && arming_stopped(&p_arm)
                                 && !arming_heartbeat(&p_arm, T);
            not_stopped += !(sim || stopped) || p_arm_refused != 0u;
            unanswered += p_arm_unanswered;
            release += p_quiet_release;
            clear += p_quiet_clear;
            frame += p_quiet_frame;
            /* And when the coprocessor is back, neither end is armed. */
            const unsigned edges = d_arm_edges;
            run_until(at + 9000u);
            armed += d_arm_edges != edges || outputs_driving(&d_bank)
                     || outputs_armed(&p_out);
        }
        if (gave_up != 0u || not_stopped != 0u || armed != 0u) {
            T_FAIL("wire %u: given up %u, not stopped %u, armed after the "
                   "return %u", w, gave_up, not_stopped, armed);
        }
        /* The sweep did put the reset under each exchange of the arm, and
         * each of them ended in the stop counted above. */
        CHECK(release >= 1u);
        CHECK(clear >= 1u);
        CHECK(frame >= 1u);
        CHECK_EQ(unanswered, release + clear + frame);
    }
}

TEST_CASE(an_arm_the_far_end_never_trusts_is_given_up_latched)
{
    /* The line is cut at the coprocessor: it reads low whatever the panel
     * drives. */
    armed_at_70(0, WIRE_LOST);
    op_at(2, 3000, OP_STOP, 0);
    run_until(5000);
    CHECK(link_dev_arm_latched(&d_dev));
    arming_request_arm(&p_arm, T);
    const uint32_t asked = T;
    while (p_arm_gave_up == 0u && (uint32_t)(T - asked) < 2000u) {
        p_pump();
        w_line = false;                      /* the cut */
        p_service_arming();
        p_poll_far_end();
        sleep_ms(5);
    }
    CHECK_EQ(p_arm_gave_up, 1);
    CHECK((uint32_t)(T - asked) >= HEARTBEAT_SETTLE_MS + ARMING_LINE_WAIT_MS);
    CHECK((uint32_t)(T - asked) < HEARTBEAT_SETTLE_MS + ARMING_LINE_WAIT_MS
                                  + 20u);
    CHECK_EQ(p_clear_sent, 1);               /* the first arm's, no second */
    CHECK(link_dev_arm_latched(&d_dev));
    CHECK(!p_arm.armed);
    CHECK(!outputs_armed(&p_out));
    CHECK_EQ(d_arm_edges, 1);
}

TEST_CASE(a_touch_outage_disarms_and_the_next_arm_is_taken)
{
    static const uint32_t outs[] = { 499, 500, 501, 600, 1000 };
    for (unsigned i = 0; i < 5u; ++i) {
        armed_at_70(0, WIRE_LOST);
        p_touch_out_at = 3000u;
        p_touch_out_ms = outs[i];
        op_at(2, 6000, OP_ARM, 0);
        run_until(5900);
        /* ARMING_TOUCH_DEAD_MS is counted from the last answer, which is
         * the pump before the outage: up to 5 ms earlier. */
        const bool disarmed = !outputs_driving(&d_bank);
        if (outs[i] >= 501u) {
            CHECK(disarmed);
            CHECK(!outputs_armed(&p_out));
            CHECK_EQ(p_throttle, 0);
        }
        if (outs[i] <= 494u) {
            CHECK(!disarmed);
        }
        /* The supply's ON is taken again with no arm in between. */
        CHECK(safety_gate_supply_ok(&d_beat, &d_dev));
        run_until(7000);
        CHECK(outputs_driving(&d_bank));
        CHECK_EQ(p_arm_refused, 0);
        CHECK_EQ(p_arm_gave_up, 0);
        CHECK_EQ(p_far_end_stops, 0);
        if (disarmed) {
            CHECK_EQ(d_arm_edges, 2);
            CHECK_EQ(outputs_actual(&d_bank, 0), 0);
        }
    }
}

TEST_CASE(a_keep_alive_nobody_answers_stops_the_panel_in_that_poll)
{
    /* Armed at 70 %.  The poll's read is answered and the coprocessor goes
     * down under the write of ARM and THROTTLE that follows it.  That poll
     * has failed: the stop is latched in it, not at the next poll's
     * timeout a second later. */
    static const wire_t wires[] = { WIRE_LOST, WIRE_BUS_OFF, WIRE_RETX };
    for (unsigned w = 0; w < 3u; ++w) {
        unsigned hit = 0, late = 0, armed = 0;
        for (uint32_t ph = 0; ph < 110u; ++ph) {
            armed_at_70(0, wires[w]);
            e_reset_at = 3000u + ph;
            e_boot_at = e_reset_at + 3000u;
            e_reset_on = e_boot_on = true;
            run_until(3000u + ph + 2500u);
            if (p_write_quiet) {
                ++hit;
                late += !(p_stop_seen
                          && (uint32_t)(p_stop_at - p_write_quiet_at) <= 5u
                          && arming_stopped(&p_arm) && !outputs_armed(&p_out)
                          && p_throttle == 0u
                          && !arming_heartbeat(&p_arm, T));
            }
            run_until(3000u + ph + 9000u);
            armed += d_arm_edges != 1u || outputs_driving(&d_bank)
                     || outputs_armed(&p_out);
        }
        CHECK(hit >= 1u);
        CHECK_EQ(late, 0);
        CHECK_EQ(armed, 0);
    }
}

int main(void)
{
    RUN(a_start_is_latched_and_reports_no_fault);
    RUN(clear_releases_the_latch_and_the_arm_is_taken);
    RUN(a_distrusted_heartbeat_latches_until_clear);
    RUN(a_silent_line_latches_at_the_ceiling_and_not_before);
    RUN(link_silence_latches_at_200_ms_across_the_wrap);
    RUN(a_refused_arm_leaves_the_latch_set);
    RUN(null_arguments_arm_nothing);
    RUN(the_power_up_arm_is_taken_on_the_first_hold);
    RUN(a_restarted_coprocessor_is_not_armed_again);
    RUN(a_restart_across_the_tick_wrap_is_not_armed_again);
    RUN(either_end_alone_holds_a_restart_it_can_see);
    RUN(a_bank_armed_with_no_link_arms_no_far_end_that_appears);
    RUN(a_pulled_cable_stops_the_panel_and_arms_nothing_on_return);
    RUN(a_heartbeat_glitch_stays_disarmed_until_clear);
    RUN(the_first_arm_after_a_stop_is_taken);
    RUN(a_link_that_goes_while_an_arm_waits_is_a_stop);
    RUN(an_arm_the_far_end_never_trusts_is_given_up_latched);
    RUN(a_touch_outage_disarms_and_the_next_arm_is_taken);
    RUN(a_keep_alive_nobody_answers_stops_the_panel_in_that_poll);
    return test_summary("safety_gate");
}
