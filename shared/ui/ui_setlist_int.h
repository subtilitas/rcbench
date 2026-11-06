/**
 * @file ui_setlist_int.h
 * @brief The settings list's private seam between ui_setlist.c, its rows,
 *        touch handling and geometry, and ui_setlist_draw.c, its drawing: the
 *        layout both work on and the functions one calls in the other.
 *        Nothing outside shared/ui includes this header; the interface is
 *        ui_setlist.h.
 *
 * Included last: its layout macros have short names (ROW_H, BTN_W).  The
 * functions one file calls in the other carry the prefix sl_.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "ui_footer.h"
#include "ui_layout.h"
#include "ui_setlist.h"

/** @brief The faces: a row's name and a heading's in the head face
 *         (20 x 36) and the body face (12 x 24); a value and the warning's
 *         sentences in the body face; help lines, units, keys, buttons and
 *         the warning's change lines in the label face (10 x 20). */
#define SL_F_HEAD (&gfx_font_20x36)  /**< names: 20 x 36 */
#define SL_F_BODY (&gfx_font_12x24)  /**< values, sentences, headings: 12 x 24 */
#define SL_F_LABEL (&gfx_font_10x20) /**< help, units, keys, buttons: 10 x 20 */

#define SL_M 10                     /**< the margin around the list, px */
#define STRIP_Y UI_FOOTER_Y         /**< the strip's top, px: 444 */
#define STRIP_H UI_FOOTER_H         /**< the strip's height, px: 48 */

#define SCROLL_W 12                 /**< the scroll bar's width, px */
#define SCROLL_X (UI_PANEL_W - SL_M - SCROLL_W) /**< its left edge, px: 1002 */
#define LIST_R UI_SL_LIST_R         /**< the list's right edge, px: 994 */
#define LIST_Y UI_SL_LIST_Y         /**< the list's top, px */
#define LIST_H (STRIP_Y - 8 - LIST_Y) /**< its height, px: 428 */
#define ROW_H UI_SL_ROW_H           /**< a row's height, px */
#define ROW_GAP UI_SL_ROW_GAP       /**< the gap under a row, px */
#define ROW_PITCH (ROW_H + ROW_GAP) /**< a row's pitch, px: 70 */
#define HEAD_H UI_SL_HEAD_H         /**< a heading's height, px */
#define ROW_NAME_DY 3               /**< a row's name, from its top, px */
#define ROW_HELP_DY 40              /**< its help line, px */
#define HEAD_NAME_DY 5              /**< a heading's name, px */

_Static_assert(SCROLL_X - 8 == LIST_R, "the list ends 8px left of the bar");
_Static_assert(LIST_Y + LIST_H == UI_FOOTER_FLOOR,
               "the list ends 8px over the strip");

/**
 * @brief A row's "-" and "+" keys: 56 x 48px (8.4 x 7.2mm), the "+" 12px in
 *        from the row's right edge, 166px between them: the value's 150px
 *        and 8px on each side.
 */
#define BTN_W 56                    /**< a key's width, px */
#define BTN_H UI_TAP_MIN            /**< its height, px: 48 */
_Static_assert(BTN_H <= ROW_H, "a row's keys fit in the row");
#define VALUE_FIELD 150             /**< the value's field, px */
#define PLUS_X (LIST_R - 12 - BTN_W) /**< the "+" key's left edge, px: 926 */
#define MINUS_X (PLUS_X - 8 - VALUE_FIELD - 8 - BTN_W) /**< "-": 704 */
#define VALUE_R (PLUS_X - 8)        /**< the value's right edge, px */
#define VALUE_X (MINUS_X + BTN_W)   /**< the keypad's tap area's left, px */
#define VALUE_W (PLUS_X - VALUE_X)  /**< its width, px: 166 */

/** @name The key repeat: 0.45s, then 8Hz, 30Hz from 1.8s later. */
/** @{ */
#define REPEAT_DELAY_S 0.45f        /**< delay before the first repeat, s */
#define REPEAT_SLOW_HZ 8.0f         /**< repeat rate after the delay, Hz */
#define REPEAT_FAST_S 1.8f          /**< from the first repeat to fast, s */
#define REPEAT_FAST_HZ 30.0f        /**< repeat rate once fast, Hz */
/** @} */

