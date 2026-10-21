/**
 * @file
 * @brief Writes a run as CSV (comma-separated values) in the format
 *        shared/logfile reads.
 *
 * The sink is injected: the host suite writes into memory and the panel
 * writes into a FILE.  Nothing here depends on the card, so the writer is
 * tested against the reader by writing a run and parsing it back.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bench_state.h"
#include "log_cols.h"
#include "supply.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The byte sink a log writer writes to, with its commit.
 */
typedef struct {
    /** @brief Returns bytes written, or a negative value on failure. */
    int (*write)(void *ctx, const void *data, size_t len);
    /**
     * @brief Commit what has been written, so that a power cut keeps it.
     *
     * NULL for a sink that needs no commit, such as memory.  Returns false
     * on failure.  Writing a row is not keeping it: on FAT (file allocation
     * table) the directory entry carries the length, and until that entry is
     * written the file reads as 0 bytes whatever the data sectors hold.
     */
    bool (*flush)(void *ctx);
    void *ctx;
} log_sink_t;

/** @brief The longest bench row, terminator included. */
#define LOG_WRITER_LINE_MAX 256

/**
 * @brief A log writer: its sink, its counts and its latched failure.
 */
typedef struct {
    log_sink_t sink;
    uint32_t   rows;
    uint32_t   pending;      /**< rows written since the last commit */
    float      last_s;       /**< t_s of the last row written */
    float      committed_s;  /**< t_s of the row the last commit ended at */
    bool       header_done;
    bool       failed;     /**< a write failed; the file is not trustworthy */
    /**
     * @brief The INA3221 columns go by cols (log_writer_set_cols()), not CH1 to
     *        CH3.
     */
    bool       by_map;
    log_cols_t cols;
    /**
     * @brief A bench row as it is built: 23 cells, here rather than on the
     *        stack of the task that writes the card.
     */
    char       line[LOG_WRITER_LINE_MAX];
} log_writer_t;

/*
 * What a power cut costs.
 *
 * The writer commits every LOG_WRITER_FLUSH_ROWS rows, and again once
 * LOG_WRITER_FLUSH_S have passed on the run's own clock, whichever comes
 * first.  At the panel's 20 Hz sample rate the two are the same point: 20
 * rows is 1000 ms of run.
 *
 * A run interrupted at any moment therefore loses at most 20 rows, spanning
 * less than 1.0 s of its own clock.  Twenty and not nineteen: the count
 * reaches LOG_WRITER_FLUSH_ROWS before the commit is attempted and is
 * cleared only once the commit has succeeded, so those twenty rows are not
 * durable for as long as the sink takes.  The price is one commit per second per
 * run.
 */
#define LOG_WRITER_FLUSH_ROWS 20u
#define LOG_WRITER_FLUSH_S    1.0f

/**
 * @brief Semicolon as the separator, full stop as the decimal point.
 *
 * The reader infers a file's number convention from every value in it and
 * rejects values that do not conform.  With ';' as the separator, '.' is
 * unambiguously a decimal point.  A file using ',' for both, as a German
 * locale writes, is ambiguous and the reader refuses it.
 */
#define LOG_WRITER_SEP ';'

/**
 * @brief The values of the last column, "source", of a bench file and a supply
 *        file: "measured" for a row whose values a meter or a supply produced,
 *        "model" for a row the panel's model produced (LINK_BN_SIMULATED on a
 *        bench row, supply_state_t.modelled on a supply row).
 *
 * A reader takes a file with the column and a file without it; a file written
 * without it ends at the previous column.
 */
#define LOG_WRITER_SRC_MEASURED "measured"
#define LOG_WRITER_SRC_MODEL    "model"

/**
 * @brief Sets up a writer on a sink.
 *
 * @param w    The writer to set up.
 * @param sink The sink the rows go to.
 */
void log_writer_init(log_writer_t *w, const log_sink_t *sink);

