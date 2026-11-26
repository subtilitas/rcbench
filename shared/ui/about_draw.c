/**
 * @file
 * @brief ABOUT's column and its drawing: the screen's own lines in the
 *        language showing, the generated parts under their headings, the
 *        scroll bar and the footer.  See about_int.h for the geometry.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "rcbench_version.h"
#include "upd_screen.h"
/* ui_text.h before ui_theme.h: ui_text.def ends by undefining UI_TEXT. */
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#include "about_int.h"

/*
 * The lines that stay as they are written in every language: the name as
 * the splash writes it, the designer, the implementers, the copyright
 * line, the licences' names and the repository.
 */
#define AB_NAME      "rcbench"                         /**< the product */
#define AB_DESIGN    "Design: Julian Wingert"          /**< the designer */
#define AB_IMPL      "Implemented by Claude and Julian" /**< the implementers */
#define AB_COPYRIGHT "Copyright 2026 Julian Wingert"   /**< the copyright */
#define AB_LIC_SW    "MIT License (MIT)"               /**< LICENSE */
/** @brief hardware/LICENSE. */
#define AB_LIC_HW    "CC BY-NC-SA 4.0 (CC-BY-NC-SA-4.0)"
#define AB_REPO      "github.com/subtilitas/rcbench"   /**< the repository */

/** @brief The formatted lines, kept while a frame draws them. */
static char s_fw[64];
static char s_cu[96];      /**< the IOmcu's version line */
static char s_sw[96];      /**< the software's licence line */
static char s_hw[96];      /**< the hardware's licence line */
static char s_repo[96];    /**< the repository line */

/** @brief The face of a style. */
static const gfx_font_t *face_of(char style)
{
    return (style == 'H') ? AB_FONT_HEAD : AB_FONT_ROW;
}

/** @brief The ink of a style. */
static gfx_color_t ink_of(char style)
{
    switch (style) {
    case 'H':
    case 'S':
        return UI_ACCENT;
    case 'D':
        return UI_TEXT_DIM;
    default:
        return UI_TEXT;
    }
}

/** @brief A line of the screen's own: its rows from the wrap that draws it. */
static ab_seg_t line(char style, const char *text)
{
    ab_seg_t s = { style, text, 0, 1 };
    if (style != 'B' && text != NULL) {
        const gfx_font_t *f = face_of(style);
        const int rows = ui_text_wrap(NULL, gfx_rect_make(AB_X, 0, AB_W,
                                                          gfx_text_height(f, 1)),
                                      AB_PITCH, text, f, UI_TEXT);
        s.rows = (rows > 0) ? rows : 1;
    }
    return s;
}

/** @brief A run of generated rows: part @p part. */
static ab_seg_t run(int part)
{
    const int first = about_gen_part[part];
    return (ab_seg_t){ 0, NULL, first, (int)about_gen_part[part + 1] - first };
}

/** @brief The version lines, formatted anew. */
static void format_lines(void)
{
    (void)snprintf(s_fw, sizeof(s_fw), TR(ABOUT_FIRMWARE),
                   RCBENCH_VERSION_STRING);
    const upd_cu_view_t *cu = upd_screen_coproc();
    char ver[24];
    if (cu != NULL && cu->known) {
        (void)snprintf(ver, sizeof(ver), "%u.%u.%u",
                       (unsigned)cu->running.major,
                       (unsigned)cu->running.minor,
                       (unsigned)cu->running.patch);
    } else {
        (void)snprintf(ver, sizeof(ver), "%s", TR(ABOUT_NOT_KNOWN));
    }
    (void)snprintf(s_cu, sizeof(s_cu), TR(ABOUT_IOMCU), ver);
    (void)snprintf(s_sw, sizeof(s_sw), TR(ABOUT_LIC_SOFTWARE), AB_LIC_SW);
    (void)snprintf(s_hw, sizeof(s_hw), TR(ABOUT_LIC_HARDWARE), AB_LIC_HW);
    (void)snprintf(s_repo, sizeof(s_repo), TR(ABOUT_REPO), AB_REPO);
}

int ab_segs(ab_seg_t *segs)
{
    format_lines();
    const ab_seg_t col[] = {
        line('H', AB_NAME),
        line('N', s_fw),
        line('N', s_cu),
        line('B', NULL),
        line('N', AB_DESIGN),
        line('N', AB_IMPL),
        line('N', AB_COPYRIGHT),
        line('B', NULL),
        line('H', TR(ABOUT_H_LICENCES)),
        line('N', s_sw),
        line('N', s_hw),
        line('N', s_repo),
        line('B', NULL),
        line('H', TR(ABOUT_H_TRADEMARKS)),
        line('D', TR(ABOUT_TRADEMARKS)),
        line('B', NULL),
        line('H', TR(ABOUT_H_PANEL)),
        run(ABOUT_GEN_PANEL),
        line('H', TR(ABOUT_H_IOMCU)),
        line('D', TR(ABOUT_IOMCU_LINE)),
        line('B', NULL),
        run(ABOUT_GEN_IOMCU),
        line('H', TR(ABOUT_H_REFS)),
        line('D', TR(ABOUT_REFS_LINE)),
        line('B', NULL),
        run(ABOUT_GEN_REFS),
        line('H', TR(ABOUT_H_NOTICES)),
        line('D', TR(ABOUT_NOTICES_LINE)),
        line('B', NULL),
        run(ABOUT_GEN_NOTICES),
    };
    const int n = (int)(sizeof(col) / sizeof(col[0]));
    _Static_assert(sizeof(col) / sizeof(col[0]) <= AB_SEGS,
                   "the column's parts fit AB_SEGS");
    for (int i = 0; i < n; ++i) {
        segs[i] = col[i];
    }
    return n;
}

