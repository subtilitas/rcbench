/**
 * @file
 * @brief The focus layer (ui_focus.h): the focused index, the Dial's
 *        SELECT, BACK and SET, and the ring with the pixels under it.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_focus.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx_damage.h"
#include "touch_types.h"
#include "ui_setlist.h"
#include "ui_shot.h"
#include "ui_theme.h"

/** @brief The pixels a ring keeps for one frame buffer. */
typedef struct {
    bool        valid; /**< a ring is drawn in this buffer */
    gfx_rect_t  r;     /**< round what, body coordinates */
    gfx_rect_t  clip;  /**< the clip it was drawn in */
    gfx_color_t color; /**< its colour */
} ring_t;

/** @brief The layer's state between frames. */
static struct {
    bool           linked;   /**< the Dial is ON and linked */
    bool           armed;    /**< the bench is ARMED */
    bool           have;     /**< cur holds the list of route */
    ui_screen_id_t route;    /**< the screen of that list */
    uint32_t       list;     /**< which list it is */
    int            index;    /**< the focused item */
    uint32_t       back_list;  /**< the list before it */
    int            back_index; /**< the item focused there */
    bool           stepping; /**< step SET runs */
    setting_id_t   step_id;  /**< its setting */
    int            step_net; /**< "+" taps that changed it, less "-" ones */
    float          step_was; /**< its value on entry */
    bool           limit;    /**< the last turn stopped at an end */
    const ui_keypad_t *kp_seen; /**< the keypad open at the last look */
    gfx_damage_t   watch;    /**< what the screen painted this frame */
    gfx_damage_t  *prev;     /**< the record watch stands in for */
    bool           watching; /**< watch is attached */
    bool           watched;  /**< watch holds this frame's painting */
    uint32_t       writes;   /**< pixels the last ring pass wrote */
    ring_t         ring[UI_FOCUS_FBS]; /**< per frame buffer */
} f;  /**< The layer's state. */

/** @brief The screen's items, collected afresh for each look. */
static ui_focus_list_t s_cur;

/** @brief The pixels under each buffer's ring, in walk order. */
static gfx_color_t s_under[UI_FOCUS_FBS][UI_FOCUS_RING_PX];

/* ------------------------------------------------------------ the items */

/**
 * @brief Copy @p src into @p dst, cut at a character boundary.
 * @param dst The label.
 * @param src The text; NULL is "".
 */
static void label_copy(char *dst, const char *src)
{
    size_t n = 0;
    if (src != NULL) {
        n = strlen(src);
        if (n > UI_FOCUS_LABEL - 1u) {
            n = UI_FOCUS_LABEL - 1u;
            /* A UTF-8 continuation byte is 10xxxxxx: never cut before
             * one. */
            while (n > 0u && ((unsigned char)src[n] & 0xC0u) == 0x80u) {
                --n;
            }
        }
        memcpy(dst, src, n);
    }
    dst[n] = '\0';
}

ui_focus_item_t *ui_focus_add(ui_focus_list_t *out, gfx_rect_t rect,
                              const char *label)
{
    if (out == NULL || out->count < 0 || out->count >= UI_FOCUS_MAX) {
        return NULL;
    }
    ui_focus_item_t *it = &out->item[out->count++];
    memset(it, 0, sizeof(*it));
    it->rect = rect;
    it->tap_x = (int16_t)(rect.x + rect.w / 2);
    it->tap_y = (int16_t)(rect.y + rect.h / 2);
    it->kind = UI_FOCUS_TAP;
    it->row = -1;
    label_copy(it->label, label);
    return it;
}

/** @brief The keypad SET runs on, or NULL.  @return it */
static ui_keypad_t *kp_open(void)
{
    return (s_cur.keypad != NULL && s_cur.keypad->open) ? s_cur.keypad
                                                        : NULL;
}

/** @brief Forget the list: the next one starts on its start item. */
static void drop(void)
{
    f.have = false;
    f.stepping = false;
    f.kp_seen = NULL;
    s_cur.count = 0;
    s_cur.keypad = NULL;
}

/**
 * @brief The item of step SET's setting in the list, or -1.
 * @return Its index.
 */
