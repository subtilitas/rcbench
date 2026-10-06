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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Nesting deeper than any profile needs (five levels) is refused rather than
 * followed, so a hostile file cannot run the stack down. */
#define MAX_DEPTH 16

typedef enum { T_OBJ, T_ARR, T_STR, T_NUM, T_TRUE, T_FALSE, T_NULL } ttype_t;

typedef struct {
    ttype_t  type;
    uint32_t start;     /* first byte; for a string, after the quote      */
    uint32_t end;       /* one past the last byte; before the quote       */
    uint32_t size;      /* members of an object, elements of an array     */
    uint32_t next;      /* the token after this value and its descendants */
} tok_t;

typedef struct {
    const char *s;
    uint32_t    len;
    uint32_t    pos;
    tok_t      *tok;    /* NULL: count only */
    uint32_t    n;
    uint32_t    cap;
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
        if (c == '\\') {
            l->pos++;
            if (l->pos >= l->len) {
                return false;
            }
            const char e = l->s[l->pos];
            if (e == 'u') {
                for (int k = 0; k < 4; ++k) {
                    l->pos++;
                    if (l->pos >= l->len) {
                        return false;
                    }
                    const char h = l->s[l->pos];
                    if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f')
                          || (h >= 'A' && h <= 'F'))) {
                        return false;
                    }
                }
            } else if (strchr("\"\\/bfnrt", e) == NULL || e == '\0') {
                return false;
            }
        }
        l->pos++;
    }
    return false;                               /* no closing quote */
}

