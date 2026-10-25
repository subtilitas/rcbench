/**
 * @file esc_stick_draw_run.c
 * @brief A run of ESC STICK drawn.
 *
 * A run of ESC STICK drawn: the stack light, the beeps the detector hears,
 * the progress, the result, the RUN warning, the manual steps and a run's
 * prompt.  The state and the seam are esc_stick_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_stick_panel.h"

#include <stdio.h>
#include <string.h>

#include "esc_profile.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: the layout macros of the headers it includes have short names
 * (W, H, PAD) and s names the state, which no header included after it
 * may meet. */
#include "esc_stick_panel_int.h"

/**
 * @brief One lens or the base of the stack light: a cylinder seen from the side.
 *        Lit, the colour runs to a near-white band down the middle; unlit, the
 *        same colour greyed and dark.  Both darken toward the edges, and a darker
 *        line every 6px is a rib of the lens.  Drawn row by row in runs of one
 *        colour, since the panel's frame buffer is cached by rows.
 */
static void sp_cylinder(gfx_canvas_t *c, int x, int y, int w, int h,
                        gfx_color_t base, bool lit, bool ribs)
{
    gfx_color_t col[64];
    gfx_color_t rib[64];
    if (w > 64) {
        w = 64;
    }
    for (int i = 0; i < w; ++i) {
        /* 0 in the middle, 255 at either edge, in eight steps. */
        const int off = (2 * i + 1 - w < 0) ? w - 2 * i - 1 : 2 * i + 1 - w;
        const int d = ((off * 8 / (w + 1)) * 255) / 7;
        gfx_color_t k;
        if (d < 64) {
            k = gfx_lerp(base, GFX_WHITE, lit ? 120 : 24);
        } else {
            k = gfx_lerp(base, GFX_BLACK, (uint8_t)((d - 64) * 150 / 191));
        }
        col[i] = k;
        rib[i] = gfx_lerp(k, GFX_BLACK, 80);
    }
    for (int row = 0; row < h; ++row) {
        const bool dark = row == 0 || row == h - 1
                          || (ribs && row % 6 == 5);
        const gfx_color_t *line = dark ? rib : col;
        int run = 0;
        for (int i = 1; i <= w; ++i) {
            if (i == w || line[i] != line[run]) {
                gfx_hline(c, x + run, y + row, i - run, line[run]);
                run = i;
            }
        }
    }
}

/** @brief A lens, lit or dark, with a glow around it when lit. */
static void sp_lens(gfx_canvas_t *c, int x, int y, gfx_color_t colour,
                    bool lit)
{
    const gfx_color_t panel = ui_theme_color(UI_C_PANEL);
    if (lit) {
        gfx_fill_rect(c, x - 4, y - 3, SP_TOWER_W + 8, SP_TOWER_LENS + 6,
                      gfx_lerp(panel, colour, 60));
        gfx_fill_rect(c, x - 2, y - 1, SP_TOWER_W + 4, SP_TOWER_LENS + 2,
                      gfx_lerp(panel, colour, 130));
        sp_cylinder(c, x, y, SP_TOWER_W, SP_TOWER_LENS, colour, true, true);
        return;
    }
    /* Off: the colour greyed, then darkened. */
    const gfx_color_t off = gfx_lerp(gfx_lerp(colour, GFX_GREY(110), 150),
                                     GFX_BLACK, 150);
    sp_cylinder(c, x, y, SP_TOWER_W, SP_TOWER_LENS, off, false, true);
}

/**
 * @brief The stack light: red over green on a light grey base, as a signal tower
 *        on a machine.  Green: the detector holds a beep.  Red: the run ended for
 *        a reason esc_stick_reason_is_fault() names.
 */
static void sp_draw_tower(gfx_canvas_t *c, bool red, bool green)
{
    const int x = SP_TOWER_X;
    int y = SP_TOWER_Y;
    const gfx_color_t grey = GFX_GREY(196);
    sp_cylinder(c, x + 6, y, SP_TOWER_W - 12, SP_TOWER_CAP, GFX_GREY(90),
                false, false);
    y += SP_TOWER_CAP;
    sp_lens(c, x, y, ui_theme_color(UI_C_DANGER), red);
    y += SP_TOWER_LENS;
    sp_cylinder(c, x, y, SP_TOWER_W, SP_TOWER_RING, grey, false, false);
    y += SP_TOWER_RING;
    sp_lens(c, x, y, ui_theme_color(UI_C_OK), green);
    y += SP_TOWER_LENS;
    sp_cylinder(c, x, y, SP_TOWER_W, SP_TOWER_RING, grey, false, false);
    y += SP_TOWER_RING;
    sp_cylinder(c, x - 2, y, SP_TOWER_W + 4, SP_TOWER_BASE, grey, false,
                false);
    y += SP_TOWER_BASE;
    sp_cylinder(c, x - 8, y, SP_TOWER_W + 16, 4, GFX_GREY(160), false,
                false);
}

/** @brief "MIN", "MID" or "MAX" for a percentage the run commands. */
static const char *sp_pos(float pct)
{
    return (pct >= ESC_STICK_PCT_MAX) ? "MAX"
         : (pct >= ESC_STICK_PCT_MID) ? "MID" : "MIN";
}

