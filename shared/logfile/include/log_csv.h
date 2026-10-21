/**
 * @file
 * @brief CSV and TSV import that streams a log too large to hold in memory.
 *
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
/** @brief Most columns one import keeps. */
#define LOG_MAX_COLUMNS 24
/** @brief Most series the build materialises. */
#define LOG_MAX_SERIES  4
/** @brief Room for a column name, bytes, with its terminator. */
#define LOG_NAME_MAX    28
/** @brief Room for one sample cell, bytes, with its terminator. */
#define LOG_SAMPLE_MAX  16
/** @brief Number of sample cells kept per column. */
#define LOG_SAMPLES     3
/** @brief Most bytes in one row. */
#define LOG_ROW_MAX     1024

/* -------------------------------------------------------------- source --- */

/**
 * @brief A byte source that can be read from the top more than once.
 *
 * Not a FILE*: the host tests feed strings, the firmware feeds a file on the
 * card, and neither knows about the other.
 */
typedef struct {
    size_t (*read)(void *ctx, char *buf, size_t max); /**< 0: end of input */
    bool (*rewind)(void *ctx);
    /**
     * @brief Whether the last read failed, as opposed to reaching the end.
     *
     * Optional: NULL means "reads never fail", which is true of a block of
     * memory and false of an SD card.  Without it a bad sector reads as a
     * clean end of file, and a truncated log is presented as a complete short
     * one.
     */
    bool (*error)(void *ctx);
    void *ctx;
    /**
     * @brief Optional length and seek of the input.
     *
     * Optional, both or neither: the length of the input in bytes, and a
     * move to an offset from its start, after which read() continues there.
     * With them the analysis reads the last bytes of a file past the row cap
     * for the cut row, instead of every row up to them.  NULL means the
     * source can only be read from the top; it is then read on to its end.
     * Last in the struct, so a source written as { read, rewind, error, ctx }
     * has neither.
     */
    bool (*size)(void *ctx, size_t *bytes);
    bool (*seek)(void *ctx, size_t offset);
} log_source_t;

/** @brief State of the memory source. */
typedef struct {
    const char *text;   /**< the input                                 */
    size_t len;         /**< length of the input, bytes                */
    size_t pos;         /**< read position, bytes                      */
} log_mem_ctx_t;

/** @brief A source over a block of memory. */
void log_source_memory(log_source_t *src, log_mem_ctx_t *ctx, const char *text,
                       size_t len);

/** @brief A source over an open stdio stream.  @p file is a FILE*. */
void log_source_stdio(log_source_t *src, void *file);

/* --------------------------------------------------------------- rows ---- */

/** @brief Streaming row splitter: quotes, doubled quotes, and CRLF. */
typedef struct {
    log_source_t *src;     /**< the input                                    */
    char buf[256];         /**< read-ahead buffer, bytes                     */
    size_t len;            /**< bytes in buf                                 */
    size_t pos;            /**< next byte of buf to read                     */
    bool eof;              /**< the input ended                              */
    bool started; /**< the byte order mark, if any, has been dealt with */
    bool failed;  /**< a read reported an error; the rest of the file is lost */
    bool overflow;/**< the last row did not fit the field store */
    bool cut;     /**< the input ended inside a row, which was not returned */
} log_reader_t;

/**
 * @brief Set up a row splitter.
 *
 * @param r    The splitter.
 * @param src  The input.
 */
void log_reader_init(log_reader_t *r, log_source_t *src);

/**
 * @brief Read one row.
 *
 * A row ends at its line feed (0x0A).  Input that ends inside a row -- a
 * write cut off by a power loss or a full card -- leaves a row nothing proves
 * complete: a number cut after two of its four digits still parses, and the
 * field count can still match the header.  That row is not returned: the
 * call answers -1 as at the end of input and sets @c r->cut.
 *
 * @param r            The splitter.
 * @param delimiter    The field delimiter.
 * @param store        scratch for the field text; fields point into it
 * @param store_size   Size of @p store, bytes.
 * @param fields       receives one pointer per field, up to @p max_fields
 * @param max_fields   Capacity of @p fields, in pointers.
 * @param total_fields receives the true field count, which may exceed
 *                     @p max_fields; that is how a ragged row is spotted
 * @return the number of fields stored, or -1 at end of input
 */
int log_reader_row(log_reader_t *r, char delimiter, char *store,
                   size_t store_size, char **fields, int max_fields,
                   int *total_fields);

/* ------------------------------------------------------------ analysis --- */

/** @brief Result of a CSV import step. */
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

/**
 * @brief Text for an import error.
 *
 * @param err  The error.
 * @return A static text.
 */
const char *log_err_str(log_err_t err);

/** @brief Analysis of one column. */
typedef struct {
    char raw_header[LOG_NAME_MAX];   /**< header text as written           */
    char name[LOG_NAME_MAX];         /**< column name                      */
    char unit[LOG_UNIT_MAX];         /**< unit of the column               */
    char sample[LOG_SAMPLES][LOG_SAMPLE_MAX];  /**< first cells, as written */
    int non_empty;                   /**< cells that are not empty         */
    int parsed;                      /**< cells that parsed as numbers     */
    bool unit_from_values;           /**< the unit came from the cells     */
    bool mixed_units;                /**< the cells carry more than one unit */
    bool numeric; /**< at least 80 % of its non-empty cells parsed */
    bool monotonic;                  /**< the values never fall            */
    double min;                      /**< smallest value                   */
    double max;                      /**< largest value                    */
    double first;                    /**< first value                      */
    double last;                     /**< last value                       */
} log_column_t;

