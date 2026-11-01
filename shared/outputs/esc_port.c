/**
 * @file
 * @brief The ESC on its signal wire behind the ESC_WIRE and ESC_DATA pages; see
 *        esc_port.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_port.h"

#include <stddef.h>
#include <string.h>

_Static_assert(LINK_EW_W_TARGET + LINK_EW_W_STAGED == LINK_EW_W_BLOCK,
               "staged registers");
_Static_assert(LINK_EW_COUNT <= LINK_MAX_REGS && LINK_EW_W_COUNT <= LINK_MAX_REGS,
               "the page");
_Static_assert(LINK_ESC_OP_COUNT <= 16, "the op's field");
_Static_assert(LINK_ED_BLOCK_BYTES * 4u == EW_PAGE_MAX, "4 blocks a page");
_Static_assert(LINK_ED_BLOCK_BYTES * 4u == EW_E32_MUSIC_MAX + 1u, "4 of music");
_Static_assert(EW_ID_NAME_MAX + 1u <= LINK_ED_BLOCK_BYTES - 4u, "name fits");
_Static_assert(sizeof(ew_text_info_t) == 106u && LINK_ED_BLOCK_BYTES * 2u >= 106u,
               "the layout docs/LinkEsc.md gives for LINK_ED_INFO");
_Static_assert(ESC_PORT_QUIET_SLOT >= (1u << LINK_PG_PORTS_MAX), "the slot's bit");
_Static_assert((int)LINK_ESC_LINE_PARK == (int)EW_LINE_PARK
               && (int)LINK_ESC_LINE_HIGH == (int)EW_LINE_HIGH
               && (int)LINK_ESC_LINE_UP == (int)EW_LINE_UP
               && (int)LINK_ESC_LINE_LOW == (int)EW_LINE_LOW,
               "the line's words");

/** @brief A staged register by its write offset. */
#define ST(p, reg) ((p)->staged[(reg) - LINK_EW_W_TARGET])

/* --- the wire, as the page tells it ------------------------------------- */

/**
 * @brief start() of the session's driver: notes the line.
 * @param ctx the port
 * @param x   the transaction
 * @return what the wire driver says
 */
static bool wire_start(void *ctx, const ew_xfer_t *x)
{
    esc_port_t *p = (esc_port_t *)ctx;
    const bool ok = p->hw.driver.start(p->hw.driver.ctx, x);

    if (ok) {
        p->line = (uint8_t)LINK_ESC_LINE_SEND;
    }
    return ok;
}

/**
 * @brief poll() of the session's driver: notes where the capture left the pad.
 * @param ctx the port
 * @param out takes the capture
 * @return what the wire driver says
 */
static bool wire_poll(void *ctx, ew_capture_t *out)
{
    esc_port_t *p = (esc_port_t *)ctx;
    const bool ended = p->hw.driver.poll(p->hw.driver.ctx, out);

    if (ended) {
        p->line = (uint8_t)(out->aborted || out->overflow ? LINK_ESC_LINE_PARK
                                                          : LINK_ESC_LINE_UP);
    }
    return ended;
}

/**
 * @brief abort() of the session's driver.
 * @param ctx the port
 */
static void wire_abort(void *ctx)
{
    esc_port_t *p = (esc_port_t *)ctx;

    /* The session's line(EW_LINE_PARK) follows every abort. */
    p->hw.driver.abort(p->hw.driver.ctx);
}

/**
 * @brief line() of the session's driver.
 * @param ctx   the port
 * @param level the pad's state
 */
static void wire_line(void *ctx, ew_line_t level)
{
    esc_port_t *p = (esc_port_t *)ctx;

    p->hw.driver.line(p->hw.driver.ctx, level);
    p->line = (uint8_t)level;
}

/**
 * @brief now_us() of the session's driver.
 * @param ctx the port
 * @return the wire driver's clock
 */
/* cppcheck-suppress constParameterPointer ; ew_driver_t's type */
static uint32_t wire_now(void *ctx)
{
    const esc_port_t *p = (const esc_port_t *)ctx;

    return p->hw.driver.now_us(p->hw.driver.ctx);
}

/* --- the supply ---------------------------------------------------------- */

/**
 * @brief What the supply says.
 * @param p the port
 * @param v takes it
 * @return whether it is online
 */
static bool supply_view(const esc_port_t *p, supply_port_view_t *v)
{
    memset(v, 0, sizeof(*v));
    p->hw.supply(p->hw.ctx, v);
    return v->online;
}

/**
 * @brief The supply off; this session no longer counts on it.
 * @param p the port
 */
static void supply_off(esc_port_t *p)
{
    (void)p->hw.supply_set(p->hw.ctx, false, 0u, 0u);
    p->switched = false;
}

/* --- states -------------------------------------------------------------- */

/**
 * @brief Enter a state.
 * @param p     the port
 * @param state the state
 */
