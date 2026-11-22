/**
 * @file
 * @brief LOAD SETTINGS drawn: the files or a load's answer on the left, the
 *        chosen file and the hold on the right.  See sbk_int.h for the
 *        geometry.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "sbk_ctl.h"
/* ui_text.h before ui_theme.h: ui_text.def ends by undefining UI_TEXT. */
#include "ui_text.h"
#include "ui_theme.h"
#include "sbk_int.h"

/** @brief The head line's, a file's name's and the date's face: 12 x 24. */
#define F_BODY  UI_FONT_BODY
/** @brief The chosen file's name's face: 16 x 28. */
#define F_HEAD  UI_FONT_HEAD
/** @brief Every other text's face: 10 x 20. */
#define F_LABEL (&gfx_font_10x20)

/** @brief A refused line's reason, by sf_line_err_t. */
static const ui_text_id_t k_reason[] = {
    [SF_LINE_MALFORMED] = TX_SBK_LINE_MALFORMED,
    [SF_LINE_KEY]       = TX_SBK_LINE_KEY,
    [SF_LINE_TWICE]     = TX_SBK_LINE_TWICE,
    [SF_LINE_VALUE]     = TX_SBK_LINE_VALUE,
    [SF_LINE_RANGE]     = TX_SBK_LINE_RANGE,
    [SF_LINE_LONG]      = TX_SBK_LINE_LONG,
    [SF_LINE_DERIVED]   = TX_SBK_LINE_DERIVED,
    [SF_LINE_HELD]      = TX_SBK_LINE_HELD,
};

/** @brief One line of text, left aligned, cut to @p w. */
static void line(gfx_canvas_t *c, int x, int y, int w, const char *s,
                 const gfx_font_t *f, gfx_color_t ink)
{
    gfx_text_in(c, gfx_rect_make(x, y, w, gfx_text_height(f, 1)), s, f, ink,
                1, GFX_ALIGN_LEFT);
}

/** @brief A file's second line: its date and version, or why it is no
 *         settings file. */
static void file_detail(const sb_entry_t *e, char *out, size_t n)
{
    if (e->err != SF_OK) {
        (void)snprintf(out, n, TR(SBK_FILE_BAD), (unsigned)e->err);
        return;
    }
    (void)snprintf(out, n, TR(SBK_FILE_LINE),
                   (e->head.written[0] != '\0') ? e->head.written
                                                : TR(SBK_DATE_UNKNOWN),
                   (e->head.version[0] != '\0') ? e->head.version
                                                : TR(SBK_VERSION_UNKNOWN));
}

/** @brief The head line over the files: what the list is, or why there is
 *         none. */
static void head_text(char *out, size_t n, uint8_t count)
{
    if (sbk_list_state() != SBK_LIST_READY) {
        (void)snprintf(out, n, "%s", TR(SBK_HEAD_READING));
    } else if (sbk_list_code() == SB_NO_CARD) {
        (void)snprintf(out, n, "%s", TR(SBK_HEAD_NO_CARD));
    } else if (sbk_list_code() != SB_OK) {
        (void)snprintf(out, n, TR(SBK_HEAD_FAILED),
                       (unsigned)sbk_list_code());
    } else if (count == 0u) {
        (void)snprintf(out, n, "%s", TR(SBK_HEAD_NONE));
    } else {
        (void)snprintf(out, n, "%s",
                       sbk_v.offer ? TR(SBK_HEAD_OFFER) : TR(SBK_HEAD_LIST));
    }
}

