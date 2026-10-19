/*
 * The Hitec bench harness's command lines and reports.  See
 * include/hitec_harness.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "hitec_harness.h"

#include <stdio.h>
#include <string.h>

#define MAX_WORDS 16u

typedef struct {
    char        buf[HH_LINE_MAX + 1u];
    const char *w[MAX_WORDS];
    unsigned    n;
} words_t;

static void fail(char *err, size_t len, const char *code, const char *detail)
{
    if (err == NULL || len == 0u) {
        return;
    }
    if (detail != NULL) {
        (void)snprintf(err, len, "err %s %s", code, detail);
    } else {
        (void)snprintf(err, len, "err %s", code);
    }
}

/* Split @p line at spaces and tabs.  False for a line too long or with too
 * many words. */
static bool split(const char *line, words_t *out)
{
    const size_t len = strlen(line);
    char *p;

    if (len > HH_LINE_MAX) {
        return false;
    }
    memcpy(out->buf, line, len + 1u);
    out->n = 0;
    p = out->buf;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r') {
            *p++ = '\0';
        }
        if (*p == '\0') {
            return true;
        }
        if (out->n == MAX_WORDS) {
            return false;
        }
        out->w[out->n++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r') {
            ++p;
        }
    }
}

/* A decimal or 0x hex number of at most @p max.  False for anything else. */
static bool num(const char *s, uint32_t max, uint32_t *out)
{
    uint64_t v = 0;
    unsigned base = 10;

    if (s == NULL || *s == '\0') {
        return false;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
        if (*s == '\0') {
            return false;
        }
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
        if (d >= base) {
            return false;
        }
        v = v * base + d;
        if (v > max) {
            return false;
        }
    }
    *out = (uint32_t)v;
    return true;
}

/* The value of option @p key in @p w ("key=value"), or NULL. */
static const char *opt(const char *w, const char *key)
{
    const size_t k = strlen(key);

    if (strncmp(w, key, k) == 0 && w[k] == '=') {
        return w + k + 1u;
    }
    return NULL;
}

static bool is_opt(const char *w)
{
    return strchr(w, '=') != NULL;
}

/* The SEND options lead=, window=, gap=, rx= in words @p from on.  False
 * and @p err set for an unknown key or a value out of range. */
static bool send_opts(const words_t *ws, unsigned from, hitec_xfer_t *x,
                      char *err, size_t err_len)
{
    static const struct {
        const char *key;
        uint32_t    max;
    } keys[] = {
        { "lead", HITEC_TIME_MAX_US },
        { "window", HITEC_TIME_MAX_US },
        { "gap", HITEC_GAP_MAX },
        { "rx", HITEC_RX_MAX },
    };

    for (unsigned i = from; i < ws->n; ++i) {
        unsigned k = 0;
        const char *v = NULL;
        uint32_t n;

        for (; k < sizeof(keys) / sizeof(keys[0]); ++k) {
            v = opt(ws->w[i], keys[k].key);
            if (v != NULL) {
                break;
            }
        }
        if (v == NULL) {
            fail(err, err_len, "option", ws->w[i]);
            return false;
        }
        if (!num(v, keys[k].max, &n)) {
            fail(err, err_len, "range", keys[k].key);
            return false;
        }
        switch (k) {
        case 0:  x->lead_us = n;              break;
        case 1:  x->window_us = n;            break;
        case 2:  x->gap_bits = (uint8_t)n;    break;
        default: x->max_rx = (uint8_t)n;      break;
        }
    }
    return true;
}

