/**
 * @file
 * @brief The control task's half of the ESC programmer: the ESC_WIRE page's link
 *        module (esc_link.h), the model's commands, a stop and what the model is
 *        fed.
 *
 * The model (esc_prog.h) runs in the render loop.  It hands its commands over
 * through s_esc_q and reads what became of them in the snapshot.
 *
 * esc_link_service() runs every pass of the control task, 5ms, with at most
 * ESC_LINK_PASS_MAX exchanges.  esc_commands(), esc_pass() and esc_publish() are
 * kept out of line: their locals stay out of control_task()'s frame.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "arming.h"
#include "esc_link.h"
#include "esc_prog.h"
#include "link_host.h"
#include "link_pages.h"
#include "outputs.h"
#include "panel.h"
#include "ui_text.h"

/** @brief The ESC_WIRE page's module (4.34).  Control task only. */
esc_link_t s_esc_link;

/** @brief The gate a command in the queue is claimed at (esc_prog.h). */
esc_prog_gate_t s_esc_gate;

/** @brief The model's commands, app_main -> control task. */
QueueHandle_t s_esc_q;

/* What became of the commands and the requests.  Control task only. */
/** @brief The id of the last command carried out; 0 none. */
static uint32_t           s_esc_answer_id;
/** @brief What the link module said to it: esc_link_start_t. */
static uint8_t            s_esc_answer;
/** @brief Counts the outcomes handed out. */
static uint32_t           s_esc_outcomes;
/** @brief The last outcome handed out. */
static esc_link_outcome_t s_esc_outcome;

/**
 * @brief Carry out one command.  No pin is taken on an armed bench: an OPEN is
 *        answered BUSY and nothing is sent.  With a stop latched only what gives
 *        the pin back or cuts goes (esc_prog_cmd_allowed()).
 * @param c The command.
 */
static void esc_command(const esc_prog_cmd_t *c)
{
    esc_link_start_t a = ESC_LINK_START_ARG;

    if (!esc_prog_cmd_allowed(c->kind, arming_stopped(&s_arm))) {
        s_esc_answer = (uint8_t)ESC_LINK_START_BUSY;
        s_esc_answer_id = c->id;
        return;
    }
    switch (c->kind) {
    case ESC_PROG_CMD_OPEN:
        a = outputs_armed(&s_out) ? ESC_LINK_START_BUSY
                                  : esc_link_open(&s_esc_link, c->target, c->id);
        break;
    case ESC_PROG_CMD_ENTER:
        a = esc_link_enter(&s_esc_link, &c->power, c->id);
        break;
    case ESC_PROG_CMD_POWERED:
        a = esc_link_powered(&s_esc_link, c->id);
        break;
    case ESC_PROG_CMD_READ:
        a = esc_link_read(&s_esc_link, c->id);
        break;
    case ESC_PROG_CMD_WRITE:
        a = esc_link_write(&s_esc_link, c->draft, c->len, c->reads, c->read_crc,
                           c->id);
        break;
    case ESC_PROG_CMD_RESTORE:
        a = esc_link_restore(&s_esc_link, c->draft, c->len, c->identity, c->reads,
                             c->read_crc, c->id);
        break;
    case ESC_PROG_CMD_LEAVE:
        a = esc_link_leave(&s_esc_link, c->id);
        break;
    case ESC_PROG_CMD_CLEAR:
        a = esc_link_clear(&s_esc_link, c->id);
        break;
    case ESC_PROG_CMD_CLOSE:
        a = esc_link_close(&s_esc_link, c->id);
        break;
    default:
        esc_link_abort(&s_esc_link);
        return;
    }
    s_esc_answer = (uint8_t)a;
    s_esc_answer_id = c->id;
}

/**
 * @brief The alert a pin that did not come back gets: it names the slot or port.
 *        The bank stays SAFE, ARM is refused, and the CLOSE goes again.
 */
static void esc_not_back_alert(void)
{
    uint16_t target = 0u;

    if (esc_link_not_back(&s_esc_link, &target)) {
        char line[UI_ALERT_MAX];
        const unsigned index = target & 0xFFu;

        snprintf(line, sizeof(line),
                 (target & LINK_EW_TARGET_PORT) != 0u ? TR(ALERT_ESC_NOT_BACK_PORT)
                                                      : TR(ALERT_ESC_NOT_BACK_SLOT),
                 index);
        control_alert(line);
    }
}

/*
 * The model's commands, every pass and whatever the link does: a command with no
 * link is answered ESC_LINK_START_DOWN.
 *
 * And STOP.  Every stop this end latches, pressed or the bench's own, is the
 * page's STOP operation: with ARM refused from OPEN on no disarm reaches a
 * session.  After the commands, so a command queued before the stop is dropped by
 * it.
 */
__attribute__((noinline)) void esc_commands(void)
{
    static esc_prog_cmd_t cmd;

    while (xQueueReceive(s_esc_q, &cmd, 0) == pdTRUE) {
        if (cmd.kind != ESC_PROG_CMD_ABORT
            && !esc_prog_gate_claim(&s_esc_gate, cmd.id)) {
            /* Given up by the model while it waited here. */
            continue;
        }
        esc_command(&cmd);
    }
    esc_link_stop_follow(&s_esc_link, arming_stop_count(&s_arm));
}

/**
 * @brief One exchange of the pages, as esc_link_xchg_fn.
 * @param ctx  Unused.
 * @param x    The exchange.
 * @param regs Receives a read's registers.
 * @return ESC_LINK_ACK, a NACK's reason, or ESC_LINK_NO_ANSWER.
 */
static int esc_xchg(void *ctx, const esc_link_xfer_t *x, uint16_t *regs)
{
    link_msg_t reply = { 0 };
    const bool answered =
        x->write ? write_regs(&s_host, x->page, x->off, x->n, x->regs, &reply)
                 : read_regs(&s_host, x->page, x->off, x->n, &reply);

    (void)ctx;
    if (!answered) {
        return ESC_LINK_NO_ANSWER;
    }
    if (reply.op == LINK_OP_NACK) {
        return (int)reply.regs[0];
    }
    if (!x->write) {
        if (reply.op != LINK_OP_DATA || reply.count < x->n) {
            return ESC_LINK_NO_ANSWER;
        }
        memcpy(regs, reply.regs, (size_t)x->n * sizeof(regs[0]));
    }
    return ESC_LINK_ACK;
}

__attribute__((noinline)) void esc_pass(bool link_up)
{
    if (link_up) {
        (void)esc_link_service(&s_esc_link, now_ms(), esc_xchg, NULL);
    }
    esc_not_back_alert();
}

bool esc_taken(void)
{
    esc_link_readout_t rd;

    esc_link_readout(&s_esc_link, &rd);
    return esc_prog_port_taken(&rd);
}

__attribute__((noinline)) void esc_publish(esc_prog_feed_t *f)
{
    esc_link_outcome_t o;

    if (esc_link_outcome(&s_esc_link, &o)) {
        s_esc_outcome = o;
        ++s_esc_outcomes;
    }
    f->now_ms = now_ms();
    esc_link_readout(&s_esc_link, &f->rd);
    f->image = *esc_link_image(&s_esc_link);
    f->answer_id = s_esc_answer_id;
    f->answer = s_esc_answer;
    f->outcomes = s_esc_outcomes;
    f->outcome = s_esc_outcome;
    f->stopped = arming_stopped(&s_arm);
    f->armed = outputs_armed(&s_out);
}
