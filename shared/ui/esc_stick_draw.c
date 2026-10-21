/**
 * @file esc_stick_draw.c
 * @brief ESC STICK's pages drawn.
 *
 * ESC STICK's pages drawn: the search field, the makers and the models of
 * the list, the TIMING page, a profile's items, the crumb row, the device
 * card, and the render that picks what to draw.  The state and the seam
 * are esc_stick_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_stick_panel.h"

#include <stdio.h>

#include "esc_profile.h"
#include "settings.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: the layout macros of the headers it includes have short names
 * (W, H, PAD) and s names the state, which no header included after it
 * may meet. */
#include "esc_stick_panel_int.h"

/* ------------------------------------------- the stick class, drawn ----- */

/* The search field: what is searched for, or SEARCH; X clears it while
 * the keyboard is closed. */
static DRAWER void sp_draw_find(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const gfx_rect_t b = t->find_box;
    const bool any = t->find[0] != '\0';
    const bool clr = any && !t->tk.open;
    gfx_fill_round_rect(c, b.x, b.y, b.w, b.h, UI_R_CTL,
                        ui_theme_color(UI_C_PANEL_SUNK));
    gfx_draw_round_rect(c, b.x, b.y, b.w, b.h, UI_R_CTL,
                        t->tk.open ? ui_theme_color(UI_C_ACCENT)
                                   : ui_theme_color(UI_C_EDGE));
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(b.x + 8), b.y,
                                 (int16_t)(b.w - 16 - (clr ? 28 : 0)),
                                 b.h },
                any ? t->find : TR(SP_FIND), UI_FONT_LABEL,
                any ? ui_theme_color(UI_C_TEXT)
                    : ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_LEFT);
    if (clr) {
        ui_button(c, t->find_clr, "X", ui_theme_color(UI_C_PANEL_HI), false,
                  true);
    }
}

/* The red MANUAL tag's width, px. */
static int sp_tag_w(void)
{
    return gfx_text_cells(TR(SP_HAND_TAG)) * 8 + 12;
}

/* The red MANUAL tag, its right edge at @p right; its width, px. */
static int sp_draw_tag(gfx_canvas_t *c, int right, int y)
{
    const char *tag = TR(SP_HAND_TAG);
    const int w = sp_tag_w();
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    gfx_fill_round_rect(c, right - w, y, w, 20, 4, red);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(right - w), (int16_t)y,
                                 (int16_t)w, 20 },
                tag, UI_FONT_LABEL,
                ui_is_light(red) ? ui_theme_color(UI_C_TEXT_ON_LIGHT)
                                 : ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_CENTER);
    return w;
}

/* "55 A" and "25 V" or "25.2 V" for a model's current and voltage; "-"
 * where it states none. */
static void sp_amps(char *out, size_t n, const esc_model_t *m)
{
    if (m->current_a == 0u) {
        snprintf(out, n, "-");
    } else {
        snprintf(out, n, "%u A", (unsigned)m->current_a);
    }
}

static void sp_volts(char *out, size_t n, const esc_model_t *m)
{
    if (m->v_max_mv == 0u) {
        snprintf(out, n, "-");
    } else if (m->v_max_mv % 1000u == 0u) {
        snprintf(out, n, "%u V", (unsigned)(m->v_max_mv / 1000u));
    } else {
        snprintf(out, n, "%u.%u V", (unsigned)(m->v_max_mv / 1000u),
                 (unsigned)(m->v_max_mv % 1000u / 100u));
    }
}

/* The run mark of a narrowed row: filled in the accent, or a ring. */
static void sp_mark_runs(gfx_canvas_t *c, gfx_rect_t r, bool runs)
{
    const int mx = r.x + r.w - 14, my = r.y + r.h / 2;
    if (runs) {
        gfx_fill_circle(c, mx, my, 5, ui_theme_color(UI_C_ACCENT));
    } else {
        gfx_draw_circle(c, mx, my, 5, ui_theme_color(UI_C_TEXT_FAINT));
    }
}

