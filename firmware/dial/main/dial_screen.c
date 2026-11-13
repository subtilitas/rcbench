/**
 * @file
 * @brief The placeholder face: a plain frame, the panel screen's title and
 *        the value the knob acts on as text, drawn with the bench's
 *        shared/gfx.
 *
 * Not the final faces: those are drawn in shared/dial by a later slice
 * (PLAN-DIAL section 4) and host-rendered there.  This face shows what the
 * link carries, so a first look at the device tells whether the panel's
 * packets arrive.  Its few own words are in English and German by the
 * SCREEN packet's language; the panel's title and items arrive translated.
 * The fonts hold ASCII and the German letters; any other character draws
 * as '?'.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>

#include "dial.h"

/** @brief Colours of the bench's dark palette, near enough for a
 *         placeholder. @{ */
#define COL_BG    GFX_RGB(14, 16, 20)
#define COL_RIM   GFX_RGB(70, 76, 88) /**< the frame */
#define COL_TEXT  GFX_RGB(230, 232, 236) /**< text */
#define COL_DIM   GFX_RGB(120, 126, 138) /**< dimmed text */
#define COL_WARN  GFX_RGB(255, 176, 32) /**< a warning */
#define COL_ARMED GFX_RGB(230, 60, 50) /**< ARMED */
/** @} */

/** @brief The placeholder's own words, English then German. */
typedef enum {
    W_NOT_BOUND,
    W_BIND_HINT,
    W_WAITING,
    W_LINK_LOST,
    W_MOVES_NOTHING,
    W_COUNT,
} word_t;

/** @brief The words by language. */
static const char *const k_words[2][W_COUNT] = {
    { "NOT BOUND", "BIND ON THE PANEL", "WAITING FOR THE PANEL",
      "LINK LOST", "THE TURN MOVES NOTHING" },
    { "NICHT GEBUNDEN", "AM PANEL BINDEN", "WARTE AUF DAS PANEL",
      "LINK VERLOREN", "DREHEN BEWEGT NICHTS" },
};

/**
 * @brief Draw a line of text centered on the face's middle column.
 * @param c    the canvas
 * @param y    its top row
 * @param s    the text
 * @param font the font
 * @param col  the colour
 */
static void line(gfx_canvas_t *c, int y, const char *s, const gfx_font_t *font,
                 gfx_color_t col)
{
    const int w = gfx_text_width(font, s, 1);
    gfx_text(c, (DIAL_LCD_SIZE - w) / 2, y, s, font, col, 1);
}

/**
 * @brief The value the knob acts on, as text.
 * @param f   the model
 * @param out the text
 * @param cap its bytes
 */
static void value_text(const dk_face_t *f, char *out, size_t cap)
{
    out[0] = '\0';
    if (!f->have_screen) {
        return;
    }
    switch (f->screen.face) {
    case DK_FACE_VALUE:
        if (f->have_value && f->have_meta) {
            dk_value_text(f->value.value, f->meta.decimals, f->meta.unit, out,
                          cap);
        }
        break;
    case DK_FACE_MOTOR:
        if (f->have_motor) {
            dk_value_text(f->motor.thr_cpct, 2, "%", out, cap);
        }
        break;
    case DK_FACE_SERVO:
        if (f->have_servo) {
            dk_value_text(f->servo.cmd_us, 0, "us", out, cap);
        }
        break;
    case DK_FACE_NAV:
        if ((f->have_nav & (1u << DK_SLOT_FOCUS)) != 0u) {
            snprintf(out, cap, "%s", f->nav[DK_SLOT_FOCUS].item);
        }
        break;
    default:
        break;
    }
}

void dial_screen_draw(gfx_canvas_t *c, const dk_face_t *f,
                      const dial_view_t *v)
{
    const unsigned lang = f->have_screen ? f->screen.lang : DK_LANG_EN;
    const char *const *w = k_words[lang == DK_LANG_DE ? 1 : 0];
    const int mid = DIAL_LCD_SIZE / 2;
    char text[32];
    bool armed;

    gfx_clear(c, COL_BG);
    gfx_draw_circle(c, mid, mid, mid - 2, COL_RIM);
    gfx_draw_circle(c, mid, mid, mid - 3, COL_RIM);

    if (!v->bound && !v->ready) {
        line(c, 60, w[W_NOT_BOUND], &gfx_font_10x20, COL_WARN);
        line(c, 104, v->name, &gfx_font_16x28, COL_TEXT);
        line(c, 160, w[W_BIND_HINT], &gfx_font_8x16, COL_DIM);
        return;
    }
    if (v->lost) {
        if (f->have_screen) {
            line(c, 52, f->screen.title, &gfx_font_10x20, COL_DIM);
        }
        line(c, 104, v->ready ? w[W_LINK_LOST] : w[W_WAITING],
             &gfx_font_10x20, COL_WARN);
        line(c, 150, w[W_MOVES_NOTHING], &gfx_font_8x16, COL_DIM);
        line(c, 190, v->name, &gfx_font_8x16, COL_DIM);
        return;
    }
    if (f->have_screen) {
        line(c, 52, f->screen.title, &gfx_font_10x20, COL_TEXT);
    }
    value_text(f, text, sizeof text);
    line(c, 104, text, &gfx_font_16x28, COL_TEXT);
    armed = f->have_state && (f->state.flags & DK_STA_ARMED) != 0u;
    line(c, 160, armed ? "ARMED" : "SAFE", &gfx_font_10x20,
         armed ? COL_ARMED : COL_DIM);
}