static bool parse_open(const words_t *ws, hh_cmd_t *c, char *err,
                       size_t err_len)
{
    uint32_t n;
    unsigned i = 3;

    if (ws->n < 3u) {
        fail(err, err_len, "usage",
             "open <classic|d> <pin> [od|pp] [mark=<pin>]");
        return false;
    }
    if (strcmp(ws->w[1], "classic") == 0) {
        c->family = HITEC_CLASSIC;
    } else if (strcmp(ws->w[1], "d") == 0) {
        c->family = HITEC_D;
    } else {
        fail(err, err_len, "family", ws->w[1]);
        return false;
    }
    if (!num(ws->w[2], HH_PIN_MAX, &n)) {
        fail(err, err_len, "range", "pin");
        return false;
    }
    c->pin = (uint8_t)n;
    c->drive = HITEC_DRIVE_OPEN_DRAIN;
    if (i < ws->n && !is_opt(ws->w[i])) {
        if (strcmp(ws->w[i], "pp") == 0) {
            c->drive = HITEC_DRIVE_PUSH_PULL;
        } else if (strcmp(ws->w[i], "od") != 0) {
            fail(err, err_len, "drive", ws->w[i]);
            return false;
        }
        ++i;
    }
    if (c->drive == HITEC_DRIVE_PUSH_PULL && c->family != HITEC_D) {
        fail(err, err_len, "drive", "classic is open drain only");
        return false;
    }
    for (; i < ws->n; ++i) {
        const char *v = opt(ws->w[i], "mark");

        if (v == NULL) {
            fail(err, err_len, "option", ws->w[i]);
            return false;
        }
        if (!num(v, HH_PIN_MAX, &n)) {
            fail(err, err_len, "range", "mark");
            return false;
        }
        c->mark = (uint8_t)n;
    }
    if (c->mark == c->pin) {
        fail(err, err_len, "range", "mark is the line's pin");
        return false;
    }
    return true;
}

/* The positional numbers of a transaction command: @p count of them from
 * word 1, each at most max[i].  Options follow. */
static bool positional(const words_t *ws, unsigned count, const uint32_t *max,
                       uint32_t *v, const char *usage, char *err,
                       size_t err_len)
{
    if (ws->n < 1u + count) {
        fail(err, err_len, "usage", usage);
        return false;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (is_opt(ws->w[1u + i])) {
            fail(err, err_len, "usage", usage);
            return false;
        }
        if (!num(ws->w[1u + i], max[i], &v[i])) {
            fail(err, err_len, "range", ws->w[1u + i]);
            return false;
        }
    }
    return true;
}

static bool parse_c(const words_t *ws, hh_cmd_t *c, char *err, size_t len)
{
    static const uint32_t max[3] = { 0xFFu, 0xFFu, 0xFFu };
    uint32_t v[3];
    hitec_c_expect_t e;

    if (!positional(ws, 3, max, v, "c <cmd> <p1> <p2>", err, len)) {
        return false;
    }
    hitec_c_xfer((uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2], &c->xfer);
    if (hitec_c_expect_of((uint8_t)v[0], &e)) {
        c->dec = HH_DEC_C;
        c->expect = (uint8_t)e;
    }
    return send_opts(ws, 4, &c->xfer, err, len);
}

static bool parse_dread(const words_t *ws, hh_cmd_t *c, char *err, size_t len)
{
    static const uint32_t max[1] = { 0xFFu };
    uint32_t v[1];
    uint32_t id = 0;
    words_t rest = *ws;

    if (!positional(ws, 1, max, v, "dread <addr> [id=<id>]", err, len)) {
        return false;
    }
    /* id= is the builder's; the rest are SEND options. */
    rest.n = 0;
    for (unsigned i = 2; i < ws->n; ++i) {
        const char *s = opt(ws->w[i], "id");

        if (s != NULL) {
            if (!num(s, 0xFFu, &id)) {
                fail(err, len, "range", "id");
                return false;
            }
        } else {
            rest.w[rest.n++] = ws->w[i];
        }
    }
    hitec_d_read_xfer((uint8_t)id, (uint8_t)v[0], &c->xfer);
    c->dec = HH_DEC_D;
    c->addr = (uint8_t)v[0];
    return send_opts(&rest, 0, &c->xfer, err, len);
}

static bool parse_dwrite(const words_t *ws, hh_cmd_t *c, char *err,
                         size_t len)
{
    static const uint32_t max[2] = { 0xFFu, 0xFFFFu };
    uint32_t v[2];
    uint32_t id = 0;
    words_t rest = *ws;

    if (!positional(ws, 2, max, v, "dwrite <addr> <value> [id=<id>]", err,
                    len)) {
        return false;
    }
    rest.n = 0;
    for (unsigned i = 3; i < ws->n; ++i) {
        const char *s = opt(ws->w[i], "id");

        if (s != NULL) {
            if (!num(s, 0xFFu, &id)) {
                fail(err, len, "range", "id");
                return false;
            }
        } else {
            rest.w[rest.n++] = ws->w[i];
        }
    }
    hitec_d_write_xfer((uint8_t)id, (uint8_t)v[0], (uint16_t)v[1], &c->xfer);
    return send_opts(&rest, 0, &c->xfer, err, len);
}

