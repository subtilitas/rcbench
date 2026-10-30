/**
 * @file
 * @brief The control task's half of the programmer for servos by Hitec: the
 *        HITEC page's link module (hitec_link.h), the commands of the
 *        programmer's model and what the model is fed.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * The model (hitec_prog.h) runs in the render loop.  It hands its commands
 * over through s_hitec_q and reads what became of them in the snapshot: the
 * number of the last command carried out with what the link module said,
 * and a count of the outcomes handed out with the last of them.
 *
 * hitec_link_service() runs every pass of the control task, 5ms, with at
 * most HITEC_LINK_PASS_MAX exchanges, so a request's status read follows
 * the page within one pass.  hitec_commands(), hitec_pass() and
 * hitec_publish() are kept out of line: their locals stay out of
 * control_task()'s frame, from which the task's deepest chain starts.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "arming.h"
#include "hitec_link.h"
#include "hitec_prog.h"
#include "hitec_rx_link.h"
#include "link_host.h"
#include "link_pages.h"
#include "outputs.h"
#include "panel.h"
#include "prog_link.h"
#include "storage.h"
#include "supply_link.h"

/** @brief The HITEC page's module (4.23).  Control task only. */
hitec_link_t s_hitec_link;

/** @brief The gate a command in the queue is claimed at (hitec_prog.h). */
hitec_prog_gate_t s_hitec_gate;

/** @brief The model's commands, app_main -> control task. */
QueueHandle_t s_hitec_q;

/* What became of the commands and the requests.  Control task only. */
/** @brief The id of the last command carried out; 0 none. */
static uint32_t             s_hitec_answer_id;
/** @brief What the link module said to it: hitec_link_start_t. */
static uint8_t              s_hitec_answer;
/** @brief Counts the outcomes handed out. */
static uint32_t             s_hitec_outcomes;
/** @brief The last outcome handed out. */
static hitec_link_outcome_t s_hitec_outcome;
/** @brief The stops an abort was sent for: arming_stop_count()'s count. */
static uint32_t             s_hitec_stops;

/**
 * @brief Carry out one command.
 *
 * No port is taken on an armed bench: an OPEN is answered BUSY and nothing
 * is sent.
 *
 * @param c The command.
 */
static void hitec_command(const hitec_prog_cmd_t *c)
{
    hitec_link_start_t a = HITEC_LINK_START_ARG;

    switch (c->kind) {
    case HITEC_PROG_CMD_OPEN:
        a = outputs_armed(&s_out)
                ? HITEC_LINK_START_BUSY
                : hitec_link_open(&s_hitec_link, c->port, c->family,
                                  c->drive, c->id);
        break;
    case HITEC_PROG_CMD_STEP:
        a = hitec_link_step(&s_hitec_link, c->port, &c->step, c->mv, c->ma,
                            c->id);
        break;
    case HITEC_PROG_CMD_CLEAR:
        a = hitec_link_clear(&s_hitec_link, c->port, c->id);
        break;
    case HITEC_PROG_CMD_CLOSE:
        a = hitec_link_close(&s_hitec_link, c->port, c->id);
        break;
    default:
        hitec_link_abort(&s_hitec_link);
        return;
    }
    s_hitec_answer = (uint8_t)a;
    s_hitec_answer_id = c->id;
}

/*
 * The model's commands, every pass and whatever the link does: a command
 * with no link is answered HITEC_LINK_START_DOWN, and the model says so.
 *
 * And STOP.  Every stop this end latches, pressed or the bench's own, ends
 * a request: hitec_link_abort() drops a command not sent yet, sends no
 * command again that went without an answer, and sends ABORT for one the
 * IOmcu runs.  After the commands, so a command queued before the stop is
 * dropped by it.
 */
__attribute__((noinline)) void hitec_commands(void)
{
    hitec_prog_cmd_t cmd;

    while (xQueueReceive(s_hitec_q, &cmd, 0) == pdTRUE) {
        if (cmd.kind != HITEC_PROG_CMD_ABORT
            && !hitec_prog_gate_claim(&s_hitec_gate, cmd.id)) {
            /* Given up by the model while it waited here: not carried out
             * and not answered. */
            continue;
        }
        hitec_command(&cmd);
    }
    const uint32_t stops = arming_stop_count(&s_arm);
    if (stops != s_hitec_stops) {
        s_hitec_stops = stops;
        hitec_link_abort(&s_hitec_link);
    }
}

/**
 * @brief One exchange of the HITEC page, as hitec_link_xchg_fn: a write, or
 *        a read of up to 32 registers into @p regs.
 *
 * @param ctx  Unused.
 * @param x    The exchange.
 * @param regs Receives a read's registers.
 * @return HITEC_LINK_ACK, a NACK's reason, or HITEC_LINK_NO_ANSWER.
 */
