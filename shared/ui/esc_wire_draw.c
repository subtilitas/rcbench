/**
 * @file
 * @brief The ESC signal-wire session's pages drawn.  See esc_wire_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "esc_wire_int.h"
#include "link_pages.h"
#include "prog_registry.h"
#include "ui_theme.h"

/** @brief The hold's button. */
#define HOLD_W 420
/** @brief The hold page's CANCEL. */
#define CANCEL_W 180
/** @brief An action's width: 4 in a row. */
#define ACT_W 243
/** @brief The gap between two actions. */
#define ACT_GAP 10
/** @brief The WRITE list's NEXT and PREV. */
#define TURN_W 180
/** @brief The left column's width on the connect page. */
#define CONNECT_W 497

/* ---------------------------------------------------------------- geometry */

/**
 * @brief The action row's top, in body coordinates.
 * @return The top.
 */
static int act_y(void)
{
    return UI_KEYS_FOOT - EWP_ACT_H;
}

gfx_rect_t ewp_tabs_rect(void)
{
    return (gfx_rect_t){ (int16_t)(ewp_s.area.x + 8),
                         (int16_t)(ewp_s.area.y + EWP_TAB_DY),
                         (int16_t)(ewp_s.area.w - 16 - EWP_BACK_W - 8), EWP_TAB_H };
}

gfx_rect_t esc_wire_panel_back_rect(void)
{
    return (gfx_rect_t){ (int16_t)(ewp_s.area.x + ewp_s.area.w - 8 - EWP_BACK_W),
                         (int16_t)(ewp_s.area.y + EWP_TAB_DY), EWP_BACK_W, EWP_TAB_H };
}

gfx_rect_t ewp_row_rect(int i)
{
    const int w = (ewp_s.area.w - 30) / EWP_COLS;
    const int col = i >= EWP_ROWS ? 1 : 0;
    const int row = i - col * EWP_ROWS;

    return (gfx_rect_t){ (int16_t)(ewp_s.area.x + 10 + col * (w + 10)),
                         (int16_t)(ewp_s.area.y + EWP_ROW_DY + row * EWP_PITCH),
                         (int16_t)w, EWP_ROW_H };
}

gfx_rect_t ewp_value_rect(int i)
{
    const gfx_rect_t r = ewp_row_rect(i);

    return (gfx_rect_t){ (int16_t)(r.x + r.w - EWP_VAL_W), r.y, EWP_VAL_W, r.h };
}

gfx_rect_t ewp_action_rect(int i)
{
    return (gfx_rect_t){ (int16_t)(ewp_s.area.x + 10 + i * (ACT_W + ACT_GAP)),
                         (int16_t)act_y(), ACT_W, EWP_ACT_H };
}

gfx_rect_t ewp_choice_rect(int i)
{
    const int rows = 7;
    const int w = (ewp_s.area.w - 30) / 2;
    const int col = i >= rows ? 1 : 0;

    return (gfx_rect_t){ (int16_t)(ewp_s.area.x + 10 + col * (w + 10)),
                         (int16_t)(ewp_s.area.y + 34 + (i - col * rows) * 50),
                         (int16_t)w, UI_TAP_MIN };
}

gfx_rect_t ewp_choice_cancel_rect(void)
{
    return (gfx_rect_t){ (int16_t)(ewp_s.area.x + ewp_s.area.w - 10 - CANCEL_W),
                         (int16_t)act_y(), CANCEL_W, EWP_ACT_H };
}

gfx_rect_t ewp_editor_area(void)
{
    return (gfx_rect_t){ ewp_s.area.x, (int16_t)(ewp_s.area.y + 26), ewp_s.area.w,
                         (int16_t)(ewp_s.area.h - 26) };
}

gfx_rect_t esc_wire_panel_hold_rect(void)
{
    const ewp_stage_t st = ewp_stage();

    if (ewp_s.kp.open || ewp_s.ch_what != EWP_CH_NONE
        || (ewp_s.page == EWP_PAGE_NONE && st != ESC_WIRE_CONNECT
            && (st != ESC_WIRE_PIN_HELD || ewp_s.no_again))
        || (ewp_s.page == EWP_PAGE_WRITE && !ewp_s.w_seen_last)) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    return (gfx_rect_t){ (int16_t)(ewp_s.area.x + 10), (int16_t)act_y(), HOLD_W,
                         EWP_ACT_H };
}

