/**
 * @file upd_wiz_view.h
 * @brief UPDATES' steps: the step list, the current step's card and its
 *        one control, SKIP THIS STEP, SHOW SKIPPED STEPS and CANCEL UPDATE.
 *        Private to upd_screen.c, upd_wiz_card.c and help_upd.c.
 *
 * The body under the band, 1024 x 540px: CHECK FOR UPDATES (upd_screen.c)
 * 460 x 56px at x 16, y 16; under it the six step rows, 460 x 48px from
 * y 88, 56px apart, each a number box 48 x 48px, the step's name, the
 * version its target runs and its tag right-aligned; the rows take no
 * press.  The card on the right, x 484 to 1016, y 16 to 436: the current
 * step's heading, versions, what it does, why it waits and how its last
 * attempt ended (upd_wiz_card.c); its control 500 x 64px at y 292, SKIP
 * THIS STEP or SHOW SKIPPED STEPS 500 x 56px at y 364.  The footer:
 * UPDATES, CANCEL UPDATE in the middle slot while a device's transfer
 * runs, and OPTIONS (upd_screen.c).
 *
 * upd_wizard_eval() decides the steps every time from the views the
 * application hands UPDATES (upd_screen_set(), upd_screen_set_coproc(),
 * upd_c6_set(), upd_dev_set()), the run flag (upd_screen_set_run()), the
 * steps skipped since the panel's start and each step's last failed
 * attempt (upd_screen_set_result()).  No step position is kept.
 *
 * The control's 2s hold sets the latch the step's own control on SINGLE
 * UPDATES sets: UPD_REQ_DOWNLOAD, the IOmcu's, the C6's or a device's;
 * the services drain them as before.  A hold whose step or control
 * changes under it, or that its gate closes on, ends unfired.  A press on
 * a grey control says why on the band.  One finger at a time across the
 * card's controls, CANCEL UPDATE, CHECK and OPTIONS.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "gfx.h"
#include "touch_types.h"
#include "upd_wizard.h"

/** @name The step list, in the body
 *  @{ */
#define UPD_WIZ_ROW_X     16   /**< the rows' left edge: CHECK's          */
#define UPD_WIZ_ROW_W     460  /**< their width: CHECK's                   */
#define UPD_WIZ_ROW_Y0    88   /**< the first row's top                    */
#define UPD_WIZ_ROW_H     48   /**< a row's height; rows take no press     */
#define UPD_WIZ_ROW_PITCH 56   /**< from one row's top to the next's      */
#define UPD_WIZ_BOX       48   /**< the number box, square                 */
#define UPD_WIZ_NAME_X    72   /**< the name's left edge                   */
#define UPD_WIZ_NAME_W    150  /**< its width: 15 cells of the 10 x 20 face */
#define UPD_WIZ_VER_X     230  /**< the version's left edge                */
#define UPD_WIZ_VER_W     80   /**< its width: 8 cells                     */
#define UPD_WIZ_TAG_W     150  /**< the tag's width, right-aligned 8px in */
/** @} */

/** @name The card and its controls, in the body
 *  @{ */
#define UPD_WIZ_CARD_X  484  /**< the card's left edge                     */
#define UPD_WIZ_CARD_Y  16   /**< its top: CHECK's                         */
#define UPD_WIZ_CARD_W  532  /**< its width                                */
#define UPD_WIZ_CARD_H  420  /**< its height: its foot at y 436            */
#define UPD_WIZ_PAD     16   /**< its inner padding                        */
#define UPD_WIZ_ACT_Y   292  /**< the control's top                        */
#define UPD_WIZ_ACT_H   64   /**< its height                               */
#define UPD_WIZ_SKIP_Y  364  /**< SKIP THIS STEP's top                     */
#define UPD_WIZ_SKIP_H  56   /**< its height                               */
/** @} */

/** @brief The steps now, from the views UPDATES holds.
 *  @param out the answer */
void upd_wiz_now(upd_wiz_t *out);

/** @brief The skip mask, the run flag, the results and every press
 *         cleared: the panel's start (UPDATES' reset). */
