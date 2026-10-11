/*
 * The servo programmer's state (shared/bench/kst_prog.c), end to end:
 * the programmer, the panel's link module, link dispatch, the programming
 * port, the session and the simulated servo of kst_sim.h.  The control
 * task of the panel is the function frame() here.
 *
 * Cases that need an answer the real chain gives only by accident (every
 * refusal, every result, a command nobody takes) feed the programmer by
 * hand.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "kst_fake_card.h"
#include "kst_link.h"
#include "kst_port.h"
#include "kst_prog.h"
#include "kst_reg.h"
#include "kst_sim.h"
#include "link_dev.h"
#include "link_msg.h"

#define CH    0u          /* the programming port the servo is on */
#define PIN   13u
#define VIN_MV 20000u     /* the PD mini's input */

static sim_t g_sim;
static kst_port_t g_port;
static link_dev_t g_dev;
static kst_link_t g_k;
static kst_prog_t g_p;
static uint32_t g_ms;
static bool g_safe, g_stop;

/* The PD mini as the port sees it; it reads what it is asked at once. */
static bool g_rail;            /* a PD mini answers */
static bool g_rail_deaf;       /* it takes no request */
static bool g_rail_stuck;      /* it takes a request and does not follow */
static bool g_no_pin;          /* the generator has no pin for the port */
static bool g_sup_on;
static uint16_t g_sup_mv, g_sup_ma;
static uint8_t g_target;       /* LINK_PG_TARGET as the control task reads */

/* The control task's side of the programmer. */
static uint32_t g_outcomes;
static kst_link_outcome_t g_outcome;
static uint32_t g_answer_id;
static kst_link_start_t g_answer;
static bool g_stopped;
static bool g_deaf;            /* the control task takes no command */
static unsigned g_starts;      /* kst_link_start() calls            */
static unsigned g_aborts;
static unsigned g_fetches;

/* The card. */
static bool g_card, g_card_fails, g_card_silent;
static char g_file[KST_PROG_FILE_MAX];
static size_t g_file_len;
static unsigned g_file_writes;
static unsigned g_starts_at_file;   /* g_starts when the file was written */
static fake_card_t g_fc;            /* the servo files */
static unsigned g_starts_at_save;   /* g_starts when a save was asked */

/* ------------------------------------------------------------- coprocessor */

static uint8_t hw_pin(void *ctx, uint8_t port)
{
    (void)ctx;
    return (port == CH && !g_no_pin) ? PIN : 0xFFu;
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
    memset(out, 0, sizeof(*out));
    out->online = g_rail;
    out->asked_on = g_sup_on;
    out->on = g_rail && g_sup_on && !g_rail_stuck;
    out->off = !g_sup_on && !g_rail_stuck;
    out->set_mv = g_sup_mv;
    out->set_ma = g_sup_ma;
    out->vin_mv = g_rail ? VIN_MV : 0u;
}

static bool hw_supply_set(void *ctx, bool on, uint16_t mv, uint16_t ma)
{
    (void)ctx;
    if (!g_rail || g_rail_deaf) {
        return false;
    }
    g_sup_on = on;
    if (on) {
        g_sup_mv = mv;
        g_sup_ma = ma;
    }
    return true;
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
    return kst_port_write(&g_port, off, n, in, g_ms);
}

static const link_page_t k_pages[] = {
    { LINK_PAGE_KST, LINK_KS_COUNT, page_read, page_write },
};

static void port_fresh(void)
{
    kst_port_hw_t hw;

    memset(&hw, 0, sizeof(hw));
    hw.ports = 1u;
    hw.pin = hw_pin;
    hw.take = hw_take;
    hw.give = hw_give;
    hw.supply = hw_supply;
    hw.supply_set = hw_supply_set;
    hw.driver = sim_driver(&g_sim);
    CHECK(kst_port_init(&g_port, &hw));
}

static void fresh_at(uint32_t t0)
{
    sim_init(&g_sim);
    g_rail = false;
    g_rail_deaf = false;
    g_rail_stuck = false;
    g_no_pin = false;
    g_sup_on = false;
    g_sup_mv = 0;
    g_sup_ma = 0;
    g_target = 1u + CH;
    port_fresh();
    g_ms = t0;
    link_dev_init(&g_dev, k_pages, 1u, NULL, g_ms);
    kst_link_init(&g_k);
    kst_prog_init(&g_p);
    g_safe = true;
    g_stop = false;
    g_outcomes = 0;
    memset(&g_outcome, 0, sizeof(g_outcome));
    g_answer_id = 0;
    g_answer = KST_LINK_START_OK;
    g_stopped = false;
    g_deaf = false;
    g_starts = 0;
    g_aborts = 0;
    g_fetches = 0;
    g_card = true;
    g_card_fails = false;
    g_card_silent = false;
    fake_card_init(&g_fc);
    g_starts_at_save = 0;
    g_file_len = 0;
    g_file_writes = 0;
    g_starts_at_file = 0;
}

/* One exchange on the link, when the module owes one. */
static void exchange(void)
{
    kst_link_xfer_t x;
    link_msg_t req, rep;

    if (!kst_link_next(&g_k, g_ms, &x)) {
        return;
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
    if (rep.op == LINK_OP_DATA) {
        kst_link_done(&g_k, KST_LINK_ACK, rep.regs);
    } else if (rep.op == LINK_OP_ACK) {
        kst_link_done(&g_k, KST_LINK_ACK, NULL);
    } else {
        kst_link_done(&g_k, rep.regs[0], NULL);
    }
}

static void publish(void)
{
    kst_prog_feed_t f;

    memset(&f, 0, sizeof(f));
    f.now_ms = g_ms;
    kst_link_readout(&g_k, &f.rd);
    f.have_image = kst_link_image(&g_k, &f.image);
    f.have_backup = kst_link_backup(&g_k, &f.backup);
    f.answer_id = g_answer_id;
    f.answer = g_answer;
    f.outcomes = g_outcomes;
    f.outcome = g_outcome;
    f.stopped = g_stopped;
    f.card = g_card;
    f.target = g_target;
    kst_prog_feed(&g_p, &f);
}

/* 1 ms: the coprocessor, one exchange, the control task, the screen's
 * frame and the card. */
static void frame(void)
{
    kst_prog_cmd_t cmd;
    kst_link_outcome_t o;
    kst_prog_card_req_t creq;
    kst_prog_card_done_t cdone;
    char text[KST_PROG_FILE_MAX];
    size_t len;

    for (unsigned i = 0; i < 10u; ++i) {
        kst_port_step(&g_port, g_safe, g_stop, g_ms);
        g_sim.now_us += 100u;
    }
    g_ms++;
    exchange();
    if (!g_deaf && kst_prog_take(&g_p, &cmd)) {
        if (cmd.kind == KST_PROG_CMD_START) {
            g_starts++;
            g_answer = kst_link_start(&g_k, &cmd.req);
            g_answer_id = cmd.id;
        } else if (cmd.kind == KST_PROG_CMD_FETCH) {
            g_fetches++;
            (void)kst_link_fetch(&g_k);
        } else {
            g_aborts++;
            kst_link_abort(&g_k);
        }
    }
    if (kst_link_outcome(&g_k, &o)) {
        g_outcome = o;
        g_outcomes++;
    }
    publish();
    len = kst_prog_file_take(&g_p, text, sizeof(text));
    if (len != 0u) {
        g_file_writes++;
        g_starts_at_file = g_starts;
        if (!g_card_fails) {
            memcpy(g_file, text, len + 1u);
            g_file_len = len;
        }
        if (!g_card_silent) {
            kst_prog_file_done(&g_p, !g_card_fails);
        }
    }
    if (kst_prog_card_take(&g_p, &creq)) {
        if (creq.op == KST_PROG_CARD_SAVE) {
            g_starts_at_save = g_starts;
        }
        if (fake_card_run(&g_fc, &creq, &cdone)) {
            kst_prog_card_done(&g_p, &cdone);
        }
    }
}

static void frames(unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        frame();
    }
}

static bool at_rest(void)
{
    const kst_prog_state_t st = kst_prog_state(&g_p);

    return st == KST_PROG_PWM || st == KST_PROG_PROGRAMMING
        || st == KST_PROG_DOWN || st == KST_PROG_NO_PAGE;
}

/* Until nothing runs; 60 s is the bound. */
static void settle(void)
{
    frame();
    for (unsigned i = 0; i < 60000u && !at_rest(); ++i) {
        frame();
    }
    CHECK(at_rest());
}

static unsigned msg(void)
{
    return kst_prog_message(&g_p)->id;
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

static void link_up(void)
{
    kst_link_came_up(&g_k, LINK_PROTOCOL_MINOR);
    settle();
}

/* Up, connected and read. */
static void up(void)
{
    fresh_at(0u);
    link_up();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
}

/* The same with the switch on. */
static void up_enabled(void)
{
    up();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK(kst_prog_write_enabled(&g_p));
}

static uint16_t draft(kst_field_id_t id)
{
    return kst_field_get(kst_prog_draft(&g_p), id);
}

/* An edit, a soft rule confirmed. */
static void edit(kst_field_id_t id, uint16_t raw)
{
    const kst_prog_edit_t e = kst_prog_edit(&g_p, id, raw);

    CHECK(e != KST_PROG_EDIT_REFUSED);
    if (e == KST_PROG_EDIT_SOFT) {
        kst_prog_soft_confirm(&g_p);
    }
    CHECK_EQ(draft(id), raw);
}

/* --------------------------------------------------- the chain, end to end */

TEST_CASE(a_connect_enters_and_reads_the_servo)
{
    const kst_image_t sv = sim_bench_image();

    fresh_at(0u);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    CHECK_EQ(kst_prog_op(&g_p), LINK_KST_OP_NONE);
    CHECK_EQ(kst_prog_message(&g_p)->id, KST_PROG_MSG_NONE);
    publish();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    kst_link_came_up(&g_k, LINK_PROTOCOL_MINOR);
    publish();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_SYNC);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK(kst_prog_first_write(&g_p));

    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_ENTERING);
    CHECK_EQ(kst_prog_op(&g_p), LINK_KST_OP_ENTER);
    /* Nothing else is taken on the way. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_START + KST_LINK_START_BUSY);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_INFO);
    CHECK(kst_prog_have_servo(&g_p));
    CHECK(kst_prog_editable(&g_p));
    CHECK(same(kst_prog_servo(&g_p), &sv));
    CHECK(same(kst_prog_draft(&g_p), &sv));
    CHECK_EQ(kst_prog_port(&g_p), CH);
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK(!kst_prog_in_doubt(&g_p));
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);

    /* A second connect: the port is taken. */
    CHECK_EQ(kst_prog_connect(&g_p, CH),
             KST_PROG_MSG_START + KST_LINK_START_BUSY);
}

TEST_CASE(a_coprocessor_without_the_page_is_named)
{
    fresh_at(0u);
    kst_link_came_up(&g_k, LINK_MINOR_KST - 1u);
    frames(50);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_NO_PAGE);
    CHECK_EQ(kst_prog_connect(&g_p, CH),
             KST_PROG_MSG_START + KST_LINK_START_NO_PAGE);
    CHECK_EQ(kst_prog_read(&g_p),
             KST_PROG_MSG_START + KST_LINK_START_NO_PAGE);
    CHECK_EQ(g_starts, 0);
    frames(10);
    CHECK_EQ(g_starts, 0);

    kst_link_lost(&g_k);
    frames(2);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    CHECK_EQ(kst_prog_connect(&g_p, CH),
             KST_PROG_MSG_START + KST_LINK_START_DOWN);
    CHECK_EQ(kst_prog_leave(&g_p),
             KST_PROG_MSG_START + KST_LINK_START_DOWN);
}

TEST_CASE(the_port_says_why_a_port_is_not_taken)
{
    fresh_at(0u);
    link_up();
    /* The build has 1 port. */
    g_target = CH + 2u;
    frame();
    CHECK_EQ(kst_prog_connect(&g_p, CH + 1u), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_PORT);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    /* The port holds no pin. */
    g_target = CH + 1u;
    frame();
    g_no_pin = true;
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_NO_PIN);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    /* No read follows an entry that was refused. */
    CHECK_EQ(g_starts, 2);
}

TEST_CASE(no_servo_on_the_port_is_a_message)
{
    fresh_at(0u);
    g_sim.powered = false;
    link_up();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_NO_SERVO);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK_EQ(g_starts, 1);
    /* What needs a servo read says so. */
    if (kst_prog_state(&g_p) == KST_PROG_PROGRAMMING) {
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NEED_READ);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NEED_READ);
        CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NEED_READ);
        CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
        settle();
    }
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NEED_PORT);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, 200), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_READ);
}

TEST_CASE(an_edit_inside_the_limits_changes_the_draft_only)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t boost = kst_field_get(&sv, KST_F_BOOST);

    up();
    CHECK(boost > 1u);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_BOOST, (uint16_t)(boost - 1u)),
             KST_PROG_EDIT_OK);
    CHECK_EQ(draft(KST_F_BOOST), boost - 1u);
    CHECK(same(kst_prog_servo(&g_p), &sv));
    CHECK_EQ(kst_prog_changed(&g_p), KST_FIELD_BIT(KST_F_BOOST));
    CHECK_EQ(kst_prog_changes(&g_p), 1);
    CHECK_EQ(g_sim.n_writes, 0);

    /* Max. Duty takes its copy along and counts once. */
    edit(KST_F_DUTY, 200);
    CHECK_EQ(draft(KST_F_DUTY_COPY), 200);
    CHECK_EQ(kst_prog_changes(&g_p), 2);

    kst_prog_discard(&g_p);
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    CHECK(same(kst_prog_draft(&g_p), &sv));
}

