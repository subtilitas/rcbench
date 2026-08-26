/*
 * Blackbox field naming: which group a channel belongs to, and what it is
 * measured in.
 *
 * Upstream matches these with regular expressions.  Every one of those
 * patterns is an anchored prefix or an exact name, so this is a table of
 * prefixes instead -- a regex engine on the board would earn its size in no
 * other place.
 *
 * A CSV exported from a blackbox tool has exactly these column names, so the
 * grouping carries over for free; anything else falls back to grouping by
 * unit, which keeps related channels together anyway.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *group;
    const char *unit;
} log_field_meta_t;

/** Group and unit for a channel name.  Never NULL; "Other"/"" when unknown. */
log_field_meta_t log_field_meta(const char *name);

#ifdef __cplusplus
}
#endif
