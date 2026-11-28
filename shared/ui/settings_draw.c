/**
 * @file settings_draw.c
 * @brief SETUP's drawing: the hub's tiles and a split pane's left list; a
 *        category's rows, the SAVE strip and the warning that holds the way
 *        out are the settings list's (ui_setlist.h).
 *        settings_screen.c holds the state, the touch handling and the
 *        screen's interface (settings_screen.h); settings_screen_int.h is what
 *        the two share.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "sensors_pane.h"
#include "settings.h"
#include "settings_rows_int.h"
#include "settings_screen.h"
#include "ui_icons.h"
#include "ui_keypad.h"
#include "ui_setlist.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_widgets.h"

#include "settings_screen_int.h"

/* ------------------------------------------------------------------ render */

/** @brief A hub tile's look: its icon, name and line (settings_screen_int.h). */
static const ui_tile_look_t k_hub_look = {
    .icon_dy = HUB_ICON_DY, .icon = HUB_ICON, .name_dy = HUB_NAME_DY,
    .line_dy = HUB_LINE_DY,
    .line_pitch = 0,   /* no second row: the tile has no room for one */
    .name_f = STG_F_BODY, .line_f = STG_F_LABEL,
};

/** @brief Draws the hub: the tiles in a grid, a tile lit while pressed. */
static void draw_hub(gfx_canvas_t *c)
{
    for (int i = 0; i < stg_tile_count(); ++i) {
        const hub_tile_t *t = stg_hub_tile(i);
        const bool down = (s.sl.hit_kind == HIT_TILE && s.sl.hit_index == i);
        if (t->cat < 0) {
            ui_tile(c, stg_tile_rect(i), &k_hub_look, t->icon,
                    ui_router_title(t->screen), ui_tr(t->line), down);
            continue;
        }
        ui_tile(c, stg_tile_rect(i), &k_hub_look, t->icon,
                ui_setting_category((setting_cat_t)t->cat), ui_tr(t->line),
                down);
    }
}

/**
 * @brief A split pane's left list, SENSORS' or WIRELESS': each entry's name
 *        and its state under it, the selected entry lit, a pressed one with
 *        the accent edge.
 * @param c    The canvas.
 * @param pane The pane (stg_pane()).
 */
static void draw_entries(gfx_canvas_t *c, int pane)
{
    for (int e = 0; e < stg_pane_count(pane); ++e) {
        const gfx_rect_t r = settings_screen_entry_rect(e);
        const bool sel = e == stg_pane_selected(pane);
        const bool down = s.sl.hit_kind == HIT_ENTRY && s.sl.hit_index == e;
        const int cy = r.y + ENTRY_INSET, ch = r.h - 2 * ENTRY_INSET;
        gfx_fill_chamfer_rect_ex(c, r.x, cy, r.w, ch, 8, 0, 8, 0,
                                 sel ? UI_PANEL_HI : UI_PANEL);
        gfx_draw_chamfer_rect_ex(c, r.x, cy, r.w, ch, 8, 0, 8, 0,
                                 (sel || down) ? UI_ACCENT : UI_EDGE);
        char name[24], state[48];
        gfx_color_t ink = UI_TEXT_DIM;
        (void)stg_pane_name(pane, e, name, sizeof(name));
        (void)stg_pane_state(pane, e, state, sizeof(state), &ink);
        gfx_text(c, r.x + 12, r.y + ENTRY_NAME_DY, name, STG_F_BODY,
                 sel ? UI_ACCENT : UI_TEXT, 1);
        gfx_text(c, r.x + 12, r.y + ENTRY_STATE_DY, state, &gfx_font_8x16, ink,
                 1);
    }
}

/**
 * @brief Draws the list of the open category: on a split pane its left
 *        list, then the rows clipped to the pane, the category's name and
 *        RESET (ui_setlist.h).
 * @param c The canvas.
 */
static void draw_list(gfx_canvas_t *c)
{
    const int pane = stg_pane(s.cat);
    if (pane >= 0) {
        draw_entries(c, pane);
    }
    ui_setlist_draw_rows(&s.sl, c);
    ui_setlist_draw_strip(&s.sl, c);
}

/**
 * @brief Row @p i's live value text, hashed (FNV-1a).
 * @param i The row.
 * @return The hash; 0 for a row that is no live row.
 */
