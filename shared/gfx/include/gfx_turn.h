/**
 * @file
 * @brief Screen rotation 180° in the flush: the UI draws its picture as it
 *        always does, and each flip copies what changed into the frame
 *        buffer the panel scans, turned, through a copy engine (on the
 *        ESP32-P4 the PPA, pixel-processing accelerator).
 *
 * Turned, the UI draws into a logical buffer of its own, one per frame
 * buffer, with a damage record attached (gfx_damage.h).  gfx_turn_flush()
 * copies the record's rectangles, turned, into the frame buffer of the
 * same index; the whole picture for the GFX_TURN_FULL_FLIPS flips after a
 * switch.  NORMAL draws straight into the frame buffer and copies nothing.
 *
 * One flag says what the panel shows: gfx_turn_shown(), set by
 * gfx_turn_presented() once the frame is on the glass.  The touch map
 * turns from that flag, so picture and touch turn on the same frame.
 *
 * A flush has GFX_TURN_BUDGET_US, 50ms, for all its copies.  A copy that
 * runs out of it or fails makes the flush copy the logical buffer as it
 * stands into the frame buffer, unturned, and present that: picture and
 * touch fall back to NORMAL together.  The fallback holds until the next
 * start (gfx_turn_init()); the stored setting stays as it is.  A copy the
 * engine finishes after its timeout writes its block once more; that block
 * shows turned until the screen repaints it.
 *
 * Every call but gfx_turn_shown() comes from the task that draws and
 * flips.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "gfx_damage.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A flush's time for all its copies, us: 50ms. */
#define GFX_TURN_BUDGET_US  50000u
/** @brief Flips that copy the whole picture after a switch, 2: one per
 *         frame buffer. */
#define GFX_TURN_FULL_FLIPS 2u

/** @brief How one copy ended. */
typedef enum {
    GFX_TURN_COPY_DONE = 0,  /**< the block is in the frame buffer         */
    GFX_TURN_COPY_TIMEOUT,   /**< the engine did not finish in the budget  */
    GFX_TURN_COPY_ERROR,     /**< the engine refused the block             */
} gfx_turn_copy_t;

/** @brief Why the flush fell back to NORMAL. */
typedef enum {
    GFX_TURN_FAIL_NONE = 0,  /**< it did not                               */
    GFX_TURN_FAIL_TIMEOUT,   /**< a copy timed out                         */
    GFX_TURN_FAIL_ERROR,     /**< a copy was refused                       */
    GFX_TURN_FAIL_ENGINE,    /**< no copy engine or no logical buffers     */
} gfx_turn_fail_t;

/** @brief What a flush leaves in the frame buffer. */
typedef enum {
    GFX_TURN_SHOW_NORMAL = 0, /**< the picture as drawn                    */
    GFX_TURN_SHOW_TURNED,     /**< the picture turned by 180°              */
    GFX_TURN_SHOW_FALLBACK,   /**< as drawn, after a failed copy           */
} gfx_turn_show_t;

/** @brief The copy engine. */
typedef struct {
    /**
     * @brief Copy block @p r of @p src into @p dst, turned by 180°, and
     *        wait for it.
     * @param ctx the engine's context
     * @param src the logical buffer, width x height, packed
     * @param dst the frame buffer, of the same size
     * @param r the block, in @p src's coordinates
     * @param budget_us the longest it may wait, us
     * @return how the copy ended
     */
    gfx_turn_copy_t (*copy)(void *ctx, const gfx_color_t *src,
                            gfx_color_t *dst, gfx_rect_t r,
                            uint32_t budget_us);
    /**
     * @brief The time, us; differences wrap.
     * @param ctx the engine's context
     * @return the time
     */
    uint32_t (*now_us)(void *ctx);
    void *ctx;   /**< passed to both */
} gfx_turn_ops_t;

