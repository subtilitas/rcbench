/*
 * The panel's half of the KST page (shared/bench/kst_link.c) end to end:
 * panel module, link dispatch (shared/link/link_dev.c), the programming
 * port and its page (shared/outputs/kst_port.c), the session
 * (protocols/kst) and the simulated servo of kst_sim.h.
 *
 * The clock: 1 ms is 10 passes of the port at 100 us on the wire's clock
 * and one exchange on the link.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "kst_link.h"
#include "kst_port.h"
#include "kst_reg.h"
#include "kst_sim.h"
#include "link_dev.h"
#include "link_msg.h"
#include "tick_wrap.h"

#define CH    3u
#define SLOT  2u
#define PIN   7u
#define CH_THROTTLE 0u

static sim_t g_sim;
static kst_port_t g_port;
static outputs_t g_out;
static link_dev_t g_dev;
static kst_link_t g_k;
static uint32_t g_ms;

static bool g_safe, g_stop;

/* What went over the link. */
static unsigned g_sent;          /* exchanges                              */
static unsigned g_writes;        /* of them writes                         */
static unsigned g_cmds;          /* of them command frames                 */
static uint16_t g_last_cmd[4];
static unsigned g_lose_cmd_reply;   /* answers to a command to lose        */
static unsigned g_lose_any;         /* requests to lose                    */
static uint8_t  g_nack_stage;       /* the answer to a staged window, or 0 */

/* ------------------------------------------------------------- coprocessor */

static bool hw_bound(void *ctx, uint8_t slot)
{
    (void)ctx;
    (void)slot;
    return true;
}

static bool hw_path(void *ctx, uint8_t pin)
{
    (void)ctx;
    (void)pin;
    return true;
}

static bool hw_take(void *ctx, uint8_t slot, uint8_t pin)
{
    (void)ctx;
    (void)slot;
    (void)pin;
    return true;
}

static void hw_give(void *ctx)
{
    (void)ctx;
}

static void page_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    kst_port_read(&g_port, off, n, out);
}

static uint8_t page_write(void *ctx, uint8_t off, uint8_t n,
                          const uint16_t *in)
{
    (void)ctx;
    if (g_nack_stage != 0u && off >= LINK_KS_W_IMAGE) {
        return g_nack_stage;
    }
    return kst_port_write(&g_port, off, n, in, &g_out, g_ms);
}

static const link_page_t k_pages[] = {
    { LINK_PAGE_KST, LINK_KS_COUNT, page_read, page_write },
};

static void port_fresh(void)
{
    kst_port_hw_t hw;

    memset(&hw, 0, sizeof(hw));
    hw.bound = hw_bound;
    hw.reply_path = hw_path;
    hw.take = hw_take;
    hw.give = hw_give;
    hw.driver = sim_driver(&g_sim);
    CHECK(kst_port_init(&g_port, &hw));
}

static void fresh_at(uint32_t t0, uint8_t pages)
{
    sim_init(&g_sim);
    memset(&g_out, 0, sizeof(g_out));
    g_out.slot[0].driver = OUT_DRIVER_PWM;
    g_out.slot[0].first_channel = CH_THROTTLE;
    g_out.slot[0].channels = 1;
    g_out.slot[0].pin = 4;
    g_out.channel[CH_THROTTLE].role = OUT_ROLE_THROTTLE;
    g_out.slot[SLOT].driver = OUT_DRIVER_PWM;
    g_out.slot[SLOT].first_channel = CH;
    g_out.slot[SLOT].channels = 1;
    g_out.slot[SLOT].pin = PIN;
    g_out.channel[CH].role = OUT_ROLE_SURFACE;
    port_fresh();
    g_ms = t0;
    link_dev_init(&g_dev, k_pages, pages, NULL, g_ms);
    kst_link_init(&g_k);
    g_safe = true;
    g_stop = false;
    g_sent = 0;
    g_writes = 0;
    g_cmds = 0;
    g_lose_cmd_reply = 0;
    g_lose_any = 0;
    g_nack_stage = 0;
    memset(g_last_cmd, 0, sizeof(g_last_cmd));
}

static void fresh(void)
{
    fresh_at(0u, 1u);
}

/* ---------------------------------------------------------------- the link */

/* One exchange, when the module owes one. */
static bool exchange(void)
{
    kst_link_xfer_t x;
    link_msg_t req, rep;
    bool cmd;

    if (!kst_link_next(&g_k, g_ms, &x)) {
        return false;
    }
    g_sent++;
    cmd = x.write && x.off == LINK_KS_W_CMD;
    if (x.write) {
        g_writes++;
    }
    if (cmd) {
        g_cmds++;
        memcpy(g_last_cmd, x.regs, sizeof(g_last_cmd));
    }
    CHECK_EQ(x.page, LINK_PAGE_KST);
    CHECK(x.n >= 1u);
    CHECK(!x.write || x.n <= 4u);
    if (g_lose_any > 0u) {
        g_lose_any--;
        kst_link_done(&g_k, KST_LINK_NO_ANSWER, NULL);
        return true;
    }
    memset(&req, 0, sizeof(req));
    memset(&rep, 0, sizeof(rep));
    req.op = x.write ? LINK_OP_WRITE : LINK_OP_READ;
    req.page = x.page;
    req.offset = x.off;
    req.count = x.n;
    if (x.write) {
        memcpy(req.regs, x.regs, x.n * sizeof(x.regs[0]));
    }
    CHECK(link_dev_dispatch(&g_dev, &req, &rep, g_ms));
    if (cmd && g_lose_cmd_reply > 0u) {
        g_lose_cmd_reply--;
        kst_link_done(&g_k, KST_LINK_NO_ANSWER, NULL);
    } else if (rep.op == LINK_OP_DATA) {
        kst_link_done(&g_k, KST_LINK_ACK, rep.regs);
    } else if (rep.op == LINK_OP_ACK) {
        kst_link_done(&g_k, KST_LINK_ACK, NULL);
    } else {
        kst_link_done(&g_k, rep.regs[0], NULL);
    }
    return true;
}

