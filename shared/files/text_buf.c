/**
 * @file
 * @brief A text file in memory: the gap buffer, the line index, the units
 *        and the cells (text_buf.h).
 *
 * The text is mem[0, gap_at) and mem[gap_at + gap_len, cap): the gap sits
 * where the last edit was, so typing at one place moves no byte, and a
 * cursor that goes elsewhere moves the gap with the next edit.
 *
 * The index holds, per line end, the text offset of the line after it.
 * An edit at offset p that takes d bytes and puts s in their place can
 * change the line ends at the bytes p - 1 to p + s only: a CR at p - 1
 * may meet an LF that comes to p, the LF at p + s may lose or gain the CR
 * in front of it, and every other line end depends on bytes the edit did
 * not touch.  So an edit scans those bytes again, replaces their entries,
 * and moves every later entry by s - d.  An edit is at most 4 bytes each
 * way, so the stretch holds at most 6 line ends; the cost of an edit is
 * one pass over the later entries, 65,536 at most.
 *
 * SPDX-License-Identifier: MIT
 */

#include "text_buf.h"

#include <string.h>

/** @brief The kind of a line end in an index entry's top 2 bits. */
#define KIND_SHIFT 30u
/** @brief The offset in an index entry. */
#define POS_MASK   0x3FFFFFFFu
/** @brief Line ends an edit can leave in the stretch it scans again. */
#define EDIT_ENDS  8u
/** @brief Bytes an edit takes or puts at most. */
#define EDIT_MAX   4u

/** @brief The UTF-8 BOM. */
static const uint8_t k_bom[TEXT_BUF_BOM_LEN] = {0xEFu, 0xBBu, 0xBFu};

/* ------------------------------------------------------------- the bytes */

/**
 * @brief Byte @p i of the text, read across the gap.
 *
 * @param tb The buffer.
 * @param i  Text offset under tb->len.
 * @return The byte.
 */
static uint8_t at(const text_buf_t *tb, size_t i)
{
    return (i < tb->gap_at) ? tb->mem[i] : tb->mem[i + tb->gap_len];
}

/**
 * @brief Decode the UTF-8 sequence at @p i, as gfx_utf8_next() does.
 *
 * @param tb The buffer.
 * @param i  Text offset under tb->len.
 * @param cp Out: the code point, '?' for a byte that starts no sequence.
 * @return The bytes of the sequence; 1 for a byte that starts none.
 */
static size_t decode(const text_buf_t *tb, size_t i, uint32_t *cp)
{
    const unsigned c0 = at(tb, i);
    size_t n;
    uint32_t v;

    *cp = c0;
    if (c0 < 0x80u) {
        return 1u;
    }
    if ((c0 & 0xE0u) == 0xC0u && c0 >= 0xC2u) {
        n = 2u;
        v = c0 & 0x1Fu;
    } else if ((c0 & 0xF0u) == 0xE0u) {
        n = 3u;
        v = c0 & 0x0Fu;
    } else if ((c0 & 0xF8u) == 0xF0u && c0 <= 0xF4u) {
        n = 4u;
        v = c0 & 0x07u;
    } else {
        *cp = '?';
        return 1u;
    }
    for (size_t k = 1u; k < n; ++k) {
        if (i + k >= tb->len || (at(tb, i + k) & 0xC0u) != 0x80u) {
            *cp = '?';
            return 1u;
        }
        v = (v << 6) | (at(tb, i + k) & 0x3Fu);
    }
    /* An overlong form, a surrogate, or above U+10FFFF is malformed. */
    if ((n == 3u && v < 0x800u) || (n == 4u && v < 0x10000u) ||
        (v >= 0xD800u && v <= 0xDFFFu) || v > 0x10FFFFu) {
        *cp = '?';
        return 1u;
    }
    *cp = v;
    return n;
}

/**
 * @brief Encode @p cp as UTF-8.
 *
 * @param cp  A code point up to U+10FFFF.
 * @param out Out: 4 bytes of room.
 * @return The bytes written.
 */
