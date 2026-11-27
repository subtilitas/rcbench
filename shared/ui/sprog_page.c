/**
 * @file sprog_page.c
 * @brief The hold page builder of the servo programmers' frame: the lines,
 *        the room they take, the groups that fold into "and n more", the
 *        fit and the parts.  See sprog.h.
 *
 * The page's lines take a row of 20px each, wrapped at the area's cells,
 * from y 64 to 8px above the buttons.  sprog_render() spaces them at the
 * widest of 28, 26, 24, 22 and 20px that ends them there; the fit and the
 * parts here keep a page to the 28px pitch.  The content (which lines, in
 * which order, the hidden values first) is the brand's.
 *
 * SPDX-License-Identifier: MIT
 */
#include "sprog.h"

#include <stdio.h>
#include <string.h>

/** @brief A page's first line, px under the area's top: the title in the
 *         20x36 face from 16px, and 12px. */
#define PAGE_TOP 64
/** @brief A wrapped row's pitch, px. */
#define LINE_H   20
/** @brief The space a line leaves under it at the widest pitch, px. */
#define GAP      8

void sprog_page_open(sprog_t *f, int16_t what, bool danger,
                     const char *title, const char *apply,
                     const char *cancel, int16_t hint, bool drop)
{
    /* A page the last one's hold opens keeps that hold's flash. */
    ui_hold_t hold = f->page.hold;

    ui_hold_spend(&hold);
    memset(&f->page, 0, sizeof(f->page));
    f->page.hold = hold;
    f->page.open = true;
    f->page.what = what;
    f->page.danger = danger;
    f->page.drop = drop;
    f->page.hint = hint;
    snprintf(f->page.title, sizeof(f->page.title), "%s", title);
    snprintf(f->page.apply, sizeof(f->page.apply), "%s", apply);
    snprintf(f->page.cancel, sizeof(f->page.cancel), "%s", cancel);
    ++f->rev;
}

bool sprog_page_line(sprog_t *f, const char *s, uint8_t level)
{
    if (f->page.count >= SPROG_PAGE_LINES) {
        return false;
    }
    f->page.level[f->page.count] = level;
    snprintf(f->page.line[f->page.count++], SPROG_LINE, "%s", s);
    return true;
}

int sprog_page_rows(const sprog_t *f)
{
    int n = 0;

    for (int i = 0; i < f->page.count; ++i) {
        n += sprog_text_rows(f, f->page.line[i]);
    }
    return n;
}

int sprog_page_room(const sprog_t *f)
{
    const int fit = (sprog_apply_rect(f).y - 8 - (f->area.y + PAGE_TOP))
                    / LINE_H;

    return (fit < SPROG_PAGE_ROWS ? fit : SPROG_PAGE_ROWS)
           - sprog_page_rows(f);
}

/* ---------------------------------------------------------------- groups */

void sprog_page_group(sprog_t *f, int total, int cap, int cap_rows,
                      uint8_t fold, uint8_t level)
{
    sprog_group_t *g = &f->page.group;
    const int lines = SPROG_PAGE_LINES - f->page.count;
    const int rows = sprog_page_room(f);

    memset(g, 0, sizeof(*g));
    g->on = true;
    g->first = f->page.count;
    g->total = (int16_t)total;
    g->cap = (int16_t)(cap < lines ? cap : lines);
    g->cap_rows = (int16_t)(cap_rows < rows ? cap_rows : rows);
    g->level = level;
    g->fold = fold;
}

/**
 * @brief The words of the group's fold line for @p left items left.
 * @param g    the group
 * @param left the items it counts
 * @param out  the line, SPROG_LINE bytes
 */
static void fold_text(const sprog_group_t *g, int left, char *out)
{
    if (g->fold != SPROG_FOLD_WARNINGS) {
        snprintf(out, SPROG_LINE, TR(KP_VALUES_MORE), (unsigned)left);
    } else if (left == 1) {
        snprintf(out, SPROG_LINE, "%s", TR(KP_CHECK_MORE_ONE));
    } else {
        snprintf(out, SPROG_LINE, TR(KP_CHECK_MORE), (unsigned)left);
    }
}

/**
 * @brief Writes the group's fold line: the items left, @p left.
 * @param f    the frame
 * @param left the items it counts
 */
static void fold(sprog_t *f, int left)
{
    sprog_group_t *g = &f->page.group;
    char line[SPROG_LINE];

    fold_text(g, left, line);
    if (sprog_page_line(f, line, g->level)) {
        ++g->lines;
    }
    g->folded = true;
}

bool sprog_page_item(sprog_t *f, const char *s, uint8_t level)
{
    sprog_group_t *g = &f->page.group;
    const int i = g->shown;
    const int rows = sprog_text_rows(f, s);
    /* An item that is not the last leaves a line and a row for the fold. */
    const int after = i == g->total - 1 ? 0 : 1;

    if (!g->on || g->folded) {
        return false;
    }
    if (i + 1 + after > g->cap || g->used + rows + after > g->cap_rows
        || !sprog_page_line(f, s, level)) {
        fold(f, g->total - i);
        return false;
    }
    ++g->shown;
    ++g->lines;
    g->used = (int16_t)(g->used + rows);
    return true;
}

/* ------------------------------------------------------- the fit, parts */

/**
 * @brief Whether the page's lines end 8px above its buttons at the 28px
 *        pitch.
 * @param f the frame
 * @return true when they do
 */
static bool fits(const sprog_t *f)
{
    const int bottom = f->area.y + PAGE_TOP + sprog_page_rows(f) * LINE_H
                       + (f->page.count > 0 ? f->page.count - 1 : 0) * GAP;

    return bottom <= sprog_apply_rect(f).y - 8;
}

/**
 * @brief Drops line @p at, the lines below it moving up.
 * @param f  the frame
 * @param at the line
 */
static void drop_line(sprog_t *f, int at)
{
    for (int i = at; i + 1 < f->page.count; ++i) {
        memcpy(f->page.line[i], f->page.line[i + 1], SPROG_LINE);
        f->page.level[i] = f->page.level[i + 1];
    }
    --f->page.count;
}

bool sprog_page_fit(sprog_t *f)
{
    sprog_group_t *g = &f->page.group;

    while (!fits(f) && g->on && g->lines > 0) {
        /* One line fewer for the group: its last line goes and the one
         * before it counts the rest, or the count goes where none is left. */
        const int keep = g->lines - 1;
        const int last = g->first + g->lines - 1;

        drop_line(f, last);
        --g->lines;
        if (keep > 0) {
            g->folded = true;
            fold_text(g, g->total - (keep - 1), f->page.line[last - 1]);
            f->page.level[last - 1] = g->level;
        }
    }
    return fits(f);
}

int sprog_page_part(sprog_t *f, int from)
{
    const int all = f->page.count;
    /* A later part keeps the first line, which names what it is about. */
    const int head = from > 0 ? 1 : 0;
    int end;

    for (int i = head; i < from && f->page.count > head; ++i) {
        drop_line(f, head);
    }
    while (f->page.count > head + 1 && !fits(f)) {
        drop_line(f, f->page.count - 1);
    }
    end = from + f->page.count - head;
    return end < all ? end : all;
}
