/*
 * A bench for the overlay that programs KST and Chaservo servos (kst_panel.h): the
 * simulated servo of kst_sim.h, the IOmcu's programming port, link
 * dispatch and the panel's link module, with rig_frame() as the panel's
 * control task and its card.
 *
 * The overlay is driven as the firmware drives it: a feed every frame, the
 * commands taken, the file written.  One frame is 1ms.
 *
 * For the host tests and the screenshot renderer.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "card_state.h"
#include "kst_fake_card.h"
#include "kst_link.h"
#include "kst_panel.h"
#include "kst_port.h"
#include "kst_prog.h"
#include "kst_sim.h"
#include "link_dev.h"
#include "link_msg.h"

#define RIG_PORT  0u          /* the programming port the servo is on */
#define RIG_PIN   13u
#define RIG_VIN_MV 20000u     /* the PD mini's input */

typedef struct {
    sim_t sim;
    kst_port_t port;

    /* The PD mini as the port sees it.  It reads what it is asked at
     * once. */
    bool rail;                 /* a PD mini answers */
    bool rail_deaf;            /* it takes no request */
    bool no_pin;               /* the generator has no pin for the port */
    uint8_t ports;             /* the build's ports; 0 is 1 */
    uint8_t pin_of[4];         /* ports past RIG_PORT: their pins, 0 none */
    uint8_t taken_port, taken_pin;  /* what the last take handed over */
    bool sup_on;
    uint16_t sup_mv, sup_ma;
    unsigned sup_sets;
    uint16_t vin_mv;           /* its input; 0 is RIG_VIN_MV */
    uint16_t load_ma;          /* the current the servo draws */
    uint8_t target;            /* LINK_PG_TARGET as the control task reads */
    link_dev_t dev;
    kst_link_t link;
    uint32_t ms;
    bool safe, stop;

    /* The control task. */
    uint32_t outcomes;
    kst_link_outcome_t outcome;
    uint32_t answer_id;
    kst_link_start_t answer;
    bool stopped;              /* STOP is latched */
    bool deaf;                 /* no command is taken */
    kst_prog_gate_t gate;      /* what the two tasks share */
    bool cut;                  /* no exchange is made: the link is silent */
    unsigned starts, aborts, fetches;

    /* The card. */
    bool card, card_fails, card_silent;
    uint8_t card_state;        /* card_state_t while card is false */
    char file[KST_PROG_FILE_MAX];
    size_t file_len;
    unsigned file_writes;
    fake_card_t fc;            /* the servo files */
    unsigned starts_at_save;   /* starts when the last save was asked */
} rig_t;

static rig_t g_rig;

static inline uint8_t rig_hw_pin(void *ctx, uint8_t port)
{
    (void)ctx;
    if (port == RIG_PORT) {
        return g_rig.no_pin ? 0xFFu : RIG_PIN;
    }
    return (port < 4u && g_rig.pin_of[port] != 0u) ? g_rig.pin_of[port]
                                                   : 0xFFu;
}

static inline bool rig_hw_take(void *ctx, uint8_t port, uint8_t pin)
{
    (void)ctx;
    g_rig.taken_port = port;
    g_rig.taken_pin = pin;
    return true;
}

static inline void rig_hw_give(void *ctx)
{
    (void)ctx;
}

static inline void rig_hw_supply(void *ctx, supply_port_view_t *out)
{
    (void)ctx;
    memset(out, 0, sizeof(*out));
    out->online = g_rig.rail;
    out->asked_on = g_rig.sup_on;
    out->on = g_rig.rail && g_rig.sup_on;
    out->off = !g_rig.sup_on;
    out->set_mv = g_rig.sup_mv;
    out->set_ma = g_rig.sup_ma;
    out->vin_mv = !g_rig.rail ? 0u
                : g_rig.vin_mv != 0u ? g_rig.vin_mv : RIG_VIN_MV;
}