static bool lex_number(lex_t *l)
{
    const uint32_t start = l->pos;
    if (l->s[l->pos] == '-') {
        l->pos++;
    }
    bool digits = false;
    while (l->pos < l->len) {
        const char c = l->s[l->pos];
        if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E'
            || c == '+' || c == '-') {
            digits = digits || (c >= '0' && c <= '9');
            l->pos++;
        } else {
            break;
        }
    }
    if (!digits) {
        return false;
    }
    const int64_t i = add(l, T_NUM, start);
    if (i < 0) {
        return false;
    }
    close_tok(l, i, l->pos, 0u);
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
            ws(l);
            if (l->pos >= l->len || l->s[l->pos] != '"' || !lex_string(l)) {
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

typedef struct {
    const char  *s;
    const tok_t *t;
    uint8_t     *base;      /* NULL while sizing */
    size_t       used;
    size_t       cap;       /* the block's size; 0 while sizing */
    char        *err;
    size_t       err_size;
    bool         failed;
} dec_t;

static void fail(dec_t *d, const char *fmt, ...)
{
    if (d->failed) {
        return;                                 /* the first reason stands */
    }
    d->failed = true;
    if (d->err != NULL && d->err_size > 0) {
        va_list ap;
        va_start(ap, fmt);
        (void)vsnprintf(d->err, d->err_size, fmt, ap);
        va_end(ap);
    }
}

static void *take(dec_t *d, size_t n, size_t align)
{
    d->used = (d->used + align - 1u) / align * align;
    void *p = NULL;
    if (d->base != NULL) {
        /* The sizing pass measured this block, so this cannot be short; it
         * is checked so that a mistake there fails here, not past the end. */
        if (d->used + n > d->cap) {
            fail(d, "internal: block too small");
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
    const size_t klen = strlen(key);
    uint32_t i = obj + 1u;
    for (uint32_t m = 0; m < d->t[obj].size; ++m) {
        const tok_t *k = &d->t[i];
        const uint32_t v = i + 1u;
        if (k->end - k->start == klen
            && memcmp(d->s + k->start, key, klen) == 0) {
            return (int64_t)v;
        }
        i = d->t[v].next;
    }
    return -1;
}

static unsigned hexval(char c)
{
    if (c >= '0' && c <= '9') { return (unsigned)(c - '0'); }
    if (c >= 'a' && c <= 'f') { return (unsigned)(c - 'a' + 10); }
    return (unsigned)(c - 'A' + 10);
}

/* Unescape a string token into the block.  \u escapes become UTF-8; a
 * surrogate pair is not joined, and either half becomes '?', which no
 * profile of record needs. */
static const char *copy_str(dec_t *d, uint32_t ti)
{
    const tok_t *t = &d->t[ti];
    char *out = take(d, (size_t)(t->end - t->start) + 1u, 1u);
    size_t o = 0;
    for (uint32_t i = t->start; i < t->end; ++i) {
        char c = d->s[i];
        if (c == '\\') {
            const char e = d->s[++i];
            unsigned cp = 0;
            switch (e) {
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u':
                for (int k = 0; k < 4; ++k) {
                    cp = (cp << 4) | hexval(d->s[++i]);
                }
                if (cp >= 0xD800u && cp <= 0xDFFFu) {
                    cp = '?';
                }
                /* Never longer than the six bytes of text it came from. */
                if (cp < 0x80u) {
                    c = (char)cp;
                } else if (cp < 0x800u) {
                    if (out != NULL) {
                        out[o] = (char)(0xC0u | (cp >> 6));
                    }
                    o++;
                    c = (char)(0x80u | (cp & 0x3Fu));
                } else {
                    if (out != NULL) {
                        out[o]      = (char)(0xE0u | (cp >> 12));
                        out[o + 1u] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
                    }
                    o += 2u;
                    c = (char)(0x80u | (cp & 0x3Fu));
                }
                break;
            default: c = e; break;              /* \" \\ \/ */
            }
        }
        if (out != NULL) {
            out[o] = c;
        }
        o++;
    }
    if (out != NULL) {
        out[o] = '\0';
    }
    return out;
}

static const char *get_str(dec_t *d, uint32_t obj, const char *key,
                           const char *where, bool empty_ok, bool null_ok)
{
    const int64_t v = member(d, obj, key);
    if ((v < 0 || d->t[v].type == T_NULL) && null_ok) {
        return (d->base != NULL) ? "" : NULL;
    }
    if (v < 0 || d->t[v].type != T_STR) {
        fail(d, "%s%s%s: not a string", where, *where ? "." : "", key);
        return "";
    }
    if (!empty_ok && d->t[v].end == d->t[v].start) {
        fail(d, "%s%s%s: empty", where, *where ? "." : "", key);
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
            fail(d, "%s%s%s: missing", where, *where ? "." : "", key);
        }
        return false;
    }
    const tok_t *t = &d->t[v];
    if (t->type != T_NUM) {
        fail(d, "%s%s%s: not a whole number", where, *where ? "." : "", key);
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
        fail(d, "%s%s%s: not a whole number", where, *where ? "." : "", key);
        return false;
    }
    for (; i < t->end; ++i) {
        const char c = d->s[i];
        if (c < '0' || c > '9' || n > 100000000) {
            fail(d, "%s%s%s: not a whole number", where, *where ? "." : "", key);
            return false;
        }
        n = n * 10 + (c - '0');
    }
    if (neg) {
        n = -n;
    }
    if (n < lo || n > hi) {
        fail(d, "%s%s%s: outside %lld..%lld", where, *where ? "." : "", key,
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
        fail(d, "%s%s%s: not a boolean", where, *where ? "." : "", key);
        return false;
    }
    return d->t[v].type == T_TRUE;
}

/* The index of @p key's string value in @p names, or -1 after a fail(). */
static int get_enum(dec_t *d, uint32_t obj, const char *key,
                    const char *where, const char *const *names, int count)
{
    const int64_t v = member(d, obj, key);
    if (v >= 0 && d->t[v].type == T_STR) {
        const tok_t *t = &d->t[v];
        for (int i = 0; i < count; ++i) {
            const size_t n = strlen(names[i]);
            if (t->end - t->start == n
                && memcmp(d->s + t->start, names[i], n) == 0) {
                return i;
            }
        }
    }
    fail(d, "%s%s%s: not a known value", where, *where ? "." : "", key);
    return -1;
}

static int64_t get_obj(dec_t *d, uint32_t obj, const char *key,
                       const char *where, ttype_t type)
{
    const int64_t v = member(d, obj, key);
    if (v < 0 || d->t[v].type != type) {
        fail(d, "%s%s%s: not an %s", where, *where ? "." : "", key,
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
static const char *const k_cells[]    = { "lipo", "nimh" };

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static void decode_models(dec_t *d, uint32_t root, esc_profile_t *p)
{
    const int64_t arr = get_obj(d, root, "models", "", T_ARR);
    if (arr < 0) {
        return;
    }
    const uint32_t n = d->t[arr].size;
    if (n == 0 || n > 1000u) {
        fail(d, "models: not 1-1000 entries");
        return;
    }
    esc_model_t *m = take(d, n * sizeof(*m), _Alignof(esc_model_t));
    uint32_t ti = (uint32_t)arr + 1u;
    for (uint32_t i = 0; i < n && !d->failed; ++i, ti = d->t[ti].next) {
        char w[24];
        (void)snprintf(w, sizeof(w), "models[%u]", (unsigned)i);
        if (d->t[ti].type != T_OBJ) {
            fail(d, "%s: not an object", w);
            return;
        }
        int64_t cmin = 0, cmax = 0, v = 0, a = 0;
        const char *name = get_str(d, ti, "name", w, false, false);
        (void)get_num(d, ti, "cells_min", w, 0, 255, true, &cmin);
        (void)get_num(d, ti, "cells_max", w, 0, 255, true, &cmax);
        (void)get_num(d, ti, "v_max_mv", w, 0, 1000000, true, &v);
        (void)get_num(d, ti, "current_a", w, 0, 65535, true, &a);
        int ct = 0;
        const int64_t cv = member(d, ti, "cell_type");
        if (cv >= 0 && d->t[cv].type != T_NULL) {
            ct = get_enum(d, ti, "cell_type", w, k_cells, COUNT(k_cells));
        }
        if (m != NULL && !d->failed) {
            m[i] = (esc_model_t){ name, (uint8_t)cmin, (uint8_t)cmax,
                                  ct == 1, (uint32_t)v, (uint16_t)a };
        }
    }
    if (m != NULL && !d->failed) {
        /* Names are unique within a profile: applies_to refers to them. */
        for (uint32_t i = 0; i < n; ++i) {
            for (uint32_t j = i + 1u; j < n; ++j) {
                if (strcmp(m[i].name, m[j].name) == 0) {
                    fail(d, "models[%u].name: duplicate", (unsigned)j);
                    return;
                }
            }
        }
    }
    p->model_count = (uint16_t)n;
    p->models = m;
}

static bool has_model(const esc_profile_t *p, const char *name)
{
    for (uint16_t i = 0; i < p->model_count; ++i) {
        if (strcmp(p->models[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

static void decode_values(dec_t *d, uint32_t arr, const char *iw,
                          esc_item_t *it)
{
    const uint32_t n = d->t[arr].size;
    if (n == 0 || n > 255u) {
        fail(d, "%s.values: not 1-255 entries", iw);
        return;
    }
    esc_value_t *v = take(d, n * sizeof(*v), _Alignof(esc_value_t));
    unsigned defaults = 0;
    uint32_t ti = arr + 1u;
    for (uint32_t j = 0; j < n && !d->failed; ++j, ti = d->t[ti].next) {
        char w[48];
        (void)snprintf(w, sizeof(w), "%s.values[%u]", iw, (unsigned)j);
        if (d->t[ti].type != T_OBJ) {
            fail(d, "%s: not an object", w);
            return;
        }
        int64_t num = 0;
        (void)get_num(d, ti, "number", w, 0, 255, false, &num);
        const char *name = get_str(d, ti, "name", w, false, false);
        const bool dflt = get_bool(d, ti, "default", w, true);
        defaults += dflt ? 1u : 0u;
        if (v != NULL && !d->failed) {
            for (uint32_t k = 0; k < j; ++k) {
                if (v[k].number == (uint8_t)num) {
                    fail(d, "%s.number: duplicate", w);
                    return;
                }
            }
            v[j] = (esc_value_t){ name, (uint8_t)num, dflt };
        }
    }
    if (defaults > 1u) {
        fail(d, "%s.values: more than one default", iw);
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
        fail(d, "items: more than 255");
        return;
    }
    if (n == 0 && p->automatable != ESC_AUTO_NONE) {
        fail(d, "items: empty for a profile the bench may run");
        return;
    }
    esc_item_t *it = take(d, n * sizeof(*it), _Alignof(esc_item_t));
    uint32_t ti = (uint32_t)arr + 1u;
    for (uint32_t i = 0; i < n && !d->failed; ++i, ti = d->t[ti].next) {
        char w[24];
        (void)snprintf(w, sizeof(w), "items[%u]", (unsigned)i);
        if (d->t[ti].type != T_OBJ) {
            fail(d, "%s: not an object", w);
            return;
        }
        esc_item_t x = { 0 };
        int64_t num = 0;
        (void)get_num(d, ti, "number", w, 1, 255, false, &num);
        x.number = (uint8_t)num;
        x.name = get_str(d, ti, "name", w, false, false);
        x.key = get_str(d, ti, "key", w, false, false);
        if (x.key != NULL && !d->failed && !key_ok(x.key, 32u, false)) {
            fail(d, "%s.key: not 1-32 of a-z 0-9 _", w);
        }
        x.applies_when = get_str(d, ti, "applies_when", w, true, true);
        const int64_t ap = member(d, ti, "applies_to");
        if (ap >= 0 && d->t[ap].type == T_ARR && d->t[ap].size > 0u) {
            const uint32_t an = d->t[ap].size;
            if (an > 255u) {
                fail(d, "%s.applies_to: more than 255", w);
                return;
            }
            const char **names = (const char **)take(d, an * sizeof(*names),
                                                     _Alignof(const char *));
            uint32_t ai = (uint32_t)ap + 1u;
            for (uint32_t k = 0; k < an; ++k, ai = d->t[ai].next) {
                if (d->t[ai].type != T_STR) {
                    fail(d, "%s.applies_to: not a list of names", w);
                    return;
                }
                const char *s = copy_str(d, ai);
                if (names != NULL) {
                    if (!has_model(p, s)) {
                        fail(d, "%s.applies_to: not a model of this profile",
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
            fail(d, "%s.applies_to: not a list of names", w);
            return;
        }
        const int64_t va = get_obj(d, ti, "values", w, T_ARR);
        if (va >= 0) {
            decode_values(d, (uint32_t)va, w, &x);
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
                    fail(d, "items: number %u twice without applies_to or "
                         "applies_when", (unsigned)it[i].number);
                    return;
                }
            }
        }
    }
    p->item_count = (uint8_t)n;
    p->items = it;
}

static void decode(dec_t *d, esc_profile_t *p)
{
    const uint32_t root = 0;
    if (d->t[root].type != T_OBJ) {
        fail(d, "not an object");
        return;
    }
    int64_t schema = 0;
    if (!get_num(d, root, "schema", "", 1, 1, false, &schema)) {
        if (!d->failed) {
            fail(d, "schema: missing");
        }
        return;
    }
    p->id = get_str(d, root, "id", "", false, false);
    if (p->id != NULL && !d->failed && !key_ok(p->id, 48u, true)) {
        fail(d, "id: not 1-48 of a-z 0-9 -");
    }
    p->brand  = get_str(d, root, "brand", "", false, false);
    p->family = get_str(d, root, "family", "", false, false);
    p->verified = get_bool(d, root, "verified", "", false);
    const int a = get_enum(d, root, "automatable", "", k_auto, COUNT(k_auto));
    p->automatable = (esc_auto_t)(a < 0 ? 0 : a);
    p->automatable_note = get_str(d, root, "automatable_note", "", true, true);
    if (!d->failed && p->automatable != ESC_AUTO_FULL
        && p->automatable_note != NULL && p->automatable_note[0] == '\0') {
        fail(d, "automatable_note: needed when not full");
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
    x = get_enum(d, si, "changes_per_entry", "scheme", k_changes,
                 COUNT(k_changes));
    p->one_change_per_entry = (x == 0);

    int64_t v = 0;
    p->entry_hold_ms = get_num(d, (uint32_t)e, "hold_ms", "scheme.entry", 0,
                               600000, true, &v) ? (uint32_t)v : 0u;
    v = 0;
    p->long_equals_short = get_num(d, (uint32_t)an, "long_equals_short",
                                   "scheme.announce", 0, 255, true, &v)
                           ? (uint8_t)v : 0u;
    if (!d->failed && p->encoding == ESC_ENC_SHORT_LONG
        && p->long_equals_short == 0u) {
        fail(d, "scheme.announce.long_equals_short: needed for short_long");
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
        fail(d, "scheme.entry.steps: not 1-255 strings");
        return;
    }
    const char **steps = (const char **)take(d, sn * sizeof(*steps),
                                             _Alignof(const char *));
    uint32_t ti = (uint32_t)st + 1u;
    for (uint32_t i = 0; i < sn; ++i, ti = d->t[ti].next) {
        if (d->t[ti].type != T_STR || d->t[ti].end == d->t[ti].start) {
            fail(d, "scheme.entry.steps: not 1-255 strings");
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

    lex_t l = { json, (uint32_t)len, 0u, NULL, 0u, 0u };
    if (!lex_all(&l)) {
        if (err != NULL && err_size > 0) {
            (void)snprintf(err, err_size, "not JSON near byte %u",
                           (unsigned)l.pos);
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
    (void)lex_all(&l);                          /* same text, same answer */

    esc_profile_t p = { 0 };
    dec_t d = { json, tok, NULL, 0u, 0u, err, err_size, false };
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