/* A maker's row: its name, the MANUAL tag where a model it lists has
 * manual steps, and its models: those that run of all, or while a search
 * is typed those found of all and those of them that run. */
static DRAWER void sp_draw_maker(gfx_canvas_t *c, gfx_rect_t r,
                                 const sp_maker_t *m, bool dock,
                                 bool searching)
{
    const bool runs = m->runs > 0u;
    ui_card(c, r, runs ? ui_theme_color(UI_C_PANEL)
                       : ui_theme_color(UI_C_PANEL_SUNK));
    const gfx_color_t ink = runs ? ui_theme_color(UI_C_TEXT)
                                 : ui_theme_color(UI_C_TEXT_FAINT);
    char right[64];
    if (dock) {
        snprintf(right, sizeof(right), "%u", (unsigned)m->found);
        const int tag = m->manual ? sp_draw_tag(c, r.x + r.w - 28, r.y + 6)
                                        + 8 : 0;
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + r.w - 28 - tag - 48),
                                     (int16_t)(r.y + 8), 40, 16 },
                    right, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1,
                    GFX_ALIGN_RIGHT);
        sp_text(c, r.x + 8, r.y + 8, m->name,
                (r.w - 8 - 28 - tag - 56) / 8, ink);
        sp_mark_runs(c, r, runs);
        return;
    }
    sp_text(c, r.x + 12, r.y + 8, m->name, 30, ink);
    if (m->manual) {
        (void)sp_draw_tag(c, r.x + r.w - 312 - 8, r.y + 6);
    }
    if (searching) {
        snprintf(right, sizeof(right), TR(SP_MAKER_FOUND),
                 (unsigned)m->found, (unsigned)m->models, (unsigned)m->runs);
    } else {
        snprintf(right, sizeof(right), TR(SP_MAKER_MODELS),
                 (unsigned)m->runs, (unsigned)m->models);
    }
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + r.w - 312),
                                 (int16_t)(r.y + 8), 300, 16 },
                right, UI_FONT_LABEL,
                runs ? ui_theme_color(UI_C_ACCENT)
                     : ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_RIGHT);
}

/* A model's row: its name, current and voltage, its family, the MANUAL
 * tag, and what its family's profile is or why it does not run. */
