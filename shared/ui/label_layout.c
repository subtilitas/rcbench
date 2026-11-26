/**
 * @file
 * @brief The servo label's layout (label_layout.h): the QR code from
 *        qrcodegen, its module size and place for the roll, and the text
 *        lines beside or under it.
 *
 * SPDX-License-Identifier: MIT
 */

#include "label_layout.h"

#include <stdio.h>
#include <string.h>

#include "qrcodegen.h"

_Static_assert(LABEL_QR_BUF
                   == qrcodegen_BUFFER_LEN_FOR_VERSION(LABEL_QR_VERSION_MAX),
               "LABEL_QR_BUF holds a matrix of the highest version");

/** @brief Faces of line 1 and of a joined line, largest first. */
static const gfx_font_t *const k_head[] = {
    &gfx_font_16x28, &gfx_font_12x24, &gfx_font_10x20, &gfx_font_8x16,
    &gfx_font_6x12,
};
/** @brief Faces of line 2, the serial, largest first. */
static const gfx_font_t *const k_big[] = {
    &gfx_font_20x36, &gfx_font_16x28, &gfx_font_12x24, &gfx_font_10x20,
    &gfx_font_8x16, &gfx_font_6x12,
};
/** @brief The smallest face: the one a cut line falls back to. */
#define FACE_MIN (&gfx_font_6x12)

/** @brief Text of a servo label's lines 1 and 2 and the code. */
typedef struct {
    const char *qr;    /**< the code's text                               */
    const char *head;  /**< line 1                                        */
    const char *big;   /**< line 2                                        */
    const char *dut;   /**< lines 3 and 4                                 */
    bool dut_first;    /**< DUT NAME alone where it and the id do not fit  */
} label_parts_t;

/** @brief The text area: x from @c x to @c x_end, y from @c y to @c y_end. */
typedef struct {
    int x;      /**< left edge                                         */
    int x_end;  /**< first x past the column                           */
    int y;      /**< top edge                                          */
    int y_end;  /**< first y past the room                             */
} label_room_t;

/**
 * @brief The smaller of two ints.
 * @param a  one
 * @param b  the other
 * @return the smaller
 */
static int imin(int a, int b)
{
    return a < b ? a : b;
}

/**
 * @brief Copy the first @p cells cells of @p s into a line, cut on a code
 *        point so it fits LABEL_LINE_MAX, trailing spaces dropped.
 * @param dst    the line's text
 * @param s      the source
 * @param cells  the cells to copy
 */
static void copy_cells(char *dst, const char *s, int cells)
{
    size_t len = gfx_text_prefix(s, cells);
    while (len > LABEL_LINE_MAX - 1u && cells > 0) {
        cells--;
        len = gfx_text_prefix(s, cells);
    }
    /* gfx_text_prefix() stops at the NUL; the bound states it here. */
    const size_t n = strlen(s);
    if (len > n) {
        len = n;
    }
    while (len > 0u && s[len - 1u] == ' ') {
        len--;
    }
    memcpy(dst, s, len);
    dst[len] = '\0';
}

/**
 * @brief Set @p line to @p s in the first face of @p faces that holds it in
 *        @p width dots and @p height dots; else in 6x12, cut to the width.
 * @param line    the line
 * @param faces   the faces, largest first
 * @param nfaces  how many
 * @param s       the text
 * @param width   the column, dots
 * @param height  the room, dots
 */
static void set_fit(label_line_t *line, const gfx_font_t *const *faces,
                    size_t nfaces, const char *s, int width, int height)
{
    const int cells = gfx_text_cells(s);
    line->face = NULL;
    for (size_t i = 0u; i < nfaces; i++) {
        if (cells * (int)faces[i]->width <= width
            && (int)faces[i]->height <= height
            && label_face_draws(faces[i], s)) {
            line->face = faces[i];
            break;
        }
    }
    if (line->face == NULL && (int)FACE_MIN->height <= height
        && width >= (int)FACE_MIN->width) {
        line->face = FACE_MIN;
    }
    if (line->face == NULL) {
        return;
    }
    copy_cells(line->text, s, imin(cells, width / (int)line->face->width));
    line->w = (uint16_t)(gfx_text_cells(line->text) * line->face->width);
    line->h = line->face->height;
}