TEST_CASE(a_value_beyond_a_hard_limit_is_not_taken)
{
    const kst_image_t sv = sim_bench_image();

    up();
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, KST_DUTY_RAW_MAX + 1u),
             KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_EDIT_HARD);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_H_DUTY_HIGH);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, KST_DUTY_RAW_MIN - 1u),
             KST_PROG_EDIT_REFUSED);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_H_DUTY_LOW);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_PULSE_LOWER, KST_PULSE_RAW_MIN - 1u),
             KST_PROG_EDIT_REFUSED);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_H_LOWER_LOW);
    /* A value the field's bits do not hold. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_SOFT_START, 2),
             KST_PROG_EDIT_REFUSED);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_RULE_COUNT);
    CHECK(same(kst_prog_draft(&g_p), &sv));
    CHECK_EQ(kst_prog_soft_rules(&g_p), 0);

    /* Fields outside the editable class. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_20K_SEL, 1), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_EDIT_LOCKED);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_GYRO, 1), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_EDIT_LOCKED);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_NODE_ADDR, 0), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_EDIT_LOCKED);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY_COPY, 200),
             KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_EDIT_LOCKED);
    CHECK(same(kst_prog_draft(&g_p), &sv));
}

TEST_CASE(a_value_beyond_a_soft_limit_waits_for_the_confirmation)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t duty = kst_field_get(&sv, KST_F_DUTY);

    up();
    CHECK(duty + 2u <= KST_DUTY_RAW_MAX);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 1u)),
             KST_PROG_EDIT_SOFT);
    CHECK((kst_prog_soft_rules(&g_p) & KST_RULE_BIT(KST_S_DUTY_RAISED))
          != 0u);
    CHECK_EQ(kst_field_get(kst_prog_soft_image(&g_p), KST_F_DUTY), duty + 1u);
    CHECK_EQ(draft(KST_F_DUTY), duty);
    kst_prog_soft_cancel(&g_p);
    CHECK_EQ(kst_prog_soft_rules(&g_p), 0);
    CHECK_EQ(draft(KST_F_DUTY), duty);
    kst_prog_soft_confirm(&g_p);     /* nothing waits: nothing happens */
    CHECK_EQ(draft(KST_F_DUTY), duty);

    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 1u)),
             KST_PROG_EDIT_SOFT);
    kst_prog_soft_confirm(&g_p);
    CHECK_EQ(draft(KST_F_DUTY), duty + 1u);
    CHECK_EQ(kst_prog_soft_rules(&g_p), 0);
    CHECK((kst_prog_rules(&g_p) & KST_RULE_BIT(KST_S_DUTY_RAISED)) != 0u);

    /* A rule confirmed once is not asked again while it stays broken. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 2u)),
             KST_PROG_EDIT_OK);
    /* Back at the servo's value it is asked again on the next raise. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, duty), KST_PROG_EDIT_OK);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 1u)),
             KST_PROG_EDIT_SOFT);
    /* Another edit drops the one that waits. */
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_BOOST,
                           (uint16_t)(kst_field_get(&sv, KST_F_BOOST) - 1u)),
             KST_PROG_EDIT_OK);
    CHECK_EQ(kst_prog_soft_rules(&g_p), 0);
    CHECK_EQ(draft(KST_F_DUTY), duty);
}

TEST_CASE(the_range_of_a_field_is_what_the_hard_rules_take)
{
    uint16_t lo = 0, hi = 0;

    fresh_at(0u);
    CHECK(!kst_prog_range(&g_p, KST_F_DUTY, &lo, &hi));
    up();
    CHECK(kst_prog_range(&g_p, KST_F_DUTY, &lo, &hi));
    CHECK_EQ(lo, KST_DUTY_RAW_MIN);
    CHECK_EQ(hi, KST_DUTY_RAW_MAX);
    CHECK(kst_prog_range(&g_p, KST_F_BOOST, &lo, &hi));
    CHECK_EQ(lo, 0);
    CHECK_EQ(hi, KST_BOOST_RAW_MAX);
    CHECK(kst_prog_range(&g_p, KST_F_STRETCH, &lo, &hi));
    CHECK_EQ(lo, 0);
    CHECK_EQ(hi, KST_STRETCH_INDEX_MAX);
    CHECK(kst_prog_range(&g_p, KST_F_SOFT_START, &lo, &hi));
    CHECK_EQ(lo, 0);
    CHECK_EQ(hi, 1);
    CHECK(kst_prog_range(&g_p, KST_F_UNCONT_TIME, &lo, &hi));
    CHECK_EQ(lo, KST_UNCONT_TIME_RAW_MIN);
    CHECK_EQ(hi, KST_UNCONT_TIME_RAW_MAX);
    CHECK(kst_prog_range(&g_p, KST_F_NEUTRAL, &lo, &hi));
    CHECK(lo >= KST_NEUTRAL_RAW_MIN);
    CHECK(hi <= KST_NEUTRAL_RAW_MAX);
    CHECK(lo < hi);
    /* Every end is taken and the value beyond it is not. */
    for (unsigned id = 0; id < KST_F_COUNT; ++id) {
        const kst_field_id_t f = (kst_field_id_t)id;
        const kst_image_t before = *kst_prog_draft(&g_p);

        if (!kst_prog_range(&g_p, f, &lo, &hi)) {
            CHECK(kst_field(f)->access != KST_ACCESS_EDITABLE
                  || f == KST_F_DUTY_COPY);
            continue;
        }
        CHECK(kst_prog_edit(&g_p, f, lo) != KST_PROG_EDIT_REFUSED);
        kst_prog_soft_cancel(&g_p);
        CHECK(kst_prog_edit(&g_p, f, hi) != KST_PROG_EDIT_REFUSED);
        kst_prog_soft_cancel(&g_p);
        if (lo > 0u) {
            CHECK_EQ(kst_prog_edit(&g_p, f, (uint16_t)(lo - 1u)),
                     KST_PROG_EDIT_REFUSED);
        }
        if (hi < kst_field_raw_max(f)) {
            CHECK_EQ(kst_prog_edit(&g_p, f, (uint16_t)(hi + 1u)),
                     KST_PROG_EDIT_REFUSED);
        }
        g_p.draft = before;
    }
    CHECK(!kst_prog_range(&g_p, KST_F_LOCK, &lo, &hi));
    CHECK(!kst_prog_range(&g_p, KST_F_DUTY_COPY, &lo, &hi));
}

TEST_CASE(a_range_is_found_from_a_draft_value_the_rules_refuse)
{
    uint16_t lo = 0, hi = 0;

    up();
    /* A servo whose own Max. Duty lies above the limit: the rule it breaks
     * already does not narrow the range. */
    g_p.servo.r[KST_REG_DUTY] = 0xFFu;
    g_p.servo.r[KST_REG_DUTY_COPY] = 0xFFu;
    g_p.draft = g_p.servo;
    CHECK(kst_prog_range(&g_p, KST_F_DUTY, &lo, &hi));
    CHECK_EQ(lo, KST_DUTY_RAW_MIN);
    CHECK_EQ(hi, 255);
    /* The planner takes no write of it until the value is inside. */
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_BOOST, 10), KST_PROG_EDIT_OK);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_EDIT_HARD);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_H_DUTY_HIGH);
    kst_prog_enable(&g_p, false);
    /* A draft value outside the run the rules take. */
    g_p.servo = sim_bench_image();
    g_p.draft.r[KST_REG_DUTY] = 0xFFu;
    g_p.draft.r[KST_REG_DUTY_COPY] = 0xFFu;
    CHECK(kst_prog_range(&g_p, KST_F_DUTY, &lo, &hi));
    CHECK_EQ(lo, KST_DUTY_RAW_MIN);
    CHECK_EQ(hi, KST_DUTY_RAW_MAX);
}

TEST_CASE(a_step_moves_one_count_and_stops_at_the_fields_end)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t boost = kst_field_get(&sv, KST_F_BOOST);

    up();
    CHECK_EQ(kst_prog_step(&g_p, KST_F_BOOST, -1), KST_PROG_EDIT_OK);
    CHECK_EQ(draft(KST_F_BOOST), boost - 1u);
    CHECK_EQ(kst_prog_step(&g_p, KST_F_BOOST, 1), KST_PROG_EDIT_OK);
    CHECK_EQ(draft(KST_F_BOOST), boost);
    CHECK_EQ(kst_prog_step(&g_p, KST_F_BOOST, 1), KST_PROG_EDIT_SOFT);
    kst_prog_soft_cancel(&g_p);

    edit(KST_F_SOFT_START, 1);
    CHECK_EQ(kst_prog_step(&g_p, KST_F_SOFT_START, 1),
             KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_EDIT_HARD);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_RULE_COUNT);
    edit(KST_F_SOFT_START, 0);
    CHECK_EQ(kst_prog_step(&g_p, KST_F_SOFT_START, -1),
             KST_PROG_EDIT_REFUSED);
    CHECK_EQ(draft(KST_F_SOFT_START), 0);
}

TEST_CASE(a_write_needs_the_switch_and_writes_the_changed_fields)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t boost = (uint16_t)(kst_field_get(&sv, KST_F_BOOST) - 2u);
    kst_image_t now;

    up();
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NO_CHANGES);
    edit(KST_F_BOOST, boost);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write_check(&g_p), KST_PROG_MSG_NEED_ENABLE);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NEED_ENABLE);
    CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_NEED_ENABLE);
    frames(20);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);

    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK(kst_prog_first_write(&g_p));
    CHECK_EQ(kst_prog_write_check(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    CHECK_EQ(kst_prog_op(&g_p), LINK_KST_OP_WRITE);
    /* The servo's file first: nothing is in doubt before it is there. */
    CHECK(!kst_prog_in_doubt(&g_p));
    frame();
    CHECK(kst_prog_in_doubt(&g_p));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 2);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_INFO);
    now = servo();
    CHECK_EQ(kst_field_get(&now, KST_F_BOOST), boost);
    CHECK_EQ(now.r[KST_REG_DUTY], 200);
    CHECK_EQ(now.r[KST_REG_DUTY_COPY], 200);
    CHECK(same(kst_prog_servo(&g_p), &now));
    CHECK(same(kst_prog_draft(&g_p), &now));
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    CHECK(!kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_journal(&g_p));
    CHECK(!kst_prog_first_write(&g_p));
    /* The switch stays on for the servo that is connected. */
    CHECK(kst_prog_write_enabled(&g_p));
    CHECK_EQ(g_sim.n_bad_writes, 0);

    kst_prog_enable(&g_p, false);
    CHECK(!kst_prog_write_enabled(&g_p));
}

/* Up, the switch on, one value written. */
static void up_written(void)
{
    up_enabled();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK(!kst_prog_first_write(&g_p));
}

TEST_CASE(the_first_write_of_every_session_is_the_first_again)
{
    /* The session is left and another entered. */
    up_written();
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(kst_prog_first_write(&g_p));
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(kst_prog_first_write(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
    /* And its first write ends that. */
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 210);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(!kst_prog_first_write(&g_p));

    /* The link is lost between two operations. */
    up_written();
    kst_link_lost(&g_k);
    frame();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    CHECK(kst_prog_first_write(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));

    /* The coprocessor starts again between two operations. */
    up_written();
    port_fresh();
    frames(600);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(kst_prog_first_write(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
}

TEST_CASE(a_draft_the_planner_refuses_is_not_sent)
{
    up_enabled();
    /* Past the editor: a pairing value in the draft. */
    g_p.draft.r[KST_REG_NODE_ADDR] ^= 0x01u;
    CHECK(kst_prog_write(&g_p) != KST_PROG_MSG_NONE);
    g_p.draft = g_p.servo;
    g_p.draft.r[0] = 0x01u;
    CHECK(kst_prog_write(&g_p) != KST_PROG_MSG_NONE);
    g_p.draft = g_p.servo;
    g_p.draft.r[KST_REG_DUTY] = 0xFFu;
    g_p.draft.r[KST_REG_DUTY_COPY] = 0xFFu;
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_EDIT_HARD);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_H_DUTY_HIGH);
    frames(20);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);
}

TEST_CASE(the_switch_goes_off_where_the_rules_say)
{
    up_enabled();
    kst_prog_screen_entry(&g_p);
    CHECK(!kst_prog_write_enabled(&g_p));

    /* STOP. */
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    g_stopped = true;
    frame();
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_STOPPED);
    CHECK(!kst_prog_write_enabled(&g_p));
    g_stopped = false;
    frame();

    /* A read that finds another image. */
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    g_sim.reg[0x03] ^= 0x01u;
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(!kst_prog_write_enabled(&g_p));
    /* A read of the same servo leaves it. */
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(kst_prog_write_enabled(&g_p));

    /* The link. */
    kst_link_lost(&g_k);
    frame();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK_EQ(kst_prog_enable(&g_p, true),
             KST_PROG_MSG_START + KST_LINK_START_DOWN);
}

TEST_CASE(leaving_gives_the_output_back_and_ends_the_session)
{
    up_enabled();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_LEAVING);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NEED_PORT);
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NEED_PORT);

    /* And in again. */
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(kst_prog_first_write(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
}

TEST_CASE(a_verify_says_whether_the_servo_holds_what_was_read)
{
    up();
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_VERIFIED);

    g_sim.reg[0x03] ^= 0x01u;
    g_sim.reg[0x10] ^= 0x01u;
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_VERIFY_DIFF);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 2);
    /* What is held is no longer the servo: read first. */
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(!kst_prog_in_doubt(&g_p));
}

TEST_CASE(a_servo_that_fails_the_fingerprint_is_read_only)
{
    fresh_at(0u);
    g_sim.reg[0x06] = 0x05u;
    link_up();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ_ONLY);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
    CHECK(kst_prog_have_servo(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_READ_ONLY);
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, 200), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_READ_ONLY);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_READ_ONLY);
    CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_READ_ONLY);
    /* Reading, keeping and comparing stay. */
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_VERIFIED);
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NONE);
    frames(3);
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    CHECK(kst_prog_backed_up(&g_p));
    CHECK_EQ(g_sim.n_writes, 0);
}

TEST_CASE(a_release_of_pairing_writes_00_to_register_1d)
{
    up();
    CHECK(g_sim.reg[KST_REG_NODE_ADDR] != 0u);
    CHECK_EQ(kst_prog_release_check(&g_p), KST_PROG_MSG_NEED_ENABLE);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_release_check(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RELEASED);
    CHECK_EQ(g_sim.reg[KST_REG_NODE_ADDR], 0);
    CHECK_EQ(kst_prog_servo(&g_p)->r[KST_REG_NODE_ADDR], 0);
    CHECK(!kst_prog_journal(&g_p));
    CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_NO_CHANGES);
}

/* ------------------------------------------------------------- the backups */

TEST_CASE(the_sessions_backup_is_restored_by_the_coprocessor)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    kst_image_t now;

    up_enabled();
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NO_RESTORE);
    edit(KST_F_DUTY, 200);
    edit(KST_F_SOFT_START, 1u - kst_field_get(&sv, KST_F_SOFT_START));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);

    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK_EQ(plan.from, KST_PROG_FROM_SESSION);
    CHECK_EQ(plan.writes, 3);
    CHECK(!plan.unchecked);
    kst_prog_enable(&g_p, false);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NEED_ENABLE);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_op(&g_p), LINK_KST_OP_RESTORE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = servo();
    CHECK(same(&now, &sv));
    CHECK(same(kst_prog_servo(&g_p), &sv));
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
}

/* -------------------------------------------------------------- the journal */

