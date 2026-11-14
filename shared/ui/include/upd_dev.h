/**
 * @file upd_dev.h
 * @brief SETUP > UPDATES' wireless devices: the WIRELESS DEVICES entry on
 *        UPDATES and the page it opens with one row per device, DIAL
 *        FIRMWARE (the M5Stack Dial), POWER NODE FIRMWARE and BALANCE NODE
 *        FIRMWARE.  Each row names the device's version and the version of
 *        the image this panel carries, and holds HOLD 2s TO UPDATE THE
 *        DIAL (THE POWER NODE, THE BALANCE NODE), or HOLD 2s TO REINSTALL
 *        while the device reports the carried version.
 *
 * The entry sits in UPDATES' left column between the IOmcu section and the
 * IOmcu's hold: its heading at body y 236, its line (connected devices and
 * devices with another version) at y 260 and its button, 460 x 56px, at
 * y 316.  A tap on the button opens the page; while an update runs the
 * button reads that row's label.
 *
 * The page covers UPDATES' body: the rows at body y 24, 128 and 232, each
 * its heading and its line in the left half, 496px from x 16, and its
 * hold, 480 x 56px, from x 528; a note under them; the footer's name and
 * BACK in the footer's right slot.  BACK and the home tag close the page.
 * A hold is live only while the application's view reads DEVUPD_ROW_READY
 * or DEVUPD_ROW_REINSTALL.  A release before 2s on a live hold shows the
 * hold's hint on the band; a tap on a grey hold shows why it is grey
 * there (upd_dev_reason()).  While an update runs the hold reads the
 * device's UPDATE: CONNECTING, its ask (PRESS THE DIAL TO ACCEPT, HOLD
 * BOOT 1s ON THE NODE) with the seconds the device gave, or its UPDATE and
 * the share the device took, and CANCEL UPDATE stands in the footer's
 * middle slot: a plain tap, as every stop is.  From END on the device
 * installs whatever comes; CANCEL UPDATE is grey then, and a tap on it
 * says so on the band.
 *
 * UPDATES (upd_screen.c) hands this module every touch event first
 * (upd_dev_event()), its ticks and its draw, and the whole body while the
 * page is open (upd_dev_shown()); the application hands each row what to
 * show (upd_dev_set()) and drains its completed hold (upd_dev_take()).  A
 * row commands nothing.
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

/** @brief The rows, in the page's order. */
typedef enum {
    UPD_DEV_DIAL = 0, /**< DIAL FIRMWARE */
    UPD_DEV_PNODE,    /**< POWER NODE FIRMWARE */
    UPD_DEV_BAL,      /**< BALANCE NODE FIRMWARE */
    UPD_DEV_COUNT,
} upd_dev_id_t;

/** @name The WIRELESS DEVICES entry, in UPDATES' body
 *  @{ */
#define UPD_DEV_X       16    /**< left edge: the IOmcu section's        */
#define UPD_DEV_W       460   /**< width: the IOmcu hold's                */
#define UPD_DEV_HEAD_Y  236   /**< the heading                            */
#define UPD_DEV_LINE_Y  260   /**< the summary line                       */
#define UPD_DEV_BTN_Y   316   /**< the button's top: one pitch over the
                                   IOmcu's hold                           */
#define UPD_DEV_BTN_H   56    /**< the button's and the holds' height     */
/** @} */

/** @name The page, in UPDATES' body
 *  @{ */
#define UPD_DEVP_Y0     24    /**< the first row's top                    */
#define UPD_DEVP_PITCH  104   /**< from one row's top to the next's       */
#define UPD_DEVP_TEXT_X 16    /**< the heading's and the line's left edge */
#define UPD_DEVP_TEXT_W 496   /**< their width                            */
#define UPD_DEVP_LINE_DY 28   /**< the line under the heading             */
#define UPD_DEVP_HOLD_X 528   /**< the holds' left edge                   */
#define UPD_DEVP_HOLD_W 480   /**< their width                            */
#define UPD_DEVP_NOTE_Y 336   /**< the note's top                         */
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
 * @brief A row's hold label.
 * @param id  the row
 * @param v   the view
 * @param out the label
 * @param n   bytes of @p out
 */
void upd_dev_label(upd_dev_id_t id, const upd_dev_view_t *v, char *out,
                   size_t n);

/**
 * @brief The entry's summary line: the devices that reported a version,
 *        and those whose hold offers another version.
 * @param out the line
 * @param n   bytes of @p out
 */
void upd_dev_summary(char *out, size_t n);

/**
 * @brief The entry's button label: the running update's hold label, or
 *        WIRELESS DEVICE FIRMWARE.
 * @param out the label
 * @param n   bytes of @p out
 */
void upd_dev_entry_label(char *out, size_t n);

/**
 * @brief A row's hold on the page, in UPDATES' body.
 * @param id the row
 * @return the rectangle
 */
gfx_rect_t upd_dev_rect(upd_dev_id_t id);

/**
 * @brief The entry's button on UPDATES, in UPDATES' body.
 * @return the rectangle
 */
gfx_rect_t upd_dev_entry_rect(void);

/**
 * @brief The page's CANCEL UPDATE, in UPDATES' body: the footer's middle
 *        slot, shown while an update runs.
 * @return the rectangle
 */
gfx_rect_t upd_dev_cancel_rect(void);

/** @brief Whether a row's update runs: CANCEL UPDATE stands.
 *  @return true while one row reads DEVUPD_ROW_UPDATING */
bool upd_dev_running(void);

/**
 * @brief Whether CANCEL UPDATE was tapped while it was live, once.
 * @return true once after the tap
 */
bool upd_dev_take_cancel(void);

/**
 * @brief The page's BACK, in UPDATES' body: the footer's right slot.
 * @return the rectangle
 */
gfx_rect_t upd_dev_back_rect(void);

/** @brief Whether the page is open. @return true while it is */
bool upd_dev_shown(void);

/** @brief Open the page.  A repaint follows. */
void upd_dev_open(void);

/** @brief Close the page; every press on it ends unfired. */
void upd_dev_close(void);

/**
 * @brief A touch event on UPDATES, offered to this module first.  With the
 *        page closed only the entry's button takes one; with it open every
 *        event is the page's.
 * @param evt   the event, body coordinates
 * @param other whether another control of UPDATES holds a press: then a
 *              DOWN on the entry is not taken
 * @return true when the module took the event
 */
bool upd_dev_event(const touch_event_t *evt, bool other);

/** @brief Whether a press is on the entry, a row or BACK.
 *  @return true while one is */
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
 * @brief Draw the entry on UPDATES.
 * @param c canvas, UPDATES' body
 */
void upd_dev_render(gfx_canvas_t *c);

/**
 * @brief Draw the page, whole.
 * @param c canvas, UPDATES' body
 */
void upd_dev_render_page(gfx_canvas_t *c);

/** @brief Whether a hold's flash runs. @return true while one does */
bool upd_dev_flashing(void);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_UPD_DEV_H */
