/*
 * SPDX-License-Identifier: MIT
 */

#include "page_console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "hitec_wire.h"

#define TOKENS_MAX 40u

/* --- output ---------------------------------------------------------------- */

static void push(page_console_t *c, const char *s, size_t n)
{
    if (n > (size_t)(PAGE_CONSOLE_OUT_MAX - c->used)) {
        c->dropped += (uint32_t)n;
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        c->out[(c->head + c->used) % PAGE_CONSOLE_OUT_MAX] = s[i];
        c->used++;
    }
}

/* One line; the LF is added. */
static void say(page_console_t *c, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void say(page_console_t *c, const char *fmt, ...)
{
    char *buf = c->fmt;
    va_list ap;
    int n;

    va_start(ap, fmt);
    /* The analyser cannot follow va_start through this wrapper and reports the
     * list as uninitialised; it is initialised on the line above. */
    /* NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized) */
    n = vsnprintf(buf, sizeof(c->fmt) - 1u, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if ((size_t)n > sizeof(c->fmt) - 2u) {
        n = (int)(sizeof(c->fmt) - 2u);
    }
    buf[n] = '\n';
    push(c, buf, (size_t)n + 1u);
}

size_t page_console_take(page_console_t *c, char *buf, size_t room)
{
    size_t n = 0;
    size_t whole = 0;

    if (c == NULL || buf == NULL) {
        return 0;
    }
    if (c->dropped != 0u && c->used + 48u <= PAGE_CONSOLE_OUT_MAX) {
        const uint32_t lost = c->dropped;

        c->dropped = 0;
        say(c, "err output full, %lu bytes dropped", (unsigned long)lost);
    }
    /* The bytes of the whole lines that fit. */
    while (n < c->used && n < room) {
        if (c->out[(c->head + n) % PAGE_CONSOLE_OUT_MAX] == '\n') {
            whole = n + 1u;
        }
        n++;
    }
    for (size_t i = 0; i < whole; ++i) {
        buf[i] = c->out[(c->head + i) % PAGE_CONSOLE_OUT_MAX];
    }
    c->head = (uint16_t)((c->head + whole) % PAGE_CONSOLE_OUT_MAX);
    c->used = (uint16_t)(c->used - whole);
    return whole;
}

/* --- names ----------------------------------------------------------------- */

static const char *name_of(const char *const *names, unsigned count,
                           unsigned v)
{
    return v < count && names[v] != NULL ? names[v] : "?";
}

#define NAME(table, v) \
    name_of((table), sizeof(table) / sizeof((table)[0]), (v))

static const char *const k_nack[] = {
    "OK", "BAD_PAGE", "BAD_RANGE", "READ_ONLY", "BAD_VALUE", "NOT_ARMED",
    "BUSY",
};
static const char *const k_state[] = {
    "FREE", "STOPPING", "OPEN", "BUSY", "POWER_OFF",
};
static const char *const k_op[] = {
    "NONE", "OPEN", "CLOSE", "SEND", "LISTEN", "LEVEL", "ABORT", "POWER",
    "CLEAR",
};
static const char *const k_result[] = {
    "NONE", "RUNNING", "DONE", "SUPPLY", "START", "NO_LINE", "CUT",
};
static const char *const k_refusal[] = {
    "NONE", "BUSY", "PORT", "HELD", "ARMED", "UNSAFE", "NOT_OPEN", "NO_KEY",
    "STAGED", "STOPPED", "NO_PIN", "RANGE", "SUPPLY_RANGE", "NO_SUPPLY",
};
static const char *const k_dec[] = { "NONE", "C", "D", "LISTEN" };
static const char *const k_rx[] = {
    "OK", "ARG", "ABORTED", "OVERFLOW", "SILENT", "EARLY", "LATE", "NAK",
    "LENGTH", "FRAMING", "GAP", "ACK", "HEADER", "ADDRESS", "SUM",
};

_Static_assert(sizeof(k_state) / sizeof(k_state[0])
                   == (size_t)LINK_HITEC_POWER_OFF + 1u, "state names");
_Static_assert(sizeof(k_op) / sizeof(k_op[0]) == (size_t)LINK_HITEC_OP_COUNT,
               "op names");
_Static_assert(sizeof(k_refusal) / sizeof(k_refusal[0])
                   == (size_t)LINK_HITEC_REF_NO_SUPPLY + 1u, "refusal names");
_Static_assert(sizeof(k_rx) / sizeof(k_rx[0])
                   == (size_t)HITEC_RX_ERR_SUM + 1u, "decoder names");

/* --- requests -------------------------------------------------------------- */

/* A read or a write through the link's handlers; the reply. */
/* The reply is c->reply. */
static bool apply(page_console_t *c, uint8_t op, uint8_t page, uint8_t off,
                  uint8_t n, const uint16_t *in)
{
    link_msg_t *req = &c->req;

    memset(req, 0, sizeof(*req));
    req->op = op;
    req->page = page;
    req->offset = off;
    req->count = n;
    if (in != NULL) {
        memcpy(req->regs, in, (size_t)n * sizeof(uint16_t));
    }
    return link_dev_apply(c->dev, req, &c->reply);
}

static bool read_regs(page_console_t *c, uint8_t page, uint8_t off,
                      uint8_t n, uint16_t *out)
{
    if (!apply(c, LINK_OP_READ, page, off, n, NULL)
        || c->reply.op != LINK_OP_DATA) {
        return false;
    }
    memcpy(out, c->reply.regs, (size_t)n * sizeof(uint16_t));
    return true;
}

/* A write; false, with the refusal said, when it is not acknowledged. */
static bool write_regs(page_console_t *c, uint8_t page, uint8_t off,
                       uint8_t n, const uint16_t *in)
{
    if (!apply(c, LINK_OP_WRITE, page, off, n, in)) {
        say(c, "err no device");
        return false;
    }
    if (c->reply.op != LINK_OP_ACK) {
        say(c, "nack page=0x%02X off=%u: %s (%u)", page, off,
            NAME(k_nack, c->reply.regs[0]), (unsigned)c->reply.regs[0]);
        return false;
    }
    return true;
}

/* --- parsing --------------------------------------------------------------- */

static bool parse_u32(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    unsigned base = 10;
    unsigned digits = 0;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    for (; *s != '\0'; ++s) {
        unsigned d;

        if (*s >= '0' && *s <= '9') {
            d = (unsigned)(*s - '0');
        } else if (base == 16 && *s >= 'a' && *s <= 'f') {
            d = (unsigned)(*s - 'a') + 10u;
        } else if (base == 16 && *s >= 'A' && *s <= 'F') {
            d = (unsigned)(*s - 'A') + 10u;
        } else {
            return false;
        }
        if (v > (0xFFFFFFFFu - d) / base) {
            return false;
        }
        v = v * base + d;
        digits++;
    }
    *out = v;
    return digits > 0u;
}

static bool parse_max(const char *s, uint32_t max, uint32_t *out)
{
    return parse_u32(s, out) && *out <= max;
}

/* "key=value": the value when @p tok starts with @p key and '='. */
static const char *value_of(const char *tok, const char *key)
{
    const size_t n = strlen(key);

    return strncmp(tok, key, n) == 0 && tok[n] == '=' ? tok + n + 1u : NULL;
}

static unsigned split(char *line, char **tok)
{
    unsigned n = 0;
    char *p = line;

    while (*p != '\0' && n < TOKENS_MAX) {
        while (*p == ' ' || *p == '\t') {
            *p++ = '\0';
        }
        if (*p == '\0') {
            break;
        }
        tok[n++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
    }
    return n;
}

/* --- raw pages ------------------------------------------------------------- */

/* The reply's registers: up to 32 of 7 characters each, so the line is
 * made in two halves of 16 when it is long. */
static void say_regs(page_console_t *c, const char *what, uint8_t page,
                     uint8_t off, uint8_t n, const uint16_t *regs)
{
    char *buf = c->fmt;
    const size_t size = sizeof(c->fmt);
    int at = snprintf(buf, size, "%s page=0x%02X off=%u n=%u:", what, page,
                      off, n);

    for (unsigned i = 0; i < n && at > 0; ++i) {
        if ((size_t)at + 8u >= size) {
            push(c, buf, (size_t)at);
            at = 0;
        }
        at += snprintf(buf + at, size - (size_t)at, " 0x%04X",
                       (unsigned)regs[i]);
    }
    if (at > 0) {
        buf[at] = '\n';
        push(c, buf, (size_t)at + 1u);
    }
}

static void cmd_pages(page_console_t *c)
{
    for (uint8_t i = 0; i < c->dev->page_count; ++i) {
        const link_page_t *p = &c->dev->pages[i];

        say(c, "page 0x%02X regs %u %s", p->page, p->count,
            p->write != NULL ? "rw" : "ro");
    }
}

static void cmd_read(page_console_t *c, char **tok, unsigned n)
{
    uint32_t page, off, cnt = 1;
    const link_msg_t *reply = &c->reply;

    if (n < 3u || n > 4u || !parse_max(tok[1], 0xFFu, &page)
        || !parse_max(tok[2], 0xFFu, &off)
        || (n == 4u && !parse_max(tok[3], LINK_MAX_REGS, &cnt)) || cnt == 0u) {
        say(c, "err usage: r <page> <off> [n 1..%u]", (unsigned)LINK_MAX_REGS);
        return;
    }
    if (!apply(c, LINK_OP_READ, (uint8_t)page, (uint8_t)off, (uint8_t)cnt,
               NULL)) {
        say(c, "err no device");
        return;
    }
    if (reply->op != LINK_OP_DATA) {
        say(c, "nack page=0x%02X off=%u: %s (%u)", (unsigned)page,
            (unsigned)off, NAME(k_nack, reply->regs[0]),
            (unsigned)reply->regs[0]);
        return;
    }
    say_regs(c, "data", (uint8_t)page, (uint8_t)off, (uint8_t)cnt,
             reply->regs);
}

static void cmd_write(page_console_t *c, char **tok, unsigned n)
{
    uint32_t page, off;
    uint16_t *v = c->regs;
    const link_msg_t *reply = &c->reply;
    const unsigned cnt = n >= 3u ? n - 3u : 0u;

    if (n < 4u || cnt > LINK_MAX_REGS || !parse_max(tok[1], 0xFFu, &page)
        || !parse_max(tok[2], 0xFFu, &off)) {
        say(c, "err usage: w <page> <off> <v> [v ...], 1 to %u values",
            (unsigned)LINK_MAX_REGS);
        return;
    }
    for (unsigned i = 0; i < cnt; ++i) {
        uint32_t x;

        if (!parse_max(tok[3u + i], 0xFFFFu, &x)) {
            say(c, "err not a 16-bit value: %s", tok[3u + i]);
            return;
        }
        v[i] = (uint16_t)x;
    }
    if (!apply(c, LINK_OP_WRITE, (uint8_t)page, (uint8_t)off, (uint8_t)cnt,
               v)) {
        say(c, "err no device");
        return;
    }
    if (reply->op != LINK_OP_ACK) {
        say(c, "nack page=0x%02X off=%u: %s (%u)", (unsigned)page,
            (unsigned)off, NAME(k_nack, reply->regs[0]),
            (unsigned)reply->regs[0]);
        return;
    }
    say_regs(c, "ack", (uint8_t)page, (uint8_t)off, (uint8_t)cnt,
             reply->regs);
}

/* --- the HITEC page -------------------------------------------------------- */

#define HST(c, reg) ((c)->staged[(reg) - LINK_HT_W_LINE])

static void hst32(page_console_t *c, unsigned lo, uint32_t v)
{
    HST(c, lo) = (uint16_t)(v & 0xFFFFu);
    HST(c, lo + 1u) = (uint16_t)(v >> 16);
}

static uint32_t u32_of(const uint16_t *r, unsigned lo)
{
    return (uint32_t)r[lo] | ((uint32_t)r[lo + 1u] << 16);
}

static void say_flags(char *buf, size_t size, uint16_t st)
{
    static const struct { uint16_t bit; const char *name; } k[] = {
        { LINK_HT_F_STOPPED, "STOPPED" }, { LINK_HT_F_RAIL, "RAIL" },
        { LINK_HT_F_CAPTURE, "CAPTURE" }, { LINK_HT_F_KEY, "KEY" },
    };
    size_t at = 0;

    buf[0] = '\0';
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        if ((st & k[i].bit) != 0u && at + 10u < size) {
            at += (size_t)snprintf(buf + at, size - at, "%s%s",
                                   at > 0u ? "," : "", k[i].name);
        }
    }
    if (at == 0u) {
        (void)snprintf(buf, size, "-");
    }
}

/* What the page says now: the state line, and the capture with its bytes
 * when it holds one. */
static void hitec_report(page_console_t *c, const char *what)
{
    uint16_t *r = c->regs;
    uint16_t *lo = c->data[0];
    uint16_t *hi = c->data[1];
    uint16_t *run = c->data[2];
    uint16_t view;
    char flags[32];

    if (!read_regs(c, LINK_PAGE_HITEC, 0, LINK_HT_COUNT, r)) {
        say(c, "err the HITEC page does not answer");
        return;
    }
    view = r[LINK_HT_VIEW];
    say_flags(flags, sizeof(flags), r[LINK_HT_STATE]);
    say(c, "hitec %s state=%s port=%u flags=%s seq=%u op=%s count=%u "
        "result=%s refusal=%s", what,
        NAME(k_state, LINK_HT_STATE_OF(r[LINK_HT_STATE])),
        (unsigned)LINK_HT_PORT_OF(r[LINK_HT_STATE]), flags, r[LINK_HT_SEQ],
        NAME(k_op, LINK_HT_OP_OF(r[LINK_HT_OP])), r[LINK_HT_OP] & 0x0FFFu,
        NAME(k_result, r[LINK_HT_RESULT] & 0xFFu),
        NAME(k_refusal, r[LINK_HT_RESULT] >> 8));
    {
        const uint16_t v0 = LINK_HITEC_VIEW_TIMES_LO;
        const uint16_t v1 = LINK_HITEC_VIEW_TIMES_HI;
        const uint16_t v2 = LINK_HITEC_VIEW_RUN;

        if (!write_regs(c, LINK_PAGE_HITEC, LINK_HT_W_VIEW, 1, &v0)
            || !read_regs(c, LINK_PAGE_HITEC, LINK_HT_DATA, 16, lo)
            || !write_regs(c, LINK_PAGE_HITEC, LINK_HT_W_VIEW, 1, &v1)
            || !read_regs(c, LINK_PAGE_HITEC, LINK_HT_DATA, 16, hi)
            || !write_regs(c, LINK_PAGE_HITEC, LINK_HT_W_VIEW, 1, &v2)
            || !read_regs(c, LINK_PAGE_HITEC, LINK_HT_DATA, 16, run)
            || !write_regs(c, LINK_PAGE_HITEC, LINK_HT_W_VIEW, 1, &view)) {
            return;
        }
    }
#define RUN_AT(reg) ((reg) - LINK_HT_DATA)
    say(c, "hitec line pin=%u line=0x%02X bit_ns=%u lead_ns=%lu span_us=%lu",
        run[RUN_AT(LINK_HT_I_PIN)], run[RUN_AT(LINK_HT_I_LINE)],
        run[RUN_AT(LINK_HT_I_BIT_NS)],
        (unsigned long)u32_of(run, RUN_AT(LINK_HT_I_LEAD_LO)),
        (unsigned long)u32_of(run, RUN_AT(LINK_HT_I_SPAN_LO)));
    if ((r[LINK_HT_STATE] & LINK_HT_F_CAPTURE) == 0u) {
        return;
    }
    {
        const uint16_t cap = r[LINK_HT_CAP];
        const unsigned n = cap & 0x1Fu;
        const uint16_t framing = run[RUN_AT(LINK_HT_I_FRAMING)];

        say(c, "hitec cap n=%u full=%u answered=%u overflow=%u aborted=%u "
            "dec=%s rx=%s id=0x%02X value=0x%04X delay_ns=%lu t_us=%lu "
            "window_us=%lu", n, (unsigned)((cap & LINK_HT_CAP_FULL) != 0u),
            (unsigned)((cap & LINK_HT_CAP_ANSWERED) != 0u),
            (unsigned)((cap & LINK_HT_CAP_OVERFLOW) != 0u),
            (unsigned)((cap & LINK_HT_CAP_ABORTED) != 0u),
            NAME(k_dec, (cap >> 12) & 3u),
            NAME(k_rx, r[LINK_HT_DEC] & 0xFFu), r[LINK_HT_DEC] >> 8,
            r[LINK_HT_VALUE],
            (unsigned long)u32_of(run, RUN_AT(LINK_HT_I_DELAY_LO)),
            (unsigned long)u32_of(run, RUN_AT(LINK_HT_I_T_LO)),
            (unsigned long)u32_of(run, RUN_AT(LINK_HT_I_WINDOW_LO)));
        for (unsigned k = 0; k < n && k < HITEC_RX_MAX; ++k) {
            const uint16_t *t = k < 8u ? lo : hi;
            const uint8_t b =
                (uint8_t)(r[LINK_HT_BYTES + k / 2u] >> (8u * (k & 1u)));

            say(c, "hitec byte %u 0x%02X at_ns=%lu framing=%u", k, b,
                (unsigned long)u32_of(t, 2u * (k % 8u)),
                (framing >> k) & 1u);
        }
    }
#undef RUN_AT
}

/* Stage, name the CRC and the next SEQ, and send the command. */
static void hitec_command(page_console_t *c, link_hitec_op_t op,
                          uint32_t port, bool key)
{
    uint16_t seq;
    uint16_t in[LINK_HT_W_CMD_FRAME];
    uint16_t r[LINK_HT_RESULT + 1u];
    const bool staged = op != LINK_HITEC_OP_ABORT
                        && op != LINK_HITEC_OP_CLOSE
                        && op != LINK_HITEC_OP_CLEAR;

    if (staged
        && !write_regs(c, LINK_PAGE_HITEC, LINK_HT_W_LINE, LINK_HT_W_STAGED,
                       c->staged)) {
        return;
    }
    if (!read_regs(c, LINK_PAGE_HITEC, LINK_HT_SEQ, 1, &seq)) {
        say(c, "err the HITEC page does not answer");
        return;
    }
    in[LINK_HT_W_CMD] = LINK_HT_W_CMD_OF(op, port);
    in[LINK_HT_W_SEQ] = (uint16_t)(seq + 1u);
    in[LINK_HT_W_KEY] = key ? (uint16_t)LINK_HITEC_WRITE_KEY : 0u;
    in[LINK_HT_W_CRC] = staged ? link_kst_crc(c->staged, LINK_HT_W_STAGED)
                               : 0u;
    if (!write_regs(c, LINK_PAGE_HITEC, LINK_HT_W_CMD, LINK_HT_W_CMD_FRAME,
                    in)
        || !read_regs(c, LINK_PAGE_HITEC, 0, LINK_HT_RESULT + 1u, r)) {
        return;
    }
    if ((r[LINK_HT_RESULT] >> 8) != 0u) {
        say(c, "hitec %s port=%lu refused %s", NAME(k_op, op),
            (unsigned long)port, NAME(k_refusal, r[LINK_HT_RESULT] >> 8));
        return;
    }
    switch ((link_hitec_state_t)LINK_HT_STATE_OF(r[LINK_HT_STATE])) {
    case LINK_HITEC_STOPPING:
    case LINK_HITEC_BUSY:
    case LINK_HITEC_POWER_OFF:
        say(c, "hitec %s port=%lu started", NAME(k_op, op),
            (unsigned long)port);
        c->watch = 1;
        break;
    case LINK_HITEC_FREE:
    case LINK_HITEC_OPEN:
    default:
        /* Ended in its own frame, and with it whatever was watched. */
        c->watch = 0;
        hitec_report(c, "done");
        break;
    }
}

/* dec=none | c:<cmd> | d:<addr> | l:<byte> */
static bool parse_dec(const char *v, uint16_t *out)
{
    uint32_t arg;
    unsigned kind;

    if (strcmp(v, "none") == 0) {
        *out = 0u;
        return true;
    }
    if (v[0] == 'c' && v[1] == ':') {
        kind = LINK_HITEC_DEC_C;
    } else if (v[0] == 'd' && v[1] == ':') {
        kind = LINK_HITEC_DEC_D;
    } else if (v[0] == 'l' && v[1] == ':') {
        kind = LINK_HITEC_DEC_LISTEN;
    } else {
        return false;
    }
    if (!parse_max(v + 2, 0xFFu, &arg)) {
        return false;
    }
    *out = (uint16_t)(kind | (arg << 8));
    return true;
}

/* The options of send, listen and power, and the bytes of a send. */
typedef struct {
    uint32_t lead, gap, window, rx, answer, mv, ma, off;
    bool     has_answer;
    uint16_t dec;
    uint8_t  tx[HITEC_TX_MAX];
    unsigned n_tx;
} hitec_args_t;

static bool parse_args(page_console_t *c, char **tok, unsigned n,
                       hitec_args_t *a)
{
    const struct {
        const char *key;
        uint32_t    max;
        uint32_t   *to;
    } k_keys[] = {
        { "lead", HITEC_TIME_MAX_US, &a->lead },
        { "gap", 0xFFu, &a->gap },
        { "window", 0xFFFFFFFFu, &a->window },
        { "rx", 0xFFu, &a->rx },
        { "answer", 0xFFu, &a->answer },
        { "mv", 0xFFFFu, &a->mv },
        { "ma", 0xFFFFu, &a->ma },
        { "off", 0xFFFFu, &a->off },
    };

    memset(a, 0, sizeof(*a));
    for (unsigned i = 0; i < n; ++i) {
        const char *v = value_of(tok[i], "dec");
        bool ok = false;
        bool named = v != NULL;

        if (named) {
            ok = parse_dec(v, &a->dec);
        }
        for (unsigned j = 0; !named && j < sizeof(k_keys) / sizeof(k_keys[0]);
             ++j) {
            v = value_of(tok[i], k_keys[j].key);
            if (v != NULL) {
                named = true;
                ok = parse_max(v, k_keys[j].max, k_keys[j].to);
                a->has_answer = a->has_answer || k_keys[j].to == &a->answer;
            }
        }
        if (!named) {
            uint32_t b;

            ok = a->n_tx < HITEC_TX_MAX && parse_max(tok[i], 0xFFu, &b);
            if (ok) {
                a->tx[a->n_tx++] = (uint8_t)b;
            }
        }
        if (!ok) {
            say(c, "err bad argument: %s", tok[i]);
            return false;
        }
    }
    return true;
}

/* The staged registers of a transaction; LINE and the supply's are 0. */
static void stage_xfer(page_console_t *c, const hitec_xfer_t *x,
                       uint16_t dec)
{
    memset(c->staged, 0, sizeof(c->staged));
    HST(c, LINK_HT_W_NTX) = (uint16_t)(x->n_tx | (x->gap_bits << 8));
    for (size_t k = 0; k < HITEC_TX_MAX / 2u; ++k) {
        HST(c, LINK_HT_W_TX + k) =
            (uint16_t)(x->tx[2u * k] | (x->tx[2u * k + 1u] << 8));
    }
    hst32(c, LINK_HT_W_LEAD_LO, x->lead_us);
    hst32(c, LINK_HT_W_WINDOW_LO, x->window_us);
    HST(c, LINK_HT_W_RX) = (uint16_t)(x->max_rx | (x->answer << 8));
    HST(c, LINK_HT_W_ANSWER) = (uint16_t)(x->answer_byte | (x->level << 8));
    hst32(c, LINK_HT_W_LEVEL_LO, x->level_us);
    HST(c, LINK_HT_W_DECODE) = dec;
}

static bool parse_port(page_console_t *c, const char *tok, uint32_t *port)
{
    if (!parse_max(tok, 15u, port)) {
        say(c, "err not a port: %s", tok);
        return false;
    }
    return true;
}

static void hitec_open(page_console_t *c, uint32_t port, char **tok,
                       unsigned n)
{
    uint8_t family, drive = HITEC_DRIVE_OPEN_DRAIN;

    if (n < 1u || n > 2u || (strcmp(tok[0], "c") != 0
                             && strcmp(tok[0], "d") != 0)
        || (n == 2u && strcmp(tok[1], "od") != 0
            && strcmp(tok[1], "pp") != 0)) {
        say(c, "err usage: hitec open <port> c|d [od|pp]");
        return;
    }
    family = tok[0][0] == 'c' ? (uint8_t)HITEC_CLASSIC : (uint8_t)HITEC_D;
    if (n == 2u && tok[1][0] == 'p') {
        drive = HITEC_DRIVE_PUSH_PULL;
    }
    memset(c->staged, 0, sizeof(c->staged));
    HST(c, LINK_HT_W_LINE) = LINK_HT_LINE_OF(family, drive);
    hitec_command(c, LINK_HITEC_OP_OPEN, port, false);
}

/* The family of the open line, from the page; false when none is open. */
static bool open_family(page_console_t *c, uint8_t *family)
{
    uint16_t *r = c->data[2];
    const uint16_t v2 = LINK_HITEC_VIEW_RUN;
    uint16_t view;

    if (!read_regs(c, LINK_PAGE_HITEC, LINK_HT_VIEW, 1, &view)
        || !write_regs(c, LINK_PAGE_HITEC, LINK_HT_W_VIEW, 1, &v2)
        || !read_regs(c, LINK_PAGE_HITEC, LINK_HT_DATA, 16, r)
        || !write_regs(c, LINK_PAGE_HITEC, LINK_HT_W_VIEW, 1, &view)) {
        return false;
    }
    *family = (uint8_t)(r[LINK_HT_I_LINE - LINK_HT_DATA] & 1u);
    return true;
}

static void hitec_send(page_console_t *c, uint32_t port, char **tok,
                       unsigned n)
{
    hitec_args_t a;
    hitec_xfer_t x;
    uint8_t family;

    if (!parse_args(c, tok, n, &a)) {
        return;
    }
    if (a.n_tx == 0u || a.has_answer || a.mv != 0u || a.ma != 0u
        || a.off != 0u) {
        say(c, "err usage: hitec send <port> [lead=] [gap=] [window=] [rx=] "
            "[dec=] <byte>...");
        return;
    }
    if (!open_family(c, &family)) {
        return;
    }
    memset(&x, 0, sizeof(x));
    x.kind = HITEC_XFER_SEND;
    x.family = family;
    x.n_tx = (uint8_t)a.n_tx;
    x.gap_bits = (uint8_t)a.gap;
    memcpy(x.tx, a.tx, sizeof(x.tx));
    x.lead_us = a.lead;
    x.window_us = a.window;
    x.max_rx = (uint8_t)a.rx;
    stage_xfer(c, &x, a.dec);
    hitec_command(c, LINK_HITEC_OP_SEND, port, true);
}

static void hitec_listen(page_console_t *c, uint32_t port, char **tok,
                         unsigned n, bool power)
{
    hitec_args_t a;
    hitec_xfer_t x;

    if (!parse_args(c, tok, n, &a)) {
        return;
    }
    if (a.n_tx != 0u || a.lead != 0u || a.gap != 0u
        || (!power && (a.mv != 0u || a.ma != 0u || a.off != 0u))) {
        say(c, power ? "err usage: hitec power <port> mv= ma= off= window= "
                       "rx= [answer=] [dec=]"
                     : "err usage: hitec listen <port> window= rx= "
                       "[answer=] [dec=]");
        return;
    }
    memset(&x, 0, sizeof(x));
    x.window_us = a.window;
    x.max_rx = (uint8_t)a.rx;
    x.answer = a.has_answer ? 1u : 0u;
    x.answer_byte = (uint8_t)a.answer;
    stage_xfer(c, &x, a.dec);
    if (power) {
        HST(c, LINK_HT_W_SUPPLY_MV) = (uint16_t)a.mv;
        HST(c, LINK_HT_W_SUPPLY_MA) = (uint16_t)a.ma;
        HST(c, LINK_HT_W_OFF_MS) = (uint16_t)a.off;
    }
    hitec_command(c, power ? LINK_HITEC_OP_POWER : LINK_HITEC_OP_LISTEN,
                  port, a.has_answer);
}

static void hitec_level(page_console_t *c, uint32_t port, char **tok,
                        unsigned n)
{
    uint32_t level, us;
    hitec_xfer_t x;

    if (n != 2u || !parse_max(tok[0], 1u, &level)
        || !parse_max(tok[1], HITEC_TIME_MAX_US, &us)) {
        say(c, "err usage: hitec level <port> 0|1 <us>");
        return;
    }
    memset(&x, 0, sizeof(x));
    x.level = (uint8_t)level;
    x.level_us = us;
    stage_xfer(c, &x, 0u);
    hitec_command(c, LINK_HITEC_OP_LEVEL, port, false);
}

/* Whether the open line is of @p family; says why not.  A frame of one
 * family on a line of the other goes out at the wrong rate and polarity. */
static bool family_is(page_console_t *c, uint8_t family)
{
    uint8_t open;

    if (!open_family(c, &open)) {
        return false;
    }
    if (open != family) {
        say(c, "err the line is %s; this command needs a %s line",
            open == (uint8_t)HITEC_CLASSIC ? "classic" : "D series",
            family == (uint8_t)HITEC_CLASSIC ? "classic" : "D series");
        return false;
    }
    return true;
}

static void hitec_classic(page_console_t *c, uint32_t port, char **tok,
                          unsigned n)
{
    uint32_t v[3];
    hitec_xfer_t x;
    hitec_c_expect_t e;

    if (n != 3u || !parse_max(tok[0], 0xFFu, &v[0])
        || !parse_max(tok[1], 0xFFu, &v[1])
        || !parse_max(tok[2], 0xFFu, &v[2])) {
        say(c, "err usage: hitec c <port> <cmd> <p1> <p2>");
        return;
    }
    if (!family_is(c, HITEC_CLASSIC)) {
        return;
    }
    hitec_c_xfer((uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2], &x);
    stage_xfer(c, &x, hitec_c_expect_of((uint8_t)v[0], &e)
                          ? (uint16_t)(LINK_HITEC_DEC_C | (v[0] << 8)) : 0u);
    hitec_command(c, LINK_HITEC_OP_SEND, port, true);
}

static void hitec_d(page_console_t *c, uint32_t port, char **tok, unsigned n)
{
    uint32_t id = 0, addr = 0, value = 0;
    hitec_xfer_t x;

    if (n >= 1u && (strcmp(tok[0], "read") == 0
                    || strcmp(tok[0], "write") == 0)
        && !family_is(c, HITEC_D)) {
        return;
    }
    if (n == 3u && strcmp(tok[0], "read") == 0
        && parse_max(tok[1], 0xFFu, &id) && parse_max(tok[2], 0xFFu, &addr)) {
        hitec_d_read_xfer((uint8_t)id, (uint8_t)addr, &x);
        stage_xfer(c, &x, (uint16_t)(LINK_HITEC_DEC_D | (addr << 8)));
    } else if (n == 4u && strcmp(tok[0], "write") == 0
               && parse_max(tok[1], 0xFFu, &id)
               && parse_max(tok[2], 0xFFu, &addr)
               && parse_max(tok[3], 0xFFFFu, &value)) {
        hitec_d_write_xfer((uint8_t)id, (uint8_t)addr, (uint16_t)value, &x);
        stage_xfer(c, &x, 0u);
    } else {
        say(c, "err usage: hitec d <port> read <id> <addr> | "
            "hitec d <port> write <id> <addr> <value>");
        return;
    }
    hitec_command(c, LINK_HITEC_OP_SEND, port, true);
}

static void cmd_hitec(page_console_t *c, char **tok, unsigned n)
{
    uint32_t port;
    const char *sub = n >= 2u ? tok[1] : "state";

    if (strcmp(sub, "state") == 0) {
        hitec_report(c, "now");
        return;
    }
    if (n < 3u) {
        say(c, "err usage: hitec <sub> <port> ...; help lists them");
        return;
    }
    if (!parse_port(c, tok[2], &port)) {
        return;
    }
    tok += 3;
    n -= 3u;
    if (strcmp(sub, "open") == 0) {
        hitec_open(c, port, tok, n);
    } else if (strcmp(sub, "close") == 0 && n == 0u) {
        hitec_command(c, LINK_HITEC_OP_CLOSE, port, false);
    } else if (strcmp(sub, "abort") == 0 && n == 0u) {
        hitec_command(c, LINK_HITEC_OP_ABORT, port, false);
    } else if (strcmp(sub, "clear") == 0 && n == 0u) {
        hitec_command(c, LINK_HITEC_OP_CLEAR, port, false);
    } else if (strcmp(sub, "send") == 0) {
        hitec_send(c, port, tok, n);
    } else if (strcmp(sub, "listen") == 0) {
        hitec_listen(c, port, tok, n, false);
    } else if (strcmp(sub, "power") == 0) {
        hitec_listen(c, port, tok, n, true);
    } else if (strcmp(sub, "level") == 0) {
        hitec_level(c, port, tok, n);
    } else if (strcmp(sub, "c") == 0) {
        hitec_classic(c, port, tok, n);
    } else if (strcmp(sub, "d") == 0) {
        hitec_d(c, port, tok, n);
    } else {
        say(c, "err unknown: hitec %s", sub);
    }
}

static void cmd_help(page_console_t *c)
{
    say(c, "help | pages | r <page> <off> [n] | w <page> <off> <v>...");
    say(c, "hitec [state] | hitec open <port> c|d [od|pp] | "
        "hitec close|abort|clear <port>");
    say(c, "hitec send <port> [lead=] [gap=] [window=] [rx=] [dec=] "
        "<byte>...");
    say(c, "hitec listen <port> window= rx= [answer=] [dec=] | "
        "hitec level <port> 0|1 <us>");
    say(c, "hitec power <port> mv= ma= off= window= rx= [answer=] [dec=]");
    say(c, "hitec c <port> <cmd> <p1> <p2> | hitec d <port> read <id> "
        "<addr> | hitec d <port> write <id> <addr> <value>");
    say(c, "dec=none|c:<cmd>|d:<addr>|l:<byte>; numbers decimal or 0x hex");
}

static void run_line(page_console_t *c)
{
    char *tok[TOKENS_MAX];
    const unsigned n = split(c->line, tok);

    if (n == 0u) {
        return;
    }
    if (strcmp(tok[0], "help") == 0) {
        cmd_help(c);
    } else if (strcmp(tok[0], "pages") == 0) {
        cmd_pages(c);
    } else if (strcmp(tok[0], "r") == 0) {
        cmd_read(c, tok, n);
    } else if (strcmp(tok[0], "w") == 0) {
        cmd_write(c, tok, n);
    } else if (strcmp(tok[0], "hitec") == 0) {
        cmd_hitec(c, tok, n);
    } else {
        say(c, "err unknown command: %s; help lists them", tok[0]);
    }
}

/* --- the console ----------------------------------------------------------- */

void page_console_init(page_console_t *c, const link_dev_t *dev)
{
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof(*c));
    c->dev = dev;
}

void page_console_key(page_console_t *c, int ch)
{
    if (c == NULL || c->dev == NULL || ch < 0) {
        return;
    }
    if (ch == '\r' || ch == '\n') {
        if (c->overlong != 0u) {
            say(c, "err line longer than %u characters",
                PAGE_CONSOLE_LINE_MAX);
        } else {
            c->line[c->len] = '\0';
            run_line(c);
        }
        c->len = 0;
        c->overlong = 0;
        return;
    }
    if (ch == 0x08 || ch == 0x7F) {
        if (c->len > 0u) {
            c->len--;
        }
        return;
    }
    if (ch < 0x20 || ch > 0x7E) {
        return;
    }
    if (c->len >= PAGE_CONSOLE_LINE_MAX) {
        c->overlong = 1;
        return;
    }
    c->line[c->len++] = (char)ch;
}

void page_console_poll(page_console_t *c)
{
    uint16_t st;

    if (c == NULL || c->dev == NULL || c->watch == 0u
        || !read_regs(c, LINK_PAGE_HITEC, LINK_HT_STATE, 1, &st)) {
        return;
    }
    switch ((link_hitec_state_t)LINK_HT_STATE_OF(st)) {
    case LINK_HITEC_STOPPING:
    case LINK_HITEC_BUSY:
    case LINK_HITEC_POWER_OFF:
        return;
    case LINK_HITEC_FREE:
    case LINK_HITEC_OPEN:
    default:
        c->watch = 0;
        hitec_report(c, "done");
        break;
    }
}
