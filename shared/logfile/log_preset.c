/**
 * @file
 * @brief The presets of the files the bench writes, and the match of a file
 *        to one.  See log_preset.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "log_preset.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include "log_fields.h"
#include "log_name.h"

/** @brief The seven presets, in log_preset_id_t order from LOG_PRESET_LIMIT. */
static const log_preset_t k_presets[LOG_PRESET_COUNT - 1] = {
    { LOG_PRESET_LIMIT, LOG_PLOT_XY, "pulse",
      { "current", "angle", NULL, NULL }, "search" },
    { LOG_PRESET_RUDDR_SEARCH, LOG_PLOT_XY, "pulse",
      { "current", "measured", NULL, NULL }, "side" },
    { LOG_PRESET_RUDDR_STEPS, LOG_PLOT_XY, "step",
      { "measured", "goal", NULL, NULL }, "side" },
    { LOG_PRESET_ATEST, LOG_PLOT_TIME, "time",
      { "command", "current", "voltage", "angle" }, NULL },
    { LOG_PRESET_SERVO, LOG_PLOT_TIME, "time",
      { "pulse", "angle", "current", "voltage" }, NULL },
    { LOG_PRESET_MOTOR, LOG_PLOT_TIME, "time",
      { "throttle", "rpm", "current", "voltage" }, NULL },
    { LOG_PRESET_SUPPL, LOG_PLOT_TIME, "time",
      { "set", "voltage", "limit", "current" }, NULL },
};

/** @brief The most columns a header signature names. */
#define SIG_MAX 4

/** @brief The columns a header must hold for a preset. */
typedef struct {
    log_preset_id_t id;              /**< the preset they name            */
    const char *cols[SIG_MAX];       /**< the columns; NULL ends the list */
} preset_sig_t;

/**
 * @brief The header signatures in the order they are tried: an AUTOMATIC
 *        TEST's file holds SUPPL's `set`, `limit` and `mode` as well, so it
 *        is tried first.
 */
static const preset_sig_t k_sigs[] = {
    { LOG_PRESET_LIMIT, { "search", "side", "pulse", "state" } },
    { LOG_PRESET_RUDDR_SEARCH, { "step", "side", "write", "within" } },
    { LOG_PRESET_ATEST, { "test", "phase", "command", NULL } },
    { LOG_PRESET_SUPPL, { "set", "limit", "mode", NULL } },
    { LOG_PRESET_MOTOR, { "rpm", "throttle", NULL, NULL } },
    { LOG_PRESET_SERVO, { "pulse", "angle", "source", NULL } },
};

/** @brief The columns of a bench file of firmware 0.21.0 and older that
 *         neither throttle nor pulse tells apart. */
static const char *const k_old_bench[SIG_MAX] = {
    "rpm", "esc", "motor", "window",
};

const log_preset_t *log_preset_get(log_preset_id_t id)
{
    if (id <= LOG_PRESET_NONE || id >= LOG_PRESET_COUNT) {
        return NULL;
    }
    return &k_presets[id - 1];
}

/**
 * @brief Whether the file holds every column of a list, of any kind.
 * @param an    The file's analysis.
 * @param cols  The columns; NULL ends a list shorter than SIG_MAX.
 * @return true when every one is there.
 */
static bool holds_all(const log_analysis_t *an, const char *const *cols)
{
    for (int k = 0; k < SIG_MAX && cols[k] != NULL; ++k) {
        if (log_fields_find(an, cols[k], false) < 0) {
            return false;
        }
    }
    return true;
}

log_preset_id_t log_preset_by_header(const log_analysis_t *an)
{
    if (an == NULL) {
        return LOG_PRESET_NONE;
    }
    for (size_t i = 0; i < sizeof(k_sigs) / sizeof(k_sigs[0]); ++i) {
        if (holds_all(an, k_sigs[i].cols)) {
            return k_sigs[i].id;
        }
    }
    return LOG_PRESET_NONE;
}