/** @brief Returns the help line for a run's end, in the language showing. */
static const char *sp_reason_help(esc_stick_reason_t r)
{
    switch (r) {
    case ESC_STICK_R_STOP:        return TR(SP_WHY_STOP);
    case ESC_STICK_R_BENCH_STOP:  return TR(SP_WHY_BENCH_STOP);
    case ESC_STICK_R_DISARMED:    return TR(SP_WHY_DISARMED);
    case ESC_STICK_R_LINK:        return TR(SP_WHY_LINK);
    case ESC_STICK_R_SUPPLY_OFF:  return TR(SP_WHY_SUPPLY_OFF);
    case ESC_STICK_R_SUPPLY_LOST: return TR(SP_WHY_SUPPLY_LOST);
    case ESC_STICK_R_STALE:       return TR(SP_WHY_STALE);
    case ESC_STICK_R_RATE:        return TR(SP_WHY_RATE);
    case ESC_STICK_R_NOT_ARMED:   return TR(SP_WHY_NOT_ARMED);
    case ESC_STICK_R_NO_POWER:    return TR(SP_WHY_NO_POWER);
    case ESC_STICK_R_NO_BEEPS:    return TR(SP_WHY_NO_BEEPS);
    case ESC_STICK_R_HIGH:        return TR(SP_WHY_HIGH);
    case ESC_STICK_R_TIMEOUT:     return TR(SP_WHY_TIMEOUT);
    case ESC_STICK_R_HAND: {
        static char buf[128];
        snprintf(buf, sizeof(buf), TR(SP_WHY_HAND),
                 (unsigned)(ESC_STICK_HAND_WAIT_MS / 1000u));
        return buf;
    }
    case ESC_STICK_R_SUPPLY_ON:   return TR(SP_WHY_SUPPLY_ON);
    case ESC_STICK_R_TOUCH:       return TR(SP_WHY_TOUCH);
    case ESC_STICK_R_USER:        return TR(SP_WHY_USER);
    case ESC_STICK_R_LEFT:        return TR(SP_WHY_LEFT);
    default:                      return "";
    }
}

/**
 * @brief The run's phase and its end in the language showing; the engine's
 *        esc_stick_phase_text() and esc_stick_reason_text() are the English.
 */
static const char *sp_phase_text(esc_stick_phase_t ph)
{
    switch (ph) {
    case ESC_STICK_IDLE:    return TR(SP_PH_READY);
    case ESC_STICK_ARMING:  return TR(SP_PH_ARMING);
    case ESC_STICK_SIGNAL:  return TR(SP_PH_SIGNAL);
    case ESC_STICK_POWER:   return TR(SP_PH_POWER);
    case ESC_STICK_ENTRY:   return TR(SP_PH_ENTRY);
    case ESC_STICK_ITEMS:   return TR(SP_PH_ITEMS);
    case ESC_STICK_VALUES:  return TR(SP_PH_VALUES);
    case ESC_STICK_STORE:   return TR(SP_PH_STORE);
    case ESC_STICK_CYCLE:   return TR(SP_PH_CYCLE);
    case ESC_STICK_HAND_OFF: return TR(SP_PH_HAND_OFF);
    case ESC_STICK_HAND_ON: return TR(SP_PH_HAND_ON);
    case ESC_STICK_HAND_END: return TR(SP_PH_HAND_END);
    case ESC_STICK_OFF:     return TR(SP_PH_OFF);
    case ESC_STICK_DONE:    return TR(SP_PH_DONE);
    case ESC_STICK_ABORTED: return TR(SP_PH_ABORTED);
    }
    return "?";
}

/** @brief Returns the name of a run's end, in the language showing. */
static const char *sp_reason_text(esc_stick_reason_t r)
{
    switch (r) {
    case ESC_STICK_R_NONE:        return "";
    case ESC_STICK_R_STOP:        return TR(SP_R_STOP);
    case ESC_STICK_R_BENCH_STOP:  return TR(SP_R_BENCH_STOP);
    case ESC_STICK_R_DISARMED:    return TR(SP_R_DISARMED);
    case ESC_STICK_R_LINK:        return TR(SP_R_LINK);
    case ESC_STICK_R_SUPPLY_OFF:  return TR(SP_R_SUPPLY_OFF);
    case ESC_STICK_R_SUPPLY_LOST: return TR(SP_R_SUPPLY_LOST);
    case ESC_STICK_R_STALE:       return TR(SP_R_STALE);
    case ESC_STICK_R_RATE:        return TR(SP_R_RATE);
    case ESC_STICK_R_NOT_ARMED:   return TR(SP_R_NOT_ARMED);
    case ESC_STICK_R_NO_POWER:    return TR(SP_R_NO_POWER);
    case ESC_STICK_R_SUPPLY_ON:   return TR(SP_R_SUPPLY_ON);
    case ESC_STICK_R_TOUCH:       return TR(SP_R_TOUCH);
    case ESC_STICK_R_NO_BEEPS:    return TR(SP_R_NO_BEEPS);
    case ESC_STICK_R_HIGH:        return TR(SP_R_HIGH);
    case ESC_STICK_R_TIMEOUT:     return TR(SP_R_TIMEOUT);
    case ESC_STICK_R_HAND:        return TR(SP_R_HAND);
    case ESC_STICK_R_USER:        return TR(SP_R_USER);
    case ESC_STICK_R_LEFT:        return TR(SP_R_LEFT);
    }
    return "?";
}

/** @brief "3 Cutoff mode -> 2 hard cutoff" for a change. */
static void sp_change_text(const esc_stick_change_t *ch, char *out, size_t n)
{
    const esc_item_t *it = &s.st.p->items[ch->item];
    const esc_value_t *v = &it->values[ch->value];
    snprintf(out, n, "%u %s -> %u %s", (unsigned)it->number, it->name,
             (unsigned)v->number, v->name);
}

/**
 * @brief "POWER-UP AT MID" for the positions the power-ups of @p n changes take,
 *        in the order first met: "MAX, MID" where a one-stage run takes both.
 *        False, and nothing written, when every one is MIN.
 */
