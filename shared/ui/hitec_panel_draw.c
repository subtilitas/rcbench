/**
 * @file hitec_panel_draw.c
 * @brief The drawing of the Hitec programmer's overlay: the frame's view of
 *        the tab shown (its cells, the head, the notes and the last
 *        message), drawn by the frame with the tabs and the list, and the
 *        hold pages.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this overlay works with.  The state and the seam are
 * hitec_panel_int.h; the frame is sprog.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel.h"

#include <stdio.h>
#include <string.h>

#include "hitec_move.h"
#include "hitec_reg.h"
#include "prog_choice.h"
#include "sprog.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: its layout macros and hp, the state, which no header included
 * after it may meet. */
#include "hitec_panel_int.h"

/** @brief The texts the view points at until the next view. */
typedef struct {
    /** @brief The notes' own texts: the state line, the layout guess, the
     *         stored values outside the settings */
    char buf[3][HITEC_PANEL_LINE];
    /** @brief The head's two lines */
    char head[2][HITEC_PANEL_LINE];
    /** @brief The message's lines a note does not say already */
    char msg[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE];
    int  lines;                   /**< lines in msg                       */
    bool loop;                    /**< MOVE SERVO's lines are the notes   */
    /** @brief The bottom line: msg's lines, one row each */
    char bottom[HITEC_PANEL_MSG_LINES * HITEC_PANEL_LINE];
} texts_t;

/**
 * @brief Static: the view's texts are built on the UI task's path, and the
 *        view points at them until the next one.
 */
static texts_t s_txt;

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
 * @brief The frame's level of a message's level.
 * @param level hitec_prog_level_t
 * @return the sprog_level_t
 */
static uint8_t msg_level(unsigned level)
{
    return level == HITEC_PROG_DANGER ? (uint8_t)SPROG_LV_DANGER
         : level == HITEC_PROG_WARN   ? (uint8_t)SPROG_LV_WARN
                                      : (uint8_t)SPROG_LV_TEXT;
}

/**
 * @brief The frame's level of a note's colour (hpn_move_notes()).
 * @param color the colour
 * @return the sprog_level_t
 */
static uint8_t color_level(gfx_color_t color)
{
    static const uint8_t lv[] = { SPROG_LV_DANGER, SPROG_LV_WARN,
                                  SPROG_LV_ACCENT, SPROG_LV_FAINT,
                                  SPROG_LV_DIM };
    static const ui_color_id_t id[] = { UI_C_DANGER, UI_C_WARN, UI_C_ACCENT,
                                        UI_C_TEXT_FAINT, UI_C_TEXT_DIM };

    for (size_t i = 0; i < sizeof(lv); ++i) {
        if (color == ui_theme_color(id[i])) {
            return lv[i];
        }
    }
    return SPROG_LV_TEXT;
}

/**
 * @brief Adds a note that says something.
 * @param s     its text
 * @param level its sprog_level_t
 */
static void note(const char *s, uint8_t level)
{
    if (s != NULL && s[0] != '\0') {
        sprog_note(&hp.f, s, level);
    }
}

/**
 * @brief What stands of a write: a record of one interrupted, and a READ
 *        ALL owed before the next.  Nothing of the record a running write
 *        made itself.
 */
static void record_notes(void)
{
    const hitec_prog_t *pr = &hp.prog;
    const hitec_prog_state_t st = hitec_prog_state(pr);

    if (pr->wr_own_rec
        && (st == HITEC_PROG_RECORDING || st == HITEC_PROG_WRITING
            || st == HITEC_PROG_BACKUP)) {
        /* The record on the card is the write in flight, not an
         * interrupted one; the state line says the write's step.  A
         * RESTORE keeps an older record, which stays named. */
        return;
    }
    if (hitec_prog_record(pr)) {
        switch (hitec_prog_record_verdict(pr)) {
        case HITEC_PROG_REC_NONE:
            note(TR(KP_M_JOURNAL), SPROG_LV_DANGER);
            break;
        case HITEC_PROG_REC_OTHER:
            note(TR(KP_M_JOURNAL_OTHER), SPROG_LV_DANGER);
            break;
        case HITEC_PROG_REC_MIX:
            note(TR(KP_M_INCONSISTENT), SPROG_LV_DANGER);
            break;
        default:
            note(TR(HP_RECORD_HELD), SPROG_LV_WARN);
            break;
        }
    } else if (pr->must_read && hitec_prog_state(pr) == HITEC_PROG_HELD) {
        note(pr->mv.owed ? TR(HP_LIVE_OWED) : TR(HP_MUST_READ),
             SPROG_LV_WARN);
    }
}

