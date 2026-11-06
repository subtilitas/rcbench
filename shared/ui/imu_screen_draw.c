/**
 * @file
 * @brief The IMU screen's drawing (imu_screen.h): the controls and the
 *        card, or the found list.  imu_screen_int.h has the geometry.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

/* ui_text.h before ui_theme.h: ui_text.def ends by undefining UI_TEXT. */
#include "ui_text.h"
#include "imu_screen_int.h"
#include "settings.h"
#include "ui_theme.h"

/** @brief The controls and the left column's lines: 10 x 20. */
#define IMS_FONT_LABEL (&gfx_font_10x20)

/**
 * @brief One line of @p t in @p f at (@p x, @p y), @p w wide.
 * @param c   The canvas.
 * @param x   Its left edge.
 * @param y   Its top.
 * @param w   Its width.
 * @param t   The text.
 * @param f   Its face.
 * @param col Its colour.
 */
static void text_in(gfx_canvas_t *c, int x, int y, int w, const char *t,
                    const gfx_font_t *f, gfx_color_t col)
{
    gfx_text_in(c, gfx_rect_make(x, y, w, gfx_text_height(f, 1)), t, f, col,
                1, GFX_ALIGN_LEFT);
}

/**
 * @brief Row @p row as a button, when it stands.
 * @param c     The canvas.
 * @param row   IMS_ROW_*.
 * @param label Its label.
 * @param fill  Its fill when enabled.
 */
static void button(gfx_canvas_t *c, int row, const char *label,
                   gfx_color_t fill)
{
    if (!ims_row_shown(row)) {
        return;
    }
    const bool on = ims_row_enabled(row);
    ui_button_font(c, imu_screen_row_rect(row), label,
                   on ? fill : UI_PANEL_HI, ims.have_press && ims.hit == row,
                   on, IMS_FONT_LABEL);
}

/**
 * @brief The calibration's hold: the fill follows the hold, the flash its
 *        end.
 * @return The fill.
 */
static gfx_color_t hold_fill(void)
{
    if (ims.hold.flash_ms > 0) {
        return ui_hold_flash(UI_OK, ims.hold.flash_ms);
    }
    return ui_hold_fill(UI_PANEL_HI, UI_OK, ims.hold.held_s);
}

/**
 * @brief A left column line, wrapped to 2 lines of the label face.
 * @param c The canvas.
 * @param i IMS_L_CAL or IMS_L_ERR.
 * @param y Its top.
 */
static void left_line(gfx_canvas_t *c, int i, int y)
{
    char buf[96];
    gfx_color_t ink;
    (void)ims_line(i, buf, sizeof(buf), &ink);
    (void)ui_text_wrap(c, gfx_rect_make(IMS_BX, y, IMS_BW,
                                        gfx_text_height(IMS_FONT_LABEL, 1)),
                       IMS_LPITCH, buf, IMS_FONT_LABEL, ink);
}

/** @brief A card line as drawn: its box, face, colour and text. */
typedef struct {
    gfx_rect_t        box;       /**< where it is drawn */
    const gfx_font_t *face;      /**< in which face */
    gfx_color_t       ink;       /**< in which colour */
    char              text[96];  /**< what it says */
} card_line_t;

/**
 * @brief Card line @p i as it reads now: the sensor, the HC-05 and the
 *        state 28px apart, 8px more after the address; 12px down the three
 *        angles in the heading face, 40px apart, the axis the runs read in
 *        the accent; 12px down the rate and the battery, 28px apart.
 * @param i   IMS_L_SENSOR to IMS_L_BATTERY.
 * @param out Takes it.
 */
