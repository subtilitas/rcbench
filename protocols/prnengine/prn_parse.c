/**
 * @file
 * @brief The printer file reader: the lexer, the line loop, the identity,
 *        GATT, head, label, roll and stream keys, and the checks at `end`
 *        (prn_spec.h).  Templates, answers, steps and jobs are in
 *        prn_parse_ext.c.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "prn_job.h"
#include "prn_parse_int.h"

/** @brief Edge words in prn_edge_t order. */
static const char *const k_edges[] = { "top", "bottom", "left", "right",
                                       NULL };

bool pp_is(const pp_t *p, unsigned i, const char *word)
{
    const size_t n = strlen(word);
    return i < p->nt && p->len[i] == n && memcmp(PP_T(p, i), word, n) == 0;
}

/**
 * @brief The value of a hexadecimal digit.
 * @param c the character
 * @return 0 to 15, or -1
 */
static int hexd(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

uint8_t pp_num_text(const char *t, size_t n, uint32_t lo, uint32_t hi,
                    uint32_t *v)
{
    if (n == 0u || n > 10u) {
        return 7u;
    }
    uint64_t x = 0u;
    if (n > 2u && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        for (size_t i = 2u; i < n; ++i) {
            const int d = hexd(t[i]);
            if (d < 0) {
                return 7u;
            }
            x = x * 16u + (uint64_t)d;
        }
    } else {
        for (size_t i = 0u; i < n; ++i) {
            if (t[i] < '0' || t[i] > '9') {
                return 7u;
            }
            x = x * 10u + (uint64_t)(t[i] - '0');
        }
    }
    if (x < lo || x > hi) {
        return 7u;
    }
    *v = (uint32_t)x;
    return 0u;
}

uint8_t pp_num(const pp_t *p, unsigned i, uint32_t lo, uint32_t hi,
               uint32_t *v)
{
    if (i >= p->nt) {
        return 7u;
    }
    return pp_num_text(PP_T(p, i), p->len[i], lo, hi, v);
}

unsigned pp_hex(const char *t, size_t n, uint8_t *out, unsigned max)
{
    if (n < 2u || n % 2u != 0u || n / 2u > max) {
        return 0u;
    }
    for (size_t i = 0; i < n; i += 2u) {
        const int hi = hexd(t[i]);
        const int lo = hexd(t[i + 1u]);
        if (hi < 0 || lo < 0) {
            return 0u;
        }
        out[i / 2u] = (uint8_t)(hi * 16 + lo);
    }
    return (unsigned)(n / 2u);
}

size_t pp_find(const char *t, size_t n, char c)
{
    size_t i = 0u;
    while (i < n && t[i] != c) {
        ++i;
    }
    return i;
}

/* pp_tok_word(): prn_parse_int.h */
int pp_tok_word(const pp_t *p, unsigned i, const char *const *words)
{
    for (int k = 0; words[k] != NULL; ++k) {
        if (pp_is(p, i, words[k])) {
            return k;
        }
    }
    return -1;
}

uint8_t pp_once(pp_t *p, pp_key_t k)
{
    const uint64_t bit = (uint64_t)1u << k;
    if ((p->seen & bit) != 0u) {
        return 8u;
    }
    p->seen |= bit;
    return 0u;
}

uint8_t pp_once_n(pp_t *p, pp_key_t k, unsigned nt, bool at_least)
{
    const uint8_t e = pp_once(p, k);
    if (e != 0u) {
        return e;
    }
    return ((at_least ? p->nt < nt : p->nt != nt)) ? 7u : 0u;
}

bool pp_has(const pp_t *p, pp_key_t k)
{
    return (p->seen & ((uint64_t)1u << k)) != 0u;
}

/**
 * @brief Read a UUID: 4 hexadecimal digits, or 8-4-4-4-12.
 * @param p the state
 * @param i the token
 * @param u the UUID
 * @return 0 or 7
 */
static uint8_t uuid(const pp_t *p, unsigned i, prn_uuid_t *u)
{
    const char *t = PP_T(p, i);
    if (p->len[i] == 4u) {
        u->len = (uint8_t)pp_hex(t, 4u, u->b, 2u);
        return (u->len == 2u) ? 0u : 7u;
    }
    if (p->len[i] != 36u || t[8] != '-' || t[13] != '-' || t[18] != '-'
        || t[23] != '-') {
        return 7u;
    }
    static const uint8_t k_at[5] = { 0u, 9u, 14u, 19u, 24u };
    static const uint8_t k_n[5] = { 8u, 4u, 4u, 4u, 12u };
    unsigned o = 0u;
    for (unsigned k = 0; k < 5u; ++k) {
        const unsigned got = pp_hex(t + k_at[k], k_n[k], u->b + o, 16u - o);
        if (got != k_n[k] / 2u) {
            return 7u;
        }
        o += got;
    }
    u->len = 16u;
    return 0u;
}

/**
 * @brief Copy text into a field after checking its characters.
 * @param dst   the field, max + 1 bytes
 * @param t     the text
 * @param n     its length
 * @param max   its largest length
 * @param kind  0 id (a-z 0-9 -), 1 printable, 2 licence (A-Z a-z 0-9 . + -)
 * @return 0 or 29
 */
static uint8_t text(char *dst, const char *t, size_t n, size_t max,
                    unsigned kind)
{
    if (n == 0u || n > max) {
        return 29u;
    }
    for (size_t i = 0; i < n; ++i) {
        const char c = t[i];
        bool ok;
        if (kind == 0u) {
            ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        } else if (kind == 1u) {
            ok = c >= 0x20 && c <= 0x7E;
        } else {
            ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                 || (c >= '0' && c <= '9') || c == '.' || c == '+'
                 || c == '-';
        }
        if (!ok) {
            return 29u;
        }
    }
    memcpy(dst, t, n);
    dst[n] = '\0';
    return 0u;
}

/**
 * @brief The text from token @p i to the line's last token.
 * @param p the state
 * @param i the first token
 * @param n set to its length
 * @return the text
 */
static const char *rest(const pp_t *p, unsigned i, size_t *n)
{
    const unsigned last = p->nt - 1u;
    *n = (size_t)(p->at[last] + p->len[last] - p->at[i]);
    return PP_T(p, i);
}

/** @brief The head's width in whole mm. */
static uint32_t head_mm(const prn_spec_t *s)
{
    return (s->dots_mm != 0u) ? s->head_dots / s->dots_mm : 0u;
}

/** @brief Whether two edges are at right angles. */
static bool square(uint8_t a, uint8_t b)
{
    return (a < 2u) != (b < 2u);
}

/**
 * @brief The identity keys: id, name, version, licence, source.
 * @param p the state
 * @return 0, a code, or 255 for another key
 */
static uint8_t identity(pp_t *p)
{
    prn_spec_t *s = p->s;
    uint8_t e;
    uint32_t v = 0u;
    size_t n;
    if (pp_is(p, 0, "id")) {
        e = pp_once_n(p, K_ID, 2u, false);
        if (e != 0u) {
            return e;
        }
        e = text(s->id, PP_T(p, 1), p->len[1], PRN_ID_MAX, 0u);
        if (e != 0u) {
            return e;
        }
        if (p->fname != NULL) {
            const size_t k = strlen(s->id);
            if (strlen(p->fname) != k + 4u
                || memcmp(p->fname, s->id, k) != 0
                || memcmp(p->fname + k, ".prn", 4u) != 0) {
                return 17u;
            }
        }
        return 0u;
    }
    if (pp_is(p, 0, "name")) {
        e = pp_once_n(p, K_NAME, 2u, true);
        if (e != 0u) {
            return e;
        }
        const char *t = rest(p, 1u, &n);
        return text(s->name, t, n, PRN_NAME_MAX, 1u);
    }
    if (pp_is(p, 0, "version")) {
        e = pp_once_n(p, K_VERSION, 2u, false);
        if (e != 0u) {
            return e;
        }
        e = pp_num(p, 1u, 1u, 65535u, &v);
        s->version = (uint16_t)v;
        return e;
    }
    if (pp_is(p, 0, "licence")) {
        e = pp_once_n(p, K_LICENCE, 2u, false);
        if (e != 0u) {
            return e;
        }
        return text(s->licence, PP_T(p, 1), p->len[1], PRN_LICENCE_MAX, 2u);
    }
    if (pp_is(p, 0, "source")) {
        if (p->nt != 2u) {
            return 7u;
        }
        if (s->nsource >= PRN_SOURCES) {
            return 30u;
        }
        e = text(s->source[s->nsource], PP_T(p, 1), p->len[1],
                 PRN_SOURCE_MAX, 1u);
        s->nsource = (uint8_t)(s->nsource + (e == 0u ? 1u : 0u));
        return e;
    }
    return 255u;
}

/**
 * @brief A match line.
 * @param p the state
 * @return 0 or a code
 */
static uint8_t match(pp_t *p)
{
    static const char *const k_kinds[] = { "oui", "name", "iname", "svc",
                                           NULL };
    prn_spec_t *s = p->s;
    if (p->nt < 3u) {
        return (p->nt == 2u && pp_tok_word(p, 1u, k_kinds) < 0) ? 6u : 7u;
    }
    const int kind = pp_tok_word(p, 1u, k_kinds);
    if (kind < 0) {
        return 6u;
    }
    const unsigned cap = (kind == PRN_M_SVC) ? PRN_MATCH_SVC : PRN_MATCH_EACH;
    if (p->nmatch_kind[kind] >= cap || s->nmatch >= PRN_MATCHES) {
        return 30u;
    }
    prn_match_t *m = &s->match[s->nmatch];
    m->kind = (uint8_t)kind;
    if (kind == PRN_M_OUI) {
        const char *t = PP_T(p, 2);
        if (p->nt != 3u || p->len[2] != 8u || t[2] != ':' || t[5] != ':'
            || pp_hex(t, 2u, m->b, 1u) != 1u
            || pp_hex(t + 3, 2u, m->b + 1, 1u) != 1u
            || pp_hex(t + 6, 2u, m->b + 2, 1u) != 1u) {
            return 7u;
        }
        m->len = 3u;
    } else if (kind == PRN_M_SVC) {
        prn_uuid_t u;
        if (p->nt != 3u || uuid(p, 2u, &u) != 0u) {
            return 7u;
        }
        memcpy(m->b, u.b, u.len);
        m->len = u.len;
    } else {
        size_t n;
        const char *t = rest(p, 2u, &n);
        char buf[PRN_NAME_MAX + 1u];
        const uint8_t e = text(buf, t, n, PRN_NAME_MAX, 1u);
        if (e != 0u) {
            return e;
        }
        memcpy(m->b, buf, n);
        m->len = (uint8_t)n;
    }
    p->nmatch_kind[kind]++;
    s->nmatch++;
    return 0u;
}

/**
 * @brief The GATT and link keys: service, chr, cccd, mtu_min, conn.
 * @param p the state
 * @return 0, a code, or 255 for another key
 */
static uint8_t gatt(pp_t *p)
{
    static const char *const k_roles[] = { "cmd", "data", "rx", "other",
                                           NULL };
    static const char *const k_props[] = { "write", "write_nr", "notify",
                                           NULL };
    prn_spec_t *s = p->s;
    uint8_t e;
    uint32_t v[4] = { 0u, 0u, 0u, 0u };
    if (pp_is(p, 0, "service")) {
        e = pp_once_n(p, K_SERVICE, 2u, false);
        if (e != 0u) {
            return e;
        }
        return uuid(p, 1u, &s->service);
    }
    if (pp_is(p, 0, "chr")) {
        if (p->nt < 4u) {
            return 7u;
        }
        const int r = pp_tok_word(p, 1u, k_roles);
        if (r < 0) {
            return 7u;
        }
        if (s->chr[r].used) {
            return 8u;
        }
        if (r == PRN_CHR_CMD) {
            (void)pp_once(p, K_CMD);
        }
        prn_chr_t *c = &s->chr[r];
        e = uuid(p, 2u, &c->uuid);
        if (e != 0u) {
            return e;
        }
        for (unsigned i = 3u; i < p->nt; ++i) {
            const int k = pp_tok_word(p, i, k_props);
            if (k < 0 || (c->props & (1u << k)) != 0u) {
                return 7u;
            }
            c->props = (uint8_t)(c->props | (1u << k));
        }
        c->used = true;
        return 0u;
    }
    if (pp_is(p, 0, "cccd")) {
        e = pp_once_n(p, K_CCCD, 2u, false);
        if (e != 0u) {
            return e;
        }
        if (!pp_is(p, 1u, "next") && !pp_is(p, 1u, "discover")) {
            return 7u;
        }
        s->cccd_next = pp_is(p, 1u, "next");
        return 0u;
    }
    if (pp_is(p, 0, "mtu_min")) {
        e = pp_once_n(p, K_MTU, 2u, false);
        if (e != 0u) {
            return e;
        }
        e = pp_num(p, 1u, 23u, 247u, &v[0]);
        s->mtu_min = (uint8_t)v[0];
        return e;
    }
    if (pp_is(p, 0, "conn")) {
        e = pp_once_n(p, K_CONN, 5u, false);
        if (e != 0u) {
            return e;
        }
        if (pp_num(p, 1u, 6u, 3200u, &v[0]) != 0u
            || pp_num(p, 2u, v[0], 3200u, &v[1]) != 0u
            || pp_num(p, 3u, 0u, 499u, &v[2]) != 0u
            || pp_num(p, 4u, 10u, 3200u, &v[3]) != 0u
            || v[3] * 4u <= (1u + v[2]) * v[1]) {
            return 7u;
        }
        for (unsigned i = 0; i < 4u; ++i) {
            s->conn[i] = (uint16_t)v[i];
        }
        return 0u;
    }
    return 255u;
}

/**
 * @brief The head keys: head_dots, dots_mm, line, density.
 * @param p the state
 * @return 0, a code, or 255 for another key
 */
static uint8_t head(pp_t *p)
{
    prn_spec_t *s = p->s;
    uint8_t e;
    uint32_t v = 0u;
    if (pp_is(p, 0, "head_dots")) {
        e = pp_once_n(p, K_HEAD, 2u, false);
        if (e != 0u) {
            return e;
        }
        e = pp_num(p, 1u, 8u, 1024u, &v);
        s->head_dots = (uint16_t)v;
        return e;
    }
    if (pp_is(p, 0, "dots_mm")) {
        e = pp_once_n(p, K_DOTS, 2u, false);
        if (e != 0u) {
            return e;
        }
        e = pp_num(p, 1u, 8u, 8u, &v);
        s->dots_mm = (uint8_t)v;
        return e;
    }
    if (pp_is(p, 0, "line")) {
        e = pp_once_n(p, K_LINE, 5u, false);
        if (e != 0u) {
            return e;
        }
        if (!pp_has(p, K_HEAD)) {
            return 9u;
        }
        if (pp_num(p, 1u, (s->head_dots + 7u) / 8u, PRN_LINE_BYTES, &v)
            != 0u) {
            return 7u;
        }
        s->line_bytes = (uint8_t)v;
        if ((!pp_is(p, 2u, "lsb") && !pp_is(p, 2u, "msb"))
            || (!pp_is(p, 3u, "1") && !pp_is(p, 3u, "0"))
            || (!pp_is(p, 4u, "no") && !pp_is(p, 4u, "yes"))) {
            return 7u;
        }
        s->line_msb = pp_is(p, 2u, "msb");
        s->line_black0 = pp_is(p, 3u, "0");
        s->line_reverse = pp_is(p, 4u, "yes");
        return 0u;
    }
    if (pp_is(p, 0, "density")) {
        e = pp_once_n(p, K_DENSITY, 2u, false);
        if (e != 0u) {
            return e;
        }
        e = pp_num(p, 1u, 0u, 255u, &v);
        s->density = (uint8_t)v;
        return e;
    }
    return 255u;
}

/**
 * @brief The label keys: feed, dot0, align, margin.
 * @param p the state
 * @return 0 or a code
 */
static uint8_t label(pp_t *p)
{
    prn_spec_t *s = p->s;
    uint8_t e;
    uint32_t v[2] = { 0u, 0u };
    if (p->nt < 2u) {
        return 6u;
    }
    if (pp_is(p, 1u, "feed") || pp_is(p, 1u, "dot0")) {
        const bool feed = pp_is(p, 1u, "feed");
        e = pp_once_n(p, feed ? K_FEED : K_DOT0, 3u, false);
        if (e != 0u) {
            return e;
        }
        const int edge = pp_tok_word(p, 2u, k_edges);
        if (edge < 0) {
            return 7u;
        }
        if (feed) {
            s->feed = (uint8_t)edge;
        } else {
            s->dot0 = (uint8_t)edge;
            p->dot0_line = p->lineno;
        }
        if (pp_has(p, K_FEED) && pp_has(p, K_DOT0)
            && !square(s->feed, s->dot0)) {
            return 21u;
        }
        return 0u;
    }
    if (pp_is(p, 1u, "align")) {
        e = pp_once_n(p, K_ALIGN, 3u, false);
        if (e != 0u) {
            return e;
        }
        if (pp_is(p, 2u, "center")) {
            s->align_center = true;
            return 0u;
        }
        if (!pp_has(p, K_HEAD)) {
            return 9u;
        }
        e = pp_num(p, 2u, 0u, s->head_dots - 1u, &v[0]);
        s->align_dot = (uint16_t)v[0];
        return e;
    }
    if (pp_is(p, 1u, "margin")) {
        e = pp_once_n(p, K_MARGIN, 4u, false);
        if (e != 0u) {
            return e;
        }
        if (pp_num(p, 2u, 0u, 64u, &v[0]) != 0u
            || pp_num(p, 3u, 0u, 64u, &v[1]) != 0u) {
            return 7u;
        }
        s->mlead = (uint8_t)v[0];
        s->mtrail = (uint8_t)v[1];
        return 0u;
    }
    return 6u;
}

/**
 * @brief The roll keys: source, fixed, limits.
 * @param p the state
 * @return 0 or a code
 */
static uint8_t roll(pp_t *p)
{
    prn_spec_t *s = p->s;
    uint8_t e;
    uint32_t v[4] = { 0u, 0u, 0u, 0u };
    if (pp_is(p, 1u, "source")) {
        e = pp_once_n(p, K_RSOURCE, 3u, false);
        if (e != 0u) {
            return e;
        }
        if (!pp_is(p, 2u, "record") && !pp_is(p, 2u, "fixed")) {
            return 7u;
        }
        s->roll_fixed = pp_is(p, 2u, "fixed");
        p->rsource_line = p->lineno;
        return 0u;
    }
    const bool fixed = pp_is(p, 1u, "fixed");
    if (!fixed && !pp_is(p, 1u, "limits")) {
        return 6u;
    }
    e = fixed ? 0u : pp_once(p, K_RLIMITS);
    if (e != 0u) {
        return e;
    }
    if (p->nt != (fixed ? 5u : 6u)) {
        return 7u;
    }
    if (!pp_has(p, K_HEAD) || !pp_has(p, K_DOTS)) {
        return 9u;
    }
    const uint32_t wmax = head_mm(s);
    if (fixed) {
        if (s->nroll >= PRN_ROLLS) {
            return 30u;
        }
        if (pp_num(p, 2u, 10u, wmax, &v[0]) != 0u
            || pp_num(p, 3u, 10u, 200u, &v[1]) != 0u
            || pp_num(p, 4u, 0u, 20u, &v[2]) != 0u) {
            return 7u;
        }
        prn_roll_t *r = &s->roll[s->nroll++];
        r->w = (uint8_t)v[0];
        r->h = (uint8_t)v[1];
        r->gap = (uint8_t)v[2];
        return 0u;
    }
    if (pp_num(p, 2u, 10u, wmax, &v[0]) != 0u
        || pp_num(p, 3u, v[0], wmax, &v[1]) != 0u
        || pp_num(p, 4u, 10u, 200u, &v[2]) != 0u
        || pp_num(p, 5u, v[2], 200u, &v[3]) != 0u) {
        return 7u;
    }
    for (unsigned i = 0; i < 4u; ++i) {
        s->roll_lim[i] = (uint8_t)v[i];
    }
    return 0u;
}

/**
 * @brief The checks at `end`.
 * @param p    the state
 * @param line set to the line of a refusal other than `end`'s
 * @return 0 or a code
 */
static uint8_t finish(pp_t *p, uint16_t *line)
{
    static const uint8_t k_required[] = {
        K_ID, K_NAME, K_VERSION, K_SERVICE, K_CMD, K_MTU, K_CONN, K_HEAD,
        K_DOTS, K_LINE, K_FEED, K_DOT0, K_ALIGN, K_MARGIN, K_RSOURCE,
        K_RLIMITS, K_WCMD, K_WDATA, K_RXJOIN, K_RXKEY, K_QUERY, K_PRINT,
        K_VERDICT,
    };
    prn_spec_t *s = p->s;
    for (size_t i = 0; i < sizeof k_required; ++i) {
        if (!pp_has(p, (pp_key_t)k_required[i])) {
            return 9u;
        }
    }
    if (!pp_has(p, K_CFRAME) && !pp_has(p, K_ROWS)) {
        return 9u;
    }
    const unsigned pg = pp_has(p, K_PSIZE) + pp_has(p, K_PLINES)
                        + pp_has(p, K_PHEAD) + pp_has(p, K_PFLAGS);
    const unsigned ck = pp_has(p, K_CSIZE) + pp_has(p, K_CPAD)
                        + pp_has(p, K_CMAX) + pp_has(p, K_CFRAME);
    if ((pg != 0u && pg != 4u) || (ck != 0u && ck != 4u)) {
        return 9u;
    }
    for (unsigned i = 0; i < s->nstep; ++i) {
        if (s->step[i].relink_name[0] != '\0' && !pp_has(p, K_RELINKMS)) {
            return 9u;
        }
    }
    if (s->nmatch == 0u) {
        return 20u;
    }
    if (!pp_has(p, K_ANSWERMS)) {
        s->answer_ms = 1000u;
    }
    const uint8_t e = pp_finish_ext(p, line);
    if (e != 0u) {
        return e;
    }
    return prn_spec_check(s);
}

/**
 * @brief Split the line into tokens, after its comment is cut.
 * @param p the state
 * @return 0, or 7 for more than PP_TOKENS tokens
 */
static uint8_t split(pp_t *p)
{
    const size_t len = strlen(p->line);
    p->line[pp_find(p->line, len, '#')] = '\0';
    p->nt = 0u;
    unsigned i = 0u;
    for (;;) {
        while (p->line[i] == ' ' || p->line[i] == '\t') {
            ++i;
        }
        if (p->line[i] == '\0') {
            return 0u;
        }
        if (p->nt == PP_TOKENS) {
            return 7u;
        }
        p->at[p->nt] = (uint8_t)i;
        while (p->line[i] != '\0' && p->line[i] != ' ' && p->line[i] != '\t') {
            ++i;
        }
        p->len[p->nt] = (uint8_t)(i - p->at[p->nt]);
        p->nt++;
    }
}

/**
 * @brief One line of the file.
 * @param p      the state
 * @param header set once the header line is read
 * @param ended  set at `end`
 * @return 0 or a code
 */
static uint8_t one_line(pp_t *p, bool *header, bool *ended)
{
    const bool indented = (p->line[0] == ' ' && p->line[1] == ' ')
                          || p->line[0] == '\t';
    uint8_t e = split(p);
    if (e != 0u || p->nt == 0u) {
        return e;
    }
    if (*ended) {
        return 16u;
    }
    if (!*header) {
        if (p->nt != 4u || !pp_is(p, 0u, "rcbench")
            || !pp_is(p, 1u, "printer,") || !pp_is(p, 2u, "format")
            || !pp_is(p, 3u, "1")) {
            return 5u;
        }
        *header = true;
        return 0u;
    }
    if (indented) {
        return p->in_step ? pp_line_ext(p) : 6u;
    }
    e = p->in_step ? pp_close_step(p) : 0u;
    if (e != 0u) {
        return e;
    }
    if (pp_is(p, 0u, "end")) {
        if (p->nt != 1u) {
            return 7u;
        }
        *ended = true;
        uint16_t line = 0u;
        e = finish(p, &line);
        if (e != 0u && line != 0u) {
            p->lineno = line;
        }
        return e;
    }
    if (pp_is(p, 0u, "match")) {
        return match(p);
    }
    if (pp_is(p, 0u, "label")) {
        return label(p);
    }
    if (pp_is(p, 0u, "roll")) {
        return roll(p);
    }
    if (pp_is(p, 0u, "page")) {
        return pp_page(p);
    }
    if (pp_is(p, 0u, "rx")) {
        return pp_rx(p);
    }
    e = identity(p);
    e = (e == 255u) ? gatt(p) : e;
    e = (e == 255u) ? head(p) : e;
    e = (e == 255u) ? pp_frames(p) : e;
    e = (e == 255u) ? pp_writes(p) : e;
    e = (e == 255u) ? pp_times(p) : e;
    e = (e == 255u) ? pp_line_ext(p) : e;
    return (e == 255u) ? 6u : e;
}

bool prn_parse(const char *txt, size_t n, const char *name, prn_spec_t *out,
               prn_perr_t *err)
{
    pp_t p;
    memset(&p, 0, sizeof p);
    memset(out, 0, sizeof *out);
    p.s = out;
    p.fname = name;
    err->code = 0u;
    err->line = 0u;
    uint8_t code = 0u;
    bool header = false;
    bool ended = false;
    if (txt == NULL || n == 0u || n > PRN_FILE_MAX) {
        code = 1u;
    }
    size_t pos = 0u;
    while (code == 0u && pos < n) {
        p.lineno++;
        size_t eol = pos;
        while (eol < n && txt[eol] != '\n') {
            ++eol;
        }
        size_t end = eol;
        if (end > pos && eol < n && txt[end - 1u] == '\r') {
            --end;
        }
        if (p.lineno > PRN_LINES_MAX) {
            code = 4u;
            break;
        }
        for (size_t i = pos; i < end && code == 0u; ++i) {
            const unsigned char c = (unsigned char)txt[i];
            if (c != 0x09u && (c < 0x20u || c > 0x7Eu)) {
                code = 2u;
            }
        }
        if (code == 0u && end - pos > PRN_LINE_MAX) {
            code = 3u;
        }
        if (code != 0u) {
            break;
        }
        memcpy(p.line, txt + pos, end - pos);
        p.line[end - pos] = '\0';
        pos = eol + 1u;
        code = one_line(&p, &header, &ended);
    }
    if (code == 0u && !header) {
        code = 5u;
        p.lineno = 0u;
    } else if (code == 0u && !ended) {
        code = 16u;
    }
    if (code == 0u) {
        return true;
    }
    memset(out, 0, sizeof *out);
    err->code = code;
    err->line = (code == 1u) ? 0u : p.lineno;
    return false;
}