static bool parse_send(const words_t *ws, uint8_t family, hh_cmd_t *c,
                       char *err, size_t len)
{
    unsigned i = 1;

    memset(&c->xfer, 0, sizeof(c->xfer));
    c->xfer.kind = HITEC_XFER_SEND;
    c->xfer.family = family;
    for (; i < ws->n && !is_opt(ws->w[i]); ++i) {
        uint32_t b;

        if (c->xfer.n_tx == HITEC_TX_MAX) {
            fail(err, len, "range", "at most 8 bytes");
            return false;
        }
        if (!num(ws->w[i], 0xFFu, &b)) {
            fail(err, len, "range", ws->w[i]);
            return false;
        }
        c->xfer.tx[c->xfer.n_tx++] = (uint8_t)b;
    }
    if (c->xfer.n_tx == 0u) {
        fail(err, len, "usage", "send <b0> [b1 .. b7] [lead=] [gap=] "
                                "[window=] [rx=]");
        return false;
    }
    return send_opts(ws, i, &c->xfer, err, len);
}

static bool parse_listen(const words_t *ws, uint8_t family, hh_cmd_t *c,
                         char *err, size_t len)
{
    static const uint32_t max[2] = { HITEC_TIME_MAX_US, HITEC_RX_MAX };
    uint32_t v[2];
    uint32_t b;
    bool answer = false;
    uint8_t answer_byte = 0;

    if (!positional(ws, 2, max, v,
                    "listen <window_us> <rx> [answer=<b>] [want=<b>]", err,
                    len)) {
        return false;
    }
    for (unsigned i = 3; i < ws->n; ++i) {
        const char *s = opt(ws->w[i], "answer");
        const char *w = opt(ws->w[i], "want");

        if (s != NULL) {
            if (!num(s, 0xFFu, &b)) {
                fail(err, len, "range", "answer");
                return false;
            }
            answer = true;
            answer_byte = (uint8_t)b;
        } else if (w != NULL) {
            if (!num(w, 0xFFu, &b)) {
                fail(err, len, "range", "want");
                return false;
            }
            c->dec = HH_DEC_LISTEN;
            c->want = (uint8_t)b;
        } else {
            fail(err, len, "option", ws->w[i]);
            return false;
        }
    }
    if (!hitec_listen_xfer(family, v[0], (uint8_t)v[1], answer, answer_byte,
                           &c->xfer)) {
        fail(err, len, "range", "window and rx are 1 or more");
        return false;
    }
    return true;
}

static bool parse_level(const words_t *ws, uint8_t family, hh_cmd_t *c,
                        char *err, size_t len)
{
    static const uint32_t max[2] = { 1u, HITEC_TIME_MAX_US };
    uint32_t v[2];

    if (!positional(ws, 2, max, v, "level <0|1> <us>", err, len)) {
        return false;
    }
    if (ws->n > 3u) {
        fail(err, len, "option", ws->w[3]);
        return false;
    }
    if (!hitec_level_xfer(family, (uint8_t)v[0], v[1], &c->xfer)) {
        fail(err, len, "range", "us is 1 or more");
        return false;
    }
    return true;
}

static const struct {
    const char *name;
    uint8_t     verb;
    uint8_t     words;   /* exactly this many for the verbs without
                          * arguments; 0: checked by its parser */
} k_verbs[] = {
    { "hello", HH_HELLO, 1 },   { "help", HH_HELP, 1 },
    { "status", HH_STATUS, 1 }, { "open", HH_OPEN, 0 },
    { "close", HH_CLOSE, 1 },   { "abort", HH_ABORT, 1 },
    { "c", HH_C, 0 },           { "dread", HH_DREAD, 0 },
    { "dwrite", HH_DWRITE, 0 }, { "send", HH_SEND, 0 },
    { "listen", HH_LISTEN, 0 }, { "level", HH_LEVEL, 0 },
};

bool hh_is_xfer(uint8_t verb)
{
    return verb >= HH_C && verb <= HH_LEVEL;
}

