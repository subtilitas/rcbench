/**
 * @file
 * @brief The panel's end of the CONSOLE page; see console_relay.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "console_relay.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "link_msg.h"

/** @brief The exchange asked for. */
enum {
    OP_NONE = 0,
    OP_POLL,  /**< what waits */
    OP_WIN,   /**< a window */
    OP_ACK,   /**< a window taken */
    OP_CHUNK, /**< a chunk of a line */
};

/** @brief The registers of a read that holds @p bytes of a window. */
#define WINDOW_REGS(bytes) ((uint8_t)(LINK_CN_DATA + ((bytes) + 1u) / 2u))

/**
 * @brief Bytes into the output ring.  Control task only.
 * @param r the relay
 * @param s the bytes
 * @param n how many
 */
static void out_put(console_relay_t *r, const char *s, size_t n)
{
    unsigned at = atomic_load(&r->out_in);

    /* What the serial port has not taken is never overwritten. */
    if (n > CONSOLE_RELAY_OUT_MAX - (size_t)(at - atomic_load(&r->out_out))) {
        r->out_lost += (unsigned)n;
        return;
    }
    for (size_t i = 0u; i < n; ++i) {
        r->out[at % CONSOLE_RELAY_OUT_MAX] = s[i];
        ++at;
    }
    atomic_store(&r->out_in, at);
}

/**
 * @brief The ring's free room.
 * @param r the relay
 * @return bytes
 */
static size_t out_room(const console_relay_t *r)
{
    return CONSOLE_RELAY_OUT_MAX
           - (size_t)(atomic_load(&r->out_in) - atomic_load(&r->out_out));
}

/**
 * @brief A line of the panel's own, if the ring has room for it.
 * @param r   the relay
 * @param fmt its printf format
 * @param ... the format's arguments
 */
__attribute__((format(printf, 2, 3)))
static void say(console_relay_t *r, const char *fmt, ...)
{
    char line[128];
    va_list ap;

    va_start(ap, fmt);
    const int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n > 0 && (size_t)n + 1u < sizeof(line) && (size_t)n + 1u <= out_room(r)) {
        if (r->out_cut) {
            /* The rest of a line cut for length follows this one. */
            out_put(r, "\n", 1u);
            r->out_cut = false;
        }
        out_put(r, line, (size_t)n);
    }
}

/**
 * @brief A byte of the IOmcu's output: a line reaches the ring whole.
 * @param r the relay
 * @param b the byte
 */
static void line_byte(console_relay_t *r, char b)
{
    r->partial[r->partial_len++] = b;
    if (b == '\n' || r->partial_len == CONSOLE_RELAY_PARTIAL_MAX) {
        out_put(r, r->partial, r->partial_len);
        r->out_cut = b != '\n';
        r->partial_len = 0u;
    }
}

/**
 * @brief The line not ended goes as it stands: the output it belongs to is gone.
 * @param r the relay
 */
static void line_end_here(console_relay_t *r)
{
    if (r->partial_len != 0u) {
        if ((size_t)r->partial_len + 1u <= out_room(r)) {
            out_put(r, r->partial, r->partial_len);
            out_put(r, "\n", 1u);
            r->out_cut = false;
        } else {
            r->out_lost += r->partial_len;
        }
        r->partial_len = 0u;
    }
}

/**
 * @brief The line at the queue's head goes, whatever became of it.
 * @param r the relay
 */
static void line_drop(console_relay_t *r)
{
    atomic_fetch_add(&r->lines_out, 1u);
    r->sent = 0u;
}

/**
 * @brief Whether a line waits.
 * @param r the relay
 * @return true then
 */
static bool line_waits(const console_relay_t *r)
{
    return atomic_load(&r->lines_in) != atomic_load(&r->lines_out);
}

