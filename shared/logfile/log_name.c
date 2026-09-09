/*
 * Run log names, and the order a bounded listing keeps them in.
 *
 * SPDX-License-Identifier: MIT
 */

#include "log_name.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define RUN_PREFIX     "BENCH"
#define RUN_PREFIX_LEN 5
#define RUN_DIGITS     3
#define RUN_SUFFIX     ".CSV"
#define RUN_NAME_LEN   (LOG_RUN_NAME_MAX - 1)

_Static_assert(RUN_PREFIX_LEN + RUN_DIGITS + (sizeof(RUN_SUFFIX) - 1u)
                   == (size_t)RUN_NAME_LEN,
               "a run's name is its three parts and nothing else");

/*
 * Does @p name open with @p pat, case folded?  @p name holds at least
 * @p pat_len characters; the caller checks its length first.
 *
 * Case folded, because the same rule has to hold for a card taken to a
 * computer and written back as bench042.csv.
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

void log_run_name(char *out, size_t out_size, int number)
{
    if (out == NULL || out_size == 0u) {
        return;
    }
    out[0] = '\0';
    if (out_size < (size_t)LOG_RUN_NAME_MAX) {
        return;
    }
    if (number < LOG_RUN_FIRST || number > LOG_RUN_LAST) {
        return;
    }
    snprintf(out, out_size, RUN_PREFIX "%03d" RUN_SUFFIX, number);
}

int log_run_number(const char *name)
{
    /*
     * A run's name has one shape and one length.  The length is taken first,
     * which settles "nothing after the suffix" and puts every read below
     * inside the name.
     */
    if (name == NULL || strlen(name) != (size_t)RUN_NAME_LEN) {
        return -1;
    }
    if (!prefix_matches(name, RUN_PREFIX, RUN_PREFIX_LEN)) {
        return -1;
    }
    const char *p = name + RUN_PREFIX_LEN;
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
    if (log_name_case_cmp(p + RUN_DIGITS, RUN_SUFFIX) != 0) {
        return -1;
    }
    return v;
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
        return rb - ra;     /* the higher number is the later run */
    }
    return log_name_case_cmp(a, b);
}
