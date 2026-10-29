/**
 * @file
 * @brief The Hitec servo wire on a programming port: the OPEN, transaction,
 *        POWER and CLOSE states.
 *
 * SPDX-License-Identifier: MIT
 */

#include "hitec_port.h"

#include <stddef.h>
#include <string.h>

#include "hitec_pre.h"

_Static_assert(LINK_HT_W_LINE + LINK_HT_W_STAGED == LINK_HT_W_VIEW,
               "staged registers");
_Static_assert(LINK_HT_W_VIEW + 1 == LINK_HT_W_RX_MODE
               && LINK_HT_W_RX_MODE + 1 == LINK_HT_W_COUNT,
               "the view and the receive mode");
_Static_assert((int)LINK_HITEC_RX_PULL_UP == (int)HITEC_RX_PULL_UP
               && (int)LINK_HITEC_RX_PRECHARGE == (int)HITEC_RX_PRECHARGE,
               "the receive mode crosses to the driver unchanged");
_Static_assert(LINK_HT_BYTES + HITEC_RX_MAX / 2 == LINK_HT_DATA,
               "the bytes on the page");
_Static_assert(LINK_HT_DATA + 16 == LINK_HT_COUNT
               && LINK_HT_COUNT <= LINK_MAX_REGS, "the page");
_Static_assert(HITEC_RX_MAX == 16 && HITEC_TX_MAX == 8,
               "the page carries 16 bytes in and 8 out");
_Static_assert(LINK_HITEC_OP_COUNT <= 16, "the op is 4 bits");

/** @brief The transaction count stops at this value (12 bits). */
#define COUNT_MAX 0x0FFFu

/* A staged register by its write offset. */
#define ST(p, reg) ((p)->staged[(reg) - LINK_HT_W_LINE])

/**
 * @brief Read the 32-bit staged value at write offset @p lo, low word first.
 *
 * @param p  The port.
 * @param lo Write offset of the low 16-bit register.
 * @return The registers lo and lo + 1 joined, lo in the low half.
 */
static uint32_t st32(const hitec_port_t *p, unsigned lo)
{
    return (uint32_t)ST(p, lo) | ((uint32_t)ST(p, lo + 1u) << 16);
}

/* --- the supply ---------------------------------------------------------- */

/**
 * @brief Read the supply's view of the port into @p v.
 *
 * @param p The port.
 * @param v Receives the view; zeroed first.
 * @return true when the supply is online; false when the build has no supply.
 */
static bool supply_view(const hitec_port_t *p, supply_port_view_t *v)
{
    memset(v, 0, sizeof(*v));
    if (p->hw.supply == NULL) {
        return false;
    }
    p->hw.supply(p->hw.ctx, v);
    return v->online;
}

/**
 * @brief Ask the supply off, when the build has a supply to switch.
 *
 * @param p The port.
 */
static void supply_off(hitec_port_t *p)
{
    if (p->hw.supply_set != NULL) {
        (void)p->hw.supply_set(p->hw.ctx, false, 0u, 0u);
    }
}

/* --- states -------------------------------------------------------------- */

/**
 * @brief Start an operation: record its code and set the result to RUNNING.
 *
 * @param p  The port.
 * @param op The operation to record.
 */
static void op_begin(hitec_port_t *p, link_hitec_op_t op)
{
    p->op = (uint8_t)op;
    p->result = (uint8_t)LINK_HITEC_RESULT_RUNNING;
}

/**
 * @brief Back to free: the line closed, the pin the generator's.
 *
 * @param p The port.
 */
static void go_free(hitec_port_t *p)
{
    const bool had_pin = p->has_pin != 0u;

    p->state = (uint8_t)LINK_HITEC_FREE;
    p->has_pin = 0;
    p->stopped = 0;
    p->cutting = 0;
    p->off_seen = 0;
    p->more = 0;
    p->wframe = 0;
    p->tail = 0;
    if (had_pin) {
        p->hw.give(p->hw.ctx);
    }
}

/**
 * @brief The capture is in: decode it as the transaction asked.
 *
 * @param p The port.
 */
static void finish(hitec_port_t *p)
{
    hitec_rx_t r = HITEC_RX_OK;

    memset(&p->reply, 0, sizeof(p->reply));
    p->cap_us = hitec_elapsed_us(p->hw.driver.now_us(p->hw.driver.ctx),
                                 p->start_us);
    switch ((link_hitec_dec_t)p->dec) {
    case LINK_HITEC_DEC_C: {
        hitec_c_expect_t e = HITEC_C_EXPECT_VALUE;

        (void)hitec_c_expect_of(p->dec_arg, &e);
        r = hitec_c_decode(&p->cap, e, &p->reply);
        break;
    }
    case LINK_HITEC_DEC_D:
        r = hitec_d_decode(&p->cap, p->dec_arg, &p->reply);
        break;
    case LINK_HITEC_DEC_LISTEN:
        r = hitec_listen_decode(&p->cap, p->dec_arg, &p->reply);
        break;
    case LINK_HITEC_DEC_NONE:
    default:
        p->reply.n = p->cap.n;
        break;
    }
    p->rx = (uint8_t)r;
    p->has_cap = 1;
    p->cutting = 0;
    p->state = (uint8_t)LINK_HITEC_OPEN;
    p->result = p->cap.aborted != 0u ? (uint8_t)LINK_HITEC_RESULT_CUT
                                     : (uint8_t)LINK_HITEC_RESULT_DONE;
}