static uint32_t live_key(int i)
{
    const settings_row_t e = stg_row_entry(i);
    if (e.custom == NULL || !e.custom->live || e.custom->value_text == NULL) {
        return 0u;
    }
    char text[48] = "";
    e.custom->value_text(text, sizeof(text));
    uint32_t h = 2166136261u;
    for (const char *p = text; *p != '\0'; ++p) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    return h | 1u;
}

/**
 * @brief What changed since buffer @p b was drawn and nothing else: a hold
 *        row's button while its fill or flash moves, and each live row's
 *        value that reads differently.
 * @param c The canvas.
 * @param b The framebuffer.
 */
static void draw_parts(gfx_canvas_t *c, int b)
{
    ui_setlist_t *sl = &s.sl;
    if (s.drawn_hold[b] != sl->hold_rev) {
        s.drawn_hold[b] = sl->hold_rev;
        if (sl->hold_row >= 0 && sl->hold_row < stg_row_count()) {
            ui_setlist_draw_part(sl, c, sl->hold_row,
                                 ui_setlist_row_part(sl, sl->hold_row,
                                                     UI_SL_PART_HOLD));
        }
    }
    if (s.drawn_live[b] == s.live_rev) {
        return;
    }
    s.drawn_live[b] = s.live_rev;
    const int n = stg_row_count();
    for (int i = 0; i < n && i < STG_LIVE_ROWS; ++i) {
        const uint32_t k = live_key(i);
        if (k != s.live_key[b][i]) {
            s.live_key[b][i] = k;
            ui_setlist_draw_part(sl, c, i, ui_setlist_value_rect(sl, i));
        }
    }
}

/**
 * @brief After a whole draw of buffer @p b: the parts as it drew them.
 * @param b The framebuffer.
 */
static void parts_drawn(int b)
{
    s.drawn_hold[b] = s.sl.hold_rev;
    s.drawn_live[b] = s.live_rev;
    const int n = stg_row_count();
    for (int i = 0; i < STG_LIVE_ROWS; ++i) {
        s.live_key[b][i] = (s.sl.open && i < n) ? live_key(i) : 0u;
    }
}

void stg_render(gfx_canvas_t *c, int buffer_index)
{
    /* The keyboard and the keypad move on their own events: every change
     * bumps a revision, and a buffer drawn under another one is stale. */
    ui_setlist_t *sl = &s.sl;
    if (sl->tk.revision != sl->drawn_tk_rev || sl->tk.open != sl->drawn_tk) {
        settings_screen_invalidate();
        sl->drawn_tk_rev = sl->tk.revision;
        sl->drawn_tk = sl->tk.open;
    }
    if (sl->kp.revision != sl->drawn_kp_rev || sl->kp.open != sl->drawn_kp) {
        settings_screen_invalidate();
        sl->drawn_kp_rev = sl->kp.revision;
        sl->drawn_kp = sl->kp.open;
    }
    bool stale = true;
    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        stale = !s.chrome_valid[buffer_index] || s.drawn_cat != s.cat
                || s.drawn_list != sl->open || s.drawn_warn != sl->warn;
        /* A part under a covering keypad, keyboard, warning or choice
         * list repaints with the whole. */
        const bool covered = sl->warn || sl->kp.open || sl->tk.open
                             || sl->choice_row >= 0 || !sl->open;
        if (!stale && covered
            && (s.drawn_live[buffer_index] != s.live_rev
                || s.drawn_hold[buffer_index] != sl->hold_rev)) {
            stale = true;
        }
    }
    if (!stale) {
        draw_parts(c, buffer_index);
        return;
    }

    /* A list draws a SAFE-only row without keys while ARMED
     * (settings_safe_only()), so an arm or a disarm redraws it. */
    sl->drawn_armed = stg_bench_armed();
    gfx_clear(c, UI_BG);
    if (sl->kp.open) {
        /* The keypad covers the list and the strip: nothing under it is
         * drawn, as nothing under it presses. */
        ui_keypad_render(&sl->kp, c);
    } else if (sl->warn) {
        ui_setlist_draw_warning(sl, c);
    } else {
        if (sl->open) {
            draw_list(c);
        } else {
            draw_hub(c);
        }
        ui_setlist_draw_save(sl, c);
        if (sl->open) {
            /* Over the rows, as ui_setlist_render() draws it. */
            ui_setlist_draw_choices(sl, c);
        }
    }
    ui_textkey_render(&sl->tk, c);

    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        parts_drawn(buffer_index);
        s.chrome_valid[buffer_index] = true;
        s.drawn_cat = s.cat;
        s.drawn_list = sl->open;
        s.drawn_warn = sl->warn;
    }
}
