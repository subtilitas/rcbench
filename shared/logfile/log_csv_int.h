/**
 * @file
 * @brief Blank-character test shared by the CSV log reader and writer.
 *
 * Shared by log_csv.c and log_reader.c only; not part of the module's
 * interface (log_csv.h is).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LOG_CSV_INT_H
#define LOG_CSV_INT_H

#include <stdbool.h>

/**
 * @brief Whether a byte is a blank: space, tab or CR.
 *
 * The bytes after the last line feed that are no row: space, tab, CR.  The
 * documented set; isspace() would add LF, VT and FF.
 *
 * @param c  The byte.
 * @return true for space, tab or CR.
 */
static inline bool is_blank(int c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

#endif /* LOG_CSV_INT_H */