static void ms(void)
{
    for (unsigned i = 0; i < 10u; ++i) {
        kst_port_step(&g_port, g_safe, g_stop, g_ms);
        g_sim.now_us += 100u;
    }
    g_ms++;
    (void)exchange();
}

static void run_ms(unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        ms();
    }
}

/* Run until the module hands an outcome out; 30 s is the bound. */
static kst_link_outcome_t outcome(void)
{
    kst_link_outcome_t out;

    memset(&out, 0, sizeof(out));
    for (unsigned i = 0; i < 30000u; ++i) {
        if (kst_link_outcome(&g_k, &out)) {
            return out;
        }
        ms();
    }
    CHECK(kst_link_outcome(&g_k, &out));
    return out;
}

static void until_phase(kst_link_phase_t p)
{
    for (unsigned i = 0; i < 30000u && kst_link_phase(&g_k) != p; ++i) {
        ms();
    }
    CHECK_EQ(kst_link_phase(&g_k), p);
}

static kst_link_request_t request(link_kst_op_t op)
{
    kst_link_request_t r;

    memset(&r, 0, sizeof(r));
    r.op = op;
    r.channel = CH;
    return r;
}

/* An operation from start to outcome. */
static kst_link_outcome_t run(const kst_link_request_t *r)
{
    kst_link_outcome_t none;

    CHECK(!kst_link_outcome(&g_k, &none));
    CHECK_EQ(kst_link_start(&g_k, r), KST_LINK_START_OK);
    return outcome();
}

static kst_link_outcome_t run_op(link_kst_op_t op)
{
    const kst_link_request_t r = request(op);

    return run(&r);
}

static void link_up(void)
{
    kst_link_came_up(&g_k, LINK_PROTOCOL_MINOR);
    until_phase(KST_LINK_READY);
}

/* Up, entered and read. */
static void up(void)
{
    kst_link_outcome_t out;

    fresh();
    link_up();
    out = run_op(LINK_KST_OP_ENTER);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.result, KST_SES_OK);
    out = run_op(LINK_KST_OP_READ_ALL);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.result, KST_SES_OK);
}

static bool same(const kst_image_t *a, const kst_image_t *b)
{
    return memcmp(a->r, b->r, sizeof(a->r)) == 0;
}

static kst_image_t servo(void)
{
    kst_image_t img;

    memcpy(img.r, g_sim.reg, sizeof(img.r));
    return img;
}

static void conduct(void)
{
    CHECK_EQ(g_sim.n_bad_frames, 0);
    CHECK_EQ(g_sim.n_bad_writes, 0);
    CHECK_EQ(g_sim.n_overlap, 0);
}

/* ---------------------------------------------------------------- the cases */