static inline bool rig_hw_supply_set(void *ctx, bool on, uint16_t mv,
                                     uint16_t ma)
{
    (void)ctx;
    if (!g_rig.rail || g_rig.rail_deaf) {
        return false;
    }
    g_rig.sup_sets++;
    g_rig.sup_on = on;
    if (on) {
        g_rig.sup_mv = mv;
        g_rig.sup_ma = ma;
    }
    return true;
}

static inline void rig_page_read(void *ctx, uint8_t off, uint8_t n,
                                 uint16_t *out)
{
    (void)ctx;
    kst_port_read(&g_rig.port, off, n, out);
}

static inline uint8_t rig_page_write(void *ctx, uint8_t off, uint8_t n,
                                     const uint16_t *in)
{
    (void)ctx;
    return kst_port_write(&g_rig.port, off, n, in, g_rig.ms);
}

static const link_page_t k_rig_pages[] = {
    { LINK_PAGE_KST, LINK_KS_COUNT, rig_page_read, rig_page_write },
};

/* The IOmcu starts: the port forgets, the servo stays as it is. */
static inline void rig_port_fresh(void)
{
    kst_port_hw_t hw;

    memset(&hw, 0, sizeof(hw));
    hw.ports = (g_rig.ports != 0u) ? g_rig.ports : 1u;
    hw.pin = rig_hw_pin;
    hw.take = rig_hw_take;
    hw.give = rig_hw_give;
    hw.supply = rig_hw_supply;
    hw.supply_set = rig_hw_supply_set;
    hw.driver = sim_driver(&g_rig.sim);
    (void)kst_port_init(&g_rig.port, &hw);
    link_dev_init(&g_rig.dev, k_rig_pages, 1u, NULL, g_rig.ms);
}

/* A servo on programming port 1, the SERVO screen's target the port, no PD
 * mini, the link down, an empty card. */
static inline void rig_fresh(void)
{
    memset(&g_rig, 0, sizeof(g_rig));
    fake_card_init(&g_rig.fc);
    sim_init(&g_rig.sim);
    g_rig.target = 1u + RIG_PORT;
    g_rig.safe = true;
    g_rig.card = true;
    rig_port_fresh();
    kst_link_init(&g_rig.link);
    kst_prog_gate_init(&g_rig.gate);
    kst_panel_gate(&g_rig.gate);
}