static size_t encode(uint32_t cp, uint8_t *out)
{
    if (cp < 0x80u) {
        out[0] = (uint8_t)cp;
        return 1u;
    }
    if (cp < 0x800u) {
        out[0] = (uint8_t)(0xC0u | (cp >> 6));
        out[1] = (uint8_t)(0x80u | (cp & 0x3Fu));
        return 2u;
    }
    if (cp < 0x10000u) {
        out[0] = (uint8_t)(0xE0u | (cp >> 12));
        out[1] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (uint8_t)(0x80u | (cp & 0x3Fu));
        return 3u;
    }
    out[0] = (uint8_t)(0xF0u | (cp >> 18));
    out[1] = (uint8_t)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (uint8_t)(0x80u | (cp & 0x3Fu));
    return 4u;
}

/**
 * @brief Move the gap to text offset @p p.
 *
 * @param tb The buffer.
 * @param p  Text offset up to tb->len.
 */
static void move_gap(text_buf_t *tb, size_t p)
{
    if (p < tb->gap_at) {
        memmove(tb->mem + p + tb->gap_len, tb->mem + p, tb->gap_at - p);
    } else if (p > tb->gap_at) {
        memmove(tb->mem + tb->gap_at, tb->mem + tb->gap_at + tb->gap_len,
                p - tb->gap_at);
    }
    tb->gap_at = p;
}

/* ------------------------------------------------------------ the index */

/**
 * @brief The text offset an index entry holds.
 *
 * @param e The entry.
 * @return The offset of the line after the line end.
 */
static size_t entry_pos(uint32_t e)
{
    return (size_t)(e & POS_MASK);
}

/**
 * @brief The kind of line end an index entry holds.
 *
 * @param e The entry.
 * @return The kind.
 */
static text_eol_t entry_kind(uint32_t e)
{
    return (text_eol_t)(e >> KIND_SHIFT);
}

/**
 * @brief An index entry.
 *
 * @param pos  The offset of the line after the line end.
 * @param kind The kind of line end.
 * @return The entry.
 */
static uint32_t entry(size_t pos, text_eol_t kind)
{
    return (uint32_t)pos | ((uint32_t)kind << KIND_SHIFT);
}

/**
 * @brief The bytes of a kind of line end.
 *
 * @param kind The kind.
 * @return 2 for CR LF, 1 otherwise.
 */
static size_t eol_bytes(text_eol_t kind)
{
    return (kind == TEXT_EOL_CRLF) ? 2u : 1u;
}

/**
 * @brief Whether a line ends at the byte at @p i, and of which kind.
 *
 * The LF of a CR LF ends it; the CR in front does not.
 *
 * @param tb   The buffer.
 * @param i    Text offset under tb->len.
 * @param kind Out: the kind when a line ends there.
 * @return true when the line ends with the byte at @p i.
 */
static bool ends_at(const text_buf_t *tb, size_t i, text_eol_t *kind)
{
    const uint8_t b = at(tb, i);

    if (b == '\n') {
        *kind = (i > 0u && at(tb, i - 1u) == '\r') ? TEXT_EOL_CRLF
                                                   : TEXT_EOL_LF;
        return true;
    }
    if (b == '\r' && (i + 1u >= tb->len || at(tb, i + 1u) != '\n')) {
        *kind = TEXT_EOL_CR;
        return true;
    }
    return false;
}

/**
 * @brief Count the line ends of the whole text into the index.
 *
 * @param tb The buffer.
 * @return false when there are more than the index holds.
 */
static bool index_all(text_buf_t *tb)
{
    text_eol_t kind;

    tb->n_ends = 0u;
    memset(tb->kinds, 0, sizeof(tb->kinds));
    for (size_t i = 0u; i < tb->len; ++i) {
        if (!ends_at(tb, i, &kind)) {
            continue;
        }
        if (tb->n_ends >= tb->ends_cap) {
            return false;
        }
        if (tb->n_ends == 0u) {
            tb->eol = kind;
        }
        tb->ends[tb->n_ends++] = entry(i + 1u, kind);
        tb->kinds[kind]++;
    }
    return true;
}

/**
 * @brief Take @p d bytes at @p p and put the @p s bytes of @p ins there,
 *        keeping the index.
 *
 * The caller has checked the room and the index: a line end comes in only
 * through text_buf_insert_eol(), which checks for one more entry, and no
 * other edit adds one.
 *
 * @param tb  The buffer.
 * @param p   Text offset of a unit's start.
 * @param d   Bytes to take, EDIT_MAX at most.
 * @param ins Bytes to put; NULL with @p s 0.
 * @param s   Their count, EDIT_MAX at most.
 */