/**
 * @brief Cut the running transaction; its capture comes at the next poll.
 *
 * @param p The port.
 */
static void cut(hitec_port_t *p)
{
    if (p->state == (uint8_t)LINK_HITEC_BUSY && p->cutting == 0u) {
        p->hw.abort(p->hw.ctx);
        p->cutting = 1;
    }
}

/**
 * @brief Start @p x on the line: BUSY, or OPEN with LINK_HITEC_RESULT_START.
 *
 * @param p     The port.
 * @param x     The transaction to start.
 * @param keyed 1 when the transaction puts a byte out, else 0.
 */
static void start(hitec_port_t *p, const hitec_xfer_t *x, uint8_t keyed)
{
    supply_port_view_t v;

    p->xfer = *x;
    p->supply_on = supply_view(p, &v) && v.asked_on ? 1u : 0u;
    /* POWER's listen starts in the pass of the ON: the output reads off
     * until the PD mini reports it on.  Any other transaction starts on a
     * supply that is on already. */
    p->read_on = p->op != (uint8_t)LINK_HITEC_OP_POWER || !v.off ? 1u : 0u;
    memset(&p->info, 0, sizeof(p->info));
    p->start_us = p->hw.driver.now_us(p->hw.driver.ctx);
    if (!p->hw.driver.start(p->hw.driver.ctx, x)) {
        p->state = (uint8_t)LINK_HITEC_OPEN;
        p->result = (uint8_t)LINK_HITEC_RESULT_START;
        return;
    }
    if (p->hw.run != NULL) {
        p->hw.run(p->hw.ctx, &p->info);
    }
    p->keyed = keyed;
    if (p->count < COUNT_MAX) {
        p->count++;
    }
    p->state = (uint8_t)LINK_HITEC_BUSY;
}

/**
 * @brief Apply a stop to the port, by its state.
 *
 * @param p The port.
 */
static void on_stop(hitec_port_t *p)
{
    /* A stopped port runs no more steps of a write. */
    p->more = 0;
    switch ((link_hitec_state_t)p->state) {
    case LINK_HITEC_STOPPING:
        go_free(p);
        p->result = (uint8_t)LINK_HITEC_RESULT_CUT;
        break;
    case LINK_HITEC_OPEN:
        p->stopped = 1;
        break;
    case LINK_HITEC_BUSY:
        cut(p);
        p->stopped = 1;
        break;
    case LINK_HITEC_POWER_OFF:
        /* The supply was asked off and stays off. */
        p->state = (uint8_t)LINK_HITEC_OPEN;
        p->result = (uint8_t)LINK_HITEC_RESULT_CUT;
        p->stopped = 1;
        break;
    case LINK_HITEC_FREE:
    default:
        break;
    }
}

/* --- commands ------------------------------------------------------------ */

/**
 * @brief The decoder the staged registers ask for, checked against the family.
 *
 * @param p  The port.
 * @param op The operation the registers were staged for.
 * @return true when the staged decoder fits the operation and the family.
 */
static bool decoder_ok(const hitec_port_t *p, link_hitec_op_t op)
{
    const uint16_t d = ST(p, LINK_HT_W_DECODE);
    const uint8_t kind = (uint8_t)(d & 0x3u);
    const uint8_t arg = (uint8_t)(d >> 8);
    hitec_c_expect_t e;

    if ((d & 0x00FCu) != 0u) {
        return false;
    }
    if (op == LINK_HITEC_OP_LEVEL) {
        return kind == (uint8_t)LINK_HITEC_DEC_NONE;
    }
    switch ((link_hitec_dec_t)kind) {
    case LINK_HITEC_DEC_C:
        return p->family == (uint8_t)HITEC_CLASSIC
               && hitec_c_expect_of(arg, &e);
    case LINK_HITEC_DEC_D:
        return p->family == (uint8_t)HITEC_D;
    case LINK_HITEC_DEC_NONE:
    case LINK_HITEC_DEC_LISTEN:
    default:
        return true;
    }
}

/**
 * @brief The transaction the staged registers say for @p op; false when it is
 * not one the line runs.
 *
 * @param p  The port.
 * @param op The operation the registers were staged for.
 * @param x  Receives the transaction the staged registers describe.
 * @return true when the transaction is valid; false when it is not one the line
 *         runs.
 */