void console_relay_init(console_relay_t *r)
{
    if (r != NULL) {
        memset(r, 0, sizeof(*r));
        atomic_init(&r->lines_in, 0u);
        atomic_init(&r->lines_out, 0u);
        atomic_init(&r->out_in, 0u);
        atomic_init(&r->out_out, 0u);
        atomic_init(&r->overlongs, 0u);
        atomic_init(&r->overflows, 0u);
        r->state = CONSOLE_RELAY_DOWN;
    }
}

void console_relay_link(console_relay_t *r, bool up, uint32_t now_ms)
{
    if (r->pass_known && (uint32_t)(now_ms - r->pass_ms) >= CONSOLE_RELAY_GAP_MS) {
        say(r, "console: no pass from t_ms=%lu to t_ms=%lu: the link was not "
               "followed\n",
            (unsigned long)r->pass_ms, (unsigned long)now_ms);
    }
    r->pass_known = true;
    r->pass_ms = now_ms;
    if (r->out_lost != 0u && out_room(r) >= 128u) {
        const unsigned lost = r->out_lost;

        r->out_lost = 0u;
        say(r, "err panel buffer full, %u bytes dropped\n", lost);
    }

    const unsigned over = atomic_load(&r->overlongs);

    if (over != r->overlongs_said) {
        r->overlongs_said = over;
        say(r, "err line longer than %u characters\n", CONSOLE_RELAY_LINE_MAX);
    }
    const unsigned full = atomic_load(&r->overflows);

    if (full != r->overflows_said) {
        r->overflows_said = full;
        say(r, "err line dropped: %u lines wait\n", CONSOLE_RELAY_LINES);
    }
    if (up != r->link_up) {
        r->link_up = up;
        r->op = OP_NONE;
        r->more = false;
        r->want = 0u;
        r->ack_owed = false;
        r->polled_once = false;
        say(r, "console: link %s t_ms=%lu\n", up ? "up" : "down",
            (unsigned long)now_ms);
        r->state = up ? CONSOLE_RELAY_PROBE : CONSOLE_RELAY_DOWN;
        if (!up && r->sent != 0u) {
            say(r, "err line lost: the link went down inside it\n");
            line_drop(r);
        }
    }
    /* A line with no link has nobody to go to. */
    while (!up && line_waits(r)) {
        say(r, "err no link: line dropped\n");
        line_drop(r);
    }
}

/**
 * @brief The next chunk of the line at the queue's head.
 * @param r  the relay
 * @param op takes the exchange
 */
static void next_chunk(console_relay_t *r, console_relay_op_t *op)
{
    const console_relay_line_t *l =
        &r->lines[atomic_load(&r->lines_out) % CONSOLE_RELAY_LINES];
    const unsigned left = (unsigned)l->len - r->sent;
    const unsigned len = left < LINK_CN_IN_BYTES ? left : LINK_CN_IN_BYTES;
    uint16_t head = (uint16_t)len;

    if (r->sent == 0u) {
        head |= LINK_CN_IN_START;
    }
    if (len == left) {
        head |= LINK_CN_IN_END;
    }
    memset(op->regs, 0, sizeof(op->regs));
    op->write = true;
    op->off = LINK_CN_W_IN;
    op->n = LINK_CN_IN_REGS;
    op->regs[0] = head;
    for (unsigned k = 0u; k < len; ++k) {
        op->regs[1u + k / 2u] = (uint16_t)(op->regs[1u + k / 2u]
                                           | ((uint16_t)(uint8_t)l->text[r->sent + k]
                                              << (8u * (k & 1u))));
    }
    r->op = OP_CHUNK;
    r->op_n = (uint8_t)len;
}

