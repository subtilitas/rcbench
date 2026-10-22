/**
 * @file
 * @brief ESC STICK's private seam between esc_stick_pages.c, the page set MOTOR &
 * ESC opens from PROGRAM's list (the session's state, its tap walk and its
 * hooks), and esc_stick_panel.c, the stick programmer's pages: the layout
 * both draw on, the state both read and write, the tap walk, the text
 * helpers both draw with, and the panel's side the page set calls.
 * Nothing outside shared/ui includes this header; the interface is
 * esc_stick_panel.h.
 *
 * The helpers below are static, and marked unused for esc_stick_pages.c,
 * which draws none of them: esc_stick_panel.c has its own copy, and GCC
 * inlines them or not.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gfx.h"
#include "prog_registry.h"
#include "touch_types.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W UI_PANEL_W
#define H UI_BODY_H

/*
 * A full-width session (prog_registry.h): MOTOR & ESC draws the output
 * strip's line above PROG_FULL_Y, and the pages lay out under it.  From
 * the top: the crumb row (BACK, the trail, SEARCH or TIMING), the device
 * card, the parameter card with ROWS_MAX rows, the help lines, and the
 * foot row of buttons.  The foot is the body's: BTN_Y and HELP_Y are where
 * they are on the 1024 x 540 px body whatever the top holds.
 */
#define PAD       UI_MARGIN
#define CRUMB_Y   (PROG_FULL_Y + 2)
#define DEV_Y     (CRUMB_Y + 34)
#define DEV_H     40
#define PARM_Y    (DEV_Y + DEV_H + 4)
#define BTN_Y     (H - PAD - 36)
#define HELP_Y    (BTN_Y - 32)
#define PARM_H    (HELP_Y - 8 - PARM_Y)

#define ROW_Y0    (PARM_Y + 34)
#define ROW_H     30
#define ROWS_MAX  7

/* The panels over the pages -- the RUN warning, the manual steps and a
 * run's prompt -- from PROG_FULL_Y to the body's margin, and the two
 * buttons at their foot, which end above the foot row's labels. */
#define SP_PANEL_Y  PROG_FULL_Y
#define SP_PANEL_H  (H - PAD - SP_PANEL_Y)
#define SP_PBTN_H   UI_BTN_H_TALL
#define SP_PBTN_Y   (H - PAD - 20 - SP_PBTN_H)

_Static_assert(ROW_Y0 + (ROWS_MAX - 1) * ROW_H + 24 <= PARM_Y + PARM_H - 4,
               "ROWS_MAX rows fit the parameter card");

/* The control column, sized to stop clear of the steppers: a value as long
 * as MEDIUM HIGH has to fit beside them, not under them. */
#define CTRL_X    400
#define CTRL_W    216
#define STEP_W    34

/*
 * Each page's drawer is its own function, so the main task's stack holds
 * the buffers of the page on screen and no other.  render() runs on the
 * panel's main task, which tools/stack_check.py holds to its stack.  GCC
 * inlines a static function called once, and inlined, every page's buffers
 * share render()'s frame: 1760 bytes at -O2.  Kept apart, render()'s frame
 * is 32 bytes and the largest page's 464.
 */
#define DRAWER __attribute__((noinline))

/* Where the session is: closed (PROGRAM's list shows), on the profile
 * list (the makers, or one maker's models), or on a profile's page. */
typedef enum { STAGE_CLOSED = 0, STAGE_LIST, STAGE_PAGE } prog_stage_t;

/*
 * A control on this screen is a tap: its press arms it and its release
 * activates it.  The release counts when the same contact makes it inside
 * the control, no further than TAP_SLOP from the press in x and in y, with
 * the screen still showing what it showed at the press, and is not an UP
 * made for the finger (TOUCH_FLAG_NO_TAP).  A finger that crosses a control
 * presses nothing.
 *
 * The controls are found by one walk, press_at(), run twice.  On the DOWN
 * it runs as PASS_PRESS: hit() notes the first control under the point and
 * answers false, so nothing acts.  On a release that counts it runs again
 * as PASS_TAP at the point of the press: hit() answers true and the control
 * acts, or does not where it has nothing to do, as a page key at the end of
 * its list.
 *
 * Three controls act on the press and are asked with hit_down(): HOLD TO
 * RUN, whose hold is the gesture; ABORT of a run, on the run's page and on
 * a manual step, which stops a motor and does so at once; and the search's
 * keyboard, whose keys follow their own press and release.
 */
#define TAP_SLOP 8

/* A helper of this header: static, and not warned of where unused. */
#define SP_SHARED static __attribute__((unused))
enum { PASS_PRESS = 0, PASS_TAP };