static void edit(text_buf_t *tb, size_t p, size_t d, const uint8_t *ins,
                 size_t s)
{
    const size_t la = (p == 0u) ? 0u : text_buf_line_of(tb, p - 1u);
    const size_t a = (p == 0u) ? 0u : p - 1u;
    uint32_t fresh[EDIT_ENDS] = {0};
    size_t n_fresh = 0u;
    size_t gone = la;
    text_eol_t kind;

    /* The entries of the line ends at bytes p - 1 to p + d: line starts
     * from p to p + d + 1.  The line end whose last byte comes to p + s
     * may change its kind. */
    while (gone < tb->n_ends && entry_pos(tb->ends[gone]) <= p + d + 1u) {
        tb->kinds[entry_kind(tb->ends[gone])]--;
        gone++;
    }

    move_gap(tb, p);
    tb->gap_len += d;
    tb->len -= d;
    if (s > 0u) {
        memcpy(tb->mem + tb->gap_at, ins, s);
    }
    tb->gap_at += s;
    tb->gap_len -= s;
    tb->len += s;

    const size_t scan = (p + s + 1u < tb->len) ? p + s + 1u : tb->len;
    for (size_t i = a; i < scan; ++i) {
        if (ends_at(tb, i, &kind) && n_fresh < EDIT_ENDS) {
            fresh[n_fresh++] = entry(i + 1u, kind);
            tb->kinds[kind]++;
        }
    }

    /* The later entries move by s - d bytes and by n_fresh - (gone - la)
     * places. */
    const size_t later = tb->n_ends - gone;
    memmove(tb->ends + la + n_fresh, tb->ends + gone,
            later * sizeof(tb->ends[0]));
    for (size_t k = la + n_fresh; k < la + n_fresh + later; ++k) {
        const uint32_t e = tb->ends[k];
        tb->ends[k] = entry(entry_pos(e) + s - d, entry_kind(e));
    }
    memcpy(tb->ends + la, fresh, n_fresh * sizeof(fresh[0]));
    tb->n_ends = la + n_fresh + later;
    tb->revision++;
    tb->dirty = true;
}

/* ------------------------------------------------------------ the setup */

/**
 * @brief Empty the text and set the most bytes it may take.
 *
 * @param tb The buffer, its memory and index set.
 */
static void empty(text_buf_t *tb)
{
    tb->n_ends = 0u;
    tb->gap_at = 0u;
    tb->gap_len = tb->cap;
    tb->len = 0u;
    tb->cursor = 0u;
    memset(tb->kinds, 0, sizeof(tb->kinds));
    tb->eol = TEXT_EOL_LF;
    tb->bom = false;
    tb->dirty = false;
    tb->limit = text_buf_room(tb);
}

text_buf_err_t text_buf_init(text_buf_t *tb, uint8_t *mem, size_t cap,
                             uint32_t *ends, size_t ends_cap)
{
    if (tb == NULL || mem == NULL || ends == NULL) {
        return TEXT_BUF_ARG;
    }
    memset(tb, 0, sizeof(*tb));
    tb->mem = mem;
    tb->cap = cap;
    tb->ends = ends;
    tb->ends_cap = (ends_cap > TEXT_BUF_ENDS_MAX) ? TEXT_BUF_ENDS_MAX
                                                  : ends_cap;
    empty(tb);
    return TEXT_BUF_OK;
}

size_t text_buf_room(const text_buf_t *tb)
{
    if (tb == NULL) {
        return 0u;
    }
    return (tb->cap < TEXT_BUF_FILE_MAX) ? tb->cap : TEXT_BUF_FILE_MAX;
}

uint8_t *text_buf_load_area(text_buf_t *tb)
{
    if (tb == NULL) {
        return NULL;
    }
    empty(tb);
    return tb->mem;
}

/**
 * @brief Take the @p len bytes at the buffer's start as the text: the BOM,
 *        the line ends and their kind, and the NUL test when @p probe.
 *
 * @param tb    The buffer, emptied, its revision moved on.
 * @param len   Bytes of the file, text_buf_room() at most.
 * @param probe Whether a NUL in the first TEXT_BUF_PROBE bytes refuses it.
 * @return TEXT_BUF_OK; TEXT_BUF_NOT_TEXT; TEXT_BUF_TOO_MANY.
 */
