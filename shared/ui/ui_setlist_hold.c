/**
 * @file
 * @brief The settings list's hold rows (ui_setlist.h): a custom row with
 *        held() draws a 2s hold button at its right; a press on it fills
 *        the button for UI_HOLD_S and calls held() once, and the button
 *        flashes as it fires.  ui_setlist.c routes the touches here.
 *        And the end of a press or a choice list whose row the host's
 *        rows moved (sl_layout_follow()).
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_text.h"
#include "ui_setlist.h"

#include "ui_theme.h"
#include "ui_widgets.h"

#include "ui_setlist_int.h"

/**
 * @brief The hooks of row @p index when it is a hold row.
 * @param sl    The list.
 * @param index The row.
 * @return The hooks; NULL for any other row.
 */
static const ui_setlist_custom_t *hold_row(const ui_setlist_t *sl, int index)
{
    const ui_setlist_row_t r = sl_row(sl, index);
    if (r.kind != UI_SL_CUSTOM || r.custom == NULL
        || r.custom->held == NULL) {
        return NULL;
    }
    return r.custom;
}

/**
 * @brief Whether row @p index is a hold row that takes a press now.
 * @param sl    The list.
 * @param index The row.
 * @return True when it is one and nothing locks it.
 */
static bool hold_live(const ui_setlist_t *sl, int index)
{
    const ui_setlist_custom_t *cu = hold_row(sl, index);
    return cu != NULL && (cu->locked == NULL || cu->locked() == NULL);
}

bool sl_hold_on(const ui_setlist_t *sl, int index, int x, int y)
{
    return hold_live(sl, index)
           && gfx_rect_contains(ui_setlist_row_part(sl, index,
                                                    UI_SL_PART_HOLD), x, y);
}

void sl_hold_press(ui_setlist_t *sl, int index)
{
    const ui_setlist_custom_t *cu = hold_row(sl, index);
    if (cu != NULL) {
        ui_hold_begin(&sl->hold, (int)cu->hold_hint);
        sl->hold_row = index;
    }
}

void sl_hold_tick(ui_setlist_t *sl, float dt_s)
{
    if (sl->hold.down || ui_hold_flashing(&sl->hold)) {
        /* The fill follows the hold, the flash its end: the button alone
         * where the host repaints parts, else the whole list. */
        ++sl->hold_rev;
        if (!sl->host->parts && sl->host->invalidate != NULL) {
            sl->host->invalidate();
        }
    }
    ui_hold_flash_step(&sl->hold, dt_s);
    if (sl->hit_kind != UI_SL_HIT_HOLD) {
        return;
    }
    if (!sl->open || !hold_live(sl, sl->hit_index)) {
        /* The row went or locked under the finger: the press ends. */
        (void)ui_hold_left(&sl->hold);
        sl->hit_kind = UI_SL_HIT_NONE;
        sl->hit_index = -1;
        return;
    }
    const ui_setlist_custom_t *cu = hold_row(sl, sl->hit_index);
    if (cu != NULL && ui_hold_tick(&sl->hold, dt_s)) {
        cu->held();
        ui_hold_reached(&sl->hold);
    }
}

/**
 * @brief The button drawn with @p hold's fill and flash.
 * @param c     The canvas.
 * @param r     The button.
 * @param label Its label.
 * @param hold  The hold it shows.
 * @param down  A press holds it.
 * @param live  It takes a press.
 */
static void hold_button(gfx_canvas_t *c, gfx_rect_t r, const char *label,
                        ui_hold_t *hold, bool down, bool live)
{
    ui_hold_button(c, r, label, UI_PANEL_HI, UI_OK, hold, down, live,
                   SL_F_LABEL);
}

void sl_hold_draw(ui_setlist_t *sl, gfx_canvas_t *c, int index,
                  const char *label, bool live)
{
    const bool down = sl->hit_kind == UI_SL_HIT_HOLD
                      && sl->hit_index == index;
    const gfx_rect_t r = ui_setlist_row_part(sl, index, UI_SL_PART_HOLD);
    /* The hold's fill and flash belong to the row it took; another hold
     * row draws idle. */
    if (sl->hold_row == index) {
        hold_button(c, r, label, &sl->hold, down, live);
    } else {
        ui_hold_t idle;
        ui_hold_reset(&idle);
        hold_button(c, r, label, &idle, down, live);
    }
}

/* ------------------------------------------------- rows that move */

uint32_t sl_layout_key(const ui_setlist_t *sl)
{
    const int n = ui_setlist_count(sl);
    uint32_t h = 2166136261u;
    h = (h ^ (uint32_t)n) * 16777619u;
    for (int i = 0; i < n; ++i) {
        const ui_setlist_row_t r = sl_row(sl, i);
        const uint32_t parts[5] = {
            r.kind, (uint32_t)(uint8_t)r.text, (uint32_t)r.id,
            (uint32_t)r.heading, (uint32_t)(uintptr_t)r.custom,
        };
        for (size_t k = 0; k < sizeof(parts) / sizeof(parts[0]); ++k) {
            h = (h ^ parts[k]) * 16777619u;
        }
    }
    return h;
}

void sl_layout_follow(ui_setlist_t *sl)
{
    if ((!sl->have_press && sl->choice_row < 0)
        || sl_layout_key(sl) == sl->layout) {
        return;
    }
    sl_drop_press(sl);
    sl->choice_row = -1;
    if (sl->host->invalidate != NULL) {
        sl->host->invalidate();
    }
}
