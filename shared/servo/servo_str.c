/**
 * @file
 * @brief The automatic servo test's words: the CSV header, the phase and abort
 *        names, and the line formats of the TXT report (servo_report.c).
 *
 * Every string the run writes or the SERVO screen shows about it is an
 * entry of ui_text.def (shared/text), with its English and its German;
 * k_str is the English column, and ui_text.c makes the other languages'
 * tables of the same entries.  The report is written in the language of
 * the table the run was started with, and the CSV always in English,
 * because tools read it by its words.  A report is UTF-8, as the table's
 * strings are.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_str.h"
#include "servo_test.h"

#include <stddef.h>

/** @brief The servo test's words in English, from ui_text.def. */
static const char *const k_str[SERVO_STR_COUNT] = {
/// @cond X_MACRO
#define UI_SERVO_TEXT(id, en, de) [id] = (en),
#include "ui_text.def"
/// @endcond
};

/** @brief The count of UI_SERVO_TEXT entries in ui_text.def. */
enum {
    k_str_entries = 0
/// @cond X_MACRO
/* A term of the sum, not an expression of its own. */
/* NOLINTNEXTLINE(bugprone-macro-parentheses) */
#define UI_SERVO_TEXT(id, en, de) + 1
#include "ui_text.def"
/// @endcond
};

/* An ID without an entry would read as "" with no error.  A duplicate
 * entry fails the host suite's and the IOmcu's builds (-Wextra turns on
 * -Woverride-init). */
_Static_assert((int)k_str_entries == (int)SERVO_STR_COUNT,
               "every SERVO_STR_ ID has one UI_SERVO_TEXT entry");

const char *servo_str_in(const char *const *table, servo_str_t id)
{
    if ((unsigned)id >= (unsigned)SERVO_STR_COUNT) {
        return "";
    }
    if (table != NULL && table[id] != NULL) {
        return table[id];
    }
    return (k_str[id] != NULL) ? k_str[id] : "";
}

const char *servo_str(servo_str_t id)
{
    return servo_str_in(NULL, id);
}

servo_str_t servo_test_phase_str(servo_test_phase_t ph)
{
    if ((unsigned)ph > (unsigned)SERVO_TEST_PH_HOLD) {
        return SERVO_STR_PHASE_NONE;
    }
    return (servo_str_t)(SERVO_STR_PHASE_NONE + (int)ph);
}

servo_str_t servo_test_abort_str(servo_test_abort_t why)
{
    if ((unsigned)why >= (unsigned)SERVO_TEST_AB_COUNT) {
        return SERVO_STR_AB_NONE;
    }
    return (servo_str_t)(SERVO_STR_AB_NONE + (int)why);
}

servo_str_t servo_test_verdict_str(servo_test_verdict_t v)
{
    switch (v) {
    case SERVO_TEST_PASS: return SERVO_STR_PASS;
    case SERVO_TEST_FAIL: return SERVO_STR_FAIL;
    case SERVO_TEST_NOT_MEASURABLE: return SERVO_STR_NOT_MEASURABLE;
    default:              return SERVO_STR_ABORTED;
    }
}

static servo_str_t servo_test_start_str(servo_test_start_t why)
{
    if ((unsigned)why > (unsigned)SERVO_TEST_START_BAD_ENDS) {
        return SERVO_STR_START_OK;
    }
    return (servo_str_t)(SERVO_STR_START_OK + (int)why);
}

const char *servo_test_phase_name(servo_test_phase_t ph)
{
    return servo_str(servo_test_phase_str(ph));
}

const char *servo_test_abort_name(servo_test_abort_t why)
{
    return servo_str(servo_test_abort_str(why));
}

const char *servo_test_verdict_name(servo_test_verdict_t v)
{
    return servo_str(servo_test_verdict_str(v));
}

const char *servo_test_start_name(servo_test_start_t why)
{
    return servo_str(servo_test_start_str(why));
}

/* The columns every run has, the encoder's, and the meter's: the last
 * group follows whatever is before it, so a column of an older file keeps
 * its place. */
#define CSV_BASE  "time (s);test;step;phase;command (us);position (us);" \
                  "set (V);voltage (V);limit (A);current (A);power (W);" \
                  "mode;travel (ms)"
#define CSV_ENC   ";angle (deg);travel angle (ms)"
#define CSV_METER ";meter;window;current max (A);current min (A);" \
                  "voltage min (V);clipped"

const char *servo_test_csv_header(void)
{
    return CSV_BASE CSV_METER;
}

const char *servo_test_csv_header_enc(void)
{
    return CSV_BASE CSV_ENC CSV_METER;
}

const char *servo_test_meter_word(uint8_t kind)
{
    switch ((servo_test_meter_kind_t)kind) {
    case SERVO_TEST_METER_INA3221: return "INA3221";
    case SERVO_TEST_METER_MODEL:   return "MODEL";
    default:                       return "PDMINI";
    }
}
