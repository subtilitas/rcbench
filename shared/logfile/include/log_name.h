/*
 * Run log names on the card, and which entries a bounded list keeps.
 *
 * A run is written under the name of what started it and a number from 001
 * to 999, in one of two folders (card_tree.h):
 *
 *   BENCH/LOGS, the automatic logs (AUTO LOGS on SETUP > SD CARD):
 *     SERVOnnn.CSV  an ARMED run armed on the SERVO screen;
 *     MOTORnnn.CSV  an ARMED run armed on MOTOR & ESC, ESC STICK included;
 *     SUPPLnnn.CSV  a switch-on of the SUPPLY output while SAFE.
 *   USER/LOGS, the tests a user starts (USER LOGS on SETUP > SD CARD):
 *     ATESTnnn.CSV  the AUTOMATIC TEST, with its report ATESTnnn.TXT;
 *     LIMITnnn.TXT  a LIMIT RUN's report, alone.
 *
 * Which screen armed the bench is read off the roles the arm asked for
 * (log_name_armed_prefix()): MOTOR & ESC and ESC STICK ask for the
 * throttle, SERVO for the surfaces.  An arm with no roles recorded is a
 * MOTOR run while the throttle outputs are armed and a SERVO run otherwise.
 *
 * Firmware 0.21 and older wrote every run to the card's root as BENCHnnn,
 * ESCnnn and SERVOnnn (a servo test, with SERVOnnn.TXT, or a limit run's
 * SERVOnnn.TXT alone).  Those names stay readable: their numbers rank, a
 * deleted BENCHnnn.CSV or SERVOnnn.CSV takes its report, and a folder that
 * holds them numbers above them.  SERVOnnn.CSV therefore means a servo test
 * in the root and an ARMED run armed on SERVO in BENCH/LOGS.
 *
 * Every name is a FAT (File Allocation Table) 8.3 name: at most 8
 * characters, a dot and 3.  The numbers are one series per folder, across
 * every prefix, so a number names one run in a folder whichever writer made
 * it.  The panel takes one above the highest number the folder already
 * holds under any prefix, so a higher number is a later run without
 * exception -- a gap left by a deleted file is not filled, because a run
 * written into one would be the newest run wearing the oldest number and
 * would rank last in a full list.  A folder holding a number 999 under any
 * prefix takes no further run.
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
 * and log_name_rank() orders a listing when a folder holds more entries than
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

/** Room for the longest name, "SUPPL999.CSV" or "ATEST999.CSV", and its
 *  terminator. */
#define LOG_RUN_NAME_MAX 13

/**
 * Which writer a run's name says made it.  The first LOG_PREFIX_WRITTEN are
 * the names this firmware writes; BENCH and ESC are read and never written.
 */
typedef enum {
    LOG_PREFIX_ATEST = 0,   /**< "ATEST": the AUTOMATIC TEST               */
    LOG_PREFIX_LIMIT,       /**< "LIMIT": a LIMIT RUN's report             */
    LOG_PREFIX_SERVO,       /**< "SERVO": ARMED on SERVO; in the root, a
                                 servo test of firmware 0.21 and older     */
    LOG_PREFIX_MOTOR,       /**< "MOTOR": ARMED on MOTOR & ESC or ESC STICK */
    LOG_PREFIX_SUPPL,       /**< "SUPPL": the SUPPLY output on while SAFE  */
    LOG_PREFIX_BENCH,       /**< "BENCH": firmware 0.21 and older          */
    LOG_PREFIX_ESC,         /**< "ESC": firmware 0.21 and older            */
    LOG_PREFIX_COUNT
} log_prefix_t;

/** How many prefixes this firmware writes: ATEST to SUPPL. */
#define LOG_PREFIX_WRITTEN ((int)LOG_PREFIX_BENCH)

/**
 * Every name a number written now can be carried by: each written prefix
 * with ".CSV" and with ".TXT", 10 names.  A number is taken while any of
 * them is in the folder; the numbering walk reads all 7 prefixes.
 */
#define LOG_NAME_FORMS (LOG_PREFIX_WRITTEN * 2)

/**
 * Write the name of run @p number by writer @p prefix: "ATEST001.CSV", or
 * with @p report its report "ATEST001.TXT".
 *
 * A number outside LOG_RUN_FIRST to LOG_RUN_LAST, a prefix outside the
 * table, or an @p out_size under LOG_RUN_NAME_MAX, writes an empty string
 * rather than a name that would name a different file.
 */
