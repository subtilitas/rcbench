/**
 * @file
 * @brief Run log names, and the order a bounded listing keeps them in.
 *
 * SPDX-License-Identifier: MIT
 */

#include "log_name.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/** @brief Digits in a run number. */
#define RUN_DIGITS     3
/** @brief Suffix of a run log. */
#define RUN_SUFFIX     ".CSV"
/** @brief Suffix of a run's report. */
#define REPORT_SUFFIX  ".TXT"
/** @brief Length of a suffix, characters. */
#define SUFFIX_LEN     4

_Static_assert(sizeof(RUN_SUFFIX) - 1u == SUFFIX_LEN
                   && sizeof(REPORT_SUFFIX) == sizeof(RUN_SUFFIX),
               "a report's name is as long as its run's");

/**
 * @brief The writers' prefixes, in log_prefix_t order.
 */
static const char *const k_prefix[LOG_PREFIX_COUNT] = {
    [LOG_PREFIX_ATEST] = "ATEST",
    [LOG_PREFIX_LIMIT] = "LIMIT",
    [LOG_PREFIX_SERVO] = "SERVO",
    [LOG_PREFIX_MOTOR] = "MOTOR",
    [LOG_PREFIX_SUPPL] = "SUPPL",
    [LOG_PREFIX_BENCH] = "BENCH",
    [LOG_PREFIX_ESC]   = "ESC",
};

/* The longest prefix fills the buffer exactly, and is an 8.3 base name: at
 * most 8 characters before the dot. */
_Static_assert(5 + RUN_DIGITS + SUFFIX_LEN == LOG_RUN_NAME_MAX - 1,
               "SUPPL999.CSV and ATEST999.CSV fill the buffer");
_Static_assert(LOG_PREFIX_WRITTEN == 5 && LOG_NAME_FORMS == 10,
               "five prefixes written, ten names probed");
_Static_assert(5 + RUN_DIGITS <= 8, "an 8.3 base name");

/**
 * @brief Does @p name open with @p pat, case folded?  @p name holds at least @p
 *        pat_len characters; the caller checks its length first.
 *
 * Case folded, because the same rule has to hold for a card taken to a
 * computer and written back as bench042.csv.
 *
 * @param name     The file name.
 * @param pat      The prefix.
 * @param pat_len  Length of @p pat, characters.
 * @return true when the first @p pat_len characters match.
 */
static bool prefix_matches(const char *name, const char *pat, size_t pat_len)
{
    for (size_t i = 0u; i < pat_len; ++i) {
        if (tolower((unsigned char)name[i]) != tolower((unsigned char)pat[i])) {
            return false;
        }
    }
    return true;
}

int log_name_case_cmp(const char *a, const char *b)
{
    for (size_t i = 0u;; ++i) {
        const int ca = tolower((unsigned char)a[i]);
        const int cb = tolower((unsigned char)b[i]);
        if (ca != cb) {
            return (ca < cb) ? -1 : 1;
        }
        if (ca == '\0') {
            return 0;
        }
    }
}

void log_name_build(char *out, size_t out_size, log_prefix_t prefix,
                    int number, bool report)
{
    if (out == NULL || out_size == 0u) {
        return;
    }
    out[0] = '\0';
    if (out_size < (size_t)LOG_RUN_NAME_MAX) {
        return;
    }
    if ((unsigned)prefix >= (unsigned)LOG_PREFIX_COUNT) {
        return;
    }
    if (number < LOG_RUN_FIRST || number > LOG_RUN_LAST) {
        return;
    }
    snprintf(out, out_size, "%s%03d%s", k_prefix[prefix], number,
             report ? REPORT_SUFFIX : RUN_SUFFIX);
}

log_prefix_t log_name_armed_prefix(bool asked, bool asked_throttle,
                                   bool throttle_armed)
{
    const bool motor = asked ? asked_throttle : throttle_armed;
    return motor ? LOG_PREFIX_MOTOR : LOG_PREFIX_SERVO;
}

void log_run_name(char *out, size_t out_size, int number)
{
    log_name_build(out, out_size, LOG_PREFIX_BENCH, number, false);
}

void log_report_name(char *out, size_t out_size, int number)
{
    log_name_build(out, out_size, LOG_PREFIX_BENCH, number, true);
}

