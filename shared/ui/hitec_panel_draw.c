/**
 * @file hitec_panel_draw.c
 * @brief The frame's view of the Hitec programmer's tab shown: its cells
 *        (text, fill, how each takes a press: a tap, a 2s hold, a stop on
 *        the press, off), the head, the notes and the last message.  The
 *        frame draws it with the tabs, the list, the keypad, the keyboard
 *        and the hold page.
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
#include "prog5v_box.h"
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
 * @brief Whether the PORT button reads ABORT: the entry, a read or a write
 *        is under way (act_value() in hitec_panel_text.c).
 * @return true while it does
 */
static bool abort_shows(void)
{
    switch (hitec_prog_state(&hp.prog)) {
    case HITEC_PROG_OPENING:
    case HITEC_PROG_ENTERING:
    case HITEC_PROG_READING:
    case HITEC_PROG_RECORDING:
    case HITEC_PROG_BACKUP:
    case HITEC_PROG_WRITING:
        return true;
    default:
        return false;
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
    if (cl->press == SPROG_P_OFF) {
        return;
    }
    if (abort_shows()) {
        cl->press = SPROG_P_STOP;           /* ABORT acts on the press */
    } else if (hpn_switches_supply()
               && hitec_prog_state(&hp.prog) == HITEC_PROG_PWM) {
        /* CONNECT that switches the port's supply on: a 2s hold, and no
         * hold from a low carrier input.  A servo the operator powers
         * takes a tap. */
        cl->press = SPROG_P_HOLD;
        cl->hint = TX_HOLD_HINT_CONNECT;
        cl->why = prog5v_box_input_why();
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
    sprog_notes_shown_t sh;
    int top;
    int y;

    hpn_view();
    sprog_notes_shown(&hp.f, -1, &sh);
    if (sh.notes == 0 && sh.bottom_rows == 0) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    top = sh.notes != 0 ? sh.top : sh.bottom_y;
    y = sh.bottom_rows != 0 ? sprog_foot(&hp.f) : sh.end;
    return (gfx_rect_t){ hp.area.x, (int16_t)top, hp.area.w,
                         (int16_t)(y - top) };
}

void hitec_panel_render(gfx_canvas_t *c)
{
    /* The tab, the list, the keypad, the keyboard or the page, and the
     * flash of a page that closed as its hold fired. */
    hpn_view();
    sprog_render(&hp.f, c);
}