static bool staged_xfer(const hitec_port_t *p, link_hitec_op_t op,
                        hitec_xfer_t *x)
{
    const uint16_t ntx = ST(p, LINK_HT_W_NTX);
    const uint16_t rx = ST(p, LINK_HT_W_RX);
    const uint16_t ans = ST(p, LINK_HT_W_ANSWER);

    if ((rx & 0xFE00u) != 0u || (ans & 0xFE00u) != 0u
        || !decoder_ok(p, op)) {
        return false;
    }
    memset(x, 0, sizeof(*x));
    x->family = p->family;
    if (op == LINK_HITEC_OP_SEND) {
        x->kind = (uint8_t)HITEC_XFER_SEND;
        x->n_tx = (uint8_t)(ntx & 0xFFu);
        x->gap_bits = (uint8_t)(ntx >> 8);
        for (size_t k = 0; k < HITEC_TX_MAX / 2u; ++k) {
            const uint16_t w = ST(p, LINK_HT_W_TX + k);

            x->tx[2u * k] = (uint8_t)(w & 0xFFu);
            x->tx[2u * k + 1u] = (uint8_t)(w >> 8);
        }
        x->lead_us = st32(p, LINK_HT_W_LEAD_LO);
        x->window_us = st32(p, LINK_HT_W_WINDOW_LO);
        x->max_rx = (uint8_t)(rx & 0xFFu);
        x->answer = (uint8_t)((rx >> 8) & 1u);
    } else if (op == LINK_HITEC_OP_LEVEL) {
        x->kind = (uint8_t)HITEC_XFER_LEVEL;
        x->level = (uint8_t)((ans >> 8) & 1u);
        x->level_us = st32(p, LINK_HT_W_LEVEL_LO);
    } else {
        x->kind = (uint8_t)HITEC_XFER_LISTEN;
        x->window_us = st32(p, LINK_HT_W_WINDOW_LO);
        x->max_rx = (uint8_t)(rx & 0xFFu);
        x->answer = (uint8_t)((rx >> 8) & 1u);
        x->answer_byte = (uint8_t)(ans & 0xFFu);
    }
    return hitec_xfer_valid(x);
}

/**
 * @brief Whether the PD mini gives the servo @p mv and @p ma: inside the POWER
 * bounds, and under the input with the module's cap.
 *
 * @param v      The supply's view.
 * @param mv     The supply set point in mV.
 * @param ma     The supply current limit in mA.
 * @param off_ms The off time in milliseconds.
 * @return true when the set points and the off time are in range and the input
 *         is unknown or holds the module's cap above mv.
 */
static bool power_range_ok(const supply_port_view_t *v, uint16_t mv,
                           uint16_t ma, uint16_t off_ms)
{
    if (mv < LINK_HITEC_SUPPLY_MIN_MV || mv > LINK_HITEC_SUPPLY_MAX_MV
        || ma < PDMINI_I_MIN_MA || ma > PDMINI_I_MAX_MA
        || off_ms > LINK_HITEC_OFF_MS_MAX) {
        return false;
    }
    return v->vin_mv == 0u || (uint32_t)mv + PDMINI_CAP_MV <= v->vin_mv;
}

/**
 * @brief Start OPEN on @p port: take the staged line and the port's pin, and
 *        enter STOPPING.
 *
 * @param p      The port.
 * @param port   Programming port index.
 * @param in     The command frame's registers.
 * @param now_ms Current time in milliseconds.
 * @return LINK_HITEC_REF_NONE when OPEN starts; otherwise the refusal.
 */
static link_hitec_refusal_t open_port(hitec_port_t *p, uint8_t port,
                                      const uint16_t *in, uint32_t now_ms)
{
    const uint16_t line = ST(p, LINK_HT_W_LINE);
    const uint8_t family = (uint8_t)(line & 1u);
    const uint8_t drive = (uint8_t)((line >> 4) & 1u);
    uint8_t pin;

    if (p->state != (uint8_t)LINK_HITEC_FREE) {
        return LINK_HITEC_REF_BUSY;
    }
    if ((p->other & (1u << port)) != 0u) {
        return LINK_HITEC_REF_HELD;
    }
    if (p->armed != 0u) {
        return LINK_HITEC_REF_ARMED;
    }
    if (p->safe == 0u) {
        return LINK_HITEC_REF_UNSAFE;
    }
    pin = p->hw.pin(p->hw.ctx, port);
    if (pin > OUT_MAX_PIN) {
        return LINK_HITEC_REF_NO_PIN;
    }
    if (link_kst_crc(p->staged, LINK_HT_W_STAGED) != in[LINK_HT_W_CRC]) {
        return LINK_HITEC_REF_STAGED;
    }
    if ((line & 0xFFEEu) != 0u
        || (family == (uint8_t)HITEC_CLASSIC
            && drive == (uint8_t)HITEC_DRIVE_PUSH_PULL)) {
        return LINK_HITEC_REF_RANGE;
    }
    p->port = port;
    p->pin = pin;
    p->family = family;
    p->drive = drive;
    p->rx_line = family == (uint8_t)HITEC_D ? p->rx_mode
                                       : (uint8_t)LINK_HITEC_RX_PULL_UP;
    p->count = 0;
    p->has_cap = 0;
    p->keyed = 0;
    p->state = (uint8_t)LINK_HITEC_STOPPING;
    p->t0_ms = now_ms;
    op_begin(p, LINK_HITEC_OP_OPEN);
    return LINK_HITEC_REF_NONE;
}

/**
 * @brief Start CLOSE on @p port: cut what runs, close the line and free the
 *        port.
 *
 * @param p    The port.
 * @param port Programming port index.
 * @return LINK_HITEC_REF_NONE when CLOSE runs; otherwise the refusal.
 */
