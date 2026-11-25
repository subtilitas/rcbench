/**
 * @file
 * @brief Programming for YGE ESCs behind the YGE_PROG and YGE_DATA pages; see
 *        yge_port.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "yge_port.h"

#include <stddef.h>
#include <string.h>

#include "link_crc.h"
#include "openyge_guard.h"
#include "openyge_map.h"

_Static_assert(LINK_YP_W_PORT + LINK_YP_W_STAGED == LINK_YP_W_BLOCK,
               "staged registers");
_Static_assert(LINK_YP_COUNT <= LINK_MAX_REGS && LINK_YP_W_COUNT <= LINK_MAX_REGS
               && LINK_YD_COUNT <= LINK_MAX_REGS, "the pages");
_Static_assert(LINK_YD_COUNT * 2u == OPENYGE_MAX_PARAMS, "2 blocks a table");
_Static_assert(LINK_YGE_OP_COUNT <= 16, "the op's field");
_Static_assert(YGE_PORT_OTHER_ALL >= (1u << LINK_PG_PORTS_MAX), "the ports' bits");

/** @brief A staged register by its write offset. */
#define ST(p, reg) ((p)->staged[(reg) - LINK_YP_W_PORT])

/* --- the line and the supply -------------------------------------------- */

/**
 * @brief The line parked or listening.
 * @param p      the page
 * @param listen true for the pull-up and the receiver
 */
static void line_set(yge_port_t *p, bool listen)
{
    p->hw.line(p->hw.ctx, listen);
    p->line = (uint8_t)(listen ? LINK_YGE_LINE_LISTEN : LINK_YGE_LINE_PARK);
}

/**
 * @brief What the supply says.
 * @param p the page
 * @param v takes it
 * @return whether it is online
 */
static bool supply_view(const yge_port_t *p, supply_port_view_t *v)
{
    memset(v, 0, sizeof(*v));
    p->hw.supply(p->hw.ctx, v);
    return v->online;
}

/**
 * @brief The supply off; this session no longer counts on it.
 * @param p the page
 */
static void supply_off(yge_port_t *p)
{
    (void)p->hw.supply_set(p->hw.ctx, false, 0u, 0u);
    p->switched = false;
}

/* --- states -------------------------------------------------------------- */

/**
 * @brief Enter a state.
 * @param p     the page
 * @param state the state
 */
static void go(yge_port_t *p, link_yge_state_t state)
{
    p->state = (uint8_t)state;
    p->t0_ms = p->now_ms;
}

/**
 * @brief Back to free: the line closed, the pin its port's.  A port that does
 *        not take the pin back keeps the page held, LINK_YGE_NOT_BACK: the
 *        bank may not arm with no signal on that pin, and a CLOSE tries again.
 * @param p the page
 * @return false when the port did not take the pin back
 */
static bool go_free(yge_port_t *p)
{
    const bool had_pin = p->has_pin || p->state == (uint8_t)LINK_YGE_NOT_BACK;

    p->has_pin = false;
    p->line = (uint8_t)LINK_YGE_LINE_PARK;
    if (had_pin && !p->hw.give(p->hw.ctx)) {
        p->stopped = true;
        p->result = (uint8_t)LINK_YGE_RESULT_NOT_BACK;
        go(p, LINK_YGE_NOT_BACK);
        return false;
    }
    go(p, LINK_YGE_FREE);
    p->stopped = false;
    p->fault = false;
    p->port = YGE_PORT_NO_PIN;
    return true;
}

/**
 * @brief Whether a line or supply is in play: a state past OPEN.
 * @param p the page
 * @return true from ENTER on
 */
static bool in_session(const yge_port_t *p)
{
    return p->state > (uint8_t)LINK_YGE_OPEN
           && p->state != (uint8_t)LINK_YGE_NOT_BACK;
}

/**
 * @brief The session, the line and the PD mini at once, also in a write.
 * @param p the page
 */
static void end_session(yge_port_t *p)
{
    oy_session_abort(&p->ses);
    line_set(p, false);
    supply_off(p);
}

/**
 * @brief An error: every output off.
 * @param p      the page
 * @param result what to say
 */
static void all_off(yge_port_t *p, link_yge_result_t result)
{
    end_session(p);
    p->hw.others_off(p->hw.ctx);
    p->fault = true;
    p->stopped = true;
    p->result = (uint8_t)result;
    go(p, LINK_YGE_OPEN);
}

