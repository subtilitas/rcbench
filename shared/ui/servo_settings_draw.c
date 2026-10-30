/**
 * @file
 * @brief The drawing of the SERVO screen's pages of rows and the panels
 *        they open: SETTINGS' pages and a run's settings with their notes
 *        and the save line, the list, the profile warning, the HV warning
 *        and SUPPLY's question about a live output.
 *
 * What a press on them does, and the rows they draw, are
 * servo_settings_panel.c; the state and the layout they share are
 * servo_screen_int.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "servo_limrun.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_tabs.h"
#include "ui_textkey.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_int.h"

/**
 * @brief Whether the settings reached the card, along the bottom of a page.
 * @param c the canvas
 */
void draw_save_line(gfx_canvas_t *c)
{
    static const ui_text_id_t k_save[] = {
        TX_SUP_SAVED, TX_SUP_SAVE_WAITING, TX_SUP_NOT_SAVED,
        TX_SUP_SETUP_NOT_SAVED,
    };
    const gfx_rect_t r = save_line_rect();
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    const uint8_t st = save_state();
    gfx_text(c, r.x, r.y, ui_tr(k_save[st]), SV_FONT_LABEL,
             (st == 2u) ? ui_theme_color(UI_C_WARN)
                        : ui_theme_color(UI_C_TEXT_DIM), 1);
}

/**
 * @brief Whether a row is drawn faint: a test setting the length rule
 *        leaves unused, a setting under its lock, a limit run's row it
 *        cannot take now.
 * @param r the row
 * @return true to draw it faint
 */
static bool row_unused(const ov_row_t *r)
{
    if (r->kind == R_ENC_CENTRE) {
        return !settings_get_bool(SET_ENC_EN);
    }
    if (r->kind == R_LIM_MODE) {
        return servo_limrun_running(&s.lim) || lim_mode_why() != NULL;
    }
    if (r->kind == R_LIM_PULSES) {
        return servo_limrun_running(&s.lim) || lim_write_why() != NULL;
    }
    if (r->kind != R_SETTING) {
        return false;
    }
    if (target_locked(r)) {
        return true;
    }
    const int by = settings_get_int(SET_SERVO_LEN_BY);
    return (r->id == SET_SERVO_LEN_S && by != 0)
           || (r->id == SET_SERVO_LEN_MOVES && by == 0)
           || ((r->id == SET_SERVO_STEP_74 || r->id == SET_SERVO_STEP_84)
               && !s.test_hv);
}

/**
 * @brief A row's value as text.
 * @param r the row
 * @param buf the text
 * @param n its size
 */
static void row_value(const ov_row_t *r, char *buf, size_t n)
{
    switch (r->kind) {
    case R_TYPE:    snprintf(buf, n, "%s", type()->name); return;
    case R_RATE:    snprintf(buf, n, "%uHz", (unsigned)s.frame_hz); return;
    case R_MIN:     snprintf(buf, n, "%uus", (unsigned)s.min_us); return;
    case R_CENTRE:  snprintf(buf, n, "%uus", (unsigned)s.centre_us); return;
    case R_MAX:     snprintf(buf, n, "%uus", (unsigned)s.max_us); return;
    case R_TRIM:    snprintf(buf, n, "%+d", (int)s.trim_us); return;
    case R_TRAVEL:  snprintf(buf, n, "+/-%d", (int)s.travel_deg); return;
    case R_REVERSE: snprintf(buf, n, "%s", ui_on_off(s.reverse)); return;
    case R_TEXT:    snprintf(buf, n, "%s", settings_text(SET_TEXT_DUT_NAME));
                    return;
    case R_HV:      snprintf(buf, n, "%s", ui_on_off(s.test_hv)); return;
    case R_LIM_MODE:
        snprintf(buf, n, "%s",
                 lim_programs() ? TR(SV_LIM_PROGRAM) : TR(SV_LIM_SEARCH));
        return;
    case R_LIM_PULSES:
        snprintf(buf, n, "%s",
                 s.lim_pulses ? TR(SV_LIM_PULSES_STD) : TR(SV_LIM_PULSES_KEEP));
        return;
    case R_ENC_CENTRE:
        snprintf(buf, n, "%d", settings_get_int(SET_ENC_CENTRE));
        return;
    default:
        break;
    }
    const setting_def_t *d = settings_def(r->id);
    const float v = settings_get(r->id);
    const bool limit = (r->id == SET_SERVO_IDLE_MAX
                        || r->id == SET_SERVO_HOLD_MAX
                        || r->id == SET_SERVO_TRAVEL_MAX_MS);
    if (d->type == SET_TYPE_BOOL) {
        snprintf(buf, n, "%s", ui_on_off(v != 0.0f));
    } else if (d->type == SET_TYPE_ENUM) {
        snprintf(buf, n, "%s",
                 ui_setting_option(r->id, settings_get_int(r->id)));
    } else if (limit && !(v > 0.0f)) {
        snprintf(buf, n, "%s", TR(OFF));
    } else {
        snprintf(buf, n, "%.*f%s", decimals_of(r->id), (double)v, d->unit);
    }
}