TEST_CASE(a_session_from_entry_to_the_power_cycle)
{
    kst_link_request_t r;
    kst_link_outcome_t out;
    kst_link_readout_t ro;
    kst_image_t img, backup, target, sv, first;

    fresh();
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_DOWN);
    CHECK(!kst_link_has_page(&g_k));
    kst_link_came_up(&g_k, LINK_PROTOCOL_MINOR);
    CHECK(kst_link_has_page(&g_k));
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_SYNC);
    kst_link_readout(&g_k, &ro);
    CHECK(!ro.known);
    until_phase(KST_LINK_READY);
    kst_link_readout(&g_k, &ro);
    CHECK(ro.known);
    CHECK_EQ(ro.port, LINK_KST_PWM);
    CHECK(!ro.have_image);
    CHECK(!ro.have_results);
    CHECK(!kst_link_image(&g_k, &img));
    CHECK(!kst_link_backup(&g_k, &img));
    CHECK_EQ(g_writes, 0);

    /* Enter: one frame out, then reads. */
    out = run_op(LINK_KST_OP_ENTER);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.op, LINK_KST_OP_ENTER);
    CHECK_EQ(out.result, KST_SES_OK);
    CHECK_EQ(out.fail_reg, 0xFF);
    CHECK(out.frames > 0u);
    CHECK_EQ(g_cmds, 1);
    CHECK_EQ(g_writes, 2);            /* the command and VIEW */
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_READY);
    CHECK(!kst_link_outcome(&g_k, &out));
    run_ms(5);
    kst_link_readout(&g_k, &ro);
    CHECK_EQ(ro.port, LINK_KST_PROGRAMMING);
    CHECK_EQ(ro.channel, CH);
    CHECK((ro.flags & LINK_KS_F_IN_MODE) != 0u);
    CHECK_EQ(ro.op, LINK_KST_OP_ENTER);
    CHECK(ro.have_results);
    CHECK(!ro.have_image);
    CHECK(!ro.have_backup);

    out = run_op(LINK_KST_OP_READ_ALL);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.result, KST_SES_OK);
    CHECK_EQ(out.frames, 96);
    CHECK_EQ(out.bad_regs, 0);
    CHECK(kst_link_image(&g_k, &img));
    first = servo();
    CHECK(same(&img, &first));
    CHECK(kst_link_backup(&g_k, &backup));
    CHECK(same(&backup, &first));
    kst_link_readout(&g_k, &ro);
    CHECK(ro.have_image);
    CHECK(ro.have_backup);
    CHECK_EQ(ro.fp_rules, 0);
    CHECK_EQ(ro.fp_regs, 0);
    CHECK_NEAR(ro.half_min_ns, SIM_HALF_NS, 600);
    CHECK_NEAR(ro.half_max_ns, SIM_HALF_NS, 600);
    CHECK_NEAR(ro.delay_min_us, SIM_READ_DELAY_NS / 1000u, 30);
    CHECK_NEAR(ro.delay_max_us, SIM_READ_DELAY_NS / 1000u, 30);

    /* Write: 6 staged windows and the command. */
    target = img;
    CHECK(kst_field_edit(&target, KST_F_BOOST, 19));
    CHECK(kst_field_edit(&target, KST_F_DEAD_BAND, 50));
    r = request(LINK_KST_OP_WRITE);
    r.write_enabled = true;
    r.image = target;
    g_writes = 0;
    g_cmds = 0;
    out = run(&r);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.op, LINK_KST_OP_WRITE);
    CHECK_EQ(out.result, KST_SES_OK);
    CHECK_EQ(out.steps_done, 2);
    CHECK_EQ(out.diff_regs, 0);
    CHECK_EQ(g_cmds, 1);
    CHECK_EQ(g_last_cmd[LINK_KS_W_KEY], LINK_KST_WRITE_KEY);
    CHECK_EQ(g_writes, 6 + 1 + 2);    /* staged, command, VIEW twice */
    CHECK_EQ(g_sim.n_writes, 2);
    sv = servo();
    CHECK(same(&sv, &target));
    CHECK(kst_link_image(&g_k, &img));
    CHECK(same(&img, &target));
    CHECK(kst_link_backup(&g_k, &backup));
    CHECK(same(&backup, &first));
    /* The page is left showing the image. */
    CHECK((g_port.view) == 0u);

    /* Verify against what was written, then against something else. */
    r = request(LINK_KST_OP_VERIFY);
    r.image = target;
    out = run(&r);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.result, KST_SES_OK);
    CHECK_EQ(g_last_cmd[LINK_KS_W_KEY], 0);
    r.image.r[0x11] ^= 0x04u;
    out = run(&r);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.result, KST_SES_ERR_VERIFY);
    CHECK_EQ(out.fail_reg, 0x11);
    CHECK_EQ(out.diff_regs, 1ul << 0x11);

    /* Restore from the backup the page handed out. */
    r = request(LINK_KST_OP_RESTORE);
    r.write_enabled = true;
    r.image = backup;
    out = run(&r);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.op, LINK_KST_OP_RESTORE);
    CHECK_EQ(out.result, KST_SES_OK);
    sv = servo();
    CHECK(same(&sv, &first));
    CHECK(kst_link_image(&g_k, &img));
    CHECK(same(&img, &first));

    /* The servo was without power: PWM again. */
    out = run_op(LINK_KST_OP_POWER_CYCLED);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.op, LINK_KST_OP_POWER_CYCLED);
    CHECK_EQ(out.result, KST_SES_OK);
    run_ms(5);
    kst_link_readout(&g_k, &ro);
    CHECK_EQ(ro.port, LINK_KST_PWM);
    CHECK((ro.flags & LINK_KS_F_IN_MODE) == 0u);
    CHECK_EQ(kst_port_state(&g_port), LINK_KST_PWM);
    conduct();
}

TEST_CASE(a_release_of_pairing_is_planned_at_the_coprocessor)
{
    kst_link_request_t r;
    kst_link_outcome_t out;
    kst_image_t img;

    up();
    r = request(LINK_KST_OP_RELEASE);
    r.write_enabled = true;
    /* The request's image is not sent: the planner's target is the page's. */
    memset(r.image.r, 0xEE, sizeof(r.image.r));
    out = run(&r);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.result, KST_SES_OK);
    CHECK(out.steps_done > 0u);
    CHECK_EQ(g_sim.n_writes, out.steps_done);
    CHECK(kst_link_image(&g_k, &img));
    CHECK_EQ(img.r[0x1D], g_sim.reg[0x1D]);
    conduct();
}

