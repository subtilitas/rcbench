/*
 * Locale-tolerant numeric parsing with unit suffixes.
 *
 * A port of logwiju's numbers.js.  The hard part is that "1,234" is genuinely
 * ambiguous: German reads 1.234, English reads 1234.  Guessing per value is
 * unsafe, because the same column would then parse inconsistently -- "1,234"
 * as 1234 on one row and "10,23" as 10.23 on the next implies two different
 * conventions in one column, which no real exporter produces.
 *
 * So the convention is decided per *file*, from every value in it.  Most files
 * contain at least one value that settles it:
 *
 *   "1.234,56"  both separators  -> the last one is the decimal   (German)
 *   "1,234.56"  both separators  -> the last one is the decimal   (English)
 *   "10,23"     one, 2 trailing  -> not a thousands group         (German)
 *   "1.5"       one, 1 trailing  -> not a thousands group         (English)
 *   "1.234.567" repeated         -> that separator groups         (German)
 *   "1,234"     one, 3 trailing  -> ambiguous, no evidence
 *
 * Only when every value is ambiguous does the fallback apply.
 *
 * The evidence gatherer is a streaming accumulator rather than a function over
 * an array, because the firmware reads logs off a card and never holds the
 * whole file: values arrive once, are voted on, and are forgotten.
 *
 * Pure C, no ESP-IDF.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest numeric body and unit suffix we will look at in one cell. */
#define LOG_DIGITS_MAX 40
#define LOG_UNIT_MAX   12

typedef enum {
    LOG_CONV_AUTO = 0, /**< only ever an *input*: "decide for me" */
    LOG_CONV_DE,       /**< 1.234,56 */
    LOG_CONV_EN        /**< 1,234.56 */
} log_conv_t;

/** What a wholly ambiguous file defaults to. */
typedef enum {
    LOG_AMBIG_THOUSANDS = 0, /**< "1,234" -> 1234    */
    LOG_AMBIG_DECIMAL        /**< "1,234" -> 1.234   */
} log_ambig_t;

/** What one numeric string proves about the convention. */
typedef enum {
    LOG_EV_NONE = 0,  /**< no separator at all: "42"    */
    LOG_EV_AMBIGUOUS, /**< one separator, 3 trailing    */
    LOG_EV_DE,
    LOG_EV_EN
} log_evidence_t;

/** A cell split into its parts.  @c digits keeps its separators. */
typedef struct {
    char digits[LOG_DIGITS_MAX];
    char unit[LOG_UNIT_MAX];
    int  sign; /**< +1 or -1 */
    int  exp;  /**< explicit exponent, 0 when absent */
} log_value_t;

/**
 * Split a raw cell into numeric text, exponent and unit.
 *
 * Grouping characters that are never decimal points -- ASCII space, the
 * apostrophe Swiss exporters use, U+00A0 and U+202F -- are removed here, so
 * @c digits carries only digits, '.' and ','.
 *
 * @return false when the cell holds no number at all.
 */
bool log_split_value(const char *raw, log_value_t *out);

/** Inspect one already-split numeric string and report what it proves. */
log_evidence_t log_evidence_of(const char *digits);

/** Running tally of the evidence seen so far. */
typedef struct {
    int  de;
    int  en;
    int  ambiguous;
    char ambiguous_sep; /**< the separator the first ambiguous value used */
} log_votes_t;

void log_votes_reset(log_votes_t *v);

/** Feed one raw cell.  Cells that hold no number are ignored. */
void log_votes_add(log_votes_t *v, const char *raw);

/**
 * Decide the convention from the tally.
 *
 * @param confident  set false when nothing proved anything and the fallback
 *                   had to decide.
 * @param conflict   set true when both conventions were proven, which means
 *                   the file itself is inconsistent -- worth telling the user
 *                   rather than silently picking the winner.
 */
log_conv_t log_votes_result(const log_votes_t *v, log_ambig_t fallback,
                            bool *confident, bool *conflict);

/**
 * Is @p digits well formed under @p convention?
 *
 * Strict on purpose.  Merely stripping the grouping separator and hoping the
 * result parses will happily turn the German "1.234,56" into 1.23456 when told
 * to read it as English -- a silently wrong number, which is worse than no
 * number at all.  Rejecting lets the caller surface the mismatch.
 */
bool log_is_well_formed(const char *digits, log_conv_t convention);

/**
 * Parse one cell with a known convention.
 *
 * @return false when the cell does not conform, rather than guessing.
 */
bool log_parse_with(const char *raw, log_conv_t convention,
                    double *value, char *unit, size_t unit_size);

/** Parse a standalone value, inferring the convention from it alone. */
bool log_parse_loose(const char *raw, log_ambig_t fallback, double *value);

/* --------------------------------------------------------------- units --- */

/** Tally of unit suffixes seen in one column, in first-seen order. */
#define LOG_UNIT_SLOTS 4
typedef struct {
    char units[LOG_UNIT_SLOTS][LOG_UNIT_MAX];
    int  counts[LOG_UNIT_SLOTS];
    int  used;
    bool overflowed; /**< more distinct units than slots: certainly mixed */
} log_unit_tally_t;

void log_unit_tally_reset(log_unit_tally_t *t);
void log_unit_tally_add(log_unit_tally_t *t, const char *unit);

/** The most common non-empty suffix.  @p mixed reports more than one. */
void log_unit_dominant(const log_unit_tally_t *t, char *out, size_t out_size,
                       bool *mixed);

/**
 * Pull a unit out of a column header: "voltage (V)", "current [A]",
 * "speed km/h", "power (W)".  "gyroADC[0]" keeps its index -- that is an axis
 * number, not a unit.
 */
void log_unit_from_header(const char *header, char *name, size_t name_size,
                          char *unit, size_t unit_size);

/** Seconds per unit for a time column, or 0 when the name is not a time unit. */
double log_time_unit_scale(const char *unit);

#ifdef __cplusplus
}
#endif