void log_name_build(char *out, size_t out_size, log_prefix_t prefix,
                    int number, bool report);

/**
 * The writer of an ARMED run's log, by the screen that armed the bench.
 *
 * @p asked: the arm recorded the roles it asked for (arming_roles() is not
 * 0).  @p asked_throttle: those roles hold the throttle, as MOTOR & ESC and
 * ESC STICK ask for; SERVO asks for the surfaces alone.  An arm that asked
 * is LOG_PREFIX_MOTOR or LOG_PREFIX_SERVO by @p asked_throttle, whatever an
 * IOmcu older than protocol 4.18 armed beside them.  An arm with nothing
 * recorded is LOG_PREFIX_MOTOR while @p throttle_armed and LOG_PREFIX_SERVO
 * otherwise.  A supply run is LOG_PREFIX_SUPPL.
 */
log_prefix_t log_name_armed_prefix(bool asked, bool asked_throttle,
                                   bool throttle_armed);

/** log_name_build() for LOG_PREFIX_BENCH: "BENCH001.CSV" to "BENCH999.CSV",
 *  the name firmware 0.21 and older gave a run in the root. */
void log_run_name(char *out, size_t out_size, int number);

/**
 * The run number in @p name, or -1 when the name is not a run log under
 * any of the 7 prefixes.
 *
 * Case is ignored, so a card taken to a computer and written back in lower
 * case still lists in run order.  The name has to be one of the prefixes,
 * exactly three digits in range, and the suffix, with nothing after it:
 * "BENCH12.CSV", "BENCH000.CSV", "SERVO001.CSV.BAK" and "ESCAPE01.CSV" are
 * not run logs.
 */
int log_run_number(const char *name);

/**
 * A run's report beside its log: "ATEST001.TXT", and "SERVO001.TXT" or
 * "BENCH001.TXT" in the root of a card written by firmware 0.21 or older.
 * log_report_number() reads any prefix, with the same rules for the number and the case as
 * log_run_number(); log_report_name() is log_name_build() for
 * LOG_PREFIX_BENCH with the report suffix.  A number belongs to a run while
 * either file carries it: the logger takes one above both, so a report whose
 * CSV was deleted is never overwritten by the next run's.
 */
void log_report_name(char *out, size_t out_size, int number);
int log_report_number(const char *name);

/**
 * The number @p name takes from its folder's series: a run log's or a
 * report's, under any of the 7 prefixes, or -1 for a name that takes none.
 * What the numbering walk reads.
 */
int log_name_number(const char *name);

/**
 * Write the @p form'th of the LOG_NAME_FORMS names number @p number can be
 * carried by, 0 to LOG_NAME_FORMS - 1: form / 2 is the written prefix and
 * form % 2 the report.  An empty string for a form or a number outside
 * them.  The logger probes each before it takes a number.
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
 * Whether @p name is a file the logger is still writing, or one whose
 * deletion would remove such a file: a run log whose number is @p open_run
 * (the armed or supply run), @p open_test (an AUTOMATIC TEST's CSV) or
 * @p open_txt (a test's or a limit run's report, which a deleted run takes
 * with it), or a report whose number is @p open_txt.  0 is no file open.
 * The numbers are one series in a folder, so the prefix does not enter into
 * it; the caller passes 0 for a file open in another folder
 * (log_marks_busy() in log_take.h).
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
 * Negative when @p a is kept ahead of @p b, positive when @p b is.  Runs
 * come first, by number, the newest first, whichever writer made them:
 * MOTOR043.CSV ranks ahead of SERVO042.CSV.  An operator opening a log is
 * almost always after a recent one, and a run is the only entry whose age is
 * known.  Everything else follows the runs by name, case folded.
 *
 * The order is strict: zero only for one name, compared with case folded
 * (log_name_case_cmp()).  Two runs with one number under different prefixes,
 * which only a card edited on a computer holds, follow by name: ATEST042.CSV,
 * then MOTOR042.CSV, then SERVO042.CSV.  Paging needs that (log_select.h): a
 * name that ranked equal to a page's edge would be on neither side of it.
 *
 * A card holding as many runs as a page has room for therefore leaves no
 * room on the first page for a file the bench did not write; the browse
 * panel says how many entries the card holds.
 */
int log_name_rank(const char *a, const char *b);

#ifdef __cplusplus
}
#endif
