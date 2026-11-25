/**
 * @file
 * @brief Programming for YGE ESCs: the pages drawn and their geometry.  See
 *        yge_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "link_pages.h"
#include "prog_registry.h"
#include "ui_theme.h"
#include "yge_panel_int.h"

/** @brief The hold's button on the CONNECT, PIN HELD and hold pages. */
#define HOLD_W 420
/** @brief The hold page's BACK and the list's CANCEL. */
#define CANCEL_W 180
/** @brief A footer button's width: 4 in a row. */
#define ACT_W 243
/** @brief The gap between two footer buttons. */
#define ACT_GAP 10
/** @brief The left column's width on the CONNECT page. */
#define CONNECT_W 497
/** @brief The footer's place of WRITE n VALUES. */
#define ACT_WRITE 2

/* ---------------------------------------------------------------- geometry */

/**
 * @brief The footer's top, in body coordinates.
 * @return The top.
 */
static int act_y(void)
{
    return UI_KEYS_FOOT - YGP_ACT_H;
}

gfx_rect_t ygp_tabs_rect(void)
{
    return (gfx_rect_t){ (int16_t)(ygp_s.area.x + 8),
                         (int16_t)(ygp_s.area.y + YGP_TAB_DY),
                         (int16_t)(ygp_s.area.w - 16 - YGP_BACK_W - 8), YGP_TAB_H };
}

gfx_rect_t yge_panel_back_rect(void)
{
    return (gfx_rect_t){ (int16_t)(ygp_s.area.x + ygp_s.area.w - 8 - YGP_BACK_W),
                         (int16_t)(ygp_s.area.y + YGP_TAB_DY), YGP_BACK_W,
                         YGP_TAB_H };
}

gfx_rect_t ygp_row_rect(int i)
{
    const int w = (ygp_s.area.w - 30) / YGP_COLS;
    const int col = i >= YGP_ROWS ? 1 : 0;
    const int row = i - col * YGP_ROWS;

    return (gfx_rect_t){ (int16_t)(ygp_s.area.x + 10 + col * (w + 10)),
                         (int16_t)(ygp_s.area.y + YGP_ROW_DY + row * YGP_PITCH),
                         (int16_t)w, YGP_ROW_H };
}

gfx_rect_t ygp_value_rect(int i)
{
    const gfx_rect_t r = ygp_row_rect(i);

    return (gfx_rect_t){ (int16_t)(r.x + r.w - YGP_VAL_W), r.y, YGP_VAL_W, r.h };
}

/**
 * @brief A row of the CONNECT page, in the left column.
 * @param i The row, a YGP_C_*.
 * @return Its value's rectangle.
 */
static gfx_rect_t connect_value_rect(int i)
{
    return (gfx_rect_t){ (int16_t)(ygp_s.area.x + 10 + CONNECT_W - YGP_VAL_W),
                         (int16_t)(ygp_s.area.y + YGP_ROW_DY + i * YGP_PITCH),
                         YGP_VAL_W, YGP_ROW_H };
}

gfx_rect_t ygp_action_rect(int i)
{
    return (gfx_rect_t){ (int16_t)(ygp_s.area.x + 10 + i * (ACT_W + ACT_GAP)),
                         (int16_t)act_y(), ACT_W, YGP_ACT_H };
}

gfx_rect_t ygp_choice_rect(int i)
{
    const int rows = 7;
    const int w = (ygp_s.area.w - 30) / 2;
    const int col = i >= rows ? 1 : 0;

    return (gfx_rect_t){ (int16_t)(ygp_s.area.x + 10 + col * (w + 10)),
                         (int16_t)(ygp_s.area.y + 34 + (i - col * rows) * 50),
                         (int16_t)w, UI_TAP_MIN };
}

gfx_rect_t yge_panel_cancel_rect(void)
{
    return (gfx_rect_t){ (int16_t)(ygp_s.area.x + ygp_s.area.w - 10 - CANCEL_W),
                         (int16_t)act_y(), CANCEL_W, YGP_ACT_H };
}

