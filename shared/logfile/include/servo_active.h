/**
 * @file
 * @brief The active servo: the labelled servo the bench's next run files
 *        under, and the one owner of it.
 *
 * A servo becomes active by a scan of its label, by NEW ID, or by a pick
 * from the card's recent ids (servo_active_from_t); NONE clears it.  Every
 * one of those goes through servo_active_set() or servo_active_clear().
 * Both refuse while a run is active and change nothing then: a run takes
 * its servo at its START (a test) or at the arm (a SERVO run) and keeps it
 * to its last file.  The caller says whether a run is active.
 *
 * The active servo is session state: none after power-on.  The prefix the
 * next NEW ID uses is a setting (SET_TEXT_SERVO_PREFIX) and survives a
 * restart.
 *
 * State of the frame task: no lock, no card access.  The panel copies the
 * id to the logger as it copies DUT NAME.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief How the active servo became active. */
typedef enum {
    SERVO_ACTIVE_NONE = 0,  /**< no servo active                        */
    SERVO_ACTIVE_SCAN,      /**< its label was scanned                  */
    SERVO_ACTIVE_NEW,       /**< NEW ID made it                         */
    SERVO_ACTIVE_PICK,      /**< picked from the card's recent ids      */
} servo_active_from_t;

/**
 * @brief The active servo's id, upper case.
 * @return The id; NULL for none.
 */
const char *servo_active_id(void);

/**
 * @brief How the active servo became active.
 * @return SERVO_ACTIVE_NONE for none.
 */
servo_active_from_t servo_active_from(void);

/**
 * @brief Make servo @p id the active one.
 *
 * The id already active stays, with @p from in place of its source and its
 * printed flag kept.  Another id starts unprinted.
 *
 * @param id         The id; servo_id_parse() must take it.  Kept in upper
 *                   case.
 * @param from       SERVO_ACTIVE_SCAN, SERVO_ACTIVE_NEW or
 *                   SERVO_ACTIVE_PICK.
 * @param run_active Whether a run is active.
 * @return false, and nothing changed, while @p run_active, for a malformed
 *         id and for SERVO_ACTIVE_NONE or a source out of range.
 */
bool servo_active_set(const char *id, servo_active_from_t from,
                      bool run_active);

/**
 * @brief No servo active.
 * @param run_active Whether a run is active.
 * @return false, and nothing changed, while @p run_active.
 */
bool servo_active_clear(bool run_active);

/**
 * @brief A count of the changes: the id, its source or its printed flag.
 *        A screen redraws what shows the active servo when it moves.
 * @return The count; it wraps.
 */
uint32_t servo_active_serial(void);

/**
 * @brief Whether a label of the active servo has printed since it became
 *        active.
 * @return false for none.
 */
bool servo_active_printed(void);

/**
 * @brief Record that the active servo's label printed.  Nothing with no
 *        servo active.
 */
void servo_active_set_printed(void);

/* ------------------------------------------------------------ the scan
 * A scanned line decided against the active servo (PLAN-B 3.3).  The
 * grammar is servo_id_parse()'s on the line as received: nothing is
 * trimmed and no case is folded, so a line that is not exactly an id in
 * upper case is no servo.  A line equal to the last one taken, less than
 * SERVO_SCAN_REPEAT_MS after it and with no change of the active servo
 * between (servo_active_serial()), is a repeat of the scanner's and is
 * dropped. */

/** @brief The bytes of a scanned line kept for the repeat check, as the
 *         scanner's framer hands out at most (SCAN_LINE_MAX). */
#define SERVO_SCAN_LINE_MAX 64u
/** @brief A line equal to the last one within this many ms is a repeat. */
#define SERVO_SCAN_REPEAT_MS 3000u

/** @brief What a scanned line did. */
typedef enum {
    SERVO_SCAN_SET = 0,      /**< the line's id is the active servo now      */
    SERVO_SCAN_SAME,         /**< the active servo, scanned again             */
    SERVO_SCAN_REPEAT,       /**< the last line again within 3.0s: dropped    */
    SERVO_SCAN_RUN_OWN,      /**< the run's own servo during the run: dropped */
    SERVO_SCAN_REFUSED_RUN,  /**< another id during a run: nothing changed    */
    SERVO_SCAN_MALFORMED,    /**< a complete line that is no servo id         */
    SERVO_SCAN_TOO_LONG,     /**< a line past SERVO_SCAN_LINE_MAX             */
} servo_scan_t;

/**
 * @brief Decide what a scanned line does, and make its id the active servo
 *        (source SERVO_ACTIVE_SCAN) where it does.
 *
 * In this order: a repeat; a line too long; a malformed line; during a
 * run, the run's own id (dropped) or another (refused); the active
 * servo's id (SAME, its source becomes SERVO_ACTIVE_SCAN); else SET.
 *
 * @param line       The line without its CR LF; NULL counts as "".
 * @param too_long   The scanner cut the line at SERVO_SCAN_LINE_MAX.
 * @param run_active Whether a run is active.
 * @param now_ms     The millisecond clock; compared wrap-safe.
 * @return What the line did.
 */
servo_scan_t servo_scan_decide(const char *line, bool too_long,
                               bool run_active, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