/**
 * @brief What the write of the edits that wait does besides the values:
 *        the D series' save and restart, or the classic checksum after each
 *        stored value, and the stored values outside the settings it writes
 *        too.  Only while edits wait; last, so a note above keeps its place.
 */
static void write_notes(void)
{
    const hitec_prog_t *pr = &hp.prog;
    const hitec_image_t *to = hitec_prog_draft(pr);
    unsigned raw;

    if (hitec_prog_state(pr) != HITEC_PROG_HELD || to == NULL
        || hitec_prog_changes(pr) == 0u) {
        return;
    }
    note(hitec_prog_series(pr) == (uint8_t)HITEC_D ? TR(HP_WRITE_D)
                                                   : TR(HP_WRITE_C),
         SPROG_LV_FAINT);
    raw = hitec_prog_raw_cells(pr, to);
    if (raw != 0u) {
        snprintf(s_txt.buf[2], sizeof(s_txt.buf[2]), TR(HP_FACTORY_RAW),
                 raw);
        note(s_txt.buf[2], SPROG_LV_FAINT);
    }
}

/** @brief The SERVO tab's notes: the state, what it needs, what stands. */
static void servo_notes(void)
{
    const hitec_prog_t *pr = &hp.prog;
    const hitec_prog_state_t st = hitec_prog_state(pr);
    const hitec_model_t *m = hitec_prog_model(pr);

    hpn_state_line(s_txt.buf[0], sizeof(s_txt.buf[0]));
    note(s_txt.buf[0], hitec_prog_stopped(pr) ? SPROG_LV_WARN
                                              : SPROG_LV_TEXT);
    if (st == HITEC_PROG_NO_PAGE) {
        note(TR(HP_NO_PAGE_2), SPROG_LV_FAINT);
    } else if (st == HITEC_PROG_PWM && !hitec_prog_on_port(pr)) {
        note(ui_tr((ui_text_id_t)prog_choice_text(TX_KP_NEED_TARGET_2)),
             SPROG_LV_FAINT);
    } else if (st == HITEC_PROG_OTHER) {
        note(TR(HP_RELEASE_HINT), SPROG_LV_FAINT);
    }
    if (hitec_prog_sum_bad(pr)) {
        note(TR(HP_SUM_BAD), SPROG_LV_DANGER);
        if (st == HITEC_PROG_HELD) {
            note(TR(HP_SUM_HINT), SPROG_LV_FAINT);
        }
    }
    if (m != NULL && hitec_model_name(m->model)[0] == '\0') {
        const char *as = m->layout == (uint8_t)HITEC_LAYOUT_D ? "D485HW"
                       : m->layout == (uint8_t)HITEC_LAYOUT_HS7 ? "HS-7955TG"
                                                               : "HS-5645MG";

        snprintf(s_txt.buf[1], sizeof(s_txt.buf[1]), TR(HP_LAYOUT_GUESS),
                 as);
        note(s_txt.buf[1], SPROG_LV_WARN);
    }
    if (hitec_prog_mv_capped(pr)
        && (st == HITEC_PROG_PWM || pr->owned)) {
        note(TR(HP_V_CAPPED), SPROG_LV_WARN);
    }
    record_notes();
    note(hpn_backup_note(), hitec_prog_backup_ram_only(pr)
                                    || hpn_backup_unsaved()
                                ? SPROG_LV_WARN
                                : SPROG_LV_FAINT);
    write_notes();
}