static int step_item(void)
{
    for (int i = 0; i < s_cur.count; ++i) {
        if (s_cur.item[i].kind == UI_FOCUS_STEP
            && s_cur.item[i].id == f.step_id) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Look at the screen on top: collect its items, follow a new list
 *        to its start, keep the index inside the list.
 * @return Whether the layer navigates it now.
 */
static bool refresh(void)
{
    if (!f.linked || f.armed || ui_router_guide_open() || ui_shot_open()) {
        drop();
        return false;
    }
    const ui_screen_id_t route = ui_router_current();
    s_cur.list = 0u;
    s_cur.count = 0;
    s_cur.start = 0;
    s_cur.sl = NULL;
    s_cur.keypad = NULL;
    s_cur.step = 0.0f;
    s_cur.off_below = 0.0f;
    if (!ui_focus_collect(route, &s_cur)
        || (s_cur.count <= 0 && kp_open() == NULL)) {
        drop();
        return false;
    }
    if (!f.have || route != f.route) {
        f.have = true;
        f.route = route;
        f.list = s_cur.list;
        f.index = s_cur.start;
        f.back_list = s_cur.list;
        f.back_index = s_cur.start;
        f.stepping = false;
    } else if (s_cur.list != f.list) {
        /* Back to the list before, from a keypad, a choice list or a
         * level down: on the item it was left on. */
        const bool back = s_cur.list == f.back_list;
        const int was = f.index;
        f.index = back ? f.back_index : s_cur.start;
        f.back_list = f.list;
        f.back_index = was;
        f.list = s_cur.list;
        f.stepping = false;
    }
    if (kp_open() != f.kp_seen) {
        f.kp_seen = kp_open();
        f.limit = false;
    }
    if (f.stepping) {
        const int at = step_item();
        if (at < 0) {
            f.stepping = false;
        } else {
            f.index = at;
        }
    }
    if (f.index >= s_cur.count) {
        f.index = s_cur.count - 1;
    }
    if (f.index < 0) {
        f.index = 0;
    }
    return true;
}

void ui_focus_set_link(bool linked, bool armed)
{
    f.linked = linked;
    f.armed = armed;
}

void ui_focus_reset(void)
{
    memset(&f, 0, sizeof(f));
    s_cur.count = 0;
    s_cur.keypad = NULL;
}

bool ui_focus_shown(void)
{
    return refresh();
}

int ui_focus_index(void)
{
    return refresh() ? f.index : 0;
}

int ui_focus_count(void)
{
    return refresh() ? s_cur.count : 0;
}

const char *ui_focus_label(int i)
{
    return (f.have && i >= 0 && i < s_cur.count) ? s_cur.item[i].label : "";
}

/* -------------------------------------------------------------- the taps */

/**
 * @brief A synthetic tap: DOWN and UP at one point, in one call, with no
 *        tick between them.  A hold on the panel needs ticks with the
 *        contact down, so this completes none.
 * @param x The point's x, in the screen's coordinates.
 * @param y Its y.
 */
static void tap(int x, int y)
{
    touch_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = TOUCH_EVENT_DOWN;
    e.point.id = UI_FOCUS_TOUCH_ID;
    e.point.x = (int16_t)x;
    e.point.y = (int16_t)(y + UI_BAND_H);
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/** @brief A synthetic tap at the middle of @p r.  @param r The rectangle. */
static void tap_rect(gfx_rect_t r)
{
    if (r.w > 0 && r.h > 0) {
        tap(r.x + r.w / 2, r.y + r.h / 2);
    }
}

/* ------------------------------------------------------------------ SET */

/**
 * @brief 10 to the power of -@p d.
 * @param d Digits after the point, 0 to 6.
 * @return One unit of the last of them.
 */
static float unit_of(int d)
{
    float u = 1.0f;
    for (int i = 0; i < d && i < 6; ++i) {
        u *= 0.1f;
    }
    return u;
}

/**
 * @brief A turn on the open keypad: the value moves by the step, out of
 *        an off gap and inside the range, and stands in the entry.
 * @param k       The keypad.
 * @param detents The turn.
 */
static void kp_turn(ui_keypad_t *k, int detents)
{
    const float step = (s_cur.step > 0.0f) ? s_cur.step : unit_of(k->decimals);
    float v = ui_keypad_value(k) + (float)detents * step;
    const float gap = s_cur.off_below;
    if (gap > 0.0f && v > 0.0f && v < gap) {
        /* Between off and the lowest value on: one detent crosses it. */
        v = (detents > 0) ? gap : 0.0f;
    }
    f.limit = false;
    if (v < k->min) {
        v = k->min;
        f.limit = true;
    }
    if (v > k->max) {
        v = k->max;
        f.limit = true;
    }
    ui_keypad_set(k, v);
}

/**
 * @brief A turn in step SET: one "-" or "+" tap a detent, counted where it
 *        changed the value; a tap that changed nothing ends the turn at
 *        the end of the range.
 * @param detents The turn.
 */
static void step_turn(int detents)
{
    const int dir = (detents > 0) ? 1 : -1;
    int n = abs(detents);
    if (n > UI_FOCUS_STEPS_MAX) {
        n = UI_FOCUS_STEPS_MAX;
    }
    f.limit = false;
    for (int i = 0; i < n; ++i) {
        const int at = step_item();
        if (at < 0) {
            f.stepping = false;
            return;
        }
        const float before = settings_get(f.step_id);
        tap_rect(dir > 0 ? s_cur.item[at].plus : s_cur.item[at].minus);
        if (settings_get(f.step_id) == before) {
            f.limit = true;
            return;
        }
        f.step_net += dir;
        /* A change can add or take rows: the keys are found again. */
        (void)refresh();
        if (!f.stepping) {
            return;
        }
    }
}

/** @brief HOLD in step SET: the value back by the taps that changed it. */
static void step_cancel(void)
{
    while (f.stepping && f.step_net != 0) {
        const int at = step_item();
        if (at < 0) {
            break;
        }
        const int dir = (f.step_net > 0) ? -1 : 1;
        const float before = settings_get(f.step_id);
        tap_rect(dir > 0 ? s_cur.item[at].plus : s_cur.item[at].minus);
        if (settings_get(f.step_id) == before) {
            break;
        }
        f.step_net += dir;
        (void)refresh();
    }
    f.stepping = false;
}

bool ui_focus_value(ui_focus_value_t *out)
{
    if (out == NULL || !f.have) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    const ui_keypad_t *k = kp_open();
    if (k != NULL) {
        out->name = k->title;
        out->unit = k->unit;
        out->decimals = k->decimals;
        out->min = k->min;
        out->max = k->max;
        out->step = (s_cur.step > 0.0f) ? s_cur.step : unit_of(k->decimals);
        out->value = ui_keypad_value(k);
        out->was = k->value;
        out->limit = f.limit;
        return true;
    }
    if (!f.stepping) {
        return false;
    }
    const setting_def_t *d = settings_def(f.step_id);
    const int at = step_item();
    if (d == NULL || at < 0) {
        return false;
    }
    out->name = s_cur.item[at].label;
    out->unit = (d->unit != NULL) ? d->unit : "";
    out->decimals = settings_decimals(f.step_id);
    out->min = d->min;
    out->max = d->max;
    out->step = (d->step > 0.0f) ? d->step : unit_of(out->decimals);
    out->value = settings_get(f.step_id);
    out->was = f.step_was;
    out->limit = f.limit;
    return true;
}

/* ---------------------------------------------------------------- input */

bool ui_focus_turn(int detents)
{
    if (!refresh()) {
        return false;
    }
    if (detents == 0) {
        return true;
    }
    ui_keypad_t *k = kp_open();
    if (k != NULL) {
        kp_turn(k, detents);
        return true;
    }
    if (f.stepping) {
        step_turn(detents);
        return true;
    }
    const int n = s_cur.count;
    if (n <= 0) {
        return true;
    }
    f.index = ((f.index + detents % n) % n + n) % n;
    const ui_focus_item_t *it = &s_cur.item[f.index];
    if (s_cur.sl != NULL && it->row >= 0) {
        ui_setlist_scroll_to(s_cur.sl, it->row);
    }
    return true;
}

bool ui_focus_press(void)
{
    if (!refresh()) {
        return false;
    }
    ui_keypad_t *k = kp_open();
    if (k != NULL) {
        tap_rect(ui_keypad_key_rect(k, UI_KEY_OK));
        return true;
    }
    if (f.stepping) {
        f.stepping = false;       /* KEEP: the value stands */
        return true;
    }
    if (s_cur.count <= 0) {
        return true;
    }
    const ui_focus_item_t *it = &s_cur.item[f.index];
    if (it->kind == UI_FOCUS_STEP) {
        f.stepping = true;
        f.step_id = it->id;
        f.step_net = 0;
        f.step_was = settings_get(it->id);
        f.limit = false;
        return true;
    }
    tap(it->tap_x, it->tap_y);
    return true;
}

bool ui_focus_hold(void)
{
    if (!refresh()) {
        return false;
    }
    ui_keypad_t *k = kp_open();
    if (k != NULL) {
        tap_rect(ui_keypad_key_rect(k, UI_KEY_CANCEL));
        return true;
    }
    if (f.stepping) {
        step_cancel();
        return true;
    }
    (void)ui_router_home();
    return true;
}

/* ------------------------------------------------------------- the ring */

/** @brief What a pass over a ring's pixels does. */
typedef enum {
    RING_DRAW,    /**< keep each pixel, then paint the ring */
    RING_RESTORE, /**< put back each pixel the ring still holds */
    RING_REFRESH, /**< keep and paint again each pixel painted over */
} ring_op_t;

/**
 * @brief One pass over the pixels of ring @p g in canvas @p c, segment by
 *        segment: top, bottom, left, right, each clipped to the ring's
 *        clip and the canvas, so the walk is the same for the same ring.
 * @param c     The body canvas.
 * @param g     The ring.
 * @param under The pixels under it, in walk order.
 * @param op    What to do.
 * @return The pixels written.
 */
static uint32_t ring_pass(gfx_canvas_t *c, const ring_t *g,
                          gfx_color_t *under, ring_op_t op)
{
    const gfx_rect_t r = g->r;
    const int t = UI_FOCUS_RING;
    const gfx_rect_t seg[4] = {
        gfx_rect_make(r.x, r.y, r.w, t),
        gfx_rect_make(r.x, r.y + r.h - t, r.w, t),
        gfx_rect_make(r.x, r.y + t, t, r.h - 2 * t),
        gfx_rect_make(r.x + r.w - t, r.y + t, t, r.h - 2 * t),
    };
    uint32_t k = 0u;
    uint32_t written = 0u;
    for (int i = 0; i < 4; ++i) {
        gfx_rect_t s = seg[i];
        if (!gfx_rect_intersect(s, g->clip, &s)
            || !gfx_rect_intersect(s, gfx_rect_make(0, 0, c->width,
                                                    c->height), &s)) {
            continue;
        }
        bool any = false;
        for (int y = s.y; y < s.y + s.h; ++y) {
            gfx_color_t *row = c->pixels + (size_t)y * (size_t)c->stride;
            for (int x = s.x; x < s.x + s.w; ++x) {
                if (k >= UI_FOCUS_RING_PX) {
                    return written;
                }
                gfx_color_t *p = &row[x];
                if (op == RING_DRAW) {
                    under[k] = *p;
                    *p = g->color;
                    ++written;
                    any = true;
                } else if (op == RING_RESTORE) {
                    if (*p == g->color) {
                        *p = under[k];
                        ++written;
                        any = true;
                    }
                } else if (*p != g->color) {
                    under[k] = *p;
                    *p = g->color;
                    ++written;
                    any = true;
                }
                ++k;
            }
        }
        if (any && f.prev != NULL) {
            gfx_damage_mark(f.prev, c, s.x, s.y, s.w, s.h);
        }
    }
    return written;
}

/**
 * @brief Whether the screen painted inside @p r in this frame.
 * @param r The rectangle, body coordinates.
 * @return true when a band of the watch record crosses it.
 */
static bool painted(gfx_rect_t r)
{
    if (!f.watched || f.watch.all) {
        return true;
    }
    for (int b = 0; b < f.watch.bands; ++b) {
        const int y0 = b * GFX_DAMAGE_BAND_H;
        if (f.watch.lo[b] >= f.watch.hi[b] || y0 >= r.y + r.h
            || y0 + GFX_DAMAGE_BAND_H <= r.y) {
            continue;
        }
        if (f.watch.lo[b] < r.x + r.w && f.watch.hi[b] > r.x) {
            return true;
        }
    }
    return false;
}

void ui_focus_render_begin(const gfx_canvas_t *body)
{
    f.watching = false;
    if (body == NULL) {
        return;
    }
    bool kept = false;
    for (int b = 0; b < UI_FOCUS_FBS; ++b) {
        kept = kept || f.ring[b].valid;
    }
    if (!f.linked && !kept) {
        return;     /* no Dial and no ring: nothing to watch */
    }
    f.prev = gfx_damage_sink;
    gfx_damage_init(&f.watch, body);
    gfx_damage_attach(&f.watch);
    f.watching = true;
}

/**
 * @brief The record the screen painted into goes back to whoever had it,
 *        with what the screen painted.
 * @param body The body canvas.
 */
static void watch_end(gfx_canvas_t *body)
{
    f.watched = f.watching;
    if (!f.watching) {
        return;
    }
    f.watching = false;
    gfx_damage_attach(f.prev);
    if (f.prev == NULL) {
        return;
    }
    if (f.watch.all) {
        gfx_damage_mark(f.prev, body, 0, 0, body->width, body->height);
        return;
    }
    for (int b = 0; b < f.watch.bands; ++b) {
        if (f.watch.lo[b] < f.watch.hi[b]) {
            gfx_damage_mark(f.prev, body, f.watch.lo[b],
                            b * GFX_DAMAGE_BAND_H,
                            f.watch.hi[b] - f.watch.lo[b],
                            GFX_DAMAGE_BAND_H);
        }
    }
}

/**
 * @brief The ring this frame wants: round the focused item, in the accent
 *        colour one step off, so no pixel the screen paints is taken for
 *        the ring's.
 * @param body The body canvas.
 * @param out  Receives it.
 * @return false while no ring shows: no focus, or SET.
 */
static bool wanted(const gfx_canvas_t *body, ring_t *out)
{
    if (!refresh() || kp_open() != NULL || s_cur.count <= 0) {
        return false;
    }
    const ui_focus_item_t *it = &s_cur.item[f.index];
    if (it->rect.w <= 2 * UI_FOCUS_RING || it->rect.h <= 2 * UI_FOCUS_RING) {
        return false;
    }
    out->valid = true;
    out->r = it->rect;
    out->clip = body->clip;
    if (it->clip.w > 0 && it->clip.h > 0
        && !gfx_rect_intersect(out->clip, it->clip, &out->clip)) {
        return false;
    }
    out->color = (gfx_color_t)(ui_theme_color(UI_C_ACCENT) ^ 0x0001u);
    return true;
}

/**
 * @brief Whether two rings cover the same pixels in the same colour.
 * @param a One.  @param b The other.
 * @return true when they do.
 */
static bool same_ring(const ring_t *a, const ring_t *b)
{
    return memcmp(&a->r, &b->r, sizeof(a->r)) == 0
           && memcmp(&a->clip, &b->clip, sizeof(a->clip)) == 0
           && a->color == b->color;
}

void ui_focus_render_end(gfx_canvas_t *body, int buffer_index)
{
    if (body == NULL) {
        return;
    }
    watch_end(body);
    f.writes = 0u;
    if (buffer_index < 0 || buffer_index >= UI_FOCUS_FBS) {
        return;
    }
    ring_t *g = &f.ring[buffer_index];
    gfx_color_t *under = s_under[buffer_index];
    ring_t want;
    memset(&want, 0, sizeof(want));
    const bool have = wanted(body, &want);
    if (g->valid && have && same_ring(g, &want)) {
        if (painted(g->r)) {
            f.writes = ring_pass(body, g, under, RING_REFRESH);
        }
        return;
    }
    if (g->valid) {
        f.writes += ring_pass(body, g, under, RING_RESTORE);
        g->valid = false;
    }
    if (have) {
        *g = want;
        f.writes += ring_pass(body, g, under, RING_DRAW);
    }
}

uint32_t ui_focus_ring_writes(void)
{
    return f.writes;
}
