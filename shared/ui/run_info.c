/**
 * @file
 * @brief The `info` cell of a run the panel starts, see run_info.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "run_info.h"

#include <stdio.h>
#include <string.h>

#include "log_info.h"
#include "rcbench_version.h"
#include "servo_test_io.h"
#include "settings.h"

_Static_assert(SERVO_RUN_INFO_MAX == LOG_INFO_MAX,
               "a SERVO run's config holds every info cell");
_Static_assert(SETTINGS_TEXT_MAX == LOG_INFO_TEXT_MAX,
               "DUT NAME and RUN NOTE fit their fields whole");

/**
 * @brief Copy @p in into field @p out of @p n bytes; NULL counts as "".
 * @param out The field.
 * @param n   Its size, bytes.
 * @param in  The text, or NULL.
 */
static void put(char *out, size_t n, const char *in)
{
    snprintf(out, n, "%s", (in != NULL) ? in : "");
}

/**
 * @brief The fields every run has: the note, the date now and the bench's
 *        version, into a cleared @p in.
 * @param in   Receives them.
 * @param note RUN NOTE, or NULL.
 */
static void common(log_info_t *in, const char *note)
{
    memset(in, 0, sizeof(*in));
    put(in->note, sizeof(in->note), note);
    log_info_date(in->date, sizeof(in->date), log_info_now());
    put(in->fw, sizeof(in->fw), RCBENCH_VERSION_STRING);
}

void run_info_servo(char *out, size_t n, const char *servo, const char *meter,
                    const char *angle)
{
    log_info_t in;
    common(&in, settings_text(SET_TEXT_RUN_NOTE));
    put(in.dut, sizeof(in.dut), settings_text(SET_TEXT_DUT_NAME));
    put(in.servo, sizeof(in.servo), servo);
    put(in.meter, sizeof(in.meter), meter);
    put(in.angle, sizeof(in.angle), angle);
    (void)log_info_format(&in, out, n);
}

void run_info_bench(char *out, size_t n, const char *dut, const char *note)
{
    log_info_t in;
    common(&in, note);
    put(in.dut, sizeof(in.dut), dut);
    (void)log_info_format(&in, out, n);
}
