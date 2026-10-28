/**
 * @file
 * @brief A text file in memory for the viewer and the editor: a gap buffer
 *        over the caller's bytes and an index of its lines.
 *
 * The buffer holds the file's bytes as they are.  Nothing is converted: a
 * line end stays LF, CR LF or CR as the file has it, a code point the text
 * faces do not draw stays as its bytes, and a NUL after the first
 * TEXT_BUF_PROBE bytes stays a NUL.  A leading UTF-8 BOM (byte order mark,
 * EF BB BF) is taken off at load, remembered, and written back in front by
 * text_buf_read(); it is no cell and no cursor position.  Serialising an
 * unchanged buffer gives the loaded file byte for byte.
 *
 * Limits:
 *   - a file of at most TEXT_BUF_FILE_MAX bytes (1MiB), the BOM included;
 *     the editor stops a text from growing past what the viewer opens, so a
 *     saved file always opens again;
 *   - at most the caller's index entries, and TEXT_BUF_ENDS_MAX (65,536),
 *     line ends; a text of n line ends has n + 1 lines, the last one empty
 *     when the file ends with a line end;
 *   - no limit on the length of a line.
 *
 * A unit is what the cursor steps over and DEL removes: one well-formed
 * UTF-8 sequence, one byte that starts none (drawn as one '?' cell, as
 * gfx_utf8_next() reads it), or one line end, CR LF included.  Every unit
 * of a line is one cell, except a tab, which reaches to the next multiple
 * of TEXT_BUF_TAB cells.  A line is drawn wrapped at a width in cells: a
 * unit that does not fit the row starts the next one, and a tab that does
 * not fit ends at the row's last cell.  A cell number is row * width +
 * column, so it counts along the wrapped line.
 *
 * Line ends: the file's kind is that of its first line end, LF for a file
 * without one.  ENTER inserts that kind.  Where it would join a neighbouring
 * lone CR or LF into one CR LF, ENTER inserts CR LF instead, so it always
 * adds one line.  A text with two kinds is mixed (text_buf_mixed()); it is
 * saved as it is.
 *
 * Every edit moves the revision on, and it wraps after 2^32 edits: compare
 * it for equality only.  The dirty flag is set by an edit and cleared by a
 * load and by text_buf_saved().
 *
 * Every buffer comes from the caller: no heap, no stdio.  Pure C.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Bytes of a file the buffer takes, its BOM included: 1MiB. */
#define TEXT_BUF_FILE_MAX 1048576u
/** @brief Line ends a text holds at most: 65,536 index entries of 4B. */
#define TEXT_BUF_ENDS_MAX 65536u
/** @brief Bytes from the start of the file a NUL makes it no text. */
#define TEXT_BUF_PROBE 4096u
/** @brief Cells between two tab stops. */
#define TEXT_BUF_TAB 8u
/** @brief Bytes of the UTF-8 BOM. */
#define TEXT_BUF_BOM_LEN 3u

/** @brief What a load or an edit answers.  The numbers are stable. */
typedef enum {
    TEXT_BUF_OK = 0,         /**< done */
    TEXT_BUF_ARG = 1,        /**< a NULL, or a code point no text holds */
    TEXT_BUF_TOO_LARGE = 2,  /**< a file over the room text_buf_room() gives */
    TEXT_BUF_TOO_MANY = 3,   /**< more line ends than the index holds */
    TEXT_BUF_NOT_TEXT = 4,   /**< a NUL in the first TEXT_BUF_PROBE bytes */
    TEXT_BUF_FULL = 5,       /**< the text is at its most bytes */
} text_buf_err_t;

/** @brief A kind of line end.  The numbers are stable. */
typedef enum {
    TEXT_EOL_LF = 0,    /**< 0x0A */
    TEXT_EOL_CRLF = 1,  /**< 0x0D 0x0A */
    TEXT_EOL_CR = 2,    /**< 0x0D not followed by 0x0A */
    TEXT_EOL_KINDS = 3  /**< the count of kinds */
} text_eol_t;

/**
 * @brief The buffer.  Read it through the functions; the fields are open
 *        only so the caller can place it.
 */
