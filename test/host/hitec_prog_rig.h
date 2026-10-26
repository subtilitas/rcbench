/*
 * The bench the Hitec programmer's suites run on: the loopback of
 * hitec_link_rig.h (the panel's link module, the IOmcu's link dispatch, the
 * HITEC page and the servo model of hitec_sim.h) with the panel's two
 * halves around it as firmware/panel/main/panel_hitec.c and panel_render.c
 * have them: the control task's pass every 5ms (the queued commands claimed
 * at the gate and carried out, a stop's abort, hitec_link_service(), the
 * feed published) and the render loop's frame every g_frame_ms (the model
 * fed, its commands queued).
 *
 * The model is either a hitec_prog_t of the suite's own (g_p) or the
 * overlay's (hitec_panel.h), as rig_use_panel() says.
 *
 * The panel's facts the feed carries are the rig's: the STOP latch, the
 * outputs armed, the tester's target, the port's pin and the receive
 * mode's mark.  A STOP latches here, withholds the heartbeat (the IOmcu's
 * safe goes false) and switches the bank off; a switch to ARMED clears the
 * latch, as panel_arm.c does, and is refused while a programmer holds a
 * port (hitec_prog_port_taken()).
 *
 * The card: g_card mounted takes the record's text (hitec_card.h) in the
 * frame that offers it, unless g_card_fails refuses it or g_card_slow
 * keeps it from answering; a panel that starts again reads it back.
 *
 * For test_hitec_prog*.c and test_hitec_panel*.c.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "hitec_link_rig.h"

#include "hitec_panel.h"
#include "hitec_prog.h"
#include "hitec_reg.h"
#include "hitec_reg_fix.h"
#include "hitec_rx_link.h"

/* The control task's queue: HITEC_Q_LEN in main.c. */
#define RIG_Q_LEN 4u

static hitec_prog_t      g_p;
static hitec_prog_gate_t g_gate;
static bool              g_panel;       /* the overlay's model, not g_p */

static hitec_prog_cmd_t  g_q[RIG_Q_LEN];
static unsigned          g_q_n;
static bool              g_q_hold;      /* the control task takes nothing */
static unsigned          g_q_lost;      /* commands the queue had no room for */

/* The control task's half (panel_hitec.c). */
static uint32_t             g_ans_id;
static uint8_t              g_ans;
static uint32_t             g_outs;
static hitec_link_outcome_t g_out;
static uint32_t             g_stops, g_stops_seen;

/* The panel's facts. */
static bool     g_stopped;              /* STOP latched                  */
static bool     g_armed;                /* the outputs armed             */
static uint8_t  g_target;               /* 0 standard, 1 + n port n      */
static bool     g_pin_known;            /* port CH names PIN             */
static uint8_t  g_rx_mark;              /* hitec_rx_link_mark_t          */
static bool     g_link;                 /* the link is up                */
static unsigned g_frame_ms = 16u;       /* the render loop's frame       */
static unsigned g_arm_refused;          /* switches to ARMED refused     */

static hitec_prog_feed_t g_f;

/* The card. */
static bool     g_card;                 /* mounted                       */
static bool     g_card_fails;           /* takes no text                 */
static bool     g_card_slow;            /* takes nothing, answers never  */
static char     g_card_text[HITEC_CARD_TEXT_MAX];
static size_t   g_card_len;             /* the text it holds; 0 none     */
static unsigned g_card_writes;          /* texts it took                 */

/* ------------------------------------------------------------- the model */

static inline hitec_prog_t *model(void)
{
    return g_panel ? hitec_panel_model() : &g_p;
}

static inline void model_feed(const hitec_prog_feed_t *f)
{
    if (g_panel) {
        hitec_panel_feed(f);
    } else {
        hitec_prog_feed(&g_p, f);
    }
}

static inline bool model_take(hitec_prog_cmd_t *c)
{
    return g_panel ? hitec_panel_take(c) : hitec_prog_take(&g_p, c);
}

/* ------------------------------------------------- the control task's half */

static inline void carry_out(const hitec_prog_cmd_t *c)
{
    hitec_link_start_t a = HITEC_LINK_START_ARG;

    switch (c->kind) {
    case HITEC_PROG_CMD_OPEN:
        a = g_armed ? HITEC_LINK_START_BUSY
                    : hitec_link_open(&g_l, c->port, c->family, c->drive,
                                      c->id);
        break;
    case HITEC_PROG_CMD_STEP:
        a = hitec_link_step(&g_l, c->port, &c->step, c->mv, c->ma, c->id);
        break;
    case HITEC_PROG_CMD_CLEAR:
        a = hitec_link_clear(&g_l, c->port, c->id);
        break;
    case HITEC_PROG_CMD_CLOSE:
        a = hitec_link_close(&g_l, c->port, c->id);
        break;
    default:
        hitec_link_abort(&g_l);
        return;
    }
    g_ans = (uint8_t)a;
    g_ans_id = c->id;
}