TEST_CASE(the_journal_reaches_the_card_before_the_write_is_sent)
{
    char at_write[KST_PROG_FILE_MAX];

    up_enabled();
    g_card = true;
    frame();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    /* The servo's file, then the journal, then the command. */
    frame();
    CHECK_EQ(g_fc.saves, 1);
    CHECK_EQ(g_file_writes, 0);
    frame();
    CHECK_EQ(g_file_writes, 1);
    CHECK_EQ(g_starts_at_file, 2);
    CHECK(strstr(g_file,
                 "journal=00F5F5144877046541A40D80530714009696000101"
                 "0000000000000A00100000\n") != NULL);
    CHECK(strstr(g_file,
                 "target=00C8C81448770465") != NULL);
    CHECK(strstr(g_file, "\nport=0\ncheck=") != NULL);
    CHECK(strstr(g_file, "\n00=") == NULL);
    memcpy(at_write, g_file, sizeof(at_write));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    /* Verified: the journal leaves the file. */
    frames(2);
    CHECK_EQ(g_file_writes, 2);
    CHECK(strstr(g_file, "journal") == NULL);
    CHECK_STR_EQ(g_file, "RCBENCH KST 1\ncheck=72DB\n");

    /* A panel that starts on the file of the write: the servo is in
     * doubt until it is read. */
    kst_prog_init(&g_p);
    CHECK(kst_prog_file_load(&g_p, at_write, strlen(at_write)));
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_DANGER);
    CHECK(kst_prog_journal(&g_p));
    CHECK(kst_prog_in_doubt(&g_p));
}

TEST_CASE(a_card_that_takes_no_journal_stops_the_write)
{
    up_enabled();
    g_card = true;
    g_card_fails = true;
    frame();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_STORE_FAILED);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK(!kst_prog_journal(&g_p));
    CHECK(!kst_prog_in_doubt(&g_p));
    CHECK_EQ(kst_prog_changes(&g_p), 1);
    /* No write started: the next one is still the session's first. */
    CHECK(kst_prog_first_write(&g_p));

    /* A card that never answers: KST_PROG_STORE_MS. */
    g_card_fails = false;
    g_card_silent = true;
    g_p.file_busy = false;
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frames(KST_PROG_STORE_MS - 2u);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    frames(3);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK_EQ(msg(), KST_PROG_MSG_STORE_FAILED);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK(kst_prog_first_write(&g_p));
    /* Its late answer starts nothing. */
    kst_prog_file_done(&g_p, true);
    frames(5);
    CHECK_EQ(g_starts, 2);
    kst_prog_file_done(&g_p, true);   /* nothing on its way: ignored */
}

/* A write of two fields cut by a lost link after its first register;
 * with @p duty the cut leaves registers 01 and 02 apart. */
static void cut_write_of(bool duty)
{
    up_enabled();
    if (duty) {
        edit(KST_F_DUTY, 200);
    } else {
        edit(KST_F_SOFT_START,
             1u - kst_field_get(kst_prog_servo(&g_p), KST_F_SOFT_START));
    }
    edit(KST_F_BOOST, 10);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    for (unsigned i = 0; i < 30000u && g_sim.n_writes == 0u; ++i) {
        frame();
    }
    CHECK(g_sim.n_writes > 0u);
    kst_link_lost(&g_k);
    g_safe = false;
    frame();
}

static void cut_write(void)
{
    cut_write_of(true);
}

TEST_CASE(a_link_lost_in_a_write_leaves_the_servo_in_doubt)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    kst_image_t now;

    cut_write();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_DANGER);
    CHECK(kst_prog_journal(&g_p));
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK(!kst_prog_have_servo(&g_p));

    /* The coprocessor stopped the write when the link went silent, and
     * its session takes the restore only. */
    frames(200);
    g_safe = true;
    kst_link_came_up(&g_k, LINK_PROTOCOL_MINOR);
    settle();
    now = servo();
    CHECK(!same(&now, &sv));
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NEED_READ);
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));

    /* The read compares the servo with the image before the write. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_DIFF);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_DANGER);
    CHECK(!kst_prog_journal(&g_p));
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    /* A second read says that the session is locked. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_LOCKED);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_JOURNAL, &plan),
             KST_PROG_MSG_NEED_READ);

    /* The session's own backup goes back. */
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK(plan.unchecked);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = servo();
    CHECK(same(&now, &sv));
    CHECK(!kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(g_sim.n_bad_writes, 0);
}

/* The cut write, then a new session on the servo as the cut left it. */
static void cut_and_come_back(bool duty)
{
    cut_write_of(duty);
    frames(200);
    g_safe = true;
    kst_link_came_up(&g_k, LINK_PROTOCOL_MINOR);
    settle();
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(kst_prog_journal(&g_p));
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_DIFF);
    CHECK(!kst_prog_in_doubt(&g_p));
}

TEST_CASE(a_later_session_cannot_write_to_a_servo_cut_between_01_and_02)
{
    kst_prog_restore_t plan;

    cut_and_come_back(true);
    CHECK(g_sim.reg[KST_REG_DUTY] != g_sim.reg[KST_REG_DUTY_COPY]);
    /* The fingerprint fails, and the session takes no edit. */
    CHECK(!kst_prog_editable(&g_p));
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_JOURNAL, &plan),
             KST_PROG_MSG_READ_ONLY);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_READ_ONLY);
}

TEST_CASE(a_panel_that_starts_on_a_locked_session_fetches_its_backup)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    kst_image_t now;

    cut_write();
    frames(200);
    /* The panel starts again: its link module and its programmer hold
     * nothing, the journal comes from the card. */
    {
        char text[KST_PROG_FILE_MAX];

        CHECK(kst_prog_file_text(&g_p, text, sizeof(text)) != 0u);
        kst_link_init(&g_k);
        kst_prog_init(&g_p);
        g_outcomes = 0;
        g_answer_id = 0;
        CHECK(kst_prog_file_load(&g_p, text, strlen(text)));
    }
    g_safe = true;
    kst_link_came_up(&g_k, LINK_PROTOCOL_MINOR);
    settle();
    frames(600);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK_EQ(g_fetches, 1);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK(kst_prog_in_doubt(&g_p));
    /* The port asks for a read before anything else. */
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_DIFF);
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK(plan.unchecked);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = servo();
    CHECK(same(&now, &sv));
    CHECK(!kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_journal(&g_p));
}

TEST_CASE(a_later_session_writes_the_journals_image_back)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    kst_image_t now;

    cut_and_come_back(false);
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    /* A read after it says no more than that it read. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));

    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_JOURNAL, &plan),
             KST_PROG_MSG_NONE);
    CHECK(plan.writes >= 1u);
    CHECK_EQ(plan.stay, 0);
    /* A plan is a count: the pending values take nothing of the record
     * until the restore starts, and a restore that is refused leaves
     * them alone too. */
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NEED_ENABLE);
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = servo();
    CHECK(same(&now, &sv));
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));
    CHECK_EQ(g_sim.n_bad_writes, 0);
}

TEST_CASE(a_second_cut_keeps_the_image_before_the_first_write)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;

    cut_and_come_back(false);
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_JOURNAL, &plan),
             KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    frames(3);          /* the servo's file, the journal, the command */
    kst_link_lost(&g_k);
    frame();
    CHECK(kst_prog_journal(&g_p));
    CHECK(same(&g_p.journal_img, &sv));
}

TEST_CASE(a_journal_whose_servo_is_unchanged_or_complete_is_closed)
{
    const kst_image_t sv = sim_bench_image();
    kst_image_t target;
    char text[KST_PROG_FILE_MAX];

    /* Unchanged: the write never reached the servo. */
    up();
    g_card = true;
    frame();
    g_p.journal = true;
    g_p.journal_open = true;
    g_p.journal_port = CH;
    g_p.journal_img = sv;
    g_p.journal_target = sv;
    g_p.journal_target.r[0x03] ^= 0x01u;
    CHECK(kst_prog_file_text(&g_p, text, sizeof(text)) != 0u);
    kst_prog_init(&g_p);
    /* A text cut short is not whole, and asking changes nothing. */
    CHECK(kst_prog_file_whole(text, strlen(text)));
    CHECK(!kst_prog_file_whole(text, strlen(text) - 1u));
    CHECK(!kst_prog_file_whole(text, 0u));
    CHECK_EQ(msg(), KST_PROG_MSG_NONE);
    CHECK(!g_p.journal);
    CHECK(kst_prog_file_load(&g_p, text, strlen(text)));
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
    CHECK_EQ(kst_prog_message(&g_p)->arg, CH + 1u);
    CHECK_EQ(kst_prog_journal_port(&g_p), CH);
    frame();
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_SAME);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_INFO);
    CHECK(!kst_prog_journal(&g_p));
    CHECK(!kst_prog_in_doubt(&g_p));
    frames(2);
    CHECK_STR_EQ(g_file, "RCBENCH KST 1\ncheck=72DB\n");

    /* Complete: every register of the target is on the servo. */
    target = sv;
    target.r[0x03] ^= 0x01u;
    memcpy(g_sim.reg, target.r, sizeof(target.r));
    kst_prog_init(&g_p);
    CHECK(kst_prog_file_load(&g_p, text, strlen(text)));
    frame();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_DONE);
    CHECK(!kst_prog_journal(&g_p));

    /* Forgotten by the operator. */
    kst_prog_forget(&g_p);
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_DONE);
    CHECK(kst_prog_file_load(&g_p, text, strlen(text)));
    kst_prog_forget(&g_p);
    CHECK_EQ(msg(), KST_PROG_MSG_FORGOTTEN);
    CHECK(!kst_prog_journal(&g_p));
    CHECK(!kst_prog_in_doubt(&g_p));
}

/* The other value of 20k_sel, a field no edit and no restore writes. */
static void flip_20k(void)
{
    kst_image_t img = servo();

    CHECK(kst_field_set(&img, KST_F_20K_SEL,
                        1u - kst_field_get(&img, KST_F_20K_SEL)));
    memcpy(g_sim.reg, img.r, sizeof(img.r));
}

TEST_CASE(a_servo_that_is_not_the_journals_takes_no_restore_and_no_write)
{
    kst_prog_restore_t plan;
    kst_image_t before;

    /* The servo of the interrupted write, read and compared. */
    cut_and_come_back(false);
    CHECK(!kst_prog_journal_other(&g_p));
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));

    /* Another servo on the port: a locked value is not the same. */
    flip_20k();
    before = servo();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_OTHER);
    CHECK_EQ(kst_prog_message(&g_p)->arg, CH + 1u);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_DANGER);
    CHECK(kst_prog_journal_other(&g_p));
    CHECK(kst_prog_journal(&g_p));
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK(kst_prog_have_servo(&g_p));
    for (unsigned from = 0; from < (unsigned)KST_PROG_FROM_COUNT; ++from) {
        CHECK(!kst_prog_restore_offered(&g_p, (kst_prog_from_t)from));
        CHECK_EQ(kst_prog_restore_plan(&g_p, (kst_prog_from_t)from, &plan),
                 KST_PROG_MSG_JOURNAL_OTHER);
        plan.from = (uint8_t)from;
        CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_JOURNAL_OTHER);
    }
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_write_check(&g_p), KST_PROG_MSG_JOURNAL_OTHER);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_JOURNAL_OTHER);
    CHECK_EQ(kst_prog_message(&g_p)->arg, CH + 1u);
    CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_JOURNAL_OTHER);
    frames(50);
    {
        const kst_image_t now = servo();

        CHECK(same(&now, &before));
    }

    /* The journal's servo again: compared, and the restore is back. */
    flip_20k();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_DIFF);
    CHECK(!kst_prog_journal_other(&g_p));
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_JOURNAL));

    /* The other servo, and the record is discarded for it. */
    flip_20k();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(kst_prog_journal_other(&g_p));
    kst_prog_forget(&g_p);
    CHECK_EQ(msg(), KST_PROG_MSG_FORGOTTEN);
    CHECK(!kst_prog_journal_other(&g_p));
    CHECK(!kst_prog_in_doubt(&g_p));
    CHECK(kst_prog_editable(&g_p));
}

TEST_CASE(a_journal_of_another_port_is_not_this_servos)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    char text[KST_PROG_FILE_MAX];

    /* The journal of a write on the next port, the servo here equal
     * to its image in every register. */
    up();
    g_card = true;
    frame();
    g_p.journal = true;
    g_p.journal_open = true;
    g_p.journal_port = CH + 1u;
    g_p.journal_img = sv;
    g_p.journal_target = sv;
    g_p.journal_target.r[0x03] ^= 0x01u;
    CHECK(kst_prog_file_text(&g_p, text, sizeof(text)) != 0u);
    kst_prog_init(&g_p);
    CHECK(kst_prog_file_load(&g_p, text, strlen(text)));
    CHECK_EQ(kst_prog_message(&g_p)->arg, CH + 2u);
    frame();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_OTHER);
    CHECK_EQ(kst_prog_message(&g_p)->arg, CH + 2u);
    /* The record stands for the servo it names. */
    CHECK(kst_prog_journal(&g_p));
    CHECK(kst_prog_journal_other(&g_p));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_JOURNAL, &plan),
             KST_PROG_MSG_JOURNAL_OTHER);

    /* The session ends: which servo comes next is not known. */
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(kst_prog_journal(&g_p));
    CHECK(!kst_prog_journal_other(&g_p));
}

TEST_CASE(a_restore_names_the_soft_limits_its_values_pass)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t duty = kst_field_get(&sv, KST_F_DUTY);
    kst_prog_restore_t plan;

    /* Max. Duty lowered by a write: the session's backup raises it. */
    up_enabled();
    edit(KST_F_DUTY, (uint16_t)(duty - 2u));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK_EQ(plan.soft, KST_RULE_BIT(KST_S_DUTY_RAISED));

    /* Raised by a write: the session's backup lowers it, nothing to warn
     * of. */
    up_enabled();
    edit(KST_F_DUTY, (uint16_t)(duty + 2u));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK_EQ(plan.soft, 0);
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_COUNT, &plan),
             KST_PROG_MSG_NO_RESTORE);
}

TEST_CASE(a_coprocessor_that_starts_again_in_a_write_leaves_the_journal)
{
    up_enabled();
    edit(KST_F_DUTY, 200);
    edit(KST_F_BOOST, 10);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    for (unsigned i = 0; i < 30000u && g_sim.n_writes == 0u; ++i) {
        frame();
    }
    port_fresh();
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(kst_prog_journal(&g_p));
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));

    /* Between operations a restart ends the session without a word. */
    up_enabled();
    port_fresh();
    frames(600);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
}

TEST_CASE(a_write_the_servo_does_not_take_names_the_register)
{
    const kst_image_t sv = sim_bench_image();
    kst_image_t now;

    up_enabled();
    edit(KST_F_BOOST, 10);
    sim_add_fault(&g_sim, SIM_WRITE, 0x03, 0, 100, SIM_F_WRITE_IGNORE, 0);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_WRITE_FAILED);
    CHECK_EQ(kst_prog_message(&g_p)->reg, 0x03);
    now = servo();
    CHECK(same(&now, &sv));
    /* Nothing was written: no journal stands. */
    CHECK(!kst_prog_journal(&g_p));
    CHECK_EQ(kst_prog_changes(&g_p), 1);
}