typedef struct {
    uint8_t *mem;        /**< the caller's bytes */
    size_t cap;          /**< their count */
    uint32_t *ends;      /**< per line end, the start of the line after it,
                              with its kind in the top 2 bits */
    size_t ends_cap;     /**< index entries, at most TEXT_BUF_ENDS_MAX */
    size_t n_ends;       /**< line ends in the text */
    size_t gap_at;       /**< text bytes before the gap */
    size_t gap_len;      /**< bytes of the gap */
    size_t len;          /**< text bytes, the BOM not counted */
    size_t limit;        /**< text bytes at most */
    size_t cursor;       /**< the cursor, a text offset at a unit's start */
    size_t kinds[TEXT_EOL_KINDS]; /**< line ends of each kind */
    text_eol_t eol;      /**< the file's kind of line end */
    uint32_t revision;   /**< moves on with every edit and load */
    bool bom;            /**< the file starts with a BOM */
    bool dirty;          /**< edited since the load or text_buf_saved() */
} text_buf_t;

/**
 * @brief An empty text over @p mem, LF line ends, no BOM.
 *
 * @param tb       The buffer.
 * @param mem      Bytes for the text; their count is the most it holds,
 *                 up to TEXT_BUF_FILE_MAX.
 * @param cap      Count of @p mem.
 * @param ends     The line index, one entry per line end.
 * @param ends_cap Count of @p ends; above TEXT_BUF_ENDS_MAX counts as that.
 * @return TEXT_BUF_OK, or TEXT_BUF_ARG for a NULL.
 */
text_buf_err_t text_buf_init(text_buf_t *tb, uint8_t *mem, size_t cap,
                             uint32_t *ends, size_t ends_cap);

/**
 * @brief The largest file text_buf_load() and text_buf_load_done() take.
 *
 * @param tb The buffer.
 * @return min(cap, TEXT_BUF_FILE_MAX) bytes; 0 for NULL.
 */
size_t text_buf_room(const text_buf_t *tb);

/**
 * @brief Where a loader puts a file's bytes in place, text_buf_room() of
 *        them at most, before text_buf_load_done().  The text held is lost.
 *
 * @param tb The buffer.
 * @return The caller's bytes; NULL for NULL.
 */
uint8_t *text_buf_load_area(text_buf_t *tb);

/**
 * @brief Take the @p len bytes a loader put at text_buf_load_area() as the
 *        text: the BOM, the line ends and their kind, the NUL test.
 *
 * Refused, the buffer is empty as text_buf_init() leaves it.  The revision
 * moves on either way.
 *
 * @param tb  The buffer.
 * @param len Bytes of the file.
 * @return TEXT_BUF_OK; TEXT_BUF_TOO_LARGE over text_buf_room();
 *         TEXT_BUF_NOT_TEXT; TEXT_BUF_TOO_MANY; TEXT_BUF_ARG for NULL.
 */
text_buf_err_t text_buf_load_done(text_buf_t *tb, size_t len);

/**
 * @brief Copy a file into the buffer and take it, as text_buf_load_done().
 *
 * @param tb    The buffer.
 * @param bytes The file; NULL with @p len 0 is an empty file.
 * @param len   Its bytes.
 * @return As text_buf_load_done().  A file over the room is refused before
 *         a byte is copied.
 */
text_buf_err_t text_buf_load(text_buf_t *tb, const void *bytes, size_t len);

/**
 * @brief Bytes text_buf_read() gives: the text and the BOM.
 *
 * @param tb The buffer.
 * @return Bytes; 0 for NULL.
 */
size_t text_buf_size(const text_buf_t *tb);

/**
 * @brief Serialised bytes @p at to @p at + @p n of the file, the BOM in
 *        front: a save writes the file in pieces with this.
 *
 * @param tb  The buffer.
 * @param at  Offset in the file.
 * @param out Out: the bytes.
 * @param n   Room in @p out.
 * @return Bytes written to @p out: fewer than @p n at the end, 0 past it.
 */
size_t text_buf_read(const text_buf_t *tb, size_t at, void *out, size_t n);