static link_hitec_refusal_t close_port(hitec_port_t *p, uint8_t port)
{
    if (p->state == (uint8_t)LINK_HITEC_FREE) {
        return LINK_HITEC_REF_NOT_OPEN;
    }
    if (port != p->port) {
        return LINK_HITEC_REF_PORT;
    }
    if (p->state == (uint8_t)LINK_HITEC_BUSY) {
        /* The bytes so far, before the line closes. */
        cut(p);
        if (p->hw.driver.poll(p->hw.driver.ctx, &p->cap)) {
            finish(p);
        }
    }
    go_free(p);
    op_begin(p, LINK_HITEC_OP_CLOSE);
    p->result = (uint8_t)LINK_HITEC_RESULT_DONE;
    return LINK_HITEC_REF_NONE;
}

/**
 * @brief Cut what runs: a STOPPING port is freed, a BUSY port is cut, and a
 *        POWER in its off time ends.
 *
 * @param p The port.
 */
static void abort_op(hitec_port_t *p)
{
    switch ((link_hitec_state_t)p->state) {
    case LINK_HITEC_STOPPING:
        go_free(p);
        p->result = (uint8_t)LINK_HITEC_RESULT_CUT;
        break;
    case LINK_HITEC_BUSY:
        cut(p);
        break;
    case LINK_HITEC_POWER_OFF:
        p->state = (uint8_t)LINK_HITEC_OPEN;
        p->result = (uint8_t)LINK_HITEC_RESULT_CUT;
        break;
    case LINK_HITEC_FREE:
    case LINK_HITEC_OPEN:
    default:
        break;
    }
}

/**
 * @brief Start POWER: check the supply and the set points, then ask the supply
 *        off.
 *
 * @param p      The port.
 * @param x      The listen the POWER runs after the ON.
 * @param now_ms Current time in milliseconds.
 * @return LINK_HITEC_REF_NONE when POWER starts; otherwise the refusal.
 */
static link_hitec_refusal_t power(hitec_port_t *p, const hitec_xfer_t *x,
                                  uint32_t now_ms)
{
    const uint16_t mv = ST(p, LINK_HT_W_SUPPLY_MV);
    const uint16_t ma = ST(p, LINK_HT_W_SUPPLY_MA);
    const uint16_t off_ms = ST(p, LINK_HT_W_OFF_MS);
    supply_port_view_t v;

    if (!supply_view(p, &v)) {
        return LINK_HITEC_REF_NO_SUPPLY;
    }
    if (!power_range_ok(&v, mv, ma, off_ms)) {
        return LINK_HITEC_REF_SUPPLY_RANGE;
    }
    p->has_cap = 0;
    p->xfer = *x;
    p->mv = mv;
    p->ma = ma;
    p->off_ms = off_ms;
    p->off_seen = 0;
    supply_off(p);
    p->state = (uint8_t)LINK_HITEC_POWER_OFF;
    p->t0_ms = now_ms;
    op_begin(p, LINK_HITEC_OP_POWER);
    return LINK_HITEC_REF_NONE;
}

/**
 * @brief Whether a SEND's frame writes to the servo: a classic WRITE_EEPROM
 *        (0x62) or WRITE_RAM (0x64), or a D series write request (0x96,
 *        HITEC_D_WRITE_LEN bytes, the save and the restart included).
 *
 * Told from the frame's bytes, so a writer marks nothing for it.  A read,
 * a MOVE, a FIRST, a LISTEN, a LEVEL and a POWER write nothing.
 *
 * @param family The line's servo family (hitec_family_t).
 * @param op     The operation.
 * @param x      The transaction.
 * @return true for a frame that writes.
 */
static bool writes_servo(uint8_t family, link_hitec_op_t op,
                         const hitec_xfer_t *x)
{
    if (op != LINK_HITEC_OP_SEND) {
        return false;
    }
    if (family == (uint8_t)HITEC_D) {
        return x->n_tx == HITEC_D_WRITE_LEN
               && x->tx[0] == HITEC_D_HEADER_REQUEST;
    }
    return x->n_tx == HITEC_C_FRAME_LEN
           && (x->tx[0] == HITEC_C_WRITE_EEPROM
               || x->tx[0] == HITEC_C_WRITE_RAM);
}

/**
 * @brief Whether a transaction puts a byte on the wire.
 *
 * @param op The operation.
 * @param x  The transaction.
 * @return true for a SEND, or a transaction that answers.
 */
static bool puts_byte_out(link_hitec_op_t op, const hitec_xfer_t *x)
{
    return op == LINK_HITEC_OP_SEND || x->answer != 0u;
}

/**
 * @brief SEND, LISTEN, LEVEL and POWER.
 *
 * @param p      The port.
 * @param op     The operation.
 * @param port   Programming port index.
 * @param in     The command frame's registers.
 * @param now_ms Current time in milliseconds.
 * @return LINK_HITEC_REF_NONE when the operation starts; otherwise the refusal.
 */
