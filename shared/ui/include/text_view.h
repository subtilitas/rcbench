/**
 * @file text_view.h
 * @brief The text viewer: a file of the SD (Secure Digital) card shown as
 *        text, read-only, with the bench's words in colour.
 *
 * FILES hosts the viewer (files_screen.h FILES_VIEW_TEXT).  OPEN on a file
 * in FILES, and OPEN on a report in LOGS, open it: the opener names the
 * file with text_view_begin(), the card's task reads it into the viewer's
 * buffer (text_view_load_area()), and text_view_loaded() takes the bytes as
 * text (text_buf.h).  The render task makes no card call for it.
 *
 * Limits: a file of TEXT_BUF_FILE_MAX bytes (1MiB) at most, the opener
 * refuses a larger one before it is read (text_view_fits()); TEXT_BUF_ENDS_MAX
 * line ends (65,536); a NUL byte in the first TEXT_BUF_PROBE bytes (4KiB)
 * makes the file no text.  The buffer is one static object of 1MiB and its
 * line index of 256KiB, both in PSRAM (pseudo-static random-access memory)
 * on the panel (psram_bss.lf).
 *
 * The body, 1024 x 540px:
 *
 *   y   4  the header, 48px: the file's name, its size, READ-ONLY, and
 *          LINE n OF m at the right
 *   y  60  the text, 1008 x 372px: rows of the 10 x 20 face 20px apart,
 *          TEXT_VIEW_ROWS_NORMAL of TEXT_VIEW_CELLS_NORMAL cells, or with
 *          SMALL rows of the 8 x 16 face 16px apart, TEXT_VIEW_ROWS_SMALL
 *          of TEXT_VIEW_CELLS_SMALL cells; a line wider than a row wraps
 *          onto the next, marked ".." at the left; the scroll bar, 56px
 *          wide, at the right
 *   y 440  the footer, 52px: SMALL (NORMAL while SMALL is on) at the
 *          left, PAGE UP and PAGE DOWN at the right, a warning between
 *   y 496  the router's alert band
 *
 * A drag on the text scrolls it a row per row pitch; a drag on the scroll
 * bar puts the thumb under the finger, the bar's travel spread over the
 * file's lines.  PAGE UP and PAGE DOWN move a screen of rows, and are
 * drawn dimmed at the text's first and last screen.  The face SMALL
 * chooses stays until the panel restarts (text_view_reset()).
 *
 * The lines are classed by text_hl_line() as they are drawn, the file's
 * kind from its name (text_hl_kind_of()), the verdict words from the
 * report writers' text IDs in every language (text_view_words()).
 *
 * The editor (F6 of the file manager's plan) adds an EDIT button, the
 * keyboard's editor mode and SAVE on this view; none of it is here.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fm_io.h"
#include "gfx.h"
#include "text_buf.h"
#include "text_hl.h"
#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Rows on screen in the 10 x 20 face. */
#define TEXT_VIEW_ROWS_NORMAL 18
/** @brief Rows on screen in the 8 x 16 face (SMALL). */
#define TEXT_VIEW_ROWS_SMALL 22
/** @brief Cells of a row in the 10 x 20 face: 920px, a report's step
 *         table whole (test_servo_report_width). */
#define TEXT_VIEW_CELLS_NORMAL 92
/** @brief Cells of a row in the 8 x 16 face: 920px. */
#define TEXT_VIEW_CELLS_SMALL 115

/** @brief Where the viewer stands. */
typedef enum {
    TEXT_VIEW_CLOSED = 0,  /**< nothing open                              */
    TEXT_VIEW_LOADING,     /**< the card's task reads the file            */
    TEXT_VIEW_SHOWN,       /**< the text shows                            */
    TEXT_VIEW_FAILED       /**< the file did not open: text_view_why()   */
} text_view_state_t;

/** @brief What a tap on the footer asks for (text_view_event()). */
typedef enum {
    TEXT_VIEW_NONE = 0,    /**< nothing the host acts on                  */
    TEXT_VIEW_CHANGED      /**< the view changed: draw it again           */
} text_view_result_t;

/** @brief Close the viewer and put the face back to NORMAL: the start. */
void text_view_reset(void);

/**
 * @brief Whether a file of @p size bytes opens.
 * @param size  the file's bytes
 * @return true up to TEXT_BUF_FILE_MAX
 */
bool text_view_fits(uint64_t size);