/**
 * @brief The INA3221 columns of this file, before its header: with SENSE_MAP in
 *        force a group per watched target (log_cols.h), read from the window
 *        bench_state_t.chw carries; NULL for SERVO_SENSE's CH1 to CH3, as an
 *        IOmcu older than 4.25 has them.
 *
 * Taken until the header is written, ignored after.
 *
 * @param w    The writer.
 * @param cols The INA3221 columns, or NULL.
 */
void log_writer_set_cols(log_writer_t *w, const log_cols_t *cols);

/**
 * @brief Write the header row.
 *
 * log_writer_row calls it if it has not been called,
 * so a file cannot lack a header.
 *
 * @param w The writer.
 * @return true on success; false when a write failed.
 */
bool log_writer_header(log_writer_t *w);

/**
 * @brief Append one sample at @p t_s seconds, and commit when the row completes
 *        an interval.
 *
 * False means one of two things, and log_writer_failed() tells them apart:
 *
 *   A write or the commit after it failed.  log_writer_failed() is true and
 *   stays true, and every later row is refused: the file has a hole in it
 *   and no amount of card answering again makes it whole.
 *
 *   The arguments were refused -- @p w or @p b NULL, or @p t_s not finite.
 *   Nothing was attempted and nothing is latched.  That is a caller with a
 *   bug rather than a card that has gone, and ending a run over it would
 *   throw away a recording for a reason the card had no part in.
 *
 * So a caller that watches only the latch has to check the return as well if
 * it can pass either of those.  The panel cannot: its row carries a struct by
 * value and a finite time: a supply run's sum of steps, or a bench run's
 * wall time since the arm (log_cadence.h).
 *
 * @param w   The writer.
 * @param t_s The time of the row, in s.
 * @param b   The sample to write.
 * @return true on success; false when refused or failed (see
 *         log_writer_failed()).
 */
bool log_writer_row(log_writer_t *w, float t_s, const bench_state_t *b);

/**
 * @brief Append one supply sample, for a run that is the supply's output being
 *        on.
 *
 * The file's first row writes the supply header -- time; set (V);
 * voltage (V); limit (A); current (A); power (W); mode; charge (mAh);
 * energy (Wh); source -- so a file is one kind or the other and the caller keeps it
 * so.  Readings that did not arrive, and totals nothing counted, are empty
 * cells; the mode is CV, CC or OFF, and empty while the supply does not
 * answer.  Same returns, commits and latch as log_writer_row().
 *
 * @param w   The writer.
 * @param t_s The time of the row, in s.
 * @param s   The supply sample.
 * @return true on success; false when refused or failed (see
 *         log_writer_failed()).
 */
bool log_writer_supply_row(log_writer_t *w, float t_s,
                           const supply_state_t *s);

/**
 * @brief Commit the rows written since the last commit.
 *
 * log_writer_row() calls it on the schedule above.  A caller calls it by
 * hand when a run has gone quiet with rows still uncommitted, and at the end
 * of a run when closing the sink does not commit by itself.
 *
 * A commit with nothing pending does nothing and reports success: on a card
 * it would be a transaction that buys nothing.  A commit that fails latches
 * the writer the way a failed write does, because a row the sink will not
 * keep is a row the file has lost.
 *
 * @param w The writer.
 * @return true on success; false when the commit failed.
 */
bool log_writer_commit(log_writer_t *w);

/**
 * @brief Rows written and not yet committed: what a power cut would cost now.
 *
 * @param w The writer.
 * @return The rows written and not yet committed.
 */
static inline uint32_t log_writer_pending(const log_writer_t *w)
{
    return (w == NULL) ? 0u : w->pending;
}

/**
 * @brief True once any write has failed: the file is incomplete, not merely
 *        short.
 *
 * @param w The writer.
 * @return true once any write has failed.
 */
static inline bool log_writer_failed(const log_writer_t *w)
{
    return w == NULL || w->failed;
}

#ifdef __cplusplus
}
#endif