static bool sp_power_up_text(const esc_profile_t *p,
                             const esc_stick_change_t *ch, size_t n,
                             char *out, size_t size)
{
    bool seen[ESC_THR_NONE + 1] = { false };
    char list[24] = "";
    bool other = false;
    for (size_t i = 0; i < n; ++i) {
        const esc_throttle_t at = esc_stick_change_entry(p, &ch[i]);
        if (seen[at]) {
            continue;
        }
        seen[at] = true;
        other = other || at != ESC_THR_MIN;
        const size_t len = strlen(list);
        snprintf(list + len, sizeof(list) - len, "%s%s",
                 (len > 0u) ? ", " : "", sp_pos(esc_stick_pct(at)));
    }
    if (!other) {
        return false;
    }
    snprintf(out, size, TR(SP_POWER_UP_AT), list);
    return true;
}

/**
 * @brief The longest hold of the profile's at_power_up steps, ms; 0 for none.
 */
static uint32_t sp_hold_ms(const esc_profile_t *p)
{
    uint32_t hold = 0u;
    for (unsigned i = 0; p->manual != NULL && i < p->manual_count; ++i) {
        if (p->manual[i].when == ESC_MANUAL_AT_POWER_UP
            && p->manual[i].hold_ms > hold) {
            hold = p->manual[i].hold_ms;
        }
    }
    return hold;
}

/** @brief The phase tap's state as its readout is worded. */
static const char *sp_tone_state(const tone_readout_t *r)
{
    switch (r->state) {
    case TONE_STATE_NO_PAGE:
        return TR(SP_TONE_NO_PAGE);
    case TONE_STATE_WAITING:
        return TR(SP_TONE_WAITING);
    case TONE_STATE_RUNNING:
        return (r->read && r->overrun) ? TR(SP_TONE_OVERRUN)
                                       : TR(SP_TONE_RUNNING);
    case TONE_STATE_REFUSED:
        return TR(SP_TONE_REFUSED);
    case TONE_STATE_STOPPED:
    default:
        return TR(SP_TONE_STOPPED);
    }
}

/**
 * @brief One beep of the readout: its number, length in ms and pitch in Hz, to
 *        the tenth, as the page reports them.
 */
static void sp_tone_beep(const tone_beep_t *b, char *buf, size_t n)
{
    snprintf(buf, n, TR(SP_TONE_BEEP), (unsigned)b->seq,
             (unsigned)(b->len_dms / 10u), (unsigned)(b->len_dms % 10u),
             (unsigned)(b->freq_dhz / 10u), (unsigned)(b->freq_dhz % 10u));
}

/** @brief The beeps' two columns and the cells each takes. */
#define SP_TONE_COL_CELLS 30

/**
 * @brief The phase tap, read only, on the run's page while the tap is enabled
 *        on SETUP: a head line, then the last four beeps two to a line.
 */
static DRAWER void sp_draw_tone(gfx_canvas_t *c, int y, int pitch)
{
    const tone_readout_t *r = &s.tone;
    if (r->state == TONE_STATE_OFF) {
        return;
    }
    const gfx_color_t dim = ui_theme_color(UI_C_TEXT_DIM);
    const bool fault = r->state == TONE_STATE_NO_PAGE
                       || r->state == TONE_STATE_REFUSED
                       || r->state == TONE_STATE_STOPPED
                       || (r->read && r->overrun);
    /* The pitch and the lost count are the page's, and shown only while a
     * status read younger than TONE_LINK_STALE_MS speaks for it. */
    char pitch_txt[16];
    char lost_txt[8];
    if (!r->read || r->win_freq_dhz == 0u) {
        snprintf(pitch_txt, sizeof(pitch_txt), "--");
    } else {
        snprintf(pitch_txt, sizeof(pitch_txt), "%u.%uHz",
                 (unsigned)(r->win_freq_dhz / 10u),
                 (unsigned)(r->win_freq_dhz % 10u));
    }
    if (r->read) {
        snprintf(lost_txt, sizeof(lost_txt), "%u", (unsigned)r->lost);
    } else {
        snprintf(lost_txt, sizeof(lost_txt), "--");
    }
    char line[128];
    snprintf(line, sizeof(line), TR(SP_TONE_HEAD), sp_tone_state(r),
             pitch_txt, lost_txt, (unsigned)r->missed);
    gfx_text(c, PAD + 12, y, line, SP_FONT,
             fault ? ui_theme_color(UI_C_WARN) : dim, 1);
    const int x0 = PAD + 12
                   + (gfx_text_cells(TR(SP_TONE_BEEPS)) + 2) * SP_CELL;
    gfx_text(c, PAD + 12, y + pitch, TR(SP_TONE_BEEPS), SP_FONT, dim,
             1);
    if (r->n == 0u) {
        gfx_text(c, x0, y + pitch, TR(SP_TONE_NONE), SP_FONT, dim, 1);
        return;
    }
    for (unsigned i = 0u; i < r->n && i < TONE_LINK_SHOWN; ++i) {
        sp_tone_beep(&r->beeps[i], line, sizeof(line));
        gfx_text(c, x0 + (int)(i % 2u) * SP_TONE_COL_CELLS * SP_CELL,
                 y + pitch + (int)(i / 2u) * pitch, line, SP_FONT,
                 ui_theme_color(UI_C_TEXT), 1);
    }
}