TEST_CASE(a_torn_servo_takes_the_sessions_restore_only)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    kst_image_t now;

    up_enabled();
    edit(KST_F_DUTY, 200);
    /* Register 01 takes the value, 02 takes nothing, and the roll-back of
     * 01 lands nowhere either. */
    sim_add_fault(&g_sim, SIM_WRITE, 0x02, 0, 100, SIM_F_WRITE_IGNORE, 0);
    sim_add_fault(&g_sim, SIM_WRITE, 0x01, 1, 100, SIM_F_WRITE_IGNORE, 0);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    now = servo();
    if (msg() != KST_PROG_MSG_SES + KST_SES_ERR_TORN) {
        /* The planner wrote 02 first: bend the other way round. */
        up_enabled();
        edit(KST_F_DUTY, 200);
        sim_add_fault(&g_sim, SIM_WRITE, 0x01, 0, 100, SIM_F_WRITE_IGNORE,
                      0);
        sim_add_fault(&g_sim, SIM_WRITE, 0x02, 1, 100, SIM_F_WRITE_IGNORE,
                      0);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        settle();
        now = servo();
    }
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_TORN);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_DANGER);
    CHECK(!same(&now, &sv));
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_NEED_READ);

    /* The session's backup is offered, and its result is the operator's
     * to confirm. */
    g_sim.n_faults = 0;
    CHECK(kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK(plan.unchecked);
    CHECK_EQ(plan.writes, 0xFF);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = servo();
    CHECK(same(&now, &sv));
    CHECK(!kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_journal(&g_p));
    CHECK(kst_prog_editable(&g_p));
}

/* ------------------------------------------------------------ stop and abort */

TEST_CASE(a_stop_aborts_the_operation_and_asks_for_a_read)
{
    up_enabled();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    frames(60);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    /* The panel's STOP: the control task aborts and its heartbeat ends. */
    g_stopped = true;
    kst_link_abort(&g_k);
    g_safe = false;
    g_stop = true;
    frame();
    g_stop = false;
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_ABORTED);
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_STOPPED);
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_STOPPED);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_STOPPED);

    /* Leaving stays possible. */
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_STOPPED);

    /* STOP released. */
    g_stopped = false;
    g_safe = true;
    frame();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(!kst_prog_in_doubt(&g_p));
}

TEST_CASE(a_stop_before_the_command_is_taken_sends_nothing)
{
    up_enabled();
    g_card = true;
    g_card_silent = true;
    frame();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frame();
    g_stopped = true;
    frame();
    CHECK_EQ(msg(), KST_PROG_MSG_STOPPED);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(!kst_prog_journal(&g_p));
    frames(20);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK(kst_prog_first_write(&g_p));
}

TEST_CASE(the_operators_abort_ends_an_operation_that_runs)
{
    up();
    kst_prog_abort(&g_p);            /* nothing runs: nothing happens */
    frames(5);
    CHECK_EQ(g_aborts, 0);

    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    frames(60);
    kst_prog_abort(&g_p);
    settle();
    CHECK_EQ(g_aborts, 1);
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_ABORTED);
    /* The port asks for a read before anything else. */
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(!kst_prog_in_doubt(&g_p));

    /* Before the control task took the command: dropped here. */
    g_deaf = true;
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    kst_prog_abort(&g_p);
    CHECK_EQ(msg(), KST_PROG_MSG_OUT + KST_LINK_OUT_DROPPED);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    g_deaf = false;
    frames(5);
    CHECK_EQ(g_starts, 4);
    CHECK_EQ(g_aborts, 1);
}

/* ------------------------------------------------------------ fed by hand */

static kst_prog_feed_t g_f;

/* A programmer on a held port with the servo read, nothing under it. */
static void by_hand(void)
{
    kst_prog_init(&g_p);
    memset(&g_f, 0, sizeof(g_f));
    g_f.now_ms = 1000u;
    g_f.rd.phase = KST_LINK_READY;
    g_f.rd.known = true;
    g_f.rd.port = LINK_KST_PROGRAMMING;
    g_f.rd.port_no = CH;
    g_f.rd.flags = LINK_KS_F_IN_MODE | LINK_KS_F_IMAGE | LINK_KS_F_BACKUP
                 | LINK_KS_F_FP_OK;
    g_f.have_image = true;
    g_f.image = sim_bench_image();
    g_f.have_backup = true;
    g_f.backup = g_f.image;
    kst_prog_feed(&g_p, &g_f);
    g_p.servo = g_f.image;
    g_p.draft = g_f.image;
    g_p.have_servo = true;
    g_p.fp_ok = true;
    g_p.backed_up = true;      /* the session's file is on the card */
}

static void pass(uint32_t dt_ms)
{
    g_f.now_ms += dt_ms;
    kst_prog_feed(&g_p, &g_f);
}

/* The control task takes the command and kst_link_start() says @p a. */
static void answer(kst_link_start_t a)
{
    kst_prog_cmd_t cmd;

    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_START);
    g_f.answer_id = cmd.id;
    g_f.answer = a;
    pass(1);
}

static void end(const kst_link_outcome_t *o)
{
    g_f.outcomes++;
    g_f.outcome = *o;
    pass(1);
}

/* ---------------------------------------------------------- the servo files */

#define BUSY (KST_PROG_MSG_START + KST_LINK_START_BUSY)

/* The raw value a line of an edited file gives for @p raw: the displayed
 * value, read back as a keypad entry is. */
static uint16_t as_typed(kst_field_id_t id, uint16_t raw)
{
    int32_t shown = 0;
    uint16_t back = 0;

    CHECK(kst_field_display(id, raw, &shown));
    CHECK(kst_field_raw_from_display(id, shown, &back));
    return back;
}

/* A file whose image is the bench servo's with @p id at @p raw. */
static fake_file_t *file_with(const char *name, kst_field_id_t id,
                              uint16_t raw)
{
    kst_image_t img = sim_bench_image();

    CHECK(kst_field_edit(&img, id, raw));
    return fake_card_put(&g_fc, name, &img);
}

static void load(const char *name)
{
    CHECK_EQ(kst_prog_load(&g_p, name), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_busy(&g_p));
    frames(3);
    CHECK(!kst_prog_card_busy(&g_p));
}

static void save(const char *name)
{
    CHECK_EQ(kst_prog_save(&g_p, name), KST_PROG_MSG_NONE);
    frames(3);
    CHECK(!kst_prog_card_busy(&g_p));
}

static void list(void)
{
    CHECK_EQ(kst_prog_files(&g_p, 0), KST_PROG_MSG_NONE);
    frames(3);
    CHECK(!kst_prog_card_busy(&g_p));
}

/* The bench servo's image as the other servo type. */
static kst_image_t other_type(void)
{
    kst_image_t img = sim_bench_image();

    CHECK(kst_field_set(&img, KST_F_20K_SEL,
                        1u - kst_field_get(&img, KST_F_20K_SEL)));
    return img;
}

TEST_CASE(a_save_writes_the_servos_image_under_the_name_given)
{
    const kst_image_t sv = sim_bench_image();

    up();
    CHECK(!kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "");
    CHECK_EQ(kst_prog_save(&g_p, "WING-L"), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_busy(&g_p));
    /* One request at a time. */
    CHECK_EQ(kst_prog_save(&g_p, "B"), BUSY);
    CHECK_EQ(kst_prog_files(&g_p, 0), BUSY);
    CHECK_EQ(kst_prog_load(&g_p, "B"), BUSY);
    frames(3);
    CHECK(!kst_prog_card_busy(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    CHECK_STR_EQ(kst_prog_card_name(&g_p), "WING-L");
    CHECK_EQ(g_fc.n, 1);
    CHECK_STR_EQ(g_fc.file[0].name, "WING-L");
    CHECK(same(&g_fc.file[0].img, &sv));
    /* A file of this servo is the session's backup. */
    CHECK(kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "WING-L");
    CHECK(!kst_prog_save_first(&g_p));
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(g_starts, 2);
}

TEST_CASE(a_name_the_card_does_not_take_saves_nothing)
{
    up();
    CHECK_EQ(kst_prog_save(&g_p, ""), KST_PROG_MSG_NAME_BAD);
    CHECK_EQ(kst_prog_save(&g_p, NULL), KST_PROG_MSG_NAME_BAD);
    CHECK_EQ(kst_prog_save(&g_p, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456"), KST_PROG_MSG_NAME_BAD);
    CHECK(!kst_prog_card_busy(&g_p));
    /* The card's side knows the names: a character no file system takes,
     * a device name, the journal's own name, a full stop at an end. */
    save("a:b");
    CHECK_EQ(msg(), KST_PROG_MSG_NAME_BAD);
    save("CON");
    CHECK_EQ(msg(), KST_PROG_MSG_NAME_BAD);
    save("kst");
    CHECK_EQ(msg(), KST_PROG_MSG_NAME_BAD);
    save(".A");
    CHECK_EQ(msg(), KST_PROG_MSG_NAME_BAD);
    CHECK_EQ(g_fc.n, 0);
    CHECK(!kst_prog_backed_up(&g_p));
    save("A");
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    /* No file is replaced, in either case of the name. */
    g_sim.reg[0x03] ^= 0x01u;
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    save("A");
    CHECK_EQ(msg(), KST_PROG_MSG_NAME_EXISTS);
    save("a");
    CHECK_EQ(msg(), KST_PROG_MSG_NAME_EXISTS);
    CHECK_EQ(g_fc.n, 1);
    CHECK_EQ(g_fc.file[0].img.r[0x03], sim_bench_image().r[0x03]);
}

TEST_CASE(a_card_that_takes_no_file_says_which_way)
{
    static const struct {
        uint8_t result;
        unsigned msg;
    } k[] = {
        { KST_PROG_CARD_NO_CARD, KST_PROG_MSG_CARD_NONE },
        { KST_PROG_CARD_FULL, KST_PROG_MSG_CARD_FULL },
        { KST_PROG_CARD_WRITE_ERR, KST_PROG_MSG_CARD_WRITE },
        { KST_PROG_CARD_READ_ERR, KST_PROG_MSG_CARD_WRITE },
        { KST_PROG_CARD_RESULTS, KST_PROG_MSG_CARD_WRITE },
    };

    up();
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        g_fc.fails = k[i].result;
        save("A");
        CHECK_EQ(msg(), k[i].msg);
        CHECK(!kst_prog_backed_up(&g_p));
    }
    /* No card in the slot: said at once, nothing is asked. */
    g_fc.fails = KST_PROG_CARD_OK;
    g_card = false;
    frame();
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_CARD_NONE);
    CHECK_EQ(kst_prog_files(&g_p, 0), KST_PROG_MSG_CARD_NONE);
    CHECK_EQ(kst_prog_load(&g_p, "A"), KST_PROG_MSG_CARD_NONE);
    CHECK(!kst_prog_card_busy(&g_p));
    CHECK_EQ(g_fc.saves, 5);
    /* A card that does not answer: KST_PROG_CARD_MS. */
    g_card = true;
    g_fc.silent = true;
    frame();
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NONE);
    frames(KST_PROG_CARD_MS - 2u);
    CHECK(kst_prog_card_busy(&g_p));
    frames(3);
    CHECK(!kst_prog_card_busy(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_WRITE);
    CHECK_EQ(kst_prog_files(&g_p, 0), KST_PROG_MSG_NONE);
    frames(KST_PROG_CARD_MS + 1u);
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_READ);
    CHECK_EQ(kst_prog_files_seq(&g_p), 0);
    CHECK_EQ(g_fc.n, 0);
}

TEST_CASE(the_list_names_the_files_their_types_and_the_next_preset)
{
    const kst_image_t sv = sim_bench_image();
    const kst_image_t ot = other_type();
    unsigned total = 9;
    uint32_t said;

    up();
    /* An empty card. */
    list();
    CHECK_EQ(msg(), KST_PROG_MSG_NO_FILES);
    CHECK_EQ(kst_prog_files_seq(&g_p), 1);
    CHECK_EQ(kst_prog_file_count(&g_p, &total), 0);
    CHECK_EQ(total, 0);
    CHECK_STR_EQ(kst_prog_preset_name(&g_p), "KST001");

    (void)fake_card_put(&g_fc, "KST001", &sv);
    (void)fake_card_put(&g_fc, "WING", &ot);
    fake_card_put(&g_fc, "HAND", &sv)->type = 0;
    list();
    CHECK_EQ(kst_prog_files_seq(&g_p), 2);
    CHECK_EQ(kst_prog_file_count(&g_p, NULL), 3);
    /* Sorted by name, whatever the order on the card. */
    CHECK_STR_EQ(kst_prog_file_name(&g_p, 0), "HAND");
    CHECK_STR_EQ(kst_prog_file_name(&g_p, 1), "KST001");
    CHECK_STR_EQ(kst_prog_file_name(&g_p, 2), "WING");
    CHECK_STR_EQ(kst_prog_file_name(&g_p, 3), "");
    CHECK_EQ(kst_prog_file_type(&g_p, 1), kst_card_type(&sv));
    CHECK_EQ(kst_prog_file_type(&g_p, 2), kst_card_type(&ot));
    CHECK_EQ(kst_prog_file_type(&g_p, 1) + kst_prog_file_type(&g_p, 2), 14);
    CHECK_EQ(kst_prog_file_type(&g_p, 0), 0);
    CHECK_EQ(kst_prog_file_type(&g_p, 3), 0);
    CHECK_STR_EQ(kst_prog_preset_name(&g_p), "KST002");

    /* More files than the list holds. */
    for (unsigned i = 0; i < 6u; ++i) {
        char name[KST_PROG_NAME_MAX];

        snprintf(name, sizeof(name), "F%u", i);
        (void)fake_card_put(&g_fc, name, &sv);
    }
    list();
    CHECK_EQ(kst_prog_file_count(&g_p, &total), KST_PROG_FILES_MAX);
    CHECK_EQ(total, 9);
    CHECK_EQ(kst_prog_files_first(&g_p), 0);
    /* The files behind them, and a page past the last. */
    CHECK_EQ(kst_prog_files(&g_p, KST_PROG_FILES_MAX), KST_PROG_MSG_NONE);
    frames(3);
    CHECK_EQ(kst_prog_files_first(&g_p), KST_PROG_FILES_MAX);
    CHECK_EQ(kst_prog_file_count(&g_p, &total), 9u - KST_PROG_FILES_MAX);
    CHECK_EQ(total, 9);
    CHECK_STR_EQ(kst_prog_file_name(&g_p, 0), "F5");
    CHECK_STR_EQ(kst_prog_file_name(&g_p, 3), "WING");
    CHECK_STR_EQ(kst_prog_file_name(&g_p, 4), "");
    CHECK_STR_EQ(kst_prog_preset_name(&g_p), "KST002");
    said = kst_prog_message(&g_p)->seq;
    CHECK_EQ(kst_prog_files(&g_p, 0x12345u), KST_PROG_MSG_NONE);
    frames(3);
    CHECK_EQ(kst_prog_files_first(&g_p), 0xFFFFu);
    CHECK_EQ(kst_prog_file_count(&g_p, &total), 0);
    CHECK_EQ(total, 9);
    /* Files are there: an empty page is no message. */
    CHECK_EQ(kst_prog_message(&g_p)->seq, said);
    list();
    CHECK_EQ(kst_prog_files_seq(&g_p), 6);
    /* A directory that does not read: the list stays as it was. */
    g_fc.fails = KST_PROG_CARD_READ_ERR;
    list();
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_READ);
    CHECK_EQ(kst_prog_files_seq(&g_p), 6);
    CHECK_EQ(kst_prog_file_count(&g_p, NULL), KST_PROG_FILES_MAX);
}

