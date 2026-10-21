/**
 * @file
 * @brief Read one ESC profile from JSON.
 *
 * See esc_profile.h.
 *
 * Two stages.  The text is first cut into tokens: one per value, each with
 * the index one past its last descendant, so a reader skips a subtree in
 * one step and never recurses to find a key.  Then the profile is decoded
 * from the tokens twice: once with no block, which only adds up the bytes
 * every string and array will need, and once into a block of exactly that
 * size.  One allocation per profile, and free() of it releases everything.
 * The first stage is esc_json_lex.c; esc_json_int.h is what it gives this
 * file.
 *
 * The rules are tools/gen_esc_profiles.py's.  The host suite parses every
 * profile of record and compares it with the generated table, so a rule
 * that differs between the two fails there.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_json_int.h"
#include "esc_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* ------------------------------------------------------------ decoding */


/**
 * @brief A model name and where it stands in the file.
 */
typedef struct {
    const char *name;   /**< the model name                                 */
    uint32_t    at;     /**< index of the model in the models array         */
} name_at_t;

/** @brief State of one decoding pass over the tokens. */
typedef struct {
    const char  *s;          /**< the JSON text                              */
    const tok_t *t;          /**< the tokens of the text                     */
    uint8_t     *base;       /**< NULL while sizing */
    size_t       used;       /**< bytes taken from the block so far          */
    size_t       cap;        /**< the block's size; 0 while sizing           */
    char        *err;        /**< receives the reason, or NULL               */
    size_t       err_size;   /**< size of err, bytes                         */
    bool         failed;     /**< a reason was set; decoding stops           */
    name_at_t   *names;      /**< the model names, sorted; NULL while sizing */
    uint32_t     name_count; /**< number of entries in names                 */
} dec_t;

/**
 * @brief The first reason stands.
 *
 * A macro rather than a variadic function: the arguments go straight to
 * snprintf(), and there is no va_list to get wrong.
 */
#define FAIL(d, ...)                                                      \
    do {                                                                  \
        if (!(d)->failed) {                                               \
            (d)->failed = true;                                           \
            if ((d)->err != NULL && (d)->err_size > 0u) {                 \
                (void)snprintf((d)->err, (d)->err_size, __VA_ARGS__);     \
            }                                                             \
        }                                                                 \
    } while (0)

/**
 * @brief Take aligned bytes from the block being filled.
 *
 * While sizing, only the used count moves; while filling, the bytes are
 * handed out from the block.
 *
 * @param d      The decoding state.
 * @param n      Bytes wanted.
 * @param align  Alignment of the returned pointer, bytes.
 * @return The pointer into the block, or NULL while sizing.
 */
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

/**
 * @brief The value of @p key in the object at token @p obj, or -1.
 *
 * @param d    The decoding state.
 * @param obj  Index of the object token.
 * @param key  The key to find.
 * @return Index of the value token, or -1 when the key is absent.
 */
static int64_t member(const dec_t *d, uint32_t obj, const char *key)
{
    if (d->t[obj].type != T_OBJ) {
        return -1;
    }
    uint32_t i = obj + 1u;
    for (uint32_t m = 0; m < d->t[obj].size; ++m) {
        const uint32_t v = i + 1u;
        if (esc_json_text_is(d->s, &d->t[i], key)) {
            return (int64_t)v;
        }
        i = d->t[v].next;
    }
    return -1;
}

/**
 * @brief A string token's text, decoded into the block.
 *
 * Never longer than the text it came from: no escape decodes to more bytes than
 * it is written in.
 *
 * @param d   The decoding state.
 * @param ti  Index of the string token.
 * @return The decoded, NUL-terminated text in the block, or NULL while sizing.
 */
