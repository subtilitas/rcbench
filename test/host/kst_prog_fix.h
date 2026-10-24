/*
 * The rig of the programmer's host suites (test_kst_prog.c and
 * test_kst_prog_card.c): the simulated servo, the link module, the
 * programming port and the control task's side, one millisecond at a time
 * (frame()), and the by-hand feed.  Only static inline helpers and the
 * state they use; every suite includes the header whole.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

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
static bool g_set_read;      /* the panel reads an output that is on */
static uint16_t g_set_mv;    /* and what it is set to                */
static uint16_t g_sup_mv, g_sup_ma;
static uint8_t g_target;       /* LINK_PG_TARGET as the control task reads */
static int  g_pin_value = -1;  /* the pin the binding gives CH, or -1: PIN */
static bool g_pin_unknown;     /* the panel has no pin for any port */

/* The control task's side of the programmer. */
static uint32_t g_outcomes;
static kst_link_outcome_t g_outcome;
static uint32_t g_answer_id;
static kst_link_start_t g_answer;
static bool g_stopped;
static bool g_armed;           /* the feed's armed                  */
static bool g_deaf;            /* the screen's task hands no command over */
static kst_prog_gate_t g_gate; /* what the two tasks share              */
static kst_prog_cmd_t g_q[16]; /* the queue between them                */
static unsigned g_qn;
static bool g_stall;           /* the control task takes none out of it */
static unsigned g_dropped;     /* STARTs it took out and did not carry out */
static int g_nack_at;          /* the exchange, counted from 0, that is
                                  answered with a NACK; -1 none         */
static bool g_nacked;          /* and it was                            */
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

/* ------------------------------------------------------------- IOmcu */

static inline uint8_t hw_pin(void *ctx, uint8_t port)
{
    (void)ctx;
    /* g_pin_value is set by the suites that bind another pin; in the others
     * it stays -1. */
    return (port == CH && !g_no_pin)
               /* cppcheck-suppress knownConditionTrueFalse */
               ? (uint8_t)(g_pin_value >= 0 ? g_pin_value : (int)PIN) : 0xFFu;
}

static inline bool hw_take(void *ctx, uint8_t port, uint8_t pin)
{
    (void)ctx;
    (void)port;
    (void)pin;
    return true;
}

static inline void hw_give(void *ctx)
{
    (void)ctx;
}

static inline void hw_supply(void *ctx, supply_port_view_t *out)
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

static inline bool hw_supply_set(void *ctx, bool on, uint16_t mv, uint16_t ma)
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

static inline void page_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    kst_port_read(&g_port, off, n, out);
}

static inline uint8_t page_write(void *ctx, uint8_t off, uint8_t n,
                          const uint16_t *in)
{
    (void)ctx;
    return kst_port_write(&g_port, off, n, in, g_ms);
}

static const link_page_t k_pages[] = {
    { LINK_PAGE_KST, LINK_KS_COUNT, page_read, page_write },
};

static inline void port_fresh(void)
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

/* The model as a start of the panel leaves it, at the gate the control
 * task claims at. */
static inline void prog_fresh(void)
{
    kst_prog_init(&g_p);
    kst_prog_gate_init(&g_gate);
    kst_prog_gate_set(&g_p, &g_gate);
}