TEST_CASE(a_file_becomes_the_draft_and_is_written_as_edits)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t duty = kst_field_get(&sv, KST_F_DUTY);

    up();
    (void)file_with("LOW", KST_F_DUTY, (uint16_t)(duty - 1u));
    load("LOW");
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 1);
    CHECK_STR_EQ(kst_prog_card_name(&g_p), "LOW");
    CHECK_EQ(kst_prog_load_kind(&g_p), KST_PROG_FILE_VERIFIED);
    CHECK_EQ(kst_prog_load_skipped(&g_p), 0);
    CHECK_EQ(draft(KST_F_DUTY), duty - 1u);
    CHECK_EQ(kst_prog_changed(&g_p), KST_FIELD_BIT(KST_F_DUTY));
    frames(20);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(g_starts, 2);
    /* The write is the ordinary one: the switch, then the changed field. */
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NEED_ENABLE);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(servo().r[KST_REG_DUTY], duty - 1u);
    CHECK_EQ(servo().r[KST_REG_DUTY_COPY], duty - 1u);
}

TEST_CASE(an_edited_file_is_named_as_one)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t duty = kst_field_get(&sv, KST_F_DUTY);
    kst_prog_card_req_t q;
    kst_prog_card_done_t d;
    fake_file_t *f;

    up();
    f = file_with("ED", KST_F_DUTY, (uint16_t)(duty - 10u));
    f->kind = KST_PROG_FILE_EDITED;
    load("ED");
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED_EDITED);
    CHECK_EQ(kst_prog_load_kind(&g_p), KST_PROG_FILE_EDITED);
    CHECK(as_typed(KST_F_DUTY, (uint16_t)(duty - 10u)) != duty);
    CHECK_EQ(draft(KST_F_DUTY), as_typed(KST_F_DUTY, (uint16_t)(duty - 10u)));
    /* A line that reads as the servo shows the value changes nothing: 1
     * step of 255 is below the 1 % the line is written in. */
    f = file_with("FINE", KST_F_DUTY, (uint16_t)(duty - 1u));
    f->kind = KST_PROG_FILE_EDITED;
    load("FINE");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SAME);
    f = file_with("UN", KST_F_DUTY, (uint16_t)(duty - 20u));
    f->kind = KST_PROG_FILE_UNTYPED;
    f->type = 0;
    load("UN");
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED_UNTYPED);
    CHECK_EQ(kst_prog_load_kind(&g_p), KST_PROG_FILE_UNTYPED);
    CHECK_EQ(draft(KST_F_DUTY), as_typed(KST_F_DUTY, (uint16_t)(duty - 20u)));
    /* A kind this code does not know is the least trusted one. */
    kst_prog_discard(&g_p);
    CHECK_EQ(kst_prog_load(&g_p, "UN"), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_take(&g_p, &q));
    kst_card_run(&g_fc.io, &q, &d);
    CHECK_EQ(d.kind, KST_PROG_FILE_UNTYPED);
    d.kind = KST_PROG_FILE_KINDS;
    kst_prog_card_done(&g_p, &d);
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED_UNTYPED);
    CHECK_EQ(kst_prog_load_kind(&g_p), KST_PROG_FILE_UNTYPED);
}

TEST_CASE(a_file_of_the_other_servo_type_is_not_loaded)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t duty = kst_field_get(&sv, KST_F_DUTY);
    kst_image_t ot = other_type();
    const unsigned t_servo = kst_card_type(&sv), t_other = kst_card_type(&ot);
    fake_file_t *f;

    CHECK(kst_field_edit(&ot, KST_F_DUTY, (uint16_t)(duty - 10u)));
    up();
    f = fake_card_put(&g_fc, "OTHER", &ot);
    load("OTHER");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_TYPE);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
    CHECK_EQ(kst_prog_message(&g_p)->arg, t_other | (t_servo << 8));
    CHECK_EQ(kst_prog_changes(&g_p), 0);

    /* The same values in a file that names no type: the type is a locked
     * field, listed and not written. */
    f->type = 0;
    f->kind = KST_PROG_FILE_UNTYPED;
    load("OTHER");
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED_UNTYPED);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 1u | (1u << 8));
    CHECK_EQ(kst_prog_load_skipped(&g_p), KST_FIELD_BIT(KST_F_20K_SEL));
    CHECK_EQ(kst_prog_changed(&g_p), KST_FIELD_BIT(KST_F_DUTY));
    CHECK_EQ(kst_field_get(kst_prog_draft(&g_p), KST_F_20K_SEL),
             kst_field_get(&sv, KST_F_20K_SEL));

    /* The other way round: the servo is of the other type. */
    kst_prog_discard(&g_p);
    CHECK(kst_field_set((kst_image_t *)(void *)g_sim.reg, KST_F_20K_SEL,
                        kst_field_get(&ot, KST_F_20K_SEL)));
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK_EQ(kst_prog_load_skipped(&g_p), 0);
    (void)fake_card_put(&g_fc, "FIRST", &sv);
    load("FIRST");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_TYPE);
    CHECK_EQ(kst_prog_message(&g_p)->arg, t_servo | (t_other << 8));
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    frames(20);
    CHECK_EQ(g_sim.n_writes, 0);
}

TEST_CASE(a_file_that_does_not_read_says_where)
{
    const kst_image_t sv = sim_bench_image();
    fake_file_t *f;

    up();
    f = file_with("X", KST_F_DUTY,
                  (uint16_t)(kst_field_get(&sv, KST_F_DUTY) - 1u));
    f->result = KST_PROG_CARD_BAD;
    load("X");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_BAD);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 0);
    f->line = 7;
    load("X");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_BAD);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 7);
    f->result = KST_PROG_CARD_CHECKSUM;
    load("X");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_CHECKSUM);
    f->result = KST_PROG_CARD_NEWER;
    load("X");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_NEWER);
    /* A value past a hard limit: its field and its line. */
    f->result = KST_PROG_CARD_VALUE;
    f->field = KST_F_BOOST;
    f->line = 5;
    load("X");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_VALUE);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 5);
    CHECK_EQ(kst_prog_message(&g_p)->reg, KST_F_BOOST);
    f->result = KST_PROG_CARD_READ_ERR;
    load("X");
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_READ);
    f->result = KST_PROG_CARD_NO_CARD;
    load("X");
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_NONE);
    /* A file that is gone, and a name that is none. */
    load("GONE");
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_READ);
    CHECK_EQ(kst_prog_load(&g_p, ""), KST_PROG_MSG_CARD_READ);
    CHECK_EQ(kst_prog_load(&g_p, NULL), KST_PROG_MSG_CARD_READ);
    CHECK_EQ(kst_prog_load(&g_p, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456"),
             KST_PROG_MSG_CARD_READ);
    CHECK_EQ(kst_prog_changes(&g_p), 0);
}

TEST_CASE(a_file_beyond_a_hard_limit_is_refused_whole)
{
    kst_prog_card_req_t q;
    kst_prog_card_done_t d;

    up();
    (void)file_with("HARD", KST_F_DUTY, 0xFFu);
    load("HARD");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_REFUSED);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_H_DUTY_HIGH);
    CHECK_EQ(kst_prog_changes(&g_p), 0);

    /* An answer that changes a locked field is not a file's. */
    CHECK_EQ(kst_prog_load(&g_p, "HARD"), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_take(&g_p, &q));
    CHECK(!kst_prog_card_take(&g_p, &q));
    CHECK_EQ(q.op, KST_PROG_CARD_LOAD);
    CHECK_STR_EQ(q.name, "HARD");
    memset(&d, 0, sizeof(d));
    d.image = q.image;
    d.image.r[KST_REG_NODE_ADDR] ^= 0x01u;
    d.id = q.id + 1u;                 /* not this request's */
    kst_prog_card_done(&g_p, &d);
    CHECK(kst_prog_card_busy(&g_p));
    d.id = q.id;
    kst_prog_card_done(&g_p, &d);
    CHECK(!kst_prog_card_busy(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_BAD);
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    kst_prog_card_done(&g_p, &d);     /* nothing on its way: ignored */
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_BAD);
}

TEST_CASE(a_file_beyond_a_soft_limit_waits_for_the_confirmation)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t duty = kst_field_get(&sv, KST_F_DUTY);

    up();
    CHECK(!kst_prog_soft_is_load(&g_p));
    (void)file_with("UP", KST_F_DUTY, (uint16_t)(duty + 1u));
    load("UP");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SOFT);
    CHECK(kst_prog_soft_is_load(&g_p));
    CHECK_EQ(kst_prog_soft_rules(&g_p), KST_RULE_BIT(KST_S_DUTY_RAISED));
    CHECK_EQ(kst_field_get(kst_prog_soft_image(&g_p), KST_F_DUTY), duty + 1u);
    CHECK_EQ(draft(KST_F_DUTY), duty);
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    kst_prog_soft_cancel(&g_p);
    CHECK(!kst_prog_soft_is_load(&g_p));
    CHECK_EQ(kst_prog_changes(&g_p), 0);

    load("UP");
    CHECK(kst_prog_soft_is_load(&g_p));
    kst_prog_soft_confirm(&g_p);
    CHECK(!kst_prog_soft_is_load(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 1);
    CHECK_EQ(draft(KST_F_DUTY), duty + 1u);
    /* An edit that waits is not a file. */
    kst_prog_discard(&g_p);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 1u)),
             KST_PROG_EDIT_SOFT);
    CHECK(!kst_prog_soft_is_load(&g_p));
    kst_prog_soft_cancel(&g_p);
    frames(20);
    CHECK_EQ(g_sim.n_writes, 0);
}

TEST_CASE(a_file_equal_to_the_servo_changes_nothing)
{
    const kst_image_t sv = sim_bench_image();
    kst_image_t pair = sv;

    up();
    (void)fake_card_put(&g_fc, "SAME", &sv);
    load("SAME");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SAME);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 0);
    /* Only the pairing value and a protection value differ: both are
     * locked, counted and not written. */
    pair.r[KST_REG_NODE_ADDR] ^= 0x22u;
    pair.r[0x14] ^= 0x01u;
    (void)fake_card_put(&g_fc, "PAIR", &pair);
    load("PAIR");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SAME);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 2);
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NO_CHANGES);
    frames(20);
    CHECK_EQ(g_sim.n_writes, 0);
}

TEST_CASE(a_load_needs_a_servo_that_is_read_and_may_be_written)
{
    const kst_image_t sv = sim_bench_image();

    /* No servo read yet. */
    fresh_at(0u);
    link_up();
    (void)fake_card_put(&g_fc, "F", &sv);
    CHECK_EQ(kst_prog_load(&g_p, "F"), KST_PROG_MSG_NEED_PORT);
    CHECK_EQ(kst_prog_save(&g_p, "G"), KST_PROG_MSG_NEED_READ);
    /* The list needs the card only. */
    list();
    CHECK_EQ(kst_prog_file_count(&g_p, NULL), 1);
    /* The card is out as well: the servo's lack is said first. */
    g_card = false;
    frame();
    CHECK_EQ(kst_prog_load(&g_p, "F"), KST_PROG_MSG_NEED_PORT);
    CHECK(!kst_prog_card_busy(&g_p));

    /* A servo that fails the fingerprint. */
    fresh_at(0u);
    g_sim.reg[0x06] = 0x05u;
    link_up();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    (void)fake_card_put(&g_fc, "F", &sv);
    CHECK_EQ(kst_prog_load(&g_p, "F"), KST_PROG_MSG_READ_ONLY);
    /* Its values are saved all the same. */
    save("RO");
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);

    /* A servo in doubt. */
    by_hand();
    g_f.card = true;
    kst_prog_feed(&g_p, &g_f);
    g_p.need_read = true;
    CHECK_EQ(kst_prog_load(&g_p, "F"), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_save(&g_p, "F"), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_files(&g_p, 0), KST_PROG_MSG_NEED_READ);
    CHECK(!kst_prog_card_busy(&g_p));
    /* Not the servo of the record. */
    by_hand();
    g_p.journal = true;
    g_p.journal_open = true;
    g_p.journal_other = true;
    g_p.journal_port = 6;
    CHECK_EQ(kst_prog_load(&g_p, "F"), KST_PROG_MSG_JOURNAL_OTHER);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 7);
    /* While an operation waits. */
    by_hand();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_load(&g_p, "F"), BUSY);
    CHECK_EQ(kst_prog_files(&g_p, 0), BUSY);
}

TEST_CASE(a_servo_that_goes_while_its_file_is_read_takes_nothing)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_card_req_t q;
    kst_prog_card_done_t d;

    up();
    (void)file_with("LOW", KST_F_DUTY,
                    (uint16_t)(kst_field_get(&sv, KST_F_DUTY) - 1u));
    CHECK_EQ(kst_prog_load(&g_p, "LOW"), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_take(&g_p, &q));
    CHECK(fake_card_run(&g_fc, &q, &d));
    /* The link goes before the answer. */
    kst_link_lost(&g_k);
    frame();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    kst_prog_card_done(&g_p, &d);
    CHECK(msg() != KST_PROG_MSG_LOADED);
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    CHECK(!kst_prog_card_busy(&g_p));
}

/* ------------------------------------------- the file before the first write */

TEST_CASE(the_first_write_saves_the_servo_to_the_card_first)
{
    const kst_image_t sv = sim_bench_image();

    up_enabled();
    edit(KST_F_DUTY, 200);
    CHECK(kst_prog_save_first(&g_p));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    /* The file, then the write's command. */
    CHECK_EQ(g_fc.saves, 1);
    CHECK_EQ(g_starts_at_save, 2);
    CHECK_EQ(g_starts, 3);
    CHECK_STR_EQ(g_fc.file[0].name, "KST001");
    CHECK(same(&g_fc.file[0].img, &sv));
    CHECK(kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "KST001");
    CHECK(!kst_prog_save_first(&g_p));
    /* Once a session. */
    edit(KST_F_DUTY, 210);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_fc.saves, 1);

    /* Another session: another file, of the servo as it is then. */
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(!kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "");
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(kst_prog_save_first(&g_p));
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 205);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_fc.saves, 2);
    CHECK_STR_EQ(g_fc.file[1].name, "KST002");
    CHECK_EQ(g_fc.file[1].img.r[KST_REG_DUTY], 210);
    CHECK_EQ(g_fc.file[0].img.r[KST_REG_DUTY], sv.r[KST_REG_DUTY]);
}