/**
 * @brief The line and the PD mini at once; a page in STOPPING goes free.
 * @param p the page
 */
static void cut_line(yge_port_t *p)
{
    if (p->state == (uint8_t)LINK_YGE_STOPPING) {
        if (go_free(p)) {
            p->result = (uint8_t)LINK_YGE_RESULT_CUT;
        }
    } else if (in_session(p)) {
        end_session(p);
        p->result = (uint8_t)LINK_YGE_RESULT_CUT;
        go(p, LINK_YGE_OPEN);
    }
}

/**
 * @brief Cut at once, and PROG_SUPPLY off with it.
 * @param p the page
 */
static void cut(yge_port_t *p)
{
    if (p->state != (uint8_t)LINK_YGE_FREE) {
        p->hw.others_off(p->hw.ctx);
    }
    cut_line(p);
}

/**
 * @brief A stop: as cut(), also in a write.  It stands until CLEAR or CLOSE.
 * @param p the page
 */
static void on_stop(yge_port_t *p)
{
    if (p->state != (uint8_t)LINK_YGE_FREE
        && p->state != (uint8_t)LINK_YGE_STOPPING) {
        p->stopped = true;
    }
    cut(p);
}

/**
 * @brief An operation of the session ended.
 * @param p the page
 * @param r its result
 */
static void op_end(yge_port_t *p, oy_res_t r)
{
    p->ses_res = (uint8_t)r;
    if (r != OY_OK) {
        all_off(p, LINK_YGE_RESULT_SESSION);
        return;
    }
    p->result = (uint8_t)LINK_YGE_RESULT_DONE;
    go(p, LINK_YGE_READY);
}

/**
 * @brief What a session's call said when it started.
 * @param p the page
 * @param r the call's result
 * @return LINK_YGE_REF_NONE, or why nothing started
 */
static link_yge_ref_t started(yge_port_t *p, oy_res_t r)
{
    p->ses_res = (uint8_t)r;
    if (r == OY_BUSY) {
        p->result = (uint8_t)LINK_YGE_RESULT_RUNNING;
        go(p, LINK_YGE_RUN);
        return LINK_YGE_REF_NONE;
    }
    if (r == OY_OK) {
        op_end(p, r);
        return LINK_YGE_REF_NONE;
    }
    if (r == OY_GUARD) {
        /* The guard said no: a draft the panel should never have staged. */
        all_off(p, LINK_YGE_RESULT_SESSION);
        return LINK_YGE_REF_SESSION;
    }
    /* Nothing went to the wire: no table, a read-only table, a state. */
    p->result = (uint8_t)LINK_YGE_RESULT_SESSION;
    return LINK_YGE_REF_SESSION;
}

/**
 * @brief The ESC has its power: the line listens, the boot wait starts.
 * @param p the page
 */
static void start_session(yge_port_t *p)
{
    line_set(p, true);
    p->op = (uint8_t)LINK_YGE_OP_ENTER;
    const oy_res_t r = oy_session_enter(&p->ses, p->boot_ms);

    p->ses_res = (uint8_t)r;
    if (r != OY_BUSY) {
        all_off(p, LINK_YGE_RESULT_SESSION);
        return;
    }
    p->result = (uint8_t)LINK_YGE_RESULT_RUNNING;
    go(p, LINK_YGE_RUN);
}

/* --- commands ------------------------------------------------------------ */

/**
 * @brief Whether the staged registers carry the frame's CRC.
 * @param p  the page
 * @param in the frame
 * @return true then
 */
static bool staged_ok(const yge_port_t *p, const uint16_t *in)
{
    return link_kst_crc(p->staged, LINK_YP_W_STAGED) == in[LINK_YP_W_CRC];
}

/**
 * @brief OPEN: take the staged port's pin.
 * @param p  the page
 * @param in the frame
 * @return why nothing started, or LINK_YGE_REF_NONE
 */
