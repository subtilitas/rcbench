/**
 * @file
 * @brief The face on the display: shared/dial draws what changed into the
 *        frame, and only the rectangles it wrote go out over SPI (serial
 *        peripheral interface).
 *
 * A damage record (gfx_damage.h) is attached to the frame while the face
 * draws.  Its rectangles, at most GFX_DAMAGE_MAX_RECTS, are sent one by
 * one; a reading that changed sends its cell and the arc between its
 * ring's old and new end, not the frame of 115,200 bytes.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>

#include "dial.h"
#include "gfx_damage.h"

#include "esp_log.h"

/** @brief The log's tag. */
static const char *TAG = "face";
/** @brief The renderer: the ring tables, 26,145 bytes, and the scene the
 *         frame holds. */
static dial_render_t s_render;
/** @brief The canvas over the frame. */
static gfx_canvas_t s_canvas;
/** @brief What the face wrote since the last flush. */
static gfx_damage_t s_damage;
/** @brief The frame. */
static gfx_color_t *s_fb;

bool dial_screen_init(gfx_color_t *fb)
{
    uint32_t *keys = malloc(sizeof(uint32_t) * DIAL_RING_KEYS);
    bool ok = false;
    s_fb = fb;
    gfx_canvas_init(&s_canvas, fb, DIAL_LCD_SIZE, DIAL_LCD_SIZE, 0);
    gfx_damage_init(&s_damage, &s_canvas);
    if (keys != NULL) {
        ok = dial_render_init(&s_render, keys);
        free(keys);
    }
    if (!ok) {
        ESP_LOGE(TAG, "the ring tables did not build");
    }
    return ok;
}

bool dial_screen_show(const dk_face_t *f, const dial_face_view_t *v,
                      bool all)
{
    gfx_rect_t box[GFX_DAMAGE_MAX_RECTS];
    int n;
    if (s_fb == NULL) {
        return false;
    }
    if (all) {
        dial_render_invalidate(&s_render);
    }
    gfx_damage_reset(&s_damage);
    gfx_damage_attach(&s_damage);
    (void)dial_render(&s_render, &s_canvas, f, v);
    gfx_damage_attach(NULL);
    n = gfx_damage_rects(&s_damage, box, GFX_DAMAGE_MAX_RECTS);
    for (int i = 0; i < n; ++i) {
        dial_lcd_flush_rect(s_fb, box[i].x, box[i].y, box[i].w, box[i].h);
    }
    return n > 0;
}
