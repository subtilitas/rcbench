/**
 * @file kst_panel_draw.c
 * @brief The drawing of the programmer's overlay.
 *
 * The drawing of the programmer's overlay for KST and Chaservo servos: the
 * notes under a page -- the standing message, the state line, a page of
 * notes and the message -- and the pages, their rows, the list of choices
 * and the warning pages.  The state and the seam are kst_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_panel.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "kst_port.h"
#include "kst_reg.h"
#include "kst_session.h"
#include "link_pages.h"
#include "ui_board.h"
#include "ui_card_text.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (ROW0, PITCH, ROW_H) and k
 * names the state, which no header included after it may meet. */
#include "kst_panel_int.h"

/** @brief A warning page's pitches between its lines, the widest that
 *         fits first: a line of one row at 28px leaves 8px under a row
 *         of KP_FONT. */
static const uint8_t k_warn_pitch[] = { 28, 26, 24, 22, LINE_H };

/**
 * @brief A button of the overlay, its label in KP_FONT.
 * @param c the canvas
 * @param r the button
 * @param label its label
 * @param fill its colour
 * @param pressed lightens the fill
 * @param enabled false dims it
 */
static void kp_button(gfx_canvas_t *c, gfx_rect_t r, const char *label,
                      gfx_color_t fill, bool pressed, bool enabled)
{
    ui_button_font(c, r, label, fill, pressed, enabled, KP_FONT);
}

/* --------------------------------------------------------------------- draw */

/**
 * @brief Returns the text colour of a warning level: danger, warning or plain.
 */
static gfx_color_t level_color(unsigned level)
{
    return level == KST_PROG_DANGER ? ui_theme_color(UI_C_DANGER)
         : level == KST_PROG_WARN   ? ui_theme_color(UI_C_WARN)
                                    : ui_theme_color(UI_C_TEXT);
}

/**
 * @brief A note under a page's rows.  Its place is @p slot rows under the last
 *        row of the SERVO tab; a note above it that wraps, or the message under
 *        it, moves it.
 */
typedef struct {
    const char *s;                      /**< the note's text */
    int         slot;                   /**< place in rows under the last row of the SERVO tab */
    gfx_color_t color;                  /**< the text colour */
    bool        standing;               /**< stays on the first page, whole */
} note_t;

/** @brief Most notes a page holds. */
#define NOTES_MAX 5
/** @brief The notes' first place: 8px under the SERVO tab's fifth row. */
#define NOTE_Y    (ROW0 + 4 * PITCH + ROW_H + 8)

/** @brief Writes note @p i of @p n and returns the next index. */
static int note(note_t *n, int i, int slot, const char *s, gfx_color_t color)
{
    n[i] = (note_t){ .s = s, .slot = slot, .color = color };
    return i + 1;
}

/**
 * @brief What stands until it is dealt with: up to two lines on the SERVO tab.
 *        Returns the lines used; @p warn is set for amber, cleared for red.
 */