TEST_CASE(a_write_without_the_switch_sends_nothing)
{
    const link_kst_op_t writes[3] = { LINK_KST_OP_WRITE, LINK_KST_OP_RESTORE,
                                      LINK_KST_OP_RELEASE };
    kst_link_outcome_t out;
    kst_image_t img;
    unsigned sent_writes, seq;

    up();
    CHECK(kst_link_image(&g_k, &img));
    CHECK(kst_field_edit(&img, KST_F_DEAD_BAND, 50));
    sent_writes = g_writes;
    seq = g_port.seq;
    for (unsigned i = 0; i < 3u; ++i) {
        kst_link_request_t r = request(writes[i]);

        r.image = img;
        CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_NO_ENABLE);
        CHECK_EQ(kst_link_phase(&g_k), KST_LINK_READY);
        run_ms(200);
        CHECK(!kst_link_outcome(&g_k, &out));
    }
    CHECK_EQ(g_writes, sent_writes);
    CHECK_EQ(g_port.seq, seq);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(g_sim.reg[0x05], 0x77);
}

TEST_CASE(nothing_is_sent_to_a_coprocessor_of_protocol_4_11)
{
    kst_link_request_t r = request(LINK_KST_OP_ENTER);
    kst_link_outcome_t out;
    kst_link_xfer_t x;
    kst_link_readout_t ro;

    fresh();
    kst_link_came_up(&g_k, LINK_MINOR_KST - 1u);
    CHECK(!kst_link_has_page(&g_k));
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_NO_PAGE);
    CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_NO_PAGE);
    CHECK_EQ(kst_link_fetch(&g_k), KST_LINK_START_NO_PAGE);
    kst_link_abort(&g_k);
    run_ms(2000);
    CHECK(!kst_link_next(&g_k, g_ms, &x));
    CHECK_EQ(g_sent, 0);
    CHECK_EQ(g_dev.requests, 0);
    CHECK(!kst_link_outcome(&g_k, &out));
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_NO_PAGE);
    kst_link_readout(&g_k, &ro);
    CHECK_EQ(ro.phase, KST_LINK_NO_PAGE);
    CHECK(!ro.known);
    CHECK_EQ(kst_port_state(&g_port), LINK_KST_PWM);

    /* The same panel on a 4.12 coprocessor. */
    kst_link_came_up(&g_k, LINK_MINOR_KST);
    CHECK(kst_link_has_page(&g_k));
    until_phase(KST_LINK_READY);
}

TEST_CASE(a_coprocessor_that_refuses_the_page_is_left_alone)
{
    kst_link_request_t r = request(LINK_KST_OP_ENTER);
    kst_link_outcome_t out;

    /* It reports 4.12 and answers BAD_PAGE. */
    fresh_at(0u, 0u);
    kst_link_came_up(&g_k, LINK_MINOR_KST);
    run_ms(2000);
    CHECK_EQ(g_sent, 1);
    CHECK_EQ(g_writes, 0);
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_NO_PAGE);
    CHECK(!kst_link_has_page(&g_k));
    CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_NO_PAGE);
    CHECK(!kst_link_outcome(&g_k, &out));
}

TEST_CASE(start_answers_why_it_sends_nothing)
{
    kst_link_request_t r;
    kst_image_t img;

    fresh();
    r = request(LINK_KST_OP_ENTER);
    CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_DOWN);
    CHECK_EQ(kst_link_fetch(&g_k), KST_LINK_START_DOWN);
    CHECK_EQ(kst_link_start(NULL, &r), KST_LINK_START_ARG);
    CHECK_EQ(kst_link_start(&g_k, NULL), KST_LINK_START_ARG);
    CHECK_EQ(kst_link_fetch(NULL), KST_LINK_START_ARG);
    kst_link_came_up(&g_k, LINK_MINOR_KST);
    CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_BUSY);
    CHECK_EQ(kst_link_fetch(&g_k), KST_LINK_START_BUSY);
    until_phase(KST_LINK_READY);

    r = request(LINK_KST_OP_NONE);
    CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_ARG);
    r = request(LINK_KST_OP_ABORT);
    CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_ARG);
    r = request(LINK_KST_OP_COUNT);
    CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_ARG);
    r = request(LINK_KST_OP_ENTER);
    r.channel = 16;
    CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_ARG);
    r = request(LINK_KST_OP_WRITE);
    r.write_enabled = true;
    r.confirm = true;
    CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_ARG);
    r.confirm = false;
    CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_NO_IMAGE);
    r.op = LINK_KST_OP_RELEASE;
    CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_NO_IMAGE);
    CHECK_EQ(g_writes, 0);
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_READY);

    /* One operation at a time. */
    r = request(LINK_KST_OP_ENTER);
    CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_OK);
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_SENDING);
    CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_BUSY);
    until_phase(KST_LINK_RUNNING);
    CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_BUSY);
    CHECK_EQ(kst_link_fetch(&g_k), KST_LINK_START_BUSY);
    CHECK_EQ(outcome().kind, KST_LINK_OUT_DONE);
    CHECK_EQ(g_cmds, 1);

    /* Null arguments to the rest. */
    kst_link_init(NULL);
    kst_link_lost(NULL);
    kst_link_came_up(NULL, 12);
    kst_link_abort(NULL);
    kst_link_done(NULL, KST_LINK_ACK, NULL);
    kst_link_readout(NULL, NULL);
    CHECK(!kst_link_has_page(NULL));
    CHECK(!kst_link_next(NULL, 0, NULL));
    CHECK(!kst_link_outcome(NULL, NULL));
    CHECK(!kst_link_image(NULL, &img));
    CHECK(!kst_link_backup(&g_k, NULL));
    CHECK_EQ(kst_link_phase(NULL), KST_LINK_DOWN);
    /* A report with no exchange owed. */
    kst_link_done(&g_k, KST_LINK_ACK, NULL);
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_READY);
}

