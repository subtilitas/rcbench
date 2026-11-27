/**
 * @file kst_panel_draw.c
 * @brief The drawing of the programmer's overlay.
 *
 * The drawing of the programmer's overlay for KST and Chaservo servos.
 * The tab shown draws through the servo programmers' frame (sprog.h):
 * kpn_view() fills its view -- the rows' values, fills and presses, the
 * SERVO tab's head, the notes (the state line, what stands until it is
 * dealt with, a tab's own note and the value tabs' hint) and the last
 * message on the bottom line.  The frame draws them, the keypad, the
 * keyboard, the list of choices and the hold page.  The state and the seam
 * are kst_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_panel.h"

#include <stdio.h>
#include <string.h>

#include "kst_port.h"
#include "kst_reg.h"
#include "kst_session.h"
#include "link_pages.h"
#include "ui_board.h"
#include "prog5v_box.h"
#include "prog_choice.h"
#include "sprog.h"
#include "ui_card_text.h"
#include "ui_text.h"
#include "ui_widgets.h"

/* Last: k names the state, which no header included after it may meet. */
#include "kst_panel_int.h"

/**
 * @brief What stands until it is dealt with: up to two lines on the SERVO tab.
 *        Returns the lines used; @p warn is set for amber, cleared for red.
 */
static int standing(const char *out[2], bool *warn)
{
    const kst_prog_t *p = &k.prog;

    *warn = false;
    if (p->f.stopped) {
        /* What stands is the latch, and the hold that resets it. */
        *warn = true;
        out[0] = TR(KP_M_STOP_LATCHED);
        return 1;
    }
    if (kst_prog_writing(p)) {
        /* Its record is on the card and is no interrupted write. */
        *warn = true;
        out[0] = TR(KP_WRITING);
        return 1;
    }
    if (kst_prog_journal_asks(p) != KST_PROG_MSG_NONE) {
        static char first[KST_PANEL_LINE];

        kpn_journal_line(first, TX_KP_M_JOURNAL_ASK, kst_prog_journal_arg(p));
        out[0] = first;
        out[1] = kst_prog_journal_asks(p) == KST_PROG_MSG_JOURNAL_SAME
                     ? TR(KP_M_JOURNAL_SAME) : TR(KP_M_JOURNAL_DONE);
        return 2;
    }
    if (kst_prog_journal(p)) {
        /* Static: the caller draws the line after this returns. */
        static char second[KST_PANEL_LINE];
        const unsigned arg = kst_prog_journal_arg(p);

        if (kst_prog_journal_other(p)) {
            out[0] = TR(KP_M_JOURNAL_OTHER);
            kpn_journal_line(second, TX_KP_M_JOURNAL_OTHER_2, arg);
        } else {
            out[0] = TR(KP_M_JOURNAL);
            kpn_journal_line(second, TX_KP_M_JOURNAL_2, arg);
        }
        out[1] = second;
        return 2;
    }
    if (kpn_flag(LINK_KS_F_LOCKED)) {
        out[0] = TR(KP_S_LOCKED);
        out[1] = TR(KP_S_LOCKED_2);
        return 2;
    }
    if (kst_prog_in_doubt(p)) {
        out[0] = TR(KP_DOUBT);
        out[1] = TR(KP_M_NEED_READ);
        return 2;
    }
    if (kst_prog_have_servo(p) && !kst_prog_editable(p)) {
        out[0] = TR(KP_M_READ_ONLY);
        out[1] = TR(KP_M_READ_ONLY_2);
        return 2;
    }
    if (p->journal) {
        *warn = true;
        out[0] = TR(KP_JOURNAL_KEPT);
        return 1;
    }
    return 0;
}

const char *kst_panel_standing_line(int i)
{
    const char *st[2];
    bool warn;

    return i >= 0 && i < standing(st, &warn) ? st[i] : NULL;
}

/**
 * @brief The lines of the last message the overlay shows.  One that only
 *        repeats what stands on the SERVO tab is left out.
 */
static int message_shown(char text[KST_PANEL_MSG_LINES][KST_PANEL_LINE])
{
    const int n = kst_panel_message_lines(kst_prog_message(&k.prog), text);
    const char *st[2];
    bool warn;

    if (n > 0 && kpn_page() == PG_SERVO && standing(st, &warn) > 0
        && strcmp(text[0], st[0]) == 0) {
        return 0;
    }
    return n;
}