static int hitec_xchg(void *ctx, const hitec_link_xfer_t *x, uint16_t *regs)
{
    link_msg_t reply = { 0 };
    const bool answered =
        x->write ? write_regs(&s_host, x->page, x->off, x->n, x->regs, &reply)
                 : read_regs(&s_host, x->page, x->off, x->n, &reply);

    (void)ctx;
    if (!answered) {
        return HITEC_LINK_NO_ANSWER;
    }
    if (reply.op == LINK_OP_NACK) {
        return (int)reply.regs[0];
    }
    if (!x->write) {
        if (reply.op != LINK_OP_DATA || reply.count < x->n) {
            return HITEC_LINK_NO_ANSWER;
        }
        memcpy(regs, reply.regs, (size_t)x->n * sizeof(regs[0]));
    }
    return HITEC_LINK_ACK;
}

/*
 * The HITEC page, one pass: status read every HITEC_LINK_POLL_MS while a
 * request runs, every HITEC_LINK_HELD_MS while the page holds a port and
 * every HITEC_LINK_IDLE_MS otherwise; a request's staged registers and
 * command; its capture (hitec_link.h).  At most HITEC_LINK_PASS_MAX
 * exchanges; nothing while the link is down, and an IOmcu older than 4.23
 * is sent nothing.
 */
__attribute__((noinline)) void hitec_pass(bool link_up)
{
    if (link_up) {
        (void)hitec_link_service(&s_hitec_link, now_ms(), hitec_xchg, NULL);
    }
}

bool hitec_taken(void)
{
    hitec_link_readout_t rd;

    hitec_link_readout(&s_hitec_link, &rd);
    return hitec_prog_port_taken(&rd);
}

int hitec_taken_port(void)
{
    hitec_link_readout_t rd;

    hitec_link_readout(&s_hitec_link, &rd);
    return (hitec_prog_port_taken(&rd) && rd.port_no < LINK_PG_PORTS_MAX)
               ? (int)rd.port_no : -1;
}

/* What the model is fed, as it stands.  Under the snapshot's lock. */
__attribute__((noinline)) void hitec_publish(hitec_prog_feed_t *f)
{
    hitec_link_outcome_t o;

    if (hitec_link_outcome(&s_hitec_link, &o)) {
        s_hitec_outcome = o;
        ++s_hitec_outcomes;
    }
    f->now_ms    = now_ms();
    hitec_link_readout(&s_hitec_link, &f->rd);
    f->answer_id = s_hitec_answer_id;
    f->answer    = s_hitec_answer;
    f->outcomes  = s_hitec_outcomes;
    f->outcome   = s_hitec_outcome;
    f->stopped   = arming_stopped(&s_arm);
    f->armed     = outputs_armed(&s_out);
    f->target    = prog_link_target(&s_prog_link);
    f->port_pin_known = 0u;
    for (uint8_t port = 0; port < LINK_PG_PORTS_MAX; ++port) {
        const uint8_t pin = prog_link_pin(&s_prog_link, port);

        f->port_pin[port] = pin;
        if (pin != PROG_LINK_NO_PIN) {
            f->port_pin_known |= (uint8_t)(1u << port);
        }
    }
    f->rx_mark = (uint8_t)hitec_rx_link_mark(&s_hitec_rx_link);
    /* The PD mini's output while it is the supply, answers and is on.  On
     * a board with the ports' own supply the PD mini feeds the servo rails
     * only, and the ports' 5V has no reading: none. */
    f->supply_read = s_supply_is_real && s_supply.online && s_supply.output
                     && !atomic_load(&s_psup_owns);
    f->fixed_on = atomic_load(&s_psup_owns)
                  && prog_supply_link_state(&s_psup_link) == PROG_SUPPLY_ON;
    f->out_mv = 0u;
    f->out_ma = 0u;
    if (f->supply_read) {
        const float mv = s_supply.v * 1000.0f + 0.5f;
        const float ma = s_supply.i * 1000.0f + 0.5f;

        f->out_mv = mv <= 0.0f ? 0u : mv >= 65535.0f ? 65535u : (uint16_t)mv;
        f->out_ma = ma <= 0.0f ? 0u : ma >= 65535.0f ? 65535u : (uint16_t)ma;
    }
    f->supply_held = supply_link_port_holds(&s_supply_link, s_supply_is_real);
    /* A card mounted: a write's record and its backup file go to it
     * (hitec_record.c, hitec_files.c); what the slot holds otherwise. */
    f->card = storage_mounted();
    f->card_state = (uint8_t)storage_card_state();
}
