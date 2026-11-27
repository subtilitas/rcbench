/**
 * @file sprog_draw.c
 * @brief The layout and the drawing of the servo programmers' frame: the
 *        flow of the rows, the head, the notes and their pager, the rows'
 *        controls, the list and the hold page.  See sprog.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "sprog.h"

#include <stdio.h>

#include "ui_layout.h"
#include "ui_theme.h"

/** @name The layout, px from the area's top left. */
/** @{ */
#define ROW0     64           /**< the first row's top: the tabs and 8px   */
#define PITCH    56           /**< a row's pitch: 8px between two          */
#define ROW_H    UI_TAP_MIN   /**< a row's height, 48px                    */
#define VAL_W    150          /**< a value, 15 cells                       */
#define WIDE_W   260          /**< a wide row's control, keys included     */
#define STEP_W   56           /**< a step's - and + keys                   */
#define LINE_H   20           /**< a wrapped row's pitch                   */
#define CELL_W   10           /**< a cell of the face                      */
#define PAGE_TOP 64           /**< a page's first line: the title and 12px */
#define LIST_TOP 44           /**< a list's first entry                    */
#define FOOT_GAP 4            /**< a control at the foot over the band     */
/** @} */

/** @brief The face of the labels, the controls, the notes and the lines. */
#define FONT       (&gfx_font_10x20)
/** @brief The face of a page's title. */
#define TITLE_FONT (&gfx_font_20x36)

/** @brief A page's pitches between its lines, the widest that fits first:
 *         a line of one row at 28px leaves 8px under it. */
static const uint8_t k_pitch[] = { 28, 26, 24, 22, LINE_H };

/* ------------------------------------------------------------- wrapping */

/**
 * @brief The cells of a line: the area less 10px each side, 60 on a 620px
 *        area, at least 1.
 * @param f the frame
 * @return the cells
 */
static int text_cells(const sprog_t *f)
{
    const int cells = (f->area.w - 20) / CELL_W;

    return cells > 0 ? cells : 1;
}

/**
 * @brief The bytes of @p s its first row takes: up to a line break, else
 *        all of it where it fits, else up to the last space that keeps it
 *        in the line; a word longer than the line is cut.
 * @param f the frame
 * @param s the text
 * @return the bytes
 */
static size_t row_len(const sprog_t *f, const char *s)
{
    const size_t fit = gfx_text_prefix(s, text_cells(f));
    size_t cut = fit;

    for (size_t i = 0; i < fit; ++i) {
        if (s[i] == '\n') {
            return i;
        }
    }
    if (s[fit] == '\0' || s[fit] == ' ' || s[fit] == '\n') {
        return fit;
    }
    while (cut > 0u && s[cut] != ' ') {
        --cut;
    }
    return cut > 0u ? cut : fit;
}

/**
 * @brief The start of the row after the one of @p len bytes at @p s: past
 *        one line break and the spaces.
 * @param s   the row
 * @param len its bytes
 * @return the next row
 */
static const char *next_row(const char *s, size_t len)
{
    s += len;
    if (*s == '\n') {
        ++s;
    }
    while (*s == ' ') {
        ++s;
    }
    return s;
}

int sprog_text_rows(const sprog_t *f, const char *s)
{
    int n = 0;

    while (s != NULL && *s != '\0') {
        s = next_row(s, row_len(f, s));
        ++n;
    }
    return n;
}

/**
 * @brief Draws @p s from @p y down, a row each LINE_H, the rows that end
 *        above @p limit.
 * @param f     the frame
 * @param c     the canvas
 * @param x     the left edge
 * @param y     the first row's top
 * @param s     the text
 * @param color its colour
 * @param limit the lowest a row may end at
 * @return the y under the last row drawn
 */
static int text_rows(const sprog_t *f, gfx_canvas_t *c, int x, int y,
                     const char *s, gfx_color_t color, int limit)
{
    while (s != NULL && *s != '\0' && y + LINE_H <= limit) {
        char row[SPROG_LINE];
        const size_t len = row_len(f, s);

        snprintf(row, sizeof(row), "%.*s", (int)len, s);
        gfx_text(c, x, y, row, FONT, color, 1);
        s = next_row(s, len);
        y += LINE_H;
    }
    return y;
}

