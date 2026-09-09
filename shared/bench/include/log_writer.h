/*
 * Writes a run as CSV (comma-separated values) in the format shared/logfile
 * reads.
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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /** Returns bytes written, or a negative value on failure. */
    int (*write)(void *ctx, const void *data, size_t len);
    /**
     * Commit what has been written, so that a power cut keeps it.
     *
     * NULL for a sink that needs no commit, such as memory.  Returns false
     * on failure.  Writing a row is not keeping it: on FAT (file allocation
     * table) the directory entry carries the length, and until that entry is
     * written the file reads as 0 bytes whatever the data sectors hold.
     */
    bool (*flush)(void *ctx);
    void *ctx;
} log_sink_t;

typedef struct {
    log_sink_t sink;
    uint32_t   rows;
    uint32_t   pending;      /**< rows written since the last commit */
    float      last_s;       /**< t_s of the last row written */
    float      committed_s;  /**< t_s of the row the last commit ended at */
    bool       header_done;
    bool       failed;     /**< a write failed; the file is not trustworthy */
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
 * Semicolon as the separator, full stop as the decimal point.
 *
 * The reader infers a file's number convention from every value in it and
 * rejects values that do not conform.  With ';' as the separator, '.' is
 * unambiguously a decimal point.  A file using ',' for both, as a German
 * locale writes, is ambiguous and the reader refuses it.
 */
#define LOG_WRITER_SEP ';'

void log_writer_init(log_writer_t *w, const log_sink_t *sink);

/**
 * Write the header row.  log_writer_row calls it if it has not been called,
 * so a file cannot lack a header.
 */
bool log_writer_header(log_writer_t *w);

/**
 * Append one sample at @p t_s seconds, and commit when the row completes an
 * interval.
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
 * value and a time that only ever increases by 1/PANEL_SAMPLE_HZ.
 */
bool log_writer_row(log_writer_t *w, float t_s, const bench_state_t *b);

/**
 * Commit the rows written since the last commit.
 *
 * log_writer_row() calls it on the schedule above.  A caller calls it by
 * hand when a run has gone quiet with rows still uncommitted, and at the end
 * of a run when closing the sink does not commit by itself.
 *
 * A commit with nothing pending does nothing and reports success: on a card
 * it would be a transaction that buys nothing.  A commit that fails latches
 * the writer the way a failed write does, because a row the sink will not
 * keep is a row the file has lost.
 */
bool log_writer_commit(log_writer_t *w);

/** Rows written and not yet committed: what a power cut would cost now. */
static inline uint32_t log_writer_pending(const log_writer_t *w)
{
    return (w == NULL) ? 0u : w->pending;
}

/** True once any write has failed: the file is incomplete, not merely short. */
static inline bool log_writer_failed(const log_writer_t *w)
{
    return w == NULL || w->failed;
}

#ifdef __cplusplus
}
#endif