DRAWER void sp_draw_progress(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_stick_t *e = &t->run;
    const gfx_color_t dim = ui_theme_color(UI_C_TEXT_DIM);
    const gfx_color_t txt = ui_theme_color(UI_C_TEXT);
    char line[128];

    gfx_text(c, PAD + 12, RUN_Y + 12, TR(SP_RUN), SP_FONT,
             ui_theme_color(UI_C_ACCENT), 1);
    if (e->entry != ESC_THR_MIN) {
        snprintf(line, sizeof(line), TR(SP_POWER_UP_AT),
                 sp_pos(esc_stick_pct(e->entry)));
        gfx_text(c, PAD + 12 + gfx_text_cells(TR(SP_RUN)) * SP_CELL + 24,
                 RUN_Y + 12, line, SP_FONT,
                 ui_theme_color(UI_C_WARN), 1);
    }
    gfx_text(c, PAD + 12, RUN_Y + 38, sp_phase_text(e->phase),
             SP_FONT_HEAD, txt, 1);
    /* The count beside the stack light. */
    snprintf(line, sizeof(line), "%u", esc_stick_beeps(e));
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(SP_TOWER_X - 24 - 250),
                                 (int16_t)(RUN_Y + 10), 250, 44 },
                line, SP_FONT_NUM, ui_theme_color(UI_C_CURR), 1,
                GFX_ALIGN_RIGHT);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(SP_TOWER_X - 24 - 250),
                                 (int16_t)(RUN_Y + 58), 250, 20 },
                TR(SP_BEEPS_GROUP), SP_FONT,
                ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_RIGHT);

    const int y0 = RUN_L0;
    const int pitch = LINE_P;
    /* The change being made; while items are counted, the first one still
     * to make, since the menu decides which comes up first. */
    uint8_t shown = e->active;
    if (e->phase != ESC_STICK_VALUES && e->phase != ESC_STICK_STORE) {
        for (uint8_t i = 0; i < e->n; ++i) {
            if (!e->done[i]) {
                shown = i;
                break;
            }
        }
    }
    char what[96];
    sp_change_text(&e->ch[shown], what, sizeof(what));
    const unsigned done = esc_stick_done_count(e);
    snprintf(line, sizeof(line), TR(SP_SELECTION),
             (done < e->n) ? done + 1u : (unsigned)e->n, (unsigned)e->n,
             what);
    sp_text(c, PAD + 12, y0, line, SP_LINE_CELLS, txt);

    switch (e->phase) {
    case ESC_STICK_ARMING:
        snprintf(line, sizeof(line), "%s", TR(SP_DO_ARMING));
        break;
    case ESC_STICK_SIGNAL:
        snprintf(line, sizeof(line), TR(SP_DO_SIGNAL),
                 sp_pos(e->out.throttle_pct),
                 (unsigned)((e->now_ms - e->phase_ms < ESC_STICK_SIGNAL_MS)
                                ? ESC_STICK_SIGNAL_MS
                                      - (e->now_ms - e->phase_ms)
                                : 0u));
        break;
    case ESC_STICK_POWER:
        snprintf(line, sizeof(line), TR(SP_DO_POWER),
                 (unsigned)(e->out.supply_mv / 1000u),
                 (unsigned)(e->out.supply_mv % 1000u / 10u));
        break;
    case ESC_STICK_ENTRY: {
        const uint32_t in = e->now_ms - e->on_ms;
        const uint32_t wait = e->entry_wait;
        const uint32_t left = (in < wait) ? wait - in : 0u;
        /* A button held while the supply came on: how long still, then
         * that it can go. */
        const uint32_t hold = sp_hold_ms(e->p);
        if (hold > in) {
            snprintf(line, sizeof(line), TR(SP_DO_HOLD),
                     (unsigned)((hold - in) / 1000u),
                     (unsigned)((hold - in) % 1000u / 100u));
        } else if (hold > 0u) {
            snprintf(line, sizeof(line), TR(SP_DO_LET_GO),
                     (unsigned)(left / 1000u),
                     (unsigned)(left % 1000u / 100u));
        } else {
            snprintf(line, sizeof(line), TR(SP_DO_ENTRY),
                     sp_pos(e->out.throttle_pct),
                     (unsigned)(left / 1000u),
                     (unsigned)(left % 1000u / 100u));
        }
        break;
    }
    case ESC_STICK_ITEMS:
        line[0] = '\0';
        for (uint8_t i = 0; i < e->n; ++i) {
            if (!e->done[i]) {
                snprintf(line, sizeof(line), TR(SP_DO_ITEMS),
                         (unsigned)e->p->items[e->ch[i].item].number,
                         sp_pos(esc_stick_pct(e->p->select_throttle)));
                break;
            }
        }
        break;
    case ESC_STICK_VALUES: {
        const esc_stick_change_t *ch = &e->ch[e->active];
        snprintf(line, sizeof(line), TR(SP_DO_VALUES),
                 (unsigned)e->p->items[ch->item].values[ch->value].number,
                 sp_pos(esc_stick_pct(
                     (e->kind == ESC_STICK_KIND_TWO_STAGE)
                         ? e->p->value_select_throttle
                         : e->p->select_throttle)));
        break;
    }
    case ESC_STICK_STORE:
        if (e->store_step > 0u) {
            snprintf(line, sizeof(line), TR(SP_DO_STORE_MOVE),
                     sp_pos(e->out.throttle_pct),
                     (unsigned)e->t.store_ms);
        } else {
            snprintf(line, sizeof(line), TR(SP_DO_STORE_HOLD),
                     (unsigned)e->t.store_ms);
        }
        break;
    case ESC_STICK_CYCLE:
        snprintf(line, sizeof(line), TR(SP_DO_CYCLE), (unsigned)e->t.off_ms);
        break;
    case ESC_STICK_OFF:
        snprintf(line, sizeof(line), TR(SP_DO_OFF),
                 sp_pos(e->out.throttle_pct));
        break;
    default:
        line[0] = '\0';
        break;
    }
    sp_text(c, PAD + 12, y0 + pitch, line, SP_LINE_CELLS, dim);

    if (e->groups == 0u) {
        snprintf(line, sizeof(line), "%s", TR(SP_LAST_NONE));
    } else {
        snprintf(line, sizeof(line), TR(SP_LAST),
                 (unsigned)e->last_count,
                 !e->last_valid ? TR(SP_NOT_CLEAN)
                 : e->last_trusted ? TR(SP_TWO_IN_ROW)
                 : e->last_in_order ? TR(SP_IN_ORDER) : TR(SP_NOT_IN_ORDER));
    }
    gfx_text(c, PAD + 12, y0 + 2 * pitch, line, SP_FONT, txt, 1);
    snprintf(line, sizeof(line), TR(SP_STATS), (unsigned)e->groups,
             (unsigned)e->entries, sp_pos(e->out.throttle_pct),
             ui_on_off(e->out.supply_on));
    gfx_text(c, PAD + 12, y0 + 3 * pitch, line, SP_FONT, dim, 1);
    snprintf(line, sizeof(line), TR(SP_CURRENT), (int)e->ma,
             (int)esc_det_floor_ma(&e->det), (unsigned)e->iv_ms);
    gfx_text(c, PAD + 12, y0 + 4 * pitch, line, SP_FONT, dim, 1);
    sp_draw_tone(c, y0 + 5 * pitch, pitch);
    /* After the lines, so a line that reached under it would show as
     * painted over in the fit check. */
    sp_draw_tower(c, false, t->green);

    (void)sp_wrap(c, PAD + 12, HELP_Y, HELP_P, TR(SP_ENDS_RUN),
                  (W - 2 * PAD - 24) / SP_CELL, 2,
                  ui_theme_color(UI_C_TEXT_FAINT));
    ui_button_font(c, pg.write_btn, TR(SP_ABORT),
                   ui_theme_color(UI_C_DANGER), false, true, SP_FONT);
}

