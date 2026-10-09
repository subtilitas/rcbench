/*
 * A binding on the link, from the panel's side.  See bind_link.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "bind_link.h"

#include <stddef.h>
#include <string.h>

#include "out_stage.h"

/* page_written() writes from register 0. */
_Static_assert(LINK_SV_FRAME_HZ == 0, "the rate reset writes register 0");

/* One page, a frame per exchange.  True when it was acknowledged whole;
 * otherwise @p res says which way it ended. */
static bool page_written(link_host_t *h, const link_port_t *port,
                         uint8_t page, uint8_t count, const uint16_t *regs,
                         bind_write_t *res)
{
    link_msg_t reply;
    if (!link_write_acked(h, port, page, 0u, count, regs, &reply)) {
        *res = BIND_NO_LINK;
        return false;
    }
    if (reply.op != LINK_OP_ACK) {
        *res = BIND_REFUSED;
        return false;
    }
    return true;
}

bind_write_t bind_link_write(link_host_t *h, const link_port_t *port,
                             uint16_t minor, const uint16_t *cfg,
                             const uint16_t *slots, bind_rate_t *rate)
{
    bind_rate_t unused;
    if (rate == NULL) {
        rate = &unused;
    }
    *rate = BIND_RATE_NOT_SENT;
    if (h == NULL || port == NULL || cfg == NULL || slots == NULL) {
        return BIND_NO_LINK;
    }
    bind_write_t res = BIND_WRITTEN;
    if (minor >= LINK_MINOR_SERVO_RATE) {
        /*
         * Each slot back at its own rate first.  The rate the SERVO page
         * holds was checked against the binding being replaced, and left in
         * place a motor bound beside a 560 Hz surface would be refused by
         * the silicon and stay still.  Nothing is written over a rate that
         * did not go back.
         */
        const uint16_t own = 0u;
        const bool ok = page_written(h, port, LINK_PAGE_SERVO, 1u, &own,
                                     &res);
        *rate = ok ? BIND_RATE_LANDED
                   : ((res == BIND_REFUSED) ? BIND_RATE_REFUSED
                                            : BIND_RATE_NO_ANSWER);
        if (!ok) {
            return res;
        }
    }
    if (minor < LINK_MINOR_BIND) {
        /*
         * CHAN_CFG first.  It says what a channel is; OUTPUTS says what
         * renders it.  A slot that starts rendering a channel whose role
         * has not arrived would drive it to the wrong rest for as long as
         * the second write takes.
         */
        if (page_written(h, port, LINK_PAGE_CHAN_CFG, LINK_CC_COUNT, cfg,
                         &res)) {
            (void)page_written(h, port, LINK_PAGE_OUTPUTS, LINK_OS_COUNT,
                               slots, &res);
        }
        return res;
    }
    if (!page_written(h, port, LINK_PAGE_BIND_CFG, LINK_CC_COUNT, cfg, &res)
        || !page_written(h, port, LINK_PAGE_BIND_OUT, LINK_OS_COUNT, slots,
                         &res)) {
        return res;
    }
    const uint16_t crc = out_stage_crc(cfg, slots);
    (void)page_written(h, port, LINK_PAGE_BIND, LINK_BD_COUNT, &crc, &res);
    return res;
}

bool bind_link_may_write(bind_read_t last, uint16_t board,
                         const outbind_t *b)
{
    if (b == NULL || b->board != board) {
        return false;
    }
    switch (last) {
    case BIND_READ_OK:
        return true;
    case BIND_READ_ODD:
        return outbind_chosen_total(b) == 0u;
    case BIND_READ_NONE:
    case BIND_READ_NO_BOARD:
    default:
        return false;
    }
}

bind_read_t bind_link_classify(bind_reading_t *out, uint16_t board,
                               const uint16_t *slots, const uint16_t *cfg)
{
    if (out == NULL) {
        return BIND_READ_NONE;
    }
    memset(out, 0, sizeof(*out));
    outbind_init(&out->bind);
    outbind_set_board(&out->bind, board);
    if (slots == NULL || cfg == NULL) {
        /*
         * Both pages or nothing: the slots page alone cannot say whether a
         * 50 Hz pulse slot is a servo or a motor, and reading the binding
         * without the roles rests an ESC (electronic speed controller) at
         * half throttle.
         */
        out->state = BIND_READ_NONE;
        return out->state;
    }
    memcpy(out->slots, slots, sizeof(out->slots));
    memcpy(out->cfg, cfg, sizeof(out->cfg));
    out->pages = true;
    if (outbind_board(board) == NULL) {
        out->state = BIND_READ_NO_BOARD;
        return out->state;
    }
    outbind_t got;
    if (outbind_from_slots(&got, board, slots, cfg)) {
        out->bind  = got;
        out->state = BIND_READ_OK;
    } else {
        out->state = BIND_READ_ODD;
    }
    return out->state;
}

bind_read_t bind_link_read(link_host_t *h, const link_port_t *port,
                           uint16_t board, bind_reading_t *out)
{
    if (out == NULL) {
        return BIND_READ_NONE;
    }
    link_msg_t orr, ccr;
    if (h == NULL || port == NULL
        || !link_read_window(h, port, LINK_PAGE_OUTPUTS, 0u, LINK_OS_COUNT,
                             &orr)
        || orr.op != LINK_OP_DATA
        || !link_read_window(h, port, LINK_PAGE_CHAN_CFG, 0u, LINK_CC_COUNT,
                             &ccr)
        || ccr.op != LINK_OP_DATA) {
        return bind_link_classify(out, board, NULL, NULL);
    }
    return bind_link_classify(out, board, orr.regs, ccr.regs);
}