/**
 * @brief Writes the programmer's state, with the port, into @p out: in a
 *        supply cycle the port's step of it.
 * @param out the line
 * @param n the size of @p out, bytes
 */
static void state_line(char *out, size_t n)
{
    const kst_prog_t *p = &k.prog;
    const unsigned ch = (unsigned)kst_prog_port(p) + 1u;
    const kst_prog_state_t st = kst_prog_state(p);

    if ((st == KST_PROG_ENTERING || st == KST_PROG_LEAVING)
        && p->f.rd.known && (p->f.rd.port == LINK_KST_RAIL_OFF
                             || p->f.rd.port == LINK_KST_RAIL_WAIT)) {
        /* The supply cycle of CONNECT or of LEAVE after a write. */
        if (p->f.rd.port == LINK_KST_RAIL_OFF) {
            snprintf(out, n, TR(KP_STATE_RAIL_OFF), ch);
        } else {
            snprintf(out, n, TR(KP_STATE_RAIL_WAIT), ch);
        }
        return;
    }
    switch (st) {
    case KST_PROG_DOWN:
        snprintf(out, n, "%s", TR(KP_ST_DOWN));
        break;
    case KST_PROG_NO_PAGE:
        snprintf(out, n, "%s", TR(KP_ST_NO_PAGE));
        break;
    case KST_PROG_SYNC:
        snprintf(out, n, "%s", TR(KP_STATE_SYNC));
        break;
    case KST_PROG_PWM:
        if (prog_choice_offered() && prog_choice_lock_line(out, n)) {
            /* The carrier's PORT row is locked: why. */
        } else if (!kpn_on_port()) {
            snprintf(out, n, "%s", TR(KP_NEED_TARGET));
        } else if (ui_board_prog_5v_pin() >= 0) {
            snprintf(out, n, TR(KP_STATE_PWM_5V), (unsigned)kpn_port_no() + 1u,
                     (unsigned)ui_board_prog_5v_pin());
        } else if (kpn_flag(LINK_KS_F_RAIL)) {
            snprintf(out, n, TR(KP_STATE_PWM), (unsigned)kpn_port_no() + 1u,
                     (unsigned)(kst_prog_supply_mv(p) / 1000u),
                     (unsigned)(kst_prog_supply_mv(p) % 1000u / 100u),
                     (unsigned)kst_prog_supply_ma(p));
        } else {
            snprintf(out, n, TR(KP_STATE_PWM_AS_IS),
                     (unsigned)kpn_port_no() + 1u);
        }
        break;
    case KST_PROG_ENTERING:
        snprintf(out, n, TR(KP_STATE_ENTERING), ch);
        break;
    case KST_PROG_PROGRAMMING:
        if (p->f.supply_read) {
            snprintf(out, n, TR(KP_STATE_PROG_V), ch,
                     (unsigned)(p->f.out_mv / 1000u),
                     (unsigned)(p->f.out_mv % 1000u / 10u),
                     (unsigned)p->f.out_ma);
        } else {
            snprintf(out, n, TR(KP_STATE_PROGRAMMING), ch);
        }
        break;
    case KST_PROG_WORKING:
        if (kst_prog_planning(p)) {
            snprintf(out, n, "%s", TR(KP_STATE_PLANNING));
        } else if (kst_prog_write_steps(p) == 1u) {
            snprintf(out, n, TR(KP_STATE_WRITING_ONE),
                     (unsigned)p->f.rd.frames);
        } else if (kst_prog_write_steps(p) != 0u) {
            snprintf(out, n, TR(KP_STATE_WRITING_N), kst_prog_write_steps(p),
                     (unsigned)p->f.rd.frames);
        } else if (kst_prog_writing(p)) {
            snprintf(out, n, TR(KP_STATE_WRITING), (unsigned)p->f.rd.frames);
        } else if (p->f.rd.frames == 1u) {
            snprintf(out, n, "%s", TR(KP_STATE_WORKING_ONE));
        } else {
            snprintf(out, n, TR(KP_STATE_WORKING), (unsigned)p->f.rd.frames);
        }
        break;
    default:
        snprintf(out, n, "%s", TR(KP_STATE_LEAVING));
        break;
    }
}

/* The texts below name the port's times: the output off for 1s, and the
 * sync 250ms after CONNECT where the port switches nothing (STOP, LOW and
 * the entry's low line, kst_port.h and kst_session.h). */