/**
 * @brief The notes of a value tab: the tab's own, then the frame's hint
 *        while the values take an edit.
 */
static void value_notes(void)
{
    const bool d = hitec_prog_series(&hp.prog) == (uint8_t)HITEC_D;
    const char *ms[SPROG_NOTES];
    gfx_color_t mc[SPROG_NOTES];
    const int mn = hpn_move_notes(ms, mc, SPROG_NOTES);

    /* MOVE SERVO's loop: its own lines take the notes' place. */
    s_txt.loop = mn >= 0;
    for (int i = 0; i < mn; ++i) {
        note(ms[i], color_level(mc[i]));
    }
    if (mn >= 0) {
        return;
    }
    if (hitec_prog_servo(&hp.prog) == NULL) {
        note(TR(HP_NOT_READ), SPROG_LV_FAINT);
    }
    switch (sprog_tab(&hp.f)) {
    case HITEC_PANEL_MOTION:
    case HITEC_PANEL_PROTECT:
        note(TR(HP_RAW_BESIDE), SPROG_LV_FAINT);
        break;
    case HITEC_PANEL_TRAVEL:
        if (d) {
            note(TR(HP_POS_NOTE), SPROG_LV_FAINT);
            note(TR(HP_DEG_NOTE), SPROG_LV_FAINT);
            note(TR(HP_DIR_NOTE), SPROG_LV_FAINT);
        } else {
            note(TR(HP_END_UNIT), SPROG_LV_FAINT);
        }
        break;
    default:
        if (d) {
            note(TR(HP_RAW_NOTE), SPROG_LV_FAINT);
        }
        break;
    }
    if (hitec_prog_servo(&hp.prog) != NULL && hitec_prog_can_edit(&hp.prog)) {
        sprog_hint(&hp.f);
    }
}

/**
 * @brief The bottom line: the message's lines a note does not say
 *        already, in their order, one row each.  While MOVE SERVO's loop
 *        runs only a warning or a danger stands there: the row goes to the
 *        loop's lines, the port and the live position first.
 */
static void bottom_line(void)
{
    const hitec_prog_msg_t *m = hitec_prog_message(&hp.prog);
    char lines[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE];
    const int n = hitec_panel_message_lines(m, lines);
    size_t at = 0;

    s_txt.lines = 0;
    s_txt.bottom[0] = '\0';
    for (int i = 0; i < n; ++i) {
        bool said = false;

        for (int j = 0; j < hp.f.view.notes; ++j) {
            said = said || strcmp(lines[i], hp.f.view.note[j]) == 0;
        }
        if (said) {
            continue;
        }
        memcpy(s_txt.msg[s_txt.lines++], lines[i], sizeof(lines[i]));
        at += (size_t)snprintf(s_txt.bottom + at, sizeof(s_txt.bottom) - at,
                               "%s%s", at > 0u ? "\n" : "", lines[i]);
        at = at < sizeof(s_txt.bottom) ? at : sizeof(s_txt.bottom) - 1u;
    }
    if (s_txt.loop && m->level != HITEC_PROG_WARN
        && m->level != HITEC_PROG_DANGER) {
        s_txt.lines = 0;
        return;
    }
    if (s_txt.lines > 0) {
        hp.f.view.bottom = s_txt.bottom;
        hp.f.view.bottom_level = msg_level(m->level);
    }
}

/**
 * @brief The PORT button's fill by the programmer's state: CONNECT green,
 *        ABORT and LEAVE red while the port is the programmer's, WAIT
 *        amber while it is let go, RELEASE neutral.
 * @return the sprog_fill_t
 */
