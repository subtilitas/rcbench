/*
 * Read one ESC profile from JSON.  See esc_profile.h.
 *
 * Two stages.  The text is first cut into tokens: one per value, each with
 * the index one past its last descendant, so a reader skips a subtree in
 * one step and never recurses to find a key.  Then the profile is decoded
 * from the tokens twice: once with no block, which only adds up the bytes
 * every string and array will need, and once into a block of exactly that
 * size.  One allocation per profile, and free() of it releases everything.
 *
 * The rules are tools/gen_esc_profiles.py's.  The host suite parses every
 * profile of record and compares it with the generated table, so a rule
 * that differs between the two fails there.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Nesting deeper than any profile needs (five levels) is refused rather than
 * followed, so a hostile file cannot run the stack down. */
#define MAX_DEPTH 16

/* No profile object has more than 14 members.  The duplicate check compares
 * each key with those before it, so this bounds it at 64 x 63 / 2
 * comparisons an object rather than letting a 64 KiB file of short keys ask
 * for tens of millions. */
#define MAX_MEMBERS 64u

typedef enum { T_OBJ, T_ARR, T_STR, T_NUM, T_TRUE, T_FALSE, T_NULL } ttype_t;

typedef struct {
    ttype_t  type;
    uint32_t start;     /* first byte; for a string, after the quote      */
    uint32_t end;       /* one past the last byte; before the quote       */
    uint32_t size;      /* members of an object, elements of an array     */
    uint32_t next;      /* the token after this value and its descendants */
} tok_t;

/* Why the text was refused, beyond its not being JSON. */
typedef enum {
    LEX_SYNTAX = 0,
    LEX_DUP,            /* a key an object already had            */
    LEX_NUL,            /* \u0000, which would end a C string     */
    LEX_SURROGATE,      /* half a surrogate pair                  */
    LEX_UTF8,           /* bytes that are not UTF-8               */
    LEX_MEMBERS,        /* an object with more than MAX_MEMBERS   */
} lex_why_t;

typedef struct {
    const char *s;
    uint32_t    len;
    uint32_t    pos;
    tok_t      *tok;    /* NULL: count only */
    uint32_t    n;
    uint32_t    cap;
    lex_why_t   why;    /* what stopped the read, past a syntax error */
} lex_t;

/* ------------------------------------------------------------ the tokens */

static void ws(lex_t *l)
{
    while (l->pos < l->len) {
        const char c = l->s[l->pos];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            return;
        }
        l->pos++;
    }
}

static int64_t add(lex_t *l, ttype_t t, uint32_t start)
{
    if (l->tok != NULL) {
        if (l->n >= l->cap) {
            return -1;
        }
        l->tok[l->n] = (tok_t){ t, start, start, 0u, 0u };
    }
    return (int64_t)l->n++;
}

static void close_tok(lex_t *l, int64_t i, uint32_t end, uint32_t size)
{
    if (l->tok != NULL) {
        l->tok[i].end  = end;
        l->tok[i].size = size;
        l->tok[i].next = l->n;
    }
}

static bool lex_value(lex_t *l, int depth);

static int hexval(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

/* The four hex digits at @p s, or -1. */
static int32_t hex4(const char *s)
{
    int32_t v = 0;
    for (int k = 0; k < 4; ++k) {
        const int h = hexval(s[k]);
        if (h < 0) {
            return -1;
        }
        v = (v << 4) | h;
    }
    return v;
}

/*
 * The length of the well-formed UTF-8 sequence at @p s, or 0: no stray
 * continuation byte, no overlong form, no encoded surrogate, nothing past
 * U+10FFFF.  What Python's UTF-8 decoder takes, so a file the generator
 * would refuse to read is refused here too.
 */
static uint32_t utf8_len(const unsigned char *s, uint32_t avail)
{
    const unsigned char c = s[0];
    uint32_t n;
    unsigned char lo = 0x80u, hi = 0xBFu;      /* the second byte's range */
    if (c >= 0xC2u && c <= 0xDFu) {
        n = 2u;
    } else if (c >= 0xE0u && c <= 0xEFu) {
        n = 3u;
        if (c == 0xE0u) { lo = 0xA0u; }         /* not overlong */
        if (c == 0xEDu) { hi = 0x9Fu; }         /* not a surrogate */
    } else if (c >= 0xF0u && c <= 0xF4u) {
        n = 4u;
        if (c == 0xF0u) { lo = 0x90u; }         /* not overlong */
        if (c == 0xF4u) { hi = 0x8Fu; }         /* not past U+10FFFF */
    } else {
        return 0u;
    }
    if (avail < n || s[1] < lo || s[1] > hi) {
        return 0u;
    }
    for (uint32_t k = 2u; k < n; ++k) {
        if (s[k] < 0x80u || s[k] > 0xBFu) {
            return 0u;
        }
    }
    return n;
}

/*
 * A string, with every escape checked here, in the one pass that sees every
 * string -- including those in fields the panel never reads.  \u0000 is
 * refused (it would end a C string early), and so is half a surrogate pair:
 * json.loads() keeps a lone half, which no UTF-8 can carry.  What passes is
 * what decode_unit() decodes without looking again.
 */
static bool lex_string(lex_t *l)
{
    const uint32_t start = ++l->pos;            /* past the quote */
    while (l->pos < l->len) {
        const unsigned char c = (unsigned char)l->s[l->pos];
        if (c == '"') {
            const int64_t i = add(l, T_STR, start);
            if (i < 0) {
                return false;
            }
            close_tok(l, i, l->pos, 0u);
            l->pos++;
            return true;
        }
        if (c < 0x20u) {
            return false;                       /* raw control character */
        }
        if (c >= 0x80u) {
            const uint32_t n = utf8_len((const unsigned char *)l->s + l->pos,
                                        l->len - l->pos);
            if (n == 0u) {
                l->why = LEX_UTF8;
                return false;
            }
            l->pos += n;
            continue;
        }
        if (c != '\\') {
            l->pos++;
            continue;
        }
        if (l->pos + 1u >= l->len) {
            return false;
        }
        const char e = l->s[l->pos + 1u];
        if (e != 'u') {
            if (strchr("\"\\/bfnrt", e) == NULL || e == '\0') {
                return false;
            }
            l->pos += 2u;
            continue;
        }
        if (l->len - l->pos < 6u) {
            return false;
        }
        const int32_t cp = hex4(l->s + l->pos + 2u);
        if (cp < 0) {
            return false;
        }
        if (cp == 0) {
            l->why = LEX_NUL;
            return false;
        }
        if (cp >= 0xDC00 && cp <= 0xDFFF) {
            l->why = LEX_SURROGATE;             /* a low half first */
            return false;
        }
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            const int32_t lo = (l->len - l->pos >= 12u
                                && l->s[l->pos + 6u] == '\\'
                                && l->s[l->pos + 7u] == 'u')
                                   ? hex4(l->s + l->pos + 8u) : -1;
            if (lo < 0xDC00 || lo > 0xDFFF) {
                l->why = LEX_SURROGATE;         /* a high half alone */
                return false;
            }
            l->pos += 6u;
        }
        l->pos += 6u;
    }
    return false;                               /* no closing quote */
}

