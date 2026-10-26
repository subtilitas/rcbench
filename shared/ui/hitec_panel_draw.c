/**
 * @file hitec_panel_draw.c
 * @brief The drawing of the Hitec programmer's overlay: the tabs, the rows
 *        of the tab shown, its notes and the last message, the list of
 *        choices and the hold pages.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this overlay works with.  The state and the seam are
 * hitec_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel.h"

#include <stdio.h>
#include <string.h>

#include "hitec_reg.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: its layout macros and hp, the state, which no header included
 * after it may meet. */
#include "hitec_panel_int.h"

/** @brief The notes a tab holds at most. */
#define NOTES_MAX 6

/** @brief One note: its text and its colour. */
typedef struct {
    const char *s;       /**< the text, static          */
    gfx_color_t color;   /**< its colour                */
} note_t;

/** @brief Where the notes and the message of the tab shown go. */
typedef struct {
    note_t n[NOTES_MAX];          /**< the notes, from the top         */
    int    count;                 /**< notes used                      */
    int    y[NOTES_MAX];          /**< each note's top; -1 not drawn   */
    int    lines;                 /**< the message's lines             */
    int    msg_y;                 /**< the message's top               */
    int    top;                   /**< the first note's top            */
    int    foot;                  /**< under the last line drawn       */
} plan_t;

/** @brief Static: the message's lines, written on the UI task's path. */
static char s_msg[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE];
/** @brief Static: the notes' own texts. */
static char s_buf[3][HITEC_PANEL_LINE];
/** @brief Static: one plan serves the draw and every query. */
static plan_t s_plan;

/**
 * @brief The text colour of a message's level.
 * @param level hitec_prog_level_t
 * @return the colour
 */
static gfx_color_t level_color(unsigned level)
{
    return level == HITEC_PROG_DANGER ? ui_theme_color(UI_C_DANGER)
         : level == HITEC_PROG_WARN   ? ui_theme_color(UI_C_WARN)
                                      : ui_theme_color(UI_C_TEXT);
}

/**
 * @brief Adds a note.
 * @param p     the plan
 * @param s     its text
 * @param color its colour
 */
static void note(plan_t *p, const char *s, gfx_color_t color)
{
    if (p->count < NOTES_MAX && s != NULL && s[0] != '\0') {
        p->n[p->count].s = s;
        p->n[p->count].color = color;
        ++p->count;
    }
}

/**
 * @brief The SERVO tab's notes: the state, what it needs, what stands.
 * @param p the plan
 */
static void servo_notes(plan_t *p)
{
    const hitec_prog_t *pr = &hp.prog;
    const hitec_prog_state_t st = hitec_prog_state(pr);
    const hitec_model_t *m = hitec_prog_model(pr);
    const gfx_color_t faint = ui_theme_color(UI_C_TEXT_FAINT);

    hpn_state_line(s_buf[0], sizeof(s_buf[0]));
    note(p, s_buf[0], ui_theme_color(hitec_prog_stopped(pr)
                                         ? UI_C_WARN : UI_C_TEXT));
    if (st == HITEC_PROG_NO_PAGE) {
        note(p, TR(HP_NO_PAGE_2), faint);
    } else if (st == HITEC_PROG_PWM && !hitec_prog_on_port(pr)) {
        note(p, TR(KP_NEED_TARGET_2), faint);
    } else if (st == HITEC_PROG_OTHER) {
        note(p, TR(HP_RELEASE_HINT), faint);
    }
    if (m != NULL && pr->series == (uint8_t)HITEC_CLASSIC) {
        hitec_value_t v;

        if (hitec_param_get(hitec_prog_servo(pr), m->layout,
                            HITEC_P_C_CHECKSUM, &v) == HITEC_GET_OK
            && v.value == HITEC_SUM_BAD) {
            note(p, TR(HP_SUM_BAD), ui_theme_color(UI_C_DANGER));
        }
    }
    if (m != NULL && hitec_model_name(m->model)[0] == '\0') {
        const char *as = m->layout == (uint8_t)HITEC_LAYOUT_D ? "D485HW"
                       : m->layout == (uint8_t)HITEC_LAYOUT_HS7 ? "HS-7955TG"
                                                               : "HS-5645MG";

        snprintf(s_buf[1], sizeof(s_buf[1]), TR(HP_LAYOUT_GUESS), as);
        note(p, s_buf[1], ui_theme_color(UI_C_WARN));
    }
    if (hitec_prog_mv_capped(pr)
        && (st == HITEC_PROG_PWM || pr->owned)) {
        note(p, TR(HP_V_CAPPED), ui_theme_color(UI_C_WARN));
    }
    note(p, TR(HP_READ_ONLY), faint);
}