/* The state both files read and write.  The panel moves between the
 * stages and bumps the revision a render follows; it draws and takes the
 * session's controls in their places: BACK in the crumb row, READ and
 * WRITE at the foot (DEFAULTS and RUN, CLOSE and OK), the page keys, and
 * the rows with their steppers. */
typedef struct {
    prog_stage_t stage;

    gfx_rect_t back, read_btn, write_btn;
    gfx_rect_t page_up, page_dn;
    gfx_rect_t row[ROWS_MAX], down[ROWS_MAX], up[ROWS_MAX];

    /*
     * The press that may become a tap: the contact, where it came down,
     * the control under it, and what the screen showed then.  pass says
     * which of the two walks over the controls is running (hit()).
     */
    struct {
        bool       live;
        uint8_t    id;
        int16_t    x, y;
        gfx_rect_t rect;
        uint32_t   showing;
    } tap;
    int        pass;
    bool       found;
    gfx_rect_t found_rect;

    uint32_t rev;
} stick_shared_t;

/* In esc_stick_pages.c. */
extern stick_shared_t pg;

SP_SHARED bool hit(gfx_rect_t r, int px, int py)
{
    if (!gfx_rect_contains(r, px, py)) {
        return false;
    }
    if (pg.pass == PASS_TAP) {
        return true;
    }
    if (!pg.found) {
        pg.found = true;
        pg.found_rect = r;
    }
    return false;
}