static void card_line(int i, card_line_t *out)
{
    const int top = IMS_RY + IMS_RPAD;
    int y;
    out->face = UI_FONT_BODY;
    (void)ims_line(i, out->text, sizeof(out->text), &out->ink);
    if (i <= IMS_L_STATE) {
        y = top + i * IMS_PITCH + ((i > IMS_L_ADDR) ? 8 : 0);
    } else if (i <= IMS_L_Z) {
        y = top + 4 * IMS_PITCH + 8 + 12 + (i - IMS_L_X) * IMS_APITCH;
        out->face = UI_FONT_HEAD;
        out->ink = (i - IMS_L_X == settings_get_int(SET_IMU_AXIS))
                       ? UI_ACCENT : UI_TEXT;
    } else {
        y = top + 4 * IMS_PITCH + 8 + 12 + 3 * IMS_APITCH + 12
            + (i - IMS_L_RATE) * IMS_PITCH;
    }
    out->box = gfx_rect_make(IMS_RX + IMS_RPAD, y, IMS_RW - 2 * IMS_RPAD,
                             gfx_text_height(out->face, 1));
}

gfx_rect_t imu_screen_line_rect(int i)
{
    if (i < 0 || i >= IMS_CARD_LINES) {
        return gfx_rect_make(0, 0, 0, 0);
    }
    card_line_t l;
    card_line(i, &l);
    return l.box;
}

/**
 * @brief The card: the sensor, the HC-05, the state, the angles, the rate
 *        and the battery.
 * @param c The canvas.
 */
static void draw_card(gfx_canvas_t *c)
{
    ui_card(c, gfx_rect_make(IMS_RX, IMS_RY, IMS_RW, IMS_RH), UI_PANEL);
    card_line_t l;
    for (int i = 0; i < IMS_CARD_LINES; ++i) {
        card_line(i, &l);
        gfx_text_in(c, l.box, l.text, l.face, l.ink, 1, GFX_ALIGN_LEFT);
    }
}

/**
 * @brief A card line's look as one number: its text and colour.  Its box
 *        and face are the line's own.
 * @param l The line.
 * @return The number.
 */
static uint32_t line_key(const card_line_t *l)
{
    uint32_t h = 2166136261u;
    for (const char *t = l->text; *t != '\0'; ++t) {
        h = (h ^ (uint8_t)*t) * 16777619u;
    }
    return (h ^ (uint32_t)l->ink) * 16777619u;
}

/**
 * @brief Which rows stand and which take a press, and whether the found
 *        list is open, as one number: a reading that changes one of them
 *        (ZERO and CLEAR ZERO follow it) repaints the screen whole.
 * @return The number.
 */
static uint32_t rows_key(void)
{
    uint32_t k = ims.list ? 1u : 0u;
    for (int row = 0; row < IMS_ROW_COUNT; ++row) {
        k = (k << 2) | (ims_row_shown(row) ? 2u : 0u)
            | (ims_row_enabled(row) ? 1u : 0u);
    }
    return k;
}

/**
 * @brief The found list: its heading, the devices on the list's ground,
 *        and its buttons.
 * @param c The canvas.
 */
static void draw_list(gfx_canvas_t *c)
{
    char buf[96];
    gfx_color_t ink;
    (void)ims_line(IMS_L_HEAD, buf, sizeof(buf), &ink);
    text_in(c, IMS_LX, IMS_HEAD_Y, IMS_LW, buf, UI_FONT_BODY, ink);
    const int n = (ims.v.link == IMU_LINK_SCANNING) ? 0 : (int)ims.v.found_n;
    if (n > 0) {
        ui_choice_list(c, gfx_rect_make(IMS_LX - 4, IMS_LY - 4, IMS_LW + 8,
                                        (n - 1) * IMS_LP + IMS_LH + 8));
    }
    for (int i = 0; i < n && i < (int)HC05_FOUND_MAX; ++i) {
        const gfx_rect_t r = imu_screen_choice_rect(i);
        const hc05_dev_t *d = &ims.v.found[i];
        ui_choice_row(c, r, "", false,
                      ims.have_press && ims.hit == IMS_HIT_DEV0 + i);
        /* A name of 31 bytes takes 372px of the body face, 12px in; the
         * address 14 cells of the label face, 12px from the right. */
        gfx_text_in(c, gfx_rect_make(r.x + 12, r.y, r.w - 176, r.h),
                    d->name[0] != '\0' ? d->name : TR(IMU_NO_NAME),
                    UI_FONT_BODY, d->name[0] != '\0' ? UI_TEXT : UI_TEXT_DIM,
                    1, GFX_ALIGN_LEFT);
        gfx_text_in(c, gfx_rect_make(r.x + r.w - 152, r.y, 140, r.h), d->addr,
                    IMS_FONT_LABEL, UI_TEXT_DIM, 1, GFX_ALIGN_RIGHT);
    }
    button(c, IMS_ROW_RESCAN, TR(IMU_RESCAN), UI_PANEL_HI);
    button(c, IMS_ROW_LIST_ANY, TR(IMU_ANY), UI_PANEL_HI);
    button(c, IMS_ROW_CLOSE, TR(IMU_CLOSE), UI_PANEL_HI);
}