/** @brief The colour of @p level, a sprog_level_t.
 * @param level the level
 * @return the colour */
static gfx_color_t level_color(unsigned level)
{
    switch (level) {
    case SPROG_LV_DIM:    return ui_theme_color(UI_C_TEXT_DIM);
    case SPROG_LV_FAINT:  return ui_theme_color(UI_C_TEXT_FAINT);
    case SPROG_LV_ACCENT: return ui_theme_color(UI_C_ACCENT);
    case SPROG_LV_WARN:   return ui_theme_color(UI_C_WARN);
    case SPROG_LV_DANGER: return ui_theme_color(UI_C_DANGER);
    default:              return ui_theme_color(UI_C_TEXT);
    }
}

/** @brief The colour of @p fill, a sprog_fill_t.
 * @param fill the fill
 * @return the colour */
static gfx_color_t fill_color(unsigned fill)
{
    switch (fill) {
    case SPROG_F_ACCENT:  return ui_theme_color(UI_C_ACCENT);
    case SPROG_F_WARN:    return ui_theme_color(UI_C_WARN);
    case SPROG_F_GO:      return ui_action_fill(UI_ACTION_AVAILABLE);
    case SPROG_F_ACTIVE:  return ui_action_fill(UI_ACTION_ACTIVE);
    case SPROG_F_WAITING: return ui_action_fill(UI_ACTION_WAITING);
    case SPROG_F_HI:      return ui_theme_color(UI_C_PANEL_HI);
    default:              return ui_theme_color(UI_C_PANEL_SUNK);
    }
}

/* -------------------------------------------------------------- geometry */

/** @brief An empty rectangle. */
static gfx_rect_t none(void)
{
    return gfx_rect_make(0, 0, 0, 0);
}

int sprog_foot(const sprog_t *f)
{
    const int area = f->area.y + f->area.h - 8;
    const int band = UI_ALERT_Y - UI_BAND_H - FOOT_GAP;

    return area < band ? area : band;
}

/**
 * @brief Whether row @p r shows by the view's mask and its tab shows.
 * @param f the frame
 * @param r the row
 * @return true when it shows
 */
static bool row_shown(const sprog_t *f, const sprog_row_t *r)
{
    return r->tab < 8u && (f->tab_bits & (1u << r->tab)) != 0u
           && (r->show == 0u || (r->show & f->view.mask) != 0u);
}

/**
 * @brief Where row @p row flows on its tab: the tab's rows that show, in
 *        table order, a wide row on a line of its own, the others two to a
 *        line.
 * @param f    the frame
 * @param row  the row
 * @param line its line, from 0
 * @param col  its column, 0 or 1
 * @return false for a row not shown or below the grid's last line
 */
static bool place(const sprog_t *f, int row, int *line, int *col)
{
    const sprog_row_t *rows = f->def->rows;
    int ln = 0, c = 0;

    if (row < 0 || row >= f->def->row_count || !row_shown(f, &rows[row])) {
        return false;
    }
    for (int i = 0; i < row; ++i) {
        if (rows[i].tab != rows[row].tab || !row_shown(f, &rows[i])) {
            continue;
        }
        if (rows[i].wide != 0u) {
            ln += c != 0 ? 2 : 1;       /* the line begun, and its own */
            c = 0;
        } else if (c == 1) {
            ++ln;
            c = 0;
        } else {
            c = 1;
        }
    }
    if (rows[row].wide != 0u && c != 0) {
        ++ln;
        c = 0;
    }
    *line = ln;
    *col = c;
    return ln < SPROG_LINES;
}

/**
 * @brief The kind row @p row has now: its cell's on the tab shown, else
 *        the table's.
 * @param f   the frame
 * @param row the row
 * @return the sprog_kind_t
 */
static unsigned kind_of(const sprog_t *f, int row)
{
    for (int i = 0; i < f->cells; ++i) {
        if (f->cell_row[i] == row) {
            return f->view.cell[i].kind;
        }
    }
    return f->def->rows[row].kind;
}