static const char *copy_str(dec_t *d, uint32_t ti)
{
    const tok_t *t = &d->t[ti];
    char *out = take(d, (size_t)(t->end - t->start) + 1u, 1u);
    size_t o = 0;
    uint32_t i = t->start;
    while (i < t->end) {
        uint8_t u[4];
        const unsigned n = esc_json_decode_unit(d->s, &i, u);
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

/**
 * @brief A string token's length once decoded, in bytes: UTF-8, as the
 *        generator measures it.
 *
 * The sizing pass has no copy to measure.
 *
 * @param d   The decoding state.
 * @param ti  Index of the string token.
 * @return The decoded length, bytes.
 */
static size_t decoded_len(const dec_t *d, uint32_t ti)
{
    const tok_t *t = &d->t[ti];
    size_t o = 0;
    uint32_t i = t->start;
    while (i < t->end) {
        uint8_t u[4];
        o += esc_json_decode_unit(d->s, &i, u);
    }
    return o;
}

/**
 * @brief Read a string member.
 *
 * Fails when the member is absent or not a string, or is empty while
 * @p empty_ok is false.  A null member is "" when @p null_ok is set.
 *
 * @param d        The decoding state.
 * @param obj      Index of the object token.
 * @param key      The member name.
 * @param where    Path of the object, for the reason text.
 * @param empty_ok true when an empty string is allowed.
 * @param null_ok  true when a missing or null member is allowed.
 * @return The decoded text, "" or NULL as described above.
 */
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

/**
 * @brief A whole number in lo..hi; *out untouched for null when null_ok.
 *
 * @param d        The decoding state.
 * @param obj      Index of the object token.
 * @param key      The member name.
 * @param where    Path of the object, for the reason text.
 * @param lo       Smallest allowed value.
 * @param hi       Largest allowed value.
 * @param null_ok  true when a missing or null member is allowed.
 * @param out      Receives the value.
 * @return true when a value was read into @p out.
 */
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

/**
 * @brief Read a boolean member.
 *
 * @param d         The decoding state.
 * @param obj       Index of the object token.
 * @param key       The member name.
 * @param where     Path of the object, for the reason text.
 * @param absent_ok true when an absent member reads as false.
 * @return The value; false when absent and @p absent_ok is set.
 */
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

/**
 * @brief The index of @p key's string value in @p names, or -1 after a FAIL().
 *
 * @param d      The decoding state.
 * @param obj    Index of the object token.
 * @param key    The member name.
 * @param where  Path of the object, for the reason text.
 * @param names  The allowed values.
 * @param count  Number of entries in @p names.
 * @return The index into @p names, or -1 after a FAIL().
 */
static int get_enum(dec_t *d, uint32_t obj, const char *key,
                    const char *where, const char *const *names, int count)
{
    const int64_t v = member(d, obj, key);
    if (v >= 0 && d->t[v].type == T_STR) {
        const tok_t *t = &d->t[v];
        for (int i = 0; i < count; ++i) {
            if (esc_json_text_is(d->s, t, names[i])) {
                return i;
            }
        }
    }
    FAIL(d, "%s%s%s: not a known value", where, *where ? "." : "", key);
    return -1;
}

/**
 * @brief Read a member that must be an object or an array.
 *
 * @param d      The decoding state.
 * @param obj    Index of the object token.
 * @param key    The member name.
 * @param where  Path of the object, for the reason text.
 * @param type   T_OBJ or T_ARR.
 * @return Index of the member token, or -1 after a FAIL().
 */
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

/**
 * @brief Check an id or key: 1 to max of a-z, 0-9 and one separator.
 *
 * @param k     The text to check.
 * @param max   Longest allowed length, characters.
 * @param dash  true for "-" as the separator, false for "_".
 * @return true when the text is allowed.
 */
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

/** @brief Allowed values of the enumerated members, as the JSON spells them. */
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

/**
 * @brief Number of entries in a static array.
 *
 * @param a  The array.
 */
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/**
 * @brief Sort comparison of model names, then by position.
 *
 * @param a  Pointer to a name_at_t.
 * @param b  Pointer to a name_at_t.
 * @return Negative, zero or positive as @p a sorts before, with or after @p b.
 */
static int by_name(const void *a, const void *b)
{
    const name_at_t *x = a, *y = b;
    const int c = strcmp(x->name, y->name);
    if (c != 0) {
        return c;
    }
    return (x->at > y->at) - (x->at < y->at);
}

/**
 * @brief Search comparison of a name with a name_at_t.
 *
 * @param key   The name searched for, a const char *.
 * @param elem  A name_at_t.
 * @return Negative, zero or positive as @p key sorts before, with or after.
 */
static int name_is(const void *key, const void *elem)
{
    return strcmp((const char *)key, ((const name_at_t *)elem)->name);
}

/**
 * @brief Decode the models array of a profile.
 *
 * Model names must be unique within the profile.
 *
 * @param d     The decoding state.
 * @param root  Index of the root object token.
 * @param p     Receives the models.
 */
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

/**
 * @brief Whether a profile has a model with a name.
 *
 * @param d     The decoding state, with the sorted model names.
 * @param name  The model name.
 * @return true when a model has the name.
 */
static bool has_model(const dec_t *d, const char *name)
{
    return d->names != NULL
           && bsearch(name, d->names, d->name_count, sizeof(*d->names),
                      name_is) != NULL;
}

/**
 * @brief Where the stick is when a value's own moves (after_select) begin: the
 *        profile's store move, else the move that stores the value;
 *        ESC_THR_NONE for a profile with no select move.
 *
 * @param p  The profile.
 * @return The position the moves start from.
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

/**
 * @brief Decode the values array of one item.
 *
 * @param d       The decoding state.
 * @param arr     Index of the values array token.
 * @param iw      Path of the item, for the reason text.
 * @param it      The item that receives the values.
 * @param before  Stick position before the item's first value move.
 */
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
            for (uint32_t k = 0; k < after_n && k < ESC_AFTER_MAX;
                 ++k, mi = d->t[mi].next) {
                int x = -1;
                for (int m = 0; d->t[mi].type == T_STR
                                && m < COUNT(k_throttle) - 1; ++m) {
                    if (esc_json_text_is(d->s, &d->t[mi], k_throttle[m])) {
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
                                  { ESC_THR_MIN } };
            /* The moves, as many as the value holds: the same size. */
            _Static_assert(sizeof(v[j].after) == sizeof(after),
                           "after_select copied whole");
            memcpy(v[j].after, after, sizeof(after));
        }
    }
    if (defaults > 1u) {
        FAIL(d, "%s.values: more than one default", iw);
    }
    it->value_count = (uint8_t)n;
    it->values = v;
}

/**
 * @brief Decode the items array of a profile.
 *
 * @param d     The decoding state.
 * @param root  Index of the root object token.
 * @param p     Receives the items.
 */
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

/**
 * @brief The operator's steps at the ESC: absent or null, none.
 *
 * Only an assisted profile has them, 1 to ESC_MANUAL_MAX, in the order a run
 * meets them.
 *
 * @param d     The decoding state.
 * @param root  Index of the root object token.
 * @param p     Receives the steps.
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

/**
 * @brief Decode a whole profile from the tokens.
 *
 * Run once to size the block and once to fill it.
 *
 * @param d  The decoding state.
 * @param p  Receives the profile.
 */
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
    /* The step that starts the menu is asked with the stick where the
     * power-up left it, and the run counts from then: the menu has to
     * rest there, for every value's power-up position. */
    for (unsigned i = 0; !d->failed && p->manual != NULL
                         && p->listen_throttle != ESC_THR_NONE
                         && i < p->manual_count; ++i) {
        if (!p->manual[i].starts_menu) {
            continue;
        }
        bool elsewhere = p->listen_throttle != p->entry_throttle;
        for (unsigned k = 0; !elsewhere && p->items != NULL
                             && k < p->item_count; ++k) {
            const esc_item_t *it = &p->items[k];
            for (unsigned vi = 0; vi < it->value_count; ++vi) {
                const esc_throttle_t et = it->values[vi].entry_throttle;
                elsewhere = elsewhere
                            || (et != ESC_THR_NONE && et != p->listen_throttle);
            }
        }
        if (elsewhere) {
            FAIL(d, "manual[%u].starts_menu: the menu rests elsewhere "
                 "(scheme.listen)", i);
        }
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
    if (!esc_json_lex_all(&l)) {
        if (err != NULL && err_size > 0) {
            (void)snprintf(err, err_size, "%s near byte %u",
                           esc_json_lex_why_text(l.why), (unsigned)l.pos);
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
    if (!esc_json_lex_all(&l)) {
        free(tok);
        if (err != NULL && err_size > 0) {
            (void)snprintf(err, err_size, "%s near byte %u",
                           esc_json_lex_why_text(l.why), (unsigned)l.pos);
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