static uint8_t port_fill(void)
{
    switch (hitec_prog_state(&hp.prog)) {
    case HITEC_PROG_PWM:
        return SPROG_F_GO;
    case HITEC_PROG_HELD:
    case HITEC_PROG_OPENING:
    case HITEC_PROG_ENTERING:
    case HITEC_PROG_READING:
    case HITEC_PROG_RECORDING:
    case HITEC_PROG_BACKUP:
    case HITEC_PROG_WRITING:
        return SPROG_F_ACTIVE;
    case HITEC_PROG_CLEARING:
    case HITEC_PROG_LEAVING:
    case HITEC_PROG_RELEASING:
    case HITEC_PROG_MOVING:
        return SPROG_F_WAITING;
    default:
        return SPROG_F_SUNK;
    }
}

/**
 * @brief The head: the port, its pin and the series over the receive mode
 *        and the supply, in the warning colour with no pin; CONNECT green,
 *        a 2s hold fading to the red of a held port.
 * @param cl its cell
 */
static void head_cell(sprog_cell_t *cl)
{
    hp.f.view.head[0] = hpn_port_line(0, s_txt.head[0],
                                      sizeof(s_txt.head[0]))
                            ? s_txt.head[0] : NULL;
    hp.f.view.head[1] = hpn_port_line(1, s_txt.head[1],
                                      sizeof(s_txt.head[1]))
                            ? s_txt.head[1] : NULL;
    hp.f.view.head_warn = hpn_port_unbound();
    hp.f.view.port_pick = hpn_port_pick_row() != NULL;
    cl->fill = port_fill();
    cl->fill_to = SPROG_F_ACTIVE;
    if (cl->press != SPROG_P_OFF && hpn_switches_supply()
        && hitec_prog_state(&hp.prog) == HITEC_PROG_PWM) {
        cl->press = SPROG_P_HOLD;
        cl->hint = TX_HOLD_HINT_CONNECT;
    }
}

/**
 * @brief An action's cell: its fill and how it takes a press.
 * @param r  the row
 * @param cl its cell
 */
static void act_cell(const hpn_row_t *r, sprog_cell_t *cl)
{
    const hitec_prog_t *p = &hp.prog;
    const int side = hitec_prog_find_side(p);

    switch (r->what) {
    case A_PORT:
        head_cell(cl);
        break;
    case A_MOVE:
        /* Red while it drives the horn, amber while it ends. */
        cl->fill = hitec_prog_move_on(p) ? SPROG_F_ACTIVE
                   : hitec_prog_state(p) == HITEC_PROG_MOVING
                       ? SPROG_F_WAITING : SPROG_F_GO;
        cl->look = SPROG_L_LABEL_DIM;
        if (hitec_prog_move_on(p)) {
            cl->press = SPROG_P_STOP;
        }
        break;
    case A_WRITE:
        /* Green fading to the amber of the write's steps. */
        cl->fill = SPROG_F_GO;
        cl->fill_to = SPROG_F_WAITING;
        if (cl->press != SPROG_P_OFF) {
            cl->press = SPROG_P_HOLD;
            cl->hint = TX_HOLD_HINT_WRITE;
        }
        break;
    case A_CLEAR:
        cl->fill = cl->press != SPROG_P_OFF ? SPROG_F_WARN : SPROG_F_SUNK;
        break;
    case A_FIND:
        /* LEFT END and RIGHT END; the side that searches in warn. */
        snprintf(cl->value, sizeof(cl->value), "%s", TR(HP_END_L));
        cl->value2 = TR(HP_END_R);
        cl->fill = side == (int)HITEC_FIND_LEFT ? SPROG_F_WARN : SPROG_F_SUNK;
        cl->fill2 = side == (int)HITEC_FIND_RIGHT ? SPROG_F_WARN
                                                  : SPROG_F_SUNK;
        cl->press = !hpn_find_live(false) ? SPROG_P_OFF
                    : side == (int)HITEC_FIND_LEFT ? SPROG_P_STOP
                                                   : SPROG_P_TAP;
        cl->press2 = !hpn_find_live(true) ? SPROG_P_OFF
                     : side == (int)HITEC_FIND_RIGHT ? SPROG_P_STOP
                                                     : SPROG_P_TAP;
        cl->look = SPROG_L_LABEL_DIM;
        break;
    default:
        break;
    }
}