gfx_rect_t sprog_tab_rect(const sprog_t *f, int tab)
{
    for (int t = 0; t < f->tabs.count; ++t) {
        if (f->tab_of[t] == tab) {
            return f->tabs.rect[t];
        }
    }
    return none();
}

gfx_rect_t sprog_row_rect(const sprog_t *f, int row)
{
    const int col_w = (f->area.w - 30) / 2;
    int line = 0, col = 0;

    if (!place(f, row, &line, &col)) {
        return none();
    }
    return gfx_rect_make(col == 0 ? f->area.x + 10 : f->area.x + 20 + col_w,
                         f->area.y + ROW0 + line * PITCH,
                         f->def->rows[row].wide != 0u ? f->area.w - 20
                                                       : col_w,
                         ROW_H);
}

/**
 * @brief Row @p row's whole control: the row's w, else 260px in a wide
 *        row and 150px in a narrow one, at the row's right edge.
 * @param f   the frame
 * @param row the row
 * @return the rectangle; empty for a row not shown
 */
static gfx_rect_t control_rect(const sprog_t *f, int row)
{
    const gfx_rect_t rr = sprog_row_rect(f, row);
    const sprog_row_t *r;
    int w;

    if (rr.w == 0) {
        return rr;
    }
    r = &f->def->rows[row];
    w = r->w != 0u ? r->w : r->wide != 0u ? WIDE_W : VAL_W;
    return gfx_rect_make(rr.x + rr.w - w, rr.y, w, rr.h);
}

gfx_rect_t sprog_value_rect(const sprog_t *f, int row)
{
    const gfx_rect_t v = control_rect(f, row);

    if (v.w == 0 || kind_of(f, row) != SPROG_K_STEP) {
        return v;
    }
    return gfx_rect_make(v.x + STEP_W + 4, v.y, v.w - 2 * (STEP_W + 4), v.h);
}

gfx_rect_t sprog_step_rect(const sprog_t *f, int row, bool up)
{
    const gfx_rect_t v = control_rect(f, row);

    if (v.w == 0) {
        return v;
    }
    return gfx_rect_make(up ? v.x + v.w - STEP_W : v.x, v.y, STEP_W, v.h);
}

gfx_rect_t sprog_pair_rect(const sprog_t *f, int row, int key)
{
    const gfx_rect_t v = control_rect(f, row);
    const int w = (v.w - 8) / 2;

    if (v.w == 0) {
        return v;
    }
    return gfx_rect_make(key != 0 ? v.x + v.w - w : v.x, v.y, w, v.h);
}

gfx_rect_t sprog_head_rect(const sprog_t *f)
{
    for (int i = 0; i < f->cells; ++i) {
        if (f->view.cell[i].kind == SPROG_K_HEAD) {
            const gfx_rect_t rr = sprog_row_rect(f, f->cell_row[i]);
            const gfx_rect_t vr = control_rect(f, f->cell_row[i]);

            return gfx_rect_make(rr.x, rr.y, vr.x - 10 - rr.x, rr.h);
        }
    }
    return none();
}

gfx_rect_t sprog_list_cancel_rect(const sprog_t *f)
{
    return gfx_rect_make(f->area.x + f->area.w - 10 - 150,
                         sprog_foot(f) - UI_BTN_H, 150, UI_BTN_H);
}

