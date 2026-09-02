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
    void *ctx;
} log_sink_t;

typedef struct {
    log_sink_t sink;
    uint32_t   rows;
    bool       header_done;
    bool       failed;     /**< a write failed; the file is not trustworthy */
} log_writer_t;

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

/** Append one sample at @p t_s seconds. */
bool log_writer_row(log_writer_t *w, float t_s, const bench_state_t *b);

/** True once any write has failed: the file is incomplete, not merely short. */
static inline bool log_writer_failed(const log_writer_t *w)
{
    return w == NULL || w->failed;
}

#ifdef __cplusplus
}
#endif