/** @brief The rotation's state. */
typedef struct {
    gfx_damage_t damage;   /**< what the frame being drawn wrote           */
    int16_t width;         /**< the picture's width, pixels                */
    int16_t height;        /**< its height, rows                           */
    bool requested;        /**< the setting asks for 180°                  */
    bool drawing;          /**< the frame being drawn goes through a copy  */
    bool failed;           /**< fallen back until the next start           */
    gfx_turn_fail_t why;   /**< why it fell back                           */
    bool alerted;          /**< the fallback's alert has been raised       */
    bool repaint;          /**< the UI repaints everything next frame      */
    bool alert;            /**< an alert for the band is due               */
    uint8_t full_left;     /**< flips left that copy the whole picture     */
    uint16_t rects;        /**< the last flush's copies                    */
    uint32_t pixels;       /**< the pixels they held                       */
    uint32_t copy_us;      /**< the time they took, us                     */
} gfx_turn_t;

/**
 * @brief NORMAL, nothing failed, for a @p width x @p height picture; the
 *        shown flag NORMAL too.
 * @param t the state
 * @param width the picture's width, pixels
 * @param height its height, rows
 */
void gfx_turn_init(gfx_turn_t *t, int width, int height);

/**
 * @brief What the setting asks for; taken at the next gfx_turn_next().
 * @param t the state
 * @param turned true for 180°
 */
void gfx_turn_request(gfx_turn_t *t, bool turned);

/**
 * @brief Fall back to NORMAL until the next start.
 * @param t the state
 * @param why the reason, not GFX_TURN_FAIL_NONE
 */
void gfx_turn_fail(gfx_turn_t *t, gfx_turn_fail_t why);

/**
 * @brief Whether the frame being drawn goes into a logical buffer.
 * @param t the state
 * @return true while turned and not fallen back
 */
bool gfx_turn_drawing(const gfx_turn_t *t);

/**
 * @brief Start a frame: drawing turned, the damage record bound to
 *        @p logical, empty and attached; NORMAL, no record attached.
 * @param t the state
 * @param logical the canvas the frame is drawn on
 */
void gfx_turn_begin(gfx_turn_t *t, const gfx_canvas_t *logical);

/**
 * @brief End a frame: copy what it drew into @p fb, turned.
 *
 * Detaches the damage record.  NORMAL copies nothing.  Turned, the record's
 * rectangles, or the whole picture while full flips are left, go through
 * @p ops within GFX_TURN_BUDGET_US; a copy that times out or fails makes
 * the frame the logical buffer as drawn, unturned, and the state fallen
 * back.
 *
 * @param t the state
 * @param logical the logical buffer the frame was drawn into
 * @param fb the frame buffer of the same index
 * @param ops the copy engine
 * @return what @p fb now holds
 */
gfx_turn_show_t gfx_turn_flush(gfx_turn_t *t, const gfx_color_t *logical,
                               gfx_color_t *fb, const gfx_turn_ops_t *ops);

/**
 * @brief The frame of the last flush is on the glass: set the shown flag.
 * @param t the state
 * @param show what that flush returned
 * @return true when the shown flag changed
 */
bool gfx_turn_presented(gfx_turn_t *t, gfx_turn_show_t show);

/**
 * @brief After a flip: take the request for the next frame.
 *
 * A switch asks the UI for a whole repaint (gfx_turn_take_repaint()), and
 * a switch to 180° copies the whole picture for GFX_TURN_FULL_FLIPS flips.
 * A request for 180° that meets a fallback asks for its alert once.
 *
 * @param t the state
 */
void gfx_turn_next(gfx_turn_t *t);

/**
 * @brief Whether the UI must repaint everything before the next frame;
 *        clears it.
 * @param t the state
 * @return true once per switch
 */
bool gfx_turn_take_repaint(gfx_turn_t *t);

/**
 * @brief Whether the fallback's alert is due; clears it.
 * @param t the state
 * @return true once per start that falls back while 180° is asked for
 */
bool gfx_turn_take_alert(gfx_turn_t *t);

/**
 * @brief What the panel shows: the one flag picture and touch turn by.
 *        Read from any task.
 * @return true while the picture on the glass is turned by 180°
 */
bool gfx_turn_shown(void);

#ifdef __cplusplus
}
#endif