/**
 * @brief The notes of a value tab.
 * @param p the plan
 */
static void value_notes(plan_t *p)
{
    const bool d = hitec_prog_series(&hp.prog) == (uint8_t)HITEC_D;
    const gfx_color_t faint = ui_theme_color(UI_C_TEXT_FAINT);

    if (hitec_prog_servo(&hp.prog) == NULL) {
        note(p, TR(HP_NOT_READ), faint);
    }
    switch (hp.tabs.selected) {
    case HITEC_PANEL_MOTION:
    case HITEC_PANEL_PROTECT:
        note(p, TR(HP_RAW_BESIDE), faint);
        break;
    case HITEC_PANEL_TRAVEL:
        if (d) {
            note(p, TR(HP_POS_NOTE), faint);
            note(p, TR(HP_DEG_NOTE), faint);
            note(p, TR(HP_DIR_NOTE), faint);
        } else {
            note(p, TR(HP_END_UNIT), faint);
        }
        break;
    default:
        if (d) {
            note(p, TR(HP_RAW_NOTE), faint);
        }
        break;
    }
}

/**
 * @brief The top of the notes: 8px under the lowest row of the tab shown.
 * @return the y, body pixels
 */
static int rows_foot(void)
{
    int y = hp.area.y + HP_ROW0;

    for (int i = 0; i < HP_ROW_COUNT; ++i) {
        const hpn_row_t *r = &hpn_rows[i];

        if (r->page == hp.tabs.selected && hpn_row_shown(r)) {
            const gfx_rect_t rr = hpn_row_rect(r);

            y = rr.y + rr.h + 8 > y ? rr.y + rr.h + 8 : y;
        }
    }
    return y;
}

/**
 * @brief Plans the notes and the message of the tab shown: the notes from
 *        the rows down, the message's lines under them at the foot.  A note
 *        that does not fit above the message is not drawn.
 * @param p the plan
 */
static void plan(plan_t *p)
{
    int msg_rows = 0;
    int y;

    memset(p, 0, sizeof(*p));
    if (hp.tabs.selected == HITEC_PANEL_SERVO) {
        servo_notes(p);
    } else {
        value_notes(p);
    }
    p->lines = hitec_panel_message_lines(hitec_prog_message(&hp.prog), s_msg);
    /* A message that says what the state line says is said once. */
    if (p->lines > 0 && p->count > 0 && strcmp(s_msg[0], p->n[0].s) == 0) {
        p->lines = 0;
    }
    for (int i = 0; i < p->lines; ++i) {
        msg_rows += hitec_panel_text_rows(s_msg[i]);
    }
    p->top = rows_foot();
    p->msg_y = hpn_notes_foot() - msg_rows * HP_LINE_H;
    y = p->top;
    for (int i = 0; i < p->count; ++i) {
        const int rows = hitec_panel_text_rows(p->n[i].s);

        if (y + rows * HP_LINE_H > p->msg_y) {
            p->y[i] = -1;
            continue;
        }
        p->y[i] = y;
        y += rows * HP_LINE_H;
    }
    p->foot = msg_rows > 0 ? hpn_notes_foot() : y;
}

/**
 * @brief Draws the notes and the message of the tab shown.
 * @param c the canvas
 */
