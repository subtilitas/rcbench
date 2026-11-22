/**
 * @file
 * @brief SETUP > SD CARD's rows SAVE SETTINGS and LOAD SETTINGS.  See
 *        sbk_screen.h.
 *
 * SAVE SETTINGS writes the stored settings, so it takes no tap while an
 * edit is not saved: the file would not hold what the list shows.  LOAD
 * SETTINGS opens SCREEN_SET_LOAD; the hold there is what replaces the
 * settings.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "sbk_ctl.h"
#include "sbk_screen.h"
#include "settings.h"
#include "ui_text.h"

/** @brief Why SAVE SETTINGS takes no tap now; NULL while it takes one. */
static const char *save_locked(void)
{
    if (!sbk_mounted()) {
        return TR(SBK_LOCK_NO_CARD);
    }
    return settings_dirty() ? TR(SBK_LOCK_UNSAVED) : NULL;
}

/** @brief SAVE SETTINGS' value: the last write's outcome. */
static void save_value(char *out, size_t n)
{
    switch (sbk_save_state()) {
    case SBK_SAVE_WRITING:
        (void)snprintf(out, n, "%s", TR(SBK_V_WRITING));
        break;
    case SBK_SAVE_WRITTEN:
        (void)snprintf(out, n, "%s", TR(SBK_V_WRITTEN));
        break;
    case SBK_SAVE_FAILED:
        if (sbk_save_code() == SB_FULL) {
            (void)snprintf(out, n, "%s", TR(SBK_V_FULL));
        } else {
            (void)snprintf(out, n, TR(SBK_V_FAILED),
                           (unsigned)sbk_save_code());
        }
        break;
    case SBK_SAVE_IDLE:
    default:
        out[0] = '\0';
        break;
    }
}

/** @brief SAVE SETTINGS tapped. */
static void save_tap(void)
{
    if (save_locked() == NULL && sbk_save_state() != SBK_SAVE_WRITING) {
        sbk_ask_save();
    }
}

/** @brief Why LOAD SETTINGS takes no tap now; NULL while it takes one.
 *         Without a card it opens, and its screen says there is none. */
static const char *load_locked(void)
{
    return sbk_armed() ? TR(SBK_LOCK_ARMED) : NULL;
}

/** @brief LOAD SETTINGS tapped: its screen opens. */
static void load_tap(void)
{
    if (load_locked() == NULL) {
        sbk_screen_open(false);
    }
}

/** @brief SAVE SETTINGS. */
static const ui_setlist_custom_t k_save = {
    .label = TX_SBK_ROW_SAVE,
    .help = TX_SBK_ROW_SAVE_HELP,
    .value_text = save_value,
    .tap = save_tap,
    .locked = save_locked,
};

/** @brief LOAD SETTINGS. */
static const ui_setlist_custom_t k_load = {
    .label = TX_SBK_ROW_LOAD,
    .help = TX_SBK_ROW_LOAD_HELP,
    .tap = load_tap,
    .locked = load_locked,
};

const ui_setlist_custom_t *sbk_setup_row(int i)
{
    switch (i) {
    case 0:  return &k_save;
    case 1:  return &k_load;
    default: return NULL;
    }
}