static void go(esc_port_t *p, link_esc_state_t state)
{
    p->state = (uint8_t)state;
    p->t0_ms = p->now_ms;
}

/**
 * @brief Back to free: the line closed, the pin its output's.
 * @param p the port
 */
static void go_free(esc_port_t *p)
{
    const bool had_pin = p->has_pin;

    go(p, LINK_ESC_FREE);
    p->has_pin = false;
    p->stopped = false;
    p->fault = false;
    p->tail = false;
    p->target = 0u;
    p->line = (uint8_t)LINK_ESC_LINE_PARK;
    if (had_pin) {
        p->hw.give(p->hw.ctx);
    }
}

/**
 * @brief Whether a line or supply is in play: a state past OPEN.
 * @param p the port
 * @return true from ENTER on
 */
static bool in_session(const esc_port_t *p)
{
    return p->state > (uint8_t)LINK_ESC_OPEN;
}

/**
 * @brief An error: every output off.  The session is aborted whatever its own
 *        policy says, which parks the line, and the supply goes off.
 * @param p      the port
 * @param result what to say
 */
static void all_off(esc_port_t *p, link_esc_result_t result)
{
    ew_session_abort(&p->ses);
    supply_off(p);
    p->fault = true;
    p->stopped = true;
    p->tail = false;
    p->result = (uint8_t)result;
    go(p, LINK_ESC_OPEN);
}

/**
 * @brief Cut at once, also in a write.
 * @param p the port
 */
static void cut(esc_port_t *p)
{
    if (p->state == (uint8_t)LINK_ESC_STOPPING) {
        go_free(p);
        p->result = (uint8_t)LINK_ESC_RESULT_CUT;
    } else if (in_session(p)) {
        ew_session_abort(&p->ses);
        supply_off(p);
        p->tail = false;
        p->result = (uint8_t)LINK_ESC_RESULT_CUT;
        go(p, LINK_ESC_OPEN);
    }
}

/**
 * @brief A stop: outside a write as cut(), in a write when the write has ended
 *        (op_end()).  It stands until CLEAR or CLOSE.
 * @param p the port
 */
static void on_stop(esc_port_t *p)
{
    if (p->state == (uint8_t)LINK_ESC_STOPPING) {
        cut(p);
        return;
    }
    if (p->state == (uint8_t)LINK_ESC_FREE) {
        return;
    }
    p->stopped = true;
    if (p->state == (uint8_t)LINK_ESC_RUN && ew_session_writing(&p->ses)) {
        ew_session_stop(&p->ses);
        return;
    }
    cut(p);
}

/**
 * @brief The CRC a read copy and a draft are named by.
 * @param b the bytes
 * @param n how many
 * @return link_crc() of them
 */
static uint16_t bytes_crc(const uint8_t *b, size_t n)
{
    return link_crc(LINK_CRC_INIT, b, n);
}

/**
 * @brief An operation of the session ended.
 * @param p the port
 * @param r its result
 */
static void op_end(esc_port_t *p, ew_ses_t r)
{
    if (ew_session_writing(&p->ses) || p->was_writing) {
        p->tail = true;
        p->tail_t0 = p->now_ms;
    }
    if (r != EW_SES_OK) {
        all_off(p, LINK_ESC_RESULT_SESSION);
        return;
    }
    p->result = (uint8_t)LINK_ESC_RESULT_DONE;
    if (p->op == (uint8_t)LINK_ESC_OP_READ) {
        p->reads++;
        p->read_crc = bytes_crc(p->ses.orig, p->ses.id.len);
    }
    if (p->stopped) {
        /* The write a stop waited for: its result stands, the stop now acts. */
        supply_off(p);
        p->tail = false;
        go(p, LINK_ESC_OPEN);
    } else if (p->op == (uint8_t)LINK_ESC_OP_LEAVE) {
        supply_off(p);
        go(p, LINK_ESC_OPEN);
    } else {
        go(p, LINK_ESC_READY);
    }
}

/**
 * @brief What a session's call said when it started.
 * @param p the port
 * @param r the call's result
 * @return LINK_ESC_REF_NONE, or why nothing started
 */
static link_esc_ref_t started(esc_port_t *p, ew_ses_t r)
{
    if (r == EW_SES_BUSY) {
        p->result = (uint8_t)LINK_ESC_RESULT_RUNNING;
        go(p, LINK_ESC_RUN);
        return LINK_ESC_REF_NONE;
    }
    if (r == EW_SES_OK) {
        op_end(p, r);
        return LINK_ESC_REF_NONE;
    }
    if (r == EW_SES_ERR_REFUSED) {
        /* The guard said no: a draft the panel should never have staged. */
        all_off(p, LINK_ESC_RESULT_SESSION);
        p->ses.result = r;
        return LINK_ESC_REF_SESSION;
    }
    /* Nothing went to the wire; a write's order, for the panel to follow. */
    p->ses.result = r;
    p->result = (uint8_t)LINK_ESC_RESULT_SESSION;
    return LINK_ESC_REF_SESSION;
}