/**
 * @brief Text bytes, the BOM not counted.
 *
 * @param tb The buffer.
 * @return Bytes; 0 for NULL.
 */
size_t text_buf_len(const text_buf_t *tb);

/**
 * @brief Byte @p pos of the text.
 *
 * @param tb  The buffer.
 * @param pos Text offset.
 * @return The byte; 0 past the end.
 */
uint8_t text_buf_byte(const text_buf_t *tb, size_t pos);

/**
 * @brief Lines of the text: its line ends and one.
 *
 * @param tb The buffer.
 * @return Lines, 1 for an empty text; 0 for NULL.
 */
size_t text_buf_lines(const text_buf_t *tb);

/**
 * @brief Where line @p line starts.
 *
 * @param tb   The buffer.
 * @param line Line index, from 0.
 * @return Text offset; text_buf_len() past the last line.
 */
size_t text_buf_line_start(const text_buf_t *tb, size_t line);

/**
 * @brief Bytes of line @p line without its line end.
 *
 * @param tb   The buffer.
 * @param line Line index, from 0.
 * @return Bytes; 0 past the last line.
 */
size_t text_buf_line_len(const text_buf_t *tb, size_t line);

/**
 * @brief Line @p line as one run of bytes, without its line end.  Moves
 *        the gap out of the line when it is in it; the text is unchanged.
 *
 * @param tb   The buffer.
 * @param line Line index, from 0.
 * @param len  Out: bytes of the line.
 * @return The first byte, valid until the next call on @p tb; NULL past
 *         the last line or for a NULL.
 */
const char *text_buf_line(text_buf_t *tb, size_t line, size_t *len);

/**
 * @brief The line text offset @p pos is on.
 *
 * @param tb  The buffer.
 * @param pos Text offset.
 * @return Line index; the last line past the end.
 */
size_t text_buf_line_of(const text_buf_t *tb, size_t pos);

/**
 * @brief The code point of the unit at @p pos.
 *
 * @param tb    The buffer.
 * @param pos   Text offset of a unit's start.
 * @param bytes Out: the unit's bytes, 0 at the end; or NULL.
 * @return The code point; '?' for a byte that starts no well-formed
 *         sequence; '\\n' for a line end of any kind; 0 at the end.
 */
uint32_t text_buf_cp(const text_buf_t *tb, size_t pos, size_t *bytes);

/**
 * @brief The start of the unit after the one at @p pos.
 *
 * @param tb  The buffer.
 * @param pos Text offset of a unit's start.
 * @return Text offset; text_buf_len() at the end.
 */
size_t text_buf_next(const text_buf_t *tb, size_t pos);

/**
 * @brief The start of the unit before @p pos.
 *
 * @param tb  The buffer.
 * @param pos Text offset of a unit's start.
 * @return Text offset; 0 at the start.
 */
size_t text_buf_prev(const text_buf_t *tb, size_t pos);

/**
 * @brief Whether a unit starts at @p pos: not inside a UTF-8 sequence and
 *        not between the CR and the LF of a CR LF.
 *
 * @param tb  The buffer.
 * @param pos Text offset.
 * @return true at a unit's start and at the end; false past it.
 */
bool text_buf_is_unit(const text_buf_t *tb, size_t pos);

/**
 * @brief Rows line @p line takes, wrapped at @p width cells.
 *
 * @param tb    The buffer.
 * @param line  Line index, from 0.
 * @param width Cells of a row; 0 does not wrap.
 * @return Rows, at least 1; 0 past the last line.
 */
size_t text_buf_rows(const text_buf_t *tb, size_t line, unsigned width);

/**
 * @brief The line and the cell at which the unit at @p pos is drawn.
 *
 * At a line's end that is the cell after its last unit, on the last row
 * (column @p width when that row is full).
 *
 * @param tb    The buffer.
 * @param pos   Text offset of a unit's start.
 * @param width Cells of a row; 0 does not wrap.
 * @param line  Out: line index; or NULL.
 * @param cell  Out: row * width + column; or NULL.
 */
void text_buf_cell_of(const text_buf_t *tb, size_t pos, unsigned width,
                      size_t *line, size_t *cell);

