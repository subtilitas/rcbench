/**
 * @file
 * @brief What feeds the servo, drawn the same on every SERVO function
 *        screen: its reading (svs_feed_now()) and the plot with its V, A
 *        and W legend (svs_power_view()) in the place the screen shown
 *        gives it (svs_power_rect()).  On the rbmod carrier's programming
 *        port it is the PROG 5V's, never the PD mini's.  The rail line
 *        under the READY rows reads the same feed (servo_ready.c).
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "link_pages.h"
#include "prog5v_box.h"
#include "sensors_rows.h"
#include "supply_screen.h"
#include "ui_text.h"
#include "ui_theme.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_core_int.h"

void svs_feed_now(svs_feed_t *f)
{
    if (f == NULL) {
        return;
    }
    memset(f, 0, sizeof(*f));
    if (lim_fixed_supply()) {
        /* The rbmod carrier's programming port: the servo runs from the
         * PROG 5V, whatever meter the rail has.  Its channel's reading, or
         * the word that there is none: never the PD mini's. */
        uint16_t mv = 0u;
        int16_t ma = 0;
        const bool ok = prog5v_box_last(&mv, &ma, NULL);
        f->r.v_ok = ok;
        f->r.i_ok = ok;
        f->r.v = (float)mv / 1000.0f;
        f->r.i = (float)ma / 1000.0f;
        f->r.p = f->r.v * f->r.i;
        /* Its meter: `INA PROG 5V` for the carrier's channel named by its
         * rail, `PROG 5V 0x40 CH3` for one named by its address; `PROG 5V`
         * and the word alone without a reading, which the line's width
         * takes beside `--V  --A`. */
        const int ch = sensors_rows_watcher((uint16_t)LINK_SM_WATCH_PROG_5V);
        char name[16];
        (void)sensors_rows_channel(ch, name, sizeof(name));
        if (!ok || name[0] == '\0') {
            snprintf(f->name, sizeof(f->name), "PROG 5V");
        } else if (strstr(name, "5V") != NULL) {
            snprintf(f->name, sizeof(f->name), "INA %s", name);
        } else {
            snprintf(f->name, sizeof(f->name), "PROG 5V %s", name);
        }
        f->word = ok ? NULL : TR(SV_P5_NO_READ);
        f->plot = &s.p5_power;
        return;
    }
    f->r = svs_rail_now();
    const bool ina = (s.source == SERVO_SOURCE_INA3221);
    if (ina) {
        (void)sensors_rows_short(f->name, sizeof(f->name));
    } else {
        snprintf(f->name, sizeof(f->name), "PD MINI");
    }
    /* The panel's model is no measurement: its word after the name. */
    f->word = (!ina && supply_screen_model()) ? TR(SV_MODELLED) : NULL;
    f->plot = &s.power;
}

void svs_feed_values(const svs_feed_t *f, char t[3][16])
{
    if (f->r.v_ok) {
        snprintf(t[0], 16, "%.2fV", (double)f->r.v);
    } else {
        snprintf(t[0], 16, "--V");
    }
    if (f->r.i_ok) {
        snprintf(t[1], 16, "%.3fA", (double)f->r.i);
    } else {
        snprintf(t[1], 16, "--A");
    }
    if (f->r.v_ok && f->r.i_ok) {
        snprintf(t[2], 16, "%.2fW", (double)f->r.p);
    } else {
        snprintf(t[2], 16, "--W");
    }
}


void svs_power_view(gfx_canvas_t *c, gfx_rect_t r)
{
    if (c == NULL || r.w <= 0 || r.h <= 0) {
        return;
    }
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    svs_feed_t f;
    svs_feed_now(&f);
    char t[3][16];
    svs_feed_values(&f, t);
    static const ui_color_id_t k_col[3] = { UI_C_VOLT, UI_C_CURR,
                                            UI_C_POWER };
    /* The legend: each value in its trace's colour, 2 cells apart, 1 where
     * 2 do not fit; the meter's name before them where the width takes
     * both.  Under SVS_POWER_FULL_H the 8 x 16 face, else the label face. */
    const bool small = r.h < SVS_POWER_FULL_H;
    const gfx_font_t *face = small ? &gfx_font_8x16 : SV_FONT_LABEL;
    const int cell = gfx_text_width(face, " ", 1);
    int vals = 0;
    for (int k = 0; k < 3; ++k) {
        vals += gfx_text_width(face, t[k], 1);
    }
    const int gap = (vals + 4 * cell <= r.w) ? 2 * cell : cell;
    const int w = vals + 2 * gap;
    const int name_w = gfx_text_width(face, f.name, 1);
    int x = r.x + r.w - w;
    const gfx_rect_t old = c->clip;
    if (gfx_clip_intersect(c, r)) {
        if (name_w + 2 * cell + w <= r.w) {
            gfx_text(c, r.x, r.y, f.name, face,
                     ui_theme_color(UI_C_TEXT_DIM), 1);
        } else {
            x = r.x;            /* the line under the card names it */
        }
        for (int k = 0; k < 3; ++k) {
            x += gfx_text(c, x, r.y, t[k], face, ui_theme_color(k_col[k]), 1)
                 + gap;
        }
    }
    c->clip = old;
    const int top = face->height + (small ? 2 : 4);
    ui_plot_t *p = f.plot;
    /* Each trace in the colour the legend names it by. */
    p->series[PS_V].color = ui_theme_color(UI_C_VOLT);
    p->series[PS_A].color = ui_theme_color(UI_C_CURR);
    p->series[PS_W].color = ui_theme_color(UI_C_POWER);
    /* One sample a column: the axis names what the width shows, not the
     * PROG5V_PLOT_COLS columns the time base was made for. */
    const float span = p->span_s;
    p->span_s = span * (float)r.w / (float)PROG5V_PLOT_COLS;
    ui_plot_render(p, c, (gfx_rect_t){ r.x, (int16_t)(r.y + top), r.w,
                                       (int16_t)(r.h - top) });
    p->span_s = span;
}

gfx_rect_t svs_power_rect(void)
{
    const gfx_rect_t none = { 0, 0, 0, 0 };
    if (drive_shown()) {
        return drive_plot_rect();
    }
    if (prog_ready_shown()) {
        /* KST and HITEC: under the READY rows, over the rail line. */
        const int y = READY_ROW_Y + prog_ready_count() * READY_ROW_H + 8;
        return (gfx_rect_t){ RC_X, (int16_t)y, RC_W,
                             (int16_t)(READY_RAIL_Y - 8 - y) };
    }
    /* The left card's: not under a panel, a page of rows or a session. */
    if (modal_open() || rows_page() >= 0 || prog_shown_set() != NULL) {
        return none;
    }
    switch ((servo_fn_t)s.fn) {
    case SERVO_FN_TEST:   return test_plot_rect();
    case SERVO_FN_LIMRUN: return lim_power_rect();
    case SERVO_FN_RAUTO:  return rauto_power_rect();
    default:              return none;
    }
}

void svs_draw_power(gfx_canvas_t *c)
{
    svs_power_view(c, svs_power_rect());
}