/*
 * The UTF-8 bytes of the character at s[*i], into @p out; *i moves past it.
 * A raw byte is itself (UTF-8 in the file passes through a byte at a time);
 * an escape is decoded, a surrogate pair into one four-byte character.  Only
 * for text lex_string() accepted.
 */
static unsigned decode_unit(const char *s, uint32_t *i, uint8_t out[4])
{
    const char c = s[*i];
    if (c != '\\') {
        out[0] = (uint8_t)c;
        *i += 1u;
        return 1u;
    }
    const char e = s[*i + 1u];
    *i += 2u;
    switch (e) {
    case 'b': out[0] = '\b'; return 1u;
    case 'f': out[0] = '\f'; return 1u;
    case 'n': out[0] = '\n'; return 1u;
    case 'r': out[0] = '\r'; return 1u;
    case 't': out[0] = '\t'; return 1u;
    case 'u': break;
    default:  out[0] = (uint8_t)e; return 1u;  /* \" \\ \/ */
    }
    uint32_t cp = (uint32_t)hex4(s + *i);
    *i += 4u;
    if (cp >= 0xD800u && cp <= 0xDBFFu) {
        const uint32_t lo = (uint32_t)hex4(s + *i + 2u);
        *i += 6u;
        cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
    }
    if (cp < 0x80u) {
        out[0] = (uint8_t)cp;
        return 1u;
    }
    if (cp < 0x800u) {
        out[0] = (uint8_t)(0xC0u | (cp >> 6));
        out[1] = (uint8_t)(0x80u | (cp & 0x3Fu));
        return 2u;
    }
    if (cp < 0x10000u) {
        out[0] = (uint8_t)(0xE0u | (cp >> 12));
        out[1] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (uint8_t)(0x80u | (cp & 0x3Fu));
        return 3u;
    }
    out[0] = (uint8_t)(0xF0u | (cp >> 18));
    out[1] = (uint8_t)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (uint8_t)(0x80u | (cp & 0x3Fu));
    return 4u;
}

/* Whether two string tokens' texts decode to the same bytes, so "schema" and
 * "sch\u0065ma" are one key, as json.loads() has them.  The two decoded
 * streams are compared byte by byte, not unit by unit: a raw "\xC3\xA9" comes
 * a byte at a time and "\u00e9" as both bytes at once, and they are the same
 * character. */
static bool same_text(const char *s, const tok_t *a, const tok_t *b)
{
    uint32_t i = a->start, j = b->start;
    uint8_t x[4], y[4];
    unsigned xn = 0, xi = 0, yn = 0, yi = 0;
    for (;;) {
        if (xi == xn && i < a->end) {
            xn = decode_unit(s, &i, x);
            xi = 0;
        }
        if (yi == yn && j < b->end) {
            yn = decode_unit(s, &j, y);
            yi = 0;
        }
        const bool x_done = (xi == xn);
        const bool y_done = (yi == yn);
        if (x_done || y_done) {
            return x_done && y_done;
        }
        if (x[xi++] != y[yi++]) {
            return false;
        }
    }
}

/* Whether a string token's text decodes to the plain @p lit. */
static bool text_is(const char *s, const tok_t *a, const char *lit)
{
    uint32_t i = a->start;
    size_t k = 0;
    const size_t n = strlen(lit);
    while (i < a->end) {
        uint8_t x[4];
        const unsigned m = decode_unit(s, &i, x);
        if (k + m > n || memcmp(x, lit + k, m) != 0) {
            return false;
        }
        k += m;
    }
    return k == n;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* -? (0 | [1-9][0-9]*) (. [0-9]+)? ([eE] [+-]? [0-9]+)?, as JSON has it and
 * as the generator's json.loads() takes it, so a file one refuses the other
 * refuses too, whichever field the number sits in. */
static bool lex_number(lex_t *l)
{
    const uint32_t start = l->pos;
    uint32_t i = l->pos;
    const char *s = l->s;
    if (i < l->len && s[i] == '-') {
        i++;
    }
    if (i < l->len && s[i] == '0') {
        i++;
    } else if (i < l->len && s[i] >= '1' && s[i] <= '9') {
        while (i < l->len && is_digit(s[i])) {
            i++;
        }
    } else {
        return false;
    }
    if (i < l->len && s[i] == '.') {
        i++;
        if (i >= l->len || !is_digit(s[i])) {
            return false;
        }
        while (i < l->len && is_digit(s[i])) {
            i++;
        }
    }
    if (i < l->len && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < l->len && (s[i] == '+' || s[i] == '-')) {
            i++;
        }
        if (i >= l->len || !is_digit(s[i])) {
            return false;
        }
        while (i < l->len && is_digit(s[i])) {
            i++;
        }
    }
    l->pos = i;
    const int64_t t = add(l, T_NUM, start);
    if (t < 0) {
        return false;
    }
    close_tok(l, t, l->pos, 0u);
    return true;
}

static bool lex_word(lex_t *l, const char *w, ttype_t t)
{
    const size_t n = strlen(w);
    if (l->len - l->pos < n || memcmp(l->s + l->pos, w, n) != 0) {
        return false;
    }
    const int64_t i = add(l, t, l->pos);
    if (i < 0) {
        return false;
    }
    l->pos += (uint32_t)n;
    close_tok(l, i, l->pos, 0u);
    return true;
}

/*
 * Whether the key just read is one of the @p members before it in the object
 * at token @p obj.  json.loads() keeps the last of two and a reader that
 * stops at the first keeps the first, so a file with a key twice is refused
 * rather than read two ways.  Only once the tokens exist: the counting pass
 * has nothing to compare.
 */
static bool key_seen(const lex_t *l, uint32_t obj, uint32_t members)
{
    const tok_t *k = &l->tok[l->n - 1u];
    uint32_t i = obj + 1u;
    for (uint32_t m = 0; m < members; ++m) {
        if (same_text(l->s, &l->tok[i], k)) {
            return true;
        }
        i = l->tok[i + 1u].next;
    }
    return false;
}