/** @brief A press that moves further than this is a scroll, px: 8. */
#define DRAG_SLOP 8

/**
 * @brief The warning: a card over the body down to the strip.  Its title
 *        in the head face, up to two lines in the body face on what unsaved
 *        means, a line per change in the label face and up to two notes in
 *        the body face.  Offsets from WARN_Y.
 */
#define WARN_X SL_M                 /**< the card's left edge, px */
#define WARN_Y SL_M                 /**< its top, px */
#define WARN_W (UI_PANEL_W - 2 * SL_M) /**< its width, px: 1004 */
#define WARN_H (STRIP_Y - SL_M - WARN_Y) /**< its height, px: 424 */
#define WARN_TITLE_Y 12             /**< the title's top */
#define WARN_WHY_Y 56               /**< the first line on unsaved */
#define WARN_WHY_PITCH 26           /**< those lines' pitch, px */
#define WARN_LINE_Y 116             /**< the first change line */
#define WARN_PITCH 24               /**< the change lines' pitch, px */
/** @brief The first note. */
#define WARN_NOTE_Y (WARN_LINE_Y + UI_SL_WARN_LINES * WARN_PITCH + 4)
#define WARN_NOTE_PITCH 26          /**< the notes' pitch, px */
_Static_assert(WARN_WHY_Y + 2 * WARN_WHY_PITCH <= WARN_LINE_Y,
               "the warning's two lines on unsaved end above the changes");
_Static_assert(WARN_NOTE_Y + 2 * WARN_NOTE_PITCH <= WARN_H - 4,
               "the warning's notes end inside its card");

/** @brief The keyboard's and the keypad's area: the warning's card down to
 *         the strip's foot, 1004 x 482px. */
#define TK_AREA gfx_rect_make(WARN_X, WARN_Y, WARN_W, STRIP_Y + STRIP_H - WARN_Y)

/** @brief A choice list's box: 400px wide at the list's right edge. */
#define CHOICE_W 400
/** @brief A choice row's height, px: UI_CHOICE_ROW_H, 56. */
#define CHOICE_H 56

/* --- ui_setlist.c: what the drawing reads ------------------------------- */

/**
 * @brief The list's left edge.
 * @param sl The list.
 * @return The edge, px.
 */
int sl_left(const ui_setlist_t *sl);

/**
 * @brief Row @p i.
 * @param sl The list.
 * @param i  The row.
 * @return The row.
 */
ui_setlist_row_t sl_row(const ui_setlist_t *sl, int i);

/**
 * @brief A setting's help line, from the host or the schema.
 * @param sl The list.
 * @param id The setting.
 * @return The line.
 */
const char *sl_help(const ui_setlist_t *sl, setting_id_t id);

/**
 * @brief Whether the IOmcu does not hold a setting, from the host.
 * @param sl The list.
 * @param id The setting.
 * @return True when it does not.
 */
bool sl_unheld(const ui_setlist_t *sl, setting_id_t id);

/**
 * @brief Why a setting's row takes no press, from the host.
 * @param sl The list.
 * @param id The setting.
 * @return The reason, NULL for none.
 */
const char *sl_locked(const ui_setlist_t *sl, setting_id_t id);

/**
 * @brief Whether the bench is ARMED: DISCARD changes nothing then.
 * @return True when ARMED.
 */
bool sl_bench_armed(void);

/**
 * @brief The rows' pitches added up, px: the scroll bar's content; one
 *        row's pitch for an empty list.
 * @param sl The list.
 * @return The sum.
 */
int sl_pitch_sum(const ui_setlist_t *sl);

/**
 * @brief A string's help line, "" for none.
 * @param id The string.
 * @return The line.
 */
const char *sl_text_help(setting_text_id_t id);

/**
 * @brief The choice list's box, while one is open.
 * @param sl The list.
 * @return The box.
 */
gfx_rect_t sl_choice_box(const ui_setlist_t *sl);