/**
 * @brief A page's note, NOTE_PITCH px a line, in the faint colour.
 * @param c the canvas
 * @param x its left edge, px
 * @param y its first line's top, px
 * @param lines the lines
 * @param count how many
 */
static void draw_note(gfx_canvas_t *c, int x, int y, const char *const *lines,
                      int count)
{
    for (int i = 0; i < count; ++i) {
        gfx_text(c, x, y + i * NOTE_PITCH, lines[i], SV_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
}

bool servo_in_force_note(char *l1, char *l2, size_t size, uint16_t hz,
                         uint8_t target, uint8_t gpio, bool pwm, int setting)
{
    if (l1 == NULL || l2 == NULL || size == 0u) {
        return false;
    }
    /* No link: the IOmcu may have restarted since the rate landed,
     * and a restart puts every pin back at its own rate, so no rate is
     * named.  The link-up resets the rate before a target is known. */
    if (target == SERVO_SCREEN_TARGET_NO_LINK) {
        snprintf(l1, size, "%s", TR(SV_IN_FORCE_NO_LINK));
        snprintf(l2, size, "%s", TR(SV_IN_FORCE_2));
        return true;
    }
    /* The page not known: the rate was acknowledged, where it runs is not
     * known. */
    if (target == SERVO_SCREEN_TARGET_UNKNOWN) {
        snprintf(l1, size, TR(SV_IN_FORCE_UNKNOWN), (unsigned)hz);
        snprintf(l2, size, "%s", TR(SV_IN_FORCE_2));
        return true;
    }
    /* The target the IOmcu holds, not the setting: the setting waits
     * for a bench at rest, and PROG PORT with no pin bound to the port
     * leaves the standard outputs the target. */
    const bool other = setting < 0 || (unsigned)setting != target;
    bool warn = other;
    if (target == 0u) {
        snprintf(l1, size, TR(SV_IN_FORCE_OUT), (unsigned)hz);
    } else if (gpio == SERVO_SCREEN_NO_PIN) {
        snprintf(l1, size, TR(SV_IN_FORCE_PORT), (unsigned)hz);
        warn = true;
    } else {
        snprintf(l1, size, TR(SV_IN_FORCE_PORT_PIN), (unsigned)gpio,
                 (unsigned)hz);
    }
    if (other) {
        snprintf(l2, size, "%s", TR(SV_IN_FORCE_NOT_SET));
    } else if (target != 0u && !pwm) {
        snprintf(l2, size, "%s", TR(SV_IN_FORCE_PWM_OFF));
        warn = true;
    } else {
        snprintf(l2, size, "%s", TR(SV_IN_FORCE_2));
    }
    return warn;
}

/** @brief A line of a page's note: the page less 10px each side, 60
 *         cells. */
#define NOTE_CELLS ((OV_W - 20) / SV_CELL)

/**
 * @brief Whether the frame rate reached the pins, in two lines or more.
 * @param c the canvas
 * @param x the note's left edge, px
 * @param y its first line's top, px
 */
static void draw_rate_note(gfx_canvas_t *c, int x, int y)
{
    const servo_rate_state_t st = (s.rate_hz == s.frame_hz
                                   || s.rate_st == SERVO_RATE_UNSUPPORTED)
                                      ? s.rate_st
                                      : SERVO_RATE_UNSENT;
    char l1[96], l2[96];
    bool warn = true;
    switch (st) {
    case SERVO_RATE_IN_FORCE:
        warn = servo_in_force_note(l1, l2, sizeof(l1), s.frame_hz,
                                   s.drive_target, s.drive_pin, s.drive_pwm,
                                   settings_get_int(SET_SERVO_TARGET));
        break;
    case SERVO_RATE_IN_FORCE_SURFACES:
        /* With no link the same "not known" line as from 4.19. */
        if (s.drive_target == SERVO_SCREEN_TARGET_NO_LINK) {
            warn = servo_in_force_note(l1, l2, sizeof(l1), s.frame_hz,
                                       s.drive_target, s.drive_pin,
                                       s.drive_pwm,
                                       settings_get_int(SET_SERVO_TARGET));
            break;
        }
        snprintf(l1, sizeof(l1), TR(SV_IN_FORCE_SURFACES_1),
                 (unsigned)s.frame_hz);
        snprintf(l2, sizeof(l2), "%s", TR(SV_IN_FORCE_2));
        warn = false;
        break;
    case SERVO_RATE_REFUSED:
        snprintf(l1, sizeof(l1), "%s", TR(SV_REFUSED_1));
        snprintf(l2, sizeof(l2), "%s", TR(SV_REFUSED_2));
        break;
    case SERVO_RATE_UNSUPPORTED:
        snprintf(l1, sizeof(l1), "%s", TR(SV_UNSUPPORTED_1));
        snprintf(l2, sizeof(l2), "%s", TR(SV_UNSUPPORTED_2));
        break;
    case SERVO_RATE_UNSENT:
    default:
        snprintf(l1, sizeof(l1), "%s", TR(SV_UNSENT_1));
        snprintf(l2, sizeof(l2), "%s", TR(SV_UNSENT_2));
        warn = false;
        break;
    }
    const gfx_color_t col = warn ? ui_theme_color(UI_C_WARN)
                                 : ui_theme_color(UI_C_TEXT_FAINT);
    /* Each line wraps onto the next where it is longer than the page's
     * 60 cells. */
    const int n1 = wrap_text(c, x, y, NOTE_PITCH, l1, NOTE_CELLS, col,
                             SV_FONT_LABEL);
    (void)wrap_text(c, x, y + NOTE_PITCH * n1, NOTE_PITCH, l2, NOTE_CELLS,
                    col, SV_FONT_LABEL);
}

/**
 * @brief A page of rows in the page area: SETTINGS' page under its tabs,
 *        or a run's settings under the run's name.  Its notes, and the
 *        save line.
 * @param c the canvas
 */
void draw_rows_page(gfx_canvas_t *c)
{
    const int page = rows_page();
    const gfx_rect_t a = page_area();
    gfx_fill_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2,
                  ui_theme_color(UI_C_PANEL));
    if (page < PG_COUNT) {
        ui_tabs_render(&s.tabs, c);
    } else {
        gfx_text(c, OV_X + 10, CARD_Y + RUN_PAGE_NAME_DY,
                 (page == PG_AUTO) ? TR(SV_AUTO_TEST) : TR(SV_ROW_LIM_MODE),
                 SV_FONT_LABEL, ui_theme_color(UI_C_ACCENT), 1);
        ui_button_font(c, run_close_rect(), TR(SUP_CLOSE),
                       ui_theme_color(UI_C_PANEL_HI),
                       s.ov_have && s.ov_pressed == OP_RUN_CLOSE, true,
                       SV_FONT_LABEL);
    }
    for (int i = 0; i < k_row_count; ++i) {
        const ov_row_t *r = &k_rows[i];
        if (r->page != page || row_hidden(r)) {
            continue;
        }
        const gfx_rect_t rr = row_rect(i);
        const gfx_rect_t vr = value_rect(i);
        const bool faint = row_unused(r);
        /* The label left in its row, or over its value on a run's page. */
        gfx_text(c, rr.x, (vr.y == rr.y) ? rr.y + 14 : rr.y,
                 ui_tr(r->label), SV_FONT_LABEL,
                 faint ? ui_theme_color(UI_C_TEXT_FAINT)
                       : ui_theme_color(UI_C_TEXT_DIM), 1);
        char v[32];
        row_value(r, v, sizeof(v));
        const bool pressed = s.ov_have && s.ov_pressed == OP_ROW
                             && s.ov_row == i;
        if (r->kind == R_TRIM) {
            gfx_fill_round_rect(c, vr.x, vr.y, vr.w, vr.h, UI_R_CTL,
                                ui_theme_color(UI_C_PANEL_SUNK));
            gfx_text_in(c, vr, v, SV_FONT_LABEL, ui_theme_color(UI_C_TEXT),
                        1, GFX_ALIGN_CENTER);
            ui_button_font(c, trim_rect(i, false), "-",
                           ui_theme_color(UI_C_PANEL_HI),
                           s.ov_have && s.ov_pressed == OP_TRIM_DN, true,
                           SV_FONT_LABEL);
            ui_button_font(c, trim_rect(i, true), "+",
                           ui_theme_color(UI_C_PANEL_HI),
                           s.ov_have && s.ov_pressed == OP_TRIM_UP, true,
                           SV_FONT_LABEL);
            continue;
        }
        /* The type and the rate in force in the danger colour when they
         * are ones that can destroy a servo not made for them. */
        const bool danger = ((r->kind == R_TYPE || r->kind == R_RATE)
                             && in_force_dangerous())
                            || (r->kind == R_HV && s.test_hv);
        ui_button_font(c, vr, v,
                       danger ? ui_theme_color(UI_C_DANGER)
                              : ui_theme_color(UI_C_PANEL_SUNK),
                       pressed, !faint, SV_FONT_LABEL);
    }

    const int nx = OV_X + 10;
    if (page == PG_OUTPUT) {
        char l1[96], l2[96];
        snprintf(l1, sizeof(l1), TR(SV_FASTEST),
                 (unsigned)max_rate_for(s.type, cmd_top()),
                 type()->heli ? "0.5ms" : "1ms");
        snprintf(l2, sizeof(l2), "%s", TR(SV_AT_START));
        const char *const lines[] = { l1, l2 };
        draw_note(c, nx, OV_NOTE_Y, lines, 2);
        draw_rate_note(c, nx, OV_NOTE_Y + 2 * NOTE_PITCH);
    } else if (page == PG_AUTO) {
        draw_test_stall(c, nx, RL_NOTE_Y);
    } else if (page == PG_LIMRUN) {
        draw_lim_mode_note(c, nx, RL_ROW0 + 2 * RL_PITCH + 6);
    } else if (page == PG_DUT) {
        const char *const lines[] = {
            TR(SV_DUT_NOTE), TR(SV_ENC_NOTE_1), TR(SV_ENC_NOTE_2),
        };
        const int y = OV_ROW0 + 3 * OV_PITCH + 6;
        draw_note(c, nx, y, lines, 3);
        draw_caps_line(c, nx, y + 4 * NOTE_PITCH);
    }
    draw_save_line(c);
}

/**
 * @brief The list: its title, its choices, the one in force in the accent,
 *        and CANCEL.
 * @param c the canvas
 */
static void draw_choice(gfx_canvas_t *c)
{
    gfx_text(c, OV_X + 10, OV_Y + 18, s.ch.title, SV_FONT_LABEL,
             ui_theme_color(UI_C_TEXT), 1);
    for (int k = 0; k < s.ch.count; ++k) {
        bool current = false;
        if (s.ch.target == CH_TYPE) {
            current = (s.ch.values[k] == s.type);
        } else if (s.ch.target == CH_RATE) {
            current = (s.ch.values[k] == (int)s.frame_hz);
        } else if (s.ch.target == CH_ENUM) {
            current = (s.ch.values[k] == settings_get_int(s.ch.id));
        }
        ui_button_font(c, choice_rect(k), s.ch.labels[k],
                       current ? ui_theme_color(UI_C_ACCENT)
                               : ui_theme_color(UI_C_PANEL_SUNK),
                       s.ov_have && s.ov_pressed == OP_CHOICE
                           && s.ov_row == k,
                       true, SV_FONT_LABEL);
    }
    ui_button_font(c, choice_cancel_rect(), TR(CANCEL),
                   ui_theme_color(UI_C_PANEL_SUNK),
                   s.ov_have && s.ov_pressed == OP_CHOICE_CANCEL, true,
                   SV_FONT_LABEL);
}

/** @brief HOLD TO APPLY's fill: sunk, fading to the danger red across its
 *         hold.  @return the fill */
static gfx_color_t warn_fill(void)
{
    return ui_hold_fill(ui_theme_color(UI_C_PANEL_SUNK),
                        ui_theme_color(UI_C_DANGER), s.warn.hold.held_s);
}

/**
 * @brief The profile warning's HOLD TO APPLY.
 * @param c the canvas
 */
void draw_warn_apply(gfx_canvas_t *c)
{
    ui_button_font(c, warn_apply_rect(), TR(SV_HOLD_TO_APPLY), warn_fill(),
                   s.warn.down, true, SV_FONT_LABEL);
}

/** @brief A line of a warning: the left column less 20px left and 12px
 *         right, in cells of SV_FONT_BODY, 49. */
#define WARN_CELLS ((OV_W - 32) / 12)
/** @brief A warning's title, what it is about, and its first line. */
#define WARN_TITLE_DY 20
#define WARN_WHAT_DY  76  /**< What it is about, under the title, px. */
#define WARN_LINE_DY  120 /**< Its first line of prose, px. */
#define WARN_PITCH    28  /**< Its lines' pitch in SV_FONT_BODY, px. */

/**
 * @brief The red frame of a warning, 3px, around the left column.
 * @param c the canvas
 * @param a the left column
 * @param red the frame's colour
 */
static void warn_frame(gfx_canvas_t *c, gfx_rect_t a, gfx_color_t red)
{
    for (int k = 0; k < 3; ++k) {
        gfx_draw_rect(c, a.x + k, a.y + k, a.w - 2 * k, a.h - 2 * k, red);
    }
}

/**
 * @brief The warning a type or a frame rate that can destroy a servo needs
 *        before it is applied: in the danger colour, saying what it is and
 *        what it does to a servo not made for it.
 * @param c the canvas
 */
static void draw_warning(gfx_canvas_t *c)
{
    const gfx_rect_t a = overlay_area();
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    warn_frame(c, a, red);
    gfx_text(c, a.x + 20, a.y + WARN_TITLE_DY, TR(SV_DESTROY), SV_FONT_HEAD,
             red, 1);
    const servo_type_t *t = &k_types[s.warn.type];
    char what[80];
    snprintf(what, sizeof(what), TR(SV_DESTROY_WHAT),
             t->name, (unsigned)s.warn.hz,
             (unsigned)((s.warn.type == s.type) ? s.min_us : t->min_us),
             (unsigned)((s.warn.type == s.type) ? s.max_us : t->max_us));
    gfx_text(c, a.x + 20, a.y + WARN_WHAT_DY, what, SV_FONT_BODY,
             ui_theme_color(UI_C_TEXT), 1);
    const char *const lines[] = {
        TR(SV_DESTROY_1), TR(SV_DESTROY_2), TR(SV_DESTROY_3),
        TR(SV_DESTROY_4), TR(SV_DESTROY_5),
    };
    int y = a.y + WARN_LINE_DY;
    for (int i = 0; i < 5; ++i) {
        y += WARN_PITCH * wrap_text(c, a.x + 20, y, WARN_PITCH, lines[i],
                                    WARN_CELLS,
                                    (i < 3) ? ui_theme_color(UI_C_TEXT)
                                            : ui_theme_color(UI_C_TEXT_DIM),
                                    SV_FONT_BODY);
    }
    draw_warn_apply(c);
    ui_button_font(c, warn_cancel_rect(), TR(CANCEL),
                   ui_theme_color(UI_C_PANEL_SUNK),
                   s.ov_have && s.ov_pressed == OP_WARN_CANCEL, true,
                   SV_FONT_LABEL);
}

/**
 * @brief The HV warning's HOLD TO APPLY.
 * @param c the canvas
 */
void draw_ask_apply(gfx_canvas_t *c)
{
    ui_button_font(c, warn_apply_rect(), TR(SV_HOLD_TO_APPLY),
                   ui_hold_fill(ui_theme_color(UI_C_PANEL_SUNK),
                                ui_theme_color(UI_C_DANGER),
                                s.ask.hold.held_s),
                   s.ask.down, true, SV_FONT_LABEL);
}

/**
 * @brief The HV warning: a voltage raised past a standard servo's rating,
 *        in the danger colour of the profile warning, saying what it does
 *        to a servo not rated for it.
 * @param c the canvas
 */
static void draw_hv(gfx_canvas_t *c)
{
    const gfx_rect_t a = overlay_area();
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    warn_frame(c, a, red);
    gfx_text(c, a.x + 20, a.y + WARN_TITLE_DY, TR(SV_HV_ONLY), SV_FONT_HEAD,
             red, 1);
    char what[64];
    if (s.ask.purpose == ASK_ON) {
        snprintf(what, sizeof(what), TR(SV_HV_ON_AT),
                 (double)supply_screen_set_v());
    } else if (s.ask.purpose == ASK_TEST) {
        snprintf(what, sizeof(what), TR(SV_HV_STEPS),
                 (double)s.ask.v);
    } else {
        snprintf(what, sizeof(what), TR(SV_HV_VOLTAGE),
                 (double)supply_screen_set_v(), (double)s.ask.v);
    }
    gfx_text(c, a.x + 20, a.y + WARN_WHAT_DY, what, SV_FONT_BODY,
             ui_theme_color(UI_C_VOLT), 1);
    const char *const lines[] = {
        TR(SV_HV_1), TR(SV_HV_2), TR(SV_HV_3), TR(SV_HV_4), TR(SV_HV_5),
    };
    int y = a.y + WARN_LINE_DY;
    for (int i = 0; i < 5; ++i) {
        y += WARN_PITCH * wrap_text(c, a.x + 20, y, WARN_PITCH, lines[i],
                                    WARN_CELLS,
                                    (i < 4) ? ui_theme_color(UI_C_TEXT)
                                            : ui_theme_color(UI_C_TEXT_DIM),
                                    SV_FONT_BODY);
    }
    if ((s.ask.purpose == ASK_SET || s.ask.purpose == ASK_RESTORE)
        && supply_screen_output_on()) {
        (void)wrap_text(c, a.x + 20, y + 8, WARN_PITCH, TR(SV_HV_LIVE),
                        WARN_CELLS, ui_theme_color(UI_C_WARN), SV_FONT_BODY);
    }
    draw_ask_apply(c);
    ui_button_font(c, warn_cancel_rect(), TR(CANCEL),
                   ui_theme_color(UI_C_PANEL_SUNK),
                   s.ov_have && s.ov_pressed == OP_ASK_CANCEL, true,
                   SV_FONT_LABEL);
}

/**
 * @brief The set points the question changes, each as `was -> now` on a
 *        line of its own; one that stays is not drawn.
 * @param c the canvas
 * @param x the lines' left edge
 * @param y the first line's top
 * @return the top of the line after the last
 */
static int draw_changes(gfx_canvas_t *c, int x, int y)
{
    const struct { const char *label; float was, now; const char *unit;
                   gfx_color_t col; } k[] = {
        { TR(SUP_VOLTAGE), supply_screen_set_v(), s.ask.v, "V",
          ui_theme_color(UI_C_VOLT) },
        { TR(SUP_CURRENT_LIMIT), supply_screen_set_i(), s.ask.i, "A",
          ui_theme_color(UI_C_CURR) },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        if (k[i].was == k[i].now) {
            continue;
        }
        gfx_text(c, x, y + 8, k[i].label, SV_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        char line[48];
        snprintf(line, sizeof(line), "%.2f -> %.2f%s", (double)k[i].was,
                 (double)k[i].now, k[i].unit);
        gfx_text(c, x + 180, y, line, SV_FONT_HEAD, k[i].col, 1);
        y += 56;
    }
    return y;
}

/**
 * @brief The question after a programming port let go of a live output:
 *        the operator's set points from before the session back onto it,
 *        YES or NO.  The change rows say what YES does; the last line what
 *        NO does.
 * @param c the canvas
 */
static void draw_restore(gfx_canvas_t *c)
{
    const gfx_rect_t a = overlay_area();
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_WARN));
    gfx_text(c, a.x + 20, a.y + WARN_TITLE_DY, TR(SUP_OUTPUT_IS_ON),
             SV_FONT_HEAD, ui_theme_color(UI_C_WARN), 1);
    char what[112];
    snprintf(what, sizeof(what), TR(SV_RESTORE_ASK), (double)s.ask.v,
             (double)s.ask.i);
    int y = a.y + WARN_WHAT_DY;
    y += WARN_PITCH * wrap_text(c, a.x + 20, y, WARN_PITCH, what, WARN_CELLS,
                                ui_theme_color(UI_C_TEXT), SV_FONT_BODY);
    y = draw_changes(c, a.x + 20, y + 16);
    y += 8;
    y += WARN_PITCH * wrap_text(c, a.x + 20, y, WARN_PITCH, TR(SUP_ASK_WHY),
                                WARN_CELLS, ui_theme_color(UI_C_TEXT_DIM),
                                SV_FONT_BODY);
    (void)wrap_text(c, a.x + 20, y + 8, WARN_PITCH, TR(SV_RESTORE_NO),
                    WARN_CELLS, ui_theme_color(UI_C_TEXT_DIM), SV_FONT_BODY);
    ui_button_font(c, warn_apply_rect(), TR(SV_ASK_YES),
                   ui_theme_color(UI_C_WARN),
                   s.ov_have && s.ov_pressed == OP_ASK_APPLY, true,
                   SV_FONT_LABEL);
    ui_button_font(c, warn_cancel_rect(), TR(SV_ASK_NO),
                   ui_theme_color(UI_C_PANEL_SUNK),
                   s.ov_have && s.ov_pressed == OP_ASK_CANCEL, true,
                   SV_FONT_LABEL);
}

