/**
 * @file
 * @brief What the text and flag columns of an XY file add to its graph: the
 *        trace each row belongs to, the rows drawn in the warning colour or
 *        with a write's tick, and the vertical and horizontal markers.
 *
 * One streaming pass over the rows log_csv_build() takes
 * (log_csv_each_row()), so row i here is sample i of the build.  The
 * presets it knows (log_preset.h):
 *
 * | Preset | Traces | Markers |
 * | --- | --- | --- |
 * | LIMIT | one a pair of `search` (1, 2, 3) and `side` (LOW, HIGH); search 1 solid, 2 and 3 dashed | CENTER at the first row's pulse; a side that bound: BOUND at the pulse of its last BOUND row and END POINT at that row's `end point (us)`; LAST at a side's outermost pulse where that is not its BOUND; FREE and THRESHOLD of a side, levels of `current` from CENTER to its outermost pulse, read from its last row that has a threshold |
 * | RUDDR SEARCH | the SEARCH rows, a new trace where the pulse turns back; a trace that falls is LOW, one that rises HIGH | CENTER at the first SEARCH row's pulse; END POINT at each `side (us)` the STEP rows hold, at most one a side; LAST at each trace's last pulse that is no END POINT |
 * | RUDDR STEPS | the STEP rows, a trace for each `side (us)`; the VERIFY rows one trace of points | none; a row with `within` 0 is WARN, a STEP row with `write` above 0 is TICK |
 *
 * A row is a SEARCH, STEP or VERIFY row by its `kind`; a file of firmware
 * 0.23.17, without `kind`, by its `write`: `S`, `V` or a count.  A row whose
 * x cell does not read as a number is in no trace.  Any other preset leaves
 * every row in trace 0 and sets no marker.
 *
 * Pure C, no ESP-IDF.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "log_csv.h"
#include "log_preset.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The most markers a file sets; more are counted in dropped. */
#define LOG_MARKS_MAX  256
/** @brief The most traces a file splits into; a row of one more is in
 *         none. */
#define LOG_TRACES_MAX 14
/** @brief The most rows classified: an XY file is at most 600 rows (LIMIT)
 *         or 197 (RUDDR); a longer one sets cut. */
#define LOG_MARKS_ROWS 2048

/** @brief log_markset_t.row: the trace's number, LOG_TRACE_NONE for none. */
#define LOG_ROW_TRACE  0x0Fu
#define LOG_TRACE_NONE 0x0Fu  /**< The row is in no trace. */
#define LOG_ROW_WARN   0x10u  /**< Drawn in the warning colour. */
#define LOG_ROW_TICK   0x20u  /**< A write's tick under the row's point. */

/** @brief What a marker shows. */
typedef enum {
    LOG_MARK_CENTER = 0,  /**< where the search started              */
    LOG_MARK_BOUND,       /**< a side's bound pulse                  */
    LOG_MARK_END,         /**< a side's end point                    */
    LOG_MARK_LAST,        /**< a side's outermost position searched  */
    LOG_MARK_FREE,        /**< a side's free current, a level        */
    LOG_MARK_THRESHOLD,   /**< a side's bound threshold, a level     */
    LOG_MARK_KINDS
} log_mark_kind_t;

/** @brief A trace's side of the center. */
typedef enum {
    LOG_SIDE_NONE = 0,  /**< no side: STEPS' verifies, other presets */
    LOG_SIDE_LOW,       /**< below the center                         */
    LOG_SIDE_HIGH       /**< above it                                 */
} log_side_t;

/** @brief One marker. */
typedef struct {
    log_mark_kind_t kind;  /**< what it shows                          */
    bool  level;           /**< a horizontal level, else a vertical line */
    int   column;          /**< a level's column in the analysis; -1   */
    float at;              /**< the x of a line; the value of a level   */
    float x0;              /**< a level's start on x: the center        */
    float x1;              /**< its end, the side's outermost pulse,
                                where its tag stands; below x0 on the
                                low side                                */
} log_mark_t;

/** @brief One trace: the rows of one search of one side. */
typedef struct {
    uint8_t    search;  /**< LIMIT's search, 1 to 3; else 0           */
    log_side_t side;    /**< its side                                 */
    bool       dashed;  /**< drawn dashed: LIMIT's searches 2 and 3   */
    bool       points;  /**< drawn as points, not joined: verifies    */
} log_trace_t;

/** @brief A file's traces and markers. */
typedef struct {
    int         rows;      /**< rows classified                          */
    bool        cut;       /**< the file holds more than LOG_MARKS_ROWS  */
    /** @brief Per row: its trace (LOG_ROW_TRACE) and flags. */
    uint8_t     row[LOG_MARKS_ROWS];
    int         n_traces;  /**< traces in use                            */
    log_trace_t trace[LOG_TRACES_MAX];  /**< each trace                  */
    int         n_marks;   /**< markers set                              */
    int         dropped;   /**< markers past LOG_MARKS_MAX               */
    log_mark_t  mark[LOG_MARKS_MAX];    /**< each marker                 */
} log_markset_t;

/**
 * @brief Read a file's traces and markers for preset @p id.
 * @param src  The file.
 * @param an   Its analysis.
 * @param id   The preset it is drawn with.
 * @param out  Takes the traces and markers; cleared first.
 * @return LOG_OK; LOG_ERR_ARG with a NULL argument; the walk's error
 *         (log_csv_each_row()).
 */
log_err_t log_marks_read(log_source_t *src, const log_analysis_t *an,
                         log_preset_id_t id, log_markset_t *out);

/**
 * @brief Add marker @p m, or count it in dropped past LOG_MARKS_MAX.
 * @param mk  The markers.
 * @param m   The marker.
 * @return true when it was added.
 */
bool log_marks_add(log_markset_t *mk, const log_mark_t *m);

#ifdef __cplusplus
}
#endif