gfx_rect_t esc_wire_panel_cancel_rect(void)
{
    return ewp_choice_cancel_rect();
}

/**
 * @brief The WRITE list's turn buttons.
 * @param place 0 for PREV, 1 for NEXT.
 * @return The rectangle; empty with one page.
 */
static gfx_rect_t turn_rect(int place)
{
    if (ewp_s.page != EWP_PAGE_WRITE || ewp_write_pages() < 2) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    return (gfx_rect_t){ (int16_t)(ewp_s.area.x + 10 + HOLD_W + 10 + place * (TURN_W + 10)),
                         (int16_t)act_y(), TURN_W, EWP_ACT_H };
}

gfx_rect_t esc_wire_panel_prev_rect(void)
{
    return turn_rect(0);
}

gfx_rect_t esc_wire_panel_next_rect(void)
{
    return turn_rect(1);
}

gfx_rect_t esc_wire_panel_value_rect(int i)
{
    ewp_row_t r;

    if (ewp_stage() == ESC_WIRE_CONNECT) {
        return i >= 0 && i < EWP_ROWS ? ewp_value_rect(i) : (gfx_rect_t){ 0, 0, 0, 0 };
    }
    return ewp_row(ewp_s.tabs.selected, i, &r) ? ewp_value_rect(i)
                                               : (gfx_rect_t){ 0, 0, 0, 0 };
}

gfx_rect_t esc_wire_panel_action_rect(int i)
{
    return ewp_action_rect(i);
}

gfx_rect_t esc_wire_panel_tab_rect(int tab)
{
    return ewp_s.tabs.rect[tab >= 0 && tab < UI_TABS_MAX ? tab : 0];
}

gfx_rect_t esc_wire_panel_choice_rect(int i)
{
    return ewp_choice_rect(i);
}

const char *esc_wire_panel_row_name(int i)
{
    ewp_row_t r;

    return ewp_row(ewp_s.tabs.selected, i, &r) ? ewp_row_name(&r) : NULL;
}

const char *esc_wire_panel_row_value(int i)
{
    static char s_text[32];
    ewp_row_t r;

    if (!ewp_row(ewp_s.tabs.selected, i, &r)) {
        return NULL;
    }
    ewp_row_text(&r, s_text, sizeof(s_text));
    return s_text;
}

const char *esc_wire_panel_choice_label(int i)
{
    return i >= 0 && i < ewp_s.ch_count ? ewp_s.ch_labels[i] : NULL;
}

int esc_wire_panel_page(void)
{
    return ewp_s.page;
}

const char *esc_wire_panel_page_line(int i)
{
    return i >= 0 && i < ewp_s.page_lines ? ewp_s.lines[i] : NULL;
}

bool esc_wire_panel_page_line_warns(int i)
{
    return i >= 0 && i < ewp_s.page_lines && ewp_s.line_warn[i];
}

const char *esc_wire_panel_name_line(void)
{
    static char s_text[EWP_LINE];

    snprintf(s_text, sizeof(s_text), TR(EW_ID_TAG), (int)sizeof(ewp_s.prog.f.image.name),
             ewp_s.prog.f.image.name);
    return s_text;
}

const char *esc_wire_panel_music_line(void)
{
    static char s_text[sizeof(ewp_s.prog.f.image.music) + 16u];
    static char s_music[sizeof(ewp_s.prog.f.image.music)];
    const char *m = ewp_s.prog.f.image.music;
    size_t n = 0u;

    for (; n + 1u < sizeof(s_music) && m[n] != '\0'; ++n) {
        const unsigned char b = (unsigned char)m[n];

        s_music[n] = b >= 0x20u && b <= 0x7Eu ? (char)b : '?';
    }
    s_music[n] = '\0';
    snprintf(s_text, sizeof(s_text), TR(EW_ID_MUSIC), n != 0u ? s_music : "--");
    return s_text;
}

ui_keypad_t *esc_wire_panel_keypad(void)
{
    return &ewp_s.kp;
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
    (void)gfx_text(c, x, y, s, EWP_FONT, ui_theme_color(col), 1);
}