static inline void commands(void)
{
    if (!g_q_hold) {
        for (unsigned i = 0; i < g_q_n; ++i) {
            if (g_q[i].kind != HITEC_PROG_CMD_ABORT
                && !hitec_prog_gate_claim(&g_gate, g_q[i].id)) {
                continue;
            }
            carry_out(&g_q[i]);
        }
        g_q_n = 0;
    }
    if (g_stops != g_stops_seen) {
        g_stops_seen = g_stops;
        hitec_link_abort(&g_l);
    }
}

static inline void publish(hitec_prog_feed_t *f)
{
    hitec_link_outcome_t o;

    if (hitec_link_outcome(&g_l, &o)) {
        g_out = o;
        ++g_outs;
    }
    memset(f, 0, sizeof(*f));
    f->now_ms = g_ms;
    hitec_link_readout(&g_l, &f->rd);
    f->answer_id = g_ans_id;
    f->answer = g_ans;
    f->outcomes = g_outs;
    f->outcome = g_out;
    f->stopped = g_stopped;
    f->armed = g_armed;
    f->target = g_target;
    for (unsigned i = 0; i < LINK_PG_PORTS_MAX; ++i) {
        f->port_pin[i] = HITEC_PROG_NO_PIN;
    }
    if (g_pin_known) {
        f->port_pin[CH] = PIN;
        f->port_pin_known = (uint8_t)(1u << CH);
    }
    f->rx_mark = g_rx_mark;
    f->supply_read = g_hw.has_supply && g_hw.v.online && g_hw.v.on;
    if (f->supply_read) {
        f->out_mv = g_hw.v.set_mv;
        f->out_ma = 41u;
    }
    /* From 4.29 a held port holds the SUPPLY page too (LINK_SP_PORT). */
    f->supply_held = g_hw.has_supply
                     && hitec_port_state(&g_port) != LINK_HITEC_FREE;
    f->card = g_card;
}

/* The render loop's card half: the record's text to the card. */
static inline void card_frame(void)
{
    char text[HITEC_CARD_TEXT_MAX];
    size_t len;

    if (g_card_slow) {
        return;
    }
    len = hitec_prog_file_take(model(), text, sizeof(text));
    if (len == 0u) {
        return;
    }
    if (!g_card_fails) {
        memcpy(g_card_text, text, len);
        g_card_len = len;
        g_card_writes++;
    }
    hitec_prog_file_done(model(), !g_card_fails);
}

/* The control task's pass. */
static inline void control_pass(void)
{
    commands();
    if (g_link) {
        pass();
    }
    publish(&g_f);
}

/* The render loop's frame. */
static inline void frame(void)
{
    hitec_prog_cmd_t c;

    model_feed(&g_f);
    while (g_q_n < RIG_Q_LEN && model_take(&c)) {
        g_q[g_q_n++] = c;
    }
    card_frame();
}

/* 1ms: a pass of the IOmcu, every 5ms the control task's, every
 * g_frame_ms a frame. */
static inline void pms(void)
{
    tick_as(g_safe, false);
    if (g_ms % HITEC_LINK_POLL_MS == 0u) {
        control_pass();
    }
    if (g_ms % g_frame_ms == 0u) {
        frame();
    }
}

static inline void prun(unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        pms();
    }
}

static inline hitec_prog_state_t pstate(void)
{
    return hitec_prog_state(model());
}

/* Run until the model is in @p s; 60s is the bound. */
static inline void until(hitec_prog_state_t s)
{
    for (unsigned i = 0; i < 60000u && pstate() != s; ++i) {
        pms();
    }
    CHECK_EQ(pstate(), s);
}

/* Run until the model leaves @p s; 60s is the bound. */
static inline void past(hitec_prog_state_t s)
{
    for (unsigned i = 0; i < 60000u && pstate() == s; ++i) {
        pms();
    }
    CHECK(pstate() != s);
}

static inline unsigned msg(void)
{
    return hitec_prog_message(model())->id;
}

/* ----------------------------------------------------------- the servos */

/* The D485HW of 2026-10-20 on the line. */
static inline void servo_d485(void)
{
    for (unsigned i = 0; i < 128u; ++i) {
        g_drv.dreg[2u * i] = fix_d485()[i];
    }
}

/* The HS-5645MG of 2026-10-18, its entry answered. */
static inline void servo_hs5645(void)
{
    hitec_image_t img;

    hs5645(&img);
    for (unsigned c = 0; c < 256u; ++c) {
        g_drv.eeprom[c] = hitec_image_cell(&img, (uint8_t)c);
    }
    g_drv.c_entry = true;
    g_drv.boot_byte = HITEC_C_ACK;
    g_drv.boot_ns = 59000000u;   /* 59ms after the ON */
    g_drv.version = 0x16u;
}

