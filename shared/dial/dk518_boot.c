/**
 * @file
 * @brief The Guition JC3636K518C's boot face (dk518_face.h).
 *
 * Rows from the top, every line centered on column 180: "GUITION K518" in
 * the 10x20 font at row 76, "rcbench" in the 16x28 font at 112, "DIAL" in
 * the 20x36 font at scale 2 (40 x 72 a character) at 146, the BLE name in
 * the 16x28 font at 234 and "v" and the version in the 12x24 font at 270.
 * Inside the ring's inner edge (radius 169) the narrowest chord a line
 * meets is at the version's bottom row, 294: 2 x 124px.  The longest
 * version line, 16 characters of 12px, is 192px.  A ring of 6px at
 * radius 172 closes the face at the rim, in the accent colour at 40% over
 * the background.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dk518_face.h"

#include <stddef.h>
#include <string.h>

#include "ui_theme.h"

/**
 * @brief A theme colour of the Dial's dark theme, mixed onto its
 *        background.
 * @param id ui_color_id_t
 * @param k  its weight, 0 (the background) to 255 (itself)
 * @return the mix
 */
static gfx_color_t col(ui_color_id_t id, unsigned k)
{
    const uint32_t a = ui_theme_rgb24(UI_THEME_DARK, id);
    const uint32_t b = ui_theme_rgb24(UI_THEME_DARK, UI_C_BG);
    unsigned ch[3];
    for (int i = 0; i < 3; ++i) {
        const unsigned sh = (unsigned)(16 - 8 * i);
        const unsigned fa = (a >> sh) & 0xFFu;
        const unsigned fb = (b >> sh) & 0xFFu;
        ch[i] = (fb * (255u - k) + fa * k + 127u) / 255u;
    }
    return GFX_RGB(ch[0], ch[1], ch[2]);
}

/**
 * @brief Where to cut a text to at most @p max characters and @p max
 *        bytes, between two characters.
 * @param s   the text
 * @param max the most characters and bytes kept
 * @return the bytes kept
 */
static size_t cut(const char *s, size_t max)
{
    int cells = (int)max;
    size_t n = gfx_text_prefix(s, cells);
    while (n > max && cells > 0) {
        n = gfx_text_prefix(s, --cells);
    }
    return n;
}

/**
 * @brief Draw one line centered on the face's middle column.
 * @param c     the canvas
 * @param y     its top row
 * @param s     the text, cut at DK518_BOOT_TEXT_MAX characters
 * @param font  the font
 * @param fg    the colour
 * @param scale the font's scale
 */
static void line(gfx_canvas_t *c, int y, const char *s,
                 const gfx_font_t *font, gfx_color_t fg, int scale)
{
    char t[DK518_BOOT_TEXT_MAX + 1u];
    const size_t n = cut(s, DK518_BOOT_TEXT_MAX);
    memcpy(t, s, n);
    t[n] = '\0';
    const gfx_rect_t box = { 0, (int16_t)y, DK518_SIZE,
                             (int16_t)gfx_text_height(font, scale) };
    gfx_text_in(c, box, t, font, fg, scale, GFX_ALIGN_CENTER);
}

bool dk518_boot_draw(gfx_canvas_t *c, const dk518_boot_t *b)
{
    if (c == NULL || b == NULL || c->width != DK518_SIZE
        || c->height != DK518_SIZE) {
        return false;
    }
    const int mid = DK518_SIZE / 2;
    gfx_clear(c, col(UI_C_BG, 255u));
    gfx_arc(c, mid, mid, DK518_RIM_R, DK518_RIM_W, 0.0f, 360.0f,
            col(UI_C_ACCENT, 102u));
    line(c, 76, "GUITION K518", &gfx_font_10x20, col(UI_C_TEXT_DIM, 255u),
         1);
    line(c, 112, "rcbench", &gfx_font_16x28, col(UI_C_TEXT_DIM, 255u), 1);
    line(c, 146, "DIAL", &gfx_font_20x36, col(UI_C_TEXT, 255u), 2);
    const bool named = b->name != NULL && b->name[0] != '\0';
    line(c, 234, named ? b->name : "DIAL", &gfx_font_16x28,
         col(UI_C_ACCENT, 255u), 1);
    if (b->version != NULL && b->version[0] != '\0') {
        char v[DK518_BOOT_TEXT_MAX + 1u];
        v[0] = 'v';
        const size_t n = cut(b->version, DK518_BOOT_TEXT_MAX - 1u);
        memcpy(v + 1, b->version, n);
        v[n + 1u] = '\0';
        line(c, 270, v, &gfx_font_12x24, col(UI_C_TEXT_DIM, 255u), 1);
    }
    return true;
}