static bool lex_container(lex_t *l, int depth, bool obj)
{
    const int64_t i = add(l, obj ? T_OBJ : T_ARR, l->pos);
    if (i < 0) {
        return false;
    }
    const char close = obj ? '}' : ']';
    uint32_t size = 0;
    l->pos++;
    ws(l);
    if (l->pos < l->len && l->s[l->pos] == close) {
        l->pos++;
        close_tok(l, i, l->pos, 0u);
        return true;
    }
    for (;;) {
        if (obj) {
            if (size >= MAX_MEMBERS) {
                l->why = LEX_MEMBERS;
                return false;
            }
            ws(l);
            if (l->pos >= l->len || l->s[l->pos] != '"' || !lex_string(l)) {
                return false;
            }
            if (l->tok != NULL && key_seen(l, (uint32_t)i, size)) {
                l->why = LEX_DUP;
                return false;
            }
            ws(l);
            if (l->pos >= l->len || l->s[l->pos] != ':') {
                return false;
            }
            l->pos++;
        }
        if (!lex_value(l, depth + 1)) {
            return false;
        }
        size++;
        ws(l);
        if (l->pos >= l->len) {
            return false;
        }
        if (l->s[l->pos] == ',') {
            l->pos++;
            continue;
        }
        if (l->s[l->pos] == close) {
            l->pos++;
            close_tok(l, i, l->pos, size);
            return true;
        }
        return false;
    }
}

static bool lex_value(lex_t *l, int depth)
{
    if (depth > MAX_DEPTH) {
        return false;
    }
    ws(l);
    if (l->pos >= l->len) {
        return false;
    }
    switch (l->s[l->pos]) {
    case '{': return lex_container(l, depth, true);
    case '[': return lex_container(l, depth, false);
    case '"': return lex_string(l);
    case 't': return lex_word(l, "true", T_TRUE);
    case 'f': return lex_word(l, "false", T_FALSE);
    case 'n': return lex_word(l, "null", T_NULL);
    default:  return lex_number(l);
    }
}

/* The whole text is one value with nothing after it but white space. */
static bool lex_all(lex_t *l)
{
    l->pos = 0;
    l->n = 0;
    if (!lex_value(l, 0)) {
        return false;
    }
    ws(l);
    return l->pos == l->len;
}

/* ------------------------------------------------------------ decoding */

/* A model name and where it stands in the file. */
typedef struct {
    const char *name;
    uint32_t    at;
} name_at_t;

typedef struct {
    const char  *s;
    const tok_t *t;
    uint8_t     *base;      /* NULL while sizing */
    size_t       used;
    size_t       cap;       /* the block's size; 0 while sizing */
    char        *err;
    size_t       err_size;
    bool         failed;
    name_at_t   *names;     /* the model names, sorted; NULL while sizing */
    uint32_t     name_count;
} dec_t;

/* The first reason stands.  A macro rather than a variadic function: the
 * arguments go straight to snprintf(), and there is no va_list to get
 * wrong. */
#define FAIL(d, ...)                                                      \
    do {                                                                  \
        if (!(d)->failed) {                                               \
            (d)->failed = true;                                           \
            if ((d)->err != NULL && (d)->err_size > 0u) {                 \
                (void)snprintf((d)->err, (d)->err_size, __VA_ARGS__);     \
            }                                                             \
        }                                                                 \
    } while (0)

static void *take(dec_t *d, size_t n, size_t align)
{
    d->used = (d->used + align - 1u) / align * align;
    void *p = NULL;
    if (d->base != NULL) {
        /* The sizing pass measured this block, so this cannot be short; it
         * is checked so that a mistake there fails here, not past the end. */
        if (d->used + n > d->cap) {
            FAIL(d, "internal: block too small");
        } else {
            p = d->base + d->used;
        }
    }
    d->used += n;
    return p;
}

/* The value of @p key in the object at token @p obj, or -1. */
static int64_t member(const dec_t *d, uint32_t obj, const char *key)
{
    if (d->t[obj].type != T_OBJ) {
        return -1;
    }
    uint32_t i = obj + 1u;
    for (uint32_t m = 0; m < d->t[obj].size; ++m) {
        const uint32_t v = i + 1u;
        if (text_is(d->s, &d->t[i], key)) {
            return (int64_t)v;
        }
        i = d->t[v].next;
    }
    return -1;
}

/* A string token's text, decoded into the block.  Never longer than the
 * text it came from: no escape decodes to more bytes than it is written in. */
static const char *copy_str(dec_t *d, uint32_t ti)
{
    const tok_t *t = &d->t[ti];
    char *out = take(d, (size_t)(t->end - t->start) + 1u, 1u);
    size_t o = 0;
    uint32_t i = t->start;
    while (i < t->end) {
        uint8_t u[4];
        const unsigned n = decode_unit(d->s, &i, u);
        if (out != NULL) {
            memcpy(out + o, u, n);
        }
        o += n;
    }
    if (out != NULL) {
        out[o] = '\0';
    }
    return out;
}

/* A string token's length once decoded, in bytes: UTF-8, as the generator
 * measures it.  The sizing pass has no copy to measure. */
static size_t decoded_len(const dec_t *d, uint32_t ti)
{
    const tok_t *t = &d->t[ti];
    size_t o = 0;
    uint32_t i = t->start;
    while (i < t->end) {
        uint8_t u[4];
        o += decode_unit(d->s, &i, u);
    }
    return o;
}

static const char *get_str(dec_t *d, uint32_t obj, const char *key,
                           const char *where, bool empty_ok, bool null_ok)
{
    const int64_t v = member(d, obj, key);
    if ((v < 0 || d->t[v].type == T_NULL) && null_ok) {
        return (d->base != NULL) ? "" : NULL;
    }
    if (v < 0 || d->t[v].type != T_STR) {
        FAIL(d, "%s%s%s: not a string", where, *where ? "." : "", key);
        return "";
    }
    if (!empty_ok && d->t[v].end == d->t[v].start) {
        FAIL(d, "%s%s%s: empty", where, *where ? "." : "", key);
        return "";
    }
    return copy_str(d, (uint32_t)v);
}