static DRAWER void sp_draw_model(gfx_canvas_t *c, gfx_rect_t r,
                                 const sp_row_t *row, bool dock)
{
    const esc_profile_t *p = esc_profiles_at(row->prof);
    const esc_model_t *m = &p->models[row->model];
    char cap[64];
    const char *why = sp_model_why(p, row->model, cap, sizeof(cap));
    const bool runs = why == NULL;
    ui_card(c, r, runs ? ui_theme_color(UI_C_PANEL)
                       : ui_theme_color(UI_C_PANEL_SUNK));
    const gfx_color_t ink = runs ? ui_theme_color(UI_C_TEXT)
                                 : ui_theme_color(UI_C_TEXT_FAINT);
    const gfx_color_t dim = runs ? ui_theme_color(UI_C_TEXT_DIM)
                                 : ui_theme_color(UI_C_TEXT_FAINT);
    char amps[16], volts[16];
    sp_amps(amps, sizeof(amps), m);
    sp_volts(volts, sizeof(volts), m);
    if (dock) {
        const int tag = (p->manual_count > 0u)
                            ? sp_draw_tag(c, r.x + r.w - 28, r.y + 6) + 8
                            : 0;
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + r.w - 28 - tag - 56),
                                     (int16_t)(r.y + 8), 48, 16 },
                    amps, UI_FONT_LABEL, dim, 1, GFX_ALIGN_RIGHT);
        sp_text(c, r.x + 8, r.y + 8, m->name,
                (r.w - 8 - 28 - tag - 64) / 8, ink);
        sp_mark_runs(c, r, runs);
        return;
    }
    sp_text(c, r.x + 12, r.y + 8, m->name, 26, ink);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + SP_AMPS_X),
                                 (int16_t)(r.y + 8), 48, 16 },
                amps, UI_FONT_LABEL, dim, 1, GFX_ALIGN_RIGHT);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + SP_VOLTS_X),
                                 (int16_t)(r.y + 8), 56, 16 },
                volts, UI_FONT_LABEL, dim, 1, GFX_ALIGN_RIGHT);
    char right[80];
    char warn[64];
    const bool unrated = runs
                         && sp_model_warn(p, row->model, warn, sizeof(warn))
                                != NULL;
    if (unrated) {
        snprintf(right, sizeof(right), "%s", warn);
    } else if (runs) {
        char items[24];
        const esc_stick_kind_t kind = esc_stick_kind(p, NULL);
        snprintf(items, sizeof(items),
                 (p->item_count == 1u) ? TR(SP_ITEM) : TR(SP_ITEMS),
                 (unsigned)p->item_count);
        snprintf(right, sizeof(right), "%s%s%s  %s",
                 esc_profiles_is_override(p) ? TR(SP_CARD) : "",
                 esc_profiles_is_override(p) ? "  " : "", items,
                 (kind == ESC_STICK_KIND_TWO_STAGE) ? TR(SP_TWO_STAGE)
                                                    : TR(SP_ONE_STAGE));
    } else {
        snprintf(right, sizeof(right), "%s", why);
    }
    /* The right text in 220 px, or as wide as it is up to the family's
     * column (SP_WHY_CELLS); the MANUAL tag left of it where both fit, and
     * the family in what is left, from 3 cells. */
    const int room = SP_WHY_CELLS * 8;
    int rw = gfx_text_cells(right) * 8;
    rw = (rw < 220) ? 220 : rw;
    rw = (rw > room) ? room : rw;
    sp_cut(right, sizeof(right), rw / 8);
    const int rx = r.w - 12 - rw;
    const int tag = (p->manual_count > 0u && rw + 4 + sp_tag_w() <= room)
                        ? sp_draw_tag(c, r.x + rx - 4, r.y + 6) + 8
                        : 0;
    const int fam = (rx - 4 - tag - SP_FAMILY_X) / 8;
    if (fam >= 3) {
        sp_text(c, r.x + SP_FAMILY_X, r.y + 8, p->family, fam,
                ui_theme_color(UI_C_TEXT_FAINT));
    }
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + rx), (int16_t)(r.y + 8),
                                 (int16_t)rw, 16 },
                right, UI_FONT_LABEL,
                unrated ? ui_theme_color(UI_C_WARN)
                : runs  ? ui_theme_color(UI_C_ACCENT)
                        : ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);
}

/*
 * The list: the crumb and the search field above, a level's rows, and the
 * footer: the count at the right under the rows, or beside the docked
 * keyboard at the left; the line that no profile is verified while the
 * keyboard is closed.
 */