static text_buf_err_t take(text_buf_t *tb, size_t len, bool probe)
{
    if (probe && memchr(tb->mem, 0, (len < TEXT_BUF_PROBE) ? len
                                                           : TEXT_BUF_PROBE)
                     != NULL) {
        return TEXT_BUF_NOT_TEXT;
    }
    if (len >= TEXT_BUF_BOM_LEN &&
        memcmp(tb->mem, k_bom, TEXT_BUF_BOM_LEN) == 0) {
        memmove(tb->mem, tb->mem + TEXT_BUF_BOM_LEN, len - TEXT_BUF_BOM_LEN);
        len -= TEXT_BUF_BOM_LEN;
        tb->bom = true;
        tb->limit -= TEXT_BUF_BOM_LEN;
    }
    tb->len = len;
    tb->gap_at = len;
    tb->gap_len = tb->cap - len;
    if (!index_all(tb)) {
        empty(tb);
        return TEXT_BUF_TOO_MANY;
    }
    return TEXT_BUF_OK;
}

text_buf_err_t text_buf_load_done(text_buf_t *tb, size_t len)
{
    if (tb == NULL) {
        return TEXT_BUF_ARG;
    }
    empty(tb);
    tb->revision++;
    if (len > text_buf_room(tb)) {
        return TEXT_BUF_TOO_LARGE;
    }
    return take(tb, len, true);
}

text_buf_err_t text_buf_load(text_buf_t *tb, const void *bytes, size_t len)
{
    if (tb == NULL || (bytes == NULL && len > 0u)) {
        return TEXT_BUF_ARG;
    }
    if (len > text_buf_room(tb)) {
        empty(tb);
        tb->revision++;
        return TEXT_BUF_TOO_LARGE;
    }
    if (len > 0u) {
        memcpy(text_buf_load_area(tb), bytes, len);
    }
    return text_buf_load_done(tb, len);
}

/**
 * @brief Line ends in @p len bytes, counted as index_all() counts them.
 *
 * @param b   The bytes.
 * @param len Their count.
 * @return The line ends.
 */
static size_t count_ends(const uint8_t *b, size_t len)
{
    size_t n = 0u;

    for (size_t i = 0u; i < len; ++i) {
        if (b[i] == '\n'
            || (b[i] == '\r' && (i + 1u >= len || b[i + 1u] != '\n'))) {
            n++;
        }
    }
    return n;
}

text_buf_err_t text_buf_restore(text_buf_t *tb, const void *bytes, size_t len)
{
    if (tb == NULL || (bytes == NULL && len > 0u)) {
        return TEXT_BUF_ARG;
    }
    if (len > text_buf_room(tb)) {
        return TEXT_BUF_TOO_LARGE;
    }
    if (count_ends(bytes, len) > tb->ends_cap) {
        return TEXT_BUF_TOO_MANY;
    }
    uint8_t *mem = text_buf_load_area(tb);
    if (len > 0u) {
        memcpy(mem, bytes, len);
    }
    tb->revision++;
    return take(tb, len, false);
}

/* ------------------------------------------------------------ the reads */

size_t text_buf_size(const text_buf_t *tb)
{
    if (tb == NULL) {
        return 0u;
    }
    return tb->len + (tb->bom ? TEXT_BUF_BOM_LEN : 0u);
}

size_t text_buf_read(const text_buf_t *tb, size_t at_, void *out, size_t n)
{
    uint8_t *o = out;
    size_t done = 0u;

    if (tb == NULL || out == NULL) {
        return 0u;
    }
    if (tb->bom) {
        while (at_ < TEXT_BUF_BOM_LEN && done < n) {
            o[done++] = k_bom[at_++];
        }
        if (at_ < TEXT_BUF_BOM_LEN) {
            return done;
        }
        at_ -= TEXT_BUF_BOM_LEN;
    }
    if (at_ >= tb->len) {
        return done;
    }
    if (n - done > tb->len - at_) {
        n = done + tb->len - at_;
    }
    if (at_ < tb->gap_at) {
        size_t k = tb->gap_at - at_;
        if (k > n - done) {
            k = n - done;
        }
        memcpy(o + done, tb->mem + at_, k);
        done += k;
        at_ += k;
    }
    memcpy(o + done, tb->mem + at_ + tb->gap_len, n - done);
    return n;
}

size_t text_buf_len(const text_buf_t *tb)
{
    return (tb == NULL) ? 0u : tb->len;
}

uint8_t text_buf_byte(const text_buf_t *tb, size_t pos)
{
    return (tb == NULL || pos >= tb->len) ? 0u : at(tb, pos);
}