gfx_rect_t sprog_list_rect(const sprog_t *f, int i)
{
    /* Under the title, 48px tall at a 56px pitch, ending 8px above
     * CANCEL.  When one column does not hold them all, two to a line in
     * columns 10px apart, in their order, and an entry wider than a
     * column on a line of its own. */
    const int top = f->area.y + LIST_TOP;
    const int fit = (sprog_list_cancel_rect(f).y - 8 - top + 8) / PITCH;
    const int per = fit < 1 ? 1 : fit;
    const int fx = f->area.x + 10;
    const int fw = f->area.w - 20;
    const int cw = (f->area.w - 30) / 2;
    int row = 0, col = 0;

    if (i < 0 || i >= f->list.count) {
        return none();
    }
    if (f->list.count <= per) {
        return gfx_rect_make(fx, top + i * PITCH, fw, ROW_H);
    }
    for (int j = 0; j <= i; ++j) {
        const bool wide = gfx_text_width(FONT, f->list.label[j], 1) > cw - 20;

        if (wide && col != 0) {
            ++row;              /* a wide entry starts a line of its own */
            col = 0;
        }
        if (j == i) {
            return wide ? gfx_rect_make(fx, top + row * PITCH, fw, ROW_H)
                        : gfx_rect_make(fx + col * (cw + 10),
                                        top + row * PITCH, cw, ROW_H);
        }
        if (wide || col == 1) {
            ++row;
            col = 0;
        } else {
            col = 1;
        }
    }
    return none();
}

gfx_rect_t sprog_apply_rect(const sprog_t *f)
{
    /* The drop page's hold takes the width up to 20px left of BACK:
     * 380px on a 620px area. */
    const int w = f->page.open && f->page.drop ? f->area.w - 240 : 300;

    return gfx_rect_make(f->area.x + 20, sprog_foot(f) - UI_BTN_H_TALL, w,
                         UI_BTN_H_TALL);
}

gfx_rect_t sprog_page_cancel_rect(const sprog_t *f)
{
    return gfx_rect_make(f->area.x + f->area.w - 20 - 180,
                         sprog_foot(f) - UI_BTN_H_TALL, 180, UI_BTN_H_TALL);
}

/* ----------------------------------------------------------------- notes */

/** @brief Where the notes and the bottom line go. */
typedef struct {
    int top;                  /**< the first note's row                    */
    int bottom_y;             /**< the bottom line's first row             */
    int room;                 /**< rows from top to the bottom line        */
    int pages;                /**< pages the notes take                    */
    int on;                   /**< the page shown                          */
    int limit;                /**< the lowest a note's row may end at      */
    int rows[SPROG_NOTES];    /**< rows each note takes                    */
    int page[SPROG_NOTES];    /**< page each note is on; -1 none           */
} plan_t;

/**
 * @brief Plans the notes of the view: their rows, pages and the bottom
 *        line's place.  Notes that need more rows than there are above the
 *        bottom line page, the last row of the room kept for the pager; a
 *        room of one row shows no note.
 * @param f the frame
 * @param p the plan
 */
static void plan(const sprog_t *f, plan_t *p)
{
    const int foot = sprog_foot(f);
    const int n = f->view.notes;
    int need = 0, used = 0, pg = 0;

    p->top = f->area.y + ROW0 + f->lines * PITCH;
    p->bottom_y = foot - sprog_text_rows(f, f->view.bottom) * LINE_H;
    if (p->bottom_y < p->top) {
        p->bottom_y = p->top;   /* a bottom line taller shows its first rows */
    }
    p->room = (p->bottom_y - p->top) / LINE_H;
    p->limit = p->bottom_y;
    for (int i = 0; i < n; ++i) {
        p->rows[i] = sprog_text_rows(f, f->view.note[i]);
        need += p->rows[i];
        p->page[i] = 0;
    }
    p->pages = 1;
    if (need <= p->room) {
        p->on = 0;
        return;
    }
    if (p->room < 2) {
        for (int i = 0; i < n; ++i) {
            p->page[i] = -1;
        }
        p->on = 0;
        return;
    }
    p->limit = p->bottom_y - LINE_H;
    for (int i = 0; i < n; ++i) {
        if (used > 0 && used + p->rows[i] > p->room - 1) {
            ++pg;
            used = 0;
        }
        p->page[i] = pg;
        used += p->rows[i];
    }
    p->pages = pg + 1;
    p->on = f->note_page < p->pages ? f->note_page : 0;
}

int sprog_note_pages(const sprog_t *f)
{
    plan_t p;

    plan(f, &p);
    return p.pages;
}

gfx_rect_t sprog_notes_rect(const sprog_t *f)
{
    plan_t p;

    plan(f, &p);
    return p.pages > 1 ? gfx_rect_make(f->area.x, p.top, f->area.w,
                                       p.room * LINE_H)
                       : none();
}

