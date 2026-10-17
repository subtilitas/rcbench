/*
 * Run log names on the card, and which entries a bounded list keeps.
 *
 * A run is written to the card's root under the name of the writer that
 * made it and a number from 001 to 999:
 *
 *   SERVOnnn.CSV  the automatic servo test, with its report SERVOnnn.TXT;
 *   SERVOnnn.TXT  a limit run's report, alone;
 *   ESCnnn.CSV    an armed run of the throttle outputs, the ESC (electronic
 *                 speed controller) the MOTOR and ESC STICK screens drive;
 *   BENCHnnn.CSV  every other run: an armed run of the surfaces alone and a
 *                 switch-on of the SUPPLY output.
 *
 * Every name is a FAT (File Allocation Table) 8.3 name: at most 8 characters,
 * a dot and 3.  The numbers are one series across the three prefixes, so a
 * number names one run on a card whichever writer made it.  The panel takes
 * one above the highest number the card already holds under any prefix, so a
 * higher number is a later run without exception -- a gap left by a deleted
 * file is not filled, because a run written into one would be the newest run
 * wearing the oldest number and would rank last in a full list.  A card
 * holding a number 999 under any prefix takes no further run.
 *
 * A card that holds only BENCHnnn files, a servo test's BENCHnnn.CSV and
 * BENCHnnn.TXT among them, reads the same way: their numbers are in the
 * series, the next run continues above them, and a deleted BENCHnnn.CSV
 * takes its BENCHnnn.TXT with it.
 *
 * Nothing else about an entry's age is readable on this bench.  The board
 * carries no clock that survives a power cycle, so every file the panel
 * writes is dated 1980-01-01 in the FAT directory -- the epoch the filesystem
 * clamps an unset clock to -- and directory order is creation order only
 * until a file is deleted and its slot filled again.  The number in the name
 * is the only age there is.
 *
 * Both ends of that rule are here: the logger builds a name with
 * log_name_build(), the viewer reads the number back with log_run_number(),
 * and log_name_rank() orders a listing when a card holds more entries than
 * the screen has room for.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The first and the last run number the panel writes. */
#define LOG_RUN_FIRST 1
#define LOG_RUN_LAST  999

/** Room for the longest name, "BENCH999.CSV" or "SERVO999.CSV", and its
 *  terminator. */
#define LOG_RUN_NAME_MAX 13

/** Which writer a run's name says made it. */
typedef enum {
    LOG_PREFIX_BENCH = 0,   /**< "BENCH": the rest                         */
    LOG_PREFIX_ESC,         /**< "ESC": an armed run of the throttle       */
    LOG_PREFIX_SERVO,       /**< "SERVO": servo test and limit run         */
    LOG_PREFIX_COUNT
} log_prefix_t;

/**
 * Every name a number can be carried by: each prefix with ".CSV" and with
 * ".TXT".  A number is taken while any of them is on the card.
 */
#define LOG_NAME_FORMS ((int)LOG_PREFIX_COUNT * 2)

/**
 * Write the name of run @p number by writer @p prefix: "SERVO001.CSV", or
 * with @p report its report "SERVO001.TXT".
 *
 * A number outside LOG_RUN_FIRST to LOG_RUN_LAST, a prefix outside the
 * table, or an @p out_size under LOG_RUN_NAME_MAX, writes an empty string
 * rather than a name that would name a different file.
 */
void log_name_build(char *out, size_t out_size, log_prefix_t prefix,
                    int number, bool report);

/**
 * The writer of an armed run's log: LOG_PREFIX_ESC while the arm covers the
 * throttle outputs (@p throttle_armed), LOG_PREFIX_BENCH while it covers the
 * surfaces alone.  A supply run is LOG_PREFIX_BENCH, and the servo test and
 * the limit run are LOG_PREFIX_SERVO whatever the arm covers.
 */
log_prefix_t log_name_armed_prefix(bool throttle_armed);

/** log_name_build() for LOG_PREFIX_BENCH: "BENCH001.CSV" to "BENCH999.CSV". */
void log_run_name(char *out, size_t out_size, int number);

/**
 * The run number in @p name, or -1 when the name is not a run log the panel
 * writes, under any of the three prefixes.
 *
 * Case is ignored, so a card taken to a computer and written back in lower
 * case still lists in run order.  The name has to be one of the prefixes,
 * exactly three digits in range, and the suffix, with nothing after it:
 * "BENCH12.CSV", "BENCH000.CSV", "SERVO001.CSV.BAK" and "ESCAPE01.CSV" are
 * not run logs.
 */
int log_run_number(const char *name);

/**
 * A run's report beside its log: "SERVO001.TXT", and "BENCH001.TXT" on a
 * card whose servo tests are named BENCH.  log_report_number()
 * reads any prefix, with the same rules for the number and the case as
 * log_run_number(); log_report_name() is log_name_build() for
 * LOG_PREFIX_BENCH with the report suffix.  A number belongs to a run while
 * either file carries it: the logger takes one above both, so a report whose
 * CSV was deleted is never overwritten by the next run's.
 */
void log_report_name(char *out, size_t out_size, int number);
int log_report_number(const char *name);

/** The prefix of a run log or a report in @p name, or -1 for any other. */
int log_name_prefix(const char *name);

/**
 * The number @p name takes from the series: a run log's or a report's, under
 * any prefix, or -1 for a name that takes none.  What the numbering walk
 * reads.
 */
int log_name_number(const char *name);

/**
 * Write the @p form'th of the LOG_NAME_FORMS names number @p number can be
 * carried by, 0 to LOG_NAME_FORMS - 1; an empty string for a form or a number
 * outside them.  The logger probes each before it takes a number.
 */
void log_name_form(char *out, size_t out_size, int number, int form);

/**
 * The first number a run may take on a card whose highest number, by
 * log_name_number(), is @p highest (below LOG_RUN_FIRST for a card that holds
 * none): one above it.  Above LOG_RUN_LAST when the card holds 999: the card
 * is full, and a lower free number is not taken.
 */
int log_name_next(int highest);

/**
 * Write the report that goes with run log @p run_name: the same prefix and
 * number, ".TXT", in upper case.  An empty string when @p run_name is not a
 * run log.  Deleting a run deletes this file with it.
 */
void log_name_report_of(char *out, size_t out_size, const char *run_name);

/**
 * Whether @p name is a file the logger is still writing: a run log whose
 * number is @p open_run (the armed or supply run) or @p open_test (a servo
 * test's CSV), or a report whose number is @p open_txt.  0 is no file open.
 * The numbers are one series, so the prefix does not enter into it.
 */
bool log_name_busy(const char *name, unsigned open_run, unsigned open_test,
                   unsigned open_txt);

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
 * the two rank the same.  Runs come first, the newest first, whichever
 * writer made them: an operator opening a log is almost always after a
 * recent one, and a run is the only entry whose age is known.  Two runs with
 * one number, which only a card edited on a computer holds, follow by name.
 * Everything else follows in the order it is drawn in, case-insensitive by
 * name.  A card holding as many runs as the list has
 * room for therefore leaves no room for a file the bench did not write, and
 * the browse panel says how many entries the card holds.
 */
int log_name_rank(const char *a, const char *b);

#ifdef __cplusplus
}
#endif