/**
 * @brief A button of the action row.
 * @param c      The canvas.
 * @param i      Its place.
 * @param label  Its text.
 * @param danger In the danger colour.
 * @param on     It takes a tap: else dimmed.
 */
static void action_btn(gfx_canvas_t *c, int i, const char *label, bool danger, bool on)
{
    ui_button_font(c, ewp_action_rect(i), label,
                   danger ? ui_theme_color(UI_C_DANGER)
                          : ui_theme_color(UI_C_PANEL_HI),
                   false, on, EWP_FONT);
}

/**
 * @brief The note, or the model's message, on its line over the action row.
 * @param c The canvas.
 */
static void message_line(gfx_canvas_t *c)
{
    const bool note = ewp_s.note[0] != '\0';
    const char *s = note ? ewp_s.note : ewp_message_text();
    const bool warn = note ? ewp_s.note_warn : ewp_message_warns();

    line_at(c, ewp_s.area.x + 10, act_y() - 24, s, warn ? UI_C_WARN : UI_C_TEXT);
}

/**
 * @brief BACK.
 * @param c The canvas.
 */
static void back_btn(gfx_canvas_t *c)
{
    ui_button_font(c, esc_wire_panel_back_rect(), TR(EW_BACK),
                   ui_theme_color(UI_C_PANEL_HI), false, true, EWP_FONT);
}

/**
 * @brief A name and its value.
 * @param c     The canvas.
 * @param i     The row's place.
 * @param name  Its name.
 * @param value Its value.
 * @param live  It takes a tap.
 * @param mark  It differs from the read copy, or is in the warning colour.
 */
static void row_draw(gfx_canvas_t *c, int i, const char *name, const char *value,
                     bool live, bool mark)
{
    const gfx_rect_t r = ewp_row_rect(i);
    const gfx_rect_t v = ewp_value_rect(i);

    line_at(c, r.x, r.y + (r.h - 20) / 2, name,
            mark ? UI_C_WARN : live ? UI_C_TEXT : UI_C_TEXT_FAINT);
    ui_button_font(c, v, value, ui_theme_color(UI_C_PANEL), false, live, EWP_FONT);
}

/**
 * @brief The 2s hold's button, its fill the time held.
 * @param c     The canvas.
 * @param label Its text.
 */
static void hold_btn(gfx_canvas_t *c, const char *label)
{
    const gfx_rect_t r = esc_wire_panel_hold_rect();
    const gfx_color_t edge = ui_theme_color(UI_C_DANGER);

    ui_button_font(c, r, label,
                   ui_hold_fill(ui_theme_color(UI_C_PANEL_SUNK), edge,
                                ewp_s.hold.held_s),
                   false, true, EWP_FONT);
    /* Red before the hold fills it. */
    gfx_draw_rect(c, r.x, r.y, r.w, r.h, edge);
    gfx_draw_rect(c, r.x + 1, r.y + 1, r.w - 2, r.h - 2, edge);
}

/* ------------------------------------------------------------------- pages */

/**
 * @brief A value of the connect rows as the page says it, from what CONNECT and ENTER
 *        AGAIN send.
 * @param row  Its row: 1 POWER, 2 voltage, 3 limit, 4 ENTRY HOLD, 5 OFF TIME.
 * @param out  Takes the text.
 * @param size Bytes at @p out.
 */
static void value_text(int row, char *out, size_t size)
{
    esc_prog_connect_t v;

    ewp_connect_values(&v);
    const bool hand = v.power.power == (uint8_t)LINK_ESC_BY_HAND;

    switch (row) {
    case 1:
        snprintf(out, size, "%s", hand ? TR(EW_POWER_HAND) : TR(EW_POWER_SWITCHED));
        break;
    case 2:
        if (hand) {
            snprintf(out, size, "%s", TR(EW_POWER_NONE));
        } else {
            snprintf(out, size, "%u.%02uV", (unsigned)v.power.mv / 1000u,
                     (unsigned)v.power.mv % 1000u / 10u);
        }
        break;
    case 3:
        if (hand) {
            snprintf(out, size, "%s", TR(EW_POWER_NONE));
        } else {
            snprintf(out, size, "%u.%02uA", (unsigned)v.power.ma / 1000u,
                     (unsigned)v.power.ma % 1000u / 10u);
        }
        break;
    case 4:
        snprintf(out, size, "%lu.%lus", (unsigned long)v.power.hold_ms / 1000ul,
                 (unsigned long)v.power.hold_ms % 1000ul / 100ul);
        break;
    default:
        snprintf(out, size, "%u.%us", (unsigned)v.power.off_ms / 1000u,
                 (unsigned)v.power.off_ms % 1000u / 100u);
        break;
    }
}