/* --- commands ------------------------------------------------------------ */

/**
 * @brief OPEN: take the staged target's pin.
 * @param p  the port
 * @param in the frame
 * @return why nothing started, or LINK_ESC_REF_NONE
 */
static link_esc_ref_t cmd_open(esc_port_t *p, const uint16_t *in)
{
    const uint16_t t = ST(p, LINK_EW_W_TARGET);
    const bool is_port = (t & LINK_EW_TARGET_PORT) != 0u;
    const uint8_t index = (uint8_t)(t & 0xFFu);

    if (p->state != (uint8_t)LINK_ESC_FREE) {
        return LINK_ESC_REF_BUSY;
    }
    if (p->other != 0u) {
        return LINK_ESC_REF_HELD;
    }
    if (p->armed) {
        return LINK_ESC_REF_ARMED;
    }
    if (!p->safe) {
        return LINK_ESC_REF_UNSAFE;
    }
    if (link_kst_crc(p->staged, LINK_EW_W_STAGED) != in[LINK_EW_W_CRC]) {
        return LINK_ESC_REF_STAGED;
    }
    if ((t & ~(uint16_t)(LINK_EW_TARGET_PORT | 0xFFu)) != 0u
        || index >= (is_port ? p->hw.ports : (uint8_t)OUT_MAX_SLOTS)) {
        return LINK_ESC_REF_TARGET;
    }
    const uint8_t pin = p->hw.pin(p->hw.ctx, is_port, index);

    if (pin > OUT_MAX_PIN) {
        return LINK_ESC_REF_NO_PIN;
    }
    p->pin = pin;
    p->target = t;
    p->stopped = false;
    p->fault = false;
    p->result = (uint8_t)LINK_ESC_RESULT_RUNNING;
    go(p, LINK_ESC_STOPPING);
    return LINK_ESC_REF_NONE;
}

/**
 * @brief ENTER's staged values.
 * @param p the port
 * @param v what the supply says
 * @return why they are refused, or LINK_ESC_REF_NONE
 */
static link_esc_ref_t enter_values(const esc_port_t *p,
                                   const supply_port_view_t *v)
{
    const uint16_t power = ST(p, LINK_EW_W_POWER);
    const uint16_t mv = ST(p, LINK_EW_W_MV);
    const uint16_t ma = ST(p, LINK_EW_W_MA);
    const uint32_t hold = (uint32_t)ST(p, LINK_EW_W_HOLD_LO)
                          | ((uint32_t)ST(p, LINK_EW_W_HOLD_HI) << 16);

    if (power >= (uint16_t)LINK_ESC_POWER_COUNT || hold < EW_HOLD_MIN_MS
        || hold > EW_HOLD_MAX_MS || ST(p, LINK_EW_W_OFF_MS) > LINK_ESC_OFF_MS_MAX) {
        return LINK_ESC_REF_RANGE;
    }
    if (power == (uint16_t)LINK_ESC_BY_HAND) {
        return LINK_ESC_REF_NONE;
    }
    if (!v->online) {
        return LINK_ESC_REF_NO_SUPPLY;
    }
    if (mv < LINK_ESC_SUPPLY_MIN_MV || mv > PDMINI_V_MAX_MV
        || ma < PDMINI_I_MIN_MA || ma > PDMINI_I_MAX_MA
        || (v->vin_mv != 0u && (uint32_t)mv + PDMINI_CAP_MV > v->vin_mv)) {
        return LINK_ESC_REF_RANGE;
    }
    return LINK_ESC_REF_NONE;
}

/**
 * @brief ENTER: the staged power, then first contact.
 * @param p  the port
 * @param in the frame
 * @return why nothing started, or LINK_ESC_REF_NONE
 */
static link_esc_ref_t cmd_enter(esc_port_t *p, const uint16_t *in)
{
    supply_port_view_t v;

    if (p->state == (uint8_t)LINK_ESC_FREE) {
        return LINK_ESC_REF_NOT_OPEN;
    }
    if (p->state != (uint8_t)LINK_ESC_OPEN) {
        return LINK_ESC_REF_BUSY;
    }
    if (p->stopped) {
        return LINK_ESC_REF_STOPPED;
    }
    if (!p->safe) {
        return LINK_ESC_REF_UNSAFE;
    }
    if (link_kst_crc(p->staged, LINK_EW_W_STAGED) != in[LINK_EW_W_CRC]) {
        return LINK_ESC_REF_STAGED;
    }
    (void)supply_view(p, &v);
    const link_esc_ref_t bad = enter_values(p, &v);

    if (bad != LINK_ESC_REF_NONE) {
        return bad;
    }
    p->by_hand = ST(p, LINK_EW_W_POWER) == (uint16_t)LINK_ESC_BY_HAND;
    p->mv = ST(p, LINK_EW_W_MV);
    p->ma = ST(p, LINK_EW_W_MA);
    p->off_ms = ST(p, LINK_EW_W_OFF_MS);
    p->hold_ms = (uint32_t)ST(p, LINK_EW_W_HOLD_LO)
                 | ((uint32_t)ST(p, LINK_EW_W_HOLD_HI) << 16);
    p->reads = 0u;
    p->read_crc = 0u;
    p->off_seen = false;
    p->result = (uint8_t)LINK_ESC_RESULT_RUNNING;
    if (p->by_hand) {
        (void)ew_session_hold(&p->ses);
        go(p, LINK_ESC_HAND_WAIT);
    } else {
        supply_off(p);
        go(p, LINK_ESC_POWER_OFF);
    }
    return LINK_ESC_REF_NONE;
}