static DRAWER void sp_draw_list(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const bool dock = t->tk.open;
    const bool searching = t->find[0] != '\0';
    char trail[96];
    if (t->level == 1 && t->maker != NULL) {
        snprintf(trail, sizeof(trail), "ESC STICK  >  %s", t->maker);
    } else {
        snprintf(trail, sizeof(trail), "ESC STICK");
    }
    sp_cut(trail, sizeof(trail), SP_TRAIL_CELLS);
    draw_crumb(c, trail);
    sp_draw_find(c);
    ui_button(c, t->list_up, "^", ui_theme_color(UI_C_PANEL_HI), false,
              t->scroll > 0);
    ui_button(c, t->list_dn, "v", ui_theme_color(UI_C_PANEL_HI), false,
              t->scroll + SP_ROWS < t->count);

    if (t->count == 0) {
        gfx_text(c, PAD + 12, SP_ROW_Y0 + 8, TR(SP_FIND_NONE), UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
    }
    for (int i = 0; i < SP_ROWS && t->scroll + i < t->count; ++i) {
        if (t->level == 1) {
            sp_draw_model(c, sp_row(i), &t->rows_m[t->scroll + i], dock);
        } else {
            sp_draw_maker(c, sp_row(i), &t->makers[t->scroll + i], dock,
                          searching);
        }
    }

    char count[64];
    const int last = (t->scroll + SP_ROWS < t->count) ? t->scroll + SP_ROWS
                                                      : t->count;
    const int first = (t->count > 0) ? t->scroll + 1 : 0;
    if (t->level == 1 && searching) {
        snprintf(count, sizeof(count), TR(SP_LIST_FOUND), first, last,
                 t->count, t->runnable);
    } else if (t->level == 1) {
        snprintf(count, sizeof(count), TR(SP_LIST_COUNT), first, last,
                 t->count, t->runnable);
    } else if (searching) {
        snprintf(count, sizeof(count), TR(SP_MAKERS_FOUND), first, last,
                 t->count, t->runnable);
    } else {
        snprintf(count, sizeof(count), TR(SP_MAKERS_COUNT), first, last,
                 t->count, t->runnable);
    }
    if (dock) {
        gfx_text_in(c, (gfx_rect_t){ PAD + 12, SP_FOOT_Y,
                                     (int16_t)(SP_DOCK_X - 8 - PAD - 12),
                                     16 },
                    count, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1,
                    GFX_ALIGN_LEFT);
        ui_textkey_render(&t->tk, c);
        return;
    }
    gfx_text(c, PAD + 12, SP_FOOT_Y, TR(SP_UNVERIFIED_ALL), UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(W - PAD - 12 - SP_COUNT_W),
                                 SP_FOOT_Y, SP_COUNT_W, 16 },
                count, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);
}

static void sp_setting_text(setting_id_t id, char *out, size_t n)
{
    if (id == SET_STICK_V && !(settings_get(id) > 0.0f)) {
        snprintf(out, n, "%s", TR(SP_FROM_PROFILE));
        return;
    }
    char v[24];
    ui_setting_value(id, v, sizeof(v));
    snprintf(out, n, "%s %s", v, settings_def(id)->unit);
}