static link_hitec_refusal_t transaction(hitec_port_t *p, link_hitec_op_t op,
                                        uint8_t port, const uint16_t *in,
                                        uint32_t now_ms)
{
    hitec_xfer_t x;
    const bool key = in[LINK_HT_W_KEY] == LINK_HITEC_WRITE_KEY;

    /* A SEND without the key is refused as that, whatever else is wrong
     * with it. */
    if (op == LINK_HITEC_OP_SEND && !key) {
        return LINK_HITEC_REF_NO_KEY;
    }
    if (p->state == (uint8_t)LINK_HITEC_FREE) {
        return LINK_HITEC_REF_NOT_OPEN;
    }
    if (p->state != (uint8_t)LINK_HITEC_OPEN) {
        return LINK_HITEC_REF_BUSY;
    }
    if (port != p->port) {
        return LINK_HITEC_REF_PORT;
    }
    if (p->stopped != 0u) {
        return LINK_HITEC_REF_STOPPED;
    }
    if (p->safe == 0u) {
        return LINK_HITEC_REF_UNSAFE;
    }
    if (link_kst_crc(p->staged, LINK_HT_W_STAGED) != in[LINK_HT_W_CRC]) {
        return LINK_HITEC_REF_STAGED;
    }
    if (!staged_xfer(p, op, &x)) {
        return LINK_HITEC_REF_RANGE;
    }
    if (puts_byte_out(op, &x) && !key) {
        return LINK_HITEC_REF_NO_KEY;
    }
    /* What the sampler does not run: an answer, a window past its
     * buffer. */
    if (p->rx_line == (uint8_t)LINK_HITEC_RX_PRECHARGE
        && op != LINK_HITEC_OP_LEVEL && x.window_us != 0u
        && (x.answer != 0u || x.window_us > HITEC_PRE_WINDOW_MAX_US)) {
        return LINK_HITEC_REF_RANGE;
    }
    p->dec = (uint8_t)(ST(p, LINK_HT_W_DECODE) & 0x3u);
    p->dec_arg = (uint8_t)(ST(p, LINK_HT_W_DECODE) >> 8);
    if (op == LINK_HITEC_OP_POWER) {
        return power(p, &x, now_ms);
    }
    /* The capture on the page is the last transaction's until this one
     * hands back its own: none while it runs. */
    p->has_cap = 0;
    op_begin(p, op);
    p->t0_ms = now_ms;
    p->wframe = writes_servo(p->family, op, &x) ? 1u : 0u;
    start(p, &x, puts_byte_out(op, &x) ? 1u : 0u);
    return LINK_HITEC_REF_NONE;
}

/**
 * @brief Route one command to its operation.
 *
 * @param p      The port.
 * @param op     The operation.
 * @param port   Programming port index.
 * @param in     The command frame's registers.
 * @param now_ms Current time in milliseconds.
 * @return LINK_HITEC_REF_NONE when the operation is taken; otherwise the
 *         refusal.
 */
static link_hitec_refusal_t dispatch(hitec_port_t *p, link_hitec_op_t op,
                                     uint8_t port, const uint16_t *in,
                                     uint32_t now_ms)
{
    if (op == LINK_HITEC_OP_ABORT) {
        abort_op(p);
        return LINK_HITEC_REF_NONE;
    }
    if (port >= p->hw.ports) {
        return LINK_HITEC_REF_PORT;
    }
    if (op == LINK_HITEC_OP_CLOSE) {
        return close_port(p, port);
    }
    if (op == LINK_HITEC_OP_OPEN) {
        return open_port(p, port, in, now_ms);
    }
    if (op == LINK_HITEC_OP_CLEAR) {
        if (p->state == (uint8_t)LINK_HITEC_FREE) {
            return LINK_HITEC_REF_NOT_OPEN;
        }
        if (port != p->port) {
            return LINK_HITEC_REF_PORT;
        }
        if (p->safe == 0u) {
            return LINK_HITEC_REF_UNSAFE;
        }
        p->stopped = 0;
        return LINK_HITEC_REF_NONE;
    }
    return transaction(p, op, port, in, now_ms);
}

/**
 * @brief Whether an operation is a step of a writer's sequence, which may
 *        carry LINK_HT_W_CMD_MORE: SEND, LISTEN, LEVEL or POWER.
 *
 * @param op The operation, bits 0-3 of the command.
 * @return true for the four transactions.
 */
static bool is_step(uint8_t op)
{
    return op == (uint8_t)LINK_HITEC_OP_SEND
           || op == (uint8_t)LINK_HITEC_OP_LISTEN
           || op == (uint8_t)LINK_HITEC_OP_LEVEL
           || op == (uint8_t)LINK_HITEC_OP_POWER;
}

/**
 * @brief Handle the command frame, registers 0 to 3.
 *
 * @param p      The port.
 * @param in     The command frame's four registers.
 * @param now_ms Current time in milliseconds.
 * @return 0 when the frame is acknowledged; LINK_NACK_BAD_VALUE for a bad frame
 *         or SEQ.
 */