const char *esc_wire_panel_value_text(int row)
{
    static char s_text[EWP_LINE];

    value_text(row, s_text, sizeof(s_text));
    return s_text;
}

/**
 * @brief The rows of the session's values, in the left column.
 * @param c        The canvas.
 * @param editable They take a tap: else read-only, dimmed.
 */
static void draw_values(gfx_canvas_t *c, bool editable)
{
    static const ui_text_id_t k_name[] = { TX_EW_ROW_POWER, TX_EW_ROW_VOLT, TX_EW_ROW_AMP,
                                           TX_EW_ROW_HOLD, TX_EW_ROW_OFF };
    static char s_text[EWP_LINE];
    const bool hand = ewp_s.power == (uint8_t)LINK_ESC_BY_HAND;
    const bool none = esc_wire_panel_target_text(s_text, sizeof(s_text));

    row_draw(c, 0, TR(EW_ROW_TARGET), s_text, editable && ewp_s.target_count != 0u, none);
    for (int row = 1; row <= 5; ++row) {
        const bool live = editable && (!hand || row == 1 || row == 4);

        value_text(row, s_text, sizeof(s_text));
        row_draw(c, row, ui_tr(k_name[row - 1]), s_text, live, false);
    }
}

/**
 * @brief The connect page: the values on the left, the warnings on the right, a red
 *        hold under them.
 * @param c The canvas.
 */
static void draw_connect(gfx_canvas_t *c)
{
    const int x = ewp_s.area.x + 10 + CONNECT_W + 10;
    int y = ewp_row_rect(0).y;

    line_at(c, ewp_s.area.x + 10, ewp_s.area.y + EWP_TAB_DY + 14,
            TR(EW_CONNECT_TITLE), UI_C_TEXT);
    back_btn(c);
    draw_values(c, true);
    /* Warnings, in the right column.  The connect page's rows take the left. */
    static const ui_text_id_t k_warn[] = { TX_EW_WARN_PROP, TX_EW_WARN_BEC,
                                           TX_EW_WARN_SERVOS, TX_EW_WARN_SIMONK };

    for (size_t i = 0u; i < sizeof(k_warn) / sizeof(k_warn[0]); ++i) {
        const gfx_rect_t box = { (int16_t)x, (int16_t)y,
                                 (int16_t)(ewp_s.area.w - x - 8), EWP_LINE_H };

        y += EWP_LINE_H * ui_text_wrap(c, box, EWP_LINE_H, ui_tr(k_warn[i]), EWP_FONT,
                                       ui_theme_color(i == 3u ? UI_C_TEXT_DIM
                                                              : UI_C_WARN)) + 8;
    }
    message_line(c);
    hold_btn(c, TR(EW_HOLD_CONNECT_BTN));
}

/**
 * @brief The state, the line and the count of an entry or an operation.
 * @param c The canvas.
 */
