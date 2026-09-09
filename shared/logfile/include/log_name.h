/*
 * Run log names on the card, and which entries a bounded list keeps.
 *
 * A run is written to the card's root as BENCH001.CSV to BENCH999.CSV.  The
 * panel takes the lowest free number, so a higher number is a later run --
 * except after a file is deleted and its number handed out again, which is
 * the one case where that order is wrong.
 *
 * Nothing else about an entry's age is readable on this bench.  The board
 * carries no clock that survives a power cycle, so every file the panel
 * writes is dated 1980-01-01 in the FAT (File Allocation Table) directory --
 * the epoch the filesystem clamps an unset clock to -- and directory order is
 * creation order only until a file is deleted and its slot filled again.  The
 * number in the name is the only age there is.
 *
 * Both ends of that rule are here: the logger builds a name with
 * log_run_name(), the viewer reads the number back with log_run_number(), and
 * log_name_rank() orders a listing when a card holds more entries than the
 * screen has room for.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The first and the last run number the panel writes. */
#define LOG_RUN_FIRST 1
#define LOG_RUN_LAST  999

/** Room for "BENCH999.CSV" and its terminator. */
#define LOG_RUN_NAME_MAX 13

/**
 * Write the name of run @p number: "BENCH001.CSV" to "BENCH999.CSV".
 *
 * A number outside LOG_RUN_FIRST to LOG_RUN_LAST, or an @p out_size under
 * LOG_RUN_NAME_MAX, writes an empty string rather than a name that would name
 * a different file.
 */
void log_run_name(char *out, size_t out_size, int number);

/**
 * The run number in @p name, or -1 when the name is not one the panel writes.
 *
 * Case is ignored, so a card taken to a computer and written back in lower
 * case still lists in run order.  The name has to be the five letters,
 * exactly three digits in range, and the suffix, with nothing after it:
 * "BENCH12.CSV", "BENCH000.CSV" and "BENCH001.CSV.BAK" are not run logs.
 */
int log_run_number(const char *name);

/**
 * Compare two names with case folded: negative when @p a sorts first, as the
 * browse list orders what it draws.  A card written on a computer holds
 * "bench042.csv" and one written here holds "BENCH042.CSV", and the two are
 * one file.
 */
int log_name_case_cmp(const char *a, const char *b);

/**
 * Order two listed names by which one a full list keeps.
 *
 * Negative when @p a is kept ahead of @p b, positive when @p b is, zero when
 * the two rank the same.  Runs come first, the newest first: an operator
 * opening a log is almost always after a recent one, and a run is the only
 * entry whose age is known.  Everything else follows in the order it is drawn
 * in, case-insensitive by name.  A card holding as many runs as the list has
 * room for therefore leaves no room for a file the bench did not write, and
 * the browse panel says how many entries the card holds.
 */
int log_name_rank(const char *a, const char *b);

#ifdef __cplusplus
}
#endif
