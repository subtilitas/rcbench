/**
 * @file
 * @brief The names of a labelled servo's files.  See servo_name.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_name.h"

#include <stdio.h>
#include <string.h>

#include "card_tree.h"
#include "servo_id.h"

/** @brief The stems, by kind. */
static const char *const k_stems[SERVO_KIND_COUNT] = {
    [SERVO_KIND_LIMIT] = "limit run",
    [SERVO_KIND_RAUTO] = "rudder auto",
    [SERVO_KIND_ATEST] = "automatic test",
    [SERVO_KIND_RUN]   = "servo run",
};

/** @brief "_", 3 digits and an extension of 4: the characters after a stem. */
#define TAIL_LEN 8u

_Static_assert(sizeof("automatic test") - 1u + TAIL_LEN + 1u == SERVO_NAME_SIZE,
               "the longest name fills SERVO_NAME_SIZE");

servo_kind_t servo_kind_of_prefix(log_prefix_t p)
{
    switch (p) {
    case LOG_PREFIX_LIMIT: return SERVO_KIND_LIMIT;
    case LOG_PREFIX_RUDDR: return SERVO_KIND_RAUTO;
    case LOG_PREFIX_ATEST: return SERVO_KIND_ATEST;
    case LOG_PREFIX_SERVO: return SERVO_KIND_RUN;
    default:               return SERVO_KIND_COUNT;
    }
}

const char *servo_kind_stem(servo_kind_t k)
{
    return ((int)k >= 0 && k < SERVO_KIND_COUNT) ? k_stems[k] : "";
}

void servo_run_name(char *out, size_t n, servo_kind_t k, int number,
                    bool report)
{
    if (out == NULL || n == 0u) {
        return;
    }
    out[0] = '\0';
    if ((int)k < 0 || k >= SERVO_KIND_COUNT || number < SERVO_RUN_FIRST
        || number > SERVO_RUN_LAST) {
        return;
    }
    const int w = snprintf(out, n, "%s_%03d.%s", k_stems[k], number,
                           report ? "txt" : "csv");
    if (w < 0 || (size_t)w >= n) {
        out[0] = '\0';
    }
}

/**
 * @brief A character in lower case.
 * @param c The character.
 * @return @p c, A-Z taken as a-z.
 */
static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/**
 * @brief Whether @p name starts with @p want, any case.
 * @param name The text.
 * @param want The lower-case start.
 * @return true when it does.
 */
static bool starts(const char *name, const char *want)
{
    for (; *want != '\0'; ++name, ++want) {
        if (lower(*name) != *want) {
            return false;
        }
    }
    return true;
}

int servo_run_parse(const char *name, servo_kind_t *k, bool *report)
{
    if (name == NULL) {
        return -1;
    }
    for (int i = 0; i < (int)SERVO_KIND_COUNT; ++i) {
        const size_t sl = strlen(k_stems[i]);
        if (!starts(name, k_stems[i]) || strlen(name) != sl + TAIL_LEN) {
            continue;
        }
        const char *t = name + sl;
        if (t[0] != '_' || t[1] < '0' || t[1] > '9' || t[2] < '0'
            || t[2] > '9' || t[3] < '0' || t[3] > '9') {
            return -1;
        }
        const bool txt = starts(t + 4, ".txt");
        if (!txt && !starts(t + 4, ".csv")) {
            return -1;
        }
        if (k != NULL) {
            *k = (servo_kind_t)i;
        }
        if (report != NULL) {
            *report = txt;
        }
        return (t[1] - '0') * 100 + (t[2] - '0') * 10 + (t[3] - '0');
    }
    return -1;
}

void servo_run_report_of(char *out, size_t n, const char *run_name)
{
    if (out == NULL || n == 0u) {
        return;
    }
    servo_kind_t k;
    const int number = servo_run_parse(run_name, &k, NULL);
    if (number < 0) {
        out[0] = '\0';
        return;
    }
    servo_run_name(out, n, k, number, true);
}

bool servo_dir_parse(const char *dir, char *id, size_t n)
{
    static const char k_servos[] = "servos/";
    if (dir == NULL || !starts(dir, k_servos)) {
        return false;
    }
    servo_id_t p;
    if (!servo_id_parse(dir + sizeof(k_servos) - 1u, &p)) {
        return false;
    }
    if (id != NULL) {
        const size_t len = strlen(p.text);
        if (n <= len) {
            return false;
        }
        memcpy(id, p.text, len + 1u);
    }
    return true;
}