static uint8_t command(hitec_port_t *p, const uint16_t *in, uint32_t now_ms)
{
    const uint16_t cmd = in[LINK_HT_W_CMD];
    const uint8_t op = (uint8_t)(cmd & 0xFu);
    const uint8_t port = (uint8_t)((cmd >> 8) & 0xFu);
    const bool more = (cmd & LINK_HT_W_CMD_MORE) != 0u;

    if ((cmd & (uint16_t)(0xF0F0u & ~LINK_HT_W_CMD_MORE)) != 0u
        || op == (uint8_t)LINK_HITEC_OP_NONE
        || op >= (uint8_t)LINK_HITEC_OP_COUNT
        || (more && !is_step(op))
        || (in[LINK_HT_W_KEY] != 0u
            && in[LINK_HT_W_KEY] != LINK_HITEC_WRITE_KEY)) {
        return LINK_NACK_BAD_VALUE;
    }
    /* The frame just taken, sent again: its answer was lost. */
    if (memcmp(in, p->last_cmd, sizeof(p->last_cmd)) == 0) {
        return 0u;
    }
    if (in[LINK_HT_W_SEQ] != (uint16_t)(p->seq + 1u)) {
        return LINK_NACK_BAD_VALUE;
    }
    p->seq = in[LINK_HT_W_SEQ];
    memcpy(p->last_cmd, in, sizeof(p->last_cmd));
    p->refusal = (uint8_t)dispatch(p, (link_hitec_op_t)op, port, in, now_ms);
    /* The write's next step is to come only after a step that started
     * with the bit: any other command ends that (LINK_HT_W_CMD_MORE). */
    p->more = more && p->refusal == (uint8_t)LINK_HITEC_REF_NONE ? 1u : 0u;
    return 0u;
}