gfx_rect_t ygp_editor_area(void)
{
    return (gfx_rect_t){ ygp_s.area.x, (int16_t)(ygp_s.area.y + 26), ygp_s.area.w,
                         (int16_t)(ygp_s.area.h - 26) };
}

gfx_rect_t yge_panel_hold_rect(void)
{
    const yge_panel_stage_t st = ygp_stage();
    const gfx_rect_t none = { 0, 0, 0, 0 };
    const gfx_rect_t left = { (int16_t)(ygp_s.area.x + 10), (int16_t)act_y(),
                              HOLD_W, YGP_ACT_H };

    if (ygp_s.kp.open || ygp_s.ch_what != YGP_CH_NONE) {
        return none;
    }
    if (ygp_s.page != YGP_PAGE_NONE || st == YGE_PANEL_CONNECT
        || (st == YGE_PANEL_PIN_HELD && !ygp_s.no_again)) {
        return left;
    }
    if (st == YGE_PANEL_SESSION
        && (ygp_s.prog.f.rd.flags & LINK_YP_F_WRITABLE) != 0u) {
        return ygp_action_rect(ACT_WRITE);
    }
    return none;
}

bool yge_panel_hold_live(void)
{
    if (ygp_stage() != YGE_PANEL_SESSION || ygp_s.page != YGP_PAGE_NONE) {
        return yge_panel_hold_rect().w != 0;
    }
    /* WRITE n VALUES: a change to write, the table written, the model free. */
    return yge_panel_hold_rect().w != 0 && ygp_changed_count() > 0
           && !yge_prog_busy(&ygp_s.prog);
}

gfx_rect_t yge_panel_value_rect(int i)
{
    ygp_row_t r;

    if (ygp_stage() == YGE_PANEL_CONNECT) {
        return i >= 0 && i < YGP_C_ROWS ? connect_value_rect(i)
                                        : (gfx_rect_t){ 0, 0, 0, 0 };
    }
    return ygp_row(ygp_s.tabs.selected, i, &r) ? ygp_value_rect(i)
                                               : (gfx_rect_t){ 0, 0, 0, 0 };
}

gfx_rect_t yge_panel_action_rect(int i)
{
    return ygp_action_rect(i);
}

gfx_rect_t yge_panel_tab_rect(int tab)
{
    return ygp_s.tabs.rect[tab >= 0 && tab < UI_TABS_MAX ? tab : 0];
}

gfx_rect_t yge_panel_choice_rect(int i)
{
    return ygp_choice_rect(i);
}

const char *yge_panel_row_name(int i)
{
    ygp_row_t r;

    return ygp_row(ygp_s.tabs.selected, i, &r) ? ygp_row_name(&r) : NULL;
}

const char *yge_panel_row_value(int i)
{
    static char s_text[32];
    ygp_row_t r;

    if (!ygp_row(ygp_s.tabs.selected, i, &r)) {
        return NULL;
    }
    ygp_value_text(&r, ygp_s.draft[r.index], s_text, sizeof(s_text));
    return s_text;
}

bool yge_panel_row_check(int i)
{
    ygp_row_t r;

    return ygp_row(ygp_s.tabs.selected, i, &r) && ygp_row_check(&r);
}

bool yge_panel_row_live(int i)
{
    ygp_row_t r;

    return ygp_row(ygp_s.tabs.selected, i, &r) && ygp_row_live(&r);
}

/* ------------------------------------------------------------------ pieces */

/**
 * @brief A line of text in the session's face.
 * @param c   The canvas.
 * @param x   Its left.
 * @param y   Its top.
 * @param s   The text.
 * @param col Its colour.
 */
static void line_at(gfx_canvas_t *c, int x, int y, const char *s, ui_color_id_t col)
{
    (void)gfx_text(c, x, y, s, YGP_FONT, ui_theme_color(col), 1);
}

/**
 * @brief A title in the large face.
 * @param c   The canvas.
 * @param x   Its left.
 * @param y   Its top.
 * @param s   The text.
 * @param col Its colour.
 */
