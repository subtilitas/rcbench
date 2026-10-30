/**
 * @file text_view.h
 * @brief The text viewer and editor: a file of the SD (Secure Digital)
 *        card shown as text with the bench's words in colour, and changed
 *        and saved on EDIT.
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
 *   y 440  the footer, 52px: SMALL (NORMAL while SMALL is on) and EDIT
 *          at the left, PAGE UP and PAGE DOWN at the right, a warning
 *          between
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
 * The editor.  EDIT switches the view to the editor and opens the
 * keyboard's editor mode (ui_textkey_open_editor()), 4 rows of keys with
 * no digit row, under a text of 8 rows (10 with SMALL); a cursor shows
 * where a key types.  SETUP > PANEL's Keyboard see-through (SET_KB_SEE)
 * lets the text show through the keyboard: OFF, 25% or 50%.  While it is
 * above OFF the panel is drawn to its full height under the keyboard and
 * the rows below the 8 (10) show through the keys; the 8 (10) over the
 * keyboard are still the rows the cursor is kept on, a tap reaches and a
 * page moves.  A tap on the text
 * puts the cursor on the nearest cell; "<-" and "->" move it a unit.  DONE
 * closes the keyboard, and the footer then holds SAVE, REVERT, KEYBOARD,
 * PAGE UP and PAGE DOWN, with a note between.  A key that would take the
 * text past TEXT_BUF_FILE_MAX bytes or TEXT_BUF_ENDS_MAX line ends types
 * nothing and says so.  EDIT is refused where FILES' rules refuse an edit
 * (fm_allowed() FM_OP_EDIT: the servo programmer's journal, the fault log,
 * a file the logger writes); the host names the refusal with
 * text_view_set_rules().
 *
 * SAVE hands the text to the host (TEXT_VIEW_SAVE), which writes it
 * through "<name>.NEW" (fm_job_save()) and answers text_view_saved().  It
 * is locked while the host's rules refuse a save: the bench ARMED, a run
 * being logged, the file being written; the note says why.  From SAVE's
 * tap until the answer the text does not change on any path: no key types,
 * REVERT is shut, DISCARD on a question is shut, and the gap of the text
 * buffer stays behind the text (text_buf_settle()) so a draw moves no byte
 * the card's task reads.  A KST file (first line "KST servo
 * configuration, format ...") that holds a checksum line loses that line
 * on a SAVE of a changed text, so the servo programmer reads it as EDITED
 * (kst_file.h); the note says so.  An ESC profile the panel reads at its
 * start says that it is read at the next start.
 *
 * REVERT, and leaving the editor with the home tag while the text differs
 * from the card, ask first: UNSAVED CHANGES, STAY or DISCARD.  DISCARD
 * puts the text back to the file as it was loaded or last saved, from a
 * copy of TEXT_BUF_FILE_MAX bytes kept while the editor is open.  There is
 * no undo of single keys.
 *
 * SAVE writes only over the file the editor opened: the host hands the
 * save the CRC-32 (cyclic redundancy check) of the file as it was read or
 * last saved, the Wi-Fi page's ETag, and the save compares it with the
 * file on the card first (fm_ops.h fm_job_match()).  A file changed or
 * removed since, by the Wi-Fi page's editor or an upload, fails the save
 * with FM_ERR_CHANGED and nothing written: the note says so, and the
 * question UNSAVED CHANGES offers STAY, which keeps the text, and READ
 * AGAIN, which drops it and reads the file from the card
 * (TEXT_VIEW_RELOAD).
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
/** @brief Spans a drawn line is classed into; a line with more has its
 *         last span reach to its end as TEXT (text_hl_line()). */
#define TEXT_VIEW_SPANS 128u
/** @brief Colours text_view_class_rgb() gives: one per class, then the
 *         text's background. */
#define TEXT_VIEW_RGB (TEXT_HL_CLASSES + 1u)

/** @brief Where the viewer stands. */
typedef enum {
    TEXT_VIEW_CLOSED = 0,  /**< nothing open                              */
    TEXT_VIEW_LOADING,     /**< the card's task reads the file            */
    TEXT_VIEW_SHOWN,       /**< the text shows                            */
    TEXT_VIEW_FAILED       /**< the file did not open: text_view_why()   */
} text_view_state_t;

/** @brief What an event asks of the host (text_view_event()). */
typedef enum {
    TEXT_VIEW_NONE = 0,    /**< nothing the host acts on                  */
    TEXT_VIEW_CHANGED,     /**< the view changed: draw it again           */
    TEXT_VIEW_SAVE,        /**< SAVE: write the text (text_view_save_len()) */
    TEXT_VIEW_LEAVE,       /**< DISCARD on a question the host asked
                                (text_view_ask_open()): go on with what
                                it held                                  */
    TEXT_VIEW_RELOAD       /**< READ AGAIN on the CHANGED question: read
                                the file from the card again; the
                                editor's text is dropped                 */
} text_view_result_t;