/* ------------------------------------------------------------ the bench */

/* A fresh bench from @p t0: the page up at @p supply, the tester on port
 * CH with its pin, the receive mode held, SAFE, the model fed. */
static inline void rig_from(uint32_t t0, supply_kind_t supply, bool panel)
{
    bench_at(t0, supply, 1u);
    g_panel = panel;
    hitec_prog_gate_init(&g_gate);
    if (panel) {
        hitec_panel_gate(&g_gate);
    } else {
        hitec_prog_init(&g_p);
        hitec_prog_gate_set(&g_p, &g_gate);
    }
    g_q_n = 0;
    g_q_hold = false;
    g_q_lost = 0;
    g_ans_id = 0;
    g_ans = 0;
    g_outs = 0;
    memset(&g_out, 0, sizeof(g_out));
    g_stops = 0;
    g_stops_seen = 0;
    g_stopped = false;
    g_armed = false;
    g_target = (uint8_t)(1u + CH);
    g_pin_known = true;
    g_rx_mark = (uint8_t)HITEC_RX_LINK_HELD;
    g_frame_ms = 16u;
    g_arm_refused = 0;
    g_card = false;
    g_card_fails = false;
    g_card_slow = false;
    g_card_len = 0;
    g_card_writes = 0;
    memset(&g_f, 0, sizeof(g_f));
    g_link = true;
    hitec_link_came_up(&g_l, LINK_PROTOCOL_MINOR);
    until(HITEC_PROG_PWM);
}

static inline void rig_at(supply_kind_t supply, bool panel)
{
    rig_from(0u, supply, panel);
}

static inline void rig(void)
{
    rig_at(SUPPLY_ON, false);
}

/* A STOP at the panel: latched, the heartbeat withheld, the bank off. */
static inline void press_stop(void)
{
    g_stopped = true;
    g_stops++;
    g_armed = false;
    g_safe = false;
    stop_pulse();
}

/* A switch to SAFE at the bench alone: no latch, the heartbeat goes on. */
static inline void disarm(void)
{
    g_armed = false;
    stop_pulse();
}

/* A switch to ARMED as panel_arm.c makes it: refused while a programmer
 * holds a port; otherwise the latch clears, the heartbeat resumes and the
 * IOmcu's failsafe is cleared (CLEAR 0x5AFE).  @return whether it armed. */
static inline bool arm(void)
{
    hitec_link_readout_t rd;

    hitec_link_readout(&g_l, &rd);
    if (hitec_prog_port_taken(&rd)) {
        g_arm_refused++;
        return false;
    }
    g_stopped = false;
    g_safe = true;
    g_armed = true;
    return true;
}

/* The link goes silent: the panel's module drops it and latches a stop,
 * the IOmcu's failsafe latches. */
static inline void link_silent(void)
{
    g_link = false;
    hitec_link_lost(&g_l);
    g_stopped = true;
    g_stops++;
    g_safe = false;
    stop_pulse();
}

static inline void link_back(void)
{
    g_link = true;
    hitec_link_came_up(&g_l, LINK_PROTOCOL_MINOR);
}

/* The panel starts again: a fresh link module and model, the page as it
 * was.  The IOmcu saw the link silent: its failsafe is latched. */
static inline void panel_restart(void)
{
    g_safe = false;
    stop_pulse();
    hitec_link_init(&g_l);
    hitec_prog_gate_init(&g_gate);
    if (g_panel) {
        hitec_panel_gate(&g_gate);
    } else {
        hitec_prog_init(&g_p);
        hitec_prog_gate_set(&g_p, &g_gate);
    }
    g_q_n = 0;
    g_ans_id = 0;
    g_outs = 0;
    g_stopped = false;
    /* The record as the card holds it, read at the start. */
    if (g_card && g_card_len != 0u) {
        (void)hitec_prog_file_load(model(), g_card_text, g_card_len);
    }
    hitec_link_came_up(&g_l, LINK_PROTOCOL_MINOR);
}

/* The CONNECT given runs through the entry and the read: held, nothing
 * running. */
static inline void settled(void)
{
    past(HITEC_PROG_OPENING);
    for (unsigned i = 0; i < 60000u
                         && (pstate() == HITEC_PROG_ENTERING
                             || pstate() == HITEC_PROG_READING);
         ++i) {
        pms();
    }
    CHECK_EQ(pstate(), HITEC_PROG_HELD);
}

/* CONNECT and the whole entry and read. */
static inline void connected(void)
{
    CHECK_EQ(hitec_prog_connect(model()), HITEC_PROG_MSG_NONE);
    settled();
}