static void title_at(gfx_canvas_t *c, int x, int y, const char *s, ui_color_id_t col)
{
    (void)gfx_text(c, x, y, s, &gfx_font_16x28, ui_theme_color(col), 1);
}

/**
 * @brief Whether a press is on a button now.
 * @param what What it is on.
 * @param i    Its row; -1 for a button of no row.
 * @return true from the press to its release.
 */
static bool pressed(int what, int i)
{
    return ygp_s.have && ygp_s.pressed == what && (i < 0 || ygp_s.which == i);
}

/**
 * @brief A button of the footer.
 * @param c      The canvas.
 * @param i      Its place.
 * @param label  Its text.
 * @param danger In the danger colour.
 */
static void action_btn(gfx_canvas_t *c, int i, const char *label, bool danger)
{
    ui_button_font(c, ygp_action_rect(i), label,
                   danger ? ui_theme_color(UI_C_DANGER)
                          : ui_theme_color(UI_C_PANEL_HI),
                   pressed(YGP_P_ACT, i), true, YGP_FONT);
}

/**
 * @brief The note, or the model's message, on its line over the footer.
 * @param c The canvas.
 */
static void message_line(gfx_canvas_t *c)
{
    const bool note = ygp_s.note[0] != '\0';
    const char *s = yge_panel_message();
    const bool warn = note ? ygp_s.note_warn : ygp_message_warns();

    line_at(c, ygp_s.area.x + 10, act_y() - 24, s, warn ? UI_C_WARN : UI_C_TEXT);
}

/**
 * @brief BACK.
 * @param c The canvas.
 */
static void back_btn(gfx_canvas_t *c)
{
    ui_button_font(c, yge_panel_back_rect(), TR(EW_BACK),
                   ui_theme_color(UI_C_PANEL_HI), pressed(YGP_P_BACK, -1), true,
                   YGP_FONT);
}

/**
 * @brief A name and its value.
 * @param c     The canvas.
 * @param n     The name's rectangle: the row's.
 * @param v     The value's rectangle.
 * @param name  Its name.
 * @param value Its value.
 * @param live  It takes a tap.
 * @param ink   The name's colour.
 * @param down  A press is on it.
 */
static void row_draw(gfx_canvas_t *c, gfx_rect_t n, gfx_rect_t v, const char *name,
                     const char *value, bool live, ui_color_id_t ink, bool down)
{
    line_at(c, n.x, n.y + (n.h - 20) / 2, name, ink);
    ui_button_font(c, v, value, ui_theme_color(UI_C_PANEL), down, live, YGP_FONT);
}

/** @brief The render under way drew the hold's button. */
static bool s_hold_drawn;

/**
 * @brief The 2s hold's button, its fill the time held, red before it fills.
 * @param c     The canvas.
 * @param label Its text.
 */
static void hold_btn(gfx_canvas_t *c, const char *label)
{
    const gfx_rect_t r = yge_panel_hold_rect();
    const gfx_color_t edge = ui_theme_color(UI_C_DANGER);

    s_hold_drawn = true;
    ui_hold_button(c, r, label, ui_theme_color(UI_C_PANEL_SUNK), edge,
                   &ygp_s.hold, false, true, YGP_FONT);
    gfx_draw_rect(c, r.x, r.y, r.w, r.h, edge);
    gfx_draw_rect(c, r.x + 1, r.y + 1, r.w - 2, r.h - 2, edge);
}

/* ------------------------------------------------------------------- pages */

/**
 * @brief The rows of the session's values, in the left column.
 * @param c        The canvas.
 * @param editable They take a tap: else read-only, dimmed.
 */
