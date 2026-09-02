/*
 * CSV (comma-separated values) and TSV (tab-separated values) import for a
 * board that cannot hold the file it is reading.
 *
 * The source is a card, a log can be tens of megabytes, and the 8 MB of PSRAM
 * (pseudo-static random-access memory) already hold two framebuffers.  So the
 * reader works through a callback and never keeps more than one row: analysis
 * is two streaming passes, and only the columns the user plots are
 * materialised as arrays.
 *
 * Two-phase: `log_csv_analyse` reports what it detected without committing to
 * it, so the import screen can show a preview and let the user override the
 * delimiter, the decimal convention and the time column before
 * `log_csv_build` runs.
 *
 * Pure C, no ESP-IDF.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "log_numbers.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ceilings.  The screen cannot draw more than 24 channels, and reporting the
 * limit beats a silent truncation. */
#define LOG_MAX_COLUMNS 24
#define LOG_MAX_SERIES  4
#define LOG_NAME_MAX    28
#define LOG_SAMPLE_MAX  16
#define LOG_SAMPLES     3
#define LOG_ROW_MAX     1024

/* -------------------------------------------------------------- source --- */

/**
 * A byte source that can be read from the top more than once.
 *
 * Not a FILE*: the host tests feed strings, the firmware feeds a file on the
 * card, and neither knows about the other.
 */
typedef struct {
    size_t (*read)(void *ctx, char *buf, size_t max); /**< 0: end of input */
    bool (*rewind)(void *ctx);
    /**
     * Did the last read fail, as opposed to reaching the end?
     *
     * Optional: NULL means "reads never fail", which is true of a block of
     * memory and false of an SD card.  Without it a bad sector reads as a
     * clean end of file, and a truncated log is presented as a complete short
     * one.
     */
    bool (*error)(void *ctx);
    void *ctx;
} log_source_t;

typedef struct {
    const char *text;
    size_t len;
    size_t pos;
} log_mem_ctx_t;

/** A source over a block of memory. */
void log_source_memory(log_source_t *src, log_mem_ctx_t *ctx, const char *text,
                       size_t len);

/** A source over an open stdio stream.  @p file is a FILE*. */
void log_source_stdio(log_source_t *src, void *file);

/* --------------------------------------------------------------- rows ---- */

/** Streaming row splitter: quotes, doubled quotes, and CRLF. */
typedef struct {
    log_source_t *src;
    char buf[256];
    size_t len;
    size_t pos;
    bool eof;
    bool started; /**< the byte order mark, if any, has been dealt with */
    bool failed;  /**< a read reported an error; the rest of the file is lost */
    bool overflow;/**< the last row did not fit the field store */
} log_reader_t;

void log_reader_init(log_reader_t *r, log_source_t *src);

/**
 * Read one row.
 *
 * @param store        scratch for the field text; fields point into it
 * @param fields       receives one pointer per field, up to @p max_fields
 * @param total_fields receives the true field count, which may exceed
 *                     @p max_fields; that is how a ragged row is spotted
 * @return the number of fields stored, or -1 at end of input
 */
int log_reader_row(log_reader_t *r, char delimiter, char *store,
                   size_t store_size, char **fields, int max_fields,
                   int *total_fields);

/* ------------------------------------------------------------ analysis --- */

typedef enum {
    LOG_OK = 0,
    LOG_ERR_READ,
    LOG_ERR_NO_HEADER,
    LOG_ERR_NO_ROWS,
    LOG_ERR_NO_NUMERIC,
    LOG_ERR_TOO_MANY_COLUMNS,
    LOG_ERR_MEMORY,
    LOG_ERR_ARG
} log_err_t;

const char *log_err_str(log_err_t err);

typedef struct {
    char raw_header[LOG_NAME_MAX];
    char name[LOG_NAME_MAX];
    char unit[LOG_UNIT_MAX];
    char sample[LOG_SAMPLES][LOG_SAMPLE_MAX];
    int non_empty;
    int parsed;
    bool unit_from_values;
    bool mixed_units;
    bool numeric; /**< at least 80 % of its non-empty cells parsed */
    bool monotonic;
    double min;
    double max;
    double first;
    double last;
} log_column_t;

typedef struct {
    char delimiter;
    log_conv_t convention;
    bool convention_forced;
    bool convention_confident;
    bool convention_conflict;
    log_votes_t votes;
    log_ambig_t ambiguous_as;

    int n_columns;
    log_column_t columns[LOG_MAX_COLUMNS];

    int time_index; /**< -1 when there is none; then the x axis is the index */
    char time_unit[LOG_UNIT_MAX];

    int row_count;
    int ragged_rows; /**< rows whose field count differs from the header */
    int long_rows;   /**< rows too long for the field store; cells were lost */
    int max_fields;
    bool truncated; /**< the row cap was hit; row_count is what was read */
} log_analysis_t;

typedef struct {
    char delimiter;          /**< 0 sniffs                                  */
    log_conv_t convention;   /**< LOG_CONV_AUTO infers                      */
    log_ambig_t ambiguous_as;
    int time_index;          /**< -2 detects, -1 forces none, >= 0 forces it */
    const char *time_unit;   /**< NULL guesses                              */
    int max_rows;            /**< 0 uses LOG_DEFAULT_MAX_ROWS               */
} log_csv_opts_t;

#define LOG_DEFAULT_MAX_ROWS 60000
#define LOG_TIME_AUTO (-2)

void log_csv_opts_default(log_csv_opts_t *opts);

/** The candidate delimiters, in the order they are scored. */
extern const char log_csv_delimiters[4];
const char *log_csv_delimiter_label(char delimiter);

/** Guess the delimiter from a text sample: the most consistent, widest split. */
char log_csv_sniff(const char *sample, size_t len);

/** Two streaming passes: geometry and evidence, then per-column statistics. */
log_err_t log_csv_analyse(log_source_t *src, const log_csv_opts_t *opts,
                          log_analysis_t *out);

/* --------------------------------------------------------------- build --- */

typedef struct {
    char name[LOG_NAME_MAX];
    char unit[LOG_UNIT_MAX];
    char group[LOG_NAME_MAX];
    int column;
    float min;
    float max;
    bool constant;
    bool mixed_units;
} log_field_t;

typedef struct {
    int count;
    int n_fields;
    float *time; /**< seconds from the first sample */
    float *value[LOG_MAX_SERIES];
    log_field_t field[LOG_MAX_SERIES];

    char time_name[LOG_NAME_MAX];
    char time_unit[LOG_UNIT_MAX];
    log_conv_t convention;
    char delimiter;
    int unparsed_cells;

    double duration_s;
    double median_dt_s;
    double max_gap_s;
    double rate_hz;
} log_data_t;

/**
 * Materialise the selected columns.
 *
 * @param columns  column indices to plot, or NULL to take the first numeric
 *                 ones that are not the time axis
 */
log_err_t log_csv_build(log_source_t *src, const log_analysis_t *a,
                        const int *columns, int n_columns, log_data_t *out);

void log_data_free(log_data_t *data);

/**
 * Where the sample arrays come from.  The firmware points this at PSRAM; the
 * default is malloc/free, which is what the host tests want.
 */
void log_set_allocator(void *(*alloc_fn)(size_t), void (*free_fn)(void *));

#ifdef __cplusplus
}
#endif