static DRAWER void sp_draw_timing(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    ui_card(c, (gfx_rect_t){ PAD, DEV_Y, (int16_t)(W - 2 * PAD),
                             (int16_t)(PARM_Y + PARM_H - DEV_Y) },
            ui_theme_color(UI_C_PANEL));
    gfx_text(c, PAD + 12, DEV_Y + 12, TR(SP_TIMING_TITLE), UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    gfx_text(c, PAD + 12, DEV_Y + 34,
             TR(SP_TIMING_NOTE), UI_FONT_LABEL, ui_theme_color(UI_C_WARN), 1);
    const int max_scroll = SP_SETTINGS - ROWS_MAX;
    ui_button(c, pg.page_up, "^", ui_theme_color(UI_C_PANEL_HI), false,
              t->tscroll > 0);
    ui_button(c, pg.page_dn, "v", ui_theme_color(UI_C_PANEL_HI), false,
              t->tscroll < max_scroll);
    for (int i = 0; i < sp_rows_shown(SP_SETTINGS, t->tscroll); ++i) {
        const int idx = t->tscroll + i;
        const setting_id_t id = sp_settings_list[idx];
        const setting_def_t *d = settings_def(id);
        const int y = ROW_Y0 + i * ROW_H;
        if (idx == t->tpicked) {
            gfx_fill_round_rect(c, PAD + 8, y - 4, W - 2 * PAD - 16,
                                ROW_H - 2, 4,
                                ui_theme_color(UI_C_PANEL_SUNK));
        }
        gfx_text(c, PAD + 12, y, ui_setting_label(id), UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
        char v[40];
        sp_setting_text(id, v, sizeof(v));
        const bool moved = settings_get(id) != d->def;
        widget_enum(c, y, v, moved ? ui_theme_color(UI_C_WARN)
                                   : ui_theme_color(UI_C_ACCENT));
        const float now = settings_get(id);
        ui_button(c, pg.down[i], "-", ui_theme_color(UI_C_PANEL_HI), false,
                  now > d->min);
        ui_button(c, pg.up[i], "+", ui_theme_color(UI_C_PANEL_HI), false,
                  now < d->max);
    }
    const setting_id_t picked = sp_settings_list[t->tpicked];
    gfx_text(c, PAD + 12, HELP_Y, ui_setting_label(picked), UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    gfx_text(c, PAD + 12, HELP_Y + 18, ui_setting_help(picked),
             UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1);
    ui_button(c, pg.read_btn, TR(SP_DEFAULTS), ui_theme_color(UI_C_PANEL_HI),
              false, true);
    ui_button(c, pg.write_btn, TR(SUP_CLOSE), ui_theme_color(UI_C_PANEL_HI),
              false, true);
}

static DRAWER void sp_draw_items(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_profile_t *p = t->p;
    const int items = sp_items();
    gfx_text(c, PAD + 12, PARM_Y + 12, TR(SP_CHANGE), UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    if (p->manual_count > 0u) {
        ui_button(c, t->hand_btn, TR(SP_HAND_BTN),
                  ui_theme_color(UI_C_DANGER), false, true);
    }
    const int max_scroll = (items > ROWS_MAX) ? items - ROWS_MAX : 0;
    char count[40];
    snprintf(count, sizeof(count), TR(PG_RANGE_OF), t->iscroll + 1,
             t->iscroll + sp_rows_shown(items, t->iscroll), items);
    gfx_text_in(c, (gfx_rect_t){ 420, (int16_t)(PARM_Y + 12), 260, 16 },
                count, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);
    ui_button(c, pg.page_up, "^", ui_theme_color(UI_C_PANEL_HI), false,
              t->iscroll > 0);
    ui_button(c, pg.page_dn, "v", ui_theme_color(UI_C_PANEL_HI), false,
              t->iscroll < max_scroll);

    for (int i = 0; i < sp_rows_shown(items, t->iscroll); ++i) {
        const int at = t->iscroll + i;
        const int idx = sp_item_at(at);
        if (idx < 0) {
            break;
        }
        const esc_item_t *it = &p->items[idx];
        const int pick = t->pick[idx];
        const int y = ROW_Y0 + i * ROW_H;
        if (idx == t->picked) {
            gfx_fill_round_rect(c, PAD + 8, y - 4, W - 2 * PAD - 16,
                                ROW_H - 2, 4,
                                ui_theme_color(UI_C_PANEL_SUNK));
        }
        char num[8];
        snprintf(num, sizeof(num), "%u", (unsigned)it->number);
        gfx_text(c, PAD + 12, y, num, UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
        if (pick >= 0) {
            gfx_fill_round_rect(c, PAD + 38, y + 2, 3, 12, 1,
                                ui_theme_color(UI_C_WARN));
        }
        sp_text(c, PAD + 48, y, it->name, (CTRL_X - PAD - 56) / 8,
                ui_theme_color(UI_C_TEXT));
        const bool action = esc_stick_not_offered(it) != NULL;
        char v[64];
        if (action) {
            snprintf(v, sizeof(v), "%s", esc_stick_is_action(it)
                                             ? TR(SP_ACTION)
                                             : TR(SP_NOTHING_TO_CHOOSE));
        } else if (pick < 0) {
            snprintf(v, sizeof(v), "%s", TR(SP_KEEP));
        } else {
            snprintf(v, sizeof(v), "%u %s",
                     (unsigned)it->values[pick].number,
                     it->values[pick].name);
        }
        sp_cut(v, sizeof(v), CTRL_W / 8);
        widget_enum(c, y, v, (pick >= 0) ? ui_theme_color(UI_C_WARN)
                                         : ui_theme_color(UI_C_TEXT_FAINT));
        ui_button(c, pg.down[i], "-", ui_theme_color(UI_C_PANEL_HI), false,
                  !action && pick > -1);
        ui_button(c, pg.up[i], "+", ui_theme_color(UI_C_PANEL_HI), false,
                  !action && pick < (int)it->value_count - 1);
    }

    /* The picked item: its values, and where it applies. */
    if (items == 0) {
        return;
    }
    const esc_item_t *it = &p->items[t->picked];
    char line[160];
    if (it->applies_when[0] != '\0') {
        snprintf(line, sizeof(line), TR(SP_ONLY_WHEN),
                 (unsigned)it->number, it->name, it->applies_when);
    } else if (it->applies_count > 0u) {
        snprintf(line, sizeof(line), TR(SP_ONLY_ON),
                 (unsigned)it->number, it->name,
                 (unsigned)it->applies_count);
    } else {
        snprintf(line, sizeof(line), "%u %s", (unsigned)it->number,
                 it->name);
    }
    sp_text(c, PAD + 12, HELP_Y, line, 94, ui_theme_color(UI_C_TEXT_DIM));
    size_t at = 0;
    line[0] = '\0';
    for (unsigned k = 0; k < it->value_count && at + 1u < sizeof(line); ++k) {
        const int w = snprintf(line + at, sizeof(line) - at, "%s%u %s%s%s",
                               (k > 0u) ? "   " : "",
                               (unsigned)it->values[k].number,
                               it->values[k].name,
                               it->values[k].is_default ? " " : "",
                               it->values[k].is_default
                                   ? TR(SP_DEFAULT_MARK) : "");
        if (w < 0) {
            break;
        }
        at += (size_t)w;
    }
    sp_text(c, PAD + 12, HELP_Y + 18, line, 94,
            ui_theme_color(UI_C_TEXT_FAINT));

    size_t picked = 0;
    esc_stick_change_t ch[ESC_STICK_MAX_CHANGES];
    (void)sp_changes(ch, &picked);
    const bool can = sp_can_run();
    if (t->note[0] != '\0') {
        sp_note(c, PAD + 12, pg.write_btn.x - 8, t->note,
                ui_theme_color(UI_C_WARN));
    } else {
        sp_note(c, PAD + 12, pg.write_btn.x - 8,
                (picked == 0u) ? TR(SP_PICK_VALUE) : TR(SP_UNVERIFIED),
                ui_theme_color(UI_C_TEXT_FAINT));
    }
    char run[32];
    if (picked > 0u) {
        snprintf(run, sizeof(run), TR(SP_RUN_N), (unsigned)picked);
    } else {
        snprintf(run, sizeof(run), "%s", TR(SP_RUN_BTN));
    }
    ui_button(c, pg.write_btn, run,
              can ? ui_theme_color(UI_C_DANGER)
                  : ui_theme_color(UI_C_PANEL_HI), false, can);
}

/* " (" ... ")" around a word, as the line after a number carries it. */
static const char *sp_mark(ui_text_id_t id)
{
    static char buf[40];
    snprintf(buf, sizeof(buf), " %s", ui_tr(id));
    return buf;
}

/*
 * The entry time the page shows: during a run and on its result, the wait
 * of the run's power-up, kept after its change is made; else what the
 * first change picked would wait, or with none picked the entry's.
 */
uint32_t sp_entry_shown(void)
{
    const stick_t *t = &s.st;
    if (esc_stick_running(&t->run) || t->shown) {
        return t->run.entry_wait;
    }
    esc_stick_timing_t tm;
    sp_timing(&tm);
    size_t picked = 0;
    esc_stick_change_t ch[ESC_STICK_MAX_CHANGES];
    const size_t n = sp_changes(ch, &picked);
    return esc_stick_change_entry_ms(t->p, (n > 0u) ? &ch[0] : NULL, &tm);
}

/* The profile page's crumb: BACK and the profile's name. */
static DRAWER void sp_draw_crumb(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_profile_t *p = t->p;
    char line[128];
    if (t->model >= 0 && (unsigned)t->model < p->model_count) {
        snprintf(line, sizeof(line), "ESC STICK  >  %s  >  %s  (%s)",
                 p->brand, p->models[t->model].name, p->family);
    } else {
        snprintf(line, sizeof(line), "ESC STICK  >  %s %s", p->brand,
                 p->family);
    }
    ui_button(c, pg.back, TR(LOG_BACK), ui_theme_color(UI_C_PANEL_HI), false,
              !esc_stick_running(&t->run));
    /* Up to TIMING where the profile page shows it. */
    const int x = pg.back.x + pg.back.w + 16;
    sp_text(c, x, CRUMB_Y + 8, line,
            t->timing ? 72 : (t->timing_btn.x - 8 - x) / 8,
            ui_theme_color(UI_C_TEXT_DIM));
}

/* The profile page's two cards, and in the upper one how the profile's
 * menu works and the supply and entry time a run uses. */
static DRAWER void sp_draw_card(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_profile_t *p = t->p;
    char line[128];
    ui_card(c, (gfx_rect_t){ PAD, DEV_Y, (int16_t)(W - 2 * PAD), DEV_H },
            ui_theme_color(UI_C_PANEL));
    ui_card(c, (gfx_rect_t){ PAD, PARM_Y, (int16_t)(W - 2 * PAD), PARM_H },
            ui_theme_color(UI_C_PANEL));

    const esc_stick_kind_t kind = esc_stick_kind(p, NULL);
    snprintf(line, sizeof(line), "%s   %s   %s",
             (kind == ESC_STICK_KIND_TWO_STAGE) ? TR(SP_TWO_STAGE_MENU)
                                                : TR(SP_ONE_STAGE_MENU),
             (p->encoding == ESC_ENC_SHORT_LONG) ? TR(SP_SHORT_LONG)
                                                 : TR(SP_COUNTED),
             p->one_change_per_entry ? TR(SP_ONE_CHANGE)
                                     : TR(SP_MANY_CHANGES));
    gfx_text(c, PAD + 12, DEV_Y + 4, line, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT), 1);
    uint32_t mv, ma;
    sp_supply(&mv, &ma);
    esc_stick_timing_t tm;
    sp_timing(&tm);
    if (esc_stick_running(&t->run) || t->shown) {
        mv = t->run.out.supply_mv;
        ma = t->run.out.supply_ma;
    }
    tm.entry_ms = sp_entry_shown();
    snprintf(line, sizeof(line), TR(SP_SUPPLY_ENTRY), (unsigned)(mv / 1000u),
             (unsigned)(mv % 1000u / 10u), (unsigned)(ma / 1000u),
             (unsigned)(ma % 1000u / 10u), (unsigned)(tm.entry_ms / 1000u),
             (unsigned)(tm.entry_ms % 1000u / 100u),
             (p->entry_hold_ms != 0u) ? sp_mark(TX_SP_FROM_PROFILE_MARK)
                                      : sp_mark(TX_SP_FROM_SETTING_MARK));
    gfx_text(c, PAD + 12, DEV_Y + 22, line, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    ui_button(c, t->timing_btn, "TIMING", ui_theme_color(UI_C_PANEL_HI),
              false, !esc_stick_running(&t->run) && !t->shown);
}

/* The profile page.  Its parts are drawn one after another, so none of
 * their buffers is on the stack below the stage drawn last. */
static void sp_draw_device(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    sp_draw_crumb(c);
    if (t->timing) {
        sp_draw_timing(c);
        return;
    }
    sp_draw_card(c);
    if (esc_stick_running(&t->run)) {
        sp_draw_progress(c);
    } else if (t->shown) {
        sp_draw_result(c);
    } else {
        sp_draw_items(c);
    }
}

void esc_stick_panel_render(gfx_canvas_t *c)
{
    if (s.st.hand_open && s.st.hand_p != NULL) {
        sp_draw_hand(c);
        return;
    }
    if (pg.stage == STAGE_PAGE && s.st.p != NULL
        && esc_stick_hand(&s.st.run) != NULL) {
        sp_draw_prompt(c);
        return;
    }
    if (s.st.warn && s.st.p != NULL) {
        sp_draw_warning(c);
        return;
    }
    if (pg.stage == STAGE_LIST || s.st.p == NULL) {
        sp_draw_list(c);
        return;
    }
    sp_draw_device(c);
}
