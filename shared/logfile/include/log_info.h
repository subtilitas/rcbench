/**
 * @file
 * @brief The run's own description in its CSV (comma-separated values) file:
 *        one text column `info`, the last of the file, filled on the first
 *        data row and empty on every other.
 *
 * The cell is `key=value` pairs joined by `&`, in the order dut, note, date,
 * fw, servo, meter, angle, id:
 *
 *     dut=MG90S&note=greased&date=2026-11-24T10:31&fw=0.23.18
 *
 * - dut: DUT NAME, the servo's name as typed (23 characters at most).
 * - note: RUN NOTE, the run's condition as typed (23 characters at most).
 * - date: the panel's local date and time at the run's start,
 *   YYYY-MM-DDTHH:MM; left out while the clock is unset (wall_valid()).
 * - fw: the bench's firmware version.
 * - servo: the servo type the run drove.
 * - meter: the meter that measured the current, "INA3221 0x41 CH2".
 * - angle: the angle source, "AS5600" or "IMU X+".
 * - id: the labelled servo's id, "KST_X10-0000A" (servo_id.h), 23
 *   characters at most; last, so a reader of firmware 0.23.17, which skips
 *   a key it does not know, reads the rest.
 *
 * A key whose value is empty is left out.  Inside a value each `&`, `=`,
 * `;` and `"`, and each control character (0x00 to 0x1F and 0x7F), is
 * written `_`: `&` and `=` would split the pair, `;` is the file's separator
 * and `"` opens a quoted field that runs on to the next one in the reader.
 * The rule loses those characters; a reader gets `_` back.
 *
 * A reader that does not know the column takes it as one more text column
 * (firmware 0.23.17's LOGS among them): it plots no text column.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "log_csv.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The column's header. */
#define LOG_INFO_HEAD "info"

/** @brief Bytes of a typed value (DUT NAME, RUN NOTE), the terminator
 *         included: SETTINGS_TEXT_MAX. */
#define LOG_INFO_TEXT_MAX 24u

/**
 * @brief Bytes of the longest cell, the terminator included: the eight keys
 *        with their `=` (38), seven `&`, and the values at their longest
 *        (23 + 23 + 16 + 15 + 19 + 19 + 11 + 23 = 149) make 194 characters.
 */
#define LOG_INFO_MAX 195u

/** @brief A run's description: each field "" where the run has none. */
typedef struct {
    char dut[LOG_INFO_TEXT_MAX];   /**< DUT NAME                            */
    char note[LOG_INFO_TEXT_MAX];  /**< RUN NOTE                            */
    char date[17];                 /**< "2026-11-24T10:31", local time      */
    char fw[16];                   /**< the bench's firmware version        */
    char servo[20];                /**< the servo type                      */
    char meter[20];                /**< the current's meter                 */
    char angle[12];                /**< the angle source                    */
    char id[LOG_INFO_TEXT_MAX];    /**< the labelled servo's id             */
} log_info_t;

/**
 * @brief Copy @p in into @p out with the escape rule applied: `&`, `=`, `;`,
 *        `"` and control characters become `_`.  Cut to @p n - 1 bytes.
 *
 * @param out Receives the text; always terminated when @p n > 0.
 * @param n   Size of @p out, bytes.
 * @param in  The value; NULL counts as "".
 */
void log_info_clean(char *out, size_t n, const char *in);

/**
 * @brief The cell of @p in: its non-empty fields as `key=value` pairs
 *        joined by `&`, each value cleaned (log_info_clean()).
 *
 * @param in  The description.
 * @param out Receives the cell; "" for a description with no field.
 * @param n   Size of @p out, bytes; LOG_INFO_MAX holds every cell.
 * @return The length of the cell; 0 when @p out is too small for it, and
 *         then @p out is "".
 */
size_t log_info_format(const log_info_t *in, char *out, size_t n);

/**
 * @brief Read a cell back: each known key's value into its field, cut to
 *        the field's size.  A pair with no `=` and an unknown key are
 *        skipped, so a later firmware's keys do not stop an older reader.
 *
 * @param cell The cell, NULL or "" for none.
 * @param out  Receives the description; every field "" first.
 * @return true when the cell held at least one known key.
 */
bool log_info_parse(const char *cell, log_info_t *out);

/**
 * @brief Read the cell of a whole file: the column headed LOG_INFO_HEAD,
 *        on the first data row.  The source is rewound first.
 *
 * @param src       The file.
 * @param delimiter The file's separator, ';' for the bench's files.
 * @param out       Receives the description; every field "" first.
 * @return true when the file has the column and its first data row a cell
 *         with a known key.
 */
bool log_info_read(log_source_t *src, char delimiter, log_info_t *out);

/**
 * @brief A time as the date field: the local date and time in the zone in
 *        force (TZ), "YYYY-MM-DDTHH:MM".
 *
 * @param out    Receives the text; "" while @p unix_s is no real date
 *               (wall_valid()), the clock being unset.
 * @param n      Size of @p out, bytes; 17 holds the text.
 * @param unix_s Seconds since 1970-01-01 00:00:00 UTC.
 */
void log_info_date(char *out, size_t n, int64_t unix_s);

/** @brief The clock the date is read from: seconds since 1970 in UTC. */
typedef int64_t (*log_info_clock_fn)(void);

/**
 * @brief Name the clock log_info_now() reads; set once at start-up, before
 *        a run starts.  The panel names time(); the host suite a clock of
 *        its own.
 *
 * @param fn The clock; NULL for none, and every date is then "".
 */
void log_info_set_clock(log_info_clock_fn fn);

/**
 * @brief The time now from the clock log_info_set_clock() named.
 *
 * @return Seconds since 1970 in UTC; 0, no real date, without a clock.
 */
int64_t log_info_now(void);

#ifdef __cplusplus
}
#endif
