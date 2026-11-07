/**
 * @file settings_draw.c
 * @brief SETUP's drawing: the hub's tiles and SENSORS' left list; a
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
 * @brief SENSORS' left list: each entry's name and its state under it, the
 *        selected entry lit, a pressed one with the accent edge.
 * @param c The canvas.
 */
static void draw_entries(gfx_canvas_t *c)
{
    for (int e = 0; e < SENS_ENTRY_COUNT; ++e) {
        const gfx_rect_t r = settings_screen_entry_rect(e);
        const bool sel = e == sensors_pane_selected();
        const bool down = s.sl.hit_kind == HIT_ENTRY && s.sl.hit_index == e;
        const int cy = r.y + ENTRY_INSET, ch = r.h - 2 * ENTRY_INSET;
        gfx_fill_chamfer_rect_ex(c, r.x, cy, r.w, ch, 8, 0, 8, 0,
                                 sel ? UI_PANEL_HI : UI_PANEL);
        gfx_draw_chamfer_rect_ex(c, r.x, cy, r.w, ch, 8, 0, 8, 0,
                                 (sel || down) ? UI_ACCENT : UI_EDGE);
        char name[24], state[48];
        gfx_color_t ink = UI_TEXT_DIM;
        (void)sensors_pane_name(e, name, sizeof(name));
        (void)sensors_pane_state(e, state, sizeof(state), &ink);
        gfx_text(c, r.x + 12, r.y + ENTRY_NAME_DY, name, STG_F_BODY,
                 sel ? UI_ACCENT : UI_TEXT, 1);
        gfx_text(c, r.x + 12, r.y + ENTRY_STATE_DY, state, &gfx_font_8x16, ink,
                 1);
    }
}

/**
 * @brief Draws the list of the open category: on SENSORS its left list, then
 *        the rows clipped to the pane, the category's name and RESET
 *        (ui_setlist.h).
 * @param c The canvas.
 */
static void draw_list(gfx_canvas_t *c)
{
    if (s.cat == SET_CAT_SENSORS) {
        draw_entries(c);
    }
    ui_setlist_draw_rows(&s.sl, c);
    ui_setlist_draw_strip(&s.sl, c);
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
    }
    if (!stale) {
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
    }
    ui_textkey_render(&sl->tk, c);

    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        s.chrome_valid[buffer_index] = true;
        s.drawn_cat = s.cat;
        s.drawn_list = sl->open;
        s.drawn_warn = sl->warn;
    }
}