/* A whole number in lo..hi; *out untouched for null when null_ok. */
static bool get_num(dec_t *d, uint32_t obj, const char *key, const char *where,
                    int64_t lo, int64_t hi, bool null_ok, int64_t *out)
{
    const int64_t v = member(d, obj, key);
    if (v < 0 || d->t[v].type == T_NULL) {
        if (!null_ok) {
            FAIL(d, "%s%s%s: missing", where, *where ? "." : "", key);
        }
        return false;
    }
    const tok_t *t = &d->t[v];
    if (t->type != T_NUM) {
        FAIL(d, "%s%s%s: not a whole number", where, *where ? "." : "", key);
        return false;
    }
    int64_t n = 0;
    bool neg = false;
    uint32_t i = t->start;
    if (d->s[i] == '-') {
        neg = true;
        i++;
    }
    if (i == t->end) {
        FAIL(d, "%s%s%s: not a whole number", where, *where ? "." : "", key);
        return false;
    }
    for (; i < t->end; ++i) {
        const char c = d->s[i];
        if (c < '0' || c > '9' || n > 100000000) {
            FAIL(d, "%s%s%s: not a whole number", where, *where ? "." : "", key);
            return false;
        }
        n = n * 10 + (c - '0');
    }
    if (neg) {
        n = -n;
    }
    if (n < lo || n > hi) {
        FAIL(d, "%s%s%s: outside %lld..%lld", where, *where ? "." : "", key,
             (long long)lo, (long long)hi);
        return false;
    }
    *out = n;
    return true;
}

static bool get_bool(dec_t *d, uint32_t obj, const char *key,
                     const char *where, bool absent_ok)
{
    const int64_t v = member(d, obj, key);
    if (v < 0 && absent_ok) {
        return false;
    }
    if (v < 0 || (d->t[v].type != T_TRUE && d->t[v].type != T_FALSE)) {
        FAIL(d, "%s%s%s: not a boolean", where, *where ? "." : "", key);
        return false;
    }
    return d->t[v].type == T_TRUE;
}

/* The index of @p key's string value in @p names, or -1 after a FAIL(). */
static int get_enum(dec_t *d, uint32_t obj, const char *key,
                    const char *where, const char *const *names, int count)
{
    const int64_t v = member(d, obj, key);
    if (v >= 0 && d->t[v].type == T_STR) {
        const tok_t *t = &d->t[v];
        for (int i = 0; i < count; ++i) {
            if (text_is(d->s, t, names[i])) {
                return i;
            }
        }
    }
    FAIL(d, "%s%s%s: not a known value", where, *where ? "." : "", key);
    return -1;
}

static int64_t get_obj(dec_t *d, uint32_t obj, const char *key,
                       const char *where, ttype_t type)
{
    const int64_t v = member(d, obj, key);
    if (v < 0 || d->t[v].type != type) {
        FAIL(d, "%s%s%s: not an %s", where, *where ? "." : "", key,
             type == T_OBJ ? "object" : "array");
        return -1;
    }
    return v;
}

static bool key_ok(const char *k, size_t max, bool dash)
{
    const size_t n = strlen(k);
    if (n == 0 || n > max) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        const char c = k[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
              || c == (dash ? '-' : '_'))) {
            return false;
        }
    }
    return true;
}

static const char *const k_scheme[]   = { "count", "short_long",
    "melody_groups", "yes_no", "stick_position", "other" };
static const char *const k_encoding[] = { "count", "short_long", "melody",
    "yes_no" };
static const char *const k_announce[] = { "item", "value",
    "item_then_value" };
static const char *const k_auto[]     = { "full", "assisted", "none" };
static const char *const k_throttle[] = { "min", "mid", "max", "none" };
static const char *const k_when[]     = { "before_power_on",
    "after_power_on" };
static const char *const k_changes[]  = { "one", "many" };
static const char *const k_manual[]   = { "before_power", "at_power_up",
    "before_menu", "during_menu", "before_power_off", "after_programming" };
static const char *const k_cells[]    = { "lipo", "nimh" };

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static int by_name(const void *a, const void *b)
{
    const name_at_t *x = a, *y = b;
    const int c = strcmp(x->name, y->name);
    if (c != 0) {
        return c;
    }
    return (x->at > y->at) - (x->at < y->at);
}

static int name_is(const void *key, const void *elem)
{
    return strcmp((const char *)key, ((const name_at_t *)elem)->name);
}

static void decode_models(dec_t *d, uint32_t root, esc_profile_t *p)
{
    const int64_t arr = get_obj(d, root, "models", "", T_ARR);
    if (arr < 0) {
        return;
    }
    const uint32_t n = d->t[arr].size;
    if (n == 0 || n > 1000u) {
        FAIL(d, "models: not 1-1000 entries");
        return;
    }
    esc_model_t *m = take(d, n * sizeof(*m), _Alignof(esc_model_t));
    uint32_t ti = (uint32_t)arr + 1u;
    for (uint32_t i = 0; i < n && !d->failed; ++i, ti = d->t[ti].next) {
        char w[24];
        (void)snprintf(w, sizeof(w), "models[%u]", (unsigned)i);
        if (d->t[ti].type != T_OBJ) {
            FAIL(d, "%s: not an object", w);
            return;
        }
        int64_t cmin = 0, cmax = 0, v = 0, a = 0, vmin = 0;
        const char *name = get_str(d, ti, "name", w, false, false);
        (void)get_num(d, ti, "cells_min", w, 0, 255, true, &cmin);
        (void)get_num(d, ti, "cells_max", w, 0, 255, true, &cmax);
        (void)get_num(d, ti, "v_max_mv", w, 0, 1000000, true, &v);
        (void)get_num(d, ti, "current_a", w, 0, 65535, true, &a);
        (void)get_num(d, ti, "v_min_mv", w, 0, 1000000, true, &vmin);
        if (!d->failed && vmin != 0 && v != 0 && vmin > v) {
            FAIL(d, "%s.v_min_mv: above v_max_mv", w);
            return;
        }
        int ct = 0;
        const int64_t cv = member(d, ti, "cell_type");
        if (cv >= 0 && d->t[cv].type != T_NULL) {
            ct = get_enum(d, ti, "cell_type", w, k_cells, COUNT(k_cells));
        }
        if (m != NULL && !d->failed) {
            m[i] = (esc_model_t){ name, (uint8_t)cmin, (uint8_t)cmax,
                                  ct == 1, (uint32_t)v, (uint16_t)a,
                                  (uint32_t)vmin };
        }
    }
    if (m != NULL && !d->failed) {
        /* Names are unique within a profile: applies_to refers to them.
         * Sorted, a duplicate stands next to its first, and applies_to
         * finds a name by halving: n log n comparisons, where comparing
         * every pair would let 1,000 names sharing a long prefix cost half
         * a million. */
        d->names = malloc(n * sizeof(*d->names));
        if (d->names == NULL) {
            FAIL(d, "out of memory");
            return;
        }
        for (uint32_t i = 0; i < n; ++i) {
            d->names[i] = (name_at_t){ m[i].name, i };
        }
        d->name_count = n;
        qsort(d->names, n, sizeof(*d->names), by_name);
        /* The second of each equal run is a duplicate; the earliest of
         * those is the one the generator names. */
        uint32_t dup = n;
        bool in_run = false;                    /* names[i - 1] repeats */
        for (uint32_t i = 1; i < n; ++i) {
            const bool same = (strcmp(d->names[i - 1u].name,
                                      d->names[i].name) == 0);
            if (same && !in_run && d->names[i].at < dup) {
                dup = d->names[i].at;
            }
            in_run = same;
        }
        if (dup < n) {
            FAIL(d, "models[%u].name: duplicate", (unsigned)dup);
            return;
        }
    }
    p->model_count = (uint16_t)n;
    p->models = m;
}