/**
 * @brief The unit drawn over cell @p cell of line @p line: the cursor
 *        position a tap there gives.
 *
 * @param tb    The buffer.
 * @param line  Line index, from 0; past the last line, the last.
 * @param cell  row * width + column.
 * @param width Cells of a row; 0 does not wrap.
 * @return The text offset of that unit; the line's end past its last unit.
 */
size_t text_buf_pos_at(const text_buf_t *tb, size_t line, size_t cell,
                       unsigned width);

/**
 * @brief The cursor, a text offset at a unit's start.
 *
 * @param tb The buffer.
 * @return Text offset; 0 for NULL.
 */
size_t text_buf_cursor(const text_buf_t *tb);

/**
 * @brief Put the cursor at @p pos, moved back to the start of the unit it
 *        is in; past the end, at the end.
 *
 * @param tb  The buffer.
 * @param pos Text offset.
 */
void text_buf_set_cursor(text_buf_t *tb, size_t pos);

/**
 * @brief Move the cursor one unit left.
 *
 * @param tb The buffer.
 * @return false at the start.
 */
bool text_buf_left(text_buf_t *tb);

/**
 * @brief Move the cursor one unit right.
 *
 * @param tb The buffer.
 * @return false at the end.
 */
bool text_buf_right(text_buf_t *tb);

/**
 * @brief Insert code point @p cp at the cursor, which goes after it.
 *
 * @param tb The buffer.
 * @param cp A tab, or U+0020 and above but U+007F to U+009F, the
 *           surrogates and above U+10FFFF.
 * @return TEXT_BUF_OK; TEXT_BUF_ARG for any other code point or NULL;
 *         TEXT_BUF_FULL when the text has no room for its bytes.
 */
text_buf_err_t text_buf_insert(text_buf_t *tb, uint32_t cp);

/**
 * @brief Insert a line end at the cursor, which goes to the new line.
 *
 * @param tb The buffer.
 * @return TEXT_BUF_OK; TEXT_BUF_FULL without room for its bytes;
 *         TEXT_BUF_TOO_MANY when the index is full; TEXT_BUF_ARG for NULL.
 */
text_buf_err_t text_buf_insert_eol(text_buf_t *tb);

/**
 * @brief Remove the unit before the cursor (backspace).
 *
 * @param tb The buffer.
 * @return false at the start.
 */
bool text_buf_delete_before(text_buf_t *tb);

/**
 * @brief Remove the unit at the cursor (delete).
 *
 * @param tb The buffer.
 * @return false at the end.
 */
bool text_buf_delete_after(text_buf_t *tb);

/**
 * @brief The file's kind of line end, which ENTER inserts.
 *
 * @param tb The buffer.
 * @return The kind; LF for NULL.
 */
text_eol_t text_buf_eol(const text_buf_t *tb);

/**
 * @brief Whether the text holds line ends of two kinds or three.
 *
 * @param tb The buffer.
 * @return true when mixed.
 */
bool text_buf_mixed(const text_buf_t *tb);

/**
 * @brief Whether the file starts with a BOM.
 *
 * @param tb The buffer.
 * @return true with a BOM.
 */
bool text_buf_bom(const text_buf_t *tb);

/**
 * @brief Whether the text was edited since the load or text_buf_saved().
 *
 * @param tb The buffer.
 * @return true when edited.
 */
bool text_buf_dirty(const text_buf_t *tb);

/**
 * @brief Move the gap behind the text, so every line is one run of bytes:
 *        text_buf_line() then moves no byte until the next edit.  Another
 *        task may read the text with text_buf_read() while this one draws
 *        it, as long as neither edits it.
 *
 * @param tb The buffer.
 */
void text_buf_settle(text_buf_t *tb);

/**
 * @brief Note that the text as it is now is on the card.
 *
 * @param tb The buffer.
 */
void text_buf_saved(text_buf_t *tb);

/**
 * @brief The revision: moves on with each edit and load; compare it for
 *        equality only.
 *
 * @param tb The buffer.
 * @return The revision; 0 for NULL.
 */
uint32_t text_buf_revision(const text_buf_t *tb);

#ifdef __cplusplus
}
#endif
