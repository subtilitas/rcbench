/*
 * The profiles the bench offers: built in, with the card's in front.  See
 * esc_profile.h.
 *
 * A card profile with a built-in id takes that profile's place in the list,
 * so an operator who corrects a default on the card sees one entry, not two.
 * A card profile with a new id follows the built-in ones in the order the
 * card was read.
 *
 * Not thread-safe: the panel fills it once at start-up, before anything
 * reads it.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_profile.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    esc_profile_t p;
    void         *block;
    bool          replaces;     /* a built-in profile has this id */
} slot_t;

static slot_t s_over[ESC_PROFILE_MAX_OVERRIDES];
static size_t s_over_n;

static const esc_profile_t *builtin_find(const char *id)
{
    for (size_t i = 0; i < esc_profiles_builtin_count; ++i) {
        if (strcmp(esc_profiles_builtin[i].id, id) == 0) {
            return &esc_profiles_builtin[i];
        }
    }
    return NULL;
}

static slot_t *over_find(const char *id)
{
    for (size_t i = 0; i < s_over_n; ++i) {
        if (strcmp(s_over[i].p.id, id) == 0) {
            return &s_over[i];
        }
    }
    return NULL;
}

bool esc_brand_same(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return a == b;
    }
    for (;; ++a, ++b) {
        int x = (unsigned char)*a;
        int y = (unsigned char)*b;
        x -= (x >= 'a' && x <= 'z') ? 32 : 0;
        y -= (y >= 'a' && y <= 'z') ? 32 : 0;
        if (x != y) {
            return false;
        }
        if (x == 0) {
            return true;
        }
    }
}

/* The models @p p's maker would list with @p p in place of the profile of
 * its id. */
static size_t maker_models_with(const esc_profile_t *p)
{
    size_t n = p->model_count;
    const size_t total = esc_profiles_count();
    for (size_t i = 0; i < total; ++i) {
        const esc_profile_t *q = esc_profiles_at(i);
        if (q != NULL && strcmp(q->id, p->id) != 0
            && esc_brand_same(q->brand, p->brand)) {
            n += q->model_count;
        }
    }
    return n;
}

static bool refuse(void *block, const char **why, const char *text)
{
    free(block);
    if (why != NULL) {
        *why = text;
    }
    return false;
}

bool esc_profiles_override(const esc_profile_t *p, void *block)
{
    return esc_profiles_override_why(p, block, NULL);
}

bool esc_profiles_override_why(const esc_profile_t *p, void *block,
                               const char **why)
{
    if (p == NULL || p->id == NULL) {
        return refuse(block, why, "no profile");
    }
    if (maker_models_with(p) > ESC_MAKER_MODELS_MAX) {
        return refuse(block, why, "its maker would list more than 512 "
                      "models");
    }
    slot_t *s = over_find(p->id);
    if (s != NULL) {
        /* The later file wins; the earlier one's memory goes with it. */
        free(s->block);
    } else if (s_over_n < ESC_PROFILE_MAX_OVERRIDES) {
        s = &s_over[s_over_n++];
    } else {
        return refuse(block, why, "more than 32 on the card");
    }
    s->p = *p;
    s->block = block;
    s->replaces = (builtin_find(p->id) != NULL);
    return true;
}

void esc_profiles_clear_overrides(void)
{
    for (size_t i = 0; i < s_over_n; ++i) {
        free(s_over[i].block);
    }
    memset(s_over, 0, sizeof(s_over));
    s_over_n = 0;
}

size_t esc_profiles_override_count(void)
{
    return s_over_n;
}

size_t esc_profiles_count(void)
{
    size_t added = 0;
    for (size_t i = 0; i < s_over_n; ++i) {
        added += s_over[i].replaces ? 0u : 1u;
    }
    return esc_profiles_builtin_count + added;
}

const esc_profile_t *esc_profiles_at(size_t i)
{
    if (i < esc_profiles_builtin_count) {
        const slot_t *s = over_find(esc_profiles_builtin[i].id);
        return (s != NULL) ? &s->p : &esc_profiles_builtin[i];
    }
    size_t k = i - esc_profiles_builtin_count;
    for (size_t j = 0; j < s_over_n; ++j) {
        if (!s_over[j].replaces) {
            if (k == 0) {
                return &s_over[j].p;
            }
            k--;
        }
    }
    return NULL;
}

const esc_profile_t *esc_profiles_find(const char *id)
{
    if (id == NULL) {
        return NULL;
    }
    const slot_t *s = over_find(id);
    return (s != NULL) ? &s->p : builtin_find(id);
}

