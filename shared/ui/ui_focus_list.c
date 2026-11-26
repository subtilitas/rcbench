/**
 * @file
 * @brief The focus layer's items of a setting list (ui_focus.h): its rows
 *        and strip, the leave warning or the keypad.
 *
 * A row's item and what a SELECT on it does:
 *
 * | Row | SELECT |
 * | --- | --- |
 * | a number the keypad takes | taps the value: the keypad opens, SET |
 * | a setting with "-" and "+" (on/off, options) | step SET |
 * | a string | taps the row: its keyboard opens, and the Dial has no focus until it closes |
 * | a custom row | taps the row's name: its action |
 * | a row that takes no press | taps the value: the panel's answer to a tap there |
 *
 * The strip adds RESET and, while a save is offered, SAVE.  Their slots
 * are kept first: of a list longer than UI_FOCUS_MAX items, the last rows
 * take no focus.
 *
 * An open keyboard or choice list takes no focus: the Dial has none until
 * it closes.  The lists the focus reaches, SETUP's and the SERVO hub's
 * profile, have no row with a choice list.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "ui_focus.h"
#include "ui_setlist.h"
#include "ui_setting_keypad.h"
#include "ui_text.h"

/* Last: its layout macros have short names. */
#include "ui_setlist_int.h"

/** @brief A list's identity per mode, mixed into the host's. @{ */
#define MODE_ROWS   0x10000000u /**< the rows and the strip */
#define MODE_WARN   0x30000000u /**< the leave warning */
#define MODE_KEYPAD 0x40000000u /**< the keypad of a setting */
/** @} */

/**
 * @brief Whether a setting's row has "-" and "+" keys a tap steps.
 * @param sl The list.
 * @param id The setting.
 * @return false for a derived, fixed or locked one.
 */
static bool has_keys(const ui_setlist_t *sl, setting_id_t id)
{
    return !settings_derived(id) && !settings_fixed(id)
           && sl_locked(sl, id) == NULL;
}

/**
 * @brief A row's name, as the list draws it.
 * @param r The row: a setting, a string or a custom row; add_row() leaves
 *          out headings and empty rows.
 * @return The name.
 */
static const char *row_label(const ui_setlist_row_t *r)
{
    if (r->kind == UI_SL_SETTING) {
        return ui_setting_label(r->id);
    }
    if (r->kind == UI_SL_TEXT) {
        return ui_setlist_text_label((setting_text_id_t)r->text);
    }
    return (r->custom != NULL) ? ui_tr(ui_setlist_custom_label(r->custom))
                               : "";
}

/**
 * @brief The leave warning's buttons: STAY, DISCARD, SAVE.
 * @param out The list.
 */
static void add_warning(ui_focus_list_t *out)
{
    out->list = out->list * 31u + MODE_WARN;
    (void)ui_focus_add(out, ui_setlist_strip_rect(0), TR(SET_STAY));
    (void)ui_focus_add(out, ui_setlist_strip_rect(1), TR(SET_DISCARD));
    (void)ui_focus_add(out, ui_setlist_strip_rect(2), TR(SET_SAVE));
}

/**
 * @brief Row @p i's item, if it is not a heading.
 * @param out The list.
 * @param sl  The setting list.
 * @param i   The row.
 */
static void add_row(ui_focus_list_t *out, const ui_setlist_t *sl, int i)
{
    const ui_setlist_row_t r = sl_row(sl, i);
    if (r.kind == UI_SL_HEADING || r.kind == UI_SL_NONE) {
        return;
    }
    const gfx_rect_t rect = ui_setlist_row_rect(sl, i);
    ui_focus_item_t *it = ui_focus_add(out, rect, row_label(&r));
    if (it == NULL) {
        return;
    }
    it->row = (int16_t)i;
    it->clip = ui_setlist_pane(sl);
    if (r.kind == UI_SL_SETTING) {
        const gfx_rect_t v = ui_setlist_row_part(sl, i, UI_SL_PART_VALUE);
        it->tap_x = (int16_t)(v.x + v.w / 2);
        it->tap_y = (int16_t)(v.y + v.h / 2);
        if (!ui_setting_keypad_offered(r.id) && has_keys(sl, r.id)) {
            it->kind = UI_FOCUS_STEP;
            it->id = r.id;
            it->minus = ui_setlist_row_part(sl, i, UI_SL_PART_MINUS);
            it->plus = ui_setlist_row_part(sl, i, UI_SL_PART_PLUS);
        }
        return;
    }
    /* A string's or a custom row's name: left of every key. */
    it->tap_x = (int16_t)(rect.x + 40);
}

bool ui_focus_add_setlist(ui_focus_list_t *out, ui_setlist_t *sl)
{
    if (out == NULL || sl == NULL) {
        return false;
    }
    if (sl->tk.open || sl->choice_row >= 0) {
        return false;          /* the keyboard or a choice list is open */
    }
    if (sl->kp.open) {
        const setting_def_t *d = settings_def(sl->kp_id);
        out->list = out->list * 31u + MODE_KEYPAD + (uint32_t)sl->kp_id;
        out->keypad = &sl->kp;
        out->step = (d != NULL) ? d->step : 0.0f;
        out->off_below = (d != NULL) ? d->off_below : 0.0f;
        return true;
    }
    if (sl->warn) {
        add_warning(out);
        return true;
    }
    if (!sl->open) {
        return true;
    }
    out->list = out->list * 31u + MODE_ROWS;
    out->sl = sl;
    const int first = out->count;
    const int n = ui_setlist_count(sl);
    /* The strip's slots are kept: a list longer than the items hold loses
     * its last rows, never RESET or SAVE. */
    const bool save = ui_setlist_save_offered();
    const int room = UI_FOCUS_MAX - 1 - (save ? 1 : 0);
    for (int i = 0; i < n && out->count < room; ++i) {
        add_row(out, sl, i);
    }
    if (out->count == first) {
        out->start = first;
    }
    char reset[48];
    if (sl->host->reset_label != NULL) {
        sl->host->reset_label(reset, sizeof(reset));
    } else {
        const char *name = (sl->host->name != NULL) ? sl->host->name() : "";
        snprintf(reset, sizeof(reset), TR(SET_RESET_ONE), name);
    }
    (void)ui_focus_add(out, ui_setlist_strip_rect(1), reset);
    if (save) {
        (void)ui_focus_add(out, ui_setlist_strip_rect(2), TR(SET_SAVE));
    }
    return true;
}