static int standing(const char *out[2], bool *warn)
{
    const kst_prog_t *p = &k.prog;

    *warn = false;
    if (p->f.stopped) {
        *warn = true;
        out[0] = TR(KP_M_STOPPED);
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

    if (n > 0 && k.tabs.selected == PG_SERVO && standing(st, &warn) > 0
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
        if (!kpn_on_port()) {
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
        return TR(KP_NEED_TARGET_2);
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

/** @brief What stands until it is dealt with, over the last message. */
static int notes_standing(note_t *n, int i)
{
    const char *st[2];
    bool warn;
    const int lines = standing(st, &warn);

    for (int j = 0; j < lines; ++j) {
        i = note(n, i, 2 + j, st[j],
                 ui_theme_color(warn ? UI_C_WARN : UI_C_DANGER));
        n[i - 1].standing = true;
    }
    return i;
}

/**
 * @brief The notes of the page shown, in their order down the page.  Their text
 *        is static: the caller draws it after this returns.
 * @param n the notes, NOTES_MAX of them
 * @return the notes written
 */
static int page_notes(note_t *n)
{
    static char buf[3][KST_PANEL_LINE];
    const kst_prog_t *p = &k.prog;
    const gfx_color_t faint = ui_theme_color(UI_C_TEXT_FAINT);
    second_t level;
    const char *second;
    int i = 0;

    switch (k.tabs.selected) {
    case PG_SERVO:
        state_line(buf[0], sizeof(buf[0]));
        i = note(n, i, 0, buf[0], ui_theme_color(UI_C_TEXT));
        second = state_second(&level);
        if (second != NULL) {
            /* The type is the TYPE row's value too. */
            i = note(n, i, 1, second,
                     level == SECOND_TYPE   ? ui_theme_color(UI_C_ACCENT)
                     : level == SECOND_WARN ? ui_theme_color(UI_C_WARN)
                                            : faint);
        }
        i = notes_standing(n, i);
        if (kst_prog_backed_up(p)) {
            snprintf(buf[1], sizeof(buf[1]), TR(KP_BACKUP_FILE),
                     kpn_shown(kst_prog_backup_name(p)));
            i = note(n, i, 4, buf[1], faint);
        } else if (kst_prog_no_file(p)) {
            i = note(n, i, 4, kpn_card_tr(UI_CARD_AT_KP_NO_BACKUP),
                     ui_theme_color(UI_C_WARN));
        }
        break;
    case PG_FAILSAFE:
        i = note(n, i, -3, TR(KP_FS_NOTE_1), faint);
        i = note(n, i, -2, TR(KP_FS_NOTE_2), faint);
        i = note(n, i, 0, TR(KP_EDIT_NOTE_1), faint);
        i = note(n, i, 1, TR(KP_EDIT_NOTE_2), faint);
        break;
    case PG_LOCKED:
        i = note(n, i, 0, TR(KP_LOCKED_NOTE), faint);
        if (kst_prog_have_servo(p)) {
            const unsigned pair = kst_prog_servo(p)->r[KST_REG_NODE_ADDR];

            snprintf(buf[0], sizeof(buf[0]), TR(KP_PAIRING), pair);
            i = note(n, i, 1, buf[0], ui_theme_color(UI_C_TEXT_DIM));
            if (pair != 0x00u && pair != 0xFFu) {
                snprintf(buf[1], sizeof(buf[1]), TR(KP_PAIRING_CARD), pair);
                i = note(n, i, 2, buf[1], faint);
            }
        }
        if (kst_prog_load_skipped(p) != 0u) {
            if (kpn_ones(kst_prog_load_skipped(p)) == 1u) {
                snprintf(buf[2], sizeof(buf[2]), "%s",
                         TR(KP_LOCKED_SKIPPED_ONE));
            } else {
                snprintf(buf[2], sizeof(buf[2]), TR(KP_LOCKED_SKIPPED),
                         kpn_ones(kst_prog_load_skipped(p)));
            }
            i = note(n, i, 3, buf[2], ui_theme_color(UI_C_WARN));
        }
        break;
    case PG_PULSE:
        i = note(n, i, -1, TR(KP_ANGLE_NOTE), faint);
        i = note(n, i, 0, TR(KP_EDIT_NOTE_1), faint);
        i = note(n, i, 1, TR(KP_EDIT_NOTE_2), faint);
        break;
    default:
        i = note(n, i, 0, TR(KP_EDIT_NOTE_1), faint);
        i = note(n, i, 1, TR(KP_EDIT_NOTE_2), faint);
        break;
    }
    return i;
}

int kst_panel_pack_notes(const int *rows, const bool *standing, int count,
                         int room, int *page)
{
    int need = 0;
    int held = 0;
    int pages = 1;
    int used;

    for (int i = 0; i < count; ++i) {
        need += rows[i];
        held += standing[i] ? rows[i] : 0;
    }
    if (need <= room) {
        for (int i = 0; i < count; ++i) {
            page[i] = 0;
        }
        return 1;
    }
    /* The standing notes take the first page before any other.  With no
     * row left beside them for the page count, they yield to the message:
     * every note pages in pages of room - 1 rows, the standing ones first,
     * and a note taller than a page waits for the message to go. */
    if (held > 0 && held > room - 1) {
        used = 0;
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < count; ++i) {
                if (standing[i] != (pass == 0)) {
                    continue;
                }
                if (rows[i] > room - 1) {
                    page[i] = -1;
                    continue;
                }
                if (used > 0 && used + rows[i] > room - 1) {
                    ++pages;
                    used = 0;
                }
                page[i] = pages - 1;
                used += rows[i];
            }
        }
        return pages;
    }
    /* A page keeps its last row for the page count. */
    used = held;
    for (int i = 0; i < count; ++i) {
        if (standing[i]) {
            page[i] = 0;
            continue;
        }
        if (used > 0 && used + rows[i] > room - 1) {
            ++pages;
            used = 0;
        }
        page[i] = pages - 1;
        used += rows[i];
    }
    return pages;
}

/**
 * @brief Static: the message's lines, written on the UI task's deepest path.
 */
static char s_msg[KST_PANEL_MSG_LINES][KST_PANEL_LINE];

/**
 * @brief Where the notes go: their rows from the first note's place to the
 *        message, the page each takes, and where the shown page puts each line.
 */
typedef struct {
    note_t n[NOTES_MAX];                /**< the notes, in their order down the page */
    int    count;                       /**< notes used */
    int    rows[NOTES_MAX];             /**< rows each note takes */
    int    lines;                       /**< the message's lines in s_msg */
    int    msg_rows;                    /**< rows the message takes */
    int    top;                         /**< the first note's place */
    int    bottom;                      /**< the lowest row the notes may reach */
    int    room;                        /**< rows for the notes above the message */
    int    page[NOTES_MAX];             /**< page each note is on */
    int    pages;                       /**< pages in all */
    int    on;                          /**< the page shown */
    int    y[NOTES_MAX];                /**< each note's first row; -1 off the page */
    int    count_y;                     /**< the page count's row; -1 for none */
    int    msg_y;                       /**< the message's first row; -1 for none */
    int    msg_shown;                   /**< the message's lines that are drawn */
} notes_plan_t;

/**
 * @brief Static: the notes are planned on the UI task's deepest path.  One plan
 *        serves the draw and every query; none of them calls another.
 */
static notes_plan_t s_plan;

/**
 * @brief Plans the notes of the page shown: their rows, pages and places, and
 *        the message's.
 */
static void plan_notes(notes_plan_t *p)
{
    bool held[NOTES_MAX] = { false };
    uint32_t sig = 2166136261u;

    p->count = page_notes(p->n);
    p->lines = message_shown(s_msg);
    p->msg_rows = 0;
    p->bottom = kpn_notes_foot();
    p->top = p->bottom;
    for (int i = 0; i < p->lines; ++i) {
        p->msg_rows += kst_panel_text_rows(s_msg[i]);
    }
    for (int i = 0; i < p->count; ++i) {
        const int at = k.area.y + NOTE_Y + p->n[i].slot * LINE_H;

        p->rows[i] = kst_panel_text_rows(p->n[i].s);
        held[i] = p->n[i].standing;
        p->top = at < p->top ? at : p->top;
        sig = (sig ^ (uint32_t)(p->n[i].slot + 8)) * 16777619u;
        sig = (sig ^ (uint32_t)p->rows[i]) * 16777619u;
    }
    p->room = (p->bottom - p->top) / LINE_H - p->msg_rows;
    p->pages = kst_panel_pack_notes(p->rows, held, p->count, p->room,
                                    p->page);
    /* Another set of notes starts on its first page. */
    sig = (sig ^ (uint32_t)p->count) * 16777619u;
    sig = (sig ^ (uint32_t)p->msg_rows) * 16777619u;
    sig = (sig ^ (uint32_t)k.tabs.selected) * 16777619u;
    if (sig != k.note_sig) {
        k.note_sig = sig;
        k.note_page = 0;
    }
}

/**
 * @brief The highest a note rises to: 4px under the lowest row of the tab
 *        shown, the area's top for a tab with none.
 * @return the y, body pixels
 */
static int rows_foot(void)
{
    int y = k.area.y;

    for (int i = 0; i < ROW_COUNT; ++i) {
        if (kpn_rows[i].page == k.tabs.selected) {
            const gfx_rect_t r = kpn_row_rect(&kpn_rows[i]);

            y = r.y + r.h + 4 > y ? r.y + r.h + 4 : y;
        }
    }
    return y;
}

/**
 * @brief The page's notes from the top, each at its place or under the one above,
 *        and the last message from the area's last row up: three rows for one
 *        that takes fewer.  Notes that need more rows than there are above the
 *        message take pages: the standing notes are on the first, whole, the
 *        other notes page after them, the area's last notes row counts the pages,
 *        and a tap on the notes shows the next.  Where the standing notes and the
 *        message leave no row for the count, the other notes wait for the message
 *        to go.  Where the standing notes leave no row for the count beside the
 *        message, they yield to it: they page with the others, first, and one
 *        taller than a page waits for the message to go.  Every line of the
 *        message is drawn: the longest takes 5 rows of the 7 under the SERVO
 *        tab's last row on SERVO's left card, which end 4px above the alert band
 *        (kpn_notes_foot()).  No note rises above 4px under the tab's last row.
 */
static void layout_notes(notes_plan_t *p)
{
    int y = rows_foot();

    plan_notes(p);
    p->on = k.note_page < p->pages ? k.note_page : 0;
    p->count_y = -1;
    if (p->pages == 1) {
        int need = 0;

        for (int i = 0; i < p->count; ++i) {
            need += p->page[i] == 0 ? p->rows[i] : 0;
        }
        for (int i = 0; i < p->count; ++i) {
            const int at = k.area.y + NOTE_Y + p->n[i].slot * LINE_H;
            const int last = p->bottom - (p->msg_rows + need) * LINE_H;

            p->y[i] = -1;
            if (p->page[i] != 0) {
                continue;
            }
            if (y < (at < last ? at : last)) {
                y = at < last ? at : last;
            }
            need -= p->rows[i];
            p->y[i] = y;
            y += p->rows[i] * LINE_H;
        }
    } else {
        y = p->top;
        for (int i = 0; i < p->count; ++i) {
            p->y[i] = -1;
            if (p->page[i] == p->on) {
                p->y[i] = y;
                y += p->rows[i] * LINE_H;
            }
        }
        p->count_y = p->top + (p->room - 1) * LINE_H;
        y = p->count_y + LINE_H;
    }
    {
        const int at = p->bottom - (p->msg_rows > KST_PANEL_MSG_LINES
                                        ? p->msg_rows : KST_PANEL_MSG_LINES)
                                   * LINE_H;

        y = y > at ? y : at;
    }
    p->msg_y = y;
    p->msg_shown = 0;
    while (p->msg_shown < p->lines) {
        const int rows = kst_panel_text_rows(s_msg[p->msg_shown]);

        if (y + rows * LINE_H > p->bottom) {
            break;
        }
        y += rows * LINE_H;
        ++p->msg_shown;
    }
}

/** @brief Draws the notes and the message of the page shown. */
static void draw_notes(gfx_canvas_t *c)
{
    notes_plan_t *p = &s_plan;
    const kst_prog_msg_t *m = kst_prog_message(&k.prog);
    int y;

    layout_notes(p);
    for (int i = 0; i < p->count; ++i) {
        if (p->y[i] >= 0) {
            (void)kpn_text_rows(c, k.area.x + 10, p->y[i], p->n[i].s,
                            p->n[i].color);
        }
    }
    if (p->count_y >= 0) {
        char count[12];

        /* At most NOTES_MAX pages: the bytes keep the count's width. */
        snprintf(count, sizeof(count), "%u/%u", (unsigned)(uint8_t)(p->on + 1),
                 (unsigned)(uint8_t)p->pages);
        gfx_text_in(c, gfx_rect_make(k.area.x + 10, p->count_y,
                                     k.area.w - 20, LINE_H),
                    count, KP_FONT, ui_theme_color(UI_C_TEXT_FAINT),
                    1, GFX_ALIGN_RIGHT);
    }
    y = p->msg_y;
    for (int i = 0; i < p->msg_shown; ++i) {
        y = kpn_text_rows(c, k.area.x + 10, y, s_msg[i], level_color(m->level));
    }
}

/** @brief Returns the area the notes take, for the page-turning press. */
static gfx_rect_t notes_rect(const notes_plan_t *p)
{
    return (gfx_rect_t){ k.area.x, (int16_t)p->top, k.area.w,
                         (int16_t)(p->room * LINE_H) };
}

/* Whether (x, y) is on notes that take more than one page. */
bool kpn_notes_paged_at(int x, int y)
{
    plan_notes(&s_plan);
    return s_plan.pages > 1 && gfx_rect_contains(notes_rect(&s_plan), x, y);
}

gfx_rect_t kst_panel_notes_rect(void)
{
    plan_notes(&s_plan);
    return s_plan.pages > 1 ? notes_rect(&s_plan)
                            : (gfx_rect_t){ 0, 0, 0, 0 };
}

int kst_panel_note_pages(void)
{
    plan_notes(&s_plan);
    return s_plan.pages;
}

int kst_panel_note_page(void)
{
    layout_notes(&s_plan);
    return s_plan.on;
}

const char *kst_panel_shown_note(int i)
{
    layout_notes(&s_plan);
    for (int j = 0; j < s_plan.count; ++j) {
        if (s_plan.y[j] >= 0 && i-- == 0) {
            return s_plan.n[j].s;
        }
    }
    return NULL;
}

int kst_panel_shown_message_lines(void)
{
    layout_notes(&s_plan);
    return s_plan.msg_shown;
}

gfx_rect_t kst_panel_notes_drawn(void)
{
    int top = INT_MAX;
    int y;

    layout_notes(&s_plan);
    y = s_plan.msg_y;
    if (s_plan.msg_shown > 0) {
        top = s_plan.msg_y;
    }
    for (int i = 0; i < s_plan.msg_shown; ++i) {
        y += kst_panel_text_rows(s_msg[i]) * LINE_H;
    }
    for (int i = 0; i < s_plan.count; ++i) {
        const int end = s_plan.y[i] + s_plan.rows[i] * LINE_H;

        if (s_plan.y[i] >= 0) {
            top = s_plan.y[i] < top ? s_plan.y[i] : top;
            y = end > y ? end : y;
        }
    }
    return top == INT_MAX ? (gfx_rect_t){ 0, 0, 0, 0 }
                          : (gfx_rect_t){ k.area.x, (int16_t)top, k.area.w,
                                          (int16_t)(y - top) };
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
    case A_ENABLE:
        snprintf(out, n, "%s", kst_prog_write_enabled(p) ? TR(ON) : TR(OFF));
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
    static char buf[40];
    const row_t *r = kpn_row_at(page, row, col);

    if (r == NULL || r->kind != K_ACT) {
        return NULL;
    }
    act_value(r, buf, sizeof(buf));
    return buf;
}

/**
 * @brief Draws row @p i of the page shown: its label and its control,
 *        highlighted while pressed.
 * @param c the canvas
 * @param i the row's index in kpn_rows
 */
static void draw_row(gfx_canvas_t *c, int i)
{
    const row_t *r = &kpn_rows[i];
    const kst_prog_t *p = &k.prog;
    const gfx_rect_t rr = kpn_row_rect(r);
    const gfx_rect_t vr = kpn_value_rect(r);
    const bool live = kpn_row_live(r);
    const bool pressed = k.have && k.pressed == P_ROW && k.row == i;
    gfx_color_t fill = ui_theme_color(UI_C_PANEL_SUNK);
    char v[40];

    gfx_text(c, rr.x, rr.y + (ROW_H - LINE_H) / 2, kpn_row_label(r), KP_FONT,
             live || r->kind == K_LOCKED ? ui_theme_color(UI_C_TEXT_DIM)
                                         : ui_theme_color(UI_C_TEXT_FAINT),
             1);
    if (r->kind == K_ACT) {
        act_value(r, v, sizeof(v));
        if (r->what == A_ENABLE && kst_prog_write_enabled(p)) {
            fill = ui_theme_color(UI_C_DANGER);
        } else if (r->what == A_WRITE && live) {
            fill = ui_theme_color(UI_C_ACCENT);
        }
        kp_button(c, vr, v, fill, pressed, live);
        return;
    }
    if (!kst_prog_have_servo(p)) {
        kp_button(c, vr, "--", fill, false, false);
        return;
    }
    {
        const kst_field_id_t id = (kst_field_id_t)r->what;
        const bool diff = kpn_changed(id);
        char num[32];

        const bool skipped = kpn_in_set(kst_prog_load_skipped(p), id);

        kpn_fmt_field(id, kst_field_get(kst_prog_draft(p), id), num,
                      sizeof(num));
        /* A value that differs from the servo's: marked twice, by the fill
         * and by the star.  One the loaded file has otherwise and that is
         * never written: by the fill and the mark of the note below. */
        snprintf(v, sizeof(v), "%s%s", diff ? "* " : skipped ? "! " : "", num);
        if (diff) {
            fill = ui_theme_color(UI_C_ACCENT);
        } else if (skipped) {
            fill = ui_theme_color(UI_C_WARN);
        }
        kp_button(c, vr, v, fill, pressed, live);
        if (r->kind != K_STEP) {
            return;
        }
        kp_button(c, kpn_step_rect(r, false), "-", ui_theme_color(UI_C_PANEL_HI),
                  k.have && k.pressed == P_MINUS && k.row == i, live);
        kp_button(c, kpn_step_rect(r, true), "+", ui_theme_color(UI_C_PANEL_HI),
                  k.have && k.pressed == P_PLUS && k.row == i, live);
    }
}

/** @brief Draws the page shown: the tabs, CLOSE, its rows and its notes. */
static void draw_page(gfx_canvas_t *c)
{
    ui_tabs_render(&k.tabs, c);
    kp_button(c, kst_panel_close_rect(), TR(SUP_CLOSE),
              ui_theme_color(UI_C_PANEL_SUNK),
              k.have && k.pressed == P_CLOSE,
              !kst_panel_holds_port() || kpn_programming());
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (kpn_rows[i].page == k.tabs.selected) {
            draw_row(c, i);
        }
    }
    draw_notes(c);
}

/**
 * @brief Draws the list of choices: its title, an entry per value and CANCEL.
 */
static void draw_choice(gfx_canvas_t *c)
{
    const row_t *r = &kpn_rows[k.kp_row >= 0 ? k.kp_row : 0];

    gfx_text(c, k.area.x + 10, k.area.y + 14, k.ch.title, KP_FONT,
             ui_theme_color(UI_C_TEXT), 1);
    for (int i = 0; i < k.ch.count; ++i) {
        const bool current = k.ch.kind == C_STRETCH
            && k.ch.values[i]
               == (int)kst_field_get(kst_prog_draft(&k.prog),
                                     (kst_field_id_t)r->what);

        kp_button(c, kst_panel_choice_rect(i), k.ch.labels[i],
                  current ? ui_theme_color(UI_C_ACCENT)
                          : ui_theme_color(UI_C_PANEL_SUNK),
                  k.have && k.pressed == P_CHOICE && k.row == i, true);
    }
    kp_button(c, kst_panel_choice_cancel_rect(), TR(CANCEL),
              ui_theme_color(UI_C_PANEL_SUNK),
              k.have && k.pressed == P_CHOICE_CANCEL, true);
}

/** @brief Draws the warning page: the title, its lines and the hold button. */
static void draw_warning(gfx_canvas_t *c)
{
    const gfx_rect_t a = k.area;
    const gfx_color_t edge = k.warn.danger ? ui_theme_color(UI_C_DANGER)
                                           : ui_theme_color(UI_C_WARN);

    gfx_draw_rect(c, a.x, a.y, a.w, a.h, edge);
    gfx_draw_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2, edge);
    gfx_draw_rect(c, a.x + 2, a.y + 2, a.w - 4, a.h - 4, edge);
    gfx_text(c, a.x + 12, a.y + 16, k.warn.title, KP_TITLE_FONT, edge, 1);
    {
        /* The widest pitch at which the lines end 8px above the buttons;
         * the builders keep them to kpn_warn_room(), which fit at
         * LINE_H. */
        const int rows = kpn_warn_rows();
        const int room = kst_panel_apply_rect().y - 8 - (a.y + WARN_TOP)
                       - LINE_H - (rows - k.warn.count) * LINE_H;
        int pitch = LINE_H;
        int y = a.y + WARN_TOP;

        for (unsigned i = 0; i < sizeof(k_warn_pitch); ++i) {
            if ((k.warn.count - 1) * k_warn_pitch[i] <= room) {
                pitch = k_warn_pitch[i];
                break;
            }
        }
        for (int i = 0; i < k.warn.count; ++i) {
            y = kpn_text_rows(c, a.x + 12, y, k.warn.lines[i],
                          level_color(k.warn.level[i]))
              + pitch - LINE_H;
        }
    }
    kp_button(c, kst_panel_apply_rect(), k.warn.apply,
              ui_hold_fill(ui_theme_color(UI_C_PANEL_SUNK), edge,
                           k.warn.hold.held_s),
              k.warn.down, true);
    kp_button(c, kst_panel_cancel_rect(), TR(CANCEL),
              ui_theme_color(UI_C_PANEL_SUNK),
              k.have && k.pressed == P_CANCEL, true);
}

/**
 * @brief Draws the overlay: the keypad or the keyboard alone while one is
 *        open, else the warning, the list of choices or the page.
 * @param c the canvas
 */
void kst_panel_render(gfx_canvas_t *c)
{
    const gfx_rect_t a = k.area;

    if (k.kp.open) {
        ui_keypad_render(&k.kp, c);
        return;
    }
    if (k.tk.open) {
        ui_textkey_render(&k.tk, c);
        return;
    }
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    if (k.warn.open) {
        draw_warning(c);
        return;
    }
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_ACCENT));
    if (k.ch.open) {
        draw_choice(c);
        return;
    }
    draw_page(c);
}