/* The red light: a result on screen, until OK, of a run that ended on
 * something not as expected. */
bool sp_red(void)
{
    const esc_stick_t *e = &s.st.run;
    return s.st.shown && e->phase == ESC_STICK_ABORTED
           && esc_stick_reason_is_fault(e->reason);
}

/* The result's count of the selections made, for certain: one cut short
 * while the ESC stored or confirmed it is not counted. */
void sp_result_head(const esc_stick_t *e, char *buf, size_t n)
{
    snprintf(buf, n, TR(SP_MADE_N), esc_stick_made_count(e),
             (unsigned)e->n);
}

/* The changes past the first @p shown, as the result's last line says
 * them: how many, how many made, and the one unsure among them. */
void sp_result_more(const esc_stick_t *e, unsigned shown, char *buf,
                           size_t n)
{
    unsigned made = 0u, unsure = 0u;
    for (unsigned i = shown; i < e->n; ++i) {
        unsure += esc_stick_unsure(e, i) ? 1u : 0u;
        made += (e->done[i] && !esc_stick_unsure(e, i)) ? 1u : 0u;
    }
    if (unsure > 0u) {
        snprintf(buf, n, TR(SP_MORE_UNSURE), (unsigned)(e->n - shown), made,
                 unsure);
    } else {
        snprintf(buf, n, TR(SP_MORE), (unsigned)(e->n - shown), made);
    }
}