static void draw_progress(gfx_canvas_t *c)
{
    static char s_text[EWP_LINE];
    const esc_link_readout_t *rd = &ewp_s.prog.f.rd;
    const int x = ewp_s.area.x + 40;
    int y = ewp_s.area.y + 90;

    (void)gfx_text(c, x, y, ewp_state_text(rd->state), &gfx_font_16x28,
                   ui_theme_color(UI_C_TEXT), 1);
    y += 50;
    ewp_line_text(s_text, sizeof(s_text));
    line_at(c, x, y, s_text, UI_C_TEXT);
    y += EWP_LINE_H + 8;
    snprintf(s_text, sizeof(s_text), TR(EW_TIME_IN_STATE), (unsigned)rd->t_ms / 1000u,
             (unsigned)rd->t_ms % 1000u / 100u);
    line_at(c, x, y, s_text, UI_C_TEXT_DIM);
    y += EWP_LINE_H + 8;
    if (ewp_s.w_step != EWP_W_NONE) {
        line_at(c, x, y, TR(EW_CARD_WAIT), UI_C_WARN);
    } else if (ewp_stage() == ESC_WIRE_ENTERING
               && ewp_s.power == (uint8_t)LINK_ESC_SWITCHED) {
        snprintf(s_text, sizeof(s_text), TR(EW_ENTRY_HOLD_IS),
                 (unsigned long)ewp_s.hold_ms / 1000ul,
                 (unsigned long)ewp_s.hold_ms % 1000ul / 100ul);
        line_at(c, x, y, s_text, UI_C_TEXT_DIM);
    } else if (ewp_stage() == ESC_WIRE_HAND) {
        line_at(c, x, y, TR(EW_HAND_NOW), UI_C_WARN);
        y += EWP_LINE_H;
        line_at(c, x, y, TR(EW_HAND_LIMIT), UI_C_TEXT_DIM);
    } else if (ewp_stage() == ESC_WIRE_RUNNING
               && (rd->flags & LINK_EW_F_WRITING) != 0u) {
        line_at(c, x, y, TR(EW_WRITING), UI_C_WARN);
    }
    message_line(c);
    action_btn(c, 0, TR(EW_ABORT), true, true);
    if (ewp_stage() == ESC_WIRE_HAND) {
        action_btn(c, 1, TR(EW_HAND_DONE), false, true);
    }
}

/**
 * @brief The identity tab.
 * @param c The canvas.
 */
static void draw_identity(gfx_canvas_t *c)
{
    static char s_text[EWP_LINE];
    const esc_link_readout_t *rd = &ewp_s.prog.f.rd;
    const esc_link_image_t *im = &ewp_s.prog.f.image;
    const bool can = (rd->flags & LINK_EW_F_WRITABLE) != 0u;
    const int x = ewp_s.area.x + 20;
    int y = ewp_s.area.y + EWP_ROW_DY + 6;

    snprintf(s_text, sizeof(s_text), TR(EW_ID_FIRMWARE), ewp_fw_name(rd->fw));
    line_at(c, x, y, s_text, UI_C_TEXT);
    y += EWP_LINE_H;
    snprintf(s_text, sizeof(s_text), rd->fw == (uint8_t)EW_FW_AM
                                         || rd->fw == (uint8_t)EW_FW_E32
                                         ? TR(EW_ID_REVISION) : TR(EW_ID_LAYOUT),
             (unsigned)rd->layout);
    line_at(c, x, y, s_text, UI_C_TEXT);
    y += EWP_LINE_H;
    if (rd->fw == (uint8_t)EW_FW_S || rd->fw == (uint8_t)EW_FW_BJ) {
        snprintf(s_text, sizeof(s_text), TR(EW_ID_VERSION), (unsigned)im->page[0],
                 (unsigned)im->page[1]);
        line_at(c, x, y, s_text, UI_C_TEXT);
        y += EWP_LINE_H;
        snprintf(s_text, sizeof(s_text), TR(EW_ID_SIGNATURE), (unsigned)rd->signature);
        line_at(c, x, y, s_text, UI_C_TEXT);
        y += EWP_LINE_H;
        snprintf(s_text, sizeof(s_text), TR(EW_ID_TAG), 16, (const char *)&im->page[0x40]);
        line_at(c, x, y, s_text, UI_C_TEXT);
        y += EWP_LINE_H;
        snprintf(s_text, sizeof(s_text), TR(EW_ID_MCU), 16, (const char *)&im->page[0x50]);
    } else if (rd->fw == (uint8_t)EW_FW_AM) {
        snprintf(s_text, sizeof(s_text), TR(EW_ID_VERSION), (unsigned)im->page[3],
                 (unsigned)im->page[4]);
        line_at(c, x, y, s_text, UI_C_TEXT);
        y += EWP_LINE_H;
        snprintf(s_text, sizeof(s_text), TR(EW_ID_BOOTLOADER), (unsigned)im->page[2]);
        line_at(c, x, y, s_text, UI_C_TEXT);
        y += EWP_LINE_H;
        snprintf(s_text, sizeof(s_text), "%s", esc_wire_panel_name_line());
    } else {
        snprintf(s_text, sizeof(s_text), "%s", esc_wire_panel_name_line());
    }
    line_at(c, x, y, s_text, UI_C_TEXT);
    y += EWP_LINE_H;
    if (rd->fw == (uint8_t)EW_FW_E32) {
        const gfx_rect_t box = { (int16_t)x, (int16_t)y,
                                 (int16_t)(ewp_s.area.w - 2 * (x - ewp_s.area.x)),
                                 EWP_LINE_H };

        y += EWP_LINE_H * ui_text_wrap(c, box, EWP_LINE_H, esc_wire_panel_music_line(),
                                       EWP_FONT, ui_theme_color(UI_C_TEXT_DIM));
    }
    y += 12;
    if (can) {
        (void)gfx_text(c, x, y, TR(EW_WRITABLE), &gfx_font_16x28,
                       ui_theme_color(UI_C_OK), 1);
    } else {
        ewp_why_text(rd->why, s_text, sizeof(s_text));
        (void)gfx_text(c, x, y, TR(EW_READ_ONLY), &gfx_font_16x28,
                       ui_theme_color(UI_C_WARN), 1);
        line_at(c, x, y + 34, s_text, UI_C_WARN);
    }
    if (rd->fw == (uint8_t)EW_FW_E32) {
        line_at(c, x, y + 60, TR(EW_E32_LEAVE_NOTE), UI_C_TEXT_DIM);
    }
}