bool console_relay_next(console_relay_t *r, uint32_t now_ms, console_relay_op_t *op)
{
    if (r->state == CONSOLE_RELAY_DOWN) {
        return false;
    }
    if (r->state == CONSOLE_RELAY_NO_PAGE) {
        while (line_waits(r)) {
            say(r, "err the IOmcu has no console\n");
            line_drop(r);
        }
        if ((uint32_t)(now_ms - r->polled_ms) < CONSOLE_RELAY_PROBE_MS) {
            return false;
        }
        r->polled_once = false;
    }
    if (r->state == CONSOLE_RELAY_UP && r->ack_owed) {
        memset(op, 0, sizeof(*op));
        op->write = true;
        op->off = LINK_CN_W_ACK;
        op->n = 1u;
        op->regs[0] = r->ack;
        r->op = OP_ACK;
        return true;
    }
    if (r->state == CONSOLE_RELAY_UP && line_waits(r)) {
        next_chunk(r, op);
        return true;
    }
    /* Room for the window and the line it may end, and for one line of the
     * relay's own, or the IOmcu's ring holds it and counts what it drops. */
    if (r->state == CONSOLE_RELAY_UP && r->more
        && out_room(r) >= LINK_CN_DATA_BYTES + CONSOLE_RELAY_PARTIAL_MAX + 128u) {
        memset(op, 0, sizeof(*op));
        op->off = 0u;
        op->n = r->want != 0u ? WINDOW_REGS(r->want) : (uint8_t)LINK_CN_COUNT;
        r->op = OP_WIN;
        r->op_n = op->n;
        return true;
    }
    if (r->polled_once && (uint32_t)(now_ms - r->polled_ms) < CONSOLE_RELAY_POLL_MS) {
        return false;
    }
    r->polled_once = true;
    r->polled_ms = now_ms;
    memset(op, 0, sizeof(*op));
    op->off = 0u;
    op->n = CONSOLE_RELAY_POLL_REGS;
    r->op = OP_POLL;
    r->op_n = op->n;
    return true;
}

/**
 * @brief Where the output stands: a place this end did not expect is a restart.
 * @param r   the relay
 * @param seq the window's place
 * @param len its bytes
 * @return false for a window already taken, whose acknowledgement went missing
 */
static bool place_output(console_relay_t *r, uint16_t seq, uint16_t len)
{
    if (r->have_next && seq != r->next_seq) {
        if (len != 0u && (uint16_t)(seq + len) == r->next_seq) {
            return false;
        }
        line_end_here(r);
        say(r, "console: the IOmcu's output restarted at %u, expected %u\n",
            (unsigned)seq, (unsigned)r->next_seq);
    }
    r->have_next = true;
    r->next_seq = seq;
    return true;
}

/**
 * @brief A read of the page answered.
 * @param r    the relay
 * @param op   the exchange it answers
 * @param regs its registers
 */
static void read_answered(console_relay_t *r, uint8_t op, const uint16_t *regs)
{
    const uint16_t seq = regs[LINK_CN_OUT_SEQ];
    const uint16_t len = regs[LINK_CN_OUT_LEN] & LINK_CN_OUT_LEN_MASK;
    const bool more = (regs[LINK_CN_OUT_LEN] & LINK_CN_OUT_MORE) != 0u;

    r->state = CONSOLE_RELAY_UP;
    if (len > LINK_CN_DATA_BYTES) {
        return;
    }
    if (op == OP_POLL || WINDOW_REGS(len) > r->op_n) {
        /* The bytes come with a read of their size. */
        (void)place_output(r, seq, len);
        r->more = len != 0u || more;
        r->want = len;
        return;
    }
    const bool fresh = place_output(r, seq, len);

    if (len != 0u) {
        if (fresh) {
            for (unsigned k = 0u; k < len; ++k) {
                line_byte(r, (char)(regs[LINK_CN_DATA + k / 2u] >> (8u * (k & 1u))));
            }
            r->next_seq = (uint16_t)(seq + len);
        }
        r->ack = (uint16_t)(seq + len);
        r->ack_owed = true;
    }
    r->more = more;
    r->want = 0u;
}

/**
 * @brief A chunk of a line answered.
 * @param r      the relay
 * @param result how
 */