DRAWER void sp_draw_result(gfx_canvas_t *c)
{
    const esc_stick_t *e = &s.st.run;
    const bool done = e->phase == ESC_STICK_DONE;
    const gfx_color_t dim = ui_theme_color(UI_C_TEXT_DIM);
    char line[128];
    gfx_text(c, PAD + 12, RUN_Y + 12, TR(SP_RESULT), SP_FONT,
             ui_theme_color(UI_C_ACCENT), 1);
    snprintf(line, sizeof(line), "%s", TR(SP_PH_DONE));
    if (!done) {
        snprintf(line, sizeof(line), TR(SP_ABORTED_WHY),
                 sp_reason_text(e->reason));
    }
    gfx_text(c, PAD + 12, RUN_Y + 38, line, SP_FONT_HEAD,
             done ? ui_theme_color(UI_C_OK) : ui_theme_color(UI_C_DANGER),
             1);
    const int y0 = RUN_L0;
    const int pitch = LINE_P;
    sp_result_head(e, line, sizeof(line));
    gfx_text(c, PAD + 12, y0, line, SP_FONT,
             ui_theme_color(UI_C_TEXT), 1);
    /* Five lines fit; past five, the fifth says how many more. */
    const unsigned shown = (e->n > 5u) ? 4u : e->n;
    for (unsigned i = 0; i < shown; ++i) {
        char what[96];
        sp_change_text(&e->ch[i], what, sizeof(what));
        snprintf(line, sizeof(line), "%s  %s",
                 esc_stick_unsure(e, i) ? TR(SP_MADE_CUT)
                 : e->done[i] ? TR(SP_MADE) : TR(SP_NOT_MADE), what);
        sp_text(c, PAD + 12, y0 + (int)(i + 1u) * pitch, line,
                SP_LINE_CELLS,
                e->done[i] ? ui_theme_color(UI_C_TEXT) : dim);
    }
    if (shown < e->n) {
        sp_result_more(e, shown, line, sizeof(line));
        gfx_text(c, PAD + 12, y0 + 5 * pitch, line, SP_FONT, dim, 1);
    }
    /* Below the changes, two lines: what the profile has a person do once
     * the run is over, and after an abort that what was fitted before
     * the power-up may still be there.  Steps that need more than the two
     * lines are counted there instead, and MANUAL INTERVENTION REQUIRED
     * above -- open by itself when the run ended -- lists every one. */
    const esc_profile_t *p = e->p;
    if (p->manual_count > 0u) {
        ui_button_font(c, s.st.hand_res, TR(SP_HAND_BTN),
                       ui_theme_color(UI_C_DANGER), false, true, SP_FONT);
    }
    int y = y0 + 6 * pitch;
    int room = 2;
    int need = 0;
    if (p->manual_count > 0u && !sp_supply_reads_off()) {
        /* An abort asks the supply off; until it reads off, no hand at the
         * ESC for anything that follows. */
        sp_text(c, PAD + 12, y, TR(SP_HAND_OVER_LIVE), SP_WIDE_CELLS,
                ui_theme_color(UI_C_DANGER));
        y += pitch;
        room--;
    }
    /* The steps after programming: counted first, then drawn, each put
     * into the one buffer as it is needed. */
    char what[256];
    unsigned after = 0u;
    for (unsigned i = 0; p->manual != NULL && i < p->manual_count; ++i) {
        const esc_manual_t *m = &p->manual[i];
        if (m->when == ESC_MANUAL_AFTER_PROGRAMMING && after < ESC_MANUAL_MAX) {
            snprintf(what, sizeof(what), "%s: %s", sp_when_text(m),
                     sp_action(m));
            need += sp_wrap_lines(what, SP_WIDE_CELLS, 2);
            ++after;
        }
    }
    if (need > room && room > 0) {
        snprintf(line, sizeof(line), TR(SP_HAND_AFTER_N), after);
        sp_text(c, PAD + 12, y, line, SP_WIDE_CELLS,
                ui_theme_color(UI_C_WARN));
        y += pitch;
        room--;
    } else {
        unsigned drawn = 0u;
        for (unsigned i = 0; p->manual != NULL && i < p->manual_count
                             && drawn < after; ++i) {
            const esc_manual_t *m = &p->manual[i];
            if (m->when != ESC_MANUAL_AFTER_PROGRAMMING) {
                continue;
            }
            snprintf(what, sizeof(what), "%s: %s", sp_when_text(m),
                     sp_action(m));
            const int n = sp_wrap(c, PAD + 12, y, pitch, what,
                                  SP_WIDE_CELLS, 2,
                                  ui_theme_color(UI_C_WARN));
            y += n * pitch;
            room -= n;
            ++drawn;
        }
    }
    if (!done && room > 0
        && (esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER) > 0u
            || esc_profile_manual_count(p, ESC_MANUAL_AT_POWER_UP) > 0u)) {
        sp_text(c, PAD + 12, y, TR(SP_HAND_UNDO), SP_WIDE_CELLS,
                ui_theme_color(UI_C_WARN));
    }
    sp_draw_tower(c, sp_red(), false);
    if (done) {
        gfx_text(c, PAD + 12, HELP_Y,
                 TR(SP_TONES_1),
                 SP_FONT, dim, 1);
        gfx_text(c, PAD + 12, HELP_Y + HELP_P,
                 TR(SP_TONES_2), SP_FONT, dim, 1);
    } else {
        gfx_text(c, PAD + 12, HELP_Y, sp_reason_help(e->reason),
                 SP_FONT, dim, 1);
        /* The supply went off while the ESC was to confirm: the value may
         * not be stored, and a Kontronik ESC whose step marks it takes
         * that for programming broken off and locks itself. */
        if (esc_stick_cut_short(e)) {
            gfx_text(c, PAD + 12, HELP_Y + HELP_P,
                     esc_stick_lock_risk(e) ? TR(SP_HAND_LOCK)
                                            : TR(SP_HAND_CUT),
                     SP_FONT, ui_theme_color(UI_C_WARN), 1);
        } else {
            gfx_text(c, PAD + 12, HELP_Y + HELP_P, TR(SP_SAFE_NOW),
                     SP_FONT, dim, 1);
        }
    }
    ui_button_font(c, pg.write_btn, "OK", ui_theme_color(UI_C_PANEL_HI),
                   false, true, SP_FONT);
}

/*
 * The warning a run needs before it starts: in the danger colour, over the
 * session's pages, saying what the run does to the ESC and what may happen
 * to a motor on it.
 */
/**
 * @brief A panel over the session's pages, edged and titled in the danger colour:
 *        the warning's, the manual steps' and the prompt's.  The output strip
 *        above it stays in sight.  Its area.
 */
static gfx_rect_t sp_panel(gfx_canvas_t *c, const char *title)
{
    const gfx_rect_t a = { PAD, SP_PANEL_Y, (int16_t)(W - 2 * PAD),
                           SP_PANEL_H };
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    gfx_fill_rect(c, a.x, a.y, a.w, 52, red);
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, red);
    gfx_draw_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2, red);
    gfx_draw_rect(c, a.x + 2, a.y + 2, a.w - 4, a.h - 4, red);
    gfx_text_in(c, (gfx_rect_t){ a.x, (int16_t)(a.y + 8), a.w, 36 },
                title, SP_FONT_HEAD,
                ui_is_light(red) ? ui_theme_color(UI_C_TEXT_ON_LIGHT)
                                 : ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_CENTER);
    return a;
}

/**
 * @brief The before-power-up steps of the profile, under the warning's lines: the
 *        hold that starts the run is the operator's word that they are done.
 *        Then, where the run asks for more on its way, a line that it will.
 */