/**
 * @brief SUPPLY's question before a set point changes a live output, with
 *        its words: the change reaches the load at once.  The HV warning
 *        where the voltage passes a standard servo's rating, and the
 *        question after a programming port let go of a live output.
 * @param c the canvas
 */
static void draw_ask(gfx_canvas_t *c)
{
    const gfx_rect_t a = overlay_area();
    if (s.ask.hv) {
        draw_hv(c);
        return;
    }
    if (s.ask.purpose == ASK_RESTORE) {
        draw_restore(c);
        return;
    }
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_WARN));
    gfx_text(c, a.x + 20, a.y + WARN_TITLE_DY, TR(SUP_OUTPUT_IS_ON),
             SV_FONT_HEAD, ui_theme_color(UI_C_WARN), 1);
    gfx_text(c, a.x + 20, a.y + WARN_WHAT_DY, TR(SUP_ASK_WHY),
             SV_FONT_BODY, ui_theme_color(UI_C_TEXT_DIM), 1);
    (void)draw_changes(c, a.x + 20, a.y + WARN_LINE_DY + 10);
    ui_button_font(c, warn_apply_rect(), TR(SUP_APPLY),
                   ui_theme_color(UI_C_WARN),
                   s.ov_have && s.ov_pressed == OP_ASK_APPLY, true,
                   SV_FONT_LABEL);
    ui_button_font(c, warn_cancel_rect(), TR(CANCEL),
                   ui_theme_color(UI_C_PANEL_SUNK),
                   s.ov_have && s.ov_pressed == OP_ASK_CANCEL, true,
                   SV_FONT_LABEL);
}

/**
 * @brief The panel open over the left column, if any: the keypad, the
 *        keyboard, the question or the HV warning, the profile warning, or
 *        the list.
 * @param c the canvas
 */
void draw_overlay(gfx_canvas_t *c)
{
    const gfx_rect_t a = overlay_area();
    if (s.kp.open) {
        ui_keypad_render(&s.kp, c);
        return;
    }
    if (s.tk.open) {
        ui_textkey_render(&s.tk, c);
        return;
    }
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    if (s.ask.open) {
        draw_ask(c);
        return;
    }
    if (s.warn.open) {
        draw_warning(c);
        return;
    }
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_ACCENT));
    if (s.ch.open) {
        draw_choice(c);
    }
}
