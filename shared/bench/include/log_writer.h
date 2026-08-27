/*
 * Writing a run to the card, in the format this project's own reader expects.
 *
 * The point of this file existing separately from the card is that the whole
 * of it can be tested against the reader: write a run, parse it back, and
 * assert the numbers survived.  A logger tested only by looking at the file it
 * produced is a logger nobody has checked.
 *
 * The sink is injected, so the host suite writes into memory and the panel
 * writes into a FILE.  Nothing here knows what a card is.
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
 * Semicolon-separated with a full stop for decimals.
 *
 * The reader decides a file's convention from every value in it and rejects
 * what does not conform, so an unambiguous pairing is the one thing a writer
 * owes it: with ';' as the separator, '.' can only be a decimal point. Writing
 * ',' for both -- which a German locale would -- produces a file that is
 * genuinely ambiguous, and the reader is right to say so rather than guess.
 */
#define LOG_WRITER_SEP ';'

void log_writer_init(log_writer_t *w, const log_sink_t *sink);

/**
 * Write the header row.  Called by log_writer_row if it has not been, so a
 * caller cannot produce a headerless file by forgetting.
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
