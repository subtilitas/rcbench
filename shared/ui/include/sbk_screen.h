/**
 * @file
 * @brief BACKUP (SCREEN_SET_LOAD), which LOAD SETTINGS opens: the card's
 *        settings files, the one chosen, its date and the panel version
 *        that wrote it, and a 2s hold that puts it in force; and SETUP > SD
 *        CARD's rows SAVE SETTINGS and LOAD SETTINGS that lead to it.
 *
 * The screen opens two ways:
 *
 * - from LOAD SETTINGS on SETUP > SD CARD: the list is read anew, no file
 *   is chosen, BACK and the home tag return to the list of SD CARD;
 * - as the restore offer at start (sbk_ctl.h), over OVERVIEW: the newest
 *   file is chosen, SKIP and the home tag leave for OVERVIEW, and any way
 *   out answers the offer.
 *
 * A tap on a file chooses it.  HOLD 2s TO LOAD takes a press while a file
 * that reads is chosen, the bench is SAFE and no load is under way; a
 * release before 2s shows HOLD_HINT_SBK_LOAD in the alert band.  The load's
 * answer takes the list's place: the file loaded, the values it changed,
 * and each line refused with its line number, key, code and reason.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "gfx.h"
#include "settings_backup.h"
#include "ui_screen.h"
#include "ui_setlist.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The files a list shows: settings_backup.h's SB_FILES. */
#define SBK_ROWS 4

/** @brief The controls of the screen, for sbk_screen_part(). */
typedef enum {
    SBK_PART_HOLD = 0,  /**< HOLD 2s TO LOAD                              */
    SBK_PART_BACK,      /**< BACK, or SKIP while the offer shows          */
    SBK_PART_LIST,      /**< the left column: the files, or the answer    */
} sbk_part_t;

/**
 * @brief The screen's table, for the router.
 * @return The table.
 */
const ui_screen_t *sbk_screen(void);

/**
 * @brief Open the screen: the restore offer with the newest file chosen, or
 *        LOAD SETTINGS with the list read anew.
 * @param offer true for the restore offer.
 */
void sbk_screen_open(bool offer);

/**
 * @brief The driver read the list again: a chosen file whose row no longer
 *        has the same name and date is no longer chosen, and a hold under
 *        way ends without a load.
 * @param before   The list before; NULL drops the choice.
 * @param before_n Its files.
 */
void sbk_screen_relisted(const sb_entry_t *before, unsigned before_n);

/** @brief Draw the screen whole at its next render. */
void sbk_screen_invalidate(void);

/**
 * @brief Where a control lies, in the body's coordinates.
 * @param part The control.
 * @return Its rectangle.
 */
gfx_rect_t sbk_screen_part(sbk_part_t part);

/**
 * @brief Where file row @p i lies, in the body's coordinates.
 * @param i The row, 0 to SBK_ROWS - 1.
 * @return Its rectangle; an empty one at (0, 0) past the last.
 */
gfx_rect_t sbk_screen_row(int i);

/** @brief The file chosen, an index of sbk_files(). @return It; -1 none. */
int sbk_screen_chosen(void);

/** @brief Whether the screen shows the restore offer. @return true when. */
bool sbk_screen_offer(void);

/**
 * @brief Whether HOLD 2s TO LOAD takes a press now.
 * @return true when it does.
 */
bool sbk_screen_hold_ready(void);

/**
 * @brief SETUP > SD CARD's rows after its settings and strings: SAVE
 *        SETTINGS and LOAD SETTINGS.
 * @param i The row, from 0.
 * @return The row; NULL past the last.
 */
const ui_setlist_custom_t *sbk_setup_row(int i);

/** @brief The rows sbk_setup_row() gives. */
#define SBK_SETUP_ROWS 2

#ifdef __cplusplus
}
#endif