log_preset_id_t log_preset_by_name(const char *name)
{
    log_prefix_t prefix = LOG_PREFIX_COUNT;
    bool report = true;
    if (name == NULL || log_name_parse(name, &prefix, &report) < 0 || report) {
        return LOG_PRESET_NONE;
    }
    /* In log_prefix_t order: BENCHnnn was a run of the surfaces alone or a
     * supply run (its header tells that one apart), ESCnnn a run of the
     * throttle. */
    static const log_preset_id_t k_by_prefix[LOG_PREFIX_COUNT] = {
        [LOG_PREFIX_ATEST] = LOG_PRESET_ATEST,
        [LOG_PREFIX_LIMIT] = LOG_PRESET_LIMIT,
        [LOG_PREFIX_SERVO] = LOG_PRESET_SERVO,
        [LOG_PREFIX_MOTOR] = LOG_PRESET_MOTOR,
        [LOG_PREFIX_SUPPL] = LOG_PRESET_SUPPL,
        [LOG_PREFIX_RUDDR] = LOG_PRESET_RUDDR_SEARCH,
        [LOG_PREFIX_BENCH] = LOG_PRESET_SERVO,
        [LOG_PREFIX_ESC]   = LOG_PRESET_MOTOR,
    };
    return ((unsigned)prefix < (unsigned)LOG_PREFIX_COUNT)
               ? k_by_prefix[prefix] : LOG_PRESET_NONE;
}

log_preset_id_t log_preset_match(const log_analysis_t *an, const char *name)
{
    const log_preset_id_t by_header = log_preset_by_header(an);
    if (by_header != LOG_PRESET_NONE) {
        return by_header;
    }
    const log_preset_id_t by_name = log_preset_by_name(name);
    if (by_name != LOG_PRESET_NONE) {
        return by_name;
    }
    return (an != NULL && holds_all(an, k_old_bench)) ? LOG_PRESET_MOTOR
                                                      : LOG_PRESET_NONE;
}

/**
 * @brief Whether column name @p col ends in a space and @p word, its case
 *        folded: `ch1 current` ends in ` current`.
 * @param col   The column's name.
 * @param word  The word.
 * @return true when it does.
 */
static bool ends_in(const char *col, const char *word)
{
    const size_t nc = strlen(col);
    const size_t nw = strlen(word);
    if (nc <= nw || col[nc - nw - 1] != ' ') {
        return false;
    }
    for (size_t i = 0; i < nw; ++i) {
        if (tolower((unsigned char)col[nc - nw + i])
            != tolower((unsigned char)word[i])) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Whether column @p c is among the first @p n of @p out.
 * @param out  The columns picked.
 * @param n    How many.
 * @param c    The column.
 * @return true when it is picked.
 */
static bool picked(const int *out, int n, int c)
{
    for (int k = 0; k < n; ++k) {
        if (out[k] == c) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Add the numeric column @p name to @p out where the file holds it.
 *        On a time plot a file without it numeric gives the first numeric
 *        column whose name ends in ` <name>`: a run whose `current` and
 *        `voltage` cells are empty plots its INA3221 channel's `ch1
 *        current` and `ch1 voltage`.  No preset names a column twice.
 * @param an    The file's analysis.
 * @param name  The column's name.
 * @param time  The preset is a time plot.
 * @param out   The columns picked so far.
 * @param n     How many.
 * @return The new count.
 */
static int pick(const log_analysis_t *an, const char *name, bool time,
                int *out, int n)
{
    int c = log_fields_find(an, name, true);
    for (int k = 0; c < 0 && time && k < an->n_columns
                    && k < LOG_MAX_COLUMNS; ++k) {
        if (k != an->time_index && an->columns[k].numeric
            && !picked(out, n, k) && ends_in(an->columns[k].name, name)) {
            c = k;
        }
    }
    if (c < 0) {
        return n;
    }
    out[n] = c;
    return n + 1;
}

int log_preset_picks(log_preset_id_t id, const log_analysis_t *an, int *out,
                     int max)
{
    const log_preset_t *p = log_preset_get(id);
    if (p == NULL || an == NULL || out == NULL) {
        return 0;
    }
    const bool time = p->plot == LOG_PLOT_TIME;
    int n = 0;
    if (!time && n < max) {
        n = pick(an, p->x, false, out, n);
    }
    for (int k = 0; k < LOG_MAX_SERIES && p->series[k] != NULL && n < max;
         ++k) {
        n = pick(an, p->series[k], time, out, n);
    }
    return n;
}
