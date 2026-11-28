/**
 * @file
 * @brief The printer file reader, second part: templates, answers,
 *        fields, error lines, steps and their lines, jobs, `abort` and
 *        `verdict` (prn_spec.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "prn_job.h"
#include "prn_parse_int.h"

const char *const pp_fields[] = {
    "busy", "printing", "buf_full", "cover", "no_label", "label_end",
    "label_err", "label_mode", "paper_out", "battery_low", "head_hot",
    "roll_w", "roll_h", "roll_gap", "roll_type", "left", "uid", "count",
    "page_index", "result", "battery", "x0", "x1", "x2", "x3", NULL,
};

const char *const pp_vars[] = {
    "stream_len", "pages", "frames", "lines", "line_bytes", "head_dots",
    "density", "speed", "mlead", "mtrail", "roll_w", "roll_h", "roll_gap",
    "pflags", "idx", "total", "line_no", "lay_w", "lay_h", "c0", "c1",
    "uid0", "w0", "h0", "v0", "v1", "v2", "v3", NULL,
};

/** @brief Encodings in prn_enc_t order. */
static const char *const k_encs[] = { "u8", "le16", "be16", "le32", "be32",
                                      NULL };
/** @brief Checksum kinds in prn_ck_t order. */
static const char *const k_cks[] = { "sum8", "sum16le", "sum16be", "xor8",
                                     "crc8", "crc16ccitt_le",
                                     "crc16ccitt_be", "crc32le", "svpage",
                                     NULL };
/** @brief Codes a file may name, NULL where it may not, in prn_err_t
 *         order. */
static const char *const k_codes[] = {
    "", "COVER", "NO_LABEL", "LABEL", "BATTERY", "HOT", "BUSY", "SILENT",
    "", "", "NOT_PRINTED", "ROLL", NULL,
};
/** @brief Field kinds in prn_fkind_t order. */
static const char *const k_fkinds[] = { "bit", "u8", "le16", "be16", "le32",
                                        "be32", "bytes", NULL };

/**
 * @brief Copy token @p i into a NUL-terminated buffer.
 * @param p   the state
 * @param i   the token
 * @param buf the buffer
 * @param cap its size
 * @return false when the token does not fit
 */
static bool tok_copy(const pp_t *p, unsigned i, char *buf, size_t cap)
{
    if (i >= p->nt || p->len[i] >= cap) {
        return false;
    }
    memcpy(buf, PP_T(p, i), p->len[i]);
    buf[p->len[i]] = '\0';
    return true;
}

/**
 * @brief The index of a word in a list, for text of a given length.
 * @param t     the text
 * @param n     its length
 * @param words the list
 * @return the index, or -1
 */
static int word_n(const char *t, size_t n, const char *const *words)
{
    for (int k = 0; words[k] != NULL; ++k) {
        if (strlen(words[k]) == n && n != 0u && memcmp(words[k], t, n) == 0) {
            return k;
        }
    }
    return -1;
}

/**
 * @brief Whether a variable may appear in a context.
 * @param var  prn_var_t
 * @param ctx  a template context, or PP_C_CHECK + 1 for a step value
 * @return true when it may
 */
static bool var_in(int var, unsigned ctx)
{
    switch (var) {
    case PRN_V_PFLAGS:
        return ctx == PP_C_PAGE;
    case PRN_V_IDX:
    case PRN_V_TOTAL:
        return ctx == PP_C_CHUNK;
    case PRN_V_LINE_NO:
        return ctx == PP_C_ROWS;
    default:
        return var > 0;
    }
}

/**
 * @brief A signed offset, -128 to 127.
 * @param t the text
 * @param n its length
 * @param v set to the offset
 * @return true when it is one
 */
static bool offset(const char *t, size_t n, int8_t *v)
{
    const bool neg = n > 0u && t[0] == '-';
    const size_t i0 = neg ? 1u : 0u;
    if (n <= i0 || n - i0 > 3u) {
        return false;
    }
    int x = 0;
    for (size_t i = i0; i < n; ++i) {
        if (t[i] < '0' || t[i] > '9') {
            return false;
        }
        x = x * 10 + (t[i] - '0');
    }
    x = neg ? -x : x;
    if (x < -128 || x > 127) {
        return false;
    }
    *v = (int8_t)x;
    return true;
}

/**
 * @brief One placeholder token, `{...}`.
 * @param t   its text inside the braces
 * @param n   its length
 * @param ctx the template context
 * @param tok set to the token
 * @return 0 or a code
 */
