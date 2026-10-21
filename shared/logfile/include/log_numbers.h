/**
 * @file
 * @brief Locale-tolerant numeric parsing with unit suffixes.
 *
 * "1,234" is ambiguous: German reads 1.234, English reads 1234.  Guessing per
 * value is unsafe, because the same column would then parse inconsistently:
 * "1,234" as 1234 on one row and "10,23" as 10.23 on the next implies two
 * conventions in one column, which no exporter produces.
 *
 * So the convention is decided per file, from every value in it.  Most files
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
 * The evidence gatherer is a streaming accumulator rather than a function
 * over an array: the firmware reads logs off a card and never holds the whole
 * file.  Values arrive once, are voted on, and are forgotten.
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

/**
 * @brief Longest numeric body and unit suffix the parser reads in one cell.
 */
#define LOG_DIGITS_MAX 40
/** @brief Longest unit suffix the parser reads in one cell, characters. */
#define LOG_UNIT_MAX   12

/** @brief Decimal convention of a file. */
typedef enum {
    LOG_CONV_AUTO = 0, /**< only ever an *input*: "decide for me" */
    LOG_CONV_DE,       /**< 1.234,56 */
    LOG_CONV_EN        /**< 1,234.56 */
} log_conv_t;

/**
 * @brief What a wholly ambiguous file defaults to.
 */
typedef enum {
    LOG_AMBIG_THOUSANDS = 0, /**< "1,234" -> 1234    */
    LOG_AMBIG_DECIMAL        /**< "1,234" -> 1.234   */
} log_ambig_t;

/**
 * @brief What one numeric string proves about the convention.
 */
typedef enum {
    LOG_EV_NONE = 0,  /**< no separator at all: "42"    */
    LOG_EV_AMBIGUOUS, /**< one separator, 3 trailing    */
    LOG_EV_DE,
    LOG_EV_EN
} log_evidence_t;

/**
 * @brief A cell split into its parts.  @c digits keeps its separators.
 */
typedef struct {
    char digits[LOG_DIGITS_MAX];  /**< numeric text, separators kept       */
    char unit[LOG_UNIT_MAX];      /**< unit suffix, "" when none           */
    int  sign; /**< +1 or -1 */
    int  exp;  /**< explicit exponent, 0 when absent */
} log_value_t;

/**
 * @brief Split a raw cell into numeric text, exponent and unit.
 *
 * Grouping characters that are never decimal points -- ASCII space, the
 * apostrophe Swiss exporters use, U+00A0 and U+202F -- are removed here, so
 * @c digits carries only digits, '.' and ','.
 *
 * @param raw  The raw cell text.
 * @param out  Receives the parts.
 * @return false when the cell holds no number at all.
 */
bool log_split_value(const char *raw, log_value_t *out);

/**
 * @brief Inspect one already-split numeric string and report what it proves.
 *
 * @param digits  Numeric text as log_split_value() gives it.
 * @return The evidence.
 */
log_evidence_t log_evidence_of(const char *digits);

/**
 * @brief Running tally of the evidence seen so far.
 */
typedef struct {
    int  de;           /**< cells that prove German, "1.234,56"            */
    int  en;           /**< cells that prove English, "1,234.56"           */
    int  ambiguous;    /**< cells with one separator and 3 trailing        */
    char ambiguous_sep; /**< the separator the first ambiguous value used */
} log_votes_t;

/**
 * @brief Clear the tally.
 *
 * @param v  The tally.
 */
void log_votes_reset(log_votes_t *v);

/**
 * @brief Feed one raw cell.
 *
 * Cells that hold no number are ignored.
 *
 * @param v    The tally.
 * @param raw  The raw cell text.
 */
void log_votes_add(log_votes_t *v, const char *raw);