static link_yge_ref_t cmd_open(yge_port_t *p, const uint16_t *in)
{
    const uint16_t port = ST(p, LINK_YP_W_PORT);

    if (p->state != (uint8_t)LINK_YGE_FREE) {
        return LINK_YGE_REF_BUSY;
    }
    if ((p->other & YGE_PORT_OTHER_ALL) != 0u) {
        return LINK_YGE_REF_HELD;
    }
    if (p->armed) {
        return LINK_YGE_REF_ARMED;
    }
    if (!p->safe) {
        return LINK_YGE_REF_UNSAFE;
    }
    if (!staged_ok(p, in)) {
        return LINK_YGE_REF_STAGED;
    }
    if (port >= p->hw.ports) {
        return LINK_YGE_REF_TARGET;
    }
    if ((p->other & (1u << port)) != 0u) {
        return LINK_YGE_REF_HELD;
    }
    const uint8_t pin = p->hw.pin(p->hw.ctx, (uint8_t)port);

    if (pin == YGE_PORT_NO_PIN) {
        return LINK_YGE_REF_NO_PIN;
    }
    p->pin = pin;
    p->port = (uint8_t)port;
    p->stopped = false;
    p->fault = false;
    p->result = (uint8_t)LINK_YGE_RESULT_RUNNING;
    go(p, LINK_YGE_STOPPING);
    return LINK_YGE_REF_NONE;
}

/**
 * @brief ENTER's staged values.
 * @param p the page
 * @param v what the supply says
 * @return why they are refused, or LINK_YGE_REF_NONE
 */
static link_yge_ref_t enter_values(const yge_port_t *p,
                                   const supply_port_view_t *v)
{
    const uint16_t power = ST(p, LINK_YP_W_POWER);
    const uint16_t mv = ST(p, LINK_YP_W_MV);
    const uint16_t ma = ST(p, LINK_YP_W_MA);
    const uint16_t boot = ST(p, LINK_YP_W_BOOT_MS);

    if (power >= (uint16_t)LINK_YGE_POWER_COUNT || boot < OY_BOOT_MS_MIN
        || boot > OY_BOOT_MS_MAX) {
        return LINK_YGE_REF_RANGE;
    }
    if (power == (uint16_t)LINK_YGE_BY_HAND) {
        return LINK_YGE_REF_NONE;
    }
    if (!v->online) {
        return LINK_YGE_REF_NO_SUPPLY;
    }
    /* It would read off and then be refused ON: say so now, as what it is. */
    if (v->latched) {
        return LINK_YGE_REF_SUPPLY_OFF;
    }
    /* The board's ceiling where the supply reports one (board 3: 8400mV). */
    const uint16_t top = (v->max_mv != 0u) ? v->max_mv
                                           : (uint16_t)PDMINI_V_MAX_MV;

    if (mv < LINK_YGE_SUPPLY_MIN_MV || mv > top
        || ma < PDMINI_I_MIN_MA || ma > PDMINI_I_MAX_MA
        || (v->vin_mv != 0u && (uint32_t)mv + PDMINI_CAP_MV > v->vin_mv)) {
        return LINK_YGE_REF_RANGE;
    }
    return LINK_YGE_REF_NONE;
}

/**
 * @brief ENTER: the staged power, then the boot wait and the takeover.
 * @param p  the page
 * @param in the frame
 * @return why nothing started, or LINK_YGE_REF_NONE
 */
static link_yge_ref_t cmd_enter(yge_port_t *p, const uint16_t *in)
{
    supply_port_view_t v;

    if (in[LINK_YP_W_KEY] != LINK_YGE_KEY) {
        return LINK_YGE_REF_NO_KEY;
    }
    if (p->state == (uint8_t)LINK_YGE_FREE) {
        return LINK_YGE_REF_NOT_OPEN;
    }
    if (p->state != (uint8_t)LINK_YGE_OPEN) {
        return LINK_YGE_REF_BUSY;
    }
    if (p->stopped) {
        return LINK_YGE_REF_STOPPED;
    }
    if (!p->safe) {
        return LINK_YGE_REF_UNSAFE;
    }
    if (!staged_ok(p, in)) {
        return LINK_YGE_REF_STAGED;
    }
    (void)supply_view(p, &v);
    const link_yge_ref_t bad = enter_values(p, &v);

    if (bad != LINK_YGE_REF_NONE) {
        return bad;
    }
    p->by_hand = ST(p, LINK_YP_W_POWER) == (uint16_t)LINK_YGE_BY_HAND;
    p->mv = ST(p, LINK_YP_W_MV);
    p->ma = ST(p, LINK_YP_W_MA);
    p->boot_ms = ST(p, LINK_YP_W_BOOT_MS);
    p->off_seen = false;
    p->ses_res = (uint8_t)OY_BUSY;
    p->result = (uint8_t)LINK_YGE_RESULT_RUNNING;
    /* The line stays parked until the ESC has its power. */
    if (p->by_hand) {
        go(p, LINK_YGE_HAND_WAIT);
    } else {
        supply_off(p);
        go(p, LINK_YGE_POWER_OFF);
    }
    return LINK_YGE_REF_NONE;
}