/** @brief The files, each a card: its name, its date and version. */
static void draw_files(gfx_canvas_t *c)
{
    uint8_t n = 0u;
    const sb_entry_t *f = sbk_files(&n);
    char buf[96];
    head_text(buf, sizeof(buf), n);
    line(c, SBK_LX, SBK_HEAD_Y, SBK_LW, buf, F_BODY, UI_TEXT);
    for (int i = 0; sbk_list_state() == SBK_LIST_READY && i < (int)n
                    && i < SBK_ROWS; ++i) {
        const gfx_rect_t r = sbk_screen_row(i);
        const bool chosen = i == sbk_v.chosen;
        const bool down = sbk_v.have_press && sbk_v.hit == SBK_HIT_ROW0 + i;
        gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0,
                                 (chosen || down) ? UI_PANEL_HI : UI_PANEL);
        gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0,
                                 chosen ? UI_ACCENT : UI_EDGE);
        line(c, r.x + 16, r.y + 6, r.w - 32, f[i].name, F_BODY,
             chosen ? UI_ACCENT : UI_TEXT);
        file_detail(&f[i], buf, sizeof(buf));
        line(c, r.x + 16, r.y + 36, r.w - 32, buf, F_LABEL,
             (f[i].err == SF_OK) ? UI_TEXT_DIM : UI_WARN);
    }
    const gfx_rect_t first = gfx_rect_make(SBK_LX, SBK_NOTE_Y, SBK_LW,
                                           gfx_text_height(F_LABEL, 1));
    const int rows = ui_text_wrap(c, first, SBK_NOTE_P, TR(SBK_NOTE_REPLACE),
                                  F_LABEL, UI_TEXT_DIM);
    (void)ui_text_wrap(c, gfx_rect_make(first.x, first.y + rows * SBK_NOTE_P
                                                 + SBK_NOTE_P / 2,
                                        first.w, first.h),
                       SBK_NOTE_P, TR(SBK_NOTE_WIFI), F_LABEL, UI_TEXT_DIM);
}

/** @brief A refused line as the answer lists it. */
static void refusal_text(const sf_refusal_t *r, char *out, size_t n)
{
    const size_t kinds = sizeof(k_reason) / sizeof(k_reason[0]);
    const char *why = (r->code > 0u && r->code < kinds)
                          ? ui_tr(k_reason[r->code]) : "";
    if (r->line > 0u) {
        (void)snprintf(out, n, TR(SBK_REFUSED_LINE), (unsigned)r->line,
                       r->key, (unsigned)r->code, why);
    } else {
        (void)snprintf(out, n, TR(SBK_REFUSED_NOLINE), r->key,
                       (unsigned)r->code, why);
    }
}

/** @brief A load's answer in the list's place. */
static void draw_answer(gfx_canvas_t *c)
{
    char buf[112];
    const char *name = sbk_load_name();
    uint8_t code = 0u;
    unsigned at = 0u;
    switch (sbk_load_state()) {
    case SBK_LOAD_READING:
        (void)snprintf(buf, sizeof(buf), TR(SBK_RESULT_READING), name);
        line(c, SBK_LX, SBK_HEAD_Y, SBK_LW, buf, F_BODY, UI_TEXT);
        return;
    case SBK_LOAD_FAILED:
        switch (sbk_load_why(&code, &at)) {
        case SBK_WHY_FILE:
            (void)snprintf(buf, sizeof(buf), TR(SBK_RESULT_FILE), name,
                           (unsigned)code, at);
            break;
        case SBK_WHY_ARMED:
            (void)snprintf(buf, sizeof(buf), TR(SBK_RESULT_ARMED), name);
            break;
        case SBK_WHY_CARD:
        case SBK_WHY_CHANGED:      /* not FAILED: the list again */
        case SBK_WHY_NONE:
        default:
            (void)snprintf(buf, sizeof(buf), TR(SBK_RESULT_CARD), name,
                           (unsigned)code);
            break;
        }
        line(c, SBK_LX, SBK_HEAD_Y, SBK_LW, buf, F_BODY, UI_WARN);
        return;
    case SBK_LOAD_DONE:
    case SBK_LOAD_IDLE:
    default:
        break;
    }
    int changed = 0;
    const sf_load_t *l = sbk_load_result(&changed);
    (void)snprintf(buf, sizeof(buf), TR(SBK_RESULT_DONE), name, changed,
                   (unsigned)l->refused_n);
    line(c, SBK_LX, SBK_HEAD_Y, SBK_LW, buf, F_BODY,
         (l->refused_n > 0u) ? UI_WARN : UI_OK);
    int y = SBK_ROW_Y;
    line(c, SBK_LX, y, SBK_LW, TR(SBK_RESULT_KEPT), F_LABEL, UI_TEXT_DIM);
    y += SBK_RES_P;
    if (l->refused_n == 0u) {
        line(c, SBK_LX, y, SBK_LW, TR(SBK_RESULT_NONE), F_LABEL, UI_TEXT);
        return;
    }
    const unsigned listed = (l->refused_n < SF_REFUSED_MAX)
                                ? l->refused_n : SF_REFUSED_MAX;
    const unsigned room = SBK_RES_LINES - 2u;
    for (unsigned i = 0u; i < listed && i < room; ++i) {
        refusal_text(&l->refused[i], buf, sizeof(buf));
        line(c, SBK_LX, y, SBK_LW, buf, F_LABEL, UI_TEXT);
        y += SBK_RES_P;
    }
    const unsigned shown = (listed < room) ? listed : room;
    if (l->refused_n > shown) {
        (void)snprintf(buf, sizeof(buf), TR(SBK_REFUSED_MORE),
                       (unsigned)(l->refused_n - shown));
        line(c, SBK_LX, y, SBK_LW, buf, F_LABEL, UI_TEXT_DIM);
    }
}