static uint8_t placeholder(const char *t, size_t n, unsigned ctx,
                           prn_tok_t *tok)
{
    const char *part[4] = { t, t, t, t };
    size_t plen[4] = { 0u, 0u, 0u, 0u }; /* an empty part matches nothing */
    unsigned np = 0u;
    size_t from = 0u;
    for (size_t i = 0; i <= n; ++i) {
        if (i == n || t[i] == ':') {
            if (np == 4u) {
                return 11u;
            }
            part[np] = t + from;
            plen[np] = i - from;
            np++;
            from = i + 1u;
        }
    }
    static const char *const k_data[] = { "data", NULL };
    if (np == 1u && word_n(part[0], plen[0], k_data) == 0) {
        if (ctx != PP_C_CHUNK && ctx != PP_C_ROWS) {
            return 22u;
        }
        tok->type = PRN_T_DATA;
        return 0u;
    }
    static const char *const k_lck[] = { "len", "ck", NULL };
    const int lk = word_n(part[0], plen[0], k_lck);
    if (lk == 1 && np == 3u && ctx == PP_C_PAGE
        && word_n(part[1], plen[1], k_cks) == PRN_CK_SVPAGE
        && word_n(part[2], plen[2], k_encs) == PRN_E_LE16) {
        tok->type = PRN_T_CK;
        tok->x = PRN_CK_SVPAGE;
        return 0u;
    }
    if (lk >= 0) {
        const int k = word_n(part[1], plen[1], (lk == 0) ? k_encs : k_cks);
        if (np != 4u || k < 0 || (lk == 1 && k == PRN_CK_SVPAGE)
            || !offset(part[2], plen[2], &tok->a)
            || !offset(part[3], plen[3], &tok->b)) {
            return 11u;
        }
        tok->type = (lk == 0) ? PRN_T_LEN : PRN_T_CK;
        tok->x = (uint8_t)k;
        return 0u;
    }
    if (np != 2u || ctx == PP_C_CHECK) {
        return 11u;
    }
    const int enc = word_n(part[1], plen[1], k_encs);
    static const char *const k_args[] = { "a", "p0", "p1", "p2", NULL };
    const int arg = word_n(part[0], plen[0], k_args);
    if (enc < 0) {
        return 11u;
    }
    if (arg >= 0) {
        if (ctx != PP_C_TMPL) {
            return 11u;
        }
        tok->type = PRN_T_ARG;
        tok->x = (uint8_t)enc;
        tok->a = (int8_t)arg;
        return 0u;
    }
    const int var = word_n(part[0], plen[0], pp_vars) + 1;
    if (!var_in(var, ctx)) {
        return 11u;
    }
    tok->type = PRN_T_VAR;
    tok->x = (uint8_t)enc;
    tok->a = (int8_t)var;
    return 0u;
}

uint8_t pp_tokens(pp_t *p, unsigned from, unsigned ctx, unsigned max,
                  prn_tseq_t *seq)
{
    prn_spec_t *s = p->s;
    const unsigned count = p->nt - from;
    if (count > max) {
        return (ctx == PP_C_CHECK) ? 7u : 10u;
    }
    if (s->ntok + count > PRN_TOKENS) {
        return 30u;
    }
    unsigned data = 0u;
    unsigned svpage = 0u;
    for (unsigned i = 0; i < count; ++i) {
        const char *t = PP_T(p, from + i);
        const size_t n = p->len[from + i];
        prn_tok_t tok = { 0u, 0u, 0, 0 };
        uint8_t b;
        if (n == 2u && pp_hex(t, 2u, &b, 1u) == 1u && ctx != PP_C_CHECK) {
            tok.type = PRN_T_BYTE;
            tok.x = b;
        } else if (n >= 2u && t[0] == '{' && t[n - 1u] == '}') {
            const uint8_t e = placeholder(t + 1, n - 2u, ctx, &tok);
            if (e != 0u) {
                return e;
            }
        } else {
            return 11u;
        }
        data += (tok.type == PRN_T_DATA) ? 1u : 0u;
        svpage += (tok.type == PRN_T_CK && tok.x == PRN_CK_SVPAGE) ? 1u : 0u;
        if (ctx == PP_C_CHECK && tok.type != PRN_T_CK) {
            return 11u;
        }
        s->tok[s->ntok + i] = tok;
    }
    if ((ctx == PP_C_CHUNK || ctx == PP_C_ROWS) && data != 1u) {
        return 22u;
    }
    if (svpage > 1u) {
        return 11u;
    }
    seq->first = s->ntok;
    seq->n = (uint8_t)count;
    s->ntok = (uint16_t)(s->ntok + count);
    return 0u;
}

size_t pp_fixed_len(const prn_spec_t *s, prn_tseq_t seq)
{
    size_t n = 0u;
    for (unsigned i = 0; i < seq.n; ++i) {
        const prn_tok_t *t = &s->tok[seq.first + i];
        if (t->type == PRN_T_BYTE) {
            n += 1u;
        } else if (t->type == PRN_T_CK) {
            n += prn_ck_width(t->x);
        } else if (t->type != PRN_T_DATA) {
            n += prn_enc_width(t->x);
        }
    }
    return n;
}