bool esc_profiles_is_override(const esc_profile_t *p)
{
    for (size_t i = 0; i < s_over_n; ++i) {
        if (p == &s_over[i].p) {
            return true;
        }
    }
    return false;
}

bool esc_profile_file_is(const char *file_name, const char *id)
{
    if (file_name == NULL || id == NULL) {
        return false;
    }
    static const char k_ext[] = ".json";
    const size_t n = strlen(id);
    if (strlen(file_name) != n + sizeof(k_ext) - 1u) {
        return false;
    }
    for (size_t i = 0; i < n + sizeof(k_ext) - 1u; ++i) {
        const char want = (i < n) ? id[i] : k_ext[i - n];
        if (tolower((unsigned char)file_name[i])
            != tolower((unsigned char)want)) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------- search */

static unsigned char fold(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c - 'A' + 'a') : c;
}

/*
 * Byte @p i of @p a, a space and @p b read as one text of @p n bytes; @p b
 * NULL is @p a alone.  No copy is made, since a card profile's strings have
 * no length limit short of the file's.
 */
static unsigned char joined_at(const char *a, size_t na, const char *b,
                               size_t i)
{
    if (i < na) {
        return (unsigned char)a[i];
    }
    return (i == na) ? (unsigned char)' ' : (unsigned char)b[i - na - 1u];
}

/*
 * The wildcard match, with a "*" implied at either end.  On a mismatch the
 * last "*" takes one byte more and the rest of the pattern is tried again
 * from there: at most text times pattern comparisons, no recursion.  A
 * multi-byte character cannot match from its middle, because a UTF-8 lead
 * byte never equals a continuation byte.
 */
static bool match_joined(const char *a, const char *b, const char *pattern)
{
    const size_t na = strlen(a);
    const size_t n = (b != NULL) ? na + 1u + strlen(b) : na;
    const char *p = (pattern != NULL) ? pattern : "";
    const char *star_p = p;     /* the pattern after the last "*"       */
    size_t star_t = 0u;         /* where the text after it was tried    */
    size_t t = 0u;
    for (;;) {
        if (*p == '*') {
            while (*p == '*') {
                ++p;
            }
            star_p = p;
            star_t = t;
            continue;
        }
        if (*p == '\0') {
            return true;        /* the "*" implied at the end */
        }
        if (t < n && fold(joined_at(a, na, b, t)) == fold((unsigned char)*p)) {
            ++t;
            ++p;
            continue;
        }
        if (star_t >= n) {
            return false;
        }
        t = ++star_t;
        p = star_p;
    }
}

bool esc_text_matches(const char *text, const char *pattern)
{
    return match_joined((text != NULL) ? text : "", NULL, pattern);
}

bool esc_profile_matches(const esc_profile_t *p, const char *pattern)
{
    if (p == NULL) {
        return false;
    }
    const char *brand = (p->brand != NULL) ? p->brand : "";
    if (match_joined(brand, (p->family != NULL) ? p->family : "", pattern)) {
        return true;
    }
    for (uint16_t i = 0; p->models != NULL && i < p->model_count; ++i) {
        if (p->models[i].name != NULL
            && match_joined(brand, p->models[i].name, pattern)) {
            return true;
        }
    }
    return false;
}

bool esc_item_applies(const esc_profile_t *p, unsigned item, int model)
{
    if (p == NULL || item >= p->item_count) {
        return false;
    }
    const esc_item_t *it = &p->items[item];
    if (it->applies_count == 0u || it->applies_to == NULL) {
        return true;
    }
    if (model < 0 || (unsigned)model >= p->model_count) {
        return false;
    }
    const char *name = p->models[model].name;
    for (unsigned i = 0; i < it->applies_count; ++i) {
        if (name != NULL && it->applies_to[i] != NULL
            && strcmp(it->applies_to[i], name) == 0) {
            return true;
        }
    }
    return false;
}

bool esc_model_matches(const esc_profile_t *p, unsigned model,
                       const char *pattern)
{
    if (p == NULL || model >= p->model_count) {
        return false;
    }
    const char *brand = (p->brand != NULL) ? p->brand : "";
    const char *name = p->models[model].name;
    return match_joined(brand, (p->family != NULL) ? p->family : "", pattern)
           || (name != NULL && match_joined(brand, name, pattern));
}

unsigned esc_profile_manual_count(const esc_profile_t *p,
                                  esc_manual_when_t when)
{
    unsigned n = 0u;
    for (unsigned i = 0; p != NULL && p->manual != NULL
                         && i < p->manual_count; ++i) {
        n += (p->manual[i].when == when) ? 1u : 0u;
    }
    return n;
}
