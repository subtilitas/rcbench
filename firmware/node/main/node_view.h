/**
 * @file node_view.h
 * @brief The display node's screen without hardware: the live values and
 *        their highest and lowest, the touch's tap and hold, the value
 *        formats, the layout and the drawing onto a gfx canvas.  Pure C,
 *        under the host suite (test_node_view, tools/render_node.py).
 *
 * The screen is 320 x 240, landscape.  A band of 30px at the top names the
 * link and, when no value comes, the sensor; under it three rows of 70px,
 * voltage, current and power.  Each row holds the live value in 32x44
 * digits on the left and its highest (an up triangle) and lowest (a down
 * triangle) in 10x20 text right of NV_SPLIT_X.
 *
 * Values: every closed 20ms window of the INA228 (rcnode_pwr_win_t) with a
 * sample and a current value counts.  Its power is its mean voltage times
 * its mean current.  The live value is the mean of the windows since the
 * last frame (NV_FRAME_MS, 5 frames a second); the highest and lowest are
 * those of the single windows since the start or the last reset.  They
 * live in RAM: a restart clears them.
 *
 * Touch: a tap (down, then up before NV_HOLD_MS) on a row's highest and
 * lowest resets that row's two; a hold of NV_HOLD_MS anywhere resets all
 * six, once, while still held.  Nothing else is a touch function.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "rcnode.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The screen and its parts, px. @{ */
#define NV_W        320  /**< width */
#define NV_H        240  /**< height */
#define NV_BAND_H    30  /**< the band at the top */
#define NV_ROW_H     70  /**< one value's row */
#define NV_SPLIT_X  220  /**< the live value left, its highest and lowest right */
#define NV_VAL_RIGHT 196 /**< the live value's digits end here */
#define NV_EXT_TEXT_X 244 /**< the highest's and lowest's text starts here */
#define NV_BAND_SPLIT 110 /**< the band's left text ends, its right text starts */
#define NV_BAND_PAD    6  /**< the band's texts from its ends */
/** @} */

/** @brief Timing, ms. @{ */
#define NV_FRAME_MS  200u  /**< a frame: 5 a second */
#define NV_HOLD_MS  2000u  /**< a hold that resets all */
#define NV_HINT_MS   300u  /**< a press this long shows the hold's bar */
#define NV_UP_MS      60u  /**< no touch this long is the release */
/** @} */

/** @brief The quantities, a row each, top to bottom. */
enum {
    NV_Q_V = 0,  /**< voltage, mV */
    NV_Q_A,      /**< current, mA */
    NV_Q_W,      /**< power, mW */
    NV_Q_N,      /**< the count */
};

/** @brief A quantity's highest and lowest. */
typedef struct {
    bool    have;  /**< a window came since the start or the reset */
    int32_t lo;    /**< the lowest */
    int32_t hi;    /**< the highest */
} nv_ext_t;

/** @brief The values the windows give. */
typedef struct {
    nv_ext_t ext[NV_Q_N];  /**< each quantity's highest and lowest */
    int64_t  sum[NV_Q_N];  /**< the sums since the last frame */
    uint32_t n;            /**< windows in the sums */
} nv_stats_t;

/** @brief One frame's values. */
typedef struct {
    bool     ok;              /**< a window came since the last frame */
    int32_t  val[NV_Q_N];     /**< the means, mV, mA, mW */
    nv_ext_t ext[NV_Q_N];     /**< the highest and lowest */
} nv_frame_t;

/** @brief What the screen shows. */
typedef struct {
    uint8_t    lang;     /**< RCNODE_LANG_*; English unless linked */
    bool       linked;   /**< a central is connected and subscribed */
    nv_frame_t f;        /**< the values */
    uint16_t   hold_pm;  /**< the hold's progress, 0 to 1000 per mille */
} nv_model_t;

/** @brief The drawing's memory: what each part shows now. */
typedef struct {
    bool     drawn;              /**< the frame holds a full draw */
    char     left[24];           /**< the band's link text */
    char     right[24];          /**< the band's right text */
    uint16_t bar;                /**< the hold bar's width, px */
    char     val[NV_Q_N][12];    /**< each live value */
    char     hi[NV_Q_N][12];     /**< each highest */
    char     lo[NV_Q_N][12];     /**< each lowest */
} nv_view_t;

/** @brief The touch's memory. */
typedef struct {
    bool     down;    /**< a press runs */
    bool     fired;   /**< its hold fired */
    uint32_t t_down;  /**< when it started */
    uint32_t t_seen;  /**< when a touch was last read */
    int16_t  x;       /**< where it started, screen px */
    int16_t  y;       /**< where it started, screen px */
} nv_touch_t;

/** @brief What a touch step gives. */
typedef enum {
    NV_TOUCH_NONE = 0,  /**< nothing */
    NV_TOUCH_TAP,       /**< a tap at nv_touch_t's x, y */
    NV_TOUCH_HOLD,      /**< the hold of NV_HOLD_MS */
} nv_touch_ev_t;

/**
 * @brief No values, no highest or lowest.
 * @param s The values; NULL does nothing.
 */
void nv_stats_init(nv_stats_t *s);

/**
 * @brief Take one closed window.  A window without a sample (n 0) or
 *        without a current value (i_max RCNODE_I_NONE) counts for nothing.
 * @param s The values; NULL does nothing.
 * @param w The window; NULL does nothing.
 */