TEST_CASE(a_file_saved_by_hand_is_the_sessions_and_no_other_is_written)
{
    up_enabled();
    save("MINE");
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_fc.saves, 1);
    CHECK_EQ(g_fc.n, 1);
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "MINE");
}

TEST_CASE(a_first_write_with_no_card_goes_without_a_file)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    kst_image_t now;

    up_enabled();
    g_card = false;
    frame();
    CHECK(kst_prog_save_first(&g_p));
    CHECK(!kst_prog_no_file(&g_p));
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    /* Not said of a write that has not started. */
    CHECK(!kst_prog_no_file(&g_p));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(servo().r[KST_REG_DUTY], 200);
    CHECK_EQ(g_fc.saves, 0);
    CHECK(kst_prog_no_file(&g_p));
    CHECK(!kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "");
    CHECK(!kst_prog_first_write(&g_p));
    /* The next write still owes the file. */
    CHECK(kst_prog_save_first(&g_p));

    /* The second without a card as well. */
    edit(KST_F_DUTY, 198);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_fc.saves, 0);
    CHECK(kst_prog_no_file(&g_p));

    /* The session's restore, the one copy, with no card and no file. */
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = servo();
    CHECK(same(&now, &sv));
    CHECK_EQ(g_fc.saves, 0);
    CHECK(kst_prog_no_file(&g_p));

    /* The session ends: so does the line. */
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(!kst_prog_no_file(&g_p));
}

TEST_CASE(a_pairing_release_with_no_card_goes_without_a_file)
{
    up_enabled();
    g_card = false;
    frame();
    CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RELEASED);
    CHECK_EQ(g_fc.saves, 0);
    CHECK(kst_prog_no_file(&g_p));
}

TEST_CASE(a_card_put_in_after_a_write_takes_the_servo_as_the_session_found_it)
{
    const kst_image_t sv = sim_bench_image();

    up_enabled();
    g_card = false;
    frame();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);

    /* A save by hand holds the written servo: it is not the backup. */
    g_card = true;
    frame();
    save("NOW");
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    CHECK_EQ(g_fc.file[0].img.r[KST_REG_DUTY], 200);
    CHECK(!kst_prog_backed_up(&g_p));
    CHECK(kst_prog_no_file(&g_p));
    CHECK(kst_prog_save_first(&g_p));

    /* The next write saves the image read before the first one. */
    edit(KST_F_DUTY, 198);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(servo().r[KST_REG_DUTY], 198);
    CHECK_EQ(g_fc.saves, 2);
    CHECK_STR_EQ(g_fc.file[1].name, "KST001");
    CHECK(same(&g_fc.file[1].img, &sv));
    CHECK(kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "KST001");
    CHECK(!kst_prog_no_file(&g_p));
    CHECK(!kst_prog_save_first(&g_p));

    /* And the one after it takes no file. */
    edit(KST_F_DUTY, 196);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(g_fc.saves, 2);
}

TEST_CASE(a_card_put_in_after_a_write_and_full_ends_the_next_write)
{
    static const struct {
        uint8_t result;
        unsigned msg;
    } k[] = {
        { KST_PROG_CARD_FULL, KST_PROG_MSG_CARD_FULL },
        { KST_PROG_CARD_WRITE_ERR, KST_PROG_MSG_CARD_WRITE },
        /* Pulled between the request and the file. */
        { KST_PROG_CARD_NO_CARD, KST_PROG_MSG_CARD_NONE },
    };

    up_enabled();
    g_card = false;
    frame();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    g_card = true;
    frame();
    edit(KST_F_DUTY, 198);
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        const unsigned starts = g_starts;

        g_fc.fails = k[i].result;
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        settle();
        CHECK_EQ(msg(), k[i].msg);
        CHECK_EQ(g_starts, starts);
        CHECK_EQ(servo().r[KST_REG_DUTY], 200);
        CHECK(kst_prog_no_file(&g_p));
        CHECK(!kst_prog_backed_up(&g_p));
    }
}

TEST_CASE(the_file_after_a_write_is_of_the_backup_while_the_session_holds_it)
{
    kst_prog_card_req_t req;

    /* The session holds its backup: the file is of it. */
    by_hand();
    g_p.backed_up = false;
    g_p.no_file = true;
    g_p.written = true;
    g_f.card = true;
    CHECK(kst_field_edit(&g_f.backup, KST_F_DUTY, 190));
    pass(1);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_take(&g_p, &req));
    CHECK_EQ(req.op, KST_PROG_CARD_SAVE);
    CHECK(req.preset);
    CHECK(same(&req.image, &g_f.backup));
    CHECK(!same(&req.image, kst_prog_servo(&g_p)));

    /* It does not: the file is of the servo as read. */
    by_hand();
    g_p.backed_up = false;
    g_p.no_file = true;
    g_p.written = true;
    g_f.card = true;
    g_f.have_backup = false;
    g_f.rd.flags &= (uint16_t)~LINK_KS_F_BACKUP;
    pass(1);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_take(&g_p, &req));
    CHECK_EQ(req.op, KST_PROG_CARD_SAVE);
    CHECK(same(&req.image, kst_prog_servo(&g_p)));

    /* A save by hand is of the servo as read, whatever the session
     * holds. */
    by_hand();
    g_p.backed_up = false;
    g_p.no_file = true;
    g_f.card = true;
    CHECK(kst_field_edit(&g_f.backup, KST_F_DUTY, 190));
    pass(1);
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_take(&g_p, &req));
    CHECK(same(&req.image, kst_prog_servo(&g_p)));
}

TEST_CASE(a_write_with_no_card_that_does_not_start_leaves_no_line)
{
    by_hand();
    g_p.backed_up = false;
    pass(1);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_BUSY);
    CHECK_EQ(msg(), BUSY);
    CHECK(!kst_prog_no_file(&g_p));
    CHECK(kst_prog_first_write(&g_p));
    /* The one that starts does. */
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    CHECK(kst_prog_no_file(&g_p));
    CHECK(!kst_prog_first_write(&g_p));
}

TEST_CASE(a_write_without_its_file_is_not_sent)
{
    static const struct {
        uint8_t result;
        unsigned msg;
    } k[] = {
        { KST_PROG_CARD_NO_CARD, KST_PROG_MSG_CARD_NONE },
        { KST_PROG_CARD_FULL, KST_PROG_MSG_CARD_FULL },
        { KST_PROG_CARD_WRITE_ERR, KST_PROG_MSG_CARD_WRITE },
    };

    up_enabled();
    edit(KST_F_DUTY, 200);
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        g_fc.fails = k[i].result;
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        settle();
        CHECK_EQ(msg(), k[i].msg);
        CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
        CHECK_EQ(g_starts, 2);
        CHECK_EQ(g_sim.n_writes, 0);
        CHECK(!kst_prog_journal(&g_p));
        CHECK(!kst_prog_in_doubt(&g_p));
        CHECK_EQ(kst_prog_changes(&g_p), 1);
        CHECK(kst_prog_first_write(&g_p));
        CHECK(!kst_prog_backed_up(&g_p));
        CHECK(kst_prog_write_enabled(&g_p));
    }
    CHECK(!kst_prog_no_file(&g_p));
    CHECK_EQ(g_fc.saves, 3);

    /* A card that never answers: KST_PROG_CARD_MS. */
    g_fc.fails = KST_PROG_CARD_OK;
    g_fc.silent = true;
    frame();
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frames(KST_PROG_CARD_MS - 2u);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    frames(3);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_WRITE);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK(kst_prog_first_write(&g_p));

    /* The card is back: the same write goes. */
    g_fc.silent = false;
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(servo().r[KST_REG_DUTY], 200);
}

TEST_CASE(a_save_given_up_sends_no_write)
{
    /* The operator's abort. */
    up_enabled();
    edit(KST_F_DUTY, 200);
    g_fc.silent = true;
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frames(5);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    kst_prog_abort(&g_p);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    frames(KST_PROG_CARD_MS + 10u);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_aborts, 0);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK(kst_prog_first_write(&g_p));
    CHECK(!kst_prog_in_doubt(&g_p));

    /* STOP. */
    up_enabled();
    edit(KST_F_DUTY, 200);
    g_fc.silent = true;
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frames(5);
    g_stopped = true;
    frame();
    CHECK_EQ(msg(), KST_PROG_MSG_STOPPED);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(!kst_prog_write_enabled(&g_p));
    frames(KST_PROG_CARD_MS + 10u);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);

    /* The link goes. */
    up_enabled();
    edit(KST_F_DUTY, 200);
    g_fc.silent = true;
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frames(5);
    kst_link_lost(&g_k);
    frame();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    frames(KST_PROG_CARD_MS + 10u);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);
}

TEST_CASE(a_fetch_that_brings_nothing_is_asked_again_once_a_second)
{
    kst_prog_cmd_t cmd;

    by_hand();
    g_f.have_backup = false;
    pass(1);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_FETCH);
    pass(KST_PROG_FETCH_MS - 1u);
    CHECK(!kst_prog_take(&g_p, &cmd));
    pass(1);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_FETCH);
    /* An operation goes first, and the fetch is dropped for it. */
    pass(KST_PROG_FETCH_MS);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_START);
    CHECK(!kst_prog_take(&g_p, &cmd));
    /* Across the wrap of the tick. */
    by_hand();
    g_f.have_backup = false;
    g_f.now_ms = 0xFFFFFF00u;
    pass(0);
    CHECK(kst_prog_take(&g_p, &cmd));
    pass(KST_PROG_FETCH_MS - 1u);
    CHECK(!kst_prog_take(&g_p, &cmd));
    pass(1);
    CHECK(kst_prog_take(&g_p, &cmd));
    /* Nothing to fetch with the images held. */
    g_f.have_backup = true;
    pass(KST_PROG_FETCH_MS);
    CHECK(!kst_prog_take(&g_p, &cmd));
}

TEST_CASE(every_answer_of_start_is_a_message)
{
    for (unsigned a = KST_LINK_START_DOWN; a <= KST_LINK_START_NO_IMAGE;
         ++a) {
        by_hand();
        CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
        answer((kst_link_start_t)a);
        CHECK_EQ(msg(), KST_PROG_MSG_START + a);
        CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
        CHECK(msg() < KST_PROG_MSG_OUT);
    }
    /* A write that was not started leaves no journal. */
    by_hand();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK(kst_prog_journal(&g_p));
    CHECK(kst_prog_first_write(&g_p));
    answer(KST_LINK_START_BUSY);
    CHECK(!kst_prog_journal(&g_p));
    CHECK(!kst_prog_in_doubt(&g_p));
    /* Nor is it the session's first write; one that started is, whatever
     * comes of it. */
    CHECK(kst_prog_first_write(&g_p));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK(kst_prog_first_write(&g_p));
    answer(KST_LINK_START_OK);
    CHECK(!kst_prog_first_write(&g_p));
    /* A command nobody answers within KST_PROG_START_MS is none. */
    by_hand();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    pass(KST_PROG_START_MS);
    CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);
    CHECK(kst_prog_first_write(&g_p));
}

TEST_CASE(every_refusal_of_the_port_is_a_message)
{
    for (unsigned r = LINK_KST_REF_BUSY; r <= LINK_KST_REF_PLAN_NO_PATH;
         ++r) {
        kst_link_outcome_t o;

        by_hand();
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
        memset(&o, 0, sizeof(o));
        o.kind = KST_LINK_OUT_REFUSED;
        o.op = LINK_KST_OP_WRITE;
        o.refusal = (link_kst_refusal_t)r;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_REF + r);
        CHECK(msg() < KST_PROG_MSG_SES);
        CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
        /* Refused: nothing was written. */
        CHECK(!kst_prog_journal(&g_p));
        CHECK_EQ(kst_prog_changes(&g_p), 1);
    }
}

TEST_CASE(every_result_of_the_session_is_a_message)
{
    for (unsigned r = KST_SES_ERR_BUSY; r <= KST_SES_ERR_ABORTED; ++r) {
        kst_link_outcome_t o;
        const bool danger = r == KST_SES_ERR_LOCKED || r == KST_SES_ERR_TORN
                         || r == KST_SES_ERR_UNINTENDED
                         || r == KST_SES_ERR_VERIFY;

        by_hand();
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        memset(&o, 0, sizeof(o));
        o.kind = KST_LINK_OUT_DONE;
        o.op = LINK_KST_OP_WRITE;
        o.result = (kst_ses_t)r;
        o.fail_reg = 0x02;
        o.steps_done = 1;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_SES + r);
        CHECK(msg() < KST_PROG_MSG_END);
        CHECK_EQ(kst_prog_message(&g_p)->reg, 0x02);
        CHECK_EQ(kst_prog_message(&g_p)->arg, 1);
        CHECK_EQ(kst_prog_message(&g_p)->level,
                 danger ? KST_PROG_DANGER : KST_PROG_WARN);
        /* A register was written: the servo is read before anything
         * else. */
        CHECK(kst_prog_in_doubt(&g_p));
        CHECK(kst_prog_journal(&g_p));
    }
    /* With no register written, the results that write nothing leave no
     * journal. */
    for (unsigned r = KST_SES_ERR_BUSY; r <= KST_SES_ERR_ABORTED; ++r) {
        kst_link_outcome_t o;
        const bool nothing = r == KST_SES_ERR_BUSY || r == KST_SES_ERR_ARG
            || r == KST_SES_ERR_NOT_IN_MODE || r == KST_SES_ERR_NO_BACKUP
            || r == KST_SES_ERR_FINGERPRINT || r == KST_SES_ERR_LOCKED
            || r == KST_SES_ERR_BAD_PLAN || r == KST_SES_ERR_UNCONFIRMED
            || r == KST_SES_ERR_CHANGED || r == KST_SES_ERR_WRITE_FAILED;

        by_hand();
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        memset(&o, 0, sizeof(o));
        o.kind = KST_LINK_OUT_DONE;
        o.op = LINK_KST_OP_WRITE;
        o.result = (kst_ses_t)r;
        o.fail_reg = 0xFF;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_SES + r);
        CHECK_EQ(kst_prog_journal(&g_p), !nothing);
        /* The servo is not what was read: read again. */
        CHECK_EQ(kst_prog_in_doubt(&g_p),
                 !nothing || r == KST_SES_ERR_CHANGED);
    }
}