TEST_CASE(the_page_says_why_a_command_started_nothing)
{
    kst_link_request_t r;
    kst_link_outcome_t out;

    fresh();
    link_up();
    r = request(LINK_KST_OP_ENTER);
    r.channel = CH_THROTTLE;
    out = run(&r);
    CHECK_EQ(out.kind, KST_LINK_OUT_REFUSED);
    CHECK_EQ(out.op, LINK_KST_OP_ENTER);
    CHECK_EQ(out.refusal, LINK_KST_REF_THROTTLE);
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_READY);
    out = run_op(LINK_KST_OP_READ_ALL);
    CHECK_EQ(out.kind, KST_LINK_OUT_REFUSED);
    CHECK_EQ(out.refusal, LINK_KST_REF_NOT_PROGRAMMING);
    g_safe = false;
    run_ms(2);
    out = run_op(LINK_KST_OP_ENTER);
    CHECK_EQ(out.kind, KST_LINK_OUT_REFUSED);
    CHECK_EQ(out.refusal, LINK_KST_REF_UNSAFE);
    g_safe = true;
    run_ms(2);
    CHECK_EQ(kst_port_state(&g_port), LINK_KST_PWM);
    CHECK_EQ(g_sim.n_frames, 0);
    out = run_op(LINK_KST_OP_ENTER);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.result, KST_SES_OK);
}

TEST_CASE(a_write_from_an_image_the_coprocessor_does_not_hold_is_refused)
{
    kst_link_request_t r;
    kst_link_outcome_t out;

    up();
    r = request(LINK_KST_OP_WRITE);
    r.write_enabled = true;
    CHECK(kst_link_image(&g_k, &r.image));
    CHECK(kst_field_edit(&r.image, KST_F_DEAD_BAND, 50));
    /* The panel's copy is not what the session read. */
    g_k.image_regs[1] ^= 0x0100u;
    out = run(&r);
    CHECK_EQ(out.kind, KST_LINK_OUT_REFUSED);
    CHECK_EQ(out.refusal, LINK_KST_REF_START);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(g_sim.reg[0x05], 0x77);
}

TEST_CASE(a_command_whose_answer_is_lost_is_sent_again_and_taken_once)
{
    kst_link_outcome_t out;
    unsigned seq;

    up();
    seq = g_port.seq;
    g_cmds = 0;
    g_lose_cmd_reply = 2;
    out = run_op(LINK_KST_OP_READ_ALL);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.result, KST_SES_OK);
    CHECK_EQ(g_cmds, 3);
    CHECK_EQ(g_port.seq, seq + 1u);
    CHECK_EQ(g_sim.n_reads, 2u * 96u);
    conduct();
}

TEST_CASE(three_exchanges_without_an_answer_give_the_operation_up)
{
    kst_link_outcome_t out;
    kst_link_readout_t ro;
    kst_image_t img;

    up();
    g_lose_any = KST_LINK_RETRIES;
    out = run_op(LINK_KST_OP_READ_ALL);
    CHECK_EQ(out.kind, KST_LINK_OUT_NO_ANSWER);
    CHECK_EQ(out.op, LINK_KST_OP_READ_ALL);
    CHECK_EQ(g_sim.n_reads, 96);
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_SYNC);
    kst_link_readout(&g_k, &ro);
    CHECK(!ro.known);
    CHECK(kst_link_image(&g_k, &img));
    /* The page is read again before anything else. */
    until_phase(KST_LINK_READY);
    out = run_op(LINK_KST_OP_READ_ALL);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.result, KST_SES_OK);

    /* Two lost and the third answered is no failure. */
    g_lose_any = KST_LINK_RETRIES - 1u;
    out = run_op(LINK_KST_OP_READ_ALL);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);

    /* The command taken and every answer after it lost: the operation
     * runs at the coprocessor, and the page says so when it is read. */
    CHECK_EQ(kst_link_start(&g_k, &(kst_link_request_t){
                 .op = LINK_KST_OP_READ_ALL, .channel = CH }),
             KST_LINK_START_OK);
    until_phase(KST_LINK_RUNNING);
    g_lose_any = KST_LINK_RETRIES;
    out = outcome();
    CHECK_EQ(out.kind, KST_LINK_OUT_NO_ANSWER);
    until_phase(KST_LINK_READY);
    run_ms(3000);
    kst_link_readout(&g_k, &ro);
    CHECK_EQ(ro.op, LINK_KST_OP_READ_ALL);
    CHECK((ro.flags & LINK_KS_F_BUSY) == 0u);
    conduct();
}