static inline void rig_exchange(void)
{
    kst_link_xfer_t x;
    link_msg_t req, rep;

    if (!kst_link_next(&g_rig.link, g_rig.ms, &x)) {
        return;
    }
    if (g_rig.cut) {
        kst_link_done(&g_rig.link, KST_LINK_NO_ANSWER, NULL);
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
    if (!link_dev_dispatch(&g_rig.dev, &req, &rep, g_rig.ms)) {
        kst_link_done(&g_rig.link, KST_LINK_NO_ANSWER, NULL);
    } else if (rep.op == LINK_OP_DATA) {
        kst_link_done(&g_rig.link, KST_LINK_ACK, rep.regs);
    } else if (rep.op == LINK_OP_ACK) {
        kst_link_done(&g_rig.link, KST_LINK_ACK, NULL);
    } else {
        kst_link_done(&g_rig.link, rep.regs[0], NULL);
    }
}

/* What the control task publishes, as it stands. */
static inline void rig_feed_now(kst_prog_feed_t *f)
{
    memset(f, 0, sizeof(*f));
    f->now_ms = g_rig.ms;
    kst_link_readout(&g_rig.link, &f->rd);
    f->have_image = kst_link_image(&g_rig.link, &f->image);
    f->have_backup = kst_link_backup(&g_rig.link, &f->backup);
    f->answer_id = g_rig.answer_id;
    f->answer = g_rig.answer;
    f->outcomes = g_rig.outcomes;
    f->outcome = g_rig.outcome;
    f->stopped = g_rig.stopped;
    f->card = g_rig.card;
    f->card_state = g_rig.card ? (uint8_t)CARD_STATE_MOUNTED : g_rig.card_state;
    f->target = g_rig.target;
    for (uint8_t i = 0; i < LINK_PG_PORTS_MAX; ++i) {
        f->port_pin[i] = rig_hw_pin(NULL, i);
        if (f->port_pin[i] != 0xFFu) {
            f->port_pin_known |= (uint8_t)(1u << i);
        }
    }
    f->supply_read = g_rig.rail && g_rig.sup_on;
    f->out_mv = f->supply_read ? g_rig.sup_mv : 0u;
    f->out_ma = f->supply_read ? g_rig.load_ma : 0u;
    f->set_mv = f->supply_read ? g_rig.sup_mv : 0u;
}

/* 1ms: the IOmcu, one exchange, the control task and the card. */
static inline void rig_frame(void)
{
    kst_prog_cmd_t cmd;
    kst_link_outcome_t o;
    kst_prog_feed_t f;
    kst_prog_card_req_t creq;
    kst_prog_card_done_t cdone;
    char text[KST_PROG_FILE_MAX];
    size_t len;

    for (unsigned i = 0; i < 10u; ++i) {
        kst_port_step(&g_rig.port, g_rig.safe, g_rig.stop, g_rig.ms);
        g_rig.sim.now_us += 100u;
    }
    g_rig.ms++;
    rig_exchange();
    if (!g_rig.deaf && kst_panel_take(&cmd)) {
        if (cmd.kind == KST_PROG_CMD_START
            && !kst_prog_gate_claim(&g_rig.gate, cmd.id)) {
            /* A START without its pass is not carried out. */
        } else if (cmd.kind == KST_PROG_CMD_START) {
            g_rig.starts++;
            g_rig.answer = kst_link_start(&g_rig.link, &cmd.req);
            g_rig.answer_id = cmd.id;
        } else if (cmd.kind == KST_PROG_CMD_FETCH) {
            g_rig.fetches++;
            (void)kst_link_fetch(&g_rig.link);
        } else {
            g_rig.aborts++;
            kst_link_abort(&g_rig.link);
        }
    }
    if (kst_link_outcome(&g_rig.link, &o)) {
        g_rig.outcome = o;
        g_rig.outcomes++;
    }
    rig_feed_now(&f);
    kst_panel_feed(&f);
    len = kst_panel_file_take(text, sizeof(text));
    if (len != 0u) {
        g_rig.file_writes++;
        if (!g_rig.card_fails) {
            memcpy(g_rig.file, text, len + 1u);
            g_rig.file_len = len;
        }
        if (!g_rig.card_silent) {
            kst_panel_file_done(!g_rig.card_fails);
        }
    }
    if (kst_panel_card_take(&creq)) {
        if (creq.op == KST_PROG_CARD_SAVE) {
            g_rig.starts_at_save = g_rig.starts;
        }
        if (fake_card_run(&g_rig.fc, &creq, &cdone)) {
            kst_panel_card_done(&cdone);
        }
    }
}

static inline void rig_frames(unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        rig_frame();
    }
}

static inline bool rig_at_rest(void)
{
    const kst_prog_state_t st = kst_prog_state(kst_panel_model());

    return st == KST_PROG_PWM || st == KST_PROG_PROGRAMMING
        || st == KST_PROG_DOWN || st == KST_PROG_NO_PAGE;
}

/* Until nothing runs; 60s is the bound.  False when something still does. */
static inline bool rig_settle(void)
{
    rig_frame();
    for (unsigned i = 0; i < 60000u && !rig_at_rest(); ++i) {
        rig_frame();
    }
    return rig_at_rest();
}

/* An IOmcu answers with protocol minor @p minor. */
static inline bool rig_link_up(uint16_t minor)
{
    kst_link_came_up(&g_rig.link, minor);
    return rig_settle();
}

static inline void rig_link_lost(void)
{
    kst_link_lost(&g_rig.link);
    rig_frame();
}

/* STOP as the control task latches it: the port aborts, the module too. */
static inline void rig_stop(bool on)
{
    g_rig.stop = on;
    g_rig.stopped = on;
    if (on) {
        kst_link_abort(&g_rig.link);
    }
}

/* The servo's registers as they are. */
static inline kst_image_t rig_servo(void)
{
    kst_image_t img;

    memcpy(img.r, g_rig.sim.reg, sizeof(img.r));
    return img;
}