/**
 * @brief Starts the notes on their first page when their set changes:
 *        another tab, other notes, another bottom line.
 * @param f the frame
 */
static void notes_follow(sprog_t *f)
{
    uint32_t sig = 2166136261u;

    for (int i = 0; i < f->view.notes; ++i) {
        sig = (sig ^ (uint32_t)sprog_text_rows(f, f->view.note[i]))
              * 16777619u;
    }
    sig = (sig ^ (uint32_t)f->view.notes) * 16777619u;
    sig = (sig ^ (uint32_t)sprog_text_rows(f, f->view.bottom)) * 16777619u;
    sig = (sig ^ (uint32_t)sprog_tab(f)) * 16777619u;
    if (sig != f->note_sig) {
        f->note_sig = sig;
        f->note_page = 0;
    }
}

/** @brief Draws the notes of the page shown, the pager and the bottom line.
 * @param f the frame
 * @param c the canvas */
static void draw_notes(sprog_t *f, gfx_canvas_t *c)
{
    plan_t p;
    int y;

    notes_follow(f);
    plan(f, &p);
    y = p.top;
    for (int i = 0; i < f->view.notes; ++i) {
        if (p.page[i] == p.on) {
            y = text_rows(f, c, f->area.x + 10, y, f->view.note[i],
                          level_color(f->view.note_level[i]), p.limit);
        }
    }
    if (p.pages > 1) {
        char count[12];

        snprintf(count, sizeof(count), "%u/%u", (unsigned)(uint8_t)(p.on + 1),
                 (unsigned)(uint8_t)p.pages);
        gfx_text_in(c, gfx_rect_make(f->area.x + 10, p.bottom_y - LINE_H,
                                     f->area.w - 20, LINE_H),
                    count, FONT, ui_theme_color(UI_C_TEXT_FAINT), 1,
                    GFX_ALIGN_RIGHT);
    }
    (void)text_rows(f, c, f->area.x + 10, p.bottom_y, f->view.bottom,
                    level_color(f->view.bottom_level), sprog_foot(f));
}

/* ------------------------------------------------------------------ rows */

/**
 * @brief A control: a button, or a flat value with SPROG_L_FLAT.
 * @param c       the canvas
 * @param r       the control
 * @param label   its text
 * @param fill    its sprog_fill_t
 * @param look    the cell's SPROG_L_* bits
 * @param pressed lightens the fill
 * @param enabled false dims a button
 */
static void control(gfx_canvas_t *c, gfx_rect_t r, const char *label,
                    unsigned fill, unsigned look, bool pressed, bool enabled)
{
    const gfx_color_t warn = ui_theme_color(UI_C_WARN);

    if ((look & SPROG_L_FLAT) != 0u) {
        gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CTL,
                            fill != SPROG_F_SUNK ? fill_color(fill)
                            : pressed ? ui_theme_color(UI_C_PANEL)
                                      : fill_color(SPROG_F_SUNK));
        gfx_text_in(c, r, label, FONT,
                    (look & SPROG_L_INK_WARN) != 0u
                        ? warn : ui_theme_color(UI_C_TEXT),
                    1, GFX_ALIGN_CENTER);
    } else if ((look & SPROG_L_INK_WARN) != 0u) {
        ui_button_ink(c, r, label, fill_color(fill), warn, pressed, enabled,
                      FONT);
    } else {
        ui_button_font(c, r, label, fill_color(fill), pressed, enabled, FONT);
    }
}

/**
 * @brief Whether the press is on row @p row's control, key @p key.
 * @param f   the frame
 * @param row the row
 * @param key the key of a pair, 0 for any other
 * @return true while it is pressed
 */
static bool pressed(const sprog_t *f, int row, int key)
{
    return f->have && f->row == row
           && ((f->on == SPROG_ON_ROW && f->key == key)
               || f->on == SPROG_ON_HOLD);
}

/**
 * @brief The head: its two lines left, in warn while no port is bound.
 * @param f  the frame
 * @param c  the canvas
 * @param rr the row
 */