/**
 * @brief Cell @p i of the tab shown: its label, its text, its fill and
 *        how it takes a press.
 * @param i the cell
 */
static void fill_cell(int i)
{
    const hpn_row_t *r = &hpn_rows[hp.f.cell_row[i]];
    sprog_cell_t *cl = &hp.f.view.cell[i];
    bool warn = false;

    if (cl->kind == SPROG_K_GAP) {
        return;
    }
    cl->label = hpn_label(r);
    hpn_value_text(r, cl->value, sizeof(cl->value), &warn);
    cl->press = hpn_row_live(r) ? SPROG_P_TAP : SPROG_P_OFF;
    if (r->kind == K_ACT) {
        act_cell(r, cl);
        return;
    }
    /* A value: flat, its label dim; the accent fill beside its `*` where
     * the draft changes it, the position MOVE SERVO's horn follows with
     * its label in the accent. */
    cl->fill = hpn_row_edited(r) ? SPROG_F_ACCENT : SPROG_F_SUNK;
    cl->look = SPROG_L_FLAT | SPROG_L_LABEL_DIM
               | (warn ? SPROG_L_INK_WARN : 0u)
               | (cl->kind == SPROG_K_STEP && hp.prog.mv.follow == r->what
                      ? SPROG_L_LABEL_ACCENT : 0u);
}

void hpn_view(void)
{
    s_txt.loop = false;
    hpn_place();
    for (int i = 0; i < hp.f.cells; ++i) {
        fill_cell(i);
    }
    if (sprog_tab(&hp.f) == HITEC_PANEL_SERVO) {
        servo_notes();
    } else {
        value_notes();
    }
    bottom_line();
}

const char *hitec_panel_note(int i)
{
    hpn_view();
    if (i >= 0 && i < hp.f.view.notes) {
        return hp.f.view.note[i];
    }
    i -= hp.f.view.notes;
    return i >= 0 && i < s_txt.lines ? s_txt.msg[i] : NULL;
}

gfx_rect_t hitec_panel_notes_drawn(void)
{
    const int top = hp.area.y + HP_ROW0;
    int y;

    hpn_view();
    y = top + hp.f.lines * HP_PITCH;
    if (hp.f.view.notes == 0 && hp.f.view.bottom == NULL) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    return (gfx_rect_t){ hp.area.x, (int16_t)y, hp.area.w,
                         (int16_t)(hpn_foot() - y) };
}

/**
 * @brief The press and the hold of the overlay as the frame draws them:
 *        the row, list entry or key the finger holds, and CONNECT's or
 *        WRITE n VALUES' hold while it runs or flashes.
 * @return the hold the frame draws, to take back after the draw; NULL
 */
static ui_hold_t *press_to_frame(void)
{
    sprog_t *f = &hp.f;
    ui_hold_t *h = NULL;

    f->have = hp.have;
    f->id = hp.id;
    f->row = (int16_t)hp.row;
    f->key = (int16_t)(hp.pressed == P_ROW && hp.row >= 0
                               && hpn_rows[hp.row].kind == K_ACT
                               && hpn_rows[hp.row].what == A_FIND
                               && hp.find_right
                           ? 1 : 0);
    switch (hp.pressed) {
    case P_ROW:           f->on = SPROG_ON_ROW;         break;
    case P_CONNECT:
    case P_WRITE:         f->on = SPROG_ON_HOLD;        break;
    case P_PLUS:          f->on = SPROG_ON_PLUS;        break;
    case P_MINUS:         f->on = SPROG_ON_MINUS;       break;
    case P_CHOICE:        f->on = SPROG_ON_LIST;        break;
    case P_CHOICE_CANCEL: f->on = SPROG_ON_LIST_CANCEL; break;
    case P_TAB:           f->on = SPROG_ON_TAB;         break;
    default:              f->on = SPROG_ON_NONE;        break;
    }
    f->hold_row = -1;
    if (hp.conn.down || ui_hold_flashing(&hp.conn)) {
        h = &hp.conn;
        f->hold_row = 0;            /* PORT, hpn_rows[0] */
    } else if (hp.wr.down || ui_hold_flashing(&hp.wr)) {
        const hpn_row_t *w = hpn_row_at(HITEC_PANEL_SERVO, 3, 1);

        h = w != NULL ? &hp.wr : NULL;
        f->hold_row = w != NULL ? (int16_t)(w - hpn_rows) : -1;
    }
    if (h != NULL) {
        f->hold = *h;
    }
    return h;
}