/**
 * @brief Name the file the card's task reads next: the viewer shows
 *        LOADING until text_view_loaded().  The buffer is not touched, so a
 *        read of an earlier file may still run into it.
 * @param dir   the file's folder, a path from the root
 * @param name  its name
 * @param size  its bytes as listed, for the header
 */
void text_view_begin(const char *dir, const char *name, uint64_t size);

/**
 * @brief Where the card's task reads the file to.
 * @param cap  takes the room, text_buf_room() of the buffer
 * @return the bytes
 */
uint8_t *text_view_load_area(size_t *cap);

/**
 * @brief The read is answered: @p len bytes are in the load area, or
 *        @p err says why not.  The text shows from its first row.
 * @param err  the card's answer
 * @param len  the bytes read
 */
void text_view_loaded(fm_err_t err, size_t len);

/** @brief Close the viewer.  The buffer stays as it is. */
void text_view_close(void);

/**
 * @brief Where the viewer stands.
 * @return the state
 */
text_view_state_t text_view_state(void);

/**
 * @brief Why the file did not open, TEXT_VIEW_FAILED.
 * @param tb_err  takes the text's refusal, TEXT_BUF_OK for a card's one;
 *                may be NULL
 * @return the card's answer, FM_OK for a refusal of the text
 */
fm_err_t text_view_why(text_buf_err_t *tb_err);

/**
 * @brief The file's name, "" with nothing open.
 * @return the name
 */
const char *text_view_name(void);

/**
 * @brief The file's folder, "" with nothing open.
 * @return the folder
 */
const char *text_view_dir(void);

/**
 * @brief Whether SMALL is on: the 8 x 16 face.
 * @return true for SMALL
 */
bool text_view_small(void);

/**
 * @brief Switch the face; the top line stays on top.
 * @param small  true for the 8 x 16 face
 */
void text_view_set_small(bool small);

/**
 * @brief Rows on screen in the face in use.
 * @return TEXT_VIEW_ROWS_NORMAL or TEXT_VIEW_ROWS_SMALL
 */
int text_view_rows(void);

/**
 * @brief Cells of a row in the face in use.
 * @return TEXT_VIEW_CELLS_NORMAL or TEXT_VIEW_CELLS_SMALL
 */
unsigned text_view_cells(void);

/**
 * @brief The file's lines as the header counts them: a line end at the
 *        file's end starts no line of its own.
 * @return lines; 0 for an empty file or nothing shown
 */
size_t text_view_lines(void);

/**
 * @brief The top row on screen: its line and its row in that line.
 * @param row  takes the row of the wrapped line, from 0; may be NULL
 * @return the line, from 0
 */
size_t text_view_top(size_t *row);

/**
 * @brief Scroll by @p rows rows, held to the text's first and last screen.
 * @param rows  rows down; negative is up
 */
void text_view_scroll(long rows);

/**
 * @brief Whether the first row is on top.
 * @return true at the start
 */
bool text_view_at_start(void);

/**
 * @brief Whether the last row is at the foot of the screen, or the whole
 *        text shows.
 * @return true at the end
 */
bool text_view_at_end(void);

/**
 * @brief Row @p k on screen as it is drawn: its cells' text, a tab as
 *        spaces, a byte no glyph draws as "?".
 * @param k    the row on screen, from 0
 * @param out  takes the row
 * @param n    bytes at @p out
 * @return true when the row holds text of the file
 */
bool text_view_row_text(int k, char *out, size_t n);

/**
 * @brief The class cell @p cell of row @p k on screen is drawn in.
 * @param k     the row on screen, from 0
 * @param cell  the cell, from 0
 * @return a text_hl_class_t; TEXT_HL_TEXT past the row's text
 */
int text_view_cell_class(int k, int cell);

/**
 * @brief The verdict words the lines are classed with: the report
 *        writers' PASS, FAIL and the words of what was not measured, in
 *        every language, and the marks "--" and "[not loaded]".
 * @return the words
 */
const text_hl_words_t *text_view_words(void);

/**
 * @brief Draw the viewer over the whole body.
 * @param c  the body's canvas
 */
void text_view_render(gfx_canvas_t *c);

/**
 * @brief One touch event, in the body's coordinates.
 * @param e  the event
 * @return TEXT_VIEW_CHANGED when the view is to be drawn again
 */
text_view_result_t text_view_event(const touch_event_t *e);

/** @brief Drop a press under way: the touch stream lost an event. */
void text_view_cancel_press(void);

#ifdef __cplusplus
}
#endif