static void chunk_answered(console_relay_t *r, int result)
{
    if (result == 0) {
        const console_relay_line_t *l =
            &r->lines[atomic_load(&r->lines_out) % CONSOLE_RELAY_LINES];

        r->sent = (uint8_t)(r->sent + r->op_n);
        if (r->sent >= l->len) {
            line_drop(r);
        }
        /* A look for the answer in the next free pass. */
        r->polled_once = false;
        return;
    }
    if (result == (int)LINK_NACK_BUSY && r->sent == 0u) {
        /* The line before this one has not run yet: the same chunk next pass. */
        return;
    }
    if (result == CONSOLE_RELAY_NO_ANSWER) {
        say(r, "err no answer: line lost\n");
    } else if (result == (int)LINK_NACK_BAD_VALUE) {
        say(r, "err line lost: the IOmcu restarted inside it\n");
    } else {
        say(r, "err line refused: %d\n", result);
    }
    line_drop(r);
}

void console_relay_done(console_relay_t *r, int result, const uint16_t *regs)
{
    const uint8_t op = r->op;

    r->op = OP_NONE;
    if (op == OP_NONE) {
        return;
    }
    if (op == OP_CHUNK) {
        chunk_answered(r, result);
        return;
    }
    if (result == CONSOLE_RELAY_NO_ANSWER) {
        /* The window stands at the IOmcu: the next look reads it again. */
        return;
    }
    if (result != 0) {
        if (result == (int)LINK_NACK_BAD_PAGE && r->state != CONSOLE_RELAY_NO_PAGE) {
            r->state = CONSOLE_RELAY_NO_PAGE;
            r->more = false;
            r->ack_owed = false;
            say(r, "console: the IOmcu has no console. Take the update UPDATES "
                   "offers, or flash a build with IOMCU_CONSOLE=ON\n");
        }
        return;
    }
    if (op == OP_ACK) {
        r->ack_owed = false;
    } else if (regs != NULL) {
        read_answered(r, op, regs);
    }
}

void console_relay_key(console_relay_t *r, int ch)
{
    if (ch == '\r' || ch == '\n') {
        const unsigned in = atomic_load(&r->lines_in);

        if (r->overlong) {
            r->overlong = false;
            r->typed = 0u;
            atomic_fetch_add(&r->overlongs, 1u);
            return;
        }
        if (r->typed != 0u) {
            if (in - atomic_load(&r->lines_out) < CONSOLE_RELAY_LINES) {
                console_relay_line_t *l = &r->lines[in % CONSOLE_RELAY_LINES];

                memcpy(l->text, r->typing, r->typed);
                l->len = r->typed;
                atomic_store(&r->lines_in, in + 1u);
            } else {
                atomic_fetch_add(&r->overflows, 1u);
            }
        }
        r->typed = 0u;
        return;
    }
    if (ch == 0x08 || ch == 0x7F) {
        if (r->typed != 0u) {
            --r->typed;
        }
        return;
    }
    if (ch < 0x20 || ch > 0x7E) {
        return;
    }
    if (r->typed >= CONSOLE_RELAY_LINE_MAX) {
        r->overlong = true;
        return;
    }
    r->typing[r->typed++] = (char)ch;
}

size_t console_relay_take(console_relay_t *r, char *buf, size_t room)
{
    const unsigned from = atomic_load(&r->out_out);
    const size_t have = (size_t)(atomic_load(&r->out_in) - from);
    size_t n = 0u;

    while (n < have && n < room) {
        const char b = r->out[(from + n) % CONSOLE_RELAY_OUT_MAX];

        buf[n++] = b;
        if (b == '\n') {
            atomic_store(&r->out_out, from + (unsigned)n);
            return n;
        }
    }
    if (n == room) {
        atomic_store(&r->out_out, from + (unsigned)n);
        return n;
    }
    return 0u;
}