/**
 * @brief Decide the convention from the tally.
 *
 * @param v         The tally.
 * @param fallback  The default for a wholly ambiguous file.
 * @param confident set false when nothing proved anything and the fallback
 *                  had to decide.
 * @param conflict  set true when both conventions were proven, which means
 *                  the file itself is inconsistent; the caller reports it
 *                  rather than silently picking the winner.
 * @return The convention.
 */
log_conv_t log_votes_result(const log_votes_t *v, log_ambig_t fallback,
                            bool *confident, bool *conflict);

/**
 * @brief Is @p digits well formed under @p convention?
 *
 * Strict on purpose.  Stripping the grouping separator and parsing what is
 * left turns the German "1.234,56" into 1.23456 when read as English: a
 * silently wrong number.  Rejecting lets the caller surface the mismatch.
 *
 * @param digits      Numeric text as log_split_value() gives it.
 * @param convention  The convention to check against.
 * @return true when the text is well formed.
 */
bool log_is_well_formed(const char *digits, log_conv_t convention);

/**
 * @brief Parse one cell with a known convention.
 *
 * @param raw        The raw cell text.
 * @param convention The convention of the file.
 * @param value      Receives the number.
 * @param unit       Receives the unit suffix.
 * @param unit_size  Size of @p unit, bytes.
 * @return false when the cell does not conform, rather than guessing.
 */
bool log_parse_with(const char *raw, log_conv_t convention,
                    double *value, char *unit, size_t unit_size);

/**
 * @brief Parse a standalone value, inferring the convention from it alone.
 *
 * @param raw       The raw cell text.
 * @param fallback  The default for an ambiguous value.
 * @param value     Receives the number.
 * @return true when the value parsed.
 */
bool log_parse_loose(const char *raw, log_ambig_t fallback, double *value);

/**
 * @brief Tally of unit suffixes seen in one column, in first-seen order.
 */
#define LOG_UNIT_SLOTS 4
/* --------------------------------------------------------------- units --- */

/** @brief Tally of unit suffixes seen in one column, in first-seen order. */
typedef struct {
    char units[LOG_UNIT_SLOTS][LOG_UNIT_MAX];  /**< one suffix per slot    */
    int  counts[LOG_UNIT_SLOTS];               /**< cells per suffix       */
    int  used;                                 /**< slots in use           */
    bool overflowed; /**< more distinct units than slots: certainly mixed */
} log_unit_tally_t;

/**
 * @brief Clear a unit tally.
 *
 * @param t  The tally.
 */
void log_unit_tally_reset(log_unit_tally_t *t);

/**
 * @brief Add a unit suffix to a tally.
 *
 * @param t     The tally.
 * @param unit  The unit suffix.
 */
void log_unit_tally_add(log_unit_tally_t *t, const char *unit);

/**
 * @brief The most common non-empty suffix.  @p mixed reports more than one.
 *
 * @param t         The tally.
 * @param out       Receives the suffix.
 * @param out_size  Size of @p out, bytes.
 * @param mixed     Receives true when more than one suffix was seen.
 */
void log_unit_dominant(const log_unit_tally_t *t, char *out, size_t out_size,
                       bool *mixed);

/**
 * @brief Pull a unit out of a column header: "voltage (V)", "current [A]",
 *        "speed km/h", "power (W)".  "gyroADC[0]" keeps its index -- that is an
 *        axis number, not a unit.
 *
 * @param header     The column header.
 * @param name       Receives the name without the unit.
 * @param name_size  Size of @p name, bytes.
 * @param unit       Receives the unit.
 * @param unit_size  Size of @p unit, bytes.
 */
void log_unit_from_header(const char *header, char *name, size_t name_size,
                          char *unit, size_t unit_size);

/**
 * @brief Seconds per unit for a time column, or 0 when the name is not a time
 *        unit.
 *
 * @param unit  The unit name.
 * @return Seconds per unit, or 0.
 */
double log_time_unit_scale(const char *unit);

#ifdef __cplusplus
}
#endif