_Static_assert(KST_PORT_RAIL_OFF_MS == 1000u,
               "KP_CONNECT_CYCLE and KP_LEAVE_CYCLE name 1s");
_Static_assert(KST_PORT_STOP_MS + KST_PORT_LOW_MS + KST_ENTRY_LOW_US / 1000u
                   == 250u,
               "KP_NOPD_ENTRY names 250ms");
_Static_assert(KST_PORT_RAIL_WAIT_MS == 1000u, "KP_NOPD_ENTRY names 1s");

/** @brief The colour of a line under the state: faint, the accent or amber. */
typedef enum { SECOND_FAINT = 0, SECOND_TYPE, SECOND_WARN } second_t;

/**
 * @brief The line under the state: what the state needs, what CONNECT does
 *        to the supply, or the servo's type.
 *
 * The type the operator set is the accent's; the one 20k_sel suggests is a
 * guess and amber until the operator sets it.
 *
 * @param level receives the line's colour
 * @return the line, NULL for none; static for the formatted ones
 */
static const char *state_second(second_t *level)
{
    static char line[KST_PANEL_LINE];
    const kst_prog_t *p = &k.prog;
    const kst_prog_state_t st = kst_prog_state(p);

    *level = SECOND_FAINT;
    if (st == KST_PROG_NO_PAGE) {
        return TR(KP_NO_PAGE_2);
    }
    if (st == KST_PROG_PWM && !kpn_on_port()) {
        return ui_tr((ui_text_id_t)prog_choice_text(TX_KP_NEED_TARGET_2));
    }
    if (st == KST_PROG_PWM && !kpn_flag(LINK_KS_F_RAIL)) {
        /* The servo enters from a power-up the operator makes. */
        *level = SECOND_WARN;
        return TR(KP_NOPD_ENTRY);
    }
    if (st == KST_PROG_PWM && kst_prog_connect_cycles(p)) {
        return TR(KP_CONNECT_CYCLE);
    }
    if (kst_prog_type(p) != 0u) {
        *level = SECOND_TYPE;
        snprintf(line, sizeof(line), TR(KP_TYPE_SET), kst_prog_type(p));
        return line;
    }
    if (kst_prog_type_guess(p) != 0u) {
        *level = SECOND_WARN;
        snprintf(line, sizeof(line), TR(KP_TYPE_GUESS),
                 kst_prog_type_guess(p));
        return line;
    }
    return NULL;
}

const char *kst_panel_status_line(int i)
{
    second_t level;

    if (i == 0) {
        static char buf[KST_PANEL_LINE];

        state_line(buf, sizeof(buf));
        return buf;
    }
    return i == 1 ? state_second(&level) : NULL;
}

/**
 * @brief Writes the value of an action row, from the programmer's state, into
 *        @p out: for TYPE the operator's type, or the guess with its mark.
 * @param r the row
 * @param out the value
 * @param n the size of @p out, bytes
 */
static void act_value(const row_t *r, char *out, size_t n)
{
    const kst_prog_t *p = &k.prog;
    const kst_prog_state_t st = kst_prog_state(p);

    switch (r->what) {
    case A_PORT:
        snprintf(out, n, "%s",
                 st == KST_PROG_ENTERING || st == KST_PROG_WORKING
                     || kpn_leave_waits(p)
                     ? TR(KP_ABORT)
                 : st == KST_PROG_PROGRAMMING || st == KST_PROG_LEAVING
                     ? TR(KP_LEAVE) : TR(KP_CONNECT));
        break;
    case A_READ:
        snprintf(out, n, "%s", TR(KP_READ_ALL));
        break;
    case A_FILES:
        snprintf(out, n, "%s", kst_prog_card_busy(p) ? TR(KP_WAIT)
                                                     : TR(KP_CHOOSE));
        break;
    case A_RESTORE:
    case A_MORE:
        snprintf(out, n, "%s", TR(KP_CHOOSE));
        break;
    case A_TYPE:
        /* The operator's type; the one 20k_sel suggests with its mark. */
        if (kst_prog_type(p) != 0u) {
            snprintf(out, n, "V%u", kst_prog_type(p));
        } else if (kst_prog_type_guess(p) != 0u) {
            snprintf(out, n, "V%u?", kst_prog_type_guess(p));
        } else {
            snprintf(out, n, "--");
        }
        break;
    default:
        kpn_write_n(out, n, kst_prog_changes(p));
        break;
    }
}