/** @brief The right column: the chosen file, the hold and BACK. */
static void draw_side(gfx_canvas_t *c)
{
    uint8_t n = 0u;
    const sb_entry_t *f = sbk_files(&n);
    const bool listing = sbk_load_state() == SBK_LOAD_IDLE;
    const int at = sbk_v.chosen;
    if (listing && at >= 0 && at < (int)n) {
        const sb_entry_t *e = &f[at];
        line(c, SBK_SX, SBK_NAME_Y, SBK_SW, e->name, F_HEAD, UI_ACCENT);
        line(c, SBK_SX, SBK_WRIT_Y, SBK_SW, TR(SBK_WRITTEN), F_LABEL,
             UI_TEXT_DIM);
        line(c, SBK_SX, SBK_DATE_Y, SBK_SW,
             (e->head.written[0] != '\0') ? e->head.written
                                          : TR(SBK_DATE_UNKNOWN),
             F_BODY, UI_TEXT);
        line(c, SBK_SX, SBK_PANEL_Y, SBK_SW, TR(SBK_PANEL), F_LABEL,
             UI_TEXT_DIM);
        line(c, SBK_SX, SBK_VER_Y, SBK_SW,
             (e->head.version[0] != '\0') ? e->head.version
                                          : TR(SBK_VERSION_UNKNOWN),
             F_BODY, UI_TEXT);
    }
    const int why = sbk_hold_why();
    if (listing && why != TX_COUNT) {
        (void)ui_text_wrap(c, gfx_rect_make(SBK_SX, SBK_WHY_Y, SBK_SW,
                                            gfx_text_height(F_LABEL, 1)),
                           SBK_NOTE_P, ui_tr((ui_text_id_t)why), F_LABEL,
                           UI_WARN);
    }
    ui_hold_button(c, sbk_screen_part(SBK_PART_HOLD), TR(SBK_HOLD),
                   UI_PANEL_HI, UI_OK, &sbk_v.hold,
                   sbk_v.have_press && sbk_v.hit == SBK_HIT_HOLD,
                   why == TX_COUNT, UI_FONT_LABEL);
    const bool skip = sbk_v.offer && sbk_load_state() == SBK_LOAD_IDLE;
    ui_button_font(c, sbk_screen_part(SBK_PART_BACK),
                   skip ? TR(SBK_SKIP) : TR(SBK_BACK), UI_PANEL_HI,
                   sbk_v.have_press && sbk_v.hit == SBK_HIT_BACK, true,
                   UI_FONT_LABEL);
}

void sbk_render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    if ((sbk_v.drawn_mask & bit) != 0u) {
        return;
    }
    gfx_clear(c, UI_BG);
    if (sbk_load_state() == SBK_LOAD_IDLE) {
        draw_files(c);
    } else {
        draw_answer(c);
    }
    draw_side(c);
    if (sbk_v.hold.flash_ms > 0) {
        sbk_v.drawn_mask = 0u;
    } else {
        sbk_v.drawn_mask |= bit;
    }
}