void upd_wiz_reset(void);

/** @brief Every press on the steps' controls ends unfired: UPDATES'
 *         enter, leave and cancel. */
void upd_wiz_drop(void);

/** @brief A hold under way on step @p step's control ends unfired; a
 *         press on another step's control goes on.
 *  @param step an upd_wiz_id_t */
void upd_wiz_drop_step(unsigned step);

/** @brief Step @p step is no longer skipped: a new IOmcu offer brings
 *         its step back.
 *  @param step an upd_wiz_id_t; another value does nothing */
void upd_wiz_unskip(unsigned step);

/**
 * @brief A touch event on UPDATES, offered to the card's controls and
 *        CANCEL UPDATE first.
 * @param evt   the event, body coordinates
 * @param other another control of UPDATES holds a press: a DOWN is not
 *              taken
 * @return true when the event was the steps'
 */
bool upd_wiz_event(const touch_event_t *evt, bool other);

/** @brief Whether a press is on a control of the steps.
 *  @return true while one is */
bool upd_wiz_pressed(void);

/** @brief Whether a finger is on the control's live hold.
 *  @return true while one is */
bool upd_wiz_holding(void);

/** @brief Whether a step's update runs: OPTIONS is grey then.
 *  @return true while one does */
bool upd_wiz_running(void);

/**
 * @brief Time passed: the hold's fill, its flash, and its firing.  A hold
 *        whose control the steps no longer offer ends unfired first.
 * @param dt_s the time, s
 * @return true while the fill or the flash changes the screen
 */
bool upd_wiz_tick(float dt_s);

/**
 * @brief Draw the step list, the card, its controls and the footer's
 *        middle slot.
 * @param c canvas, the body
 * @return true while the hold's flash runs: draw again next frame
 */
bool upd_wiz_render(gfx_canvas_t *c);

/** @brief A step's row. @param i the step @return its rectangle */
gfx_rect_t upd_wiz_row_rect(unsigned i);

/** @brief The card. @return its rectangle */
gfx_rect_t upd_wiz_card_rect(void);

/** @brief The control: the current step's hold, or the button that opens
 *         WI-FI on the last card. @return its rectangle */
gfx_rect_t upd_wiz_act_rect(void);

/** @brief SKIP THIS STEP, and SHOW SKIPPED STEPS. @return its rectangle */
gfx_rect_t upd_wiz_skip_rect(void);

/** @brief Whether the last card shows the button that opens WI-FI: no
 *         step is current, Wi-Fi was set up and no home network is kept.
 *  @param w the steps
 *  @return true then */
bool upd_wiz_net_shown(const upd_wiz_t *w);

/**
 * @brief A step's name, as its row and the card's heading show it.
 * @param i the step
 * @return the text
 */
const char *upd_wiz_name(unsigned i);

/**
 * @brief The control's label: the hold's, or a running update's phase.
 * @param w   the steps
 * @param out the label
 * @param n   bytes of @p out
 */
void upd_wiz_act_label(const upd_wiz_t *w, char *out, size_t n);

/**
 * @brief Why the current step waits, one line for the card and the band.
 * @param w the steps
 * @param out the line, empty for none
 * @param n   bytes of @p out
 */
void upd_wiz_why_text(const upd_wiz_t *w, char *out, size_t n);

/**
 * @brief The card's paragraphs for @p w, as its next draw shows them:
 *        those that do not fit already left out (upd_wiz_card.c).
 * @param w the steps
 * @return how many
 */
int upd_wiz_card_build(const upd_wiz_t *w);

/**
 * @brief A paragraph of the last upd_wiz_card_build().
 * @param i the paragraph, from 0
 * @return its text, or NULL past the last
 */
const char *upd_wiz_card_para(int i);

/**
 * @brief Draw the card: the current step's or the last one
 *        (upd_wiz_card.c).
 * @param c canvas, the body
 * @param w the steps
 */
void upd_wiz_card_render(gfx_canvas_t *c, const upd_wiz_t *w);
