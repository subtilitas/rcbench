/*
 * Blackbox field naming: which group a channel belongs to, and what it is
 * measured in.
 *
 * Every pattern is an anchored prefix or an exact name, so a table of
 * prefixes is sufficient and no regular-expression engine is needed on the
 * board.
 *
 * A CSV (comma-separated values) file exported from a blackbox tool carries
 * exactly these column names, so the grouping applies to it directly;
 * anything else is grouped by unit, which keeps related channels together.
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