static bool has_model(const dec_t *d, const char *name)
{
    return d->names != NULL
           && bsearch(name, d->names, d->name_count, sizeof(*d->names),
                      name_is) != NULL;
}

/*
 * Where the stick is when a value's own moves (after_select) begin: the
 * profile's store move, else the move that stores the value; ESC_THR_NONE
 * for a profile with no select move.
 */
static esc_throttle_t moves_from(const esc_profile_t *p)
{
    if (p->select_throttle == ESC_THR_NONE) {
        return ESC_THR_NONE;
    }
    if (p->store_throttle != ESC_THR_NONE) {
        return p->store_throttle;
    }
    return (p->value_select_throttle != ESC_THR_NONE)
               ? p->value_select_throttle : p->select_throttle;
}

static void decode_values(dec_t *d, uint32_t arr, const char *iw,
                          esc_item_t *it, esc_throttle_t before)
{
    const uint32_t n = d->t[arr].size;
    if (n == 0 || n > 255u) {
        FAIL(d, "%s.values: not 1-255 entries", iw);
        return;
    }
    esc_value_t *v = take(d, n * sizeof(*v), _Alignof(esc_value_t));
    unsigned defaults = 0;
    uint8_t seen[256u / 8u] = { 0 };            /* value numbers, one bit each */
    uint32_t ti = arr + 1u;
    for (uint32_t j = 0; j < n && !d->failed; ++j, ti = d->t[ti].next) {
        char w[48];
        (void)snprintf(w, sizeof(w), "%s.values[%u]", iw, (unsigned)j);
        if (d->t[ti].type != T_OBJ) {
            FAIL(d, "%s: not an object", w);
            return;
        }
        int64_t num = 0;
        (void)get_num(d, ti, "number", w, 0, 255, false, &num);
        const char *name = get_str(d, ti, "name", w, false, false);
        const bool dflt = get_bool(d, ti, "default", w, true);
        defaults += dflt ? 1u : 0u;
        /* The stick position the value is programmed from, where the
         * manual gives one other than the entry's: absent or null, the
         * entry's.  "none" is no position, so it is refused. */
        esc_throttle_t et = ESC_THR_NONE;
        const int64_t ev = member(d, ti, "entry_throttle");
        if (ev >= 0 && d->t[ev].type != T_NULL) {
            const int x = get_enum(d, ti, "entry_throttle", w, k_throttle,
                                   COUNT(k_throttle) - 1);
            et = (esc_throttle_t)(x < 0 ? 0 : x);
        }
        /* Power-on to the menu for this value, where the manual gives one
         * other than the entry's: absent or null, the entry's. */
        int64_t hold = 0;
        (void)get_num(d, ti, "entry_hold_ms", w, 0, 600000, true, &hold);
        /* The moves after the select move: absent or null, none; else 1 to
         * ESC_AFTER_MAX of min, mid, max. */
        esc_throttle_t after[ESC_AFTER_MAX] = { ESC_THR_MIN };
        uint32_t after_n = 0u;
        const int64_t av = member(d, ti, "after_select");
        if (av >= 0 && d->t[av].type != T_NULL) {
            after_n = (d->t[av].type == T_ARR) ? d->t[av].size : 0u;
            if (after_n == 0u || after_n > ESC_AFTER_MAX) {
                FAIL(d, "%s.after_select: not 1-%u moves", w,
                     (unsigned)ESC_AFTER_MAX);
                return;
            }
            uint32_t mi = (uint32_t)av + 1u;
            for (uint32_t k = 0; k < after_n; ++k, mi = d->t[mi].next) {
                int x = -1;
                for (int m = 0; d->t[mi].type == T_STR
                                && m < COUNT(k_throttle) - 1; ++m) {
                    if (text_is(d->s, &d->t[mi], k_throttle[m])) {
                        x = m;
                    }
                }
                if (x < 0) {
                    FAIL(d, "%s.after_select[%u]: not a known value", w,
                         (unsigned)k);
                    return;
                }
                /* A move: not where the stick already is. */
                const esc_throttle_t from = (k == 0u) ? before : after[k - 1u];
                if ((esc_throttle_t)x == from) {
                    FAIL(d, "%s.after_select[%u]: no move from the position "
                         "before", w, (unsigned)k);
                    return;
                }
                after[k] = (esc_throttle_t)x;
            }
        }
        if (v != NULL && !d->failed) {
            const uint8_t bit = (uint8_t)(1u << ((uint8_t)num & 7u));
            if ((seen[(uint8_t)num >> 3] & bit) != 0u) {
                FAIL(d, "%s.number: duplicate", w);
                return;
            }
            seen[(uint8_t)num >> 3] |= bit;
            v[j] = (esc_value_t){ name, (uint8_t)num, dflt, et,
                                  (uint32_t)hold, (uint8_t)after_n,
                                  { after[0], after[1], after[2],
                                    after[3] } };
        }
    }
    if (defaults > 1u) {
        FAIL(d, "%s.values: more than one default", iw);
    }
    it->value_count = (uint8_t)n;
    it->values = v;
}