static void draw_values(gfx_canvas_t *c, bool editable)
{
    static const ui_text_id_t k_name[YGP_C_ROWS] = {
        TX_YG_ROW_PORT, TX_EW_ROW_POWER, TX_EW_ROW_VOLT, TX_EW_ROW_AMP,
        TX_YG_ROW_BOOT,
    };
    static char s_text[YGP_LINE];
    const bool hand = ygp_s.power == (uint8_t)LINK_YGE_BY_HAND;

    for (int row = 0; row < YGP_C_ROWS; ++row) {
        const gfx_rect_t v = connect_value_rect(row);
        const gfx_rect_t n = { (int16_t)(ygp_s.area.x + 10), v.y,
                               (int16_t)(CONNECT_W - YGP_VAL_W), v.h };
        const bool live = editable
                          && (!hand || (row != YGP_C_VOLT && row != YGP_C_AMP));
        bool warn = false;

        if (row == YGP_C_PORT) {
            warn = yge_panel_target_text(s_text, sizeof(s_text));
        } else {
            ygp_connect_text(row, s_text, sizeof(s_text));
        }
        row_draw(c, n, v, ui_tr(k_name[row]), s_text, live,
                 warn ? UI_C_WARN : live ? UI_C_TEXT : UI_C_TEXT_FAINT,
                 pressed(YGP_P_CONNECT_ROW, row));
    }
}

/**
 * @brief The CONNECT page: the values on the left, the warnings on the right,
 *        a red hold under them.
 * @param c The canvas.
 */
static void draw_connect(gfx_canvas_t *c)
{
    static const ui_text_id_t k_warn[] = { TX_YG_WARN_PROP, TX_YG_WARN_WIRE,
                                           TX_YG_WARN_STICK, TX_YG_WARN_BATTERY };
    const int x = ygp_s.area.x + 10 + CONNECT_W + 10;
    int y = connect_value_rect(0).y;

    line_at(c, ygp_s.area.x + 10, ygp_s.area.y + YGP_TAB_DY + 14,
            TR(YG_CONNECT_TITLE), UI_C_TEXT);
    back_btn(c);
    draw_values(c, true);
    for (size_t i = 0u; i < sizeof(k_warn) / sizeof(k_warn[0]); ++i) {
        const gfx_rect_t box = { (int16_t)x, (int16_t)y,
                                 (int16_t)(ygp_s.area.w - x - 8), YGP_LINE_H };

        y += YGP_LINE_H * ui_text_wrap(c, box, YGP_LINE_H, ui_tr(k_warn[i]),
                                       YGP_FONT,
                                       ui_theme_color(i == 3u ? UI_C_TEXT_DIM
                                                              : UI_C_WARN)) + 8;
    }
    message_line(c);
    hold_btn(c, TR(EW_HOLD_CONNECT_BTN));
}

/**
 * @brief The state, the line, what the session does and the time of an entry
 *        or an operation.
 * @param c The canvas.
 */
static void draw_progress(gfx_canvas_t *c)
{
    static char s_text[YGP_LINE];
    const yge_link_readout_t *rd = &ygp_s.prog.f.rd;
    const yge_panel_stage_t st = ygp_stage();
    const int x = ygp_s.area.x + 40;
    int y = ygp_s.area.y + 90;

    title_at(c, x, y, ygp_state_text(), UI_C_TEXT);
    y += 50;
    ygp_line_text(s_text, sizeof(s_text));
    line_at(c, x, y, s_text, UI_C_TEXT);
    y += YGP_LINE_H + 8;
    ygp_session_text(s_text, sizeof(s_text));
    if (s_text[0] != '\0') {
        line_at(c, x, y, s_text, UI_C_TEXT);
        y += YGP_LINE_H + 8;
    }
    snprintf(s_text, sizeof(s_text), TR(EW_TIME_IN_STATE), (unsigned)rd->t_ms / 1000u,
             (unsigned)rd->t_ms % 1000u / 100u);
    line_at(c, x, y, s_text, UI_C_TEXT_DIM);
    y += YGP_LINE_H + 8;
    if (yge_prog_card_waits(&ygp_s.prog)) {
        line_at(c, x, y, TR(YG_CARD_WAIT), UI_C_WARN);
    } else if (st == YGE_PANEL_HAND) {
        line_at(c, x, y, TR(YG_HAND_NOW), UI_C_WARN);
        y += YGP_LINE_H;
        line_at(c, x, y, TR(YG_HAND_LIMIT), UI_C_TEXT_DIM);
    } else if ((rd->flags & LINK_YP_F_WRITING) != 0u) {
        line_at(c, x, y, TR(EW_WRITING), UI_C_WARN);
    }
    message_line(c);
    action_btn(c, 0, TR(EW_ABORT), true);
    if (st == YGE_PANEL_HAND) {
        action_btn(c, 1, TR(EW_HAND_DONE), false);
    }
}

