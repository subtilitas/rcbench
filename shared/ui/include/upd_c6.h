/**
 * @file upd_c6.h
 * @brief SETUP > UPDATES' C6 RADIO row: the version the board's ESP32-C6
 *        reports, the version of the image this panel carries, and
 *        HOLD 2s TO UPDATE THE C6 RADIO.
 *
 * The row sits in UPDATES' right column under the card: one line with both
 * versions ("not reported" while the C6 has named none), and the hold
 * under it, 532 x 56px at body y 428.  The hold is live only while the
 * application's view reads FW_C6_ROW_READY.  A release before 2s on a live
 * hold shows the hold's hint on the alert band; a tap on a grey hold shows
 * why it is grey there (upd_c6_reason()).  While an update runs the button
 * reads C6 UPDATE and the share sent.
 *
 * UPDATES (upd_screen.c) hands the row every touch event first
 * (upd_c6_event()), its ticks, and its draw; the application hands it what
 * to show (upd_c6_set()) and drains the completed hold (upd_c6_take()).
 * The row commands nothing.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_UPD_C6_H
#define RCBENCH_UPD_C6_H

#include <stdbool.h>
#include <stddef.h>

#include "fw_c6.h"
#include "gfx.h"
#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @name The row, in UPDATES' body
 *  @{ */
#define UPD_C6_X      484   /**< left edge: the card's                    */
#define UPD_C6_W      532   /**< width: the card's                        */
#define UPD_C6_LINE_Y 396   /**< the versions' line                       */
#define UPD_C6_BTN_Y  428   /**< the hold's top: the IOmcu hold's row     */
#define UPD_C6_BTN_H  56    /**< the hold's height                        */
/** @} */

/** @brief What the row shows. */
typedef struct {
    fw_c6_row_t  row;        /**< fw_c6_row()                             */
    fw_c6_why_t  why;        /**< FW_C6_ROW_BLOCKED's reason              */
    bool         known;      /**< the C6 reported its version             */
    fw_version_t running;    /**< that version                            */
    bool         have_image; /**< this panel carries a C6 image           */
    fw_version_t carried;    /**< its version                             */
    unsigned     percent;    /**< FW_C6_ROW_UPDATING: the share sent, 0-100 */
} upd_c6_view_t;

/**
 * @brief Hand the row what to show; a hold the new state no longer allows
 *        ends unfired.  A change repaints UPDATES.
 * @param v the view; NULL does nothing
 */
void upd_c6_set(const upd_c6_view_t *v);

/** @brief What the row shows, for the tests. @return the view */
const upd_c6_view_t *upd_c6_view(void);

/**
 * @brief Whether the hold completed, once.
 * @return true once after the hold completed
 */
bool upd_c6_take(void);

/**
 * @brief The band's line for a grey hold: why it is grey.
 * @param v the view; NULL is none
 * @return the text, or NULL while the hold is live
 */
const char *upd_c6_reason(const upd_c6_view_t *v);

/**
 * @brief The versions' line.
 * @param v   the view
 * @param out the line
 * @param n   bytes of @p out
 */
void upd_c6_line(const upd_c6_view_t *v, char *out, size_t n);

/**
 * @brief The button's label: the hold's, or C6 UPDATE and the share while
 *        an update runs.
 * @param v   the view
 * @param out the label
 * @param n   bytes of @p out
 */
void upd_c6_label(const upd_c6_view_t *v, char *out, size_t n);

/** @brief The hold's rectangle in UPDATES' body. @return the rectangle */
gfx_rect_t upd_c6_rect(void);

/**
 * @brief A touch event on UPDATES, offered to the row first.
 * @param evt   the event, body coordinates
 * @param other whether another control of UPDATES holds a press: then a
 *              DOWN is not the row's
 * @return true when the row took the event
 */
bool upd_c6_event(const touch_event_t *evt, bool other);

/** @brief Whether a press is on the row. @return true while one is */
bool upd_c6_pressed(void);

/**
 * @brief Time passed for the hold and its flash.
 * @param dt_s the time, s
 * @return true while the fill or the flash changes the row
 */
bool upd_c6_tick(float dt_s);

/** @brief The press ends unfired: UPDATES' cancel, leave, enter, reset. */
void upd_c6_drop(void);

/**
 * @brief Draw the row.
 * @param c canvas, UPDATES' body
 */
void upd_c6_render(gfx_canvas_t *c);

/** @brief Whether the hold's flash runs. @return true while it does */
bool upd_c6_flashing(void);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_UPD_C6_H */