static void draw_head(const sprog_t *f, gfx_canvas_t *c, gfx_rect_t rr)
{
    if (f->view.head[0] != NULL) {
        gfx_text(c, rr.x, rr.y + 4, f->view.head[0], FONT,
                 ui_theme_color(f->view.head_warn ? UI_C_WARN : UI_C_TEXT),
                 1);
    }
    if (f->view.head[1] != NULL) {
        gfx_text(c, rr.x, rr.y + 4 + LINE_H, f->view.head[1], FONT,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
    }
}

/**
 * @brief A row's label: dim while its control takes a press, a SHOW row's
 *        always; faint while off.
 * @param f  the frame
 * @param c  the canvas
 * @param rr the row
 * @param r  its table entry
 * @param cl its cell
 */
static void draw_label(const sprog_t *f, gfx_canvas_t *c, gfx_rect_t rr,
                       const sprog_row_t *r, const sprog_cell_t *cl)
{
    const char *s = cl->label != NULL ? cl->label
                    : r->label >= 0 ? ui_tr((ui_text_id_t)r->label)
                                    : r->name;
    const bool live = cl->press != SPROG_P_OFF || cl->kind == SPROG_K_SHOW
                      || (cl->look & SPROG_L_LABEL_DIM) != 0u;
    const ui_color_id_t col = (cl->look & SPROG_L_LABEL_ACCENT) != 0u
                                     ? UI_C_ACCENT
                                 : live ? UI_C_TEXT_DIM : UI_C_TEXT_FAINT;

    (void)f;
    if (s != NULL) {
        gfx_text(c, rr.x, rr.y + (ROW_H - LINE_H) / 2, s, FONT,
                 ui_theme_color(col), 1);
    }
}

/**
 * @brief Draws cell @p i of the tab shown: its label and its control.
 * @param f the frame
 * @param c the canvas
 * @param i the cell
 */
static void draw_cell(sprog_t *f, gfx_canvas_t *c, int i)
{
    const int row = f->cell_row[i];
    const sprog_row_t *r = &f->def->rows[row];
    const sprog_cell_t *cl = &f->view.cell[i];
    const gfx_rect_t rr = sprog_row_rect(f, row);
    const gfx_rect_t vr = sprog_value_rect(f, row);
    const bool live = cl->press != SPROG_P_OFF;

    switch (cl->kind) {
    case SPROG_K_GAP:
        return;
    case SPROG_K_HEAD:
        draw_head(f, c, rr);
        break;
    default:
        draw_label(f, c, rr, r, cl);
        break;
    }
    switch (cl->kind) {
    case SPROG_K_HEAD:
    case SPROG_K_ACT:
        if (row == f->hold_row) {
            /* The row hold's control: its fade, and its flash after. */
            ui_hold_button(c, vr, cl->value, fill_color(cl->fill),
                           fill_color(cl->fill_to), &f->hold,
                           pressed(f, row, 0), live, FONT);
        } else {
            control(c, vr, cl->value, cl->fill, cl->look, pressed(f, row, 0),
                    live);
        }
        break;
    case SPROG_K_PAIR:
        control(c, sprog_pair_rect(f, row, 0), cl->value, cl->fill, cl->look,
                pressed(f, row, 0), live);
        control(c, sprog_pair_rect(f, row, 1),
                cl->value2 != NULL ? cl->value2 : "", cl->fill2, cl->look,
                pressed(f, row, 1), cl->press2 != SPROG_P_OFF);
        break;
    case SPROG_K_SHOW:
        control(c, vr, cl->value, cl->fill, cl->look, false, false);
        break;
    case SPROG_K_STEP:
        control(c, vr, cl->value, cl->fill, cl->look, pressed(f, row, 0),
                live);
        if ((cl->look & SPROG_L_NO_KEYS) != 0u) {
            break;
        }
        ui_button_font(c, sprog_step_rect(f, row, false), "-",
                       fill_color(SPROG_F_HI),
                       f->have && f->on == SPROG_ON_MINUS && f->row == row,
                       live, FONT);
        ui_button_font(c, sprog_step_rect(f, row, true), "+",
                       fill_color(SPROG_F_HI),
                       f->have && f->on == SPROG_ON_PLUS && f->row == row,
                       live, FONT);
        break;
    default:
        control(c, vr, cl->value, cl->fill, cl->look, pressed(f, row, 0),
                live);
        break;
    }
}

/* -------------------------------------------------------------- overlays */

/** @brief Draws the list: its title, an entry per value and CANCEL.
 * @param f the frame
 * @param c the canvas */
static void draw_list(const sprog_t *f, gfx_canvas_t *c)
{
    gfx_text(c, f->area.x + 10, f->area.y + 14, f->list.title, FONT,
             ui_theme_color(UI_C_TEXT), 1);
    for (int i = 0; i < f->list.count; ++i) {
        const bool current = f->list.value[i] == f->list.current;

        ui_button_font(c, sprog_list_rect(f, i), f->list.label[i],
                       fill_color(current ? SPROG_F_ACCENT : SPROG_F_SUNK),
                       f->have && f->on == SPROG_ON_LIST && f->row == i,
                       true, FONT);
    }
    ui_button_font(c, sprog_list_cancel_rect(f), TR(CANCEL),
                   fill_color(SPROG_F_SUNK),
                   f->have && f->on == SPROG_ON_LIST_CANCEL, true, FONT);
}

/** @brief The page's edge colour: danger or warn.
 * @param f the frame
 * @return the colour */
static gfx_color_t edge_color(const sprog_t *f)
{
    return ui_theme_color(f->page.danger ? UI_C_DANGER : UI_C_WARN);
}

/** @brief Draws the page: the edge, the title, the lines at the widest
 *         pitch that ends them 8px above the buttons, the hold and CANCEL.
 * @param f the frame
 * @param c the canvas */
static void draw_page(sprog_t *f, gfx_canvas_t *c)
{
    const gfx_rect_t a = f->area;
    const gfx_color_t edge = edge_color(f);
    const int rows = sprog_page_rows(f);
    const int room = sprog_apply_rect(f).y - 8 - (a.y + PAGE_TOP) - LINE_H
                     - (rows - f->page.count) * LINE_H;
    int pitch = LINE_H;
    int y = a.y + PAGE_TOP;

    for (int i = 0; i < 3; ++i) {
        gfx_draw_rect(c, a.x + i, a.y + i, a.w - 2 * i, a.h - 2 * i, edge);
    }
    gfx_text(c, a.x + 12, a.y + 16, f->page.title, TITLE_FONT, edge, 1);
    for (unsigned i = 0; i < sizeof(k_pitch); ++i) {
        if ((f->page.count - 1) * k_pitch[i] <= room) {
            pitch = k_pitch[i];
            break;
        }
    }
    for (int i = 0; i < f->page.count; ++i) {
        y = text_rows(f, c, a.x + 12, y, f->page.line[i],
                      level_color(f->page.level[i]), a.y + a.h)
            + pitch - LINE_H;
    }
    ui_hold_button(c, sprog_apply_rect(f), f->page.apply,
                   fill_color(SPROG_F_SUNK), edge, &f->page.hold,
                   f->page.down, true, FONT);
    ui_button_font(c, sprog_page_cancel_rect(f), f->page.cancel,
                   fill_color(SPROG_F_SUNK),
                   f->have && f->on == SPROG_ON_CANCEL, true, FONT);
}

void sprog_render(sprog_t *f, gfx_canvas_t *c)
{
    const gfx_rect_t a = f->area;

    if (f->kp.open) {
        ui_keypad_render(&f->kp, c);
        return;
    }
    if (f->tk.open) {
        ui_textkey_render(&f->tk, c);
        return;
    }
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    if (f->page.open) {
        draw_page(f, c);
        return;
    }
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_ACCENT));
    if (f->list.open) {
        draw_list(f, c);
    } else {
        ui_tabs_render(&f->tabs, c);
        for (int i = 0; i < f->cells; ++i) {
            draw_cell(f, c, i);
        }
        draw_notes(f, c);
    }
    /* A page closes as its hold fires: its button flashes where it was. */
    ui_hold_ghost(c, &f->page.hold);
}