/**
 * @brief The IDENTITY tab: type, firmware, serial, count, and whether the
 *        table is written.
 * @param c The canvas.
 */
static void draw_identity(gfx_canvas_t *c)
{
    static char s_text[YGP_LINE];
    const yge_link_readout_t *rd = &ygp_s.prog.f.rd;
    const int x = ygp_s.area.x + 20;
    int y = ygp_s.area.y + YGP_ROW_DY + 6;

    if ((rd->flags & LINK_YP_F_HAVE_READ) == 0u) {
        line_at(c, x, y, TR(YG_ID_NOT_READ), UI_C_WARN);
        return;
    }
    const char *name = oy_map_type_name(rd->type);

    if (name != NULL) {
        snprintf(s_text, sizeof(s_text), TR(YG_ID_TYPE_NAME), name,
                 (unsigned)rd->type);
    } else {
        snprintf(s_text, sizeof(s_text), TR(YG_ID_TYPE), (unsigned)rd->type);
    }
    line_at(c, x, y, s_text, UI_C_TEXT);
    y += YGP_LINE_H;
    snprintf(s_text, sizeof(s_text), TR(YG_ID_FIRMWARE),
             (unsigned long)(rd->firmware / 100000ul),
             (unsigned long)(rd->firmware % 100000ul));
    line_at(c, x, y, s_text, UI_C_TEXT);
    y += YGP_LINE_H;
    snprintf(s_text, sizeof(s_text), TR(YG_ID_SERIAL), (unsigned long)rd->serial);
    line_at(c, x, y, s_text, UI_C_TEXT);
    y += YGP_LINE_H;
    snprintf(s_text, sizeof(s_text), TR(YG_ID_COUNT), (unsigned)rd->entries);
    line_at(c, x, y, s_text, UI_C_TEXT);
    y += YGP_LINE_H + 12;
    if ((rd->flags & LINK_YP_F_WRITABLE) != 0u) {
        title_at(c, x, y, TR(EW_WRITABLE), UI_C_OK);
    } else {
        title_at(c, x, y, TR(EW_READ_ONLY), UI_C_WARN);
        line_at(c, x, y + 34, ygp_ro_text(rd->ro_why), UI_C_WARN);
    }
}

/**
 * @brief The rows of a field tab, and the tab's note under them.
 * @param c The canvas.
 */
static void draw_rows(gfx_canvas_t *c)
{
    static char s_text[32];
    const int tab = ygp_s.tabs.selected;

    for (int i = 0; i < YGP_TAB_ROWS; ++i) {
        ygp_row_t r;

        if (!ygp_row(tab, i, &r)) {
            break;
        }
        const bool live = ygp_row_live(&r);
        const ui_color_id_t ink = ygp_row_check(&r) ? UI_C_WARN
                                  : ygp_row_changed(&r) ? UI_C_ACCENT
                                  : live ? UI_C_TEXT : UI_C_TEXT_FAINT;
        char name[40];

        snprintf(name, sizeof(name), "%s%s", ygp_row_changed(&r) ? "* " : "",
                 ygp_row_name(&r));
        ygp_value_text(&r, ygp_s.draft[r.index], s_text, sizeof(s_text));
        row_draw(c, ygp_row_rect(i), ygp_value_rect(i), name, s_text, live, ink,
                 pressed(YGP_P_ROW, i));
    }
    const int y = ygp_row_rect(YGP_ROWS - 1).y + YGP_PITCH + 4;

    if (tab == 4) {
        line_at(c, ygp_s.area.x + 10, y, TR(YG_NOTE_POLES), UI_C_TEXT_DIM);
    } else if (tab == YGP_TABS - 1) {
        line_at(c, ygp_s.area.x + 10, y, TR(YG_NOTE_RAW), UI_C_TEXT_DIM);
    }
}

