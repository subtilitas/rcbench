/**
 * @file
 * @brief The control task's half of programming for YGE ESCs: the YGE_PROG
 *        page's link module (yge_link.h), a stop, and what the page holds.
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
