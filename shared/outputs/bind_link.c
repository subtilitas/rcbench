/**
 * @file
 * @brief A binding on the link, from the panel's side.
 *
 * See bind_link.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "bind_link.h"

#include <stddef.h>
#include <string.h>

#include "out_stage.h"
#include "outputs_pages.h"

/** @brief The commit writes register 0 alone. */
_Static_assert(LINK_BD_COMMIT == 0, "the commit writes register 0");
/** @brief page_written() writes from register 0. */
_Static_assert(LINK_SV_FRAME_HZ == 0, "the rate reset writes register 0");

/**
 * @brief Whether a slot of the _HI half of @p slots has a driver.
 *
 * @param slots OUTPUTS and OUTPUTS_HI, LINK_OS_ALL registers.
 * @return true when a slot from 9 to 16 is bound.
 */
static bool hi_bound(const uint16_t *slots)
{
    for (unsigned s = LINK_OUT_SLOTS; s < LINK_OUT_SLOTS_ALL; ++s) {
        if (slots[(size_t)s * LINK_OS_STRIDE + LINK_OS_DRIVER]
            != (uint16_t)LINK_DRIVER_NONE) {
            return true;
        }
    }
    return false;
}

/**
 * @brief @p count registers from @p off, a frame per exchange.  True when they
 *        were acknowledged whole; otherwise @p res says which way it ended.
 *
 * @param h The link host to write through.
 * @param port The link port the IOmcu answers on.
 * @param page The page to write.
 * @param off First register written.
 * @param count Number of registers written.
 * @param regs The registers to write, @p count of them.
 * @param res Receives BIND_NO_LINK for an unanswered exchange and BIND_REFUSED
 *        for a reply other than an ACK. Set only when false is returned.
 * @return true when the write was acknowledged whole, false otherwise.
 */
static bool reg_written(link_host_t *h, const link_port_t *port, uint8_t page,
                        uint8_t off, uint8_t count, const uint16_t *regs,
                        bind_write_t *res)
{
    link_msg_t reply;
    if (!link_write_acked(h, port, page, off, count, regs, &reply)) {
        *res = BIND_NO_LINK;
        return false;
    }
    if (reply.op != LINK_OP_ACK) {
        *res = BIND_REFUSED;
        return false;
    }
    return true;
}

/**
 * @brief One page, a frame per exchange.  True when it was acknowledged whole;
 *        otherwise @p res says which way it ended.
 *
 * @param h The link host to write through.
 * @param port The link port the IOmcu answers on.
 * @param page The page to write.
 * @param count Number of registers written, from register 0.
 * @param regs The registers to write, @p count of them.
 * @param res Receives BIND_NO_LINK for an unanswered exchange and BIND_REFUSED
 *        for a reply other than an ACK. Set only when false is returned.
 * @return true when the write was acknowledged whole, false otherwise.
 */
static bool page_written(link_host_t *h, const link_port_t *port,
                         uint8_t page, uint8_t count, const uint16_t *regs,
                         bind_write_t *res)
{
    return reg_written(h, port, page, 0u, count, regs, res);
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
    /* Slots 9 to 16 reach a 4.41 IOmcu only: an older one has none. */
    if (minor < LINK_MINOR_OUT16 && hi_bound(slots)) {
        return BIND_REFUSED;
    }
    bind_write_t res = BIND_WRITTEN;
    if (minor >= LINK_MINOR_SERVO_RATE) {
        /*
         * Each slot back at its own rate first.  The rate the SERVO page
         * holds was checked against the binding being replaced, and left in
         * place a motor bound beside a 560Hz surface would be refused by
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
    if (minor < LINK_MINOR_OUT16) {
        const uint16_t crc = out_stage_crc(cfg, slots);
        (void)page_written(h, port, LINK_PAGE_BIND, 1u, &crc, &res);
        return res;
    }
    /* Both _HI halves as well, and all four taken as one. */
    if (!page_written(h, port, LINK_PAGE_BIND_CFG_HI, LINK_CC_COUNT,
                      &cfg[(size_t)LINK_CC_COUNT], &res)
        || !page_written(h, port, LINK_PAGE_BIND_OUT_HI, LINK_OS_COUNT,
                         &slots[(size_t)LINK_OS_COUNT], &res)) {
        return res;
    }
    const uint16_t crc = out_stage_crc_all(cfg, slots);
    (void)reg_written(h, port, LINK_PAGE_BIND, (uint8_t)LINK_BD_COMMIT_ALL,
                      1u, &crc, &res);
    return res;
}

uint8_t bind_link_why(link_host_t *h, const link_port_t *port,
                      uint16_t minor)
{
    link_msg_t reply;
    if (h == NULL || port == NULL || minor < LINK_MINOR_BIND_WHY
        || !link_read_window(h, port, LINK_PAGE_BIND, LINK_BD_WHY, 1u,
                             &reply)
        || reply.op != LINK_OP_DATA || reply.regs[0] > 0xFFu) {
        return (uint8_t)LINK_BW_NONE;
    }
    return (uint8_t)reply.regs[0];
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
         * 50Hz pulse slot is a servo or a motor, and reading the binding
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
    /* The binding model holds slots 1 to 8: a slot past them is a page it
     * cannot describe. */
    if (!hi_bound(slots) && outbind_from_slots(&got, board, slots, cfg)) {
        out->bind  = got;
        out->state = BIND_READ_OK;
    } else {
        out->state = BIND_READ_ODD;
    }
    return out->state;
}

/**
 * @brief One page read whole into @p regs.
 *
 * @param h The link host.
 * @param port The link port.
 * @param page The page.
 * @param count Its registers.
 * @param regs Receives @p count registers.
 * @return true when the page read.
 */
static bool page_read(link_host_t *h, const link_port_t *port, uint8_t page,
                      uint8_t count, uint16_t *regs)
{
    link_msg_t r;
    if (!link_read_split(h, port, page, 0u, count, &r)
        || r.op != LINK_OP_DATA) {
        return false;
    }
    memcpy(regs, r.regs, (size_t)count * sizeof(uint16_t));
    return true;
}

bind_read_t bind_link_read(link_host_t *h, const link_port_t *port,
                           uint16_t minor, uint16_t board,
                           bind_reading_t *out)
{
    if (out == NULL) {
        return BIND_READ_NONE;
    }
    /* Static: two pages of each, off the control task's stack. */
    static uint16_t slots[LINK_OS_ALL];
    static uint16_t cfg[LINK_CC_ALL];
    memset(slots, 0, sizeof(slots));
    outputs_chan_cfg_defaults(&cfg[(size_t)LINK_CC_COUNT]);
    const bool hi = minor >= LINK_MINOR_OUT16;
    if (h == NULL || port == NULL
        || !page_read(h, port, LINK_PAGE_OUTPUTS, LINK_OS_COUNT, slots)
        || !page_read(h, port, LINK_PAGE_CHAN_CFG, LINK_CC_COUNT, cfg)
        || (hi && !page_read(h, port, LINK_PAGE_OUTPUTS_HI, LINK_OS_COUNT,
                             &slots[(size_t)LINK_OS_COUNT]))
        || (hi && !page_read(h, port, LINK_PAGE_CHAN_CFG_HI, LINK_CC_COUNT,
                             &cfg[(size_t)LINK_CC_COUNT]))) {
        return bind_link_classify(out, board, NULL, NULL);
    }
    return bind_link_classify(out, board, slots, cfg);
}
