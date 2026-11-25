/**
 * @file
 * @brief The control task's half of programming for YGE ESCs: the YGE_PROG
 *        page's link module (yge_link.h), the model's commands, a stop, what
 *        the page holds and what the model is fed.
 *
 * The model (yge_prog.h) runs in the render loop (yge_panel.h).  It hands its
 * commands over s_yge_q; yge_commands() carries out each one whose pass it
 * claims at s_yge_gate, and yge_publish() fills its feed under the
 * snapshot's lock.
 *
 * yge_link_service() runs every pass of the control task, 5ms, with at most
 * YGE_LINK_PASS_MAX exchanges.  The first read after a panel start closes a
 * pin the page holds; while it holds one, ARM is refused and the band's SAFE
 * chip names the port (panel_services.c), and each error the page reports is
 * an error of the bench (prog_faults()).  Every stop this end latches is the
 * page's STOP operation.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "arming.h"
#include "link_host.h"
#include "link_pages.h"
#include "panel.h"
#include "ui_text.h"
#include "yge_link.h"
#include "yge_prog.h"

/** @brief The YGE_PROG page's module (4.42).  Control task only. */
yge_link_t s_yge_link;

/** @brief The gate a command in the queue is claimed at (yge_prog.h). */
yge_prog_gate_t s_yge_gate;

/** @brief The model's commands, app_main -> control task. */
QueueHandle_t s_yge_q;

/* What became of the commands and the requests.  Control task only. */
/** @brief The id of the last command carried out; 0 none. */
static uint32_t           s_yge_answer_id;
/** @brief What the link module said to it: a yge_link_start_t. */
static uint8_t            s_yge_answer;
/** @brief Counts the outcomes handed out. */
static uint32_t           s_yge_outcomes;
/** @brief The last outcome handed out. */
static yge_link_outcome_t s_yge_outcome;

/**
 * @brief Carry out one command.  No pin is taken on an armed bench: an OPEN
 *        is answered BUSY and nothing is sent.  While an update runs (the C6
 *        radio's, the IOmcu's or a wireless device's) an OPEN and an ENTER,
 *        which powers the ESC, are answered BUSY.  With a stop latched only
 *        what gives the pin back or cuts goes (yge_prog_cmd_allowed()).
 * @param c The command.
 */
static void yge_command(const yge_prog_cmd_t *c)
{
    yge_link_start_t a = YGE_LINK_START_ARG;

    s_yge_answer_id = c->id;
    if (!yge_prog_cmd_allowed(c->kind, arming_stopped(&s_arm))
        || ((c->kind == YGE_PROG_CMD_OPEN || c->kind == YGE_PROG_CMD_ENTER)
            && prog_start_refused_upd())) {
        s_yge_answer = (uint8_t)YGE_LINK_START_BUSY;
        return;
    }
    switch (c->kind) {
    case YGE_PROG_CMD_OPEN:
        a = outputs_armed(&s_out) ? YGE_LINK_START_BUSY
                                  : yge_link_open(&s_yge_link, c->port, c->id);
        break;
    case YGE_PROG_CMD_ENTER:
        a = yge_link_enter(&s_yge_link, &c->power, c->id);
        break;
    case YGE_PROG_CMD_HAND_DONE:
        a = yge_link_hand_done(&s_yge_link, c->id);
        break;
    case YGE_PROG_CMD_READ:
        a = yge_link_read(&s_yge_link, c->id);
        break;
    case YGE_PROG_CMD_WRITE:
        a = yge_link_write(&s_yge_link, c->draft, c->n, c->reads, c->read_crc,
                           c->id);
        break;
    case YGE_PROG_CMD_LEAVE:
        a = yge_link_leave(&s_yge_link, c->id);
        break;
    case YGE_PROG_CMD_CLEAR:
        a = yge_link_clear(&s_yge_link, c->id);
        break;
    case YGE_PROG_CMD_CLOSE:
        a = yge_link_close(&s_yge_link, c->id);
        break;
    default:
        yge_link_abort(&s_yge_link);
        return;
    }
    s_yge_answer = (uint8_t)a;
}

/*
 * The model's commands, every pass and whatever the link does: a command
 * with no link is answered YGE_LINK_START_DOWN.  Before yge_pass(), so a
 * command queued before a stop is dropped by it.
 */
__attribute__((noinline)) void yge_commands(void)
{
    static yge_prog_cmd_t cmd;

    while (xQueueReceive(s_yge_q, &cmd, 0) == pdTRUE) {
        if (cmd.kind != YGE_PROG_CMD_ABORT
            && !yge_prog_gate_claim(&s_yge_gate, cmd.id)) {
            /* Given up by the model while it waited here. */
            continue;
        }
        yge_command(&cmd);
    }
}

/**
 * @brief One exchange of the pages, as yge_link_xchg_fn.
 * @param ctx  Unused.
 * @param x    The exchange.
 * @param regs Receives a read's registers.
 * @return YGE_LINK_ACK, a NACK's reason, or YGE_LINK_NO_ANSWER.
 */
static int yge_xchg(void *ctx, const yge_link_xfer_t *x, uint16_t *regs)
{
    link_msg_t reply = { 0 };
    const bool answered =
        x->write ? write_regs(&s_host, x->page, x->off, x->n, x->regs, &reply)
                 : read_regs(&s_host, x->page, x->off, x->n, &reply);

    (void)ctx;
    if (!answered) {
        return YGE_LINK_NO_ANSWER;
    }
    if (reply.op == LINK_OP_NACK) {
        return (int)reply.regs[0];
    }
    if (!x->write) {
        if (reply.op != LINK_OP_DATA || reply.count < x->n) {
            return YGE_LINK_NO_ANSWER;
        }
        memcpy(regs, reply.regs, (size_t)x->n * sizeof(regs[0]));
    }
    return YGE_LINK_ACK;
}

/**
 * @brief The alert a pin that did not come back gets: it names the port.  The
 *        bank stays SAFE, ARM is refused, and the CLOSE goes again.
 */
static void yge_not_back_alert(void)
{
    uint8_t port = 0u;

    if (yge_link_not_back(&s_yge_link, &port)) {
        char line[UI_ALERT_MAX];

        snprintf(line, sizeof(line), TR(ALERT_YGE_NOT_BACK), (unsigned)port);
        control_alert(line);
    }
}

__attribute__((noinline)) void yge_pass(bool link_up)
{
    yge_link_stop_follow(&s_yge_link, arming_stop_count(&s_arm));
    if (link_up) {
        (void)yge_link_service(&s_yge_link, now_ms(), yge_xchg, NULL);
    }
    yge_not_back_alert();
}

bool yge_taken(void)
{
    yge_link_readout_t rd;

    yge_link_readout(&s_yge_link, &rd);
    return yge_prog_port_taken(&rd);
}

int yge_taken_port(void)
{
    yge_link_readout_t rd;

    yge_link_readout(&s_yge_link, &rd);
    return yge_prog_taken_port(&rd);
}

__attribute__((noinline)) void yge_publish(yge_prog_feed_t *f)
{
    yge_link_outcome_t o;

    if (yge_link_outcome(&s_yge_link, &o)) {
        s_yge_outcome = o;
        ++s_yge_outcomes;
    }
    f->now_ms = now_ms();
    yge_link_readout(&s_yge_link, &f->rd);
    f->image = *yge_link_image(&s_yge_link);
    f->answer_id = s_yge_answer_id;
    f->answer = s_yge_answer;
    f->outcomes = s_yge_outcomes;
    f->outcome = s_yge_outcome;
    f->stopped = arming_stopped(&s_arm);
    f->armed = outputs_armed(&s_out);
}
