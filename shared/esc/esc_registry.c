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

bool esc_profiles_override(const esc_profile_t *p, void *block)
{
    if (p == NULL || p->id == NULL) {
        free(block);
        return false;
    }
    slot_t *s = over_find(p->id);
    if (s != NULL) {
        /* The later file wins; the earlier one's memory goes with it. */
        free(s->block);
    } else if (s_over_n < ESC_PROFILE_MAX_OVERRIDES) {
        s = &s_over[s_over_n++];
    } else {
        free(block);
        return false;
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
