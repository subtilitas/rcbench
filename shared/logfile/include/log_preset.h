/**
 * @file
 * @brief What each kind of file the bench writes shows first in LOGS' plot:
 *        the presets, and which one a file takes.
 *
 * Seven presets cover the run files: LIMIT (a LIMIT RUN's LIMITnnn.CSV),
 * RUDDR's two pictures SEARCH and STEPS (RUDDER AUTO's RUDDRnnn.CSV),
 * ATEST (the AUTOMATIC TEST), SERVO, MOTOR and SUPPL (the runs logged while
 * ARMED or while the SUPPLY output is on).  A preset names the x column,
 * the plot kind (a time plot or an XY plot over the x column), the series
 * shown first, and the column that splits a file into traces.
 *
 * A file is matched by its header's column names first and by its name
 * second (log_name.h), so a labelled name (MOTOR042_kv1200.CSV), a name in
 * lower case and a file of an older firmware match as well:
 *
 * | Preset | Header holds | Name |
 * | --- | --- | --- |
 * | LIMIT | `search`, `side`, `pulse`, `state` | LIMITnnn |
 * | RUDDR SEARCH | `step`, `side`, `write`, `within` | RUDDRnnn |
 * | ATEST | `test`, `phase`, `command` | ATESTnnn |
 * | SUPPL | `set`, `limit`, `mode` | SUPPLnnn |
 * | MOTOR | `rpm` and `throttle` | MOTORnnn, ESCnnn |
 * | SERVO | `pulse`, `angle`, `source` | SERVOnnn, BENCHnnn |
 *
 * A bench file of firmware 0.21.0 and older carries neither `throttle` nor
 * `pulse` (`rpm`, `esc`, `motor` and `window` among its columns): its name
 * decides, ESCnnn a MOTOR run and BENCHnnn or SERVOnnn a SERVO run, and any
 * other name reads as a MOTOR run.  RUDDR's STEPS picture is not matched:
 * a RUDDR file opens on SEARCH.
 *
 * Pure C, no ESP-IDF.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

#include "log_csv.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A preset: the picture one kind of file shows first. */
typedef enum {
    LOG_PRESET_NONE = 0,      /**< no preset: the import view's choice    */
    LOG_PRESET_LIMIT,         /**< LIMITnnn.CSV, current over the pulse   */
    LOG_PRESET_RUDDR_SEARCH,  /**< RUDDRnnn.CSV, the search positions     */
    LOG_PRESET_RUDDR_STEPS,   /**< RUDDRnnn.CSV, the steps and verifies   */
    LOG_PRESET_ATEST,         /**< ATESTnnn.CSV, the AUTOMATIC TEST       */
    LOG_PRESET_SERVO,         /**< SERVOnnn.CSV, ARMED on SERVO           */
    LOG_PRESET_MOTOR,         /**< MOTORnnn.CSV, ARMED on MOTOR & ESC     */
    LOG_PRESET_SUPPL,         /**< SUPPLnnn.CSV, the SUPPLY output on     */
    LOG_PRESET_COUNT
} log_preset_id_t;

/** @brief How a preset places its rows. */
typedef enum {
    LOG_PLOT_TIME = 0,  /**< over the time column                      */
    LOG_PLOT_XY         /**< over the x column, a point a row          */
} log_plot_kind_t;

/** @brief One preset's picture. */
typedef struct {
    log_preset_id_t id;      /**< the preset                                */
    log_plot_kind_t plot;    /**< the plot kind                             */
    const char *x;           /**< the x column's name                       */
    /** @brief The series shown first, in order; NULL ends a shorter list. */
    const char *series[LOG_MAX_SERIES];
    const char *split;       /**< the column that splits the traces, or NULL */
} log_preset_t;

/**
 * @brief The picture of preset @p id.
 * @param id  The preset.
 * @return The preset, or NULL for LOG_PRESET_NONE and an id outside the
 *         table.
 */
const log_preset_t *log_preset_get(log_preset_id_t id);

/**
 * @brief The preset a file's header names, by the table in this file's
 *        description.
 * @param an  The file's analysis.
 * @return The preset; LOG_PRESET_NONE for a header no preset knows, and a
 *         bench header of firmware 0.21.0 and older (its name decides), and
 *         with @p an NULL.
 */
log_preset_id_t log_preset_by_header(const log_analysis_t *an);

/**
 * @brief The preset a run log's name names: its prefix, case and label
 *        aside (log_name_parse()).
 * @param name  The file's name.
 * @return The preset; LOG_PRESET_NONE for a report (.TXT), a name that is
 *         no run log, and NULL.
 */
log_preset_id_t log_preset_by_name(const char *name);

/**
 * @brief The preset of a file: by its header, else by its name; a bench
 *        header of firmware 0.21.0 and older with a name no preset knows is
 *        a MOTOR run.
 * @param an    The file's analysis; NULL matches by the name alone.
 * @param name  The file's name; NULL matches by the header alone.
 * @return The preset, LOG_PRESET_NONE for a file no preset matches.
 */
log_preset_id_t log_preset_match(const log_analysis_t *an, const char *name);

/**
 * @brief The columns LOGS' plot of rows loads for preset @p id: on an XY
 *        preset its x column first, then its series, each where the file
 *        holds it as a numeric column that is not the time axis.  A column the file lacks is left out, and nothing takes its
 *        place.
 * @param id   The preset.
 * @param an   The file's analysis.
 * @param out  Takes the column indices.
 * @param max  The most columns to pick.
 * @return The number of columns picked; 0 for LOG_PRESET_NONE, a file that
 *         holds none of them, and @p an or @p out NULL.
 */
int log_preset_picks(log_preset_id_t id, const log_analysis_t *an, int *out,
                     int max);

#ifdef __cplusplus
}
#endif