static void decode_items(dec_t *d, uint32_t root, esc_profile_t *p)
{
    const int64_t arr = get_obj(d, root, "items", "", T_ARR);
    if (arr < 0) {
        return;
    }
    const uint32_t n = d->t[arr].size;
    if (n > 255u) {
        FAIL(d, "items: more than 255");
        return;
    }
    if (n == 0 && p->automatable != ESC_AUTO_NONE) {
        FAIL(d, "items: empty for a profile the bench may run");
        return;
    }
    esc_item_t *it = take(d, n * sizeof(*it), _Alignof(esc_item_t));
    uint32_t ti = (uint32_t)arr + 1u;
    for (uint32_t i = 0; i < n && !d->failed; ++i, ti = d->t[ti].next) {
        char w[24];
        (void)snprintf(w, sizeof(w), "items[%u]", (unsigned)i);
        if (d->t[ti].type != T_OBJ) {
            FAIL(d, "%s: not an object", w);
            return;
        }
        esc_item_t x = { 0 };
        int64_t num = 0;
        (void)get_num(d, ti, "number", w, 1, 255, false, &num);
        x.number = (uint8_t)num;
        x.name = get_str(d, ti, "name", w, false, false);
        x.key = get_str(d, ti, "key", w, false, false);
        if (x.key != NULL && !d->failed && !key_ok(x.key, 32u, false)) {
            FAIL(d, "%s.key: not 1-32 of a-z 0-9 _", w);
        }
        x.applies_when = get_str(d, ti, "applies_when", w, true, true);
        const int64_t ap = member(d, ti, "applies_to");
        if (ap >= 0 && d->t[ap].type == T_ARR && d->t[ap].size > 0u) {
            const uint32_t an = d->t[ap].size;
            if (an > 255u) {
                FAIL(d, "%s.applies_to: more than 255", w);
                return;
            }
            const char **names = (const char **)take(d, an * sizeof(*names),
                                                     _Alignof(const char *));
            uint32_t ai = (uint32_t)ap + 1u;
            for (uint32_t k = 0; k < an; ++k, ai = d->t[ai].next) {
                if (d->t[ai].type != T_STR) {
                    FAIL(d, "%s.applies_to: not a list of names", w);
                    return;
                }
                const char *s = copy_str(d, ai);
                if (names != NULL) {
                    if (!has_model(d, s)) {
                        FAIL(d, "%s.applies_to: not a model of this profile",
                             w);
                        return;
                    }
                    names[k] = s;
                }
            }
            x.applies_count = (uint8_t)an;
            x.applies_to = (const char *const *)names;
        } else if (ap >= 0 && d->t[ap].type != T_NULL
                   && d->t[ap].type != T_ARR) {
            FAIL(d, "%s.applies_to: not a list of names", w);
            return;
        }
        const int64_t va = get_obj(d, ti, "values", w, T_ARR);
        if (va >= 0) {
            decode_values(d, (uint32_t)va, w, &x, moves_from(p));
        }
        if (it != NULL) {
            it[i] = x;
        }
    }
    if (it != NULL && !d->failed) {
        /* One number twice only where every holder has a condition. */
        for (uint32_t i = 0; i < n; ++i) {
            for (uint32_t j = i + 1u; j < n; ++j) {
                const bool ci = it[i].applies_count > 0u
                                || it[i].applies_when[0] != '\0';
                const bool cj = it[j].applies_count > 0u
                                || it[j].applies_when[0] != '\0';
                if (it[i].number == it[j].number && !(ci && cj)) {
                    FAIL(d, "items: number %u twice without applies_to or "
                         "applies_when", (unsigned)it[i].number);
                    return;
                }
            }
        }
    }
    p->item_count = (uint8_t)n;
    p->items = it;
}

/*
 * The operator's steps at the ESC: absent or null, none.  Only an assisted
 * profile has them, 1 to ESC_MANUAL_MAX, in the order a run meets them.
 */
static void decode_manual(dec_t *d, uint32_t root, esc_profile_t *p)
{
    const int64_t arr = member(d, root, "manual");
    if (arr < 0 || d->t[arr].type == T_NULL) {
        return;
    }
    const uint32_t n = (d->t[arr].type == T_ARR) ? d->t[arr].size : 0u;
    if (n == 0u || n > ESC_MANUAL_MAX) {
        FAIL(d, "manual: not 1-%u steps", (unsigned)ESC_MANUAL_MAX);
        return;
    }
    if (p->automatable != ESC_AUTO_ASSISTED) {
        FAIL(d, "manual: only on an assisted profile");
        return;
    }
    esc_manual_t *m = take(d, n * sizeof(*m), _Alignof(esc_manual_t));
    int last = 0;
    bool started = false;
    uint32_t ti = (uint32_t)arr + 1u;
    for (uint32_t i = 0; i < n && !d->failed; ++i, ti = d->t[ti].next) {
        char w[24];
        (void)snprintf(w, sizeof(w), "manual[%u]", (unsigned)i);
        if (d->t[ti].type != T_OBJ) {
            FAIL(d, "%s: not an object", w);
            return;
        }
        const int when = get_enum(d, ti, "when", w, k_manual,
                                  COUNT(k_manual));
        if (d->failed) {
            return;
        }
        if (when < last) {
            FAIL(d, "%s.when: before the step above it", w);
            return;
        }
        last = when;
        const char *action = get_str(d, ti, "action", w, false, false);
        if (d->failed) {
            return;
        }
        const int64_t a = member(d, ti, "action");
        if (decoded_len(d, (uint32_t)a) > ESC_MANUAL_ACTION_MAX) {
            FAIL(d, "%s.action: longer than %u bytes", w,
                 (unsigned)ESC_MANUAL_ACTION_MAX);
            return;
        }
        int64_t hold = 0;
        const bool held = get_num(d, ti, "hold_ms", w, 0, 60000, true, &hold);
        if (d->failed) {
            return;
        }
        if (held && when != (int)ESC_MANUAL_AT_POWER_UP) {
            FAIL(d, "%s.hold_ms: only for at_power_up", w);
            return;
        }
        /* The German beside it: absent or null, none; else as the
         * action, 1 to ESC_MANUAL_ACTION_MAX bytes. */
        const char *de = get_str(d, ti, "action_de", w, false, true);
        if (d->failed) {
            return;
        }
        const int64_t dv = member(d, ti, "action_de");
        if (dv >= 0 && d->t[dv].type == T_STR
            && decoded_len(d, (uint32_t)dv) > ESC_MANUAL_ACTION_MAX) {
            FAIL(d, "%s.action_de: longer than %u bytes", w,
                 (unsigned)ESC_MANUAL_ACTION_MAX);
            return;
        }
        /* Whether the step starts the menu: absent or null, false.  Only
         * a before_menu step, the last of them, once. */
        const int64_t sv = member(d, ti, "starts_menu");
        bool starts = false;
        if (sv >= 0 && d->t[sv].type != T_NULL) {
            if (d->t[sv].type != T_TRUE && d->t[sv].type != T_FALSE) {
                FAIL(d, "%s.starts_menu: not a boolean", w);
                return;
            }
            starts = d->t[sv].type == T_TRUE;
        }
        if (starts && when != (int)ESC_MANUAL_BEFORE_MENU) {
            FAIL(d, "%s.starts_menu: only for before_menu", w);
            return;
        }
        if (starts && started) {
            FAIL(d, "%s.starts_menu: a second step that starts the menu", w);
            return;
        }
        if (started && when == (int)ESC_MANUAL_BEFORE_MENU) {
            FAIL(d, "%s.when: before_menu after the step that starts the "
                 "menu", w);
            return;
        }
        started = started || starts;
        /* Whether the ESC locks when the supply goes off before the step
         * is done: absent or null, false.  Only a before_power_off step. */
        const int64_t lv = member(d, ti, "locks");
        bool locks = false;
        if (lv >= 0 && d->t[lv].type != T_NULL) {
            if (d->t[lv].type != T_TRUE && d->t[lv].type != T_FALSE) {
                FAIL(d, "%s.locks: not a boolean", w);
                return;
            }
            locks = d->t[lv].type == T_TRUE;
        }
        if (locks && when != (int)ESC_MANUAL_BEFORE_POWER_OFF) {
            FAIL(d, "%s.locks: only for before_power_off", w);
            return;
        }
        if (m != NULL) {
            m[i] = (esc_manual_t){ (esc_manual_when_t)when, action,
                                   (uint32_t)hold, de, starts, locks };
        }
    }
    p->manual_count = (uint8_t)n;
    p->manual = m;
}

