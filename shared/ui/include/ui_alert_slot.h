/**
 * @file
 * @brief The control task's alert slot, and the band's record of the alert
 *        it took from it.
 *
 * The control task never calls the router (ui_screen.h): it posts an alert
 * into a slot, and the render loop takes it into the band once a frame.
 * The slot holds one alert; a later one replaces it.  Every alert posted is
 * numbered, so the control task can tell an alert shown from one replaced
 * before a frame took it, and can take back one whose cause is over:
 *
 *   - not yet taken: it is dropped and never shows;
 *   - taken, and still the last one taken: the slot records its number,
 *     and the render loop clears it from the band while the band still
 *     shows it (ui_router_withdraw_alert());
 *   - anything else -- an older alert, one replaced or already withdrawn --
 *     is left alone: the band shows no alert of the slot but the last one
 *     taken, so the record of that one is all a withdrawal needs.
 *
 * The slot has no lock of its own; the caller holds the one that guards
 * the memory it lives in.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The slot: one alert, numbered, and the record of the last one
 *         taken and taken back. */
typedef struct {
    char     text[UI_ALERT_MAX]; /**< the alert posted                  */
    bool     pending;    /**< posted and not yet taken                    */
    uint32_t posted;     /**< number of the last alert posted, 0 none     */
    uint32_t taken;      /**< number of the last alert taken, 0 none      */
    uint32_t withdrawn;  /**< number of a taken alert taken back, 0 none  */
} ui_alert_slot_t;

/**
 * @brief Post @p text, replacing an alert not yet taken.
 * @param s    the slot
 * @param text the alert, cut to UI_ALERT_MAX - 1 bytes
 * @return its number, never 0
 */
uint32_t ui_alert_slot_post(ui_alert_slot_t *s, const char *text);

/**
 * @brief Take back alert @p number: its cause is over.
 * @param s      the slot
 * @param number its number; 0 does nothing
 */
void ui_alert_slot_withdraw(ui_alert_slot_t *s, uint32_t number);

/**
 * @brief Take back alert @p *number once its cause is over, and forget it:
 *        the alert a standing STOP gives an ARM or an ON it refuses, which
 *        goes when the STOP's reset completes, at once and not after
 *        UI_ALERT_SHOW_S.  While the cause stands, or with no number kept,
 *        nothing changes.
 * @param s      the slot
 * @param number the alert's number as ui_alert_slot_post() gave it, 0 for
 *               none; set to 0 when it is taken back
 * @param stands whether its cause still stands
 * @return true when it took the alert back
 */
bool ui_alert_slot_withdraw_over(ui_alert_slot_t *s, uint32_t *number,
                                 bool stands);

/**
 * @brief Copy the alert posted and not yet taken into @p out and mark it
 *        taken.
 * @param s    the slot
 * @param out  takes the alert; untouched when there is none
 * @param size bytes at @p out
 * @return false when there is none
 */
bool ui_alert_slot_take(ui_alert_slot_t *s, char *out, size_t size);

/** @brief What the render loop keeps of the slot's alert it put in the
 *         band. */
typedef struct {
    uint32_t slot;   /**< its number in the slot, 0 none           */
    uint32_t band;   /**< its number in the band, ui_router_alert_number() */
} ui_alert_band_t;

/**
 * @brief Hand the slot's news to the router, outside the slot's lock.
 *        Shows a new alert, and clears the one in the band when the slot
 *        took it back and the band still shows it.
 * @param b         the band's record
 * @param text      the alert just taken, NULL for none
 * @param taken     its slot number
 * @param withdrawn the slot's withdrawal record as read with it
 * @return true when it cleared one
 */
bool ui_alert_band_update(ui_alert_band_t *b, const char *text,
                          uint32_t taken, uint32_t withdrawn);

#ifdef __cplusplus
}
#endif
