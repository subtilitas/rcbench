/**
 * @file upd_dev.h
 * @brief SETUP > UPDATES' rows of the wireless devices: DIAL FIRMWARE, the
 *        M5Stack Dial's.  Each row names the device's version and the
 *        version of the image this panel carries, and holds HOLD 2s TO
 *        UPDATE THE DIAL, or HOLD 2s TO REINSTALL THE DIAL FIRMWARE while
 *        the Dial reports the carried version.
 *
 * The DIAL FIRMWARE row sits in UPDATES' left column between the IOmcu
 * section and the IOmcu's hold: its heading at body y 236, its line at
 * y 260 and its hold, 460 x 56px, at y 316.  The hold is live only while
 * the application's view reads DEVUPD_ROW_READY or DEVUPD_ROW_REINSTALL.
 * A release before 2s on a live hold shows the hold's hint on the band; a
 * tap on a grey hold shows why it is grey there (upd_dev_reason()).  While
 * an update runs the button reads DIAL UPDATE: CONNECTING, PRESS THE DIAL
 * TO ACCEPT with the seconds the Dial gave, or DIAL UPDATE and the share
 * the Dial took.
 *
 * UPDATES (upd_screen.c) hands the rows every touch event first
 * (upd_dev_event()), its ticks and its draw; the application hands each
 * row what to show (upd_dev_set()) and drains its completed hold
 * (upd_dev_take()).  A row commands nothing.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_UPD_DEV_H
#define RCBENCH_UPD_DEV_H

#include <stdbool.h>
#include <stddef.h>

#include "devupd.h"
#include "gfx.h"
#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The rows. */
typedef enum {
    UPD_DEV_DIAL = 0, /**< DIAL FIRMWARE */
    UPD_DEV_COUNT,
} upd_dev_id_t;

/** @name The DIAL FIRMWARE row, in UPDATES' body
 *  @{ */
#define UPD_DEV_X       16    /**< left edge: the IOmcu section's        */
#define UPD_DEV_W       460   /**< width: the IOmcu hold's                */
#define UPD_DEV_HEAD_Y  236   /**< the heading                            */
#define UPD_DEV_LINE_Y  260   /**< the versions' line                     */
#define UPD_DEV_BTN_Y   316   /**< the hold's top: one pitch over the
                                   IOmcu's                                */
#define UPD_DEV_BTN_H   56    /**< the hold's height                      */
/** @} */

/** @brief What a row shows. */
typedef struct {
    devupd_row_t row;        /**< devupd_row()                            */
    bool         known;      /**< the device reported its version         */
    fw_version_t running;    /**< that version                            */
    bool         have_image; /**< this panel carries an image that passed */
    fw_version_t carried;    /**< its version                             */
    uint8_t      phase;      /**< DEVUPD_ROW_UPDATING: devupd_phase_t     */
    unsigned     percent;    /**< DEVUPD_SEND: the share taken, 0-100     */
    unsigned     secs;       /**< DEVUPD_CONFIRM: seconds the device gave */
} upd_dev_view_t;

/**
 * @brief Hand a row what to show; a hold the new state no longer allows
 *        ends unfired.  A change repaints UPDATES.
 * @param id the row
 * @param v  the view; NULL does nothing
 */
void upd_dev_set(upd_dev_id_t id, const upd_dev_view_t *v);

/**
 * @brief What a row shows, for the tests.
 * @param id the row
 * @return the view; the first row's for an id out of range
 */
const upd_dev_view_t *upd_dev_view(upd_dev_id_t id);

/**
 * @brief Whether a row's hold completed, once.
 * @param id the row
 * @return true once after the hold completed
 */
bool upd_dev_take(upd_dev_id_t id);

/**
 * @brief The band's line for a grey hold: why it is grey.
 * @param v the view; NULL is none
 * @return the text, or NULL while the hold is live
 */
const char *upd_dev_reason(const upd_dev_view_t *v);

/**
 * @brief A row's versions' line.
 * @param id  the row
 * @param v   the view
 * @param out the line
 * @param n   bytes of @p out
 */
void upd_dev_line(upd_dev_id_t id, const upd_dev_view_t *v, char *out,
                  size_t n);

/**
 * @brief A row's button label.
 * @param id  the row
 * @param v   the view
 * @param out the label
 * @param n   bytes of @p out
 */
void upd_dev_label(upd_dev_id_t id, const upd_dev_view_t *v, char *out,
                   size_t n);

/**
 * @brief A row's hold, in UPDATES' body.
 * @param id the row
 * @return the rectangle
 */
gfx_rect_t upd_dev_rect(upd_dev_id_t id);

/**
 * @brief A touch event on UPDATES, offered to the rows first.
 * @param evt   the event, body coordinates
 * @param other whether another control of UPDATES holds a press: then a
 *              DOWN is not a row's
 * @return true when a row took the event
 */
bool upd_dev_event(const touch_event_t *evt, bool other);

/** @brief Whether a press is on a row. @return true while one is */
bool upd_dev_pressed(void);

/**
 * @brief Time passed for the holds and their flashes.
 * @param dt_s the time, s
 * @return true while a fill or a flash changes a row
 */
bool upd_dev_tick(float dt_s);

/** @brief Every press ends unfired: UPDATES' cancel, leave, enter, reset. */
void upd_dev_drop(void);

/**
 * @brief Draw the rows.
 * @param c canvas, UPDATES' body
 */
void upd_dev_render(gfx_canvas_t *c);

/** @brief Whether a hold's flash runs. @return true while one does */
bool upd_dev_flashing(void);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_UPD_DEV_H */