static void decode(dec_t *d, esc_profile_t *p)
{
    const uint32_t root = 0;
    if (d->t[root].type != T_OBJ) {
        FAIL(d, "not an object");
        return;
    }
    int64_t schema = 0;
    if (!get_num(d, root, "schema", "", 1, 1, false, &schema)) {
        if (!d->failed) {
            FAIL(d, "schema: missing");
        }
        return;
    }
    p->id = get_str(d, root, "id", "", false, false);
    if (p->id != NULL && !d->failed && !key_ok(p->id, 48u, true)) {
        FAIL(d, "id: not 1-48 of a-z 0-9 -");
    }
    p->brand  = get_str(d, root, "brand", "", false, false);
    p->family = get_str(d, root, "family", "", false, false);
    p->verified = get_bool(d, root, "verified", "", false);
    const int a = get_enum(d, root, "automatable", "", k_auto, COUNT(k_auto));
    p->automatable = (esc_auto_t)(a < 0 ? 0 : a);
    p->automatable_note = get_str(d, root, "automatable_note", "", true, true);
    if (!d->failed && p->automatable != ESC_AUTO_FULL
        && p->automatable_note != NULL && p->automatable_note[0] == '\0') {
        FAIL(d, "automatable_note: needed when not full");
    }
    if (!d->failed) {
        decode_manual(d, root, p);
    }
    if (d->failed) {
        return;
    }

    const int64_t s = get_obj(d, root, "scheme", "", T_OBJ);
    if (s < 0) {
        return;
    }
    const uint32_t si = (uint32_t)s;
    const int64_t e = get_obj(d, si, "entry", "scheme", T_OBJ);
    const int64_t an = get_obj(d, si, "announce", "scheme", T_OBJ);
    const int64_t se = get_obj(d, si, "select", "scheme", T_OBJ);
    const int64_t sk = get_obj(d, si, "skip", "scheme", T_OBJ);
    if (d->failed) {
        return;
    }
    int x = get_enum(d, si, "type", "scheme", k_scheme, COUNT(k_scheme));
    p->scheme = (esc_scheme_t)(x < 0 ? 0 : x);
    x = get_enum(d, (uint32_t)an, "encoding", "scheme.announce", k_encoding,
                 COUNT(k_encoding));
    p->encoding = (esc_encoding_t)(x < 0 ? 0 : x);
    x = get_enum(d, (uint32_t)an, "what", "scheme.announce", k_announce,
                 COUNT(k_announce));
    p->announce = (esc_announce_t)(x < 0 ? 0 : x);
    x = get_enum(d, (uint32_t)e, "throttle", "scheme.entry", k_throttle,
                 COUNT(k_throttle) - 1);        /* "none" enters nothing */
    p->entry_throttle = (esc_throttle_t)(x < 0 ? 0 : x);
    x = get_enum(d, (uint32_t)e, "when", "scheme.entry", k_when,
                 COUNT(k_when));
    p->entry_after_power = (x == 1);
    x = get_enum(d, (uint32_t)se, "throttle", "scheme.select", k_throttle,
                 COUNT(k_throttle));
    p->select_throttle = (esc_throttle_t)(x < 0 ? 0 : x);
    x = get_enum(d, (uint32_t)sk, "throttle", "scheme.skip", k_throttle,
                 COUNT(k_throttle));
    p->skip_throttle = (esc_throttle_t)(x < 0 ? 0 : x);
    /* Where the stick rests while the menu sounds: absent or null, where
     * the entry left it.  "none" is no resting place, so it is refused. */
    const int64_t li = member(d, si, "listen");
    p->listen_throttle = ESC_THR_NONE;
    if (li >= 0 && d->t[li].type != T_NULL) {
        if (d->t[li].type != T_OBJ) {
            FAIL(d, "scheme.listen: not an object");
            return;
        }
        x = get_enum(d, (uint32_t)li, "throttle", "scheme.listen",
                     k_throttle, COUNT(k_throttle) - 1);
        p->listen_throttle = (esc_throttle_t)(x < 0 ? 0 : x);
    }
    /* The move that stores a selection once the ESC has answered it:
     * absent or null, the selection stores.  "none" is refused here too. */
    const int64_t so = member(d, si, "store");
    p->store_throttle = ESC_THR_NONE;
    if (so >= 0 && d->t[so].type != T_NULL) {
        if (d->t[so].type != T_OBJ) {
            FAIL(d, "scheme.store: not an object");
            return;
        }
        x = get_enum(d, (uint32_t)so, "throttle", "scheme.store",
                     k_throttle, COUNT(k_throttle) - 1);
        p->store_throttle = (esc_throttle_t)(x < 0 ? 0 : x);
    }
    /* Only a two-stage menu has one: absent or null, the select move
     * stores the value too. */
    const int64_t vs = member(d, si, "value_select");
    p->value_select_throttle = ESC_THR_NONE;
    if (vs >= 0 && d->t[vs].type != T_NULL) {
        if (d->t[vs].type != T_OBJ) {
            FAIL(d, "scheme.value_select: not an object");
            return;
        }
        x = get_enum(d, (uint32_t)vs, "throttle", "scheme.value_select",
                     k_throttle, COUNT(k_throttle));
        p->value_select_throttle = (esc_throttle_t)(x < 0 ? 0 : x);
    }
    x = get_enum(d, si, "changes_per_entry", "scheme", k_changes,
                 COUNT(k_changes));
    p->one_change_per_entry = (x == 0);

    int64_t v = 0;
    p->entry_hold_ms = get_num(d, (uint32_t)e, "hold_ms", "scheme.entry", 0,
                               600000, true, &v) ? (uint32_t)v : 0u;
    v = 0;
    p->select_within_ms = get_num(d, (uint32_t)se, "within_ms",
                                  "scheme.select", 0, 60000, true, &v)
                          ? (uint32_t)v : 0u;
    v = 0;
    p->value_select_within_ms =
        (vs >= 0 && d->t[vs].type == T_OBJ
         && get_num(d, (uint32_t)vs, "within_ms", "scheme.value_select", 0,
                    60000, true, &v))
        ? (uint32_t)v : 0u;
    v = 0;
    p->long_equals_short = get_num(d, (uint32_t)an, "long_equals_short",
                                   "scheme.announce", 0, 255, true, &v)
                           ? (uint8_t)v : 0u;
    if (!d->failed && p->encoding == ESC_ENC_SHORT_LONG
        && p->long_equals_short == 0u) {
        FAIL(d, "scheme.announce.long_equals_short: needed for short_long");
    }
    v = 0;
    p->beep_ms = get_num(d, (uint32_t)an, "beep_ms", "scheme.announce", 0,
                         60000, true, &v) ? (uint32_t)v : 0u;
    v = 0;
    p->gap_ms = get_num(d, (uint32_t)an, "gap_ms", "scheme.announce", 0,
                        60000, true, &v) ? (uint32_t)v : 0u;
    v = 0;
    p->group_gap_ms = get_num(d, (uint32_t)an, "group_gap_ms",
                              "scheme.announce", 0, 60000, true, &v)
                      ? (uint32_t)v : 0u;
    v = -1;
    (void)get_num(d, (uint32_t)an, "repeat", "scheme.announce", 0, 255, true,
                  &v);
    p->repeat = (int16_t)v;

    const int64_t st = get_obj(d, (uint32_t)e, "steps", "scheme.entry", T_ARR);
    if (st < 0) {
        return;
    }
    const uint32_t sn = d->t[st].size;
    if (sn == 0 || sn > 255u) {
        FAIL(d, "scheme.entry.steps: not 1-255 strings");
        return;
    }
    const char **steps = (const char **)take(d, sn * sizeof(*steps),
                                             _Alignof(const char *));
    uint32_t ti = (uint32_t)st + 1u;
    for (uint32_t i = 0; i < sn; ++i, ti = d->t[ti].next) {
        if (d->t[ti].type != T_STR || d->t[ti].end == d->t[ti].start) {
            FAIL(d, "scheme.entry.steps: not 1-255 strings");
            return;
        }
        const char *str = copy_str(d, ti);
        if (steps != NULL) {
            steps[i] = str;
        }
    }
    p->step_count = (uint8_t)sn;
    p->steps = (const char *const *)steps;

    decode_models(d, root, p);
    if (!d->failed) {
        decode_items(d, root, p);
    }
}