/**
 * @brief Where the line that starts at @p s ends when lines hold @p cpl
 *        cells: at the last space within reach, else at the column's end.
 * @param s    the line's start, no leading space
 * @param cpl  cells a line
 * @return bytes of the line, trailing space included
 */
static size_t line_end(const char *s, int cpl)
{
    if (gfx_text_cells(s) <= cpl) {
        return strlen(s);
    }
    const size_t fit = gfx_text_prefix(s, cpl);
    if (s[fit] == ' ') {
        return fit;
    }
    for (size_t i = fit; i > 0u; i--) {
        if (s[i - 1u] == ' ') {
            return i - 1u;
        }
    }
    return fit;
}

/**
 * @brief Skip spaces.
 * @param s  the text
 * @return the first byte that is not a space
 */
static const char *skip_spaces(const char *s)
{
    while (*s == ' ') {
        s++;
    }
    return s;
}

/**
 * @brief The lines @p s needs at @p cpl cells a line, broken at spaces.
 * @param s    the text, no leading space
 * @param cpl  cells a line, 1 or more
 * @return the line count
 */
static int lines_needed(const char *s, int cpl)
{
    int n = 0;
    while (*s != '\0') {
        const size_t end = line_end(s, cpl);
        s = skip_spaces(s + (end > 0u ? end : 1u));
        n++;
    }
    return n;
}

/**
 * @brief Set lines 3 and 4 to DUT NAME in @p face: the first broken at a
 *        space or at the column's end, the second the rest, cut.
 * @param l3     line 3
 * @param l4     line 4
 * @param s      DUT NAME, no leading space
 * @param face   the face
 * @param width  the column, dots
 * @param lines  lines to fill, 1 or 2
 */
static void put_dut(label_line_t *l3, label_line_t *l4, const char *s,
                    const gfx_font_t *face, int width, int lines)
{
    const int cpl = width / (int)face->width;
    if (*s == '\0' || cpl < 1) {
        return;
    }
    const size_t end = line_end(s, cpl);
    label_line_t *const out[2] = { l3, l4 };
    const char *part[2] = { s, skip_spaces(s + end) };
    for (int i = 0; i < lines && i < 2; i++) {
        const int cells = (i == 0) ? gfx_text_cells(s) - gfx_text_cells(s + end)
                                   : gfx_text_cells(part[1]);
        copy_cells(out[i]->text, part[i], imin(cells, cpl));
        if (out[i]->text[0] != '\0') {
            out[i]->face = face;
            out[i]->w = (uint16_t)(gfx_text_cells(out[i]->text) * face->width);
            out[i]->h = face->height;
        }
    }
}

/**
 * @brief DUT NAME in two lines at most: 8x16 where it needs two or fewer,
 *        else 6x12; the first broken at a space, the second the rest, cut.
 * @param l3     line 3
 * @param l4     line 4
 * @param dut    DUT NAME
 * @param width  the column, dots
 */
static void set_dut(label_line_t *l3, label_line_t *l4, const char *dut,
                    int width)
{
    const char *s = skip_spaces(dut);
    const gfx_font_t *face = &gfx_font_8x16;
    if (lines_needed(s, width / (int)face->width) > 2) {
        face = FACE_MIN;
    }
    put_dut(l3, l4, s, face, width, 2);
}

/**
 * @brief DUT NAME without the id: the largest of 16x28, 12x24, 10x20, 8x16
 *        and 6x12 that holds it in one or two lines of @p height and draws
 *        each of its characters; else 6x12 in the lines that fit, cut.
 * @param l3      line 3
 * @param l4      line 4
 * @param dut     DUT NAME
 * @param width   the column, dots
 * @param height  the room, dots
 */
static void set_dut_alone(label_line_t *l3, label_line_t *l4,
                          const char *dut, int width, int height)
{
    const char *s = skip_spaces(dut);
    const gfx_font_t *face = FACE_MIN;
    for (size_t i = 0u; i < sizeof k_head / sizeof k_head[0]; i++) {
        const int cpl = width / (int)k_head[i]->width;
        if (cpl < 1) {
            continue;
        }
        const int n = lines_needed(s, cpl);
        if (n <= 2 && n * ((int)k_head[i]->height + (int)LABEL_GAP)
                          - (int)LABEL_GAP <= height
            && label_face_draws(k_head[i], s)) {
            face = k_head[i];
            break;
        }
    }
    const int lines = (height + (int)LABEL_GAP)
                      / ((int)face->height + (int)LABEL_GAP);
    if (lines > 0) {
        put_dut(l3, l4, s, face, width, lines);
    }
}

