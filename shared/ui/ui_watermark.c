/*
 * SPDX-License-Identifier: MIT
 */

#include "ui_watermark.h"

#include <math.h>
#include <string.h>

#include "ui_theme.h"

#define TEXT "SIMULATION"

/*
 * 15% -- the middle of what was asked for.  Below about 10% it disappears
 * against the plot's own grid on a bright bench photo; above about 20% it
 * starts competing with the numbers it is warning you about, which would make
 * people want it switched off.
 */
#define ALPHA 38   /* 0.15 * 255 */

void ui_watermark(gfx_canvas_t *c)
{
    if (c == NULL || c->width <= 0 || c->height <= 0) {
        return;
    }

    /*
     * Along the screen's own diagonal rather than a fixed 45 degrees, so it
     * runs corner to corner on this 5:3 panel instead of leaving two empty
     * triangles.
     */
    const float angle = -atan2f((float)c->height, (float)c->width)
                        * 180.0f / 3.14159265358979f;
    const float diag = sqrtf((float)c->width * (float)c->width
                             + (float)c->height * (float)c->height);

    /*
     * The largest whole scale whose *rotated bounding box* still fits the
     * canvas -- solved rather than fudged.  Sizing against the diagonal alone
     * ignores the text's own height, which is what pushes the first and last
     * letters off the corners: at 5:3 the box overflowed vertically by 26 px
     * while fitting horizontally with room to spare.
     */
    const int len = (int)strlen(TEXT);
    const float fw = (float)(len * gfx_font_16x28.width);
    const float fh = (float)gfx_font_16x28.height;
    const float ca = fabsf(cosf(angle * 3.14159265358979f / 180.0f));
    const float sa = fabsf(sinf(angle * 3.14159265358979f / 180.0f));

    const float by_w = (float)c->width  / (fw * ca + fh * sa);
    const float by_h = (float)c->height / (fw * sa + fh * ca);
    float fit = (by_w < by_h) ? by_w : by_h;
    fit *= 0.96f;   /* a hair of air, so no stroke touches an edge */

    int scale = (int)fit;
    if (scale < 1) {
        scale = 1;
    }
    (void)diag;

    gfx_text_rotated(c, c->width / 2, c->height / 2, TEXT, &gfx_font_16x28,
                     ui_theme_color(UI_C_TEXT), scale, ALPHA, angle);
}