const char *kst_panel_button(int page, int row, int col)
{
    static char buf[KST_PANEL_LINE];
    const row_t *r = kpn_row_at(page, row, col);

    if (r == NULL || r->kind != K_ACT) {
        return NULL;
    }
    act_value(r, buf, sizeof(buf));
    return buf;
}

/* ---------------------------------------------------------------- the view */

/**
 * @brief The texts the frame's view points at: kpn_view() writes them and
 *        the frame draws them after it returns.
 */
typedef struct {
    char state[KST_PANEL_LINE];         /**< the state line */
    char backup[KST_PANEL_LINE];        /**< the backup file's line */
    char pair[2][KST_PANEL_LINE];       /**< LOCKED's pairing lines */
    char skipped[KST_PANEL_LINE];       /**< LOCKED's values never written */
    /** @brief The last message's lines */
    char msg[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    int  lines;                         /**< the message's lines shown */
    /** @brief The same lines, one after the other, a '\n' between two */
    char bottom[KST_PANEL_MSG_LINES * KST_PANEL_LINE];
} view_text_t;

/** @brief The view's texts, static: the frame draws them after kpn_view(). */
static view_text_t s_view;

/**
 * @brief The frame's level of a message's level.
 * @param level the kst_prog_level_t
 * @return the sprog_level_t
 */
static uint8_t msg_level(unsigned level)
{
    return level == KST_PROG_DANGER ? SPROG_LV_DANGER
         : level == KST_PROG_WARN   ? SPROG_LV_WARN
                                    : SPROG_LV_TEXT;
}

/** @brief The SERVO tab's notes, top-down: the state, the line under it,
 *         what stands until it is dealt with and the backup file. */
static void servo_notes(void)
{
    const kst_prog_t *p = &k.prog;
    const char *st[2];
    bool warn;
    const int n = standing(st, &warn);
    second_t level;
    const char *second;

    state_line(s_view.state, sizeof(s_view.state));
    sprog_note(&k.f, s_view.state, SPROG_LV_TEXT);
    second = state_second(&level);
    /* The type is the TYPE row's value too. */
    sprog_note(&k.f, second,
               level == SECOND_TYPE   ? SPROG_LV_ACCENT
               : level == SECOND_WARN ? SPROG_LV_WARN
                                      : SPROG_LV_FAINT);
    for (int j = 0; j < n; ++j) {
        sprog_note(&k.f, st[j], warn ? SPROG_LV_WARN : SPROG_LV_DANGER);
    }
    if (kst_prog_backed_up(p)) {
        snprintf(s_view.backup, sizeof(s_view.backup), TR(KP_BACKUP_FILE),
                 kpn_shown(kst_prog_backup_name(p)));
        sprog_note(&k.f, s_view.backup, SPROG_LV_FAINT);
    } else if (kst_prog_no_file(p)) {
        sprog_note(&k.f, kpn_card_tr(UI_CARD_AT_KP_NO_BACKUP), SPROG_LV_WARN);
    } else if (kst_prog_changes(p) != 0u && kst_prog_save_none(p)) {
        /* WRITE's hold sends the edits with no page before them: the file
         * the write goes without is named here. */
        sprog_note(&k.f, kpn_card_tr(UI_CARD_AT_KP_NO_FILE), SPROG_LV_WARN);
    }
}

/** @brief The LOCKED tab's notes: what it is, the pairing value and the
 *         values a loaded file has that are never written. */
static void locked_notes(void)
{
    const kst_prog_t *p = &k.prog;

    sprog_note(&k.f, TR(KP_LOCKED_NOTE), SPROG_LV_FAINT);
    if (kst_prog_have_servo(p)) {
        const unsigned pair = kst_prog_servo(p)->r[KST_REG_NODE_ADDR];

        snprintf(s_view.pair[0], sizeof(s_view.pair[0]), TR(KP_PAIRING), pair);
        sprog_note(&k.f, s_view.pair[0], SPROG_LV_DIM);
        if (pair != 0x00u && pair != 0xFFu) {
            snprintf(s_view.pair[1], sizeof(s_view.pair[1]),
                     TR(KP_PAIRING_CARD), pair);
            sprog_note(&k.f, s_view.pair[1], SPROG_LV_FAINT);
        }
    }
    if (kst_prog_load_skipped(p) != 0u) {
        if (kpn_ones(kst_prog_load_skipped(p)) == 1u) {
            snprintf(s_view.skipped, sizeof(s_view.skipped), "%s",
                     TR(KP_LOCKED_SKIPPED_ONE));
        } else {
            snprintf(s_view.skipped, sizeof(s_view.skipped),
                     TR(KP_LOCKED_SKIPPED),
                     kpn_ones(kst_prog_load_skipped(p)));
        }
        sprog_note(&k.f, s_view.skipped, SPROG_LV_WARN);
    }
}

/** @brief The notes of the tab shown: SERVO's and LOCKED's own; a value
 *         tab's note, then the common hint. */
static void tab_notes(void)
{
    switch (kpn_page()) {
    case PG_SERVO:
        servo_notes();
        break;
    case PG_LOCKED:
        locked_notes();
        break;
    case PG_FAILSAFE:
        sprog_note(&k.f, TR(KP_FS_NOTE_1), SPROG_LV_FAINT);
        sprog_note(&k.f, TR(KP_FS_NOTE_2), SPROG_LV_FAINT);
        sprog_hint(&k.f);
        break;
    case PG_PULSE:
        sprog_note(&k.f, TR(KP_ANGLE_NOTE), SPROG_LV_FAINT);
        sprog_hint(&k.f);
        break;
    default:
        sprog_hint(&k.f);
        break;
    }
}

/** @brief The bottom line: the last message's lines, one under the other. */
static void bottom_line(void)
{
    const int n = message_shown(s_view.msg);
    size_t at = 0;

    s_view.lines = n;

    s_view.bottom[0] = '\0';
    for (int i = 0; i < n && i < KST_PANEL_MSG_LINES
                    && at < sizeof(s_view.bottom); ++i) {
        const int w = snprintf(s_view.bottom + at, sizeof(s_view.bottom) - at,
                               "%s%s", i > 0 ? "\n" : "", s_view.msg[i]);

        at += w > 0 ? (size_t)w : 0u;
    }
    k.f.view.bottom = n > 0 ? s_view.bottom : NULL;
    k.f.view.bottom_level = msg_level(kst_prog_message(&k.prog)->level);
}

/**
 * @brief The press a row's control takes now: PORT's CONNECT a 2s hold
 *        where it switches a supply on, its ABORT a stop; WRITE n VALUES a
 *        2s hold; any other a tap.
 * @param r the row, live
 * @param cl its cell
 */
static void act_press(const row_t *r, sprog_cell_t *cl)
{
    const kst_prog_t *p = &k.prog;
    const kst_prog_state_t st = kst_prog_state(p);

    if (r->what == A_PORT) {
        const bool held = st == KST_PROG_ENTERING || st == KST_PROG_WORKING
                          || st == KST_PROG_PROGRAMMING
                          || st == KST_PROG_LEAVING || kpn_leave_waits(p);

        /* Green while a press starts it, red while the port is the
         * programmer's (ABORT, LEAVE); CONNECT fades to that red. */
        cl->fill = held ? SPROG_F_ACTIVE : SPROG_F_GO;
        cl->fill_to = SPROG_F_ACTIVE;
        if (st == KST_PROG_ENTERING || st == KST_PROG_WORKING
            || kpn_leave_waits(p)) {
            cl->press = SPROG_P_STOP;
        } else if (st == KST_PROG_PWM && kpn_switches_supply()) {
            /* A 2s hold that switches the supply on; none from a low
             * carrier input, which the press names. */
            cl->press = SPROG_P_HOLD;
            cl->hint = TX_HOLD_HINT_CONNECT;
            cl->why = prog5v_box_input_why();
        }
    } else if (r->what == A_WRITE) {
        /* Green fading to the amber of the write's steps. */
        cl->fill = SPROG_F_GO;
        cl->fill_to = SPROG_F_WAITING;
        cl->press = SPROG_P_HOLD;
        cl->hint = TX_HOLD_HINT_WRITE;
    }
}

/**
 * @brief Cell @p cl of row @p i: its value, fill and press.
 * @param i  the row's index in kpn_rows
 * @param cl its cell
 */
static void fill_cell(int i, sprog_cell_t *cl)
{
    const row_t *r = &kpn_rows[i];
    const kst_prog_t *p = &k.prog;
    const bool live = kpn_row_live(r);

    cl->press = live ? SPROG_P_TAP : SPROG_P_OFF;
    if (r->kind == K_ACT) {
        act_value(r, cl->value, sizeof(cl->value));
        act_press(r, cl);
        if (!live) {
            cl->press = SPROG_P_OFF;
        }
        return;
    }
    if (r->kind == K_LOCKED) {
        cl->press = SPROG_P_OFF;
    }
    if (!kst_prog_have_servo(p)) {
        snprintf(cl->value, sizeof(cl->value), "--");
        cl->look = (uint8_t)(SPROG_L_NO_KEYS | (live ? SPROG_L_LABEL_DIM : 0u));
        cl->press = SPROG_P_OFF;
        return;
    }
    {
        const kst_field_id_t id = (kst_field_id_t)r->what;
        const bool diff = kpn_changed(id);
        const bool skipped = kpn_in_set(kst_prog_load_skipped(p), id);
        char num[20];

        kpn_fmt_field(id, kst_field_get(kst_prog_draft(p), id), num,
                      sizeof(num));
        /* A value that differs from the servo's: marked twice, by the fill
         * and by the star.  One the loaded file has otherwise and that is
         * never written: by the fill and the mark of the note below. */
        snprintf(cl->value, sizeof(cl->value), "%s%s",
                 diff ? "* " : skipped ? "! " : "", num);
        cl->fill = diff ? SPROG_F_ACCENT : skipped ? SPROG_F_WARN
                                                   : SPROG_F_SUNK;
    }
}

void kpn_view(void)
{
    const row_t *pick = kpn_port_pick_row();

    sprog_view_begin(&k.f, kpn_mask());
    for (int c = 0; c < k.f.cells; ++c) {
        fill_cell(k.f.cell_row[c], &k.f.view.cell[c]);
    }
    k.f.view.head[0] = kpn_head_line(0);
    k.f.view.head[1] = kpn_head_line(1);
    k.f.view.head_warn = !kpn_on_port();
    k.f.view.port_pick = pick != NULL;
    tab_notes();
    bottom_line();
}

/* --------------------------------------------------------------- the notes */

/**
 * @brief The page of notes shown, as the frame draws it, of the view just
 *        filled.
 * @param sh receives the page
 */
static void shown(sprog_notes_shown_t *sh)
{
    kpn_view();
    sprog_notes_shown(&k.f, -1, sh);
}

gfx_rect_t kst_panel_notes_rect(void)
{
    kpn_view();
    return sprog_notes_rect(&k.f);
}

int kst_panel_note_pages(void)
{
    kpn_view();
    return sprog_note_pages(&k.f);
}

int kst_panel_note_page(void)
{
    const int pages = kst_panel_note_pages();

    return k.f.note_page < pages ? k.f.note_page : 0;
}

const char *kst_panel_shown_note(int i)
{
    sprog_notes_shown_t sh;

    shown(&sh);
    return i >= 0 && i < sh.notes ? k.f.view.note[sh.note[i]] : NULL;
}

int kst_panel_shown_message_lines(void)
{
    sprog_notes_shown_t sh;
    int rows = 0;
    int n = 0;

    shown(&sh);
    for (int i = 0; i < s_view.lines && i < KST_PANEL_MSG_LINES; ++i) {
        rows += sprog_text_rows(&k.f, s_view.msg[i]);
        if (rows > sh.bottom_rows) {
            break;
        }
        ++n;
    }
    return n;
}

gfx_rect_t kst_panel_notes_drawn(void)
{
    sprog_notes_shown_t sh;
    int top;
    int y;

    shown(&sh);
    if (sh.notes == 0 && sh.bottom_rows == 0) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    top = sh.notes != 0 ? sh.top : sh.bottom_y;
    y = sh.bottom_rows != 0 ? sprog_foot(&k.f) : sh.end;
    return (gfx_rect_t){ k.area.x, (int16_t)top, k.area.w, (int16_t)(y - top) };
}

/**
 * @brief Draws the overlay: the keypad or the keyboard alone while one is
 *        open, else the hold page, the list of choices or the tab shown,
 *        all through the frame.
 * @param c the canvas
 */
void kst_panel_render(gfx_canvas_t *c)
{
    kpn_mode_sync();
    kpn_view();
    sprog_render(&k.f, c);
}