/**
 * @brief Whether a draft may go: the key, the staged registers, and a draft made
 *        from this read copy.
 * @param p  the port
 * @param in the frame
 * @param fresh false for RESTORE, which follows no read copy
 * @return why not, or LINK_ESC_REF_NONE
 */
static link_esc_ref_t draft_ok(const esc_port_t *p, const uint16_t *in, bool fresh)
{
    if (link_kst_crc(p->staged, LINK_EW_W_STAGED) != in[LINK_EW_W_CRC]) {
        return LINK_ESC_REF_STAGED;
    }
    if (fresh && (ST(p, LINK_EW_W_READS) != p->reads
                  || ST(p, LINK_EW_W_READ_CRC) != p->read_crc)) {
        return LINK_ESC_REF_STALE;
    }
    if (bytes_crc(p->draft, p->ses.id.len) != ST(p, LINK_EW_W_DRAFT_CRC)) {
        return LINK_ESC_REF_DRAFT;
    }
    return LINK_ESC_REF_NONE;
}

/**
 * @brief RESTORE: the draft as a backup, with the identity of LINK_ED_BACKUP.
 * @param p the port
 * @return why nothing started, or LINK_ESC_REF_NONE
 */
static link_esc_ref_t cmd_restore(esc_port_t *p)
{
    ew_backup_id_t from;

    memset(&from, 0, sizeof(from));
    from.pin_code = p->backup[0];
    from.flash_code = p->backup[1];
    from.protocol = p->backup[2];
    memcpy(from.name, &p->backup[4], sizeof(from.name) - 1u);
    return started(p, ew_session_restore(&p->ses, &from, p->draft,
                                         p->ses.id.len));
}

/**
 * @brief The operations on an ESC that answered.
 * @param p  the port
 * @param op the operation
 * @param in the frame
 * @return why nothing started, or LINK_ESC_REF_NONE
 */
static link_esc_ref_t cmd_ready(esc_port_t *p, link_esc_op_t op,
                                const uint16_t *in)
{
    const bool keyed = op == LINK_ESC_OP_WRITE || op == LINK_ESC_OP_RESTORE;

    if (keyed && in[LINK_EW_W_KEY] != LINK_ESC_WRITE_KEY) {
        return LINK_ESC_REF_NO_KEY;
    }
    if (p->state == (uint8_t)LINK_ESC_FREE || p->state == (uint8_t)LINK_ESC_OPEN) {
        return p->stopped ? LINK_ESC_REF_STOPPED : LINK_ESC_REF_NOT_OPEN;
    }
    if (p->state != (uint8_t)LINK_ESC_READY) {
        return LINK_ESC_REF_BUSY;
    }
    if (!p->safe) {
        return LINK_ESC_REF_UNSAFE;
    }
    if (keyed) {
        const link_esc_ref_t bad = draft_ok(p, in, op == LINK_ESC_OP_WRITE);

        if (bad != LINK_ESC_REF_NONE) {
            return bad;
        }
    }
    p->op = (uint8_t)op;
    switch (op) {
    case LINK_ESC_OP_READ:
        return started(p, ew_session_read(&p->ses));
    case LINK_ESC_OP_WRITE:
        return started(p, ew_session_write(&p->ses, p->draft, p->ses.id.len));
    case LINK_ESC_OP_RESTORE:
        return cmd_restore(p);
    case LINK_ESC_OP_LEAVE:
    default:
        return started(p, ew_session_leave(&p->ses));
    }
}

/**
 * @brief One command.
 * @param p  the port
 * @param op the operation
 * @param in the frame
 * @return why nothing started, or LINK_ESC_REF_NONE
 */