/**
 * @brief The session: tabs, rows, the CHECK line, the message and the
 *        footer.
 * @param c The canvas.
 */
static void draw_session(gfx_canvas_t *c)
{
    static char s_text[YGP_LINE];
    const bool can = (ygp_s.prog.f.rd.flags & LINK_YP_F_WRITABLE) != 0u;

    ui_tabs_render(&ygp_s.tabs, c);
    back_btn(c);
    if (ygp_s.tabs.selected == 0) {
        draw_identity(c);
    } else {
        draw_rows(c);
    }
    ygp_check_line(s_text, sizeof(s_text));
    line_at(c, ygp_s.area.x + 10, act_y() - 48, s_text, UI_C_WARN);
    message_line(c);
    action_btn(c, 0, TR(EW_READ), false);
    action_btn(c, 1, TR(EW_FILES), false);
    if (can) {
        snprintf(s_text, sizeof(s_text), TR(EW_WRITE_N),
                 (unsigned)ygp_changed_count());
        if (yge_panel_hold_live()) {
            hold_btn(c, s_text);
        } else {
            /* Greyed: no change, or a flow runs.  A press does nothing. */
            ui_button_font(c, ygp_action_rect(ACT_WRITE), s_text,
                           ui_theme_color(UI_C_DANGER), false, false, YGP_FONT);
        }
    }
    action_btn(c, 3, TR(YG_LEAVE), false);
}

/**
 * @brief A hold page: its title, its lines, the red hold and BACK.
 * @param c The canvas.
 */
static void draw_page(gfx_canvas_t *c)
{
    const int x = ygp_s.area.x + 20;
    int y = ygp_s.area.y + 44;
    const char *title = TR(KP_LEAVE_TITLE);
    const char *apply = TR(KP_HOLD_LEAVE_DROP);

    if (ygp_s.page == YGP_PAGE_READ) {
        title = TR(EW_READ);
        apply = TR(EW_HOLD_READ);
    } else if (ygp_s.page == YGP_PAGE_RELEASE) {
        title = TR(EW_PAGE_RELEASE);
        apply = TR(EW_HOLD_RELEASE);
    }
    title_at(c, x, ygp_s.area.y + 6, title, UI_C_TEXT);
    for (int i = 0; i < ygp_s.page_lines; ++i) {
        line_at(c, x, y, ygp_s.lines[i], i == 0 ? UI_C_WARN : UI_C_TEXT);
        y += YGP_LINE_H;
    }
    hold_btn(c, apply);
    ui_button_font(c, yge_panel_cancel_rect(), TR(EW_BACK),
                   ui_theme_color(UI_C_PANEL_HI), pressed(YGP_P_CANCEL, -1), true,
                   YGP_FONT);
}

/**
 * @brief The list of choices.
 * @param c The canvas.
 */
static void draw_choice(gfx_canvas_t *c)
{
    if (ygp_s.ch_what == YGP_CH_FIELD) {
        line_at(c, ygp_s.area.x + 10, ygp_s.area.y + 6, ygp_row_name(&ygp_s.edit),
                UI_C_TEXT_DIM);
    }
    for (int i = 0; i < ygp_s.ch_count; ++i) {
        ui_button_font(c, ygp_choice_rect(i), ygp_s.ch_labels[i],
                       ui_theme_color(UI_C_PANEL), pressed(YGP_P_CHOICE, i), true,
                       YGP_FONT);
    }
    ui_button_font(c, yge_panel_cancel_rect(), TR(EW_CANCEL),
                   ui_theme_color(UI_C_PANEL_HI), pressed(YGP_P_CHOICE_CANCEL, -1),
                   true, YGP_FONT);
}

/**
 * @brief A stop, a fault, another writer or a pin not back: the words and
 *        what ends them.
 * @param c  The canvas.
 * @param st The stage.
 */