TEST_CASE(an_unanswered_page_is_asked_again_without_an_outcome)
{
    kst_link_outcome_t out;

    fresh();
    kst_link_came_up(&g_k, LINK_MINOR_KST);
    g_lose_any = 7;
    run_ms(7u * KST_LINK_POLL_MS - 10u);
    CHECK_EQ(g_sent, 7);
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_SYNC);
    CHECK(!kst_link_outcome(&g_k, &out));
    until_phase(KST_LINK_READY);

    /* A read acknowledged without registers counts as no answer. */
    kst_link_lost(&g_k);
    kst_link_came_up(&g_k, LINK_MINOR_KST);
    for (unsigned i = 0; i < 4u; ++i) {
        kst_link_xfer_t x;

        CHECK(kst_link_next(&g_k, g_ms + i * KST_LINK_POLL_MS, &x));
        CHECK(!x.write);
        kst_link_done(&g_k, KST_LINK_ACK, NULL);
        CHECK_EQ(kst_link_phase(&g_k), KST_LINK_SYNC);
    }
}

TEST_CASE(a_command_with_a_stale_seq_is_refused_and_the_page_read_again)
{
    kst_link_outcome_t out;

    up();
    run_ms(2);
    /* The page's SEQ moved without the panel, and no read has shown it. */
    g_port.seq = (uint16_t)(g_port.seq + 5u);
    out = run_op(LINK_KST_OP_READ_ALL);
    CHECK_EQ(out.kind, KST_LINK_OUT_NACK);
    CHECK_EQ(out.nack, LINK_NACK_BAD_VALUE);
    CHECK_EQ(g_sim.n_reads, 96);
    until_phase(KST_LINK_READY);
    out = run_op(LINK_KST_OP_READ_ALL);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.result, KST_SES_OK);
}

TEST_CASE(a_staged_window_the_page_refuses_ends_the_operation)
{
    kst_link_request_t r;
    kst_link_outcome_t out;

    up();
    r = request(LINK_KST_OP_VERIFY);
    CHECK(kst_link_image(&g_k, &r.image));
    g_cmds = 0;
    g_nack_stage = LINK_NACK_BAD_RANGE;
    out = run(&r);
    CHECK_EQ(out.kind, KST_LINK_OUT_NACK);
    CHECK_EQ(out.nack, LINK_NACK_BAD_RANGE);
    CHECK_EQ(g_cmds, 0);
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_READY);
    g_nack_stage = 0;
    out = run(&r);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.result, KST_SES_OK);
}

TEST_CASE(an_abort_before_the_command_drops_the_request)
{
    kst_link_request_t r;
    kst_link_outcome_t out;
    kst_link_readout_t ro;

    up();
    r = request(LINK_KST_OP_WRITE);
    r.write_enabled = true;
    CHECK(kst_link_image(&g_k, &r.image));
    CHECK(kst_field_edit(&r.image, KST_F_DEAD_BAND, 50));
    g_cmds = 0;
    CHECK_EQ(kst_link_start(&g_k, &r), KST_LINK_START_OK);
    run_ms(3);
    CHECK_EQ(g_cmds, 0);
    kst_link_abort(&g_k);
    CHECK(kst_link_outcome(&g_k, &out));
    CHECK_EQ(out.kind, KST_LINK_OUT_DROPPED);
    CHECK_EQ(out.op, LINK_KST_OP_WRITE);
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_READY);
    run_ms(200);
    /* The one command that went is the abort. */
    CHECK_EQ(g_cmds, 1);
    CHECK_EQ(g_last_cmd[LINK_KS_W_CMD] & 0xFu, LINK_KST_OP_ABORT);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK(!kst_link_outcome(&g_k, &out));
    kst_link_readout(&g_k, &ro);
    CHECK_EQ(ro.op, LINK_KST_OP_READ_ALL);
    run_ms(2000);
    CHECK_EQ(g_cmds, 1);
}

TEST_CASE(an_abort_ends_the_operation_that_runs)
{
    kst_link_outcome_t out;
    kst_link_readout_t ro;
    unsigned frames;

    up();
    g_cmds = 0;
    CHECK_EQ(kst_link_start(&g_k, &(kst_link_request_t){
                 .op = LINK_KST_OP_READ_ALL, .channel = CH }),
             KST_LINK_START_OK);
    until_phase(KST_LINK_RUNNING);
    run_ms(40);
    frames = g_sim.n_frames;
    kst_link_abort(&g_k);
    ms();
    CHECK_EQ(g_cmds, 2);
    out = outcome();
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.op, LINK_KST_OP_READ_ALL);
    CHECK_EQ(out.result, KST_SES_ERR_ABORTED);
    CHECK(g_sim.n_frames <= frames + 1u);
    run_ms(5);
    kst_link_readout(&g_k, &ro);
    CHECK((ro.flags & LINK_KS_F_MUST_READ) != 0u);
    CHECK_EQ(ro.port, LINK_KST_PROGRAMMING);
    CHECK_EQ(g_cmds, 2);
    conduct();
}

TEST_CASE(an_abort_with_a_stale_seq_is_sent_again)
{
    up();
    run_ms(2);
    g_cmds = 0;
    g_port.seq = (uint16_t)(g_port.seq + 9u);
    kst_link_abort(&g_k);
    run_ms(300);
    /* Refused, the page read, sent again and taken. */
    CHECK_EQ(g_cmds, 2);
    CHECK_EQ(g_last_cmd[LINK_KS_W_SEQ], g_port.seq);
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_READY);
    run_ms(2000);
    CHECK_EQ(g_cmds, 2);
}