/**
 * @brief The hex page of a layout with no field names: 16 bytes a line.
 * @param c The canvas.
 */
static void draw_hex(gfx_canvas_t *c)
{
    static char s_text[EWP_LINE];
    const esc_link_image_t *im = &ewp_s.prog.f.image;
    const int x = ewp_s.area.x + 20;
    int y = ewp_s.area.y + EWP_ROW_DY;

    for (unsigned at = 0u; at < im->len && at < 16u * 15u; at += 16u) {
        size_t n = (size_t)snprintf(s_text, sizeof(s_text), "%02X:", at);

        for (unsigned i = 0u; i < 16u && at + i < im->len; ++i) {
            n += (size_t)snprintf(s_text + n, sizeof(s_text) - n, " %02X",
                                  (unsigned)im->page[at + i]);
        }
        line_at(c, x, y, s_text, UI_C_TEXT_DIM);
        y += 22;
    }
}

/**
 * @brief The session: tabs, rows, one line of messages and the actions.
 * @param c The canvas.
 */
static void draw_session(gfx_canvas_t *c)
{
    const esc_link_readout_t *rd = &ewp_s.prog.f.rd;
    const bool erased = (rd->flags & LINK_EW_F_ERASED) != 0u;

    ui_tabs_render(&ewp_s.tabs, c);
    back_btn(c);
    if (ewp_s.tabs.selected == 0) {
        draw_identity(c);
    } else if (!ewp_has_rows()) {
        draw_hex(c);
    } else {
        static char s_text[32];

        for (int i = 0; i < EWP_ROWS * EWP_COLS; ++i) {
            ewp_row_t r;

            if (ewp_row(ewp_s.tabs.selected, i, &r)) {
                ewp_row_text(&r, s_text, sizeof(s_text));
                row_draw(c, i, ewp_row_name(&r), s_text, ewp_row_live(&r),
                         ewp_row_changed(&r));
            }
        }
    }
    message_line(c);
    if (erased) {
        action_btn(c, 0, TR(EW_RESTORE_BTN), true, true);
        return;
    }
    action_btn(c, 0, TR(EW_READ), false, true);
    action_btn(c, 1, TR(EW_FILES), false, true);
    if ((rd->flags & LINK_EW_F_WRITABLE) != 0u) {
        static char s_write[40];

        snprintf(s_write, sizeof(s_write), TR(EW_WRITE_N),
                 (unsigned)ewp_changed_count());
        action_btn(c, 2, s_write, true, ewp_changed_count() > 0);
    }
}

/**
 * @brief A hold page: its title, its lines, the red hold and CANCEL.
 * @param c The canvas.
 */