static void draw_held(gfx_canvas_t *c, yge_panel_stage_t st)
{
    static char s_text[YGP_LINE];
    const int x = ygp_s.area.x + 40;
    const int y = ygp_s.area.y + 90;

    back_btn(c);
    title_at(c, x, y,
             st == YGE_PANEL_STOPPED ? TR(EW_STOPPED_TITLE)
             : st == YGE_PANEL_OTHER ? TR(EW_OTHER_TITLE) : TR(EW_NOT_BACK_TITLE),
             UI_C_WARN);
    ygp_line_text(s_text, sizeof(s_text));
    line_at(c, x, y + 50, s_text, UI_C_TEXT);
    message_line(c);
    if (st == YGE_PANEL_STOPPED) {
        if (ygp_s.prog.f.stopped) {
            line_at(c, x, y + 50 + YGP_LINE_H + 8, TR(EW_STOP_PIN_BACK), UI_C_WARN);
        } else {
            action_btn(c, 0, TR(EW_CLEAR), false);
        }
        action_btn(c, 1, TR(EW_CLOSE_PIN), false);
    } else if (st == YGE_PANEL_OTHER) {
        action_btn(c, 0, TR(EW_RELEASE_BTN), false);
    } else {
        action_btn(c, 0, TR(EW_CLOSE_AGAIN), false);
    }
}

/**
 * @brief A pin held with nothing going on: what ENTER AGAIN would send,
 *        read-only, the hold under it and PIN BACK.
 * @param c The canvas.
 */
static void draw_pin_held(gfx_canvas_t *c)
{
    static char s_text[YGP_LINE];

    back_btn(c);
    if (ygp_s.no_again) {
        const int x0 = ygp_s.area.x + 40;
        const int y0 = ygp_s.area.y + 90;

        title_at(c, x0, y0, TR(EW_HELD_TITLE), UI_C_WARN);
        ygp_line_text(s_text, sizeof(s_text));
        line_at(c, x0, y0 + 50, s_text, UI_C_TEXT);
        if (ygp_s.held_stop) {
            line_at(c, x0, y0 + 50 + YGP_LINE_H, TR(EW_HELD_AFTER_STOP), UI_C_WARN);
        }
    } else {
        const int x = ygp_s.area.x + 10 + CONNECT_W + 10;
        const int y = connect_value_rect(0).y;

        draw_values(c, false);
        title_at(c, x, y, TR(EW_HELD_TITLE), UI_C_WARN);
        ygp_line_text(s_text, sizeof(s_text));
        line_at(c, x, y + 40, s_text, UI_C_TEXT);
        line_at(c, x, y + 40 + YGP_LINE_H, TR(YG_HELD_1), UI_C_TEXT_DIM);
        hold_btn(c, TR(EW_HOLD_ENTER_BTN));
    }
    message_line(c);
    action_btn(c, 2, TR(EW_CLOSE_PIN), false);
}

void ygp_render(gfx_canvas_t *c)
{
    const yge_panel_stage_t st = ygp_stage();

    s_hold_drawn = false;
    gfx_fill_rect(c, 0, PROG_FULL_Y, ygp_s.area.w, ygp_s.area.h,
                  ui_theme_color(UI_C_BG));
    if (ygp_s.kp.open) {
        ui_keypad_render(&ygp_s.kp, c);
    } else if (ygp_s.ch_what != YGP_CH_NONE) {
        draw_choice(c);
    } else if (ygp_s.page != YGP_PAGE_NONE) {
        draw_page(c);
    } else if (st == YGE_PANEL_CONNECT) {
        draw_connect(c);
    } else if (st == YGE_PANEL_SESSION) {
        draw_session(c);
    } else if (st == YGE_PANEL_ENTERING || st == YGE_PANEL_HAND
               || st == YGE_PANEL_RUNNING) {
        draw_progress(c);
    } else if (st == YGE_PANEL_PIN_HELD) {
        draw_pin_held(c);
    } else {
        draw_held(c, st);
    }
    /* A hold whose page or step went as it fired flashes where it was. */
    if (!s_hold_drawn) {
        ui_hold_ghost(c, &ygp_s.hold);
    }
}