/** @brief The question over the editor. */
typedef enum {
    TEXT_VIEW_ASK_NONE = 0,  /**< no question                             */
    TEXT_VIEW_ASK_BACK,      /**< the home tag leaves the editor           */
    TEXT_VIEW_ASK_REVERT,    /**< REVERT                                   */
    TEXT_VIEW_ASK_OPEN,      /**< the host opens another file              */
    TEXT_VIEW_ASK_CHANGED    /**< SAVE found the file changed on the card  */
} text_view_ask_t;

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
 * @brief Rows on screen in the face in use, over the keyboard while it is
 *        open.
 * @return TEXT_VIEW_ROWS_NORMAL or TEXT_VIEW_ROWS_SMALL; 8 or 10 while the
 *         keyboard is open
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
 * @brief The colours the viewer draws in, from a palette as defined: each
 *        class's role (TEXT_HL_CLASS_ROWS), then the background of the
 *        text, for a page drawn elsewhere (the Wi-Fi page).
 * @param theme  the palette, a ui_theme_id_t
 * @param out    takes 0xRRGGBB per class, then the background
 * @param n      entries at @p out; TEXT_VIEW_RGB for all
 * @return the entries written, at most @p n
 */
size_t text_view_class_rgb(int theme, uint32_t *out, size_t n);

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

/* ------------------------------------------------------------ the editor */

/**
 * @brief The host's rules for this file as the bench stands now.
 * @param edit  FM_OK when EDIT may open the editor, else why not
 * @param save  FM_OK when SAVE may write, else why not
 * @return true when they differ from the ones set last: draw again
 */
bool text_view_set_rules(fm_err_t edit, fm_err_t save);

/**
 * @brief Switch the shown text to the editor, its keyboard open, as EDIT
 *        does.
 * @return true when it switched; false with no text shown or an edit the
 *         rules refuse
 */
bool text_view_edit(void);

/**
 * @brief Whether the editor is on.
 * @return true while editing
 */
bool text_view_editing(void);

/**
 * @brief Whether the keyboard is open.
 * @return true while it shows
 */
bool text_view_keyboard(void);

/**
 * @brief Whether the text differs from the file as loaded or last saved.
 * @return true with changes no SAVE wrote
 */
bool text_view_dirty(void);

/**
 * @brief The cursor, a byte offset in the text, its BOM not counted.
 * @param line  takes its line, from 0; may be NULL
 * @return the offset
 */
size_t text_view_cursor(size_t *line);

/**
 * @brief The question that shows.
 * @return TEXT_VIEW_ASK_NONE without one
 */
text_view_ask_t text_view_ask(void);

/**
 * @brief The home tag: the keyboard closes; a question closes as STAY; the
 *        editor leaves for the viewer, after the question while the text
 *        is dirty.
 * @return true when it took the tap; false in the viewer, which the host
 *         closes
 */
bool text_view_back(void);

/**
 * @brief The host is to open another file over a dirty editor: the
 *        question shows, and DISCARD answers TEXT_VIEW_LEAVE.
 */
void text_view_ask_open(void);

/**
 * @brief Start a save: a KST file's checksum line goes (see the top), the
 *        text is held unchanged until text_view_saved(), and the note says
 *        that it is saved.
 * @return the bytes the file takes, its BOM included
 */
size_t text_view_save_len(void);

/**
 * @brief The text's bytes for the save (fm_fill_fn).
 * @param ctx  unused
 * @param at   the offset of the first byte wanted
 * @param dst  takes the bytes
 * @param cap  room at @p dst
 * @return the bytes written, 0 past the end
 */
size_t text_view_fill(void *ctx, uint64_t at, uint8_t *dst, size_t cap);

/**
 * @brief The save is answered.  A question that showed while it ran (its
 *        DISCARD shut meanwhile) closes after a save that wrote the text:
 *        the host's held open goes on, the home tag's leaves the editor.
 *        After a failed save it stays, DISCARD live again.  A save that
 *        found the file changed on the card (FM_ERR_CHANGED) asks STAY or
 *        READ AGAIN when no other question shows.
 * @param err      FM_OK when the file is on the card; else why not, the
 *                 text still dirty
 * @param stopped  the gate stopped it part way (FM_JOB_STOPPED)
 * @param restart  the file is read at the panel's next start
 * @return TEXT_VIEW_LEAVE when the host's held open goes on, else
 *         TEXT_VIEW_NONE
 */
text_view_result_t text_view_saved(fm_err_t err, bool stopped, bool restart);

/**
 * @brief The bytes the header shows: the file as listed while it is read,
 *        as read once it shows, and as written after each SAVE that wrote
 *        it, its BOM included.
 * @return the bytes
 */
uint64_t text_view_size(void);

/**
 * @brief Whether a save runs or waits to: the text does not change.
 * @return true from SAVE's tap, or text_view_save_len(), to
 *         text_view_saved()
 */
bool text_view_saving(void);

/**
 * @brief The note in the footer as it is drawn, "" for none.
 * @param out  takes it
 * @param n    bytes at @p out
 * @return @p out
 */
const char *text_view_note(char *out, size_t n);

#ifdef __cplusplus
}
#endif