void nv_stats_window(nv_stats_t *s, const rcnode_pwr_win_t *w);

/**
 * @brief Take one window's means.
 * @param s    The values; NULL does nothing.
 * @param v_mv The mean voltage, mV.
 * @param i_ua The mean current, µA.
 */
void nv_stats_add(nv_stats_t *s, int32_t v_mv, int32_t i_ua);

/**
 * @brief Reset a quantity's highest and lowest: the next window sets them.
 * @param s The values; NULL does nothing.
 * @param q NV_Q_*; NV_Q_N resets all three.
 */
void nv_stats_reset(nv_stats_t *s, int q);

/**
 * @brief Close a frame: the means of the windows since the last one, and
 *        the highest and lowest.  The sums restart.
 * @param s   The values; NULL gives a frame without values.
 * @param out Receives the frame; NULL does nothing.
 */
void nv_stats_frame(nv_stats_t *s, nv_frame_t *out);

/**
 * @brief A value as the screen writes it, without its unit: voltage
 *        "12.60" (0.00 to 99.99), current "-35.2" (-999.9 to 999.9),
 *        power "12.6" under 100.0 and "443" from 100 (-99999 to 99999),
 *        each rounded half away from zero and clamped to its range.
 * @param q   NV_Q_*.
 * @param v   The value, mV, mA or mW.
 * @param buf Receives the text.
 * @param n   Its size, 8 or more.
 * @return The text's length; 0 for an unknown quantity, a NULL buffer or
 *         one too small.
 */
size_t nv_fmt(int q, int32_t v, char *buf, size_t n);

/**
 * @brief nv_fmt() and the unit, no space: "12.60V", "35.2A", "443W".
 * @param q   NV_Q_*.
 * @param v   The value.
 * @param buf Receives the text.
 * @param n   Its size, 9 or more.
 * @return The text's length; 0 as nv_fmt().
 */
size_t nv_fmt_unit(int q, int32_t v, char *buf, size_t n);

/**
 * @brief One look at the touch controller, every 20ms or so.
 * @param t      The touch; NULL gives nothing.
 * @param down   A finger is on the screen.
 * @param x      Where, screen px (nv_touch_map()).
 * @param y      Where, screen px.
 * @param now_ms The clock; wraps are handled.
 * @return NV_TOUCH_HOLD once, on the first read with the finger down
 *         NV_HOLD_MS or more into a press; NV_TOUCH_TAP when a press that
 *         did not hold ends, NV_UP_MS after the last touch;
 *         NV_TOUCH_NONE otherwise.
 */
nv_touch_ev_t nv_touch_step(nv_touch_t *t, bool down, int x, int y,
                            uint32_t now_ms);

/**
 * @brief The hold's progress for its bar.
 * @param t      The touch; NULL is 0.
 * @param now_ms The clock.
 * @return 0 without a press, before NV_HINT_MS and after the hold fired;
 *         else the time held over NV_HOLD_MS, per mille, up to 1000.
 */
uint16_t nv_touch_progress(const nv_touch_t *t, uint32_t now_ms);

/**
 * @brief The row whose highest and lowest a point falls on.
 * @param x Screen px.
 * @param y Screen px.
 * @return NV_Q_*; -1 left of NV_SPLIT_X, in the band or off the screen.
 */
int nv_hit(int x, int y);

/**
 * @brief The touch controller's point on the screen.  The FT6336G reports
 *        the panel's portrait frame, x 0 to 239 and y 0 to 319; the screen
 *        is that frame turned for landscape.  The turn's direction is
 *        unconfirmed on the board (docs/Nodes.md, "The display node").
 * @param tx The controller's x.
 * @param ty The controller's y.
 * @param sx Receives the screen's x, 0 to NV_W - 1.
 * @param sy Receives the screen's y, 0 to NV_H - 1.
 */
void nv_touch_map(int tx, int ty, int *sx, int *sy);

/**
 * @brief The texts' words in @p lang.
 * @param lang   RCNODE_LANG_*; another value is English.
 * @param linked The link stands.
 * @return The band's left text: "STANDALONE" without a link, whatever the
 *         language; "CONNECTED" or "VERBUNDEN" with one.
 */
const char *nv_text_link(uint8_t lang, bool linked);

/**
 * @brief The band's right text.
 * @param m The model.
 * @return During a hold "RESET ALL" or "ALLE ZURÜCKSETZEN"; without values
 *         "NO SENSOR" or "KEIN SENSOR"; else "".
 */
const char *nv_text_right(const nv_model_t *m);

/**
 * @brief Forget what the frame holds: the next nv_render() draws all.
 * @param v The drawing's memory; NULL does nothing.
 */
void nv_view_init(nv_view_t *v);

/**
 * @brief Draw the parts of @p m that changed since the last call; all of
 *        it after nv_view_init().  A gfx_damage record attached to the
 *        canvas collects the rectangles written.
 * @param v The drawing's memory.
 * @param c The canvas, NV_W x NV_H.
 * @param m The model.
 * @return The parts drawn; 0 when nothing changed or an argument is NULL.
 */
int nv_render(nv_view_t *v, gfx_canvas_t *c, const nv_model_t *m);

#ifdef __cplusplus
}
#endif