TEST_CASE(every_kind_of_outcome_is_a_message)
{
    static const kst_link_outcome_kind_t kinds[] = {
        KST_LINK_OUT_NACK, KST_LINK_OUT_NO_ANSWER, KST_LINK_OUT_RESTARTED,
        KST_LINK_OUT_DROPPED,
    };

    for (unsigned i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        kst_link_outcome_t o;

        by_hand();
        CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        memset(&o, 0, sizeof(o));
        o.kind = kinds[i];
        o.op = LINK_KST_OP_READ_ALL;
        o.nack = 5;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_OUT + (unsigned)kinds[i]);
        CHECK(msg() < KST_PROG_MSG_REF);
        CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
        CHECK_EQ(kst_prog_message(&g_p)->arg,
                 kinds[i] == KST_LINK_OUT_NACK ? 5 : 0);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
        /* A restart ends the session. */
        CHECK_EQ(kst_prog_have_servo(&g_p),
                 kinds[i] != KST_LINK_OUT_RESTARTED);
    }
    /* In a write: a frame refused or a request dropped wrote nothing; no
     * answer and a restart leave the journal. */
    for (unsigned i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        kst_link_outcome_t o;
        const bool open = kinds[i] == KST_LINK_OUT_NO_ANSWER
                       || kinds[i] == KST_LINK_OUT_RESTARTED;

        by_hand();
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        memset(&o, 0, sizeof(o));
        o.kind = kinds[i];
        o.op = LINK_KST_OP_WRITE;
        end(&o);
        CHECK_EQ(kst_prog_journal(&g_p), open);
        CHECK_EQ(msg(), open ? (unsigned)KST_PROG_MSG_JOURNAL
                             : KST_PROG_MSG_OUT + (unsigned)kinds[i]);
    }
    /* A read that says it succeeded and brought no image. */
    {
        kst_link_outcome_t o;

        by_hand();
        g_f.have_image = false;
        CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        memset(&o, 0, sizeof(o));
        o.kind = KST_LINK_OUT_DONE;
        o.op = LINK_KST_OP_READ_ALL;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_READ);
        /* And a write that did: read before anything else. */
        by_hand();
        g_f.have_image = false;
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        edit(KST_F_DUTY, 200);
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        o.op = LINK_KST_OP_WRITE;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
        CHECK(kst_prog_in_doubt(&g_p));
    }
    /* A read that fails leaves what was read before in doubt. */
    {
        kst_link_outcome_t o;

        by_hand();
        CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
        answer(KST_LINK_START_OK);
        memset(&o, 0, sizeof(o));
        o.kind = KST_LINK_OUT_DONE;
        o.op = LINK_KST_OP_READ_ALL;
        o.result = KST_SES_ERR_READ;
        o.fail_reg = 0x07;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_READ);
        CHECK_EQ(kst_prog_message(&g_p)->reg, 0x07);
        CHECK(kst_prog_in_doubt(&g_p));
    }
    /* An outcome nobody waits for is not this programmer's. */
    {
        kst_link_outcome_t o;

        by_hand();
        memset(&o, 0, sizeof(o));
        o.kind = KST_LINK_OUT_DONE;
        o.op = LINK_KST_OP_READ_ALL;
        o.result = KST_SES_ERR_READ;
        end(&o);
        CHECK_EQ(msg(), KST_PROG_MSG_NONE);
    }
}

static void answer_never_comes(uint32_t t0)
{
    by_hand();
    g_f.now_ms = t0;
    pass(0);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    pass(KST_PROG_START_MS - 1u);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);
    /* The command is withdrawn. */
    {
        kst_prog_cmd_t cmd;

        CHECK(!kst_prog_take(&g_p, &cmd));
    }
}

static void outcome_never_comes(uint32_t t0)
{
    kst_prog_cmd_t cmd;

    by_hand();
    g_f.now_ms = t0;
    pass(0);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    pass(KST_PROG_RUN_MS - 1u);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    CHECK(!kst_prog_take(&g_p, &cmd));
    pass(1);
    CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_ABORT);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(kst_prog_journal(&g_p));
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
}

/* A read or a verify with no outcome: what was read is not trusted. */
static void read_never_ends(uint32_t t0, bool verify)
{
    kst_prog_cmd_t cmd;
    kst_link_outcome_t o;

    by_hand();
    g_f.now_ms = t0;
    pass(0);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK(kst_prog_editable(&g_p));
    CHECK_EQ(verify ? kst_prog_verify(&g_p) : kst_prog_read(&g_p),
             KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    pass(KST_PROG_RUN_MS - 1u);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    pass(1);
    CHECK_EQ(msg(), KST_PROG_MSG_TIMEOUT);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_ABORT);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_journal(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, 200), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_NEED_READ);

    /* A read that ends is what clears it. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_DONE;
    o.op = LINK_KST_OP_READ_ALL;
    o.result = KST_SES_OK;
    o.fail_reg = 0xFF;
    end(&o);
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(!kst_prog_in_doubt(&g_p));
    CHECK(kst_prog_editable(&g_p));
}

/* A read or a verify the link gave up on may have run on the coprocessor:
 * the image here is not the one there. */
static void read_gets_no_answer(bool verify)
{
    kst_link_outcome_t o;

    by_hand();
    CHECK(kst_prog_editable(&g_p));
    CHECK_EQ(verify ? kst_prog_verify(&g_p) : kst_prog_read(&g_p),
             KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_NO_ANSWER;
    o.op = verify ? LINK_KST_OP_VERIFY : LINK_KST_OP_READ_ALL;
    end(&o);
    CHECK_EQ(msg(), KST_PROG_MSG_OUT + KST_LINK_OUT_NO_ANSWER);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK(!kst_prog_editable(&g_p));
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, 200), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NEED_READ);
    CHECK(!kst_prog_card_busy(&g_p));
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    o.kind = KST_LINK_OUT_DONE;
    o.op = LINK_KST_OP_READ_ALL;
    o.result = KST_SES_OK;
    o.fail_reg = 0xFF;
    end(&o);
    CHECK(kst_prog_editable(&g_p));
}

TEST_CASE(a_read_the_link_gave_up_on_asks_for_a_read)
{
    kst_link_outcome_t o;

    read_gets_no_answer(false);
    read_gets_no_answer(true);
    /* A frame the page refused ran nothing: the image stands. */
    by_hand();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_NACK;
    o.op = LINK_KST_OP_READ_ALL;
    end(&o);
    CHECK(kst_prog_editable(&g_p));
}

TEST_CASE(no_backup_is_taken_while_an_operation_runs)
{
    kst_link_outcome_t o;

    by_hand();
    g_f.card = true;
    kst_prog_feed(&g_p, &g_f);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    /* The command waits for the control task. */
    CHECK_EQ(kst_prog_save(&g_p, "A"),
             KST_PROG_MSG_START + KST_LINK_START_BUSY);
    CHECK(!kst_prog_card_busy(&g_p));
    answer(KST_LINK_START_OK);
    /* The read runs. */
    CHECK_EQ(kst_prog_save(&g_p, "A"),
             KST_PROG_MSG_START + KST_LINK_START_BUSY);
    CHECK(!kst_prog_card_busy(&g_p));
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_DONE;
    o.op = LINK_KST_OP_READ_ALL;
    o.result = KST_SES_OK;
    o.fail_reg = 0xFF;
    end(&o);
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_busy(&g_p));
}

TEST_CASE(a_file_the_card_did_not_take_is_written_again)
{
    char text[KST_PROG_FILE_MAX];

    /* The record of a write is discarded and the card keeps the old
     * file: without another write the next start finds the record. */
    by_hand();
    g_f.now_ms = 0xFFFFF000u;            /* the wait crosses the wrap */
    g_f.card = true;
    pass(0);
    g_p.journal = true;
    g_p.journal_open = true;
    g_p.journal_port = CH;
    g_p.journal_img = g_p.servo;
    g_p.journal_target = g_p.servo;
    kst_prog_forget(&g_p);
    CHECK_EQ(msg(), KST_PROG_MSG_FORGOTTEN);
    CHECK(kst_prog_file_take(&g_p, text, sizeof(text)) != 0u);
    kst_prog_file_done(&g_p, false);
    CHECK_EQ(kst_prog_file_take(&g_p, text, sizeof(text)), 0);
    pass(KST_PROG_FILE_RETRY_MS - 1u);
    CHECK_EQ(kst_prog_file_take(&g_p, text, sizeof(text)), 0);
    pass(1);
    CHECK(kst_prog_file_take(&g_p, text, sizeof(text)) != 0u);
    CHECK(strstr(text, "journal") == NULL);
    /* It fails again, and is written again. */
    kst_prog_file_done(&g_p, false);
    pass(KST_PROG_FILE_RETRY_MS - 1u);
    CHECK_EQ(kst_prog_file_take(&g_p, text, sizeof(text)), 0);
    pass(1);
    CHECK(kst_prog_file_take(&g_p, text, sizeof(text)) != 0u);
    /* The card takes it: that is the end of it. */
    kst_prog_file_done(&g_p, true);
    pass(2u * KST_PROG_FILE_RETRY_MS);
    CHECK_EQ(kst_prog_file_take(&g_p, text, sizeof(text)), 0);

    /* A card that is gone waits for one. */
    g_p.journal = true;
    g_p.journal_open = true;
    kst_prog_forget(&g_p);
    CHECK(kst_prog_file_take(&g_p, text, sizeof(text)) != 0u);
    kst_prog_file_done(&g_p, false);
    g_f.card = false;
    pass(KST_PROG_FILE_RETRY_MS);
    CHECK_EQ(kst_prog_file_take(&g_p, text, sizeof(text)), 0);
    g_f.card = true;
    pass(1);
    CHECK(kst_prog_file_take(&g_p, text, sizeof(text)) != 0u);
}

TEST_CASE(a_command_nobody_answers_is_given_up)
{
    answer_never_comes(1000u);
    outcome_never_comes(1000u);
    read_never_ends(1000u, false);
    read_never_ends(1000u, true);
}

TEST_CASE(the_waits_hold_across_the_tick_wrap)
{
    answer_never_comes(0xFFFFFFFFu - KST_PROG_START_MS / 2u);
    outcome_never_comes(0xFFFFFFFFu - KST_PROG_RUN_MS / 2u);
    outcome_never_comes(0xFFFFFFFFu);
}

TEST_CASE(the_command_counter_skips_0_when_it_wraps)
{
    kst_prog_cmd_t cmd;

    by_hand();
    g_p.next_id = 0xFFFFFFFFu;
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.id, 1);
    /* An answer to an older command is not this one's. */
    g_f.answer_id = 0xFFFFFFFFu;
    g_f.answer = KST_LINK_START_BUSY;
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    CHECK_EQ(msg(), KST_PROG_MSG_NONE);
}

TEST_CASE(the_state_follows_the_port)
{
    by_hand();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    g_f.rd.flags |= LINK_KS_F_BUSY;
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_START + KST_LINK_START_BUSY);
    g_f.rd.flags &= (uint16_t)~LINK_KS_F_BUSY;
    g_f.rd.port = LINK_KST_STOPPING;
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_ENTERING);
    g_f.rd.port = LINK_KST_LOW;
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_ENTERING);
    g_f.rd.port = LINK_KST_RAIL_OFF;
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_LEAVING);
    g_f.rd.port = LINK_KST_RAIL_WAIT;
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_LEAVING);
    CHECK_EQ(kst_prog_leave(&g_p),
             KST_PROG_MSG_START + KST_LINK_START_BUSY);
    g_f.rd.phase = KST_LINK_SYNC;
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_SYNC);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_START + KST_LINK_START_BUSY);
    g_f.rd.phase = KST_LINK_READY;
    g_f.rd.known = false;
    pass(1);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_SYNC);

    /* The flags of the port that put the servo in doubt. */
    by_hand();
    CHECK(!kst_prog_in_doubt(&g_p));
    g_f.rd.flags |= LINK_KS_F_MUST_READ;
    pass(1);
    CHECK(kst_prog_in_doubt(&g_p));
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, 200), KST_PROG_EDIT_REFUSED);
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_READ);
    g_f.rd.flags = (uint16_t)((g_f.rd.flags & ~LINK_KS_F_MUST_READ)
                              | LINK_KS_F_LOCKED);
    pass(1);
    CHECK(kst_prog_in_doubt(&g_p));

    /* A link lost with a command not yet taken: nothing was sent. */
    by_hand();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    g_f.rd.phase = KST_LINK_DOWN;
    pass(1);
    CHECK_EQ(msg(), KST_PROG_MSG_OUT + KST_LINK_OUT_NO_ANSWER);
    CHECK(!kst_prog_journal(&g_p));
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    /* And with the operation on its way, when the module says nothing. */
    by_hand();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    g_f.rd.phase = KST_LINK_DOWN;
    pass(1);
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
    CHECK(kst_prog_journal(&g_p));
}

/* ----------------------------------------------------------------- the file */

static size_t text_of(char *out, bool journal)
{
    kst_prog_t p;

    kst_prog_init(&p);
    p.journal = journal;
    p.journal_port = 15;
    p.journal_img = sim_bench_image();
    p.journal_img.r[0x05] = 0xABu;
    p.journal_target = sim_bench_image();
    p.journal_target.r[0x1F] = 0xCDu;
    return kst_prog_file_text(&p, out, KST_PROG_FILE_MAX);
}

TEST_CASE(the_file_text_is_read_back_as_it_was_written)
{
    char text[KST_PROG_FILE_MAX], again[KST_PROG_FILE_MAX];
    kst_prog_t p;

    for (unsigned v = 0; v < 2u; ++v) {
        const bool journal = v != 0u;
        const size_t len = text_of(text, journal);

        CHECK(len != 0u);
        CHECK(len < KST_PROG_FILE_MAX);
        CHECK_EQ(len, strlen(text));
        kst_prog_init(&p);
        CHECK(kst_prog_file_load(&p, text, len));
        CHECK_EQ(kst_prog_journal(&p), journal);
        CHECK_EQ(kst_prog_in_doubt(&p), journal);
        CHECK_EQ(kst_prog_message(&p)->id,
                 journal ? KST_PROG_MSG_JOURNAL : KST_PROG_MSG_NONE);
        if (journal) {
            CHECK_EQ(p.journal_port, 15);
            CHECK_EQ(p.journal_img.r[0x05], 0xAB);
            CHECK_EQ(p.journal_target.r[0x1F], 0xCD);
        }
        CHECK_EQ(kst_prog_file_text(&p, again, sizeof(again)), len);
        CHECK_STR_EQ(again, text);
    }
    /* A buffer too small takes nothing. */
    CHECK_EQ(kst_prog_file_text(&p, again, KST_PROG_FILE_MAX - 1u), 0);
    /* The longest text fits. */
    CHECK(text_of(text, true) + 1u <= KST_PROG_FILE_MAX);
}