bool hh_parse(const char *line, const hh_line_t *line_state, hh_cmd_t *out,
              char *err, size_t err_len)
{
    words_t ws;
    hh_cmd_t c;
    unsigned v = sizeof(k_verbs) / sizeof(k_verbs[0]);
    bool ok;

    if (line == NULL || line_state == NULL || out == NULL) {
        fail(err, err_len, "arg", NULL);
        return false;
    }
    if (!split(line, &ws)) {
        fail(err, err_len, "line", "too long or too many words");
        return false;
    }
    if (ws.n == 0u) {
        fail(err, err_len, "empty", NULL);
        return false;
    }
    for (unsigned i = 0; i < sizeof(k_verbs) / sizeof(k_verbs[0]); ++i) {
        if (strcmp(ws.w[0], k_verbs[i].name) == 0) {
            v = i;
            break;
        }
    }
    if (v == sizeof(k_verbs) / sizeof(k_verbs[0])) {
        fail(err, err_len, "unknown", ws.w[0]);
        return false;
    }
    if (k_verbs[v].words != 0u && ws.n != k_verbs[v].words) {
        fail(err, err_len, "usage", ws.w[0]);
        return false;
    }

    memset(&c, 0, sizeof(c));
    c.verb = k_verbs[v].verb;
    c.mark = HH_NO_PIN;
    if (hh_is_xfer(c.verb) && !line_state->open) {
        fail(err, err_len, "closed", "open a line first");
        return false;
    }
    switch (c.verb) {
    case HH_OPEN:
        ok = parse_open(&ws, &c, err, err_len);
        break;
    case HH_C:
        ok = line_state->family == HITEC_CLASSIC;
        if (!ok) {
            fail(err, err_len, "family", "c needs a classic line");
        } else {
            ok = parse_c(&ws, &c, err, err_len);
        }
        break;
    case HH_DREAD:
    case HH_DWRITE:
        ok = line_state->family == HITEC_D;
        if (!ok) {
            fail(err, err_len, "family", "dread and dwrite need a d line");
        } else if (c.verb == HH_DREAD) {
            ok = parse_dread(&ws, &c, err, err_len);
        } else {
            ok = parse_dwrite(&ws, &c, err, err_len);
        }
        break;
    case HH_SEND:
        ok = parse_send(&ws, line_state->family, &c, err, err_len);
        break;
    case HH_LISTEN:
        ok = parse_listen(&ws, line_state->family, &c, err, err_len);
        break;
    case HH_LEVEL:
        ok = parse_level(&ws, line_state->family, &c, err, err_len);
        break;
    default:
        ok = true;
        break;
    }
    if (ok && hh_is_xfer(c.verb) && !hitec_xfer_valid(&c.xfer)) {
        /* An option made the transaction one no driver runs: a window
         * without rx, rx without a window. */
        fail(err, err_len, "range", "window and rx go together");
        ok = false;
    }
    if (ok) {
        *out = c;
    }
    return ok;
}

const char *hh_rx_name(hitec_rx_t code)
{
    static const char *const names[] = {
        "OK",   "ARG",     "ABORTED", "OVERFLOW", "SILENT",
        "EARLY", "LATE",   "NAK",     "LENGTH",   "FRAMING",
        "GAP",  "ACK",     "HEADER",  "ADDRESS",  "SUM",
    };

    if ((unsigned)code >= sizeof(names) / sizeof(names[0])) {
        return "?";
    }
    return names[code];
}

hitec_rx_t hh_decode(const hh_cmd_t *cmd, const hitec_capture_t *cap,
                     hitec_reply_t *out)
{
    memset(out, 0, sizeof(*out));
    switch (cmd->dec) {
    case HH_DEC_C:
        return hitec_c_decode(cap, (hitec_c_expect_t)cmd->expect, out);
    case HH_DEC_D:
        return hitec_d_decode(cap, cmd->addr, out);
    case HH_DEC_LISTEN:
        return hitec_listen_decode(cap, cmd->want, out);
    default:
        return HITEC_RX_OK;
    }
}

static const char *kind_name(uint8_t kind)
{
    switch (kind) {
    case HITEC_XFER_SEND:   return "send";
    case HITEC_XFER_LISTEN: return "listen";
    case HITEC_XFER_LEVEL:  return "level";
    default:                return "?";
    }
}