static DRAWER void sp_draw_warning_hand(gfx_canvas_t *c,
                                        const esc_profile_t *p,
                                        gfx_rect_t a, int y)
{
    const int last = s.st.hold_btn.y - 20;     /* the last line's top */
    const unsigned later = sp_later_steps(p);
    if (esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER) > 0u
        && sp_warn_gated()) {
        /* No hand at the ESC until the supply reads off. */
        (void)sp_wrap(c, a.x + 20, y, 22, TR(SP_WARN_HAND_WAIT), 92, 2,
                      ui_theme_color(UI_C_DANGER));
        return;
    }
    if (esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER) > 0u) {
        gfx_text(c, a.x + 20, y, TR(SP_WARN_HAND), SP_FONT,
                 ui_theme_color(UI_C_WARN), 1);
        y += 22;
        if (!sp_warn_steps_fit(p)) {
            /* Not every step fits: none is shown cut, and ALL STEPS shows
             * them, as HOLD TO RUN needs. */
            char line[128];
            snprintf(line, sizeof(line), TR(SP_WARN_HAND_READ),
                     esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER));
            sp_text(c, a.x + 44, y, line, 90, ui_theme_color(UI_C_TEXT));
            y += 22;
        } else {
            for (unsigned i = 0; i < p->manual_count; ++i) {
                const esc_manual_t *m = &p->manual[i];
                if (m->when == ESC_MANUAL_BEFORE_POWER) {
                    y += 22 * sp_wrap(c, a.x + 44, y, 22, sp_action(m), 90,
                                      2, ui_theme_color(UI_C_TEXT));
                }
            }
        }
    }
    if (later > 0u && y <= last) {
        gfx_text(c, a.x + 20, y, TR(SP_WARN_HAND_LATER), SP_FONT,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
    }
}

DRAWER void sp_draw_warning(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    const gfx_rect_t a = sp_panel(c, TR(SP_WARN_TITLE));
    static const ui_text_id_t k_lines[] = {
        TX_SP_WARN_1, TX_SP_WARN_2, TX_SP_WARN_3, TX_SP_WARN_4,
        TX_SP_WARN_5,
    };
    const int n = (int)(sizeof(k_lines) / sizeof(k_lines[0]));
    for (int i = 0; i < n; ++i) {
        gfx_text(c, a.x + 20, a.y + 68 + i * 22, ui_tr(k_lines[i]),
                 SP_FONT,
                 ui_theme_color(UI_C_TEXT), 1);
    }
    char line[128];
    /* A profile with manual steps gives up the blank line above its name
     * to the steps below. */
    const int top = a.y + 68 + ((t->p->manual_count > 0u) ? n : n + 1) * 22;
    snprintf(line, sizeof(line), "%s %s", t->p->brand, t->p->family);
    sp_text(c, a.x + 20, top, line, 90, ui_theme_color(UI_C_ACCENT));
    uint32_t mv, ma;
    sp_supply(&mv, &ma);
    size_t picked = 0;
    esc_stick_change_t ch[ESC_STICK_MAX_CHANGES];
    (void)sp_changes(ch, &picked);
    snprintf(line, sizeof(line),
             (picked == 1u) ? TR(SP_WARN_SUPPLY) : TR(SP_WARN_SUPPLY_N),
             (unsigned)(mv / 1000u), (unsigned)(mv % 1000u / 10u),
             (unsigned)(ma / 1000u), (unsigned)(ma % 1000u / 10u),
             (unsigned)picked);
    gfx_text(c, a.x + 20, top + 22, line, SP_FONT,
             ui_theme_color(UI_C_VOLT), 1);
    /* Where the stick is when the supply comes on, beside the supply,
     * when it is not MIN: every position the run's power-ups take. */
    char at[40];
    if (sp_power_up_text(t->p, ch, picked < ESC_STICK_MAX_CHANGES
                                       ? picked : ESC_STICK_MAX_CHANGES,
                         at, sizeof(at))) {
        gfx_text(c, a.x + 20 + gfx_text_cells(line) * SP_CELL + 24, top + 22,
                 at,
                 SP_FONT, ui_theme_color(UI_C_WARN), 1);
    }
    gfx_text(c, a.x + 20, top + 44, TR(SP_WARN_UNVERIFIED),
             SP_FONT, ui_theme_color(UI_C_TEXT_DIM), 1);
    sp_draw_warning_hand(c, t->p, a, top + 66);
    ui_button_font(c, t->hold_btn, TR(SP_HOLD_TO_RUN),
                   ui_hold_fill(ui_theme_color(UI_C_PANEL_SUNK), red,
                                t->hold.held_s),
                   t->warn_down, !sp_hold_blocked(), SP_FONT);
    if (!sp_warn_steps_fit(t->p)) {
        ui_button_font(c, t->steps_btn, TR(SP_STEPS_BTN),
                       t->warn_read ? ui_theme_color(UI_C_PANEL_HI) : red,
                       false, true, SP_FONT);
    }
    ui_button_font(c, t->cancel_btn, TR(CANCEL),
                   ui_theme_color(UI_C_PANEL_SUNK), false, true, SP_FONT);
}

/*
 * The manual steps of one profile: when each is due and what it is, and
 * whether a run asks for them.  Opened by MANUAL INTERVENTION REQUIRED,
 * by the first opening of the profile, or by a tap on a row the bench does
 * not run.
 */