/**
 * @brief The page's hold button: sunk, fading to the page's edge colour
 *        across its hold, flashing as it fires.
 * @param c the canvas
 */
static void draw_apply(gfx_canvas_t *c)
{
    const gfx_color_t edge = ui_theme_color(hp.page.danger ? UI_C_DANGER
                                                           : UI_C_WARN);
    ui_hold_button(c, hitec_panel_apply_rect(), hp.page.apply,
                   ui_theme_color(UI_C_PANEL_SUNK), edge, &hp.page.hold,
                   hp.page.down, true, HP_FONT);
}

/**
 * @brief Draws a hold page: its title, its lines and the two buttons.
 * @param c the canvas
 */
static void draw_hold(gfx_canvas_t *c)
{
    const gfx_rect_t a = hp.area;
    const gfx_color_t edge = ui_theme_color(hp.page.danger ? UI_C_DANGER
                                                           : UI_C_WARN);
    int y = a.y + HP_PAGE_TOP;

    gfx_draw_rect(c, a.x, a.y, a.w, a.h, edge);
    gfx_draw_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2, edge);
    gfx_draw_rect(c, a.x + 2, a.y + 2, a.w - 4, a.h - 4, edge);
    gfx_text(c, a.x + 12, a.y + 16, hp.page.title, HP_TITLE_FONT, edge, 1);
    /* 8px between the lines, less where that would take them under the
     * buttons: SERVO's left card is 428px tall. */
    int rows = 0;
    for (int i = 0; i < hp.page.count; ++i) {
        rows += hitec_panel_text_rows(hp.page.lines[i]);
    }
    const int room = hitec_panel_apply_rect().y - 8 - y - rows * HP_LINE_H;
    int gap = 8;
    if (hp.page.count > 1 && room < 8 * (hp.page.count - 1)) {
        gap = room > 0 ? room / (hp.page.count - 1) : 0;
    }
    for (int i = 0; i < hp.page.count; ++i) {
        y = hpn_text_rows(c, a.x + 12, y, hp.page.lines[i],
                          level_color(hp.page.level[i]))
            + gap;
    }
    draw_apply(c);
    /* BACK on the LEAVE page that lists unwritten edits: they stay. */
    ui_button_font(c, hitec_panel_cancel_rect(), hitec_panel_cancel_label(),
                   ui_theme_color(UI_C_PANEL_SUNK),
                   hp.have && hp.pressed == P_CANCEL, true, HP_FONT);
}

/**
 * @brief The overlay less the flash of a page that closed as its hold
 *        fired (hitec_panel_render()).
 * @param c the canvas
 */
static void render_body(gfx_canvas_t *c)
{
    const gfx_rect_t a = hp.area;
    ui_hold_t *h;

    if (hp.kp.open) {
        ui_keypad_render(&hp.kp, c);
        return;
    }
    if (hp.tk.open) {
        ui_textkey_render(&hp.tk, c);
        return;
    }
    if (hp.page.open) {
        gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
        draw_hold(c);
        return;
    }
    /* The tab or the list, drawn by the frame from the view. */
    hpn_view();
    h = press_to_frame();
    sprog_render(&hp.f, c);
    if (h != NULL) {
        *h = hp.f.hold;             /* where the hold was drawn */
    }
}

void hitec_panel_render(gfx_canvas_t *c)
{
    render_body(c);
    /* A page closes as its hold fires: its button flashes where it was
     * until the flash ends (hitec_panel_tick()). */
    if (!hp.page.open && ui_hold_flashing(&hp.page.hold)) {
        draw_apply(c);
    }
}