static void draw_notes(gfx_canvas_t *c)
{
    plan_t *p = &s_plan;
    const hitec_prog_msg_t *m = hitec_prog_message(&hp.prog);
    int y;

    plan(p);
    for (int i = 0; i < p->count; ++i) {
        if (p->y[i] >= 0) {
            (void)hpn_text_rows(c, hp.area.x + 10, p->y[i], p->n[i].s,
                                p->n[i].color);
        }
    }
    y = p->msg_y;
    for (int i = 0; i < p->lines; ++i) {
        y = hpn_text_rows(c, hp.area.x + 10, y, s_msg[i],
                          level_color(m->level));
    }
}

const char *hitec_panel_note(int i)
{
    plan(&s_plan);
    for (int j = 0; j < s_plan.count; ++j) {
        if (s_plan.y[j] >= 0 && i-- == 0) {
            return s_plan.n[j].s;
        }
    }
    return i >= 0 && i < s_plan.lines ? s_msg[i] : NULL;
}

gfx_rect_t hitec_panel_notes_drawn(void)
{
    /* Every tab has a note or a message: a value tab with no note is a
     * classic INFO tab after a read, and a read leaves its message. */
    plan(&s_plan);
    return (gfx_rect_t){ hp.area.x, (int16_t)s_plan.top, hp.area.w,
                         (int16_t)(s_plan.foot - s_plan.top) };
}

/**
 * @brief A button of the overlay in HP_FONT.
 * @param c       the canvas
 * @param r       the button
 * @param label   its label
 * @param fill    its colour
 * @param pressed lightens the fill
 * @param enabled false dims it
 */
static void button(gfx_canvas_t *c, gfx_rect_t r, const char *label,
                   gfx_color_t fill, bool pressed, bool enabled)
{
    ui_button_font(c, r, label, fill, pressed, enabled, HP_FONT);
}

/**
 * @brief Draws the PORT row's label: the port, the pin and the series over
 *        the receive mode and the supply, in the warning colour with no pin.
 * @param c  the canvas
 * @param rr the row
 */