TEST_CASE(a_stop_at_the_coprocessor_shows_as_an_aborted_operation)
{
    kst_link_outcome_t out;

    up();
    CHECK_EQ(kst_link_start(&g_k, &(kst_link_request_t){
                 .op = LINK_KST_OP_READ_ALL, .channel = CH }),
             KST_LINK_START_OK);
    until_phase(KST_LINK_RUNNING);
    run_ms(40);
    g_stop = true;
    ms();
    g_stop = false;
    out = outcome();
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.result, KST_SES_ERR_ABORTED);

    /* What the screen is told when it writes next. */
    out = run(&(kst_link_request_t){ .op = LINK_KST_OP_VERIFY,
                                     .channel = CH });
    CHECK_EQ(out.kind, KST_LINK_OUT_REFUSED);
    CHECK_EQ(out.refusal, LINK_KST_REF_MUST_READ);
    conduct();
}

TEST_CASE(a_coprocessor_that_started_again_ends_the_operation)
{
    kst_link_outcome_t out;
    kst_link_readout_t ro;

    up();
    CHECK_EQ(kst_link_start(&g_k, &(kst_link_request_t){
                 .op = LINK_KST_OP_READ_ALL, .channel = CH }),
             KST_LINK_START_OK);
    until_phase(KST_LINK_RUNNING);
    run_ms(20);
    port_fresh();
    out = outcome();
    CHECK_EQ(out.kind, KST_LINK_OUT_RESTARTED);
    CHECK_EQ(out.op, LINK_KST_OP_READ_ALL);
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_READY);
    kst_link_readout(&g_k, &ro);
    CHECK_EQ(ro.port, LINK_KST_PWM);

    /* Between operations it is no outcome. */
    out = run_op(LINK_KST_OP_ENTER);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    port_fresh();
    run_ms(200);
    CHECK(!kst_link_outcome(&g_k, &out));
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_READY);
    kst_link_readout(&g_k, &ro);
    CHECK_EQ(ro.port, LINK_KST_PWM);
}

TEST_CASE(the_sequence_counter_wraps_under_the_panel)
{
    kst_link_request_t r;
    kst_link_outcome_t out;

    fresh();
    g_port.seq = 0xFFFEu;
    link_up();
    out = run_op(LINK_KST_OP_ENTER);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(g_last_cmd[LINK_KS_W_SEQ], 0xFFFF);
    out = run_op(LINK_KST_OP_READ_ALL);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.result, KST_SES_OK);
    CHECK_EQ(g_last_cmd[LINK_KS_W_SEQ], 0x0000);
    CHECK_EQ(g_port.seq, 0);
    r = request(LINK_KST_OP_VERIFY);
    CHECK(kst_link_image(&g_k, &r.image));
    out = run(&r);
    CHECK_EQ(out.kind, KST_LINK_OUT_DONE);
    CHECK_EQ(out.result, KST_SES_OK);
    CHECK_EQ(g_last_cmd[LINK_KS_W_SEQ], 0x0001);
}

TEST_CASE(a_lost_link_ends_the_operation_and_keeps_the_images)
{
    kst_link_outcome_t out;
    kst_link_xfer_t x;
    kst_image_t img;

    up();
    CHECK_EQ(kst_link_start(&g_k, &(kst_link_request_t){
                 .op = LINK_KST_OP_READ_ALL, .channel = CH }),
             KST_LINK_START_OK);
    until_phase(KST_LINK_RUNNING);
    kst_link_lost(&g_k);
    CHECK(kst_link_outcome(&g_k, &out));
    CHECK_EQ(out.kind, KST_LINK_OUT_NO_ANSWER);
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_DOWN);
    CHECK(!kst_link_has_page(&g_k));
    CHECK(!kst_link_next(&g_k, g_ms + 1000u, &x));
    CHECK(kst_link_image(&g_k, &img));
    CHECK(kst_link_backup(&g_k, &img));
    kst_link_abort(&g_k);
    CHECK(!kst_link_next(&g_k, g_ms + 2000u, &x));

    /* Lost with nothing on its way: no outcome. */
    kst_link_lost(&g_k);
    CHECK(!kst_link_outcome(&g_k, &out));
    kst_link_came_up(&g_k, LINK_MINOR_KST);
    until_phase(KST_LINK_READY);
    CHECK(!kst_link_outcome(&g_k, &out));
}

TEST_CASE(a_panel_that_starts_on_a_held_port_fetches_what_the_page_has)
{
    kst_link_outcome_t out;
    kst_link_readout_t ro;
    kst_image_t img, sv;

    up();
    /* The panel started again; the coprocessor did not. */
    kst_link_init(&g_k);
    link_up();
    kst_link_readout(&g_k, &ro);
    CHECK_EQ(ro.port, LINK_KST_PROGRAMMING);
    CHECK((ro.flags & LINK_KS_F_IMAGE) != 0u);
    CHECK(!ro.have_image);
    CHECK_EQ(kst_link_fetch(&g_k), KST_LINK_START_OK);
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_FETCHING);
    until_phase(KST_LINK_READY);
    CHECK(!kst_link_outcome(&g_k, &out));
    CHECK(kst_link_image(&g_k, &img));
    sv = servo();
    CHECK(same(&img, &sv));
    CHECK(kst_link_backup(&g_k, &img));
    kst_link_readout(&g_k, &ro);
    CHECK(ro.have_results);
    CHECK(ro.have_image);
}