TEST_CASE(a_file_that_is_not_whole_changes_nothing)
{
    char good[KST_PROG_FILE_MAX], text[KST_PROG_FILE_MAX + 8u];
    const size_t len = text_of(good, true);
    kst_prog_t p;
    unsigned taken = 0;

    /* Every single character changed, every length but the whole. */
    for (size_t i = 0; i < len; ++i) {
        memcpy(text, good, len + 1u);
        text[i] = text[i] == 'x' ? 'y' : 'x';
        kst_prog_init(&p);
        if (kst_prog_file_load(&p, text, len)) {
            taken++;
        } else {
            CHECK_EQ(kst_prog_message(&p)->id, KST_PROG_MSG_FILE_BAD);
            CHECK(!kst_prog_journal(&p));
        }
    }
    CHECK_EQ(taken, 0);
    for (size_t n = 0; n < len; ++n) {
        kst_prog_init(&p);
        CHECK(!kst_prog_file_load(&p, good, n));
    }
    /* A text with a valid check and a body that is none. */
    {
        static const char *const bodies[] = {
            "RCBENCH KST 1\n00=00\n",                 /* a backup cut short */
            "RCBENCH KST 1\njunk\n",
            "RCBENCH KST 1\n00=0g\n01=00\n",
            "RCBENCH KST 2\n",
        };

        for (unsigned i = 0; i < sizeof(bodies) / sizeof(bodies[0]); ++i) {
            kst_prog_t w;
            size_t n;

            /* The check of the body, by the writer's own arithmetic. */
            kst_prog_init(&w);
            n = kst_prog_file_text(&w, text, KST_PROG_FILE_MAX);
            CHECK(n != 0u);
            (void)n;
            kst_prog_init(&p);
            CHECK(!kst_prog_file_load(&p, bodies[i], strlen(bodies[i])));
        }
    }
    /* Too long. */
    memset(text, 'A', sizeof(text));
    kst_prog_init(&p);
    CHECK(!kst_prog_file_load(&p, text, sizeof(text)));
    /* A text that is not whole leaves what is held. */
    kst_prog_init(&p);
    CHECK(kst_prog_file_load(&p, good, len));
    CHECK(!kst_prog_file_load(&p, good, len - 1u));
    CHECK(kst_prog_journal(&p));
}

TEST_CASE(the_file_is_written_once_per_change_and_only_to_a_card)
{
    char text[KST_PROG_FILE_MAX];

    by_hand();
    g_f.card = true;
    kst_prog_feed(&g_p, &g_f);
    CHECK_EQ(kst_prog_file_take(&g_p, text, sizeof(text)), 0);
    g_p.journal = true;
    g_p.journal_open = true;
    g_p.journal_port = CH;
    g_p.journal_img = g_p.servo;
    g_p.journal_target = g_p.servo;
    kst_prog_forget(&g_p);
    /* A buffer too small takes nothing. */
    CHECK_EQ(kst_prog_file_take(&g_p, text, 10), 0);
    CHECK(kst_prog_file_take(&g_p, text, sizeof(text)) != 0u);
    /* One write at a time. */
    CHECK_EQ(kst_prog_file_take(&g_p, text, sizeof(text)), 0);
    kst_prog_file_done(&g_p, true);
    CHECK_EQ(kst_prog_file_take(&g_p, text, sizeof(text)), 0);
}

/* ------------------------------------------- the port, the target, the supply */

TEST_CASE(a_connect_needs_the_port_as_the_testers_target)
{
    fresh_at(0u);
    link_up();
    /* The SERVO screen drives the servo outputs. */
    g_target = 0u;
    frame();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NEED_TARGET);
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_TARGET);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
    CHECK_EQ(g_starts, 0);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    /* Another port than the one asked for. */
    g_target = CH + 2u;
    frame();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NEED_TARGET);
    CHECK_EQ(g_starts, 0);
    /* STOP is said before the target. */
    g_stopped = true;
    frame();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_STOPPED);
    g_stopped = false;
    g_target = CH + 1u;
    frame();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    /* The target moves under a held port: nothing is taken away. */
    g_target = 0u;
    frame();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_have_servo(&g_p));
    CHECK_EQ(kst_prog_verify(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_VERIFIED);
}

TEST_CASE(a_connect_sets_the_supply_it_was_given)
{
    fresh_at(0u);
    g_rail = true;
    link_up();
    CHECK_EQ(kst_prog_supply_mv(&g_p), KST_PROG_SUPPLY_MV);
    CHECK_EQ(kst_prog_supply_ma(&g_p), KST_PROG_SUPPLY_MA);
    CHECK_EQ(kst_prog_supply_mv(&g_p), 5000);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(g_sup_on);
    CHECK_EQ(g_sup_mv, 5000);
    CHECK_EQ(g_sup_ma, 1000);

    /* Another voltage and limit, at the port's ends. */
    fresh_at(0u);
    g_rail = true;
    link_up();
    kst_prog_supply_set(&g_p, KST_PORT_SUPPLY_MAX_MV, PDMINI_I_MAX_MA);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK_EQ(g_sup_mv, KST_PORT_SUPPLY_MAX_MV);
    CHECK_EQ(g_sup_ma, PDMINI_I_MAX_MA);

    fresh_at(0u);
    g_rail = true;
    link_up();
    kst_prog_supply_set(&g_p, PDMINI_V_MIN_MV, PDMINI_I_MIN_MA);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(g_sup_mv, PDMINI_V_MIN_MV);
    CHECK_EQ(g_sup_ma, PDMINI_I_MIN_MA);
}

TEST_CASE(a_supply_outside_the_ports_range_is_refused_by_name)
{
    static const uint16_t bad[][2] = {
        { KST_PORT_SUPPLY_MAX_MV + 1u, 1000u },
        { PDMINI_V_MIN_MV - 1u, 1000u },
        { 5000u, PDMINI_I_MAX_MA + 1u },
        { 5000u, PDMINI_I_MIN_MA - 1u },
    };

    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        fresh_at(0u);
        g_rail = true;
        link_up();
        kst_prog_supply_set(&g_p, bad[i][0], bad[i][1]);
        CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_SUPPLY_RANGE);
        CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
        CHECK(!g_sup_on);
        /* No read is chained to it. */
        CHECK_EQ(g_starts, 1);
    }
    /* With no PD mini the supply is not touched and nothing is refused. */
    fresh_at(0u);
    link_up();
    kst_prog_supply_set(&g_p, KST_PORT_SUPPLY_MAX_MV + 1u, 1000u);
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(!g_sup_on);
}

TEST_CASE(a_supply_that_does_not_switch_on_ends_the_connect_with_result_32)
{
    fresh_at(0u);
    g_rail = true;
    g_rail_stuck = true;
    link_up();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_ENTERING);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SES + LINK_KST_RESULT_SUPPLY);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK_EQ(g_starts, 1);
}

TEST_CASE(a_leave_goes_back_to_pwm_and_switches_no_supply)
{
    /* No PD mini answers: the port's command changes its state only. */
    fresh_at(0u);
    link_up();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_LEAVING);
    /* Nothing else starts under it. */
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_START + KST_LINK_START_BUSY);
    CHECK_EQ(kst_prog_connect(&g_p, CH),
             KST_PROG_MSG_START + KST_LINK_START_BUSY);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_INFO);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK_EQ(kst_prog_changes(&g_p), 0);
}

TEST_CASE(a_leave_with_a_pd_mini_sends_nothing_and_keeps_the_session)
{
    int starts;

    fresh_at(0u);
    g_rail = true;
    link_up();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(g_sup_on);
    starts = g_starts;
    /* The port's only command to PWM cycles the supply: it is not sent. */
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_LEAVE_SUPPLY);
    CHECK_EQ(msg(), KST_PROG_MSG_LEAVE_SUPPLY);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
    settle();
    CHECK_EQ(g_starts, starts);
    CHECK(g_sup_on);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_have_servo(&g_p));
    /* The session goes on. */
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    /* The PD mini stops answering: the leave is the port's command. */
    g_rail = false;
    frame();
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
}

TEST_CASE(the_wait_before_pwm_keeps_its_limits)
{
    fresh_at(0u);
    CHECK_EQ(kst_prog_wait_ms(&g_p), KST_PROG_WAIT_MS);
    CHECK_EQ(KST_PROG_WAIT_MS, 100u);
    CHECK(kst_prog_wait_set(&g_p, 0u));
    CHECK_EQ(kst_prog_wait_ms(&g_p), 0u);
    CHECK(kst_prog_wait_set(&g_p, KST_PROG_WAIT_MAX_MS));
    CHECK_EQ(kst_prog_wait_ms(&g_p), 10000u);
    /* 1 ms above the range changes nothing, and nothing wraps. */
    CHECK(!kst_prog_wait_set(&g_p, KST_PROG_WAIT_MAX_MS + 1u));
    CHECK_EQ(kst_prog_wait_ms(&g_p), 10000u);
    CHECK(!kst_prog_wait_set(&g_p, 65536u + 5u));
    CHECK(!kst_prog_wait_set(&g_p, UINT32_MAX));
    CHECK_EQ(kst_prog_wait_ms(&g_p), 10000u);
    CHECK(kst_prog_wait_set(&g_p, 250u));
    /* A link that comes and goes keeps it. */
    link_up();
    CHECK_EQ(kst_prog_wait_ms(&g_p), 250u);
}

int main(void)
{
    RUN(a_connect_enters_and_reads_the_servo);
    RUN(a_coprocessor_without_the_page_is_named);
    RUN(the_port_says_why_a_port_is_not_taken);
    RUN(no_servo_on_the_port_is_a_message);
    RUN(a_connect_needs_the_port_as_the_testers_target);
    RUN(a_connect_sets_the_supply_it_was_given);
    RUN(a_supply_outside_the_ports_range_is_refused_by_name);
    RUN(a_supply_that_does_not_switch_on_ends_the_connect_with_result_32);
    RUN(a_leave_goes_back_to_pwm_and_switches_no_supply);
    RUN(a_leave_with_a_pd_mini_sends_nothing_and_keeps_the_session);
    RUN(the_wait_before_pwm_keeps_its_limits);
    RUN(an_edit_inside_the_limits_changes_the_draft_only);
    RUN(a_value_beyond_a_hard_limit_is_not_taken);
    RUN(a_value_beyond_a_soft_limit_waits_for_the_confirmation);
    RUN(the_range_of_a_field_is_what_the_hard_rules_take);
    RUN(a_range_is_found_from_a_draft_value_the_rules_refuse);
    RUN(a_step_moves_one_count_and_stops_at_the_fields_end);
    RUN(a_write_needs_the_switch_and_writes_the_changed_fields);
    RUN(the_first_write_of_every_session_is_the_first_again);
    RUN(a_draft_the_planner_refuses_is_not_sent);
    RUN(the_switch_goes_off_where_the_rules_say);
    RUN(leaving_gives_the_output_back_and_ends_the_session);
    RUN(a_verify_says_whether_the_servo_holds_what_was_read);
    RUN(a_servo_that_fails_the_fingerprint_is_read_only);
    RUN(a_release_of_pairing_writes_00_to_register_1d);
    RUN(the_sessions_backup_is_restored_by_the_coprocessor);
    RUN(the_journal_reaches_the_card_before_the_write_is_sent);
    RUN(a_card_that_takes_no_journal_stops_the_write);
    RUN(a_link_lost_in_a_write_leaves_the_servo_in_doubt);
    RUN(a_later_session_cannot_write_to_a_servo_cut_between_01_and_02);
    RUN(a_panel_that_starts_on_a_locked_session_fetches_its_backup);
    RUN(a_save_writes_the_servos_image_under_the_name_given);
    RUN(a_name_the_card_does_not_take_saves_nothing);
    RUN(a_card_that_takes_no_file_says_which_way);
    RUN(the_list_names_the_files_their_types_and_the_next_preset);
    RUN(a_file_becomes_the_draft_and_is_written_as_edits);
    RUN(an_edited_file_is_named_as_one);
    RUN(a_file_of_the_other_servo_type_is_not_loaded);
    RUN(a_file_that_does_not_read_says_where);
    RUN(a_file_beyond_a_hard_limit_is_refused_whole);
    RUN(a_file_beyond_a_soft_limit_waits_for_the_confirmation);
    RUN(a_file_equal_to_the_servo_changes_nothing);
    RUN(a_load_needs_a_servo_that_is_read_and_may_be_written);
    RUN(a_servo_that_goes_while_its_file_is_read_takes_nothing);
    RUN(the_first_write_saves_the_servo_to_the_card_first);
    RUN(a_file_saved_by_hand_is_the_sessions_and_no_other_is_written);
    RUN(a_first_write_with_no_card_goes_without_a_file);
    RUN(a_pairing_release_with_no_card_goes_without_a_file);
    RUN(a_card_put_in_after_a_write_takes_the_servo_as_the_session_found_it);
    RUN(a_card_put_in_after_a_write_and_full_ends_the_next_write);
    RUN(the_file_after_a_write_is_of_the_backup_while_the_session_holds_it);
    RUN(a_write_with_no_card_that_does_not_start_leaves_no_line);
    RUN(a_write_without_its_file_is_not_sent);
    RUN(a_save_given_up_sends_no_write);
    RUN(a_fetch_that_brings_nothing_is_asked_again_once_a_second);
    RUN(a_later_session_writes_the_journals_image_back);
    RUN(a_second_cut_keeps_the_image_before_the_first_write);
    RUN(a_journal_whose_servo_is_unchanged_or_complete_is_closed);
    RUN(a_servo_that_is_not_the_journals_takes_no_restore_and_no_write);
    RUN(a_journal_of_another_port_is_not_this_servos);
    RUN(a_restore_names_the_soft_limits_its_values_pass);
    RUN(a_coprocessor_that_starts_again_in_a_write_leaves_the_journal);
    RUN(a_write_the_servo_does_not_take_names_the_register);
    RUN(a_torn_servo_takes_the_sessions_restore_only);
    RUN(a_stop_aborts_the_operation_and_asks_for_a_read);
    RUN(a_stop_before_the_command_is_taken_sends_nothing);
    RUN(the_operators_abort_ends_an_operation_that_runs);
    RUN(every_answer_of_start_is_a_message);
    RUN(every_refusal_of_the_port_is_a_message);
    RUN(every_result_of_the_session_is_a_message);
    RUN(every_kind_of_outcome_is_a_message);
    RUN(a_read_the_link_gave_up_on_asks_for_a_read);
    RUN(no_backup_is_taken_while_an_operation_runs);
    RUN(a_file_the_card_did_not_take_is_written_again);
    RUN(a_command_nobody_answers_is_given_up);
    RUN(the_waits_hold_across_the_tick_wrap);
    RUN(the_command_counter_skips_0_when_it_wraps);
    RUN(the_state_follows_the_port);
    RUN(the_file_text_is_read_back_as_it_was_written);
    RUN(a_file_that_is_not_whole_changes_nothing);
    RUN(the_file_is_written_once_per_change_and_only_to_a_card);
    return test_summary("kst_prog");
}