DRAWER void sp_draw_hand(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_profile_t *p = t->hand_p;
    const gfx_rect_t a = sp_panel(c, TR(SP_HAND_BTN));
    char line[256];
    snprintf(line, sizeof(line), "%s %s", p->brand, p->family);
    sp_text(c, a.x + 20, a.y + 64, line, 92, ui_theme_color(UI_C_ACCENT));
    /* ESC_MANUAL_MAX steps of a label and two lines each fit above the
     * buttons at a pitch of LINE_P. */
    int y = a.y + 90;
    for (unsigned i = 0; i < p->manual_count; ++i) {
        const esc_manual_t *m = &p->manual[i];
        snprintf(line, sizeof(line), "%u  %s", i + 1u, sp_when_text(m));
        gfx_text(c, a.x + 20, y, line, SP_FONT,
                 ui_theme_color(UI_C_WARN), 1);
        y += LINE_P;
        y += LINE_P * sp_wrap(c, a.x + 44, y, LINE_P, sp_action(m), 90, 2,
                              ui_theme_color(UI_C_TEXT));
        y += 4;
    }
    char why[64];
    const char *no = sp_model_why(p, t->hand_model, why, sizeof(why));
    if (t->shown && t->p == p && !sp_supply_reads_off()) {
        snprintf(line, sizeof(line), "%s", TR(SP_HAND_OVER_LIVE));
    } else if (t->shown && t->p == p) {
        snprintf(line, sizeof(line), "%s", TR(SP_HAND_OVER));
    } else if (no == NULL) {
        snprintf(line, sizeof(line), "%s", TR(SP_HAND_ASKS));
    } else {
        snprintf(line, sizeof(line), TR(SP_HAND_NOT_RUN), no);
    }
    sp_wrap(c, a.x + 20, t->cancel_btn.y + 4, LINE_P, line,
            (t->cancel_btn.x - a.x - 36) / SP_CELL, 2,
            (t->shown && t->p == p && !sp_supply_reads_off())
                ? ui_theme_color(UI_C_DANGER)
                : ui_theme_color(UI_C_TEXT_DIM));
    ui_button_font(c, t->cancel_btn, "OK", ui_theme_color(UI_C_PANEL_HI),
                   false, true, SP_FONT);
}

/*
 * A run waiting for a manual step: what to do, where the supply and the
 * stick are, and how long it waits.  DONE goes on, ABORT ends the run;
 * STOP in the band ends it too.  DONE is dark for ESC_STICK_HAND_MIN_MS
 * after the step is asked.
 */
/* What no DONE does, as the prompt says it (sp_draw_prompt(), which
 * spells the choice out for the format check): before the supply goes
 * off, that the mode may not be stored, or that the ESC may lock while any
 * of the profile's steps before the power-off is still to confirm. */
const char *sp_left_text(const esc_stick_t *e)
{
    if (e->phase != ESC_STICK_HAND_END) {
        return TR(SP_PROMPT_LEFT);
    }
    return esc_stick_end_locks(e) ? TR(SP_PROMPT_LEFT_END)
                                  : TR(SP_PROMPT_LEFT_CUT);
}

DRAWER void sp_draw_prompt(gfx_canvas_t *c)
{
    const stick_t *t = &s.st;
    const esc_stick_t *e = &t->run;
    const esc_manual_t *m = esc_stick_hand(e);
    const gfx_rect_t a = sp_panel(c, TR(SP_PROMPT_TITLE));
    char line[256];
    snprintf(line, sizeof(line), "%s %s", e->p->brand, e->p->family);
    sp_text(c, a.x + 20, a.y + 64, line, 92, ui_theme_color(UI_C_ACCENT));
    gfx_text(c, a.x + 20, a.y + 92, sp_when_text(m), SP_FONT,
             ui_theme_color(UI_C_WARN), 1);
    int y = a.y + 118;
    y += 22 * sp_wrap(c, a.x + 20, y, 22, sp_action(m), 92, 3,
                      ui_theme_color(UI_C_TEXT));
    y += 12;
    const char *pos = sp_pos(e->out.throttle_pct);
    if (e->hand_menu) {
        snprintf(line, sizeof(line), TR(SP_PROMPT_LISTEN), pos);
    } else if (e->phase == ESC_STICK_HAND_ON) {
        snprintf(line, sizeof(line), TR(SP_PROMPT_ON), pos);
    } else if (e->phase == ESC_STICK_HAND_END) {
        snprintf(line, sizeof(line), TR(SP_PROMPT_END), pos);
    } else if (m->when == ESC_MANUAL_AT_POWER_UP) {
        snprintf(line, sizeof(line), "%s", TR(SP_PROMPT_AT_POWER));
    } else {
        snprintf(line, sizeof(line), TR(SP_PROMPT_OFF), pos);
    }
    sp_text(c, a.x + 20, y, line, 92, ui_theme_color(UI_C_TEXT_DIM));
    y += 22;
    if (m->when == ESC_MANUAL_AT_POWER_UP && m->hold_ms != 0u) {
        snprintf(line, sizeof(line), TR(SP_PROMPT_HOLD),
                 (unsigned)(m->hold_ms / 1000u),
                 (unsigned)(m->hold_ms % 1000u / 100u));
        sp_text(c, a.x + 20, y, line, 92, ui_theme_color(UI_C_TEXT_DIM));
        y += 22;
    }
    if (e->hand_menu) {
        sp_text(c, a.x + 20, y, TR(SP_PROMPT_HEARD), 92,
                ui_theme_color(UI_C_TEXT_DIM));
        y += 22;
    }
    /* Before the supply goes off, an end switches it off under the ESC's
     * confirmation: said here, as the result says it after. */
    snprintf(line, sizeof(line),
             (e->phase != ESC_STICK_HAND_END) ? TR(SP_PROMPT_LEFT)
             : esc_stick_end_locks(e)         ? TR(SP_PROMPT_LEFT_END)
                                              : TR(SP_PROMPT_LEFT_CUT),
             (unsigned)((esc_stick_hand_left_ms(e) + 999u) / 1000u));
    (void)sp_wrap(c, a.x + 20, y, 22, line, 92, 2,
                  ui_theme_color(UI_C_TEXT_FAINT));
    const bool ready = esc_stick_hand_ready(e);
    ui_button_font(c, t->hold_btn, TR(SP_HAND_DONE),
                   ready ? ui_theme_color(UI_C_OK)
                         : ui_theme_color(UI_C_PANEL_SUNK), false, ready,
                   SP_FONT);
    ui_button_font(c, t->cancel_btn, TR(SP_ABORT),
                   ui_theme_color(UI_C_DANGER), false, true, SP_FONT);
}