uint8_t hitec_port_write(hitec_port_t *p, uint8_t off, uint8_t n,
                         const uint16_t *in, uint32_t now_ms)
{
    if (p == NULL || in == NULL || n == 0u
        || (unsigned)off + n > LINK_HT_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    if (off < LINK_HT_W_CMD_FRAME) {
        if (off != 0u || n != LINK_HT_W_CMD_FRAME) {
            return LINK_NACK_BAD_RANGE;
        }
        return command(p, in, now_ms);
    }
    if ((unsigned)off + n > LINK_HT_W_COUNT) {
        return LINK_NACK_READ_ONLY;
    }
    if ((unsigned)off <= LINK_HT_W_VIEW && (unsigned)off + n > LINK_HT_W_VIEW
        && in[LINK_HT_W_VIEW - off] >= (uint16_t)LINK_HITEC_VIEW_COUNT) {
        return LINK_NACK_BAD_VALUE;
    }
    if ((unsigned)off + n > LINK_HT_W_RX_MODE
        && in[LINK_HT_W_RX_MODE - off] >= (uint16_t)LINK_HITEC_RX_COUNT) {
        return LINK_NACK_BAD_VALUE;
    }
    for (unsigned i = 0; i < n; ++i) {
        const unsigned at = off + i;

        if (at == LINK_HT_W_VIEW) {
            p->view = (uint8_t)in[i];
        } else if (at == LINK_HT_W_RX_MODE) {
            p->rx_mode = (uint8_t)in[i];
        } else if (at - LINK_HT_W_LINE < LINK_HT_W_STAGED) {
            p->staged[at - LINK_HT_W_LINE] = in[i];
        }
    }
    return 0u;
}

/* --- the page ------------------------------------------------------------ */

/**
 * @brief Store a 32-bit value in two registers, low word first.
 *
 * @param at The first of two registers.
 * @param v  The value.
 */
static void put32(uint16_t *at, uint32_t v)
{
    at[0] = (uint16_t)(v & 0xFFFFu);
    at[1] = (uint16_t)(v >> 16);
}

/**
 * @brief The STATE register: the state, the port and the flags.
 *
 * @param p The port.
 * @return The STATE register value.
 */
static uint16_t state_reg(const hitec_port_t *p)
{
    uint16_t r = (uint16_t)(p->state | ((uint16_t)p->port << 12));
    supply_port_view_t v;

    r |= p->stopped != 0u ? LINK_HT_F_STOPPED : 0u;
    r |= supply_view(p, &v) ? LINK_HT_F_RAIL : 0u;
    r |= p->has_cap != 0u ? LINK_HT_F_CAPTURE : 0u;
    r |= p->keyed != 0u ? LINK_HT_F_KEY : 0u;
    r |= p->rx_mode == (uint8_t)LINK_HITEC_RX_PRECHARGE ? LINK_HT_F_PRECHARGE
                                                        : 0u;
    r |= p->more != 0u ? LINK_HT_F_MORE : 0u;
    return r;
}

/**
 * @brief Registers 16 to 31 of @p all, as VIEW selects.
 *
 * @param p   The port.
 * @param all The register image, LINK_HT_COUNT entries, filled in place.
 */
static void data_regs(const hitec_port_t *p, uint16_t *all)
{
    const hitec_capture_t *c = &p->cap;

    switch ((link_hitec_view_t)p->view) {
    case LINK_HITEC_VIEW_TIMES_LO:
    case LINK_HITEC_VIEW_TIMES_HI: {
        const unsigned first = p->view == 0u ? 0u : 8u;

        for (size_t k = 0; k < 8u; ++k) {
            if (p->has_cap != 0u && first + k < c->n) {
                put32(&all[LINK_HT_DATA + 2u * k], c->byte[first + k].at_ns);
            }
        }
        break;
    }
    case LINK_HITEC_VIEW_RUN:
    default: {
        if (p->has_cap != 0u) {
            uint16_t f = 0u;

            put32(&all[LINK_HT_I_DELAY_LO], p->reply.delay_ns);
            put32(&all[LINK_HT_I_T_LO], p->cap_us);
            for (unsigned k = 0; k < c->n && k < HITEC_RX_MAX; ++k) {
                f |= (uint16_t)((c->byte[k].framing != 0u ? 1u : 0u) << k);
            }
            all[LINK_HT_I_FRAMING] = f;
        }
        put32(&all[LINK_HT_I_LEAD_LO], p->info.lead_ns);
        put32(&all[LINK_HT_I_SPAN_LO], p->info.span_us);
        all[LINK_HT_I_BIT_NS] =
            p->has_pin != 0u ? (uint16_t)(p->info.bit_ns & 0xFFFFu) : 0u;
        all[LINK_HT_I_LINE] =
            (uint16_t)(LINK_HT_LINE_OF(p->family, p->drive)
                       | (p->rx_line == (uint8_t)LINK_HITEC_RX_PRECHARGE
                              ? LINK_HT_LINE_PRECHARGE
                              : 0u));
        all[LINK_HT_I_PIN] = p->has_pin != 0u ? p->pin : (uint16_t)0xFFFFu;
        put32(&all[LINK_HT_I_WINDOW_LO], p->xfer.window_us);
        break;
    }
    }
}

void hitec_port_read(const hitec_port_t *p, uint8_t off, uint8_t n,
                     uint16_t *out)
{
    uint16_t all[LINK_HT_COUNT];
    const hitec_capture_t *c;

    if (p == NULL || out == NULL || (unsigned)off + n > LINK_HT_COUNT) {
        return;
    }
    c = &p->cap;
    memset(all, 0, sizeof(all));
    all[LINK_HT_STATE] = state_reg(p);
    all[LINK_HT_SEQ] = p->seq;
    all[LINK_HT_OP] = (uint16_t)(((uint16_t)p->op << 12) | p->count);
    all[LINK_HT_RESULT] = (uint16_t)(((uint16_t)p->refusal << 8) | p->result);
    all[LINK_HT_VIEW] = p->view;
    if (p->has_cap != 0u) {
        all[LINK_HT_CAP] = (uint16_t)(c->n
                                      | (c->full != 0u ? LINK_HT_CAP_FULL : 0u)
                                      | (c->answered != 0u
                                             ? LINK_HT_CAP_ANSWERED : 0u)
                                      | (c->overflow != 0u
                                             ? LINK_HT_CAP_OVERFLOW : 0u)
                                      | (c->aborted != 0u
                                             ? LINK_HT_CAP_ABORTED : 0u)
                                      | ((uint16_t)p->dec << 12));
        all[LINK_HT_DEC] = (uint16_t)(p->rx | ((uint16_t)p->reply.id << 8));
        all[LINK_HT_VALUE] = p->reply.value;
        for (unsigned k = 0; k < c->n && k < HITEC_RX_MAX; ++k) {
            all[LINK_HT_BYTES + k / 2u] |=
                (uint16_t)((uint16_t)c->byte[k].value << (8u * (k & 1u)));
        }
    }
    data_regs(p, all);
    for (unsigned i = 0; i < n; ++i) {
        out[i] = all[off + i];
    }
}

/* --- the pass ------------------------------------------------------------ */

/**
 * @brief The pass of a BUSY port: take the capture, or cut a precharge line or
 *        a run past its time.
 *
 * @param p      The port.
 * @param now_ms Current time in milliseconds.
 */
static void step_busy(hitec_port_t *p, uint32_t now_ms)
{
    supply_port_view_t v;

    if (p->hw.driver.poll(p->hw.driver.ctx, &p->cap)) {
        finish(p);
        /* A frame that wrote: the servo keeps its supply for
         * HITEC_PORT_WRITE_TAIL_MS after it (hitec_port_writing()). */
        if (p->wframe != 0u) {
            p->wframe = 0;
            p->tail = 1;
            p->tail_t0 = now_ms;
        }
        return;
    }
    /* The sampler precharges the line every 1.1us: not into a servo
     * whose supply went off under it, asked off, or read off after it
     * read on (the PD mini's protection leaves OUTPUT asked on). */
    if (p->rx_line == (uint8_t)LINK_HITEC_RX_PRECHARGE && p->supply_on != 0u) {
        const bool gone = !supply_view(p, &v) || !v.asked_on;

        if (!gone && !v.off) {
            p->read_on = 1;
        }
        if (gone || (v.off && p->read_on != 0u)) {
            cut(p);
        }
    }
    if ((uint32_t)(now_ms - p->t0_ms) >= HITEC_PORT_BUSY_MS) {
        cut(p);
    }
}

/**
 * @brief The pass of a POWER port: wait for the supply to read off, its off
 *        time, then the ON and the listen.
 *
 * @param p      The port.
 * @param now_ms Current time in milliseconds.
 */
static void step_power(hitec_port_t *p, uint32_t now_ms)
{
    supply_port_view_t v;

    if (p->off_seen == 0u) {
        if (supply_view(p, &v) && v.off) {
            p->off_seen = 1;
            p->t1_ms = now_ms;
        } else if ((uint32_t)(now_ms - p->t0_ms) >= HITEC_PORT_SUPPLY_MS) {
            p->state = (uint8_t)LINK_HITEC_OPEN;
            p->result = (uint8_t)LINK_HITEC_RESULT_SUPPLY;
            return;
        }
        if (p->off_seen == 0u) {
            return;
        }
    }
    if ((uint32_t)(now_ms - p->t1_ms) < p->off_ms) {
        return;
    }
    if (!p->hw.supply_set(p->hw.ctx, true, p->mv, p->ma)) {
        p->state = (uint8_t)LINK_HITEC_OPEN;
        p->result = (uint8_t)LINK_HITEC_RESULT_SUPPLY;
        return;
    }
    {
        const hitec_xfer_t x = p->xfer;

        p->t0_ms = now_ms;
        start(p, &x, x.answer);
    }
}

void hitec_port_step(hitec_port_t *p, bool safe, bool stop, uint32_t now_ms)
{
    if (p == NULL) {
        return;
    }
    p->safe = safe ? 1u : 0u;
    if (stop || !safe) {
        on_stop(p);
    }
    if (p->tail != 0u
        && (uint32_t)(now_ms - p->tail_t0) >= HITEC_PORT_WRITE_TAIL_MS) {
        p->tail = 0;
    }
    switch ((link_hitec_state_t)p->state) {
    case LINK_HITEC_STOPPING:
        if ((uint32_t)(now_ms - p->t0_ms) >= HITEC_PORT_STOP_MS) {
            p->has_pin = 1;
            if (!p->hw.take(p->hw.ctx, p->port, p->pin, p->family,
                            p->drive, p->rx_line)) {
                go_free(p);
                p->result = (uint8_t)LINK_HITEC_RESULT_NO_LINE;
            } else {
                if (p->hw.run != NULL) {
                    p->hw.run(p->hw.ctx, &p->info);
                }
                p->state = (uint8_t)LINK_HITEC_OPEN;
                p->result = (uint8_t)LINK_HITEC_RESULT_DONE;
            }
        }
        break;
    case LINK_HITEC_BUSY:
        step_busy(p, now_ms);
        break;
    case LINK_HITEC_POWER_OFF:
        step_power(p, now_ms);
        break;
    case LINK_HITEC_FREE:
    case LINK_HITEC_OPEN:
    default:
        break;
    }
}

bool hitec_port_init(hitec_port_t *p, const hitec_port_hw_t *hw)
{
    if (p == NULL) {
        return false;
    }
    memset(p, 0, sizeof(*p));
    if (hw == NULL || hw->ports == 0u || hw->ports > LINK_PG_PORTS_MAX
        || hw->pin == NULL || hw->take == NULL || hw->give == NULL
        || hw->abort == NULL
        || (hw->supply == NULL) != (hw->supply_set == NULL)
        || hw->driver.start == NULL || hw->driver.poll == NULL
        || hw->driver.now_us == NULL) {
        return false;
    }
    p->hw = *hw;
    return true;
}

link_hitec_state_t hitec_port_state(const hitec_port_t *p)
{
    return p != NULL ? (link_hitec_state_t)p->state : LINK_HITEC_FREE;
}

uint8_t hitec_port_quiet_mask(const hitec_port_t *p)
{
    return (uint8_t)(p != NULL && p->state != (uint8_t)LINK_HITEC_FREE
                         ? 1u << p->port : 0u);
}

uint8_t hitec_port_pin_mask(const hitec_port_t *p)
{
    return (uint8_t)(p != NULL && p->has_pin != 0u ? 1u << p->port : 0u);
}

void hitec_port_bench_armed(hitec_port_t *p, bool armed)
{
    if (p != NULL) {
        p->armed = armed ? 1u : 0u;
    }
}

void hitec_port_other_mask(hitec_port_t *p, uint8_t mask)
{
    if (p != NULL) {
        p->other = mask;
    }
}

bool hitec_port_supply_hold(const hitec_port_t *p)
{
    return p != NULL && p->state != (uint8_t)LINK_HITEC_FREE;
}

bool hitec_port_writing(const hitec_port_t *p)
{
    return p != NULL
           && ((p->state == (uint8_t)LINK_HITEC_BUSY && p->wframe != 0u)
               || ((p->more != 0u || p->tail != 0u)
                   && p->state != (uint8_t)LINK_HITEC_FREE));
}

bool hitec_port_frame_out(const hitec_port_t *p, uint32_t now_ms)
{
    uint32_t us;

    if (p == NULL || p->state != (uint8_t)LINK_HITEC_BUSY) {
        return false;
    }
    /* An answer goes out when the first byte has come, which the port does
     * not see: the whole receive counts. */
    if (p->xfer.answer != 0u) {
        return true;
    }
    if (p->xfer.kind != (uint8_t)HITEC_XFER_SEND) {
        return false;
    }
    us = p->xfer.lead_us
         + hitec_bits_ns(p->xfer.family,
                         (uint32_t)p->xfer.n_tx * (10u + p->xfer.gap_bits)
                             + 1u)
               / 1000u;
    return (uint32_t)(now_ms - p->t0_ms) <= us / 1000u + 1u;
}
