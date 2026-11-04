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

/**
 * @brief The card: the sensor, the HC-05, the state, the angles, the rate
 *        and the battery.
 * @param c The canvas.
 */
static void draw_card(gfx_canvas_t *c)
{
    ui_card(c, gfx_rect_make(IMS_RX, IMS_RY, IMS_RW, IMS_RH), UI_PANEL);
    const int x = IMS_RX + IMS_RPAD, w = IMS_RW - 2 * IMS_RPAD;
    int y = IMS_RY + IMS_RPAD;
    char buf[96];
    gfx_color_t ink;
    for (int i = IMS_L_SENSOR; i <= IMS_L_STATE; ++i) {
        (void)ims_line(i, buf, sizeof(buf), &ink);
        text_in(c, x, y, w, buf, UI_FONT_BODY, ink);
        y += IMS_PITCH + ((i == IMS_L_ADDR) ? 8 : 0);
    }
    y += 12;
    const int axis = settings_get_int(SET_IMU_AXIS);
    for (int i = IMS_L_X; i <= IMS_L_Z; ++i) {
        (void)ims_line(i, buf, sizeof(buf), &ink);
        /* The axis the runs read, in the accent. */
        text_in(c, x, y, w, buf, UI_FONT_HEAD,
                (i - IMS_L_X == axis) ? UI_ACCENT : UI_TEXT);
        y += IMS_APITCH;
    }
    y += 12;
    for (int i = IMS_L_RATE; i <= IMS_L_BATTERY; ++i) {
        (void)ims_line(i, buf, sizeof(buf), &ink);
        text_in(c, x, y, w, buf, UI_FONT_BODY, ink);
        y += IMS_PITCH;
    }
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

void ims_render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    if ((ims.drawn_mask & bit) != 0u) {
        return;            /* every event and every change clears the mask */
    }
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
    if (ims.hold.flash_ms > 0) {
        ims.drawn_mask = 0u;
    } else {
        ims.drawn_mask |= bit;
    }
}
