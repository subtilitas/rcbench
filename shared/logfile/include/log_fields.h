/**
 * @file
 * @brief Blackbox field naming: which group a channel belongs to, and what it
 *        is measured in.
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

#include "log_csv.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Group and unit of one channel. */
typedef struct {
    const char *group;   /**< group the channel belongs to                 */
    const char *unit;    /**< unit the channel is measured in               */
} log_field_meta_t;

/**
 * @brief Group and unit for a channel name.
 *
 * Never NULL; "Other"/"" when unknown.
 *
 * @param name  The channel name.
 * @return The group and unit of the channel.
 */
log_field_meta_t log_field_meta(const char *name);

/**
 * @brief The columns LOGS plots in a file before the operator picks any.
 *
 * In a file with a numeric `pulse` column, a SERVO run's, its `pulse`,
 * `angle`, `current` and `voltage` first, each where it is numeric; then,
 * and in any other file, the first numeric columns in file order.  Never
 * the time axis, and no column twice.
 *
 * @param an  The file's analysis.
 * @param out Receives the column indices.
 * @param max The most columns to pick.
 * @return The number of columns picked; 0 with @p an or @p out NULL.
 */
int log_fields_picks(const log_analysis_t *an, int *out, int max);

#ifdef __cplusplus
}
#endif