SP_SHARED void draw_crumb(gfx_canvas_t *c, const char *trail)
{
    ui_button(c, pg.back, TR(LOG_BACK), ui_theme_color(UI_C_PANEL_HI), false,
              true);
    gfx_text(c, pg.back.x + pg.back.w + 16, CRUMB_Y + 8, trail,
             UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
}

SP_SHARED void widget_enum(gfx_canvas_t *c, int y, const char *text,
                        gfx_color_t ink)
{
    gfx_text_in(c, (gfx_rect_t){ CTRL_X, (int16_t)y, CTRL_W, 16 },
                text, UI_FONT_LABEL, ink, 1, GFX_ALIGN_RIGHT);
}

/* --- text in cells ------------------------------------------------------- */

/*
 * @p buf cut to @p max_chars cells, with ".." in place of what did not fit.
 * Cut between characters, never inside one: a cell is a code point and a
 * German letter is two bytes.
 */
SP_SHARED void sp_cut(char *buf, size_t n, int max_chars)
{
    if (max_chars < 3 || gfx_text_cells(buf) <= max_chars) {
        return;
    }
    const size_t at = gfx_text_prefix(buf, max_chars - 2);
    if (at + 3u <= n) {
        buf[at] = '.';
        buf[at + 1u] = '.';
        buf[at + 2u] = '\0';
    }
}

/*
 * @p src into @p dst, as much as fits with its terminator.  The copy every
 * row and line makes, without snprintf(): whatever the format, a call to
 * snprintf() reaches newlib's float conversion, and the panel's call graph
 * puts 1952 bytes of stack below it (tools/stack_check.py -v).
 */
SP_SHARED void sp_copy(char *dst, size_t n, const char *src)
{
    size_t i = 0;
    while (src != NULL && i + 1u < n && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

/* The bytes of the next line of @p at that fit @p cells: up to the last
 * space in it, or the whole width where no space falls after its start.
 * A line holds one cell at least, so every line ends. */
SP_SHARED size_t sp_line_len(const char *at, int cells)
{
    cells = (cells > 0) ? cells : 1;
    if (gfx_text_cells(at) <= cells) {
        return strlen(at);
    }
    const size_t len = gfx_text_prefix(at, cells);
    size_t cut = len;
    while (cut > 0u && at[cut] != ' ') {
        --cut;
    }
    return (cut > 0u) ? cut : len;
}

/*
 * @p text in lines of at most @p cells, broken after a space where one
 * falls in the line and inside a word where none does, from (x, y) down by
 * @p pitch.  At most @p max_lines lines; the last of them ends in ".." when
 * the text goes on.  Returns the lines drawn.  A manual step is the
 * profile's own text and can be up to ESC_MANUAL_ACTION_MAX bytes, so it is
 * wrapped, not cut to one line.
 */
SP_SHARED int sp_wrap(gfx_canvas_t *c, int x, int y, int pitch,
                   const char *text, int cells, int max_lines,
                   gfx_color_t ink)
{
    int lines = 0;
    const char *at = (text != NULL) ? text : "";
    while (*at == ' ') {
        ++at;
    }
    while (*at != '\0' && lines < max_lines) {
        char buf[192];
        ++lines;
        if (lines == max_lines) {
            /* The last line takes the rest, cut with ".." if it is more
             * than fits. */
            sp_copy(buf, sizeof(buf), at);
            sp_cut(buf, sizeof(buf), cells);
            at += strlen(at);
        } else {
            /* The line, no further than the text's end; the copy no
             * longer than the buffer. */
            const size_t rest = strlen(at);
            size_t len = sp_line_len(at, cells);
            len = (len < rest) ? len : rest;
            const size_t n = (len < sizeof(buf)) ? len : sizeof(buf) - 1u;
            memcpy(buf, at, n);
            buf[n] = '\0';
            at += len;
            /* len is at most strlen(at): at stops on the terminator. */
            /* NOLINTNEXTLINE(clang-analyzer-security.ArrayBound) */
            while (*at == ' ') {
                ++at;
            }
        }
        gfx_text(c, x, y, buf, UI_FONT_LABEL, ink, 1);
        y += pitch;
    }
    return lines;
}

/* The lines sp_wrap() would draw, without drawing them. */
static inline int sp_wrap_lines(const char *text, int cells, int max_lines)
{
    int lines = 0;
    const char *at = (text != NULL) ? text : "";
    while (*at == ' ') {
        ++at;
    }
    while (*at != '\0' && lines < max_lines) {
        at += sp_line_len(at, cells);
        while (*at == ' ') {
            ++at;
        }
        ++lines;
    }
    return lines;
}

/*
 * A line at the foot of a page, from @p x to @p right, left of the buttons
 * there: on one line level with their labels, or where it needs more, on
 * two from 2 px under their top, the second ending 2 px under their foot.
 */
SP_SHARED void sp_note(gfx_canvas_t *c, int x, int right, const char *text,
                    gfx_color_t ink)
{
    const int cells = (right - x) / 8;
    const int y = (sp_wrap_lines(text, cells, 2) > 1) ? BTN_Y + 2
                                                      : BTN_Y + 9;
    (void)sp_wrap(c, x, y, 18, text, cells, 2, ink);
}

/* --- the panel's side, in esc_stick_panel.c ----------------------------- */

/**
 * @brief The page set's init: the panel's state cleared and its controls
 *        laid out, TIMING's keypad closed.
 */
void esc_stick_panel_reset(void);

/** @brief The host's entry. */
void esc_stick_panel_enter(void);
/**
 * @brief The host is left: a run ends, the warning, the search's keyboard
 *        and TIMING's keypad close, and TIMING asks for a save of what it
 *        changed.
 */
void esc_stick_panel_leave(void);
/**
 * @brief A frame.
 * @param dt_s The frame's time, s.
 */
void esc_stick_panel_tick(float dt_s);
/**
 * @brief Touches were lost: a press that may become a tap ends, a hold
 *        ends, a key held on the keyboard or the keypad is let go with what
 *        was typed kept, and a run whose arm is not yet in force ends.
 */
void esc_stick_panel_cancel(void);

/**
 * @brief A DOWN on the session's pages, in the walk of hit().  TIMING's
 *        keypad, while open, takes the DOWN and nothing under it does.
 * @param evt The DOWN, or the press a tap replays.
 * @return True: the pages take every DOWN.
 */
bool esc_stick_panel_down(const touch_event_t *evt);

/** A DOWN at (x, y) lands on the ABORT of a run under way, which
 *  esc_stick_panel_down() acts on at the press. */
bool esc_stick_panel_safe_at(int px, int py);

/**
 * @brief The home tag in the session: TIMING's keypad closes as CANCEL
 *        does; otherwise what covers the page closes, a run ends or the
 *        session climbs a level, as BACK does; from the makers it closes
 *        the session (STAGE_CLOSED).
 * @return True: it always acts.
 */
bool esc_stick_panel_back(void);

/**
 * @brief A MOVE or an UP: the search's keys, TIMING's keypad and the
 *        warning's hold follow it.
 * @param evt The event.
 */
void esc_stick_panel_follow(const touch_event_t *evt);

/**
 * @brief Whether contact @p id holds the warning's hold, a key of the
 *        search's keyboard or a key of TIMING's keypad.
 * @param id The contact.
 * @return True when it does.
 */
bool esc_stick_panel_holds_press(uint8_t id);

/**
 * @brief The session opens on the makers, from the top, the search as
 *        typed; TIMING and its keypad closed.
 */
void esc_stick_panel_open(void);

/**
 * @brief The panel's part of what decides which control lies under a
 *        point: the level, the pages' scrolls, what covers the page, the
 *        keyboard and TIMING's keypad.
 * @param h The hash so far.
 * @return @p h hashed on with the panel's part.
 */
uint32_t esc_stick_panel_showing(uint32_t h);

/* The session's pages, on a revision not yet drawn: everything under
 * PROG_FULL_Y. */
void esc_stick_panel_render(gfx_canvas_t *c);
