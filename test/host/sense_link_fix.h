/*
 * What the sense_link suites share: the IOmcu's end of the link (the
 * real SENSE page code, or a 4.6 IOmcu that refuses the pages), the
 * panel's end under test, a clock, and the helpers that poll and set the
 * far end's registers.  Functions are static inline: a suite uses some of
 * them.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string.h>

#include "greatest.h"
#include "tick_wrap.h"

#include "link_msg.h"
#include "link_pages.h"
#include "outputs.h"
#include "sense_link.h"
#include "sense_page.h"

static outputs_t    o;
static sense_page_t pg;      /* the IOmcu's end */
static sense_link_t sl;      /* the panel's end */
static uint32_t     now;
static uint16_t     minor;   /* the IOmcu's protocol minor */
static unsigned     writes, reads, to_sense, idents;
static bool         idle;
static unsigned     last_status_n;    /* the registers of the last status read */

/* The setup SETUP starts with: both parts off, the page's own defaults. */
static inline sense_setup_t setup_default(void)
{
    const sense_setup_t w = {
        .i228 = false, .i3221 = false, .sda = 16, .scl = 17,
        .i228_addr = 0x45u, .i228_uohm = 200u, .i228_max_da = 2048u,
        .i3221_addr = 0x40u, .i3221_dmohm = 1000u, .i3221_ch = 0x01u,
    };
    return w;
}

static inline void fresh(uint16_t m)
{
    outputs_init(&o, 0u);
    outputs_reserve_pins(&o, (uint64_t)1u << 3);
    sense_page_init(&pg);
    sense_link_init(&sl);
    now = 10000u;
    minor = m;
    writes = reads = to_sense = idents = 0u;
    idle = true;
    const sense_setup_t w = setup_default();
    sense_link_want(&sl, &w, now);
    sense_link_came_up(&sl, minor, now);
}

/*
 * One exchange as the IOmcu answers it.  A 4.6 IOmcu has no
 * SENSE or SERVO_SENSE page and refuses both with BAD_PAGE.
 */
static inline int far_exchange(const sense_link_op_t *op, uint16_t *regs)
{
    if (op->page == LINK_PAGE_SENSE || op->page == LINK_PAGE_SERVO_SENSE) {
        ++to_sense;
        if (minor < SENSE_LINK_MINOR) {
            return LINK_NACK_BAD_PAGE;
        }
    }
    if (op->write) {
        ++writes;
        const uint8_t nack = sense_page_write(&pg, op->off, op->n, op->regs,
                                              &o, 0u);
        return (nack == 0u) ? SENSE_LINK_ACK : (int)nack;
    }
    ++reads;
    if (op->kind == SENSE_LINK_OP_STATUS) {
        last_status_n = op->n;
    }
    memset(regs, 0, LINK_MAX_REGS * sizeof(uint16_t));
    if (op->page == LINK_PAGE_IDENTITY) {
        ++idents;
        regs[LINK_ID_PROTOCOL_MAJOR] = LINK_PROTOCOL_MAJOR;
        regs[LINK_ID_PROTOCOL_MINOR] = minor;
        regs[LINK_ID_CAPABILITIES]   = sense_page_caps(&pg);
    } else if (op->page == LINK_PAGE_SENSE) {
        sense_page_read(&pg, op->off, op->n, regs);
    } else {
        sense_servo_read(&pg, op->off, op->n, regs);
    }
    return SENSE_LINK_ACK;
}

/* One poll of the control task: every exchange owed, then 50 ms on. */
static inline void poll_once(void)
{
    for (int k = 0; k < 8; ++k) {
        sense_link_op_t op;
        if (!sense_link_next(&sl, now, idle, &op)) {
            break;
        }
        uint16_t regs[LINK_MAX_REGS];
        const int result = far_exchange(&op, regs);
        sense_link_done(&sl, result, op.write ? NULL : regs, now);
    }
    now += 50u;
}

static inline void polls(int n)
{
    for (int i = 0; i < n; ++i) {
        poll_once();
    }
}

static inline void want(const sense_setup_t *w)
{
    sense_link_want(&sl, w, now);
}

/* The page as core 1 would leave it: FLAGS and what goes with them. */
static inline void far_flags(uint16_t f)
{
    pg.sense[LINK_SN_FLAGS] = f;
}

/* The encoder as core 1 would leave it: its flags and what goes with them. */
static inline void far_enc(uint16_t flags, uint16_t raw, uint16_t samples,
                           uint16_t still)
{
    pg.sense[LINK_SN_AS5600_FLAGS]    = flags;
    pg.sense[LINK_SN_AS5600_ANGLE]    = raw;
    pg.sense[LINK_SN_AS5600_SAMPLES]  = samples;
    pg.sense[LINK_SN_AS5600_STILL_MS] = still;
}

#define ENC_OK (LINK_SN_ENC_ONLINE | LINK_SN_ENC_MD | LINK_SN_ENC_VALID)

/* The encoder asked for on an IOmcu of minor @p m, the set-up
 * written. */
static inline void enc_wanted(uint16_t m)
{
    fresh(m);
    sense_setup_t w = setup_default();
    w.as5600 = true;
    want(&w);
    polls(20);
}
