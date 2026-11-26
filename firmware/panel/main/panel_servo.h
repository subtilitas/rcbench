/**
 * @file panel_servo.h
 * @brief The labelled servos on the panel: the card's ids (one walk of
 *        `Servos` on the logger task), NEW ID's folder, and the console's
 *        `servo` lines.
 *
 * The logger task walks `Servos` after each mount, after each change FILES
 * makes (log_series_forget_all()) and before each NEW ID, and publishes
 * what it read: the highest serial, the id folders seen and the 6 highest
 * ids (servo_ids_t).  NEW ID makes the next id from ID PREFIX and the
 * highest serial, makes its folder `Servos/<id>` (the folder reserves the
 * id) and answers with the id; the frame task then makes it the active
 * servo (servo_active.h), which a run refuses.
 *
 * The frame task calls panel_servo_frame() once a frame and the logger
 * task panel_servo_card_serve() in its pass; the console's task hands its
 * line to panel_servo_console().
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "servo_id.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief How NEW ID ended. */
typedef enum {
    PANEL_SERVO_NEW_OK = 0,      /**< the id and its folder made          */
    PANEL_SERVO_NEW_NO_CARD,     /**< no card mounted                     */
    PANEL_SERVO_NEW_UNREADABLE,  /**< `Servos` does not read to its end   */
    PANEL_SERVO_NEW_NO_FREE,     /**< the card holds ZZZZZ: no next id    */
    PANEL_SERVO_NEW_PREFIX,      /**< ID PREFIX is no prefix (servo_id.h) */
    PANEL_SERVO_NEW_FOLDER,      /**< `Servos` or the id's folder refused */
    PANEL_SERVO_NEW_RUN,         /**< a run holds the active servo        */
} panel_servo_new_t;

/** @brief The card's ids as the last walk read them. */
typedef struct {
    bool        known;     /**< a walk of this mount read to its end    */
    servo_ids_t ids;       /**< what it read                            */
    unsigned    walk_ms;   /**< how long it took, ms                    */
} panel_servo_view_t;

/**
 * @brief The last walk's view, under its lock.
 * @param out Takes it.
 */
void panel_servo_view(panel_servo_view_t *out);

/**
 * @brief Ask for NEW ID with @p prefix, on the frame task.  The answer
 *        comes through panel_servo_frame().
 * @param prefix ID PREFIX now.
 * @return false while a request is out.
 */
bool panel_servo_new(const char *prefix);

/**
 * @brief Whether a NEW ID asked through panel_servo_new() is not answered
 *        yet.
 * @return true from the ask to its answer.
 */
bool panel_servo_new_busy(void);

/**
 * @brief The frame task's pass: a console line taken, a NEW ID answered
 *        made the active servo, and the active servo copied to the logger
 *        (log_set_run_servo()).
 */
void panel_servo_frame(void);

/**
 * @brief The logger task's pass: the walk when the card or its files
 *        changed, and a NEW ID that waits.
 */
void panel_servo_card_serve(void);

/**
 * @brief A console line that starts with `servo`: `servo`, `servo new`,
 *        `servo id <id>`, `servo none`.  Taken on the next frame.
 * @param line The line, without its end.
 */
void panel_servo_console(const char *line);

#ifdef __cplusplus
}
#endif