/**
 * @brief Whether a draft may go: the staged registers, a draft made from this
 *        read copy, and the draft's CRC.
 * @param p  the page
 * @param in the frame
 * @return why not, or LINK_YGE_REF_NONE
 */
static link_yge_ref_t draft_ok(const yge_port_t *p, const uint16_t *in)
{
    if (!staged_ok(p, in)) {
        return LINK_YGE_REF_STAGED;
    }
    if (ST(p, LINK_YP_W_READS) != p->ses.guard.read_no
        || ST(p, LINK_YP_W_READ_CRC) != p->ses.guard.read_crc) {
        return LINK_YGE_REF_STALE;
    }
    if (oy_guard_table_crc(p->draft, p->ses.count)
        != ST(p, LINK_YP_W_DRAFT_CRC)) {
        return LINK_YGE_REF_DRAFT;
    }
    return LINK_YGE_REF_NONE;
}

/**
 * @brief The operations on an ESC that answered.
 * @param p  the page
 * @param op the operation
 * @param in the frame
 * @return why nothing started, or LINK_YGE_REF_NONE
 */
static link_yge_ref_t cmd_ready(yge_port_t *p, link_yge_op_t op,
                                const uint16_t *in)
{
    const bool write = op == LINK_YGE_OP_WRITE;

    if (write && in[LINK_YP_W_KEY] != LINK_YGE_KEY) {
        return LINK_YGE_REF_NO_KEY;
    }
    if (p->state == (uint8_t)LINK_YGE_FREE || p->state == (uint8_t)LINK_YGE_OPEN) {
        return p->stopped ? LINK_YGE_REF_STOPPED : LINK_YGE_REF_NOT_OPEN;
    }
    if (p->state != (uint8_t)LINK_YGE_READY) {
        return LINK_YGE_REF_BUSY;
    }
    if (!p->safe) {
        return LINK_YGE_REF_UNSAFE;
    }
    if (write) {
        const link_yge_ref_t bad = draft_ok(p, in);

        if (bad != LINK_YGE_REF_NONE) {
            return bad;
        }
    }
    p->op = (uint8_t)op;
    switch (op) {
    case LINK_YGE_OP_READ:
        return started(p, oy_session_read(&p->ses));
    case LINK_YGE_OP_WRITE:
        return started(p, oy_session_write(&p->ses, p->draft, p->ses.count,
                                           ST(p, LINK_YP_W_READS),
                                           ST(p, LINK_YP_W_READ_CRC)));
    case LINK_YGE_OP_LEAVE:
    default:
        end_session(p);
        p->result = (uint8_t)LINK_YGE_RESULT_DONE;
        go(p, LINK_YGE_OPEN);
        return LINK_YGE_REF_NONE;
    }
}

/**
 * @brief CLOSE: the line and the supply off, the pin back.
 * @param p the page
 * @return why not, or LINK_YGE_REF_NONE
 */
static link_yge_ref_t cmd_close(yge_port_t *p)
{
    if (p->state == (uint8_t)LINK_YGE_FREE) {
        return LINK_YGE_REF_NOT_OPEN;
    }
    if (yge_port_writing(p)) {
        return LINK_YGE_REF_WRITING;
    }
    cut_line(p);
    p->op = (uint8_t)LINK_YGE_OP_CLOSE;
    if (go_free(p)) {
        p->result = (uint8_t)LINK_YGE_RESULT_DONE;
    }
    return LINK_YGE_REF_NONE;
}

/**
 * @brief One command.
 * @param p  the page
 * @param op the operation
 * @param in the frame
 * @return why nothing started, or LINK_YGE_REF_NONE
 */