/** @brief Result of the analysis of a whole file. */
typedef struct {
    char delimiter;                  /**< field delimiter                  */
    log_conv_t convention;           /**< decimal convention               */
    bool convention_forced;          /**< set by the caller                */
    bool convention_confident;       /**< the detected convention is sure  */
    bool convention_conflict;        /**< the evidence disagrees           */
    log_votes_t votes;               /**< evidence for each convention     */
    log_ambig_t ambiguous_as;        /**< how an ambiguous cell reads      */

    int n_columns;                   /**< columns found                    */
    log_column_t columns[LOG_MAX_COLUMNS];  /**< one per column           */

    int time_index; /**< -1 when there is none; then the x axis is the index */
    char time_unit[LOG_UNIT_MAX];    /**< unit of the time column          */

    int row_count;                   /**< rows read                        */
    int ragged_rows; /**< rows whose field count differs from the header */
    int long_rows;   /**< rows too long for the field store; cells were lost */
    int max_fields;                  /**< most fields in one row           */
    bool truncated; /**< the row cap was hit; row_count is what was read */
    /** @brief The file ends inside a row, without its line feed.
     *
     *  That row is left out of row_count and of every figure: a write cut off
     *  mid-row leaves a number that is wrong and still parses. */
    bool cut_row;
} log_analysis_t;

/** @brief Options of the CSV import. */
typedef struct {
    char delimiter;          /**< 0 sniffs                                  */
    log_conv_t convention;   /**< LOG_CONV_AUTO infers                      */
    log_ambig_t ambiguous_as;
    int time_index;          /**< -2 detects, -1 forces none, >= 0 forces it */
    const char *time_unit;   /**< NULL guesses                              */
    int max_rows;            /**< 0 uses LOG_DEFAULT_MAX_ROWS               */
} log_csv_opts_t;

/** @brief Row cap used when the options give 0, rows. */
#define LOG_DEFAULT_MAX_ROWS 60000
/** @brief time_index value that detects the time column. */
#define LOG_TIME_AUTO (-2)

/**
 * @brief Set the default import options.
 *
 * @param opts  Receives the defaults.
 */
void log_csv_opts_default(log_csv_opts_t *opts);

/** @brief The candidate delimiters, in the order they are scored. */
extern const char log_csv_delimiters[4];

/**
 * @brief Label of a delimiter.
 *
 * @param delimiter  The delimiter.
 * @return A static text.
 */
const char *log_csv_delimiter_label(char delimiter);

/**
 * @brief Guess the delimiter from a text sample.
 *
 * Guess the delimiter from a text sample: the most consistent, widest split.
 *
 * @param sample  The sample text.
 * @param len     Length of @p sample, bytes.
 * @return The delimiter.
 */
char log_csv_sniff(const char *sample, size_t len);

/**
 * @brief Analyse a file in two streaming passes.
 *
 * Two streaming passes: geometry and evidence, then per-column statistics.
 *
 * @param src   The input.
 * @param opts  The import options.
 * @param out   Receives the analysis.
 * @return LOG_OK, or the reason the analysis failed.
 */
log_err_t log_csv_analyse(log_source_t *src, const log_csv_opts_t *opts,
                          log_analysis_t *out);

/* --------------------------------------------------------------- build --- */

/** @brief One plotted field. */
typedef struct {
    char name[LOG_NAME_MAX];   /**< column name                             */
    char unit[LOG_UNIT_MAX];   /**< unit of the column                      */
    char group[LOG_NAME_MAX];  /**< group the field belongs to              */
    int column;                /**< index into the analysis columns         */
    float min;                 /**< smallest value                          */
    float max;                 /**< largest value                           */
    bool constant;             /**< every value is the same                 */
    bool mixed_units;          /**< the cells carry more than one unit      */
} log_field_t;

/** @brief Materialised data of the selected columns. */
typedef struct {
    int count;                 /**< samples per series                      */
    int n_fields;              /**< series in use                           */
    float *time; /**< seconds from the first sample */
    float *value[LOG_MAX_SERIES];  /**< sample arrays, one per series       */
    log_field_t field[LOG_MAX_SERIES];  /**< one per series                 */

    char time_name[LOG_NAME_MAX];   /**< name of the time column            */
    char time_unit[LOG_UNIT_MAX];   /**< unit of the time column            */
    log_conv_t convention;          /**< decimal convention used            */
    char delimiter;                 /**< field delimiter used               */
    int unparsed_cells;             /**< cells that did not parse           */

    double duration_s;              /**< time from first to last sample, s  */
    double median_dt_s;             /**< median time between samples, s     */
    double max_gap_s;               /**< longest time between samples, s    */
    double rate_hz;                 /**< sample rate, Hz                    */
} log_data_t;

/**
 * @brief Materialise the selected columns.
 *
 * @param src        The input.
 * @param a          The analysis.
 * @param columns    column indices to plot, or NULL to take the first numeric
 *                   ones that are not the time axis
 * @param n_columns  Number of entries in @p columns.
 * @param out        Receives the data.
 * @return LOG_OK, or the reason the data was not built.
 */
log_err_t log_csv_build(log_source_t *src, const log_analysis_t *a,
                        const int *columns, int n_columns, log_data_t *out);

/**
 * @brief Free the sample arrays of a build.
 *
 * @param data  The data to free.
 */
void log_data_free(log_data_t *data);

/**
 * @brief Set the allocator for the sample arrays.
 *
 * Where the sample arrays come from.  The firmware points this at PSRAM; the
 * default is malloc/free, which is what the host tests want.
 *
 * @param alloc_fn  Allocates a block of bytes.
 * @param free_fn   Frees a block from @p alloc_fn.
 */
void log_set_allocator(void *(*alloc_fn)(size_t), void (*free_fn)(void *));

#ifdef __cplusplus
}
#endif