/**
 * @brief Whether a range lies inside a frame of @p len bytes and its
 *        length fits @p enc.
 * @param t   the token
 * @param len the frame's length
 * @return true when it does
 */
static bool range_in(const prn_tok_t *t, size_t len)
{
    const long f = (t->a < 0) ? (long)len + t->a : t->a;
    const long e = (t->b < 0) ? (long)len + t->b : t->b;
    if (f < 0 || e < f || e >= (long)len) {
        return false;
    }
    if (t->type == PRN_T_LEN) {
        const unsigned w = prn_enc_width(t->x);
        return w >= 4u || (unsigned long)(e - f + 1) < (1ul << (8u * w));
    }
    return true;
}

bool pp_ranges_ok(const prn_spec_t *s, prn_tseq_t seq, size_t lo, size_t hi)
{
    for (unsigned i = 0; i < seq.n; ++i) {
        const prn_tok_t *t = &s->tok[seq.first + i];
        if ((t->type == PRN_T_LEN
             || (t->type == PRN_T_CK && t->x != PRN_CK_SVPAGE))
            && (!range_in(t, lo) || !range_in(t, hi))) {
            return false;
        }
    }
    return true;
}

int prn_field_find(const prn_spec_t *s, uint8_t key, uint8_t name)
{
    for (unsigned i = 0; i < s->nfield && i < PRN_FIELDS; ++i) {
        if (s->field[i].key == key && s->field[i].name == name) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief The first field line of a name, under any key.
 * @param s    the spec
 * @param name prn_field_name_t
 * @return its index, or -1
 */
static int field_any(const prn_spec_t *s, int name)
{
    for (unsigned i = 0; i < s->nfield; ++i) {
        if (s->field[i].name == name) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief A named template's index.
 * @param p the state
 * @param i the token naming it
 * @return the index, or -1
 */
static int tmpl_find(const pp_t *p, unsigned i)
{
    for (unsigned k = 0; k < p->s->ntmpl; ++k) {
        if (pp_is(p, i, p->s->tmpl[k].name)) {
            return (int)k;
        }
    }
    return -1;
}

/**
 * @brief A step's index.
 * @param s    the spec
 * @param name its name
 * @param n    the name's length
 * @return the index, or -1
 */
static int step_find(const prn_spec_t *s, const char *name, size_t n)
{
    for (unsigned k = 0; k < s->nstep; ++k) {
        if (strlen(s->step[k].name) == n && memcmp(s->step[k].name, name, n)
                                                == 0) {
            return (int)k;
        }
    }
    return -1;
}

/**
 * @brief A template's or step's name: 1 to 11 characters a-z 0-9 _.
 * @param p   the state
 * @param i   the token
 * @param out the name, PRN_TMPL_NAME + 1 bytes
 * @return 0 or 29
 */
static uint8_t name_tok(const pp_t *p, unsigned i, char *out)
{
    const char *t = PP_T(p, i);
    if (p->len[i] == 0u || p->len[i] > PRN_TMPL_NAME) {
        return 29u;
    }
    for (unsigned k = 0; k < p->len[i]; ++k) {
        const char c = t[k];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return 29u;
        }
    }
    (void)tok_copy(p, i, out, PRN_TMPL_NAME + 1u);
    return 0u;
}

/**
 * @brief The arguments of a send: `a=n`, `p0=x`, ... from token @p from
 *        to @p to (exclusive).
 * @param p    the state
 * @param from the first token
 * @param to   past the last
 * @param snd  the send
 * @return 0 or a code
 */
static uint8_t args(const pp_t *p, unsigned from, unsigned to,
                    prn_send_t *snd)
{
    static const char *const k_args[] = { "a", "p0", "p1", "p2", NULL };
    unsigned given = 0u;
    for (unsigned i = from; i < to; ++i) {
        const char *t = PP_T(p, i);
        const size_t eq = pp_find(t, p->len[i], '=');
        if (eq == p->len[i]) {
            return 7u;
        }
        const int a = word_n(t, eq, k_args);
        if (a < 0 || (given & (1u << a)) != 0u) {
            return 7u;
        }
        given |= 1u << a;
        const char *val = t + eq + 1u;
        const size_t vn = p->len[i] - eq - 1u;
        if (pp_num_text(val, vn, 0u, 0xFFFFFFFFu, &snd->n[a]) == 0u) {
            continue;
        }
        const int var = word_n(val, vn, pp_vars) + 1;
        if (!var_in(var, PP_C_CHECK + 1u)) {
            return 14u;
        }
        snd->var[a] = (uint8_t)var;
    }
    return 0u;
}

/**
 * @brief A send: a template's name and its arguments.
 * @param p    the state
 * @param from the token naming the template
 * @param to   past the last argument
 * @param snd  the send
 * @return 0 or a code
 */
static uint8_t send_of(const pp_t *p, unsigned from, unsigned to,
                       prn_send_t *snd)
{
    const int t = tmpl_find(p, from);
    if (t < 0) {
        return 14u;
    }
    snd->tmpl = (uint8_t)t;
    return args(p, from + 1u, to, snd);
}

/**
 * @brief A field name declared under the open step's answer key.
 * @param p the state
 * @param i the token
 * @param f set to the name
 * @return 0 or 14
 */
static uint8_t step_field(const pp_t *p, unsigned i, uint8_t *f)
{
    const prn_step_t *st = &p->s->step[p->s->nstep - 1u];
    const int name = word_n(PP_T(p, i), p->len[i], pp_fields);
    if (name < 0 || !st->has_expect
        || prn_field_find(p->s, st->expect, (uint8_t)name) < 0) {
        return 14u;
    }
    *f = (uint8_t)name;
    return 0u;
}

/**
 * @brief A condition from tokens @p from to @p to (exclusive).
 * @param p    the state
 * @param from the first token
 * @param to   past the last
 * @param c    the condition
 * @return 0 or a code
 */
static uint8_t cond(const pp_t *p, unsigned from, unsigned to, prn_cond_t *c)
{
    const unsigned n = to - from;
    if (n != 2u && n != 3u) {
        return 7u;
    }
    uint8_t e = step_field(p, from, &c->field);
    if (e != 0u) {
        return e;
    }
    if (n == 2u) {
        if (!pp_is(p, from + 1u, "set") && !pp_is(p, from + 1u, "clear")) {
            return 7u;
        }
        c->op = pp_is(p, from + 1u, "set") ? PRN_OP_SET : PRN_OP_CLEAR;
        return 0u;
    }
    const bool eq = pp_is(p, from + 1u, "=");
    if (!eq && !pp_is(p, from + 1u, "!=")) {
        return 7u;
    }
    if (pp_num(p, from + 2u, 0u, 0xFFFFFFFFu, &c->n) == 0u) {
        c->op = eq ? PRN_OP_EQ_N : PRN_OP_NE_N;
        return 0u;
    }
    const char *t = PP_T(p, from + 2u);
    size_t len = p->len[from + 2u];
    bool minus1 = false;
    if (len > 2u && t[len - 2u] == '-' && t[len - 1u] == '1') {
        minus1 = true;
        len -= 2u;
    }
    const int var = word_n(t, len, pp_vars) + 1;
    if (!var_in(var, PP_C_CHECK + 1u)) {
        return 14u;
    }
    if (minus1 && !eq) {
        return 7u;
    }
    c->var = (uint8_t)var;
    c->op = minus1 ? PRN_OP_EQ_V1 : (eq ? PRN_OP_EQ_V : PRN_OP_NE_V);
    return 0u;
}

/**
 * @brief A code a file may name.
 * @param p the state
 * @param i the token
 * @param c set to the code
 * @return 0 or 7
 */
static uint8_t code_tok(const pp_t *p, unsigned i, uint8_t *c)
{
    if (i >= p->nt) {
        return 7u;
    }
    const int k = word_n(PP_T(p, i), p->len[i], k_codes);
    if (k <= 0) {
        return 7u;
    }
    *c = (uint8_t)k;
    return 0u;
}

/**
 * @brief Whether a sub-key was seen in the open step; mark it seen.
 * @param p   the state
 * @param sub the sub-key
 * @return 0 or 8
 */
static uint8_t sub_once(pp_t *p, pp_sub_t sub)
{
    if ((p->sub & (1u << sub)) != 0u) {
        return 8u;
    }
    p->sub = (uint16_t)(p->sub | (1u << sub));
    return 0u;
}

/**
 * @brief Mark a sub-key seen and hold the line to a token count.
 * @param p        the state
 * @param sub      the sub-key
 * @param nt       the count
 * @param at_least the count is a least count
 * @return 0, 8 (E08) or 7 (E07)
 */
static uint8_t sub_once_n(pp_t *p, pp_sub_t sub, unsigned nt, bool at_least)
{
    const uint8_t e = sub_once(p, sub);
    if (e != 0u) {
        return e;
    }
    return ((at_least ? p->nt < nt : p->nt != nt)) ? 7u : 0u;
}

/**
 * @brief A sub-key that takes one number in a range.
 * @param p   the state
 * @param sub the sub-key
 * @param lo  the least value
 * @param hi  the largest
 * @param v   the value
 * @return 0 or a code
 */
static uint8_t sub_num(pp_t *p, pp_sub_t sub, uint32_t lo, uint32_t hi,
                       uint16_t *v)
{
    uint32_t x = 0u;
    uint8_t e = sub_once_n(p, sub, 2u, false);
    if (e != 0u) {
        return e;
    }
    e = pp_num(p, 1u, lo, hi, &x);
    *v = (uint16_t)x;
    return e;
}

/**
 * @brief Whether a sub-key is on the step.
 * @param f   the step's sub-key bits
 * @param sub the sub-key
 * @return true when it is
 */
static bool has(uint16_t f, pp_sub_t sub)
{
    return (f & (1u << (unsigned)sub)) != 0u;
}

/**
 * @brief A line of the open step.
 * @param p the state
 * @return 0 or a code
 */
static uint8_t step_line(pp_t *p)
{
    prn_spec_t *s = p->s;
    prn_step_t *st = &s->step[s->nstep - 1u];
    uint8_t e;
    uint16_t v = 0u;
    if (pp_is(p, 0u, "send")) {
        e = sub_once_n(p, S_SEND, 2u, true);
        if (e != 0u) {
            return e;
        }
        st->kind = PRN_S_SEND;
        return send_of(p, 1u, p->nt, &st->send);
    }
    if (pp_is(p, 0u, "data") || pp_is(p, 0u, "flags")
        || pp_is(p, 0u, "roll")) {
        const bool flags = pp_is(p, 0u, "flags");
        const pp_sub_t sub = flags ? S_FLAGS
                                   : (pp_is(p, 0u, "data") ? S_DATA : S_ROLL);
        e = sub_once_n(p, sub, flags ? 2u : 1u, false);
        if (e != 0u) {
            return e;
        }
        if (sub == S_DATA) {
            st->kind = PRN_S_DATA;
        } else if (flags) {
            st->flags = true;
            return pp_is(p, 1u, "on") ? 0u : 7u;
        } else {
            if (!st->has_expect
                || prn_field_find(s, st->expect, PRN_F_ROLL_W) < 0
                || prn_field_find(s, st->expect, PRN_F_ROLL_H) < 0) {
                return 14u;
            }
            st->roll = true;
        }
        return 0u;
    }
    if (pp_is(p, 0u, "wait")) {
        st->kind = PRN_S_WAIT;
        return sub_num(p, S_WAIT, 1u, 5000u, &st->wait_ms);
    }
    if (pp_is(p, 0u, "poll")) {
        return sub_num(p, S_POLL, 20u, 5000u, &st->poll_ms);
    }
    if (pp_is(p, 0u, "within")) {
        return sub_num(p, S_WITHIN, 100u, 60000u, &st->within_ms);
    }
    if (pp_is(p, 0u, "delay")) {
        return sub_num(p, S_DELAY, 1u, 5000u, &st->delay_ms);
    }
    if (pp_is(p, 0u, "tries")) {
        e = sub_num(p, S_TRIES, 1u, 5u, &v);
        st->tries = (uint8_t)v;
        return e;
    }
    if (pp_is(p, 0u, "expect")) {
        e = sub_once_n(p, S_EXPECT, 2u, false);
        if (e != 0u) {
            return e;
        }
        st->has_expect = true;
        return (pp_hex(PP_T(p, 1), p->len[1], &st->expect, 1u) == 1u) ? 0u
                                                                       : 7u;
    }
    if (pp_is(p, 0u, "else")) {
        e = sub_once_n(p, S_ELSE, 2u, false);
        if (e != 0u) {
            return e;
        }
        return code_tok(p, 1u, &st->else_err);
    }
    if (pp_is(p, 0u, "until")) {
        e = sub_once_n(p, S_UNTIL, 3u, true);
        if (e != 0u) {
            return e;
        }
        unsigned k = 2u;
        while (k < p->nt && !pp_is(p, k, "and")) {
            ++k;
        }
        e = cond(p, 1u, k, &st->until[0]);
        if (e != 0u) {
            return e;
        }
        st->nuntil = 1u;
        if (k < p->nt) {
            e = cond(p, k + 1u, p->nt, &st->until[1]);
            if (e != 0u) {
                return e;
            }
            st->nuntil = 2u;
        }
        return 0u;
    }
    if (pp_is(p, 0u, "save")) {
        if (st->nsave >= PRN_STEP_SAVES) {
            return 30u;
        }
        if (p->nt != 3u) {
            return 7u;
        }
        const int var = word_n(PP_T(p, 1), p->len[1], pp_vars) + 1;
        if (var < (int)PRN_V_SAVED) {
            return 7u;
        }
        e = step_field(p, 2u, &st->save_field[st->nsave]);
        if (e != 0u) {
            return e;
        }
        st->save_var[st->nsave++] = (uint8_t)var;
        return 0u;
    }
    if (pp_is(p, 0u, "check")) {
        if (st->ncheck >= PRN_STEP_CHECKS) {
            return 30u;
        }
        unsigned k = 1u;
        while (k < p->nt && !pp_is(p, k, "else")) {
            ++k;
        }
        if (k + 2u != p->nt) {
            return 7u;
        }
        prn_cond_t *c = &st->check[st->ncheck];
        e = cond(p, 1u, k, c);
        if (e == 0u) {
            e = code_tok(p, k + 1u, &c->err);
        }
        if (e != 0u) {
            return e;
        }
        st->ncheck++;
        return 0u;
    }
    if (pp_is(p, 0u, "relink")) {
        e = sub_once_n(p, S_RELINK, 2u, false);
        if (e != 0u) {
            return e;
        }
        p->relink_line[s->nstep - 1u] = p->lineno;
        if (pp_is(p, 1u, "verdict")) {
            st->relink = PRN_RELINK_VERDICT;
        }
        return name_tok(p, 1u, st->relink_name);
    }
    return 6u;
}

uint8_t pp_close_step(pp_t *p)
{
    const prn_step_t *st = &p->s->step[p->s->nstep - 1u];
    const uint16_t f = p->sub;
    const unsigned kinds = has(f, S_SEND) + has(f, S_DATA) + has(f, S_WAIT);
    bool ok = kinds == 1u;
    const uint16_t answer = (uint16_t)((1u << S_EXPECT) | (1u << S_POLL)
                                       | (1u << S_UNTIL) | (1u << S_TRIES)
                                       | (1u << S_ROLL) | (1u << S_DELAY)
                                       | (1u << S_FLAGS));
    if (has(f, S_WAIT)) {
        ok = ok && (f & (answer | (1u << S_WITHIN) | (1u << S_ELSE))) == 0u
             && st->nsave == 0u && st->ncheck == 0u;
    } else if (has(f, S_DATA)) {
        ok = ok && has(f, S_WITHIN) && (f & answer) == 0u && st->nsave == 0u
             && st->ncheck == 0u;
    } else {
        ok = ok && has(f, S_WITHIN) && (has(f, S_POLL) == has(f, S_UNTIL))
             && !(has(f, S_POLL) && (!has(f, S_EXPECT) || has(f, S_TRIES)))
             && !(has(f, S_FLAGS) && !has(f, S_EXPECT));
    }
    p->in_step = false;
    if (!ok) {
        p->lineno = p->step_line;
        return 24u;
    }
    return 0u;
}

/**
 * @brief A job line.
 * @param p the state
 * @return 0 or a code
 */
static uint8_t job(pp_t *p)
{
    prn_spec_t *s = p->s;
    const bool print = pp_is(p, 1u, "print");
    if (!print && !pp_is(p, 1u, "query")) {
        return 6u;
    }
    const uint8_t e = pp_once(p, print ? K_PRINT : K_QUERY);
    if (e != 0u) {
        return e;
    }
    const unsigned n = p->nt - 2u;
    uint8_t *list = print ? s->print : s->query;
    if (n == 0u || n > PRN_STEPS) {
        return 15u;
    }
    unsigned data = 0u;
    for (unsigned i = 0; i < n; ++i) {
        const int k = step_find(s, PP_T(p, 2u + i), p->len[2u + i]);
        if (k < 0) {
            return 15u;
        }
        for (unsigned j = 0; j < i; ++j) {
            if (list[j] == (uint8_t)k) {
                return 15u;
            }
        }
        list[i] = (uint8_t)k;
        data += (s->step[k].kind == PRN_S_DATA) ? 1u : 0u;
    }
    if (data != (print ? 1u : 0u)) {
        return 15u;
    }
    if (print) {
        s->nprint = (uint8_t)n;
    } else {
        s->nquery = (uint8_t)n;
    }
    return 0u;
}

/**
 * @brief A step's position in the print job.
 * @param p the state
 * @param i the token naming it
 * @return the position, or -1
 */
static int print_pos(const pp_t *p, unsigned i)
{
    const int k = step_find(p->s, PP_T(p, i), p->len[i]);
    for (unsigned j = 0; k >= 0 && j < p->s->nprint; ++j) {
        if (p->s->print[j] == (uint8_t)k) {
            return (int)j;
        }
    }
    return -1;
}

/**
 * @brief The abort line.
 * @param p the state
 * @return 0 or a code
 */
static uint8_t abort_line(pp_t *p)
{
    prn_spec_t *s = p->s;
    uint8_t e = pp_once_n(p, K_ABORT, 6u, true);
    if (e != 0u) {
        return e;
    }
    unsigned k = 2u;
    while (k < p->nt && !pp_is(p, k, "from")) {
        ++k;
    }
    if (k + 4u != p->nt || !pp_is(p, k + 2u, "to")) {
        return 7u;
    }
    if (!pp_has(p, K_PRINT)) {
        return 26u;
    }
    e = send_of(p, 1u, k, &s->abort);
    if (e != 0u) {
        return e;
    }
    const int from = print_pos(p, k + 1u);
    const int to = print_pos(p, k + 3u);
    if (from < 0 || to < from) {
        return 26u;
    }
    s->abort_from = (uint8_t)from;
    s->abort_to = (uint8_t)to;
    s->has_abort = true;
    return 0u;
}

/**
 * @brief The verdict line.
 * @param p the state
 * @return 0 or a code
 */
static uint8_t verdict(pp_t *p)
{
    static const char *const k_kinds[] = { "counter", "result", "done",
                                           "none", NULL };
    prn_spec_t *s = p->s;
    uint8_t e = pp_once_n(p, K_VERDICT, 2u, true);
    if (e != 0u) {
        return e;
    }
    const int kind = word_n(PP_T(p, 1), p->len[1], k_kinds);
    if (kind < 0) {
        return 7u;
    }
    s->verdict = (uint8_t)kind;
    if (kind >= PRN_VD_DONE) {
        return (p->nt == 2u) ? 0u : 7u;
    }
    if (p->nt < ((kind == PRN_VD_COUNTER) ? 6u : 4u)
        || (kind == PRN_VD_RESULT && p->nt > 3u + PRN_RESULTS)) {
        return 7u;
    }
    const int name = word_n(PP_T(p, 2), p->len[2], pp_fields);
    const int f = (name < 0) ? -1 : field_any(s, name);
    if (f < 0) {
        return 14u;
    }
    s->vd_field = (uint8_t)name;
    s->vd_key = s->field[f].key;
    uint32_t v[2];
    if (kind == PRN_VD_RESULT) {
        for (unsigned i = 3u; i < p->nt; ++i) {
            if (pp_num(p, i, 0u, 0xFFFFFFFFu, &s->result[i - 3u]) != 0u) {
                return 7u;
            }
        }
        s->nresult = (uint8_t)(p->nt - 3u);
        return 0u;
    }
    if (pp_num(p, 3u, 1u, 10u, &v[0]) != 0u
        || pp_num(p, 4u, 0u, 5000u, &v[1]) != 0u) {
        return 7u;
    }
    s->vd_reads = (uint8_t)v[0];
    s->vd_gap_ms = (uint16_t)v[1];
    return send_of(p, 5u, p->nt, &s->vd_send);
}

/**
 * @brief A tmpl line.
 * @param p the state
 * @return 0 or a code
 */
static uint8_t tmpl_line(pp_t *p)
{
    prn_spec_t *s = p->s;
    if (p->nt < 3u) {
        return 7u;
    }
    if (s->ntmpl >= PRN_TMPLS) {
        return 10u;
    }
    prn_tmpl_t *t = &s->tmpl[s->ntmpl];
    uint8_t e = name_tok(p, 1u, t->name);
    if (e != 0u) {
        return e;
    }
    if (tmpl_find(p, 1u) >= 0) {
        return 8u;
    }
    e = pp_tokens(p, 2u, PP_C_TMPL, PRN_TMPL_TOKENS, &t->seq);
    if (e != 0u) {
        return e;
    }
    const size_t len = pp_fixed_len(s, t->seq);
    if (!pp_ranges_ok(s, t->seq, len, len)) {
        return 12u;
    }
    s->ntmpl++;
    return 0u;
}

/**
 * @brief An answer, field or error line.
 * @param p the state
 * @return 0 or a code
 */
static uint8_t answer_line(pp_t *p)
{
    prn_spec_t *s = p->s;
    uint32_t v[2];
    uint8_t key;
    if (pp_is(p, 0u, "answer")) {
        if (p->nt != 3u) {
            return 7u;
        }
        if (!pp_has(p, K_RXKEY)) {
            return 9u;
        }
        if (s->nanswer >= PRN_ANSWERS) {
            return 28u;
        }
        if (pp_hex(PP_T(p, 1), p->len[1], &key, 1u) != 1u) {
            return 7u;
        }
        for (unsigned i = 0; i < s->nanswer; ++i) {
            if (s->answer_key[i] == key) {
                return 8u;
            }
        }
        if (pp_num(p, 2u, (uint32_t)s->key_off + 1u, s->rx_max, &v[0])
            != 0u) {
            return 7u;
        }
        s->answer_key[s->nanswer] = key;
        s->answer_min[s->nanswer++] = (uint8_t)v[0];
        return 0u;
    }
    if (pp_is(p, 0u, "field")) {
        if (p->nt != 5u && p->nt != 6u) {
            return 7u;
        }
        if (s->nfield >= PRN_FIELDS) {
            return 28u;
        }
        if (pp_hex(PP_T(p, 1), p->len[1], &key, 1u) != 1u) {
            return 7u;
        }
        int a = -1;
        for (unsigned i = 0; i < s->nanswer; ++i) {
            a = (s->answer_key[i] == key) ? (int)i : a;
        }
        if (a < 0) {
            return 14u;
        }
        const int name = word_n(PP_T(p, 2), p->len[2], pp_fields);
        if (name < 0) {
            return 7u;
        }
        if (prn_field_find(s, key, (uint8_t)name) >= 0) {
            return 8u;
        }
        const int kind = word_n(PP_T(p, 4), p->len[4], k_fkinds);
        const bool has_n = kind == PRN_FK_BIT || kind == PRN_FK_BYTES;
        if (kind < 0 || has_n != (p->nt == 6u)
            || pp_num(p, 3u, 0u, 127u, &v[0]) != 0u
            || (has_n && pp_num(p, 5u, (kind == PRN_FK_BIT) ? 0u : 1u,
                                (kind == PRN_FK_BIT) ? 7u : 8u, &v[1])
                             != 0u)) {
            return 7u;
        }
        static const uint8_t k_w[] = { 1u, 1u, 2u, 2u, 4u, 4u, 0u };
        const uint32_t w = (kind == PRN_FK_BYTES) ? v[1] : k_w[kind];
        if (v[0] + w > s->answer_min[a]) {
            return 13u;
        }
        prn_field_t *f = &s->field[s->nfield++];
        f->key = key;
        f->name = (uint8_t)name;
        f->off = (uint8_t)v[0];
        f->kind = (uint8_t)kind;
        f->n = has_n ? (uint8_t)v[1] : 0u;
        return 0u;
    }
    /* error */
    if (p->nt != 3u) {
        return 7u;
    }
    if (s->nerror >= PRN_ERRORS) {
        return 28u;
    }
    const int name = word_n(PP_T(p, 1), p->len[1], pp_fields);
    bool bit = false;
    for (unsigned i = 0; name >= 0 && i < s->nfield; ++i) {
        bit = bit || (s->field[i].name == name
                      && s->field[i].kind == PRN_FK_BIT);
    }
    if (!bit) {
        return 14u;
    }
    const uint8_t e = code_tok(p, 2u, &s->error_code[s->nerror]);
    if (e == 0u) {
        s->error_field[s->nerror++] = (uint8_t)name;
    }
    return e;
}

uint8_t pp_line_ext(pp_t *p)
{
    prn_spec_t *s = p->s;
    if (p->in_step) {
        return step_line(p);
    }
    if (pp_is(p, 0u, "tmpl")) {
        return tmpl_line(p);
    }
    if (pp_is(p, 0u, "answer") || pp_is(p, 0u, "field")
        || pp_is(p, 0u, "error")) {
        return answer_line(p);
    }
    if (pp_is(p, 0u, "job")) {
        return job(p);
    }
    if (pp_is(p, 0u, "abort")) {
        return abort_line(p);
    }
    if (pp_is(p, 0u, "verdict")) {
        return verdict(p);
    }
    if (!pp_is(p, 0u, "step")) {
        return 255u;
    }
    if (p->nt != 2u) {
        return 7u;
    }
    if (s->nstep >= PRN_STEPS) {
        return 28u;
    }
    prn_step_t *st = &s->step[s->nstep];
    const uint8_t e = name_tok(p, 1u, st->name);
    if (e != 0u) {
        return e;
    }
    if (step_find(s, st->name, strlen(st->name)) >= 0) {
        memset(st->name, 0, sizeof st->name);
        return 8u;
    }
    st->tries = 1u;
    st->else_err = PRN_SILENT;
    st->relink = PRN_RELINK_NONE;
    s->nstep++;
    p->in_step = true;
    p->step_line = p->lineno;
    p->sub = 0u;
    return 0u;
}

uint8_t pp_finish_ext(pp_t *p, uint16_t *line)
{
    prn_spec_t *s = p->s;
    for (unsigned i = 0; i < s->nstep; ++i) {
        prn_step_t *st = &s->step[i];
        if (st->relink_name[0] == '\0' || st->relink == PRN_RELINK_VERDICT) {
            continue;
        }
        const int k = step_find(s, st->relink_name, strlen(st->relink_name));
        bool in = false;
        for (unsigned j = 0; k >= 0 && j < s->nprint; ++j) {
            in = in || s->print[j] == (uint8_t)k;
        }
        if (!in) {
            *line = p->relink_line[i];
            return 25u;
        }
        st->relink = (uint8_t)k;
    }
    const bool fields = field_any(s, PRN_F_ROLL_W) >= 0
                        && field_any(s, PRN_F_ROLL_H) >= 0;
    if (s->roll_fixed ? (s->nroll == 0u) : !fields) {
        *line = p->rsource_line;
        return 27u;
    }
    return 0u;
}