static link_yge_ref_t dispatch(yge_port_t *p, link_yge_op_t op,
                               const uint16_t *in)
{
    link_yge_ref_t r = LINK_YGE_REF_NONE;

    switch (op) {
    case LINK_YGE_OP_OPEN:
        r = cmd_open(p, in);
        break;
    case LINK_YGE_OP_CLOSE:
        return cmd_close(p);
    case LINK_YGE_OP_ABORT:
        cut(p);
        break;
    case LINK_YGE_OP_STOP:
        on_stop(p);
        break;
    case LINK_YGE_OP_CLEAR:
        if (p->state == (uint8_t)LINK_YGE_FREE) {
            return LINK_YGE_REF_NOT_OPEN;
        }
        if (p->state != (uint8_t)LINK_YGE_OPEN) {
            return LINK_YGE_REF_BUSY;
        }
        if (!p->safe) {
            return LINK_YGE_REF_UNSAFE;
        }
        p->stopped = false;
        p->fault = false;
        break;
    case LINK_YGE_OP_ENTER:
        r = cmd_enter(p, in);
        break;
    case LINK_YGE_OP_HAND_DONE:
        if (p->state != (uint8_t)LINK_YGE_HAND_WAIT) {
            return LINK_YGE_REF_NOT_OPEN;
        }
        if (!p->safe) {
            return LINK_YGE_REF_UNSAFE;
        }
        start_session(p);
        return LINK_YGE_REF_NONE;
    case LINK_YGE_OP_READ:
    case LINK_YGE_OP_WRITE:
    case LINK_YGE_OP_LEAVE:
    default:
        return cmd_ready(p, op, in);
    }
    if (r == LINK_YGE_REF_NONE) {
        p->op = (uint8_t)op;
    }
    return r;
}

/**
 * @brief The command frame.
 * @param p  the page
 * @param in its 4 registers
 * @return 0, or LINK_NACK_BAD_VALUE for a bad frame or SEQ
 */
static uint8_t command(yge_port_t *p, const uint16_t *in)
{
    const uint16_t cmd = in[LINK_YP_W_CMD];

    if (cmd == (uint16_t)LINK_YGE_OP_NONE || cmd >= (uint16_t)LINK_YGE_OP_COUNT
        || (in[LINK_YP_W_KEY] != 0u && in[LINK_YP_W_KEY] != LINK_YGE_KEY)) {
        return LINK_NACK_BAD_VALUE;
    }
    /* The frame just taken, sent again: its answer was lost. */
    if (memcmp(in, p->last_cmd, sizeof(p->last_cmd)) == 0) {
        return 0u;
    }
    if (in[LINK_YP_W_SEQ] != (uint16_t)(p->seq + 1u)) {
        return LINK_NACK_BAD_VALUE;
    }
    p->seq = in[LINK_YP_W_SEQ];
    memcpy(p->last_cmd, in, sizeof(p->last_cmd));
    p->refusal = (uint8_t)dispatch(p, (link_yge_op_t)cmd, in);
    return 0u;
}

