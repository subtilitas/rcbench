/**
 * @file
 * @brief The Guition JC3636K518C's faces, drawn natively at 360 x 360: the
 *        boot face.
 *
 * The K518 draws its own faces at its display's size; the M5Stack Dial's
 * faces (dial_face.h, 240 x 240) are not scaled onto it.  The boot face
 * stands from the display's start until the K518's faces take over: the
 * product's name, the board, the Dial's BLE name and its firmware version,
 * inside a ring at the rim.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The display's width and height in pixels. */
#define DK518_SIZE 360
/** @brief The rim ring's middle radius and width, pixels. @{ */
#define DK518_RIM_R 172
#define DK518_RIM_W 6 /**< the rim ring's width, pixels */
/** @} */
/** @brief The longest name and version drawn, in characters; a longer one
 *         is cut. */
#define DK518_BOOT_TEXT_MAX 16u

/** @brief What the boot face shows. */
typedef struct {
    const char *name;    /**< the BLE name, "DIAL-3A10"; NULL or "" before
                              the BLE host knows its address */
    const char *version; /**< the firmware version, "0.23.18"; NULL or ""
                              for none */
} dk518_boot_t;

/**
 * @brief Draw the boot face over the whole canvas.
 * @param c the canvas, DK518_SIZE x DK518_SIZE
 * @param b what it shows
 * @return false, drawing nothing, for a NULL argument or a canvas of
 *         another size
 */
bool dk518_boot_draw(gfx_canvas_t *c, const dk518_boot_t *b);

#ifdef __cplusplus
}
#endif