static inline void fresh_at(uint32_t t0)
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
    g_pin_unknown = false;
    port_fresh();
    g_ms = t0;
    link_dev_init(&g_dev, k_pages, 1u, NULL, g_ms);
    kst_link_init(&g_k);
    prog_fresh();
    g_qn = 0;
    g_stall = false;
    g_dropped = 0;
    g_nack_at = -1;
    g_nacked = false;
    g_safe = true;
    g_stop = false;
    g_outcomes = 0;
    memset(&g_outcome, 0, sizeof(g_outcome));
    g_answer_id = 0;
    g_answer = KST_LINK_START_OK;
    g_stopped = false;
    g_armed = false;
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
static inline void exchange(void)
{
    kst_link_xfer_t x;
    link_msg_t req, rep;

    if (!kst_link_next(&g_k, g_ms, &x)) {
        return;
    }
    if (g_nack_at >= 0 && g_nack_at-- == 0) {
        /* The IOmcu refuses this one frame. */
        g_nacked = true;
        kst_link_done(&g_k, LINK_NACK_BAD_VALUE, NULL);
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

static inline void publish(void)
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
    f.armed = g_armed;
    f.card = g_card;
    f.target = g_target;
    /* The pins as the binding gives them to the panel. */
    for (uint8_t i = 0; i < LINK_PG_PORTS_MAX; ++i) {
        f.port_pin[i] = hw_pin(NULL, i);
        if (!g_pin_unknown && f.port_pin[i] != 0xFFu) {
            f.port_pin_known |= (uint8_t)(1u << i);
        }
    }
    f.supply_read = g_set_read;
    f.set_mv = g_set_mv;
    kst_prog_feed(&g_p, &f);
}

/* 1ms: the IOmcu, one exchange, the control task, the screen's
 * frame and the card. */
static inline void frame(void)
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
    /* The screen's task: every command into the queue. */
    while (!g_deaf && g_qn < sizeof(g_q) / sizeof(g_q[0])
           && kst_prog_take(&g_p, &g_q[g_qn])) {
        g_qn++;
    }
    /* The control task: the queue, in order.  A START is carried out with
     * its pass and dropped without. */
    for (unsigned i = 0; !g_stall && i < g_qn; ++i) {
        cmd = g_q[i];
        if (cmd.kind == KST_PROG_CMD_START) {
            if (!kst_prog_gate_claim(&g_gate, cmd.id)) {
                g_dropped++;
                continue;
            }
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
    if (!g_stall) {
        g_qn = 0;
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

static inline void frames(unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        frame();
    }
}

static inline bool at_rest(void)
{
    const kst_prog_state_t st = kst_prog_state(&g_p);

    return st == KST_PROG_PWM || st == KST_PROG_PROGRAMMING
        || st == KST_PROG_DOWN || st == KST_PROG_NO_PAGE;
}

/* Until nothing runs; 60s is the bound. */
static inline void settle(void)
{
    frame();
    for (unsigned i = 0; i < 60000u && !at_rest(); ++i) {
        frame();
    }
    CHECK(at_rest());
}

static inline unsigned msg(void)
{
    return kst_prog_message(&g_p)->id;
}

static inline bool same(const kst_image_t *a, const kst_image_t *b)
{
    return memcmp(a->r, b->r, sizeof(a->r)) == 0;
}

static inline kst_image_t servo(void)
{
    kst_image_t img;

    memcpy(img.r, g_sim.reg, sizeof(img.r));
    return img;
}

static inline void link_up(void)
{
    kst_link_came_up(&g_k, LINK_PROTOCOL_MINOR);
    settle();
}

/* Up, connected and read. */
static inline void up(void)
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
static inline void up_enabled(void)
{
    up();
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK(kst_prog_write_enabled(&g_p));
}

static inline uint16_t draft(kst_field_id_t id)
{
    return kst_field_get(kst_prog_draft(&g_p), id);
}

/* An edit, a soft rule confirmed. */
static inline void edit(kst_field_id_t id, uint16_t raw)
{
    const kst_prog_edit_t e = kst_prog_edit(&g_p, id, raw);

    CHECK(e != KST_PROG_EDIT_REFUSED);
    if (e == KST_PROG_EDIT_SOFT) {
        kst_prog_soft_confirm(&g_p);
    }
    CHECK_EQ(draft(id), raw);
}

/* Servo B on the port: it differs from the session's servo in the duty
 * alone, and the session's backup is not B's. */
static inline void duty_swapped_is_another_servo(void)
{
    kst_prog_restore_t plan;

    CHECK_EQ(g_sim.n_writes, 0);
    g_sim.reg[KST_REG_DUTY] = 190;
    g_sim.reg[KST_REG_DUTY_COPY] = 190;
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_BACKUP_OTHER);
    CHECK(kst_prog_backup_other(&g_p));
    CHECK(!kst_prog_write_enabled(&g_p));
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK(!kst_prog_restore_offered(&g_p, KST_PROG_FROM_SESSION));
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_BACKUP_OTHER);
}

/* ------------------------------------------------------------ fed by hand */

static kst_prog_feed_t g_f;

/* A programmer on a held port with the servo read, nothing under it. */
static inline void by_hand(void)
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

static inline void pass(uint32_t dt_ms)
{
    g_f.now_ms += dt_ms;
    kst_prog_feed(&g_p, &g_f);
}

/* The control task takes the command and kst_link_start() says @p a. */
static inline void answer(kst_link_start_t a)
{
    kst_prog_cmd_t cmd;

    CHECK(kst_prog_take(&g_p, &cmd));
    CHECK_EQ(cmd.kind, KST_PROG_CMD_START);
    g_f.answer_id = cmd.id;
    g_f.answer = a;
    pass(1);
}

static inline void end(const kst_link_outcome_t *o)
{
    g_f.outcomes++;
    g_f.outcome = *o;
    if (o->id == 0u) {
        /* The outcome of the command answer() answered. */
        g_f.outcome.id = g_f.answer_id;
    }
    pass(1);
}