static link_esc_ref_t dispatch(esc_port_t *p, link_esc_op_t op,
                               const uint16_t *in)
{
    switch (op) {
    case LINK_ESC_OP_OPEN:
    {
        const link_esc_ref_t r = cmd_open(p, in);

        if (r == LINK_ESC_REF_NONE) {
            p->op = (uint8_t)op;
        }
        return r;
    }
    case LINK_ESC_OP_CLOSE:
        if (p->state == (uint8_t)LINK_ESC_FREE) {
            return LINK_ESC_REF_NOT_OPEN;
        }
        if (esc_port_writing(p)) {
            return LINK_ESC_REF_WRITING;
        }
        cut(p);
        go_free(p);
        p->op = (uint8_t)op;
        p->result = (uint8_t)LINK_ESC_RESULT_DONE;
        return LINK_ESC_REF_NONE;
    case LINK_ESC_OP_ABORT:
        p->op = (uint8_t)op;
        cut(p);
        return LINK_ESC_REF_NONE;
    case LINK_ESC_OP_STOP:
        p->op = (uint8_t)op;
        on_stop(p);
        return LINK_ESC_REF_NONE;
    case LINK_ESC_OP_CLEAR:
        if (p->state == (uint8_t)LINK_ESC_FREE) {
            return LINK_ESC_REF_NOT_OPEN;
        }
        if (p->state != (uint8_t)LINK_ESC_OPEN) {
            return LINK_ESC_REF_BUSY;
        }
        if (!p->safe) {
            return LINK_ESC_REF_UNSAFE;
        }
        p->stopped = false;
        p->fault = false;
        return LINK_ESC_REF_NONE;
    case LINK_ESC_OP_ENTER:
    {
        const link_esc_ref_t r = cmd_enter(p, in);

        if (r == LINK_ESC_REF_NONE) {
            p->op = (uint8_t)op;
        }
        return r;
    }
    case LINK_ESC_OP_DONE:
        if (p->state != (uint8_t)LINK_ESC_HAND_WAIT) {
            return LINK_ESC_REF_NOT_OPEN;
        }
        if (!p->safe) {
            return LINK_ESC_REF_UNSAFE;
        }
        p->op = (uint8_t)LINK_ESC_OP_ENTER;
        return started(p, ew_session_enter(&p->ses, p->hold_ms));
    case LINK_ESC_OP_READ:
    case LINK_ESC_OP_WRITE:
    case LINK_ESC_OP_RESTORE:
    case LINK_ESC_OP_LEAVE:
    default:
        return cmd_ready(p, op, in);
    }
}

/**
 * @brief The command frame.
 * @param p  the port
 * @param in its 4 registers
 * @return 0, or LINK_NACK_BAD_VALUE for a bad frame or SEQ
 */
static uint8_t command(esc_port_t *p, const uint16_t *in)
{
    const uint16_t cmd = in[LINK_EW_W_CMD];

    if (cmd == (uint16_t)LINK_ESC_OP_NONE || cmd >= (uint16_t)LINK_ESC_OP_COUNT
        || (in[LINK_EW_W_KEY] != 0u && in[LINK_EW_W_KEY] != LINK_ESC_WRITE_KEY)) {
        return LINK_NACK_BAD_VALUE;
    }
    /* The frame just taken, sent again: its answer was lost. */
    if (memcmp(in, p->last_cmd, sizeof(p->last_cmd)) == 0) {
        return 0u;
    }
    if (in[LINK_EW_W_SEQ] != (uint16_t)(p->seq + 1u)) {
        return LINK_NACK_BAD_VALUE;
    }
    p->seq = in[LINK_EW_W_SEQ];
    memcpy(p->last_cmd, in, sizeof(p->last_cmd));
    p->refusal = (uint8_t)dispatch(p, (link_esc_op_t)cmd, in);
    return 0u;
}

