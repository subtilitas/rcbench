/**
 * @file
 * @brief The names of a labelled servo's files: `Servos/<id>/<kind>_NNN.csv`
 *        and its report `<kind>_NNN.txt`.
 *
 * A servo's folder holds the runs of 4 kinds, each numbered from 000 to 999
 * on its own:
 *
 * | Kind           | servo_kind_t     | Stem             | Name on main  |
 * | -------------- | ---------------- | ---------------- | ------------- |
 * | LIMIT RUN      | SERVO_KIND_LIMIT | `limit run`      | LIMITnnn      |
 * | RUDDER AUTO    | SERVO_KIND_RAUTO | `rudder auto`    | RUDDRnnn      |
 * | AUTOMATIC TEST | SERVO_KIND_ATEST | `automatic test` | ATESTnnn      |
 * | an ARMED run from SERVO | SERVO_KIND_RUN | `servo run` | SERVOnnn    |
 *
 * The names are FAT (File Allocation Table) long names, written in lower
 * case with `.csv` and `.txt`; a reader takes any case.  The longest is
 * `automatic test_999.csv`, 22 characters.  A name the parser does not take
 * (`trend_limit run.csv`, `limit run_007.csv.bak`) is no run.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "log_name.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The first number of a kind. */
#define SERVO_RUN_FIRST 0
/** @brief The last number of a kind. */
#define SERVO_RUN_LAST  999
/** @brief Bytes of the longest name, "automatic test_999.csv", and its
 *         terminator. */
#define SERVO_NAME_SIZE 23u

/** @brief The kinds of run a servo's folder holds. */
typedef enum {
    SERVO_KIND_LIMIT = 0,  /**< LIMIT RUN                                */
    SERVO_KIND_RAUTO,      /**< RUDDER AUTO                              */
    SERVO_KIND_ATEST,      /**< AUTOMATIC TEST                           */
    SERVO_KIND_RUN,        /**< an ARMED run from LIVE, SWEEP or RUDDER  */
    SERVO_KIND_COUNT
} servo_kind_t;

/**
 * @brief The kind a writer of main files as.
 * @param p The writer's prefix.
 * @return LIMIT, RUDDR, ATEST and SERVO's kind; SERVO_KIND_COUNT for the
 *         others, which never file under a servo.
 */
servo_kind_t servo_kind_of_prefix(log_prefix_t p);

/**
 * @brief The stem of kind @p k, "limit run".
 * @param k The kind.
 * @return The stem; "" for a kind out of range.  Never NULL.
 */
const char *servo_kind_stem(servo_kind_t k);

/**
 * @brief The name of run @p number of kind @p k: "limit run_007.csv", or
 *        its report "limit run_007.txt".
 *
 * @param out    Receives the name; "" for a kind or number out of range or
 *               an @p out too small.
 * @param n      Size of @p out, bytes; SERVO_NAME_SIZE holds every name.
 * @param k      The kind.
 * @param number The number, SERVO_RUN_FIRST to SERVO_RUN_LAST.
 * @param report true for the report's name.
 */
void servo_run_name(char *out, size_t n, servo_kind_t k, int number,
                    bool report);

/**
 * @brief Read a name back: exactly `<stem>_<3 digits>.csv` or `.txt`, any
 *        case.
 *
 * @param name   The name.
 * @param k      Receives the kind when a run; NULL for none.
 * @param report Receives whether it is the report; NULL for none.
 * @return The number, 0 to 999; -1 for a name that is no run.
 */
int servo_run_parse(const char *name, servo_kind_t *k, bool *report);

/**
 * @brief The report's name of run @p run_name: "limit run_007.csv" gives
 *        "limit run_007.txt".
 *
 * @param out      Receives the name, lower case; "" for a name that is no
 *                 run or an @p out too small.
 * @param n        Size of @p out, bytes.
 * @param run_name The run's CSV or report name.
 */
void servo_run_report_of(char *out, size_t n, const char *run_name);

/**
 * @brief Whether @p dir is a servo's folder, `Servos/<id>` in any case, and
 *        its id.
 *
 * @param dir The folder from the card's root.
 * @param id  Receives the id in upper case when true; NULL for none.
 * @param n   Size of @p id, bytes; SERVO_ID_SIZE holds every id.  Too small
 *            is false.
 * @return true for a well-formed id under CARD_SERVOS.
 */
bool servo_dir_parse(const char *dir, char *id, size_t n);

#ifdef __cplusplus
}
#endif
