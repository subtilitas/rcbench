/*
 * The run log's cadence: which pass of the control loop writes a row, and
 * the time that row carries.
 *
 * Owns no clock and no queue.  The control task hands in its millisecond
 * tick once per pass and says whether the pass brought a sample; the answer
 * is a row or none, and the row's time.  The same code runs in the host
 * suite against a model of the loop's pass grid.
 *
 * The rules:
 *
 * - One row per sample.  A pass that brought a sample of an armed bench
 *   writes at least one row; a pass that brought none writes none.
 * - One row per INA3221 window.  A pass that writes rows writes one for
 *   each window handed over in it (log_cadence_rows()): the sample is in
 *   the last, with the newest window, and each row before it carries its
 *   window alone.  The rows of one pass carry the same time; the window
 *   number orders them.
 * - No gate of its own.  What bounds the row rate is what bounds the sample
 *   rate: the poll's period with the link up, LOG_CADENCE_MODEL_MS for the
 *   model with the link down.
 * - A row's time is the wall time since the run's start, in seconds.  It
 *   advances on every pass, with or without a row, so a stretch without
 *   samples and a row the logger's queue refused both show as a step in the
 *   column.  The tick's wrap at 2^32 ms does not show in it.
 * - A row the queue refuses is counted as lost, a row it takes as sent.
 *   Every row log_cadence_row() grants is one or the other.
 * - No bench row while an automatic servo test runs
 *   (log_cadence_bench_run()).  The test writes its own file, one row per
 *   reading of its meter, and the bench log's time column steps over the
 *   test.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The model's period with the link down, in ms: 20 samples a second. */
#define LOG_CADENCE_MODEL_MS     50u

/**
 * The longest step the model is given, in ms.  A probe for the coprocessor
 * can hold the loop for its whole timeout; the model then advances by this
 * much and no more.  Equal to BENCH_TOTALS_MAX_STEP_S.
 */
#define LOG_CADENCE_MODEL_MAX_MS 1000u

typedef struct {
    uint32_t model_ms;  /**< tick at the model gate's last opening        */
    uint32_t seen_ms;   /**< tick of the last log_cadence_row()           */
    uint64_t run_ms;    /**< wall time since log_cadence_run_start()      */
    uint32_t sent;      /**< the run's rows the queue took                */
    uint32_t lost;      /**< the run's rows the queue refused             */
} log_cadence_t;

/** Once, before the loop: both clocks at @p now_ms, no rows counted. */
void log_cadence_init(log_cadence_t *c, uint32_t now_ms);

/**
 * The model's gate, once per pass.  Opens when LOG_CADENCE_MODEL_MS or more
 * have passed since it last opened, and its clock moves then whatever
 * @p link_up says, so the first step after a link is lost is not the whole
 * time the link was up.
 *
 * @return true when the model takes a step in this pass: the gate opened and
 *         the link is down.  @p step_s is then the time since the gate last
 *         opened, at most LOG_CADENCE_MODEL_MAX_MS, in seconds; otherwise 0.
 */
bool log_cadence_model_due(log_cadence_t *c, uint32_t now_ms, bool link_up,
                           float *step_s);

/** A run starts: its time is 0 at @p now_ms and its row counts are 0. */
void log_cadence_run_start(log_cadence_t *c, uint32_t now_ms);

/**
 * Whether the bench log records in this pass: the log's run is an armed
 * bench's (@p bench_kind) and no automatic servo test is running
 * (@p servo_test).  The answer is log_cadence_row()'s @p bench_run.
 */
bool log_cadence_bench_run(bool bench_kind, bool servo_test);

/**
 * Whether this pass writes a bench row, once per pass and on every pass,
 * with @p now_ms never behind the call before.  The run's time advances by
 * the ticks since the last call, modulo 2^32.
 *
 * @param new_sample  the pass brought a sample: an answered bench read, or
 *                    a step of the model
 * @param bench_run   the log is recording an armed bench
 * @param t_s         the row's time in seconds since the run's start;
 *                    written only when a row is granted
 * @return true exactly when @p new_sample and @p bench_run are both true.
 */
bool log_cadence_row(log_cadence_t *c, uint32_t now_ms, bool new_sample,
                     bool bench_run, float *t_s);

/**
 * How many rows a pass writes that log_cadence_row() granted: one for each
 * of the @p windows INA3221 windows handed over in the pass, and one for a
 * pass with none.  Each is posted and counted with log_cadence_posted().
 */
unsigned log_cadence_rows(unsigned windows);

/** What the logger's queue did with a row: @p taken, or refused as full. */
void log_cadence_posted(log_cadence_t *c, bool taken);

/** The run's rows the queue took. */
uint32_t log_cadence_sent(const log_cadence_t *c);

/** The run's rows the queue refused. */
uint32_t log_cadence_lost(const log_cadence_t *c);

#ifdef __cplusplus
}
#endif