/**
 * @brief The number in @p name under @p suffix, any prefix, or -1; @p prefix,
 *        when not NULL, takes the prefix's index.
 *
 * @param name    The file name.
 * @param suffix  The suffix the name must end with.
 * @param prefix  Receives the index of the prefix, or NULL.
 * @return The run number, or -1 when the name is not one.
 */
static int numbered(const char *name, const char *suffix, int *prefix)
{
    if (name == NULL) {
        return -1;
    }
    /*
     * A run's name has one shape and one length per prefix.  The length is
     * taken first, which settles "nothing after the suffix" and puts every
     * read below inside the name.
     */
    const size_t len = strlen(name);
    for (int k = 0; k < (int)LOG_PREFIX_COUNT; ++k) {
        const size_t plen = strlen(k_prefix[k]);
        if (len != plen + RUN_DIGITS + SUFFIX_LEN
            || !prefix_matches(name, k_prefix[k], plen)) {
            continue;
        }
        const char *p = name + plen;
        int v = 0;
        for (int i = 0; i < RUN_DIGITS; ++i) {
            if (p[i] < '0' || p[i] > '9') {
                return -1;
            }
            v = (v * 10) + (p[i] - '0');
        }
        if (v < LOG_RUN_FIRST || v > LOG_RUN_LAST) {
            return -1;
        }
        if (log_name_case_cmp(p + RUN_DIGITS, suffix) != 0) {
            return -1;
        }
        if (prefix != NULL) {
            *prefix = k;
        }
        return v;
    }
    return -1;
}

int log_run_number(const char *name)
{
    return numbered(name, RUN_SUFFIX, NULL);
}

int log_report_number(const char *name)
{
    return numbered(name, REPORT_SUFFIX, NULL);
}

int log_name_number(const char *name)
{
    const int n = log_run_number(name);
    return (n >= 0) ? n : log_report_number(name);
}

void log_name_form(char *out, size_t out_size, int number, int form)
{
    if (out != NULL && out_size > 0u) {
        out[0] = '\0';
    }
    if (form < 0 || form >= LOG_NAME_FORMS) {
        return;
    }
    log_name_build(out, out_size, (log_prefix_t)(form / 2), number,
                   (form % 2) != 0);
}

int log_name_next(int highest)
{
    if (highest < LOG_RUN_FIRST) {
        return LOG_RUN_FIRST;
    }
    if (highest >= LOG_RUN_LAST) {
        return LOG_RUN_LAST + 1;    /* full */
    }
    return highest + 1;
}

void log_name_report_of(char *out, size_t out_size, const char *run_name)
{
    int k = -1;
    const int n = numbered(run_name, RUN_SUFFIX, &k);
    if (n < 0) {
        if (out != NULL && out_size > 0u) {
            out[0] = '\0';
        }
        return;
    }
    log_name_build(out, out_size, (log_prefix_t)k, n, true);
}

bool log_name_busy(const char *name, unsigned open_run, unsigned open_test,
                   unsigned open_txt)
{
    const int n = log_run_number(name);
    const int r = log_report_number(name);
    /*
     * A run log under @p open_txt is busy too.  Deleting a run deletes its
     * report (log_name_report_of()), and that report is the one still open:
     * with no file lock in FatFs, the unlink frees clusters the logger goes
     * on writing into.
     */
    return (n > 0 && ((open_run != 0u && (unsigned)n == open_run)
                      || (open_test != 0u && (unsigned)n == open_test)
                      || (open_txt != 0u && (unsigned)n == open_txt)))
        || (r > 0 && open_txt != 0u && (unsigned)r == open_txt);
}

int log_name_rank(const char *a, const char *b)
{
    if (a == NULL) {
        a = "";
    }
    if (b == NULL) {
        b = "";
    }
    const int ra = log_run_number(a);
    const int rb = log_run_number(b);
    if (ra >= 0 || rb >= 0) {
        if (ra < 0) {
            return 1;
        }
        if (rb < 0) {
            return -1;
        }
        if (ra != rb) {
            return rb - ra;     /* the higher number is the later run */
        }
        /* One number under two names only on a card edited on a computer:
         * by name, so the order does not hang on which arrived first. */
    }
    return log_name_case_cmp(a, b);
}