/**
 * @brief Height of the drawn lines with their gaps.
 * @param info  the layout
 * @return dots
 */
static int lines_height(const label_info_t *info)
{
    int h = 0;
    int n = 0;
    for (unsigned i = 0u; i < LABEL_LINES; i++) {
        if (info->line[i].face != NULL) {
            h += info->line[i].h;
            n++;
        }
    }
    return n > 0 ? h + (n - 1) * (int)LABEL_GAP : 0;
}

/**
 * @brief Make the lines fit @p room's height: DUT NAME's lines go from the
 *        bottom, then lines 1 and 2 become one, then that one goes.
 * @param info   the layout
 * @param p      the texts
 * @param room   the text area
 */
static void shrink_lines(label_info_t *info, const label_parts_t *p,
                         const label_room_t *room)
{
    const int width  = room->x_end - room->x;
    const int height = room->y_end - room->y;
    label_line_t *const l = info->line;
    while (lines_height(info) > height) {
        if (l[3].face != NULL) {
            l[3].face = NULL;
        } else if (l[2].face != NULL) {
            l[2].face = NULL;
        } else if (l[0].face != NULL && l[1].face != NULL) {
            char both[2u * LABEL_LINE_MAX];
            const size_t n0 = strlen(p->head);
            const size_t n1 = strlen(p->big);
            if (n0 + n1 >= sizeof both) {
                l[0].face = NULL;
                continue;
            }
            memcpy(both, p->head, n0);
            memcpy(both + n0, p->big, n1 + 1u);
            set_fit(&l[0], k_head, sizeof k_head / sizeof k_head[0], both,
                    width, height);
            l[1].face = NULL;
        } else {
            for (unsigned i = 0u; i < LABEL_LINES; i++) {
                l[i].face = NULL;
            }
        }
    }
}

/**
 * @brief Lay the lines out in @p room and draw them.
 * @param out   the bitmap
 * @param info  the layout
 * @param p     the texts
 * @param room  the text area
 */
static void draw_lines(label_bmp_t *out, label_info_t *info,
                       const label_parts_t *p, const label_room_t *room)
{
    const int width  = room->x_end - room->x;
    const int height = room->y_end - room->y;
    label_line_t *const l = info->line;
    if (p->head[0] != '\0') {
        set_fit(&l[0], k_head, sizeof k_head / sizeof k_head[0], p->head,
                width, height);
    }
    if (p->big[0] != '\0') {
        set_fit(&l[1], k_big, sizeof k_big / sizeof k_big[0], p->big,
                width, height);
    }
    if (p->dut != NULL && p->dut[0] != '\0') {
        set_dut(&l[2], &l[3], p->dut, width);
    }
    const bool dut_set = l[2].face != NULL;
    const bool dut_two = l[3].face != NULL;
    shrink_lines(info, p, room);
    /* A DUT NAME line the height dropped: the label cannot hold both, and
     * DUT NAME takes the room alone; the code still carries the id. */
    if (p->dut_first && dut_set
        && (l[2].face == NULL || (dut_two && l[3].face == NULL))) {
        label_line_t alone[2];
        memset(alone, 0, sizeof alone);
        set_dut_alone(&alone[0], &alone[1], p->dut, width, height);
        if (alone[0].face != NULL) {
            memset(l, 0, LABEL_LINES * sizeof l[0]);
            l[2] = alone[0];
            l[3] = alone[1];
        }
    }
    /* Side by side the block is centered beside the code; stacked it
     * starts right under the code, as long rolls leave room below. */
    int y = info->stacked ? room->y
                          : room->y + (height - lines_height(info)) / 2;
    for (unsigned i = 0u; i < LABEL_LINES; i++) {
        if (l[i].face == NULL) {
            continue;
        }
        l[i].x = (int16_t)room->x;
        l[i].y = (int16_t)y;
        (void)label_text(out, room->x, y, l[i].face, l[i].text, room->x_end);
        y += l[i].h + (int)LABEL_GAP;
    }
}

