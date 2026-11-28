/**
 * @file
 * @brief The Guition JC3636K518C's faces, drawn natively at 360 x 360: the
 *        boot face and every face of the panel's model.
 *
 * The K518 draws its own faces at its display's size; the M5Stack Dial's
 * faces (dial_face.h, 240 x 240) are not scaled onto it.  What a face shows
 * is the M5 faces' scene (dial_scene_build()); where it stands is the
 * K518's own (dk518_draw.c).  The boot face (dk518_boot.c) stands from the
 * display's start until the face task's first draw: the product's name,
 * the board, the Dial's BLE name and its firmware version, inside a ring
 * at the rim.  The reading faces, MOTOR & ESC and SERVO, show the M5 faces'
 * six cells, six rings, title, pill and hints; the others their body
 * blocks, rim ticks and rim decorations.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "dial_face.h"
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

/** @brief Every string's cells lie inside this radius of the middle. */
#define DK518_R_TEXT 150
/** @brief Shapes (the title tag, the pill, the icons) lie inside this
 *         radius, clear of the innermost ring. */
#define DK518_R_SHAPE 151
/** @brief The ring stack: outermost radius, pitch inward and width,
 *         pixels; DIAL_RINGS rings. @{ */
#define DK518_RING_R0    176
#define DK518_RING_PITCH 4 /**< from one ring to the next inward */
#define DK518_RING_W     2 /**< a ring's width */
/** @} */
/** @brief A reading cell: the label's cap top to the unit's descender. @{ */
#define DK518_CELL_W 112
#define DK518_CELL_H 51 /**< a cell's height */
/** @} */

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

/** @brief A string as drawn, for a test: its cells' box. */
typedef struct {
    const char *s;    /**< the text */
    gfx_rect_t  box;  /**< its cells */
    int         cell; /**< the grid cell it belongs to, or -1 */
} dk518_seen_t;

/** @brief Told of every string drawn. @param ctx the caller's
 *         @param t the string */
typedef void (*dk518_seen_fn)(void *ctx, const dk518_seen_t *t);

/** @brief The K518's face renderer. */
typedef struct {
    dial_scene_t  shown;     /**< what the canvas holds */
    bool          valid;     /**< shown is on the canvas */
    uint8_t       lang;      /**< the last language the panel gave */
    uint32_t      shortened; /**< lines drawn cut short of their text, or
                                  not drawn, since init */
    dk518_seen_fn seen;      /**< told of each string, or NULL */
    void         *seen_ctx;  /**< seen's argument */
} dk518_render_t;

/** @brief What dk518_render() did. */
typedef enum {
    DK518_NONE = 0, /**< a bad argument: nothing drawn */
    DK518_SAME,     /**< the canvas holds the face already */
    DK518_DRAWN,    /**< the face was drawn in full */
} dk518_draw_t;

/**
 * @brief Clear a renderer; its first dk518_render() draws in full.
 * @param r the renderer
 * @return true unless r is NULL
 */
bool dk518_render_init(dk518_render_t *r);

/**
 * @brief Make the canvas show the face of a model and a view, drawn in
 *        full when its scene changed.
 * @param r the renderer
 * @param c the canvas, DK518_SIZE square
 * @param f the panel's model
 * @param v the Dial's state
 * @return what was done
 */
dk518_draw_t dk518_render(dk518_render_t *r, gfx_canvas_t *c,
                          const dk_face_t *f, const dial_face_view_t *v);

/**
 * @brief A theme colour of the dark palette mixed onto its background.
 * @param id ui_color_id_t
 * @param k  its weight, 0 (the background) to 255 (itself)
 * @return the mix
 */
gfx_color_t dk518_color(uint8_t id, unsigned k);

/**
 * @brief Whether a box lies inside a circle about the display's middle:
 *        every corner pixel's center within @p r.
 * @param b the box
 * @param r the radius, pixels
 * @return true when it does; true for an empty box
 */
bool dk518_box_inside(gfx_rect_t b, int r);

/**
 * @brief Where reading cell @p i lies: row by row, left to right.
 * @param i 0 to DIAL_CELLS - 1
 * @return the cell; empty for another @p i
 */
gfx_rect_t dk518_cell_rect(int i);

/**
 * @brief A ring's middle radius.
 * @param i 0 (the outermost) to DIAL_RINGS - 1
 * @return pixels
 */
int dk518_ring_radius(int i);

#ifdef __cplusplus
}
#endif
