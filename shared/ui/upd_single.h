/**
 * @file upd_single.h
 * @brief SINGLE UPDATES, the page of UPDATES that OPTIONS' row Single
 *        updates opens (upd_settings.c): one update or reinstall at a
 *        time, outside the order of UPDATES.  Private to upd_screen.c,
 *        upd_settings.c and help_upd.c.
 *
 * The page covers UPDATES' body at the coordinates upd_screen_int.h names:
 * its heading in the card's place (x 484 to 1016, y 16 to 336); the IOmcu
 * section in the left column from y 88 and HOLD 2s TO UPDATE THE IOMCU,
 * 460 x 56px at y 380; the WIRELESS DEVICES entry from y 236 (upd_dev.h),
 * whose button opens the devices' page over this one; the C6 RADIO row
 * under the heading from y 348 (upd_c6.h); the footer's name and BACK in
 * the footer's right slot.  BACK and the home tag close the page; leaving
 * UPDATES closes it, and every hold on it ends unfired.  A check or a
 * download that starts while the page shows closes nothing.
 *
 * The IOmcu hold is live only while coproc_update_section() reads
 * CU_SEC_READY; a tap or a hold on it while it is grey says why on the
 * band (upd_cu_text()).  One finger at a time across the IOmcu hold, the
 * C6 RADIO row, the devices' entry and BACK.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "touch_types.h"

/** @brief Whether the page shows. @return True while it does. */
bool upd_single_shown(void);

/** @brief Show the page, no press on it. */
void upd_single_open(void);

/** @brief Close the page: its holds end unfired, the devices' page closes. */
void upd_single_close(void);

/** @brief The page closed, the IOmcu section's view and its latch cleared:
 *         UPDATES' reset. */
void upd_single_reset(void);

/**
 * @brief A touch event while the page shows: the devices' page's when it
 *        is open, else the C6 RADIO row's, the devices' entry's, the IOmcu
 *        hold's or BACK's.
 * @param evt The event, body coordinates.
 */
void upd_single_event(const touch_event_t *evt);

/** @brief Touch events were lost: every press on the page ends unfired. */
void upd_single_cancel(void);

/**
 * @brief Time passed for the IOmcu hold, the C6 RADIO row's and the
 *        devices' holds.
 * @param dt_s The time, s.
 * @return True while a fill or a flash changes the page.
 */
bool upd_single_tick(float dt_s);

/**
 * @brief Draw the page whole, or the devices' page while it is open.
 * @param c Canvas, the body.
 * @return True while a hold's flash runs: draw it again next frame.
 */
bool upd_single_render(gfx_canvas_t *c);

/** @brief Set the latch the IOmcu hold sets: UPDATES' IOMCU step's hold
 *         completed.  upd_screen_take_coproc() drains it;
 *         coproc_update_drive() reads the section once more before it
 *         starts. */
void upd_single_fire_coproc(void);

/** @brief The page's BACK, in the footer's right slot.
 *  @return Its rectangle in the body. */
gfx_rect_t upd_single_back_rect(void);