/**
 * @brief Encode, place and draw the code and the lines.
 * @param roll  the roll
 * @param p     the texts
 * @param out   the bitmap
 * @param info  out: the layout
 * @return as label_layout()
 */
static label_err_t draw_label(const label_roll_t *roll,
                              const label_parts_t *p, label_bmp_t *out,
                              label_info_t *info)
{
    memset(info, 0, sizeof *info);
    const label_err_t err = label_bmp_init(out, roll);
    if (err != LABEL_OK) {
        return err;
    }
    if (p->qr[0] == '\0'
        || !qrcodegen_encodeText(p->qr, info->work, info->qr,
                                 qrcodegen_Ecc_MEDIUM, 1,
                                 (int)LABEL_QR_VERSION_MAX,
                                 qrcodegen_Mask_AUTO, true)) {
        return LABEL_QR;
    }
    const int n  = qrcodegen_getSize(info->qr);
    const int su = n + 2 * (int)LABEL_QUIET;
    const int w  = out->w;
    const int h  = out->cols;
    info->modules = (uint8_t)n;
    info->version = (uint8_t)((n - 17) / 4);

    label_room_t room;
    int m = imin((int)LABEL_MODULE_MAX, imin((h - 8) / su, 55 * w / 100 / su));
    if (w >= h + 64 && m >= (int)LABEL_MODULE_MIN
        && w - m * su - 16 >= (int)LABEL_SIDE_MIN_COL) {
        info->qr_x = 0;
        info->qr_y = (int16_t)((h - m * su) / 2);
        room = (label_room_t){ m * su + 8, w - 8, 4, h - 4 };
    } else {
        m = imin((int)LABEL_MODULE_MAX, imin(w / su, (h - 28) / su));
        if (m < (int)LABEL_MODULE_MIN) {
            return LABEL_TOO_SMALL;
        }
        info->stacked = true;
        info->qr_x = (int16_t)((w - m * su) / 2);
        info->qr_y = 4;
        room = (label_room_t){ 8, w - 8, 4 + m * su + (int)LABEL_GAP, h - 4 };
    }
    info->module  = (uint8_t)m;
    info->qr_side = (uint16_t)(m * su);

    const int x0 = info->qr_x + (int)LABEL_QUIET * m;
    const int y0 = info->qr_y + (int)LABEL_QUIET * m;
    for (int my = 0; my < n; my++) {
        for (int mx = 0; mx < n; mx++) {
            if (qrcodegen_getModule(info->qr, mx, my)) {
                label_fill(out, x0 + mx * m, y0 + my * m, m, m);
            }
        }
    }
    draw_lines(out, info, p, &room);
    return LABEL_OK;
}

label_err_t label_layout(const label_roll_t *roll, const label_text_t *t,
                         label_bmp_t *out, label_info_t *info)
{
    char head[LABEL_LINE_MAX];
    const char *id = (t != NULL && t->id != NULL) ? t->id : "";
    const char *dash = strrchr(id, '-');
    size_t n = (dash != NULL) ? (size_t)(dash - id) + 1u : 0u;
    if (n >= sizeof head) {
        n = sizeof head - 1u;
    }
    memcpy(head, id, n);
    head[n] = '\0';
    const label_parts_t p = {
        .qr   = id,
        .head = head,
        .big  = id + ((dash != NULL) ? (size_t)(dash - id) + 1u : 0u),
        .dut  = (t != NULL && t->dut != NULL) ? t->dut : "",
        .dut_first = true,
    };
    return draw_label(roll, &p, out, info);
}

label_err_t label_layout_test(const label_roll_t *roll, const char *version,
                              label_bmp_t *out, label_info_t *info)
{
    char size[16] = "";
    if (roll != NULL) {
        (void)snprintf(size, sizeof size, "%ux%umm", (unsigned)roll->w_mm,
                       (unsigned)roll->h_mm);
    }
    const label_parts_t p = {
        .qr   = "RCBENCH TEST",
        .head = "RCBENCH",
        .big  = (version != NULL) ? version : "",
        .dut  = size,
        .dut_first = false,
    };
    return draw_label(roll, &p, out, info);
}

bool label_qr_module(const label_info_t *info, int mx, int my)
{
    if (info->modules == 0u || mx < 0 || my < 0 || mx >= info->modules
        || my >= info->modules) {
        return false;
    }
    return qrcodegen_getModule(info->qr, mx, my);
}