static const char *family_name(uint8_t family)
{
    return family == HITEC_D ? "d" : "classic";
}

void hh_report_start(const hh_cmd_t *cmd, const hitec_pio_run_t *run,
                     hh_emit_t emit, void *ctx)
{
    const hitec_xfer_t *x = &cmd->xfer;
    char line[HH_OUT_MAX];
    char tx[3u * HITEC_TX_MAX + 1u];
    char ans[8];
    size_t at = 0;

    tx[0] = '-';
    tx[1] = '\0';
    for (unsigned i = 0; i < x->n_tx; ++i) {
        at += (size_t)snprintf(tx + at, sizeof(tx) - at, "%s%02x",
                               i == 0u ? "" : ",", x->tx[i]);
    }
    if (x->answer != 0u) {
        (void)snprintf(ans, sizeof(ans), "0x%02x", (unsigned)x->answer_byte);
    } else {
        (void)snprintf(ans, sizeof(ans), "-");
    }
    (void)snprintf(line, sizeof(line),
                   "ok start kind=%s family=%s tx=%s lead_us=%lu gap=%u "
                   "window_us=%lu rx=%u answer=%s level=%u level_us=%lu "
                   "lead_ns=%lu span_us=%lu",
                   kind_name(x->kind), family_name(x->family), tx,
                   (unsigned long)x->lead_us, (unsigned)x->gap_bits,
                   (unsigned long)x->window_us, (unsigned)x->max_rx, ans,
                   (unsigned)x->level, (unsigned long)x->level_us,
                   (unsigned long)run->lead_ns, (unsigned long)run->span_us);
    emit(ctx, line);
}

void hh_report_capture(const hh_cmd_t *cmd, const hitec_capture_t *cap,
                       uint32_t t_us, hh_emit_t emit, void *ctx)
{
    char line[HH_OUT_MAX];
    hitec_reply_t r;
    hitec_rx_t rx;
    static const char *const dec_names[] = { "none", "c", "d", "listen" };

    (void)snprintf(line, sizeof(line),
                   "cap n=%u full=%u answered=%u overflow=%u aborted=%u "
                   "window_ns=%lu t_us=%lu",
                   (unsigned)cap->n, (unsigned)cap->full,
                   (unsigned)cap->answered, (unsigned)cap->overflow,
                   (unsigned)cap->aborted, (unsigned long)cap->window_ns,
                   (unsigned long)t_us);
    emit(ctx, line);
    for (unsigned k = 0; k < cap->n && k < HITEC_RX_MAX; ++k) {
        (void)snprintf(line, sizeof(line),
                       "byte %u at_ns=%lu value=0x%02x framing=%u", k,
                       (unsigned long)cap->byte[k].at_ns,
                       (unsigned)cap->byte[k].value,
                       (unsigned)cap->byte[k].framing);
        emit(ctx, line);
    }
    rx = hh_decode(cmd, cap, &r);
    (void)snprintf(line, sizeof(line),
                   "dec kind=%s rx=%s value=0x%04x id=0x%02x n=%u "
                   "delay_ns=%lu",
                   cmd->dec < 4u ? dec_names[cmd->dec] : "?",
                   hh_rx_name(rx), (unsigned)r.value, (unsigned)r.id,
                   (unsigned)r.n, (unsigned long)r.delay_ns);
    emit(ctx, line);
    emit(ctx, "done");
}

void hh_report_help(hh_emit_t emit, void *ctx)
{
    static const char *const lines[] = {
        "# hello | help | status | close | abort",
        "# open <classic|d> <pin> [od|pp] [mark=<pin>]",
        "# c <cmd> <p1> <p2>              classic, decoded",
        "# dread <addr> [id=<id>]         d series, decoded",
        "# dwrite <addr> <value> [id=<id>]",
        "# send <b0> [b1 .. b7]",
        "#   c dread dwrite send: lead=<us> gap=<bits> window=<us> rx=<n>",
        "# listen <window_us> <rx> [answer=<b>] [want=<b>]",
        "# level <0|1> <us>",
        "ok help",
    };

    for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
        emit(ctx, lines[i]);
    }
}