/**
 * @brief The whole screen: the controls and the card, or the found list.
 * @param c The canvas.
 */
static void draw_all(gfx_canvas_t *c)
{
    gfx_clear(c, UI_BG);
    if (ims.list) {
        draw_list(c);
    } else {
        button(c, IMS_ROW_SCAN, TR(IMU_SCAN), UI_ACCENT);
        button(c, IMS_ROW_ANY, TR(IMU_ANY), UI_PANEL_HI);
        const bool cal_on = ims_row_enabled(IMS_ROW_CAL);
        ui_button_font(c, imu_screen_hold_rect(), TR(IMU_CAL_HOLD),
                       cal_on ? hold_fill() : UI_PANEL_HI, false, cal_on,
                       IMS_FONT_LABEL);
        button(c, IMS_ROW_ZERO, TR(IMU_ZERO), UI_PANEL_HI);
        button(c, IMS_ROW_UNZERO, TR(IMU_UNZERO), UI_PANEL_HI);
        button(c, IMS_ROW_SETTINGS, TR(IMU_SETTINGS), UI_PANEL_HI);
        left_line(c, IMS_L_CAL, IMS_LINE_Y);
        left_line(c, IMS_L_ERR, IMS_ERR_Y);
        draw_card(c);
    }
}

/**
 * @brief The whole screen drawn into @p r alone, within the clip it has.
 * @param c The canvas.
 * @param r The box.
 */
static void draw_in(gfx_canvas_t *c, gfx_rect_t r)
{
    const gfx_rect_t old = c->clip;
    if (gfx_clip_intersect(c, r)) {
        draw_all(c);
    }
    c->clip = old;
}

/**
 * @brief What moved on its own in buffer @p b since it last drew: after a
 *        reading or a frame of the hold, the card lines that read
 *        differently (RATE's age counts on between readings), and the
 *        hold's button while its fill moves.  Each is the whole screen
 *        drawn into its box, so the pixels are a full repaint's.
 * @param c The canvas.
 * @param b The framebuffer, 0 or 1.
 */
static void draw_parts(gfx_canvas_t *c, int b)
{
    if (!ims.list && (ims.drawn_read[b] != ims.read_rev
                      || ims.drawn_hold[b] != ims.hold_rev)) {
        card_line_t l;
        for (int i = 0; i < IMS_CARD_LINES; ++i) {
            card_line(i, &l);
            const uint32_t k = line_key(&l);
            if (k != ims.drawn_line[b][i]) {
                ims.drawn_line[b][i] = k;
                draw_in(c, l.box);
            }
        }
    }
    if (!ims.list && ims.drawn_hold[b] != ims.hold_rev) {
        draw_in(c, imu_screen_hold_rect());
    }
    ims.drawn_read[b] = ims.read_rev;
    ims.drawn_hold[b] = ims.hold_rev;
}

void ims_render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int b = buffer_index & 1;
    const uint32_t rows = rows_key();
    /* Every event and every change but the reading's and the hold's clears
     * the mask; a reading that changes the rows repaints whole too. */
    if ((ims.drawn_mask & bit) != 0u && ims.drawn_rows[b] == rows) {
        draw_parts(c, b);
        return;
    }
    draw_all(c);
    ims.drawn_mask |= bit;
    ims.drawn_rows[b] = rows;
    ims.drawn_read[b] = ims.read_rev;
    ims.drawn_hold[b] = ims.hold_rev;
    card_line_t l;
    for (int i = 0; i < IMS_CARD_LINES; ++i) {
        card_line(i, &l);
        ims.drawn_line[b][i] = line_key(&l);
    }
}