int ab_rows(void)
{
    ab_seg_t segs[AB_SEGS];
    const int n = ab_segs(segs);
    int rows = 0;
    for (int i = 0; i < n; ++i) {
        rows += segs[i].rows;
    }
    return rows;
}

/** @brief A row's box at screen row @p k: the face's height, centered in
 *         the pitch. */
static gfx_rect_t row_box(int k, const gfx_font_t *f)
{
    const int h = gfx_text_height(f, 1);
    return gfx_rect_make(AB_X, AB_TOP + k * AB_PITCH + (AB_PITCH - h) / 2,
                         AB_W, h);
}

/** @brief The rows on screen. */
static void draw_rows(gfx_canvas_t *c)
{
    ab_seg_t segs[AB_SEGS];
    const int n = ab_segs(segs);
    const gfx_rect_t saved = gfx_clip_get(c);
    (void)gfx_clip_intersect(c, gfx_rect_make(0, AB_TOP, AB_BAR_X, AB_H));
    const int top = ab_s.top;
    int at = 0;
    for (int i = 0; i < n && at < top + ABOUT_ROWS; ++i) {
        const ab_seg_t *s = &segs[i];
        const int end = at + s->rows;
        if (end > top && s->text != NULL && s->style != 'B') {
            const gfx_font_t *f = face_of(s->style);
            (void)ui_text_wrap(c, row_box(at - top, f), AB_PITCH, s->text, f,
                               ink_of(s->style));
        } else if (end > top && s->style == 0) {
            const int from = (top > at) ? top : at;
            const int to = (end < top + ABOUT_ROWS) ? end : top + ABOUT_ROWS;
            for (int r = from; r < to; ++r) {
                const int g = s->first + r - at;
                const char st = about_gen_style[g];
                if (st != 'B') {
                    gfx_text_in(c, row_box(r - top, AB_FONT_ROW),
                                about_gen_text[g], AB_FONT_ROW, ink_of(st),
                                1, GFX_ALIGN_LEFT);
                }
            }
        }
        at = end;
    }
    c->clip = saved;
}

/** @brief The scroll bar, while there is somewhere to scroll to. */
static void draw_bar(gfx_canvas_t *c)
{
    if (ab_max_top() <= 0) {
        return;
    }
    int ty = 0;
    int th = 0;
    ab_thumb(&ty, &th);
    gfx_fill_round_rect(c, AB_BAR_X, AB_TOP, AB_BAR_W, AB_H, 8,
                        UI_PANEL_SUNK);
    gfx_fill_round_rect(c, AB_BAR_X, ty, AB_BAR_W, th, 8,
                        (ab_s.have_press && ab_s.hit == AB_HIT_BAR)
                            ? UI_TEXT : UI_ACCENT);
}

/** @brief BACK, PAGE UP and PAGE DOWN in the footer. */
static void draw_footer(gfx_canvas_t *c)
{
    static const struct {
        about_part_t part;
        int          hit;
        ui_text_id_t label;
    } k[] = {
        { ABOUT_PART_BACK, AB_HIT_BACK, TX_ABOUT_BACK },
        { ABOUT_PART_PAGE_UP, AB_HIT_PAGE_UP, TX_ABOUT_PAGE_UP },
        { ABOUT_PART_PAGE_DN, AB_HIT_PAGE_DN, TX_ABOUT_PAGE_DOWN },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        const bool enabled = (k[i].hit == AB_HIT_PAGE_UP) ? ab_s.top > 0
                             : (k[i].hit == AB_HIT_PAGE_DN)
                                 ? ab_s.top < ab_max_top()
                                 : true;
        ui_button_font(c, about_screen_part(k[i].part), ui_tr(k[i].label),
                       UI_PANEL_HI,
                       ab_s.have_press && ab_s.hit == k[i].hit, enabled,
                       UI_FONT_LABEL);
    }
}

void ab_render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    if ((ab_s.drawn_mask & bit) != 0u) {
        return;
    }
    gfx_clear(c, UI_BG);
    draw_rows(c);
    draw_bar(c);
    draw_footer(c);
    ab_s.drawn_mask |= bit;
}