static const char *lex_why_text(lex_why_t why)
{
    switch (why) {
    case LEX_DUP:       return "a key twice";
    case LEX_NUL:       return "a string holds \\u0000";
    case LEX_SURROGATE: return "half a surrogate pair";
    case LEX_UTF8:      return "not UTF-8";
    case LEX_MEMBERS:   return "an object with more than 64 members";
    case LEX_SYNTAX:
    default:            return "not JSON";
    }
}

bool esc_profile_parse(const char *json, size_t len, esc_profile_t *out,
                       void **block, char *err, size_t err_size)
{
    if (err != NULL && err_size > 0) {
        err[0] = '\0';
    }
    if (block != NULL) {
        *block = NULL;
    }
    if (json == NULL || out == NULL || block == NULL) {
        if (err != NULL && err_size > 0) {
            (void)snprintf(err, err_size, "no input");
        }
        return false;
    }
    if (len > ESC_PROFILE_MAX_BYTES) {
        if (err != NULL && err_size > 0) {
            (void)snprintf(err, err_size, "larger than %u bytes",
                           (unsigned)ESC_PROFILE_MAX_BYTES);
        }
        return false;
    }

    lex_t l = { json, (uint32_t)len, 0u, NULL, 0u, 0u, LEX_SYNTAX };
    if (!lex_all(&l)) {
        if (err != NULL && err_size > 0) {
            (void)snprintf(err, err_size, "%s near byte %u",
                           lex_why_text(l.why), (unsigned)l.pos);
        }
        return false;
    }
    tok_t *tok = (l.n > 0u) ? calloc(l.n, sizeof(*tok)) : NULL;
    if (tok == NULL) {
        if (err != NULL && err_size > 0) {
            (void)snprintf(err, err_size, "out of memory");
        }
        return false;
    }
    l.tok = tok;
    l.cap = l.n;
    /* Same text, same tokens; what this pass adds is the duplicate check. */
    if (!lex_all(&l)) {
        free(tok);
        if (err != NULL && err_size > 0) {
            (void)snprintf(err, err_size, "%s near byte %u",
                           lex_why_text(l.why), (unsigned)l.pos);
        }
        return false;
    }

    esc_profile_t p = { 0 };
    dec_t d = { json, tok, NULL, 0u, 0u, err, err_size, false, NULL, 0u };
    decode(&d, &p);                             /* sizing */
    if (d.failed) {
        free(tok);
        return false;
    }
    uint8_t *mem = malloc(d.used > 0u ? d.used : 1u);
    if (mem == NULL) {
        free(tok);
        if (err != NULL && err_size > 0) {
            (void)snprintf(err, err_size, "out of memory");
        }
        return false;
    }
    const size_t sized = d.used;
    p = (esc_profile_t){ 0 };
    d.base = mem;
    d.cap = sized;
    d.used = 0u;
    decode(&d, &p);
    free(tok);
    free(d.names);
    if (d.failed || d.used != sized) {
        free(mem);
        if (!d.failed && err != NULL && err_size > 0) {
            (void)snprintf(err, err_size, "internal: sizes differ");
        }
        return false;
    }
    *out = p;
    *block = mem;
    return true;
}