static void draw_page(gfx_canvas_t *c)
{
    const int x = ewp_s.area.x + 20;
    int y = ewp_s.area.y + 44;
    const char *title;
    const char *apply;

    switch (ewp_s.page) {
    case EWP_PAGE_WRITE:
        title = TR(EW_PAGE_WRITE);
        apply = TR(EW_HOLD_WRITE);
        break;
    case EWP_PAGE_NO_BACKUP:
        title = TR(EW_PAGE_NO_BACKUP);
        apply = TR(EW_HOLD_NO_BACKUP);
        break;
    case EWP_PAGE_LEAVE:
        title = TR(EW_PAGE_LEAVE);
        apply = TR(EW_HOLD_LEAVE);
        break;
    case EWP_PAGE_RELEASE:
        title = TR(EW_PAGE_RELEASE);
        apply = TR(EW_HOLD_RELEASE);
        break;
    default:
        title = TR(EW_RESTORE_BTN);
        apply = TR(EW_HOLD_RESTORE);
        break;
    }
    const int pages = ewp_s.page == EWP_PAGE_WRITE ? ewp_write_pages() : 1;
    char head[EWP_LINE];

    if (pages > 1) {
        snprintf(head, sizeof(head), TR(EW_WRITE_PAGE_OF), title,
                 (unsigned)ewp_s.w_page + 1u, (unsigned)pages);
    } else {
        snprintf(head, sizeof(head), "%s", title);
    }
    (void)gfx_text(c, x, ewp_s.area.y + 6, head, &gfx_font_16x28,
                   ui_theme_color(UI_C_TEXT), 1);
    for (int i = 0; i < ewp_s.page_lines; ++i) {
        line_at(c, x, y, ewp_s.lines[i], ewp_s.line_warn[i] ? UI_C_WARN : UI_C_TEXT);
        y += EWP_LINE_H;
    }
    /* A page's own lines are what stands: only a note goes under them. */
    if (ewp_s.note[0] != '\0') {
        line_at(c, ewp_s.area.x + 10, act_y() - 24, ewp_s.note,
                ewp_s.note_warn ? UI_C_WARN : UI_C_TEXT);
    }
    if (esc_wire_panel_hold_rect().w != 0) {
        hold_btn(c, apply);
    } else {
        line_at(c, ewp_s.area.x + 10, act_y() + 14, TR(EW_WRITE_HOLD_LATER), UI_C_TEXT_DIM);
    }
    if (pages > 1) {
        ui_button_font(c, esc_wire_panel_prev_rect(), TR(EW_WRITE_PREV),
                       ui_theme_color(UI_C_PANEL_HI), false, ewp_s.w_page > 0, EWP_FONT);
        ui_button_font(c, esc_wire_panel_next_rect(), TR(EW_WRITE_NEXT),
                       ui_theme_color(UI_C_PANEL_HI), false, ewp_s.w_page < pages - 1,
                       EWP_FONT);
    }
    ui_button_font(c, esc_wire_panel_cancel_rect(), TR(EW_CANCEL),
                   ui_theme_color(UI_C_PANEL_HI), false, true, EWP_FONT);
}

/**
 * @brief The list of choices, with the row's help line above it.
 * @param c The canvas.
 */
static void draw_choice(gfx_canvas_t *c)
{
    if (ewp_s.ch_what == EWP_CH_FIELD) {
        line_at(c, ewp_s.area.x + 10, ewp_s.area.y + 6, ewp_help_text(&ewp_s.edit),
                UI_C_TEXT_DIM);
    }
    for (int i = 0; i < ewp_s.ch_count; ++i) {
        ui_button_font(c, ewp_choice_rect(i), ewp_s.ch_labels[i],
                       ui_theme_color(UI_C_PANEL), false, true, EWP_FONT);
    }
    ui_button_font(c, ewp_choice_cancel_rect(), TR(EW_CANCEL),
                   ui_theme_color(UI_C_PANEL_HI), false, true, EWP_FONT);
}

/**
 * @brief A stop, a fault, another writer or a pin not back: the words and what ends
 *        them.
 * @param c  The canvas.
 * @param st The stage.
 */