size_t text_buf_lines(const text_buf_t *tb)
{
    return (tb == NULL) ? 0u : tb->n_ends + 1u;
}

size_t text_buf_line_start(const text_buf_t *tb, size_t line)
{
    if (tb == NULL) {
        return 0u;
    }
    if (line == 0u) {
        return 0u;
    }
    if (line > tb->n_ends) {
        return tb->len;
    }
    return entry_pos(tb->ends[line - 1u]);
}

/**
 * @brief Where line @p line's text ends, before its line end.
 *
 * @param tb   The buffer.
 * @param line Line index up to tb->n_ends.
 * @return Text offset.
 */
static size_t line_end(const text_buf_t *tb, size_t line)
{
    if (line >= tb->n_ends) {
        return tb->len;
    }
    const uint32_t e = tb->ends[line];
    return entry_pos(e) - eol_bytes(entry_kind(e));
}

size_t text_buf_line_len(const text_buf_t *tb, size_t line)
{
    if (tb == NULL || line > tb->n_ends) {
        return 0u;
    }
    return line_end(tb, line) - text_buf_line_start(tb, line);
}

const char *text_buf_line(text_buf_t *tb, size_t line, size_t *len)
{
    if (tb == NULL || len == NULL || line > tb->n_ends) {
        return NULL;
    }
    const size_t s = text_buf_line_start(tb, line);
    const size_t e = line_end(tb, line);

    if (tb->gap_at > s && tb->gap_at < e) {
        move_gap(tb, (tb->gap_at - s < e - tb->gap_at) ? s : e);
    }
    *len = e - s;
    return (const char *)((e <= tb->gap_at) ? tb->mem + s
                                            : tb->mem + s + tb->gap_len);
}

