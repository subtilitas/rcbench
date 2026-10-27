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
 * keeps it from answering; a panel that starts again reads it back.  The
 * servo files (hitec_card_run.h) live in g_files, the folder
 * BENCH/HITEC/CONF in memory behind servo_store_io_t: a request is carried
 * out in the frame that offers it, unless g_files_slow keeps it unanswered
 * until files_answer().
 *
 * For test_hitec_prog*.c and test_hitec_panel*.c.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "hitec_link_rig.h"

#include <ctype.h>

#include "hitec_card_run.h"
#include "hitec_panel.h"
#include "hitec_prog.h"
#include "servo_store.h"
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

/* The servo files. */
#define RIG_FILES 1010u
typedef struct {
    char   name[48];                    /* with its extension            */
    char   text[HITEC_CARD_FILE_MAX + 16u];
    size_t len;
    bool   dir;
} rig_file_t;
static rig_file_t g_files[RIG_FILES];
static unsigned   g_files_n;
static bool       g_files_slow;         /* taken, not answered           */
static uint8_t    g_files_create;       /* servo_store_io_result_t       */
static bool       g_files_walk_fails;
static bool       g_files_read_fails;
static unsigned   g_files_saves, g_files_lists, g_files_loads;
static bool       g_creq_out;           /* a request waits for its answer */
static hitec_card_req_t  g_creq;
static hitec_card_done_t g_cdone;

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


/* ---------------------------------------------------------- the files */

static inline bool files_same(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static inline rig_file_t *files_find(const char *name)
{
    for (unsigned i = 0; i < g_files_n; ++i) {
        if (!g_files[i].dir && files_same(g_files[i].name, name)) {
            return &g_files[i];
        }
    }
    return NULL;
}

static inline bool files_mounted(void *ctx)
{
    (void)ctx;
    return g_card;
}

static inline bool files_walk(void *ctx, servo_store_visit_fn visit,
                              void *visit_ctx)
{
    (void)ctx;
    for (unsigned i = 0; i < g_files_n; ++i) {
        visit(g_files[i].name, g_files[i].dir, visit_ctx);
    }
    return !g_files_walk_fails;
}

static inline int32_t files_size(void *ctx, const char *name)
{
    const rig_file_t *rf = files_find(name);

    (void)ctx;
    return rf != NULL ? (int32_t)rf->len : -1;
}

static inline int32_t files_read(void *ctx, const char *name, char *buf,
                                 size_t cap)
{
    const rig_file_t *rf = files_find(name);
    size_t n;

    (void)ctx;
    if (rf == NULL || g_files_read_fails) {
        return -1;
    }
    n = rf->len < cap ? rf->len : cap;
    memcpy(buf, rf->text, n);
    return (int32_t)n;
}

static inline servo_store_io_result_t files_create(void *ctx,
                                                   const char *name,
                                                   const char *data,
                                                   size_t len)
{
    rig_file_t *rf;

    (void)ctx;
    if (files_find(name) != NULL) {
        return SERVO_STORE_IO_EXISTS;
    }
    if (g_files_create != (uint8_t)SERVO_STORE_IO_OK) {
        return (servo_store_io_result_t)g_files_create;
    }
    if (g_files_n == RIG_FILES || len > sizeof(rf->text)) {
        return SERVO_STORE_IO_FULL;
    }
    rf = &g_files[g_files_n++];
    memset(rf, 0, sizeof(*rf));
    snprintf(rf->name, sizeof(rf->name), "%s", name);
    memcpy(rf->text, data, len);
    rf->len = len;
    return SERVO_STORE_IO_OK;
}

static const servo_store_io_t g_files_io = {
    .ctx = NULL,
    .stem = HITEC_CARD_STEM,
    .mounted = files_mounted,
    .walk = files_walk,
    .size = files_size,
    .read = files_read,
    .create = files_create,
};

/* A file of @p text under @p name, the extension included. */
static inline void files_put(const char *name, const char *text, size_t len)
{
    rig_file_t *rf = &g_files[g_files_n++];

    memset(rf, 0, sizeof(*rf));
    snprintf(rf->name, sizeof(rf->name), "%s", name);
    memcpy(rf->text, text, len);
    rf->len = len;
}

/* A servo file of @p img under @p name, the extension included. */
static inline void files_put_image(const char *name, const hitec_image_t *img)
{
    char text[HITEC_CARD_FILE_MAX];
    const size_t len = hitec_card_file_text(img, false, text, sizeof(text));

    files_put(name, text, len);
}

/* The image of the file @p name; false for none that parses. */
static inline bool files_image(const char *name, hitec_image_t *out)
{
    const rig_file_t *rf = files_find(name);

    return rf != NULL && hitec_card_file_parse(rf->text, rf->len, out, NULL,
                                              NULL, NULL) == HITEC_CARD_OK;
}

static inline void files_reset(void)
{
    g_files_n = 0;
    g_files_slow = false;
    g_files_create = (uint8_t)SERVO_STORE_IO_OK;
    g_files_walk_fails = false;
    g_files_read_fails = false;
    g_files_saves = g_files_lists = g_files_loads = 0;
    g_creq_out = false;
}

/* The request out answered: carried out on g_files now. */
static inline void files_answer(void)
{
    if (!g_creq_out) {
        return;
    }
    g_creq_out = false;
    hitec_card_run(&g_files_io, &g_creq, &g_cdone);
    if (g_creq.op == HITEC_CARD_SAVE) {
        g_files_saves++;
    } else if (g_creq.op == HITEC_CARD_LIST) {
        g_files_lists++;
    } else {
        g_files_loads++;
    }
    if (g_panel) {
        hitec_panel_card_done(&g_cdone);
    } else {
        hitec_prog_card_done(&g_p, &g_cdone);
    }
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
    f->card_state = g_card ? 1u : 0u;
}

/* The render loop's card half: a request of the files, then the record's
 * text to the card. */
static inline void card_frame(void)
{
    char text[HITEC_CARD_TEXT_MAX];
    size_t len;

    if (!g_creq_out
        && (g_panel ? hitec_panel_card_take(&g_creq)
                    : hitec_prog_card_take(&g_p, &g_creq))) {
        g_creq_out = true;
    }
    if (!g_files_slow) {
        files_answer();
    }

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
    files_reset();
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
    g_creq_out = false;   /* a request of the old panel is gone */
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

/* A card goes in under a held session: the record goes to it, and a save
 * by hand, PRE, is the session's backup file, so a write goes to the
 * record at once. */
static inline void card_in(void)
{
    g_card = true;
    prun(20u);
    CHECK_EQ(hitec_prog_save(model(), "PRE"), HITEC_PROG_MSG_NONE);
    prun(40u);
    CHECK_EQ(msg(), HITEC_PROG_MSG_SAVED);
    CHECK_STR_EQ(hitec_prog_backup_name(model()), "PRE");
}

/* CONNECT and the whole entry and read. */
static inline void connected(void)
{
    CHECK_EQ(hitec_prog_connect(model()), HITEC_PROG_MSG_NONE);
    settled();
}
