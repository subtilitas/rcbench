/**
 * @file
 * @brief The `info` cell (log_info.h) of a run the panel starts: DUT NAME,
 *        RUN NOTE, the date at the start, the firmware, and the servo
 *        type, meter and angle source where the run has them.
 *
 * Which keys a file carries depends on its kind:
 *
 * | File          | dut | note | date | fw | servo | meter | angle |
 * | ------------- | --- | ---- | ---- | -- | ----- | ----- | ----- |
 * | ATEST, LIMIT, RUDDR | yes | yes | yes | yes | yes | yes | with an angle source |
 * | SERVO         | yes | yes  | yes  | yes | -    | -     | -     |
 * | MOTOR, SUPPL  | -   | yes  | yes  | yes | -    | -     | -     |
 *
 * A key with no value is left out: the date while the clock is unset, the
 * note while RUN NOTE is empty.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The cell of a run started on a SERVO screen (AUTOMATIC TEST, LIMIT
 *        RUN, RUDDER AUTO): DUT NAME and RUN NOTE as the settings hold them
 *        now, the date from log_info_now(), the bench's version, and
 *        @p servo, @p meter and @p angle.  On the task that draws: it reads
 *        the settings.
 *
 * @param out   Receives the cell (log_info_format()).
 * @param n     Size of @p out, bytes; SERVO_RUN_INFO_MAX holds every cell.
 * @param servo The servo type; NULL or "" for none.
 * @param meter The meter of the current; NULL or "" for none.
 * @param angle The angle source; NULL or "" for none.
 */
void run_info_servo(char *out, size_t n, const char *servo, const char *meter,
                    const char *angle);

/**
 * @brief The cell of a bench run's file (MOTOR, SERVO, SUPPL), from the
 *        texts the caller holds: @p dut on a SERVO run's, NULL on a MOTOR
 *        or supply run's; @p note, the date from log_info_now() and the
 *        bench's version.  Reads no setting, so any task may call it.
 *
 * @param out  Receives the cell (log_info_format()).
 * @param n    Size of @p out, bytes; LOG_INFO_MAX holds every cell.
 * @param dut  DUT NAME on a SERVO run, NULL otherwise.
 * @param note RUN NOTE; NULL or "" for none.
 */
void run_info_bench(char *out, size_t n, const char *dut, const char *note);

#ifdef __cplusplus
}
#endif