uint8_t esc_port_write(esc_port_t *p, uint8_t off, uint8_t n,
                       const uint16_t *in, uint32_t now_ms)
{
    if (p == NULL || in == NULL || n == 0u
        || (unsigned)off + n > LINK_EW_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    p->now_ms = now_ms;
    if (off < LINK_EW_W_CMD_FRAME) {
        if (off != 0u || n != LINK_EW_W_CMD_FRAME) {
            return LINK_NACK_BAD_RANGE;
        }
        return command(p, in);
    }
    if ((unsigned)off + n > LINK_EW_W_COUNT) {
        return LINK_NACK_READ_ONLY;
    }
    if ((unsigned)off + n > LINK_EW_W_BLOCK
        && in[LINK_EW_W_BLOCK - off] >= (uint16_t)LINK_ED_BLOCKS) {
        return LINK_NACK_BAD_VALUE;
    }
    for (unsigned i = 0u; i < n; ++i) {
        const unsigned at = off + i;

        if (at == LINK_EW_W_BLOCK) {
            p->block = (uint8_t)in[i];
        } else {
            p->staged[at - LINK_EW_W_TARGET] = in[i];
        }
    }
    return 0u;
}

/* --- the pages ----------------------------------------------------------- */

/**
 * @brief The FLAGS register.
 * @param p the port
 * @return its value
 */
static uint16_t flags_reg(const esc_port_t *p)
{
    supply_port_view_t v;
    uint16_t f = 0u;

    f |= p->stopped ? LINK_EW_F_STOPPED : 0u;
    f |= p->fault ? LINK_EW_F_FAULT : 0u;
    f |= p->switched ? LINK_EW_F_SUPPLY : 0u;
    f |= supply_view(p, &v) ? LINK_EW_F_RAIL : 0u;
    f |= p->ses.id.writable ? LINK_EW_F_WRITABLE : 0u;
    f |= p->ses.id.erased ? LINK_EW_F_ERASED : 0u;
    f |= p->ses.id.can_build ? LINK_EW_F_CAN : 0u;
    f |= p->ses.in_mode ? LINK_EW_F_IN_MODE : 0u;
    f |= esc_port_writing(p) ? LINK_EW_F_WRITING : 0u;
    f |= p->ses.dirty ? LINK_EW_F_DIRTY : 0u;
    f |= p->ses.have_read ? LINK_EW_F_HAVE_READ : 0u;
    f |= p->ses.save_open ? LINK_EW_F_SAVE_OPEN : 0u;
    return f;
}

void esc_port_read(const esc_port_t *p, uint8_t off, uint8_t n, uint16_t *out)
{
    uint16_t all[LINK_MAX_REGS];

    if (p == NULL || out == NULL || (unsigned)off + n > LINK_EW_COUNT) {
        return;
    }
    const ew_session_t *s = &p->ses;
    const uint32_t t = p->now_ms - p->t0_ms;

    memset(all, 0, sizeof(all));
    all[LINK_EW_STATE] = p->state;
    all[LINK_EW_SEQ] = p->seq;
    all[LINK_EW_OP] = p->op;
    all[LINK_EW_RESULT] = (uint16_t)(p->result | ((uint16_t)p->refusal << 8));
    all[LINK_EW_FLAGS] = flags_reg(p);
    all[LINK_EW_LINE] = (uint16_t)(p->line
                                   | ((uint16_t)(p->has_pin ? p->pin
                                                            : ESC_PORT_NO_PIN)
                                      << 8));
    all[LINK_EW_TARGET] = p->target;
    all[LINK_EW_SES] = (uint16_t)((uint16_t)s->result
                                  | ((uint16_t)s->refuse << 8));
    all[LINK_EW_ID] = (uint16_t)(s->id.fw | ((uint16_t)s->id.layout << 8));
    all[LINK_EW_WHY] = (uint16_t)(s->id.why != 0u ? s->id.why : s->contact.why);
    all[LINK_EW_WHY] |= (uint16_t)((uint16_t)s->contact.cpu << 8);
    all[LINK_EW_SIGNATURE] = s->contact.signature;
    all[LINK_EW_LEN] = s->id.len;
    all[LINK_EW_BAD_OFF] = s->bad_off;
    all[LINK_EW_CLIPPED] = (uint16_t)s->clipped;
    all[LINK_EW_TRIES] = (uint16_t)(s->attempts | ((uint16_t)s->last_ans << 8));
    all[LINK_EW_READS] = p->reads;
    all[LINK_EW_READ_CRC] = p->read_crc;
    all[LINK_EW_T_MS] = (uint16_t)(t > 0xFFFFu ? 0xFFFFu : t);
    all[LINK_EW_CONTACT] = (uint16_t)(s->contact.pin_code
                                      | ((uint16_t)s->contact.flash_code << 8));
    all[LINK_EW_PROTOCOL] = s->contact.protocol;
    all[LINK_EW_BLOCK] = p->block;
    for (unsigned i = 0u; i < n; ++i) {
        out[i] = all[off + i];
    }
}

/**
 * @brief Where a block's bytes are, and how far into them it reaches.
 * @param p     the port
 * @param block the block
 * @param size  takes its bytes
 * @return the first byte
 */
static const uint8_t *block_bytes(const esc_port_t *p, unsigned block,
                                  size_t *size)
{
    const ew_session_t *s = &p->ses;
    const uint8_t *base;
    size_t room;
    unsigned first = 0u;

    if (block < (unsigned)LINK_ED_DRAFT) {
        base = s->orig;
        room = sizeof(s->orig);
    } else if (block < (unsigned)LINK_ED_PAGE) {
        base = p->draft;
        room = sizeof(p->draft);
        first = LINK_ED_DRAFT;
    } else if (block < (unsigned)LINK_ED_NAME) {
        base = s->page;
        room = sizeof(s->page);
        first = LINK_ED_PAGE;
    } else if (block == (unsigned)LINK_ED_NAME) {
        base = (const uint8_t *)s->id.name;
        room = sizeof(s->id.name);
        first = LINK_ED_NAME;
    } else if (block < (unsigned)LINK_ED_MUSIC) {
        base = (const uint8_t *)&s->info;
        room = sizeof(s->info);
        first = LINK_ED_INFO;
    } else if (block < (unsigned)LINK_ED_BACKUP) {
        base = (const uint8_t *)s->music;
        room = sizeof(s->music);
        first = LINK_ED_MUSIC;
    } else {
        base = p->backup;
        room = sizeof(p->backup);
        first = LINK_ED_BACKUP;
    }
    const size_t at = (size_t)(block - first) * LINK_ED_BLOCK_BYTES;

    *size = room - at < LINK_ED_BLOCK_BYTES ? room - at : LINK_ED_BLOCK_BYTES;
    return base + at;
}

void esc_port_data_read(const esc_port_t *p, uint8_t off, uint8_t n,
                        uint16_t *out)
{
    size_t size = 0u;

    if (p == NULL || out == NULL || (unsigned)off + n > LINK_ED_COUNT) {
        return;
    }
    const uint8_t *b = block_bytes(p, p->block, &size);

    for (unsigned i = 0u; i < n; ++i) {
        const size_t at = ((size_t)off + i) * 2u;

        out[i] = 0u;
        if (at + 1u < size) {
            out[i] = (uint16_t)(b[at] | ((uint16_t)b[at + 1u] << 8));
        }
    }
}

uint8_t esc_port_data_write(esc_port_t *p, uint8_t off, uint8_t n,
                            const uint16_t *in)
{
    if (p == NULL || in == NULL || n == 0u
        || (unsigned)off + n > LINK_ED_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    const bool draft = p->block >= (uint8_t)LINK_ED_DRAFT
                       && p->block < (uint8_t)LINK_ED_PAGE;

    if (!draft && p->block != (uint8_t)LINK_ED_BACKUP) {
        return LINK_NACK_READ_ONLY;
    }
    if (p->state == (uint8_t)LINK_ESC_RUN) {
        return LINK_NACK_BUSY;
    }
    uint8_t *b = draft ? p->draft + (size_t)(p->block - (uint8_t)LINK_ED_DRAFT)
                                        * LINK_ED_BLOCK_BYTES
                       : p->backup;

    for (unsigned i = 0u; i < n; ++i) {
        const size_t at = ((size_t)off + i) * 2u;

        b[at] = (uint8_t)(in[i] & 0xFFu);
        b[at + 1u] = (uint8_t)(in[i] >> 8);
    }
    return 0u;
}

/* --- the pass ------------------------------------------------------------ */

/**
 * @brief ENTER's way to ON: the supply off, read off, its off time.
 * @param p the port
 */
static void step_power_off(esc_port_t *p)
{
    supply_port_view_t v;
    const uint32_t t = p->now_ms - p->t0_ms;

    if (!p->off_seen) {
        if (supply_view(p, &v) && v.off && t >= ESC_PORT_READ_OFF_MIN_MS) {
            p->off_seen = true;
            p->t0_ms = p->now_ms;
        } else if (t >= ESC_PORT_READ_OFF_MAX_MS) {
            all_off(p, LINK_ESC_RESULT_SUPPLY);
        }
    } else if (t >= p->off_ms) {
        (void)ew_session_hold(&p->ses);
        go(p, LINK_ESC_LEAD);
    }
}

/**
 * @brief Whether the supply this session switched on is gone: asked off, a PD
 *        mini that stopped answering, or read off after it read on.
 * @param p the port
 * @return true then
 */
static bool supply_gone(esc_port_t *p)
{
    supply_port_view_t v;

    if (!p->switched) {
        return false;
    }
    if (!supply_view(p, &v) || !v.asked_on) {
        return true;
    }
    if (!v.off) {
        p->read_on = true;
    }
    return v.off && p->read_on;
}

/**
 * @brief The line has been high long enough: ON and the hold.
 * @param p the port
 */
static void step_lead(esc_port_t *p)
{
    if (p->now_ms - p->t0_ms < ESC_PORT_LEAD_MS) {
        return;
    }
    if (!p->hw.supply_set(p->hw.ctx, true, p->mv, p->ma)) {
        all_off(p, LINK_ESC_RESULT_SUPPLY);
        return;
    }
    p->switched = true;
    p->read_on = false;
    (void)started(p, ew_session_enter(&p->ses, p->hold_ms));
}

void esc_port_step(esc_port_t *p, bool safe, bool stop, uint32_t now_ms)
{
    if (p == NULL) {
        return;
    }
    p->now_ms = now_ms;
    p->safe = safe;
    if (!safe) {
        const bool held = p->state != (uint8_t)LINK_ESC_FREE;

        cut(p);
        p->stopped = held && p->state != (uint8_t)LINK_ESC_FREE;
    } else if (stop) {
        on_stop(p);
    }
    if (p->tail && now_ms - p->tail_t0 >= ESC_PORT_WRITE_TAIL_MS) {
        p->tail = false;
    }
    switch ((link_esc_state_t)p->state) {
    case LINK_ESC_STOPPING:
        if (now_ms - p->t0_ms >= ESC_PORT_STOP_MS) {
            p->has_pin = true;
            if (!p->hw.take(p->hw.ctx, p->pin)) {
                go_free(p);
                p->result = (uint8_t)LINK_ESC_RESULT_NO_LINE;
            } else {
                p->line = (uint8_t)LINK_ESC_LINE_PARK;
                p->result = (uint8_t)LINK_ESC_RESULT_DONE;
                go(p, LINK_ESC_OPEN);
            }
        }
        break;
    case LINK_ESC_POWER_OFF:
        step_power_off(p);
        break;
    case LINK_ESC_LEAD:
        step_lead(p);
        break;
    case LINK_ESC_HAND_WAIT:
        if (now_ms - p->t0_ms >= ESC_PORT_HAND_MS) {
            all_off(p, LINK_ESC_RESULT_TIMEOUT);
        }
        break;
    case LINK_ESC_RUN:
        if (supply_gone(p)) {
            all_off(p, LINK_ESC_RESULT_SUPPLY);
            break;
        }
        {
            const bool writing = ew_session_writing(&p->ses);
            const ew_ses_t r = ew_session_step(&p->ses);

            p->was_writing = writing;
            if (r != EW_SES_BUSY) {
                op_end(p, r);
                p->was_writing = false;
            }
        }
        break;
    case LINK_ESC_READY:
        if (supply_gone(p)) {
            all_off(p, LINK_ESC_RESULT_SUPPLY);
        }
        break;
    case LINK_ESC_FREE:
    case LINK_ESC_OPEN:
    default:
        break;
    }
}

bool esc_port_init(esc_port_t *p, const esc_port_hw_t *hw)
{
    if (p == NULL) {
        return false;
    }
    memset(p, 0, sizeof(*p));
    if (hw == NULL || hw->ports == 0u || hw->ports > LINK_PG_PORTS_MAX
        || hw->pin == NULL || hw->take == NULL || hw->give == NULL
        || hw->supply == NULL || hw->supply_set == NULL
        || hw->driver.start == NULL || hw->driver.poll == NULL
        || hw->driver.abort == NULL || hw->driver.line == NULL
        || hw->driver.now_us == NULL) {
        return false;
    }
    p->hw = *hw;
    const ew_driver_t d = { p, wire_start, wire_poll, wire_abort, wire_line,
                            wire_now };

    return ew_session_init(&p->ses, &d) == EW_SES_OK;
}

link_esc_state_t esc_port_state(const esc_port_t *p)
{
    return p != NULL ? (link_esc_state_t)p->state : LINK_ESC_FREE;
}

uint8_t esc_port_quiet_mask(const esc_port_t *p)
{
    if (p == NULL || p->state == (uint8_t)LINK_ESC_FREE) {
        return 0u;
    }
    if ((p->target & LINK_EW_TARGET_PORT) != 0u) {
        return (uint8_t)(1u << (p->target & 0x0Fu));
    }
    return (uint8_t)ESC_PORT_QUIET_SLOT;
}

uint8_t esc_port_pin_mask(const esc_port_t *p)
{
    return (uint8_t)(p != NULL && p->has_pin
                             && (p->target & LINK_EW_TARGET_PORT) != 0u
                         ? 1u << (p->target & 0x0Fu)
                         : 0u);
}

uint8_t esc_port_may_bind(const esc_port_t *p, uint8_t mask)
{
    if (p == NULL || !p->has_pin || (p->target & LINK_EW_TARGET_PORT) != 0u) {
        return mask;
    }
    return (uint8_t)(mask & ~(1u << (p->target & 0x07u)));
}

uint64_t esc_port_pins(const esc_port_t *p)
{
    return p != NULL && p->has_pin ? (uint64_t)1u << p->pin : 0u;
}

bool esc_port_supply_hold(const esc_port_t *p)
{
    return p != NULL && p->state != (uint8_t)LINK_ESC_FREE;
}

bool esc_port_writing(const esc_port_t *p)
{
    return p != NULL && (ew_session_writing(&p->ses) || p->tail);
}

void esc_port_bench_armed(esc_port_t *p, bool armed)
{
    if (p != NULL) {
        p->armed = armed;
    }
}

void esc_port_other(esc_port_t *p, uint8_t mask)
{
    if (p != NULL) {
        p->other = mask;
    }
}

uint8_t esc_port_slot_pin(const outputs_t *o, uint8_t slot, uint8_t bound)
{
    if (o == NULL || slot >= OUT_MAX_SLOTS || (bound & (1u << slot)) == 0u) {
        return ESC_PORT_NO_PIN;
    }
    const out_slot_t *s = &o->slot[slot];

    if (s->driver == OUT_DRIVER_NONE || s->first_channel >= OUT_MAX_CHANNELS
        || o->channel[s->first_channel].role != OUT_ROLE_THROTTLE) {
        return ESC_PORT_NO_PIN;
    }
    return s->pin;
}
