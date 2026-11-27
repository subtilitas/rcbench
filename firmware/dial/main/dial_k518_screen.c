/**
 * @file
 * @brief The Guition JC3636K518C's face on its display: the reading faces
 *        drawn natively at 360 x 360 (shared/dial, dk518_render()), the
 *        boot face for every other face, and the whole frame sent at each
 *        change.
 *
 * A reading face is drawn and sent when its scene changed, about every
 * 50ms on MOTOR & ESC and SERVO LIVE while the readings move: a draw of
 * 259,200 bytes into PSRAM and about 13ms on the wire (arithmetic, not
 * measured).  A face the K518 does not draw yet shows the boot face, drawn
 * again only when the BLE name changes or the face before was a reading
 * face.  A frame that did not go whole is drawn and sent again at the next
 * call.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dial.h"
#include "dk518_face.h"

/** @brief The renderer and the scene the frame holds. */
static dk518_render_t s_render;
/** @brief The boot face is what the frame holds. */
static bool s_boot;

bool dial_screen_init(gfx_color_t *fb)
{
    /* The M5Stack Dial's frame: the K518 draws into its own. */
    (void)fb;
    s_boot = true;
    return dk518_render_init(&s_render);
}

bool dial_screen_show(const dk_face_t *f, const dial_face_view_t *v,
                      bool all)
{
    gfx_color_t *fb = dial_k518_frame();
    gfx_canvas_t c;
    if (fb == NULL) {
        return false;
    }
    if (all) {
        s_render.valid = false;
    }
    gfx_canvas_init(&c, fb, DK518_SIZE, DK518_SIZE, 0);
    switch (dk518_render(&s_render, &c, f, v)) {
    case DK518_DRAWN:
        s_boot = false;
        if (!dial_k518_send()) {
            s_render.valid = false;
        }
        return true;
    case DK518_SAME:
        return false;
    case DK518_NONE:
    default: {
        const bool drew = dial_k518_boot(all || !s_boot);
        s_boot = true;
        return drew;
    }
    }
}