static void draw_held(gfx_canvas_t *c, ewp_stage_t st)
{
    static char s_text[EWP_LINE];
    const int x = ewp_s.area.x + 40;
    const int y = ewp_s.area.y + 90;

    back_btn(c);
    (void)gfx_text(c, x, y,
                   st == ESC_WIRE_STOPPED ? TR(EW_STOPPED_TITLE)
                   : st == ESC_WIRE_OTHER ? TR(EW_OTHER_TITLE) : TR(EW_NOT_BACK_TITLE),
                   &gfx_font_16x28, ui_theme_color(UI_C_WARN), 1);
    ewp_line_text(s_text, sizeof(s_text));
    line_at(c, x, y + 50, s_text, UI_C_TEXT);
    message_line(c);
    if (st == ESC_WIRE_STOPPED) {
        if (ewp_s.prog.f.stopped) {
            line_at(c, x, y + 50 + EWP_LINE_H + 8, TR(EW_STOP_PIN_BACK), UI_C_WARN);
        } else {
            action_btn(c, 0, TR(EW_CLEAR), false, true);
        }
        action_btn(c, 1, TR(EW_CLOSE_PIN), false, true);
    } else if (st == ESC_WIRE_OTHER) {
        action_btn(c, 0, TR(EW_RELEASE_BTN), false, true);
    } else {
        action_btn(c, 0, TR(EW_CLOSE_AGAIN), false, true);
    }
}

/**
 * @brief A pin held with nothing going on: what ENTER AGAIN would send, read-only, the
 *        hold under it and PIN BACK.
 * @param c The canvas.
 */
static void draw_pin_held(gfx_canvas_t *c)
{
    static char s_text[EWP_LINE];
    const int x = ewp_s.area.x + 10 + CONNECT_W + 10;
    const int y = ewp_row_rect(0).y;

    back_btn(c);
    /* The values are what ENTER AGAIN would send: with none to send, none show. */
    if (ewp_s.no_again) {
        const int x0 = ewp_s.area.x + 40;
        const int y0 = ewp_s.area.y + 90;

        (void)gfx_text(c, x0, y0, TR(EW_HELD_TITLE), &gfx_font_16x28,
                       ui_theme_color(UI_C_WARN), 1);
        ewp_line_text(s_text, sizeof(s_text));
        line_at(c, x0, y0 + 50, s_text, UI_C_TEXT);
        line_at(c, x0, y0 + 50 + EWP_LINE_H, TR(EW_HELD_AFTER_STOP), UI_C_WARN);
    } else {
        draw_values(c, false);
        (void)gfx_text(c, x, y, TR(EW_HELD_TITLE), &gfx_font_16x28,
                       ui_theme_color(UI_C_WARN), 1);
        ewp_line_text(s_text, sizeof(s_text));
        line_at(c, x, y + 40, s_text, UI_C_TEXT);
        line_at(c, x, y + 40 + EWP_LINE_H, TR(EW_HELD_1), UI_C_TEXT_DIM);
        hold_btn(c, TR(EW_HOLD_ENTER_BTN));
    }
    message_line(c);
    action_btn(c, 2, TR(EW_CLOSE_PIN), false, true);
}

void ewp_render(gfx_canvas_t *c)
{
    const ewp_stage_t st = ewp_stage();

    gfx_fill_rect(c, 0, PROG_FULL_Y, ewp_s.area.w, ewp_s.area.h,
                  ui_theme_color(UI_C_BG));
    if (ewp_s.kp.open) {
        if (ewp_s.kp_what == EWP_KP_ROW) {
            line_at(c, ewp_s.area.x + 10, ewp_s.area.y + 4,
                    ewp_help_text(&ewp_s.edit), UI_C_TEXT_DIM);
        }
        ui_keypad_render(&ewp_s.kp, c);
    } else if (ewp_s.ch_what != EWP_CH_NONE) {
        draw_choice(c);
    } else if (ewp_s.page != EWP_PAGE_NONE) {
        draw_page(c);
    } else if (st == ESC_WIRE_CONNECT) {
        draw_connect(c);
    } else if (st == ESC_WIRE_SESSION) {
        draw_session(c);
    } else if (st == ESC_WIRE_ENTERING || st == ESC_WIRE_HAND
               || st == ESC_WIRE_RUNNING) {
        draw_progress(c);
    } else if (st == ESC_WIRE_PIN_HELD) {
        draw_pin_held(c);
    } else {
        draw_held(c, st);
    }
}