static void draw_port_label(gfx_canvas_t *c, gfx_rect_t rr)
{
    const gfx_color_t col = hpn_port_unbound() ? ui_theme_color(UI_C_WARN)
                                               : ui_theme_color(UI_C_TEXT);
    char line[HITEC_PANEL_LINE];

    if (hpn_port_line(0, line, sizeof(line))) {
        gfx_text(c, rr.x, rr.y + 4, line, HP_FONT, col, 1);
    }
    if (hpn_port_line(1, line, sizeof(line))) {
        gfx_text(c, rr.x, rr.y + 4 + HP_LINE_H, line, HP_FONT,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
    }
}

/**
 * @brief Draws row @p i: its label and its control.
 * @param c the canvas
 * @param i the row's index
 */
static void draw_row(gfx_canvas_t *c, int i)
{
    const hpn_row_t *r = &hpn_rows[i];
    const gfx_rect_t rr = hpn_row_rect(r);
    const gfx_rect_t vr = hpn_value_rect(r);
    const bool live = hpn_row_live(r);
    const bool pressed = hp.have && hp.pressed == P_ROW && hp.row == i;
    char v[HITEC_PANEL_LINE];
    bool warn = false;

    if (r->kind == K_ACT && r->what == A_PORT) {
        draw_port_label(c, rr);
    } else {
        gfx_text(c, rr.x, rr.y + (HP_ROW_H - HP_LINE_H) / 2, hpn_label(r),
                 HP_FONT, ui_theme_color(UI_C_TEXT_DIM), 1);
    }
    hpn_value_text(r, v, sizeof(v), &warn);
    if (r->kind == K_ACT) {
        gfx_color_t fill = ui_theme_color(UI_C_PANEL_SUNK);

        if (r->what == A_CLEAR && live) {
            fill = ui_theme_color(UI_C_WARN);
        }
        button(c, vr, v, fill, pressed, live);
        return;
    }
    gfx_fill_round_rect(c, vr.x, vr.y, vr.w, vr.h, UI_R_CTL,
                        ui_theme_color(UI_C_PANEL_SUNK));
    gfx_text_in(c, vr, v, HP_FONT,
                warn ? ui_theme_color(UI_C_WARN) : ui_theme_color(UI_C_TEXT),
                1, GFX_ALIGN_CENTER);
}

/**
 * @brief Draws the tab shown: the tabs, CLOSE, its rows and its notes.
 * @param c the canvas
 */
static void draw_tab(gfx_canvas_t *c)
{
    const hitec_prog_state_t st = hitec_prog_state(&hp.prog);

    ui_tabs_render(&hp.tabs, c);
    button(c, hitec_panel_close_rect(), TR(SUP_CLOSE),
           ui_theme_color(UI_C_PANEL_SUNK), hp.have && hp.pressed == P_CLOSE,
           !hitec_panel_holds_port() || st == HITEC_PROG_HELD
               || st == HITEC_PROG_OTHER);
    for (int i = 0; i < HP_ROW_COUNT; ++i) {
        if (hpn_rows[i].page == hp.tabs.selected && hpn_row_shown(&hpn_rows[i])) {
            draw_row(c, i);
        }
    }
    draw_notes(c);
}

/**
 * @brief Draws the list: its title, its entries and CANCEL.
 * @param c the canvas
 */
static void draw_choice(gfx_canvas_t *c)
{
    gfx_text(c, hp.area.x + 10, hp.area.y + 14, hp.ch.title, HP_FONT,
             ui_theme_color(UI_C_TEXT), 1);
    for (int i = 0; i < hp.ch.count; ++i) {
        const bool current = hp.ch.kind == C_SERIES
                             && hp.ch.values[i]
                                    == (int)hitec_prog_series(&hp.prog);

        button(c, hitec_panel_choice_rect(i), hp.ch.labels[i],
               current ? ui_theme_color(UI_C_ACCENT)
                       : ui_theme_color(UI_C_PANEL_SUNK),
               hp.have && hp.pressed == P_CHOICE && hp.row == i, true);
    }
    button(c, hitec_panel_choice_cancel_rect(), TR(CANCEL),
           ui_theme_color(UI_C_PANEL_SUNK),
           hp.have && hp.pressed == P_CHOICE_CANCEL, true);
}

/**
 * @brief Draws a hold page: its title, its lines and the two buttons.
 * @param c the canvas
 */
static void draw_hold(gfx_canvas_t *c)
{
    const gfx_rect_t a = hp.area;
    const gfx_color_t edge = ui_theme_color(UI_C_WARN);
    int y = a.y + HP_PAGE_TOP;

    gfx_draw_rect(c, a.x, a.y, a.w, a.h, edge);
    gfx_draw_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2, edge);
    gfx_draw_rect(c, a.x + 2, a.y + 2, a.w - 4, a.h - 4, edge);
    gfx_text(c, a.x + 12, a.y + 16, hp.page.title, HP_TITLE_FONT, edge, 1);
    for (int i = 0; i < hp.page.count; ++i) {
        y = hpn_text_rows(c, a.x + 12, y, hp.page.lines[i],
                          ui_theme_color(UI_C_TEXT))
            + 8;
    }
    button(c, hitec_panel_apply_rect(), hp.page.apply,
           ui_hold_fill(ui_theme_color(UI_C_PANEL_SUNK), edge,
                        hp.page.hold.held_s),
           hp.page.down, true);
    button(c, hitec_panel_cancel_rect(), TR(CANCEL),
           ui_theme_color(UI_C_PANEL_SUNK), hp.have && hp.pressed == P_CANCEL,
           true);
}

void hitec_panel_render(gfx_canvas_t *c)
{
    const gfx_rect_t a = hp.area;

    if (hp.kp.open) {
        ui_keypad_render(&hp.kp, c);
        return;
    }
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    if (hp.page.open) {
        draw_hold(c);
        return;
    }
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_ACCENT));
    if (hp.ch.open) {
        draw_choice(c);
        return;
    }
    draw_tab(c);
}