uint8_t yge_port_write(yge_port_t *p, uint8_t off, uint8_t n,
                       const uint16_t *in, uint32_t now_ms)
{
    if (p == NULL || in == NULL || n == 0u
        || (unsigned)off + n > LINK_YP_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    p->now_ms = now_ms;
    if (off < LINK_YP_W_CMD_FRAME) {
        if (off != 0u || n != LINK_YP_W_CMD_FRAME) {
            return LINK_NACK_BAD_RANGE;
        }
        return command(p, in);
    }
    if ((unsigned)off + n > LINK_YP_W_COUNT) {
        return LINK_NACK_READ_ONLY;
    }
    if ((unsigned)off + n > LINK_YP_W_BLOCK
        && in[LINK_YP_W_BLOCK - off] >= (uint16_t)LINK_YD_BLOCKS) {
        return LINK_NACK_BAD_VALUE;
    }
    for (unsigned i = 0u; i < n; ++i) {
        const unsigned at = off + i;

        if (at == LINK_YP_W_BLOCK) {
            p->block = (uint8_t)in[i];
        } else {
            p->staged[at - LINK_YP_W_PORT] = in[i];
        }
    }
    return 0u;
}

/* --- the pages ----------------------------------------------------------- */

/**
 * @brief The FLAGS register.
 * @param p the page
 * @return its value
 */
static uint16_t flags_reg(const yge_port_t *p)
{
    supply_port_view_t v;
    const oy_session_t *s = &p->ses;
    const bool taken = s->state >= (uint8_t)OY_ST_READY;
    uint16_t f = 0u;

    f |= p->stopped ? LINK_YP_F_STOPPED : 0u;
    f |= p->fault ? LINK_YP_F_FAULT : 0u;
    f |= p->switched ? LINK_YP_F_SUPPLY : 0u;
    f |= supply_view(p, &v) ? LINK_YP_F_RAIL : 0u;
    f |= taken && s->guard.table && s->guard.ro == (uint8_t)OY_REF_NONE
             ? LINK_YP_F_WRITABLE : 0u;
    f |= s->legacy ? LINK_YP_F_LEGACY : 0u;
    f |= taken ? LINK_YP_F_TAKEN : 0u;
    f |= oy_session_writing(s) ? LINK_YP_F_WRITING : 0u;
    f |= s->guard.table ? LINK_YP_F_HAVE_READ : 0u;
    f |= oy_session_cut_write(s) ? LINK_YP_F_CUT_WRITE : 0u;
    f |= p->by_hand && in_session(p) ? LINK_YP_F_BY_HAND : 0u;
    return f;
}

/**
 * @brief A 32-bit count as a register.
 * @param v the count
 * @return v, or 65535 above it
 */
static uint16_t sat16(uint32_t v)
{
    return (uint16_t)(v > 0xFFFFu ? 0xFFFFu : v);
}

/**
 * @brief The PROGRESS register.
 * @param s the session
 * @return low byte indices seen, high byte the index being written or 0xFF
 */
static uint16_t progress_reg(const oy_session_t *s)
{
    unsigned seen = 0u;

    for (uint64_t m = s->params.seen; m != 0u; m &= m - 1u) {
        ++seen;
    }
    const uint16_t widx = s->state == (uint8_t)OY_ST_WRITE ? (uint16_t)(s->widx & 0xFFu)
                                                          : 0xFFu;

    return (uint16_t)(seen | ((uint16_t)widx << 8));
}

void yge_port_read(const yge_port_t *p, uint8_t off, uint8_t n, uint16_t *out)
{
    uint16_t all[LINK_YP_COUNT];

    if (p == NULL || out == NULL || (unsigned)off + n > LINK_YP_COUNT) {
        return;
    }
    const oy_session_t *s = &p->ses;
    const uint16_t *t = s->orig;
    const bool have = s->count > OY_I_SERIAL_HI;

    memset(all, 0, sizeof(all));
    all[LINK_YP_STATE] = p->state;
    all[LINK_YP_SEQ] = p->seq;
    all[LINK_YP_OP] = p->op;
    all[LINK_YP_RESULT] = (uint16_t)(p->result | ((uint16_t)p->refusal << 8));
    all[LINK_YP_FLAGS] = flags_reg(p);
    all[LINK_YP_LINE] = (uint16_t)(p->line
                                   | ((uint16_t)(p->has_pin ? p->pin
                                                            : YGE_PORT_NO_PIN)
                                      << 8));
    all[LINK_YP_PORT] = p->port;
    all[LINK_YP_SES] = (uint16_t)(p->ses_res | ((uint16_t)s->refuse << 8));
    all[LINK_YP_BAD_INDEX] = s->bad_index;
    all[LINK_YP_TYPE] = have ? t[OY_I_TYPE] : 0u;
    all[LINK_YP_FW_LO] = have ? t[OY_I_FW_LO] : 0u;
    all[LINK_YP_FW_HI] = have ? t[OY_I_FW_HI] : 0u;
    all[LINK_YP_SERIAL_LO] = have ? t[OY_I_SERIAL_LO] : 0u;
    all[LINK_YP_SERIAL_HI] = have ? t[OY_I_SERIAL_HI] : 0u;
    all[LINK_YP_ENTRIES] = s->count;
    all[LINK_YP_READS] = s->guard.read_no;
    all[LINK_YP_READ_CRC] = s->guard.read_crc;
    all[LINK_YP_PROGRESS] = progress_reg(s);
    all[LINK_YP_T_MS] = sat16(p->now_ms - p->t0_ms);
    all[LINK_YP_BLOCK] = p->block;
    all[LINK_YP_SES_STATE] = (uint16_t)(s->state | ((uint16_t)s->guard.ro << 8));
    all[LINK_YP_MISSES] = sat16(s->misses);
    all[LINK_YP_DROPPED] = sat16(s->dropped);
    for (unsigned i = 0u; i < n; ++i) {
        out[i] = all[off + i];
    }
}

/**
 * @brief The values a block shows.
 * @param p     the page
 * @param block the block
 * @return its first value
 */
static const uint16_t *block_values(const yge_port_t *p, unsigned block)
{
    const uint16_t *base;

    if (block < (unsigned)LINK_YD_DRAFT) {
        base = p->ses.orig;
    } else if (block < (unsigned)LINK_YD_LAST) {
        base = p->draft;
    } else {
        base = p->ses.params.value;
    }
    return base + (size_t)(block % 2u) * LINK_YD_COUNT;
}

void yge_port_data_read(const yge_port_t *p, uint8_t off, uint8_t n,
                        uint16_t *out)
{
    if (p == NULL || out == NULL || (unsigned)off + n > LINK_YD_COUNT) {
        return;
    }
    memcpy(out, block_values(p, p->block) + off, (size_t)n * sizeof(out[0]));
}

uint8_t yge_port_data_write(yge_port_t *p, uint8_t off, uint8_t n,
                            const uint16_t *in)
{
    if (p == NULL || in == NULL || n == 0u
        || (unsigned)off + n > LINK_YD_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    if (p->block < (uint8_t)LINK_YD_DRAFT || p->block >= (uint8_t)LINK_YD_LAST) {
        return LINK_NACK_READ_ONLY;
    }
    if (p->state == (uint8_t)LINK_YGE_RUN) {
        return LINK_NACK_BUSY;
    }
    memcpy(&p->draft[(p->block % 2u) * LINK_YD_COUNT + off], in,
           (size_t)n * sizeof(in[0]));
    return 0u;
}

/* --- the pass ------------------------------------------------------------ */

/**
 * @brief ENTER's way to ON: the supply off and read off, then ON asked.
 * @param p the page
 */
static void step_power_off(yge_port_t *p)
{
    supply_port_view_t v;
    const uint32_t t = p->now_ms - p->t0_ms;

    if (supply_view(p, &v) && v.off && t >= YGE_PORT_READ_OFF_MIN_MS) {
        if (!p->hw.supply_set(p->hw.ctx, true, p->mv, p->ma)) {
            all_off(p, LINK_YGE_RESULT_SUPPLY);
            return;
        }
        p->off_seen = true;
        p->switched = true;
        go(p, LINK_YGE_POWER_ON);
    } else if (t >= YGE_PORT_READ_OFF_MAX_MS) {
        all_off(p, LINK_YGE_RESULT_SUPPLY);
    }
}

/**
 * @brief ON was asked: the boot wait starts when the PD mini reads on.
 * @param p the page
 */
static void step_power_on(yge_port_t *p)
{
    supply_port_view_t v;

    if (!supply_view(p, &v) || !v.asked_on
        || (!v.on && p->now_ms - p->t0_ms >= YGE_PORT_READ_ON_MAX_MS)) {
        all_off(p, LINK_YGE_RESULT_SUPPLY);
    } else if (v.on) {
        start_session(p);
    }
}

/**
 * @brief Whether the supply this session switched on is gone: asked off, a PD
 *        mini that stopped answering, or read off.
 * @param p the page
 * @return true then
 */
static bool supply_gone(const yge_port_t *p)
{
    supply_port_view_t v;

    if (!p->switched) {
        return false;
    }
    if (!supply_view(p, &v) || !v.asked_on) {
        return true;
    }
    return v.off;
}

/**
 * @brief RUN and READY: the supply under the session, then a step of it.  In
 *        READY the session requests between operations and may end there.
 * @param p the page
 */
static void step_session(yge_port_t *p)
{
    if (supply_gone(p)) {
        all_off(p, LINK_YGE_RESULT_SUPPLY);
        return;
    }
    const oy_res_t r = oy_session_step(&p->ses);

    if (p->state == (uint8_t)LINK_YGE_RUN) {
        if (r != OY_BUSY) {
            op_end(p, r);
        }
    } else if (r != OY_OK) {
        op_end(p, r);
    }
}

void yge_port_step(yge_port_t *p, bool safe, bool stop, uint32_t now_ms)
{
    if (p == NULL) {
        return;
    }
    p->now_ms = now_ms;
    p->safe = safe;
    if (!safe) {
        const bool held = p->state != (uint8_t)LINK_YGE_FREE;

        cut(p);
        p->stopped = held && p->state != (uint8_t)LINK_YGE_FREE;
    } else if (stop) {
        on_stop(p);
    }
    switch ((link_yge_state_t)p->state) {
    case LINK_YGE_STOPPING:
        if (now_ms - p->t0_ms >= YGE_PORT_STOP_MS) {
            p->has_pin = true;
            if (!p->hw.take(p->hw.ctx, p->port, p->pin)) {
                if (go_free(p)) {
                    p->result = (uint8_t)LINK_YGE_RESULT_NO_LINE;
                }
            } else {
                p->line = (uint8_t)LINK_YGE_LINE_PARK;
                p->result = (uint8_t)LINK_YGE_RESULT_DONE;
                go(p, LINK_YGE_OPEN);
            }
        }
        break;
    case LINK_YGE_POWER_OFF:
        step_power_off(p);
        break;
    case LINK_YGE_POWER_ON:
        step_power_on(p);
        break;
    case LINK_YGE_HAND_WAIT:
        if (now_ms - p->t0_ms > YGE_PORT_HAND_MS) {
            all_off(p, LINK_YGE_RESULT_TIMEOUT);
        }
        break;
    case LINK_YGE_RUN:
    case LINK_YGE_READY:
        step_session(p);
        break;
    case LINK_YGE_FREE:
    case LINK_YGE_OPEN:
    case LINK_YGE_NOT_BACK:
    default:
        break;
    }
}

bool yge_port_init(yge_port_t *p, const yge_port_hw_t *hw)
{
    if (p == NULL) {
        return false;
    }
    memset(p, 0, sizeof(*p));
    p->port = YGE_PORT_NO_PIN;
    if (hw == NULL || hw->ports == 0u || hw->ports > LINK_PG_PORTS_MAX
        || hw->pin == NULL || hw->take == NULL || hw->give == NULL
        || hw->line == NULL || hw->supply == NULL || hw->supply_set == NULL
        || hw->others_off == NULL) {
        return false;
    }
    p->hw = *hw;
    return oy_session_init(&p->ses, &hw->driver) == OY_OK;
}

link_yge_state_t yge_port_state(const yge_port_t *p)
{
    return p != NULL ? (link_yge_state_t)p->state : LINK_YGE_FREE;
}

uint8_t yge_port_quiet_mask(const yge_port_t *p)
{
    if (p == NULL || p->state == (uint8_t)LINK_YGE_FREE
        || p->port >= LINK_PG_PORTS_MAX) {
        return 0u;
    }
    return (uint8_t)(1u << p->port);
}

uint8_t yge_port_pin_mask(const yge_port_t *p)
{
    return (uint8_t)(p != NULL && p->has_pin && p->port < LINK_PG_PORTS_MAX
                         ? 1u << p->port
                         : 0u);
}

bool yge_port_supply_hold(const yge_port_t *p)
{
    return p != NULL && p->state != (uint8_t)LINK_YGE_FREE;
}

bool yge_port_writing(const yge_port_t *p)
{
    return p != NULL && oy_session_writing(&p->ses);
}

void yge_port_bench_armed(yge_port_t *p, bool armed)
{
    if (p != NULL) {
        p->armed = armed;
    }
}

void yge_port_other(yge_port_t *p, uint8_t mask)
{
    if (p != NULL) {
        p->other = mask;
    }
}

uint8_t yge_port_others(uint8_t servo_ports, bool esc, bool attach, bool update,
                        uint8_t rail)
{
    uint8_t m = (uint8_t)(rail & ~YGE_PORT_OTHER_ALL);

    m |= servo_ports != 0u ? YGE_PORT_OTHER_SERVO : 0u;
    m |= esc ? YGE_PORT_OTHER_ESC : 0u;
    m |= attach ? YGE_PORT_OTHER_ATTACH : 0u;
    m |= update ? YGE_PORT_OTHER_UPDATE : 0u;
    return m;
}