/* --- the poll times across the wrap of the millisecond clock ------------------ */

static void status_regs(link_kst_state_t st, uint16_t *regs)
{
    kst_port_read(&g_port, 0, LINK_KS_W_CMD_FRAME, regs);
    regs[LINK_KS_STATE] = (uint16_t)((regs[LINK_KS_STATE] & ~7u)
                                     | (unsigned)st);
}

static void poll_case(uint32_t t0)
{
    kst_link_xfer_t x;
    uint16_t regs[LINK_KS_W_CMD_FRAME];
    uint32_t t = t0;

    fresh_at(t0, 1u);
    kst_link_came_up(&g_k, LINK_MINOR_KST);

    /* No answer: asked again KST_LINK_POLL_MS later. */
    CHECK(kst_link_next(&g_k, t, &x));
    CHECK(!x.write);
    CHECK_EQ(x.off, 0);
    CHECK_EQ(x.n, 4);
    kst_link_done(&g_k, KST_LINK_NO_ANSWER, NULL);
    CHECK(!kst_link_next(&g_k, t + 1u, &x));
    CHECK(!kst_link_next(&g_k, t + KST_LINK_POLL_MS - 1u, &x));
    t += KST_LINK_POLL_MS;
    CHECK(kst_link_next(&g_k, t, &x));
    status_regs(LINK_KST_PWM, regs);
    kst_link_done(&g_k, KST_LINK_ACK, regs);
    CHECK_EQ(kst_link_phase(&g_k), KST_LINK_READY);

    /* In PWM: KST_LINK_IDLE_MS. */
    CHECK(!kst_link_next(&g_k, t + 1u, &x));
    CHECK(!kst_link_next(&g_k, t + KST_LINK_IDLE_MS - 1u, &x));
    t += KST_LINK_IDLE_MS;
    CHECK(kst_link_next(&g_k, t, &x));
    status_regs(LINK_KST_PROGRAMMING, regs);
    kst_link_done(&g_k, KST_LINK_ACK, regs);

    /* With a channel held: KST_LINK_POLL_MS. */
    CHECK(!kst_link_next(&g_k, t + 1u, &x));
    CHECK(!kst_link_next(&g_k, t + KST_LINK_POLL_MS - 1u, &x));
    t += KST_LINK_POLL_MS;
    CHECK(kst_link_next(&g_k, t, &x));
    CHECK(!x.write);
    /* Nothing more while that one is in flight. */
    CHECK(!kst_link_next(&g_k, t + 10000u, &x));
}

TEST_CASE(the_first_read_is_repeated_on_time_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(poll_case, 20u);
}

TEST_CASE(the_idle_poll_holds_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(poll_case, KST_LINK_POLL_MS + 300u);
}

TEST_CASE(the_poll_of_a_held_port_holds_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(
        poll_case, KST_LINK_POLL_MS + KST_LINK_IDLE_MS + 20u);
}

int main(void)
{
    RUN(a_session_from_entry_to_the_power_cycle);
    RUN(a_release_of_pairing_is_planned_at_the_coprocessor);
    RUN(a_write_without_the_switch_sends_nothing);
    RUN(nothing_is_sent_to_a_coprocessor_of_protocol_4_11);
    RUN(a_coprocessor_that_refuses_the_page_is_left_alone);
    RUN(start_answers_why_it_sends_nothing);
    RUN(the_page_says_why_a_command_started_nothing);
    RUN(a_write_from_an_image_the_coprocessor_does_not_hold_is_refused);
    RUN(a_command_whose_answer_is_lost_is_sent_again_and_taken_once);
    RUN(three_exchanges_without_an_answer_give_the_operation_up);
    RUN(an_unanswered_page_is_asked_again_without_an_outcome);
    RUN(a_command_with_a_stale_seq_is_refused_and_the_page_read_again);
    RUN(a_staged_window_the_page_refuses_ends_the_operation);
    RUN(an_abort_before_the_command_drops_the_request);
    RUN(an_abort_ends_the_operation_that_runs);
    RUN(an_abort_with_a_stale_seq_is_sent_again);
    RUN(a_stop_at_the_coprocessor_shows_as_an_aborted_operation);
    RUN(a_coprocessor_that_started_again_ends_the_operation);
    RUN(the_sequence_counter_wraps_under_the_panel);
    RUN(a_lost_link_ends_the_operation_and_keeps_the_images);
    RUN(a_panel_that_starts_on_a_held_port_fetches_what_the_page_has);
    RUN(the_first_read_is_repeated_on_time_across_the_tick_wrap);
    RUN(the_idle_poll_holds_across_the_tick_wrap);
    RUN(the_poll_of_a_held_port_holds_across_the_tick_wrap);
    return test_summary("kst_link");
}