size_t text_buf_line_of(const text_buf_t *tb, size_t pos)
{
    size_t lo = 0u;

    if (tb == NULL) {
        return 0u;
    }
    size_t hi = tb->n_ends;
    /* The count of line starts at or before pos. */
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2u;
        if (entry_pos(tb->ends[mid]) <= pos) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/* ------------------------------------------------------------ the units */

uint32_t text_buf_cp(const text_buf_t *tb, size_t pos, size_t *bytes)
{
    uint32_t cp = 0u;
    size_t n = 0u;

    if (tb != NULL && pos < tb->len) {
        const uint8_t b = at(tb, pos);
        if (b == '\r' || b == '\n') {
            cp = '\n';
            n = (b == '\r' && pos + 1u < tb->len && at(tb, pos + 1u) == '\n')
                    ? 2u : 1u;
        } else {
            n = decode(tb, pos, &cp);
        }
    }
    if (bytes != NULL) {
        *bytes = n;
    }
    return cp;
}

size_t text_buf_next(const text_buf_t *tb, size_t pos)
{
    size_t n;

    if (tb == NULL) {
        return 0u;
    }
    if (pos >= tb->len) {
        return tb->len;
    }
    (void)text_buf_cp(tb, pos, &n);
    return pos + n;
}

bool text_buf_is_unit(const text_buf_t *tb, size_t pos)
{
    uint32_t cp;

    if (tb == NULL || pos > tb->len) {
        return false;
    }
    if (pos == tb->len || pos == 0u) {
        return true;
    }
    const uint8_t b = at(tb, pos);
    if (b == '\n') {
        return at(tb, pos - 1u) != '\r';
    }
    if ((b & 0xC0u) != 0x80u) {
        return true;
    }
    /* A byte that starts a well-formed sequence is never inside one, so a
     * continuation byte is inside one when a sequence from 1 to 3 bytes
     * before it reaches over it. */
    for (size_t k = 1u; k <= 3u && k <= pos; ++k) {
        if (decode(tb, pos - k, &cp) > k) {
            return false;
        }
    }
    return true;
}

size_t text_buf_prev(const text_buf_t *tb, size_t pos)
{
    if (tb == NULL || pos == 0u) {
        return 0u;
    }
    if (pos > tb->len) {
        pos = tb->len;
    }
    /* A unit is 4 bytes at most, and offset 0 starts one. */
    for (size_t k = 1u; k < EDIT_MAX && k < pos; ++k) {
        if (text_buf_is_unit(tb, pos - k)) {
            return pos - k;
        }
    }
    return (pos > EDIT_MAX) ? pos - EDIT_MAX : 0u;
}

/* ------------------------------------------------------------ the cells */

/** @brief Where a walk along a line's cells is. */
typedef struct {
    size_t pos;   /**< text offset of the next unit */
    size_t end;   /**< where the line's text ends */
    size_t row;   /**< row of the wrapped line */
    size_t col;   /**< column in the row after the last unit */
    size_t cell;  /**< the next unit's first cell, row * width + column */
    size_t next;  /**< the cell after the next unit */
} cells_t;

/**
 * @brief Start a walk over line @p line.
 *
 * @param tb   The buffer.
 * @param line Line index up to tb->n_ends.
 * @param w    Out: the walk.
 */
static void cells_start(const text_buf_t *tb, size_t line, cells_t *w)
{
    memset(w, 0, sizeof(*w));
    w->pos = text_buf_line_start(tb, line);
    w->end = line_end(tb, line);
}

/**
 * @brief Place the unit at w->pos: its first cell and the one after it,
 *        starting a row where it does not fit.
 *
 * @param tb    The buffer.
 * @param w     The walk; w->pos under w->end.
 * @param width Cells of a row; 0 does not wrap.
 */
static void cells_place(const text_buf_t *tb, cells_t *w, unsigned width)
{
    const bool tab = at(tb, w->pos) == '\t';
    size_t cells = 1u;

    if (width > 0u && w->col >= width) {
        w->row++;
        w->col = 0u;
    }
    if (tab) {
        cells = TEXT_BUF_TAB - w->col % TEXT_BUF_TAB;
        if (width > 0u && w->col + cells > width) {
            cells = width - w->col;
        }
    }
    w->cell = w->row * width + w->col;
    w->next = w->cell + cells;
}

/**
 * @brief Step the walk past the unit cells_place() placed.
 *
 * @param tb The buffer.
 * @param w  The walk.
 */
static void cells_step(const text_buf_t *tb, cells_t *w)
{
    w->col += w->next - w->cell;
    w->pos = text_buf_next(tb, w->pos);
    if (w->pos > w->end) {
        w->pos = w->end;
    }
}

/**
 * @brief The cell after a walk's last unit.
 *
 * @param w     The walk at the line's end.
 * @param width Cells of a row.
 * @return row * width + column.
 */
static size_t cells_end(const cells_t *w, unsigned width)
{
    return w->row * width + w->col;
}

size_t text_buf_rows(const text_buf_t *tb, size_t line, unsigned width)
{
    cells_t w;

    if (tb == NULL || line > tb->n_ends) {
        return 0u;
    }
    cells_start(tb, line, &w);
    while (w.pos < w.end) {
        cells_place(tb, &w, width);
        cells_step(tb, &w);
    }
    return w.row + 1u;
}

void text_buf_cell_of(const text_buf_t *tb, size_t pos, unsigned width,
                      size_t *line, size_t *cell)
{
    cells_t w;
    size_t l = 0u;
    size_t c = 0u;

    if (tb != NULL) {
        if (pos > tb->len) {
            pos = tb->len;
        }
        l = text_buf_line_of(tb, pos);
        cells_start(tb, l, &w);
        while (w.pos < w.end) {
            cells_place(tb, &w, width);
            if (w.pos >= pos) {
                break;
            }
            cells_step(tb, &w);
        }
        c = (w.pos < w.end) ? w.cell : cells_end(&w, width);
    }
    if (line != NULL) {
        *line = l;
    }
    if (cell != NULL) {
        *cell = c;
    }
}

size_t text_buf_pos_at(const text_buf_t *tb, size_t line, size_t cell,
                       unsigned width)
{
    cells_t w;

    if (tb == NULL) {
        return 0u;
    }
    if (line > tb->n_ends) {
        line = tb->n_ends;
    }
    cells_start(tb, line, &w);
    while (w.pos < w.end) {
        cells_place(tb, &w, width);
        if (cell < w.next) {
            return w.pos;
        }
        cells_step(tb, &w);
    }
    return w.end;
}

/* ----------------------------------------------------------- the cursor */

size_t text_buf_cursor(const text_buf_t *tb)
{
    return (tb == NULL) ? 0u : tb->cursor;
}

void text_buf_set_cursor(text_buf_t *tb, size_t pos)
{
    if (tb == NULL) {
        return;
    }
    if (pos > tb->len) {
        pos = tb->len;
    }
    while (!text_buf_is_unit(tb, pos)) {
        pos--;
    }
    tb->cursor = pos;
}

bool text_buf_left(text_buf_t *tb)
{
    if (tb == NULL || tb->cursor == 0u) {
        return false;
    }
    tb->cursor = text_buf_prev(tb, tb->cursor);
    return true;
}

bool text_buf_right(text_buf_t *tb)
{
    if (tb == NULL || tb->cursor >= tb->len) {
        return false;
    }
    tb->cursor = text_buf_next(tb, tb->cursor);
    return true;
}

/* ------------------------------------------------------------ the edits */

text_buf_err_t text_buf_insert(text_buf_t *tb, uint32_t cp)
{
    uint8_t b[EDIT_MAX];

    if (tb == NULL || (cp < 0x20u && cp != '\t') ||
        (cp >= 0x7Fu && cp <= 0x9Fu) || (cp >= 0xD800u && cp <= 0xDFFFu) ||
        cp > 0x10FFFFu) {
        return TEXT_BUF_ARG;
    }
    const size_t n = encode(cp, b);
    if (tb->len + n > tb->limit) {
        return TEXT_BUF_FULL;
    }
    edit(tb, tb->cursor, 0u, b, n);
    tb->cursor += n;
    return TEXT_BUF_OK;
}

text_buf_err_t text_buf_insert_eol(text_buf_t *tb)
{
    static const uint8_t k_crlf[2] = {'\r', '\n'};
    text_eol_t kind;

    if (tb == NULL) {
        return TEXT_BUF_ARG;
    }
    kind = tb->eol;
    /* An LF after a lone CR, or a CR before an LF, would make one CR LF
     * of two line ends and add no line. */
    if ((kind == TEXT_EOL_LF && tb->cursor > 0u &&
         at(tb, tb->cursor - 1u) == '\r') ||
        (kind == TEXT_EOL_CR && tb->cursor < tb->len &&
         at(tb, tb->cursor) == '\n')) {
        kind = TEXT_EOL_CRLF;
    }
    const size_t n = eol_bytes(kind);
    if (tb->len + n > tb->limit) {
        return TEXT_BUF_FULL;
    }
    if (tb->n_ends >= tb->ends_cap) {
        return TEXT_BUF_TOO_MANY;
    }
    edit(tb, tb->cursor, 0u, (kind == TEXT_EOL_CR) ? k_crlf
                             : (kind == TEXT_EOL_LF) ? k_crlf + 1 : k_crlf,
         n);
    tb->cursor += n;
    return TEXT_BUF_OK;
}

bool text_buf_delete_before(text_buf_t *tb)
{
    if (tb == NULL || tb->cursor == 0u) {
        return false;
    }
    const size_t q = text_buf_prev(tb, tb->cursor);
    edit(tb, q, tb->cursor - q, NULL, 0u);
    /* A lone CR and an LF that meet are one CR LF: the cursor goes in
     * front of it. */
    text_buf_set_cursor(tb, q);
    return true;
}

bool text_buf_delete_after(text_buf_t *tb)
{
    if (tb == NULL || tb->cursor >= tb->len) {
        return false;
    }
    const size_t q = text_buf_next(tb, tb->cursor);
    edit(tb, tb->cursor, q - tb->cursor, NULL, 0u);
    text_buf_set_cursor(tb, tb->cursor);
    return true;
}

/* ------------------------------------------------------------ the state */

text_eol_t text_buf_eol(const text_buf_t *tb)
{
    return (tb == NULL) ? TEXT_EOL_LF : tb->eol;
}

bool text_buf_mixed(const text_buf_t *tb)
{
    unsigned kinds = 0u;

    if (tb == NULL) {
        return false;
    }
    for (size_t k = 0u; k < TEXT_EOL_KINDS; ++k) {
        kinds += (tb->kinds[k] > 0u) ? 1u : 0u;
    }
    return kinds > 1u;
}

bool text_buf_bom(const text_buf_t *tb)
{
    return tb != NULL && tb->bom;
}

bool text_buf_dirty(const text_buf_t *tb)
{
    return tb != NULL && tb->dirty;
}

void text_buf_settle(text_buf_t *tb)
{
    if (tb != NULL) {
        move_gap(tb, tb->len);
    }
}

void text_buf_saved(text_buf_t *tb)
{
    if (tb != NULL) {
        tb->dirty = false;
    }
}

uint32_t text_buf_revision(const text_buf_t *tb)
{
    return (tb == NULL) ? 0u : tb->revision;
}
