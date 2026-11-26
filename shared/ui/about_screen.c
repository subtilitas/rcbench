/**
 * @file
 * @brief ABOUT: the state, the scrolling, the touches, the screen's table
 *        and SETUP > PANEL's row that opens it.  See about_screen.h;
 *        about_draw.c builds the column and draws it.
 *
 * SPDX-License-Identifier: MIT
 */

#include "about_screen.h"

#include <string.h>

#include "settings_screen.h"
#include "touch_types.h"
#include "ui_footer.h"
#include "ui_text.h"
#include "upd_screen.h"

#include "about_int.h"

ab_state_t ab_s;

/** @brief Short name for the screen's state in this file. */
#define v ab_s

void about_screen_invalidate(void) { v.drawn_mask = 0u; }

gfx_rect_t about_screen_part(about_part_t part)
{
    switch (part) {
    case ABOUT_PART_TEXT:
        return gfx_rect_make(UI_MARGIN, AB_TOP, AB_BAR_X - 4 - UI_MARGIN,
                             AB_H);
    case ABOUT_PART_BAR:
        return gfx_rect_make(AB_BAR_X, AB_TOP, AB_BAR_W, AB_H);
    case ABOUT_PART_BACK:
        return ui_footer_rect(UI_FOOTER_NAME);
    case ABOUT_PART_PAGE_UP:
        return ui_footer_rect(UI_FOOTER_HOLD);
    case ABOUT_PART_PAGE_DN:
        return ui_footer_rect(UI_FOOTER_SETTINGS);
    default:
        return gfx_rect_make(0, 0, 0, 0);
    }
}

int ab_max_top(void)
{
    const int n = ab_rows() - ABOUT_ROWS;
    return (n > 0) ? n : 0;
}

/** @brief The top held to the first and the last screen. */
static void clamp_top(void)
{
    const int max = ab_max_top();
    v.top = (v.top < 0) ? 0 : (v.top > max) ? max : v.top;
}

int about_screen_rows(void) { return ab_rows(); }

int about_screen_top(void) { return v.top; }

void about_screen_scroll(int rows)
{
    const int was = v.top;
    v.top += rows;
    clamp_top();
    if (v.top != was) {
        about_screen_invalidate();
    }
}

bool about_screen_at_end(void) { return v.top >= ab_max_top(); }

const char *about_screen_gen_text(int row)
{
    ab_seg_t segs[AB_SEGS];
    const int n = ab_segs(segs);
    int at = 0;
    for (int i = 0; i < n; ++i) {
        if (row < at + segs[i].rows) {
            return (segs[i].style == 0)
                       ? about_gen_text[segs[i].first + row - at] : NULL;
        }
        at += segs[i].rows;
    }
    return NULL;
}

void ab_thumb(int *y, int *h)
{
    const int max = ab_max_top();
    const int rows = ab_rows();
    int th = (rows > 0) ? AB_H * ABOUT_ROWS / rows : AB_H;
    th = (th < AB_BAR_MIN) ? AB_BAR_MIN : (th > AB_H) ? AB_H : th;
    const int travel = AB_H - th;
    *h = th;
    *y = AB_TOP + ((max > 0) ? (int)((long)travel * v.top / max) : 0);
}

/**
 * @brief Put the thumb's top nearest @p y: the top its place along the
 *        travel gives, the travel's ends the first and the last screen.
 * @param y The thumb's top the finger asks for, body px.
 */
static void bar_to(int y)
{
    int ty = 0;
    int th = 0;
    ab_thumb(&ty, &th);
    const int travel = AB_H - th;
    if (travel <= 0) {
        return;
    }
    int rel = y - AB_TOP;
    rel = (rel < 0) ? 0 : (rel > travel) ? travel : rel;
    const int was = v.top;
    v.top = (int)(((long)rel * ab_max_top() + travel / 2) / travel);
    clamp_top();
    if (v.top != was) {
        about_screen_invalidate();
    }
}

/** @brief The control at a point of the body. */
static int hit_at(int x, int y)
{
    static const struct {
        about_part_t part;
        int          hit;
    } k[] = {
        { ABOUT_PART_BACK, AB_HIT_BACK },
        { ABOUT_PART_PAGE_UP, AB_HIT_PAGE_UP },
        { ABOUT_PART_PAGE_DN, AB_HIT_PAGE_DN },
        { ABOUT_PART_BAR, AB_HIT_BAR },
        { ABOUT_PART_TEXT, AB_HIT_TEXT },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        if (gfx_rect_contains(about_screen_part(k[i].part), x, y)) {
            return k[i].hit;
        }
    }
    return AB_HIT_NONE;
}

/** @brief The press is over. */
static void drop_press(void)
{
    v.have_press = false;
    v.hit = AB_HIT_NONE;
    v.moved = false;
}

/** @brief BACK: the screen is left for its parent. */
static void go_back(void)
{
    ui_router_goto(ui_router_parent(SCREEN_ABOUT));
}

/** @brief Whether @p e is further than AB_SLOP from the DOWN. */
static bool far_from_press(const touch_event_t *e)
{
    const int dx = e->point.x - v.x0;
    const int dy = e->point.y - v.y0;
    return dx > AB_SLOP || dx < -AB_SLOP || dy > AB_SLOP || dy < -AB_SLOP;
}

/** @brief A DOWN: the press, unless another contact holds it. */
static void press_down(const touch_event_t *e)
{
    if (v.have_press) {
        return;
    }
    v.hit = hit_at(e->point.x, e->point.y);
    if (v.hit == AB_HIT_NONE) {
        return;
    }
    v.have_press = true;
    v.press_id = e->point.id;
    v.x0 = e->point.x;
    v.y0 = e->point.y;
    v.top0 = v.top;
    v.moved = false;
    if (v.hit == AB_HIT_BAR) {
        int ty = 0;
        int th = 0;
        ab_thumb(&ty, &th);
        if (e->point.y >= ty && e->point.y < ty + th) {
            v.grab = e->point.y - ty;       /* taken by the thumb */
        } else {
            v.grab = th / 2;                /* the thumb jumps there */
            bar_to(e->point.y - v.grab);
        }
    }
    about_screen_invalidate();
}

/** @brief A MOVE of the held contact: a drag on the rows or on the bar. */
static void press_move(const touch_event_t *e)
{
    if (far_from_press(e)) {
        v.moved = true;
    }
    if (v.hit == AB_HIT_BAR) {
        bar_to(e->point.y - v.grab);
    } else if (v.hit == AB_HIT_TEXT && v.moved) {
        const int was = v.top;
        v.top = v.top0 - (e->point.y - v.y0) / AB_PITCH;
        clamp_top();
        if (v.top != was) {
            about_screen_invalidate();
        }
    }
}

/** @brief The UP of the held contact: a button tapped acts. */
static void press_up(const touch_event_t *e)
{
    const int hit = v.hit;
    const bool tap = touch_event_is_tap_up(e) && !v.moved
                     && !far_from_press(e)
                     && hit_at(e->point.x, e->point.y) == hit;
    drop_press();
    about_screen_invalidate();
    if (!tap) {
        return;
    }
    switch (hit) {
    case AB_HIT_BACK:
        go_back();
        break;
    case AB_HIT_PAGE_UP:
        about_screen_scroll(-ABOUT_ROWS);
        break;
    case AB_HIT_PAGE_DN:
        about_screen_scroll(ABOUT_ROWS);
        break;
    default:
        break;
    }
}

/** @brief Takes a touch event. */
static void event(const touch_event_t *e)
{
    if (e == NULL) {
        return;
    }
    if (e->type == TOUCH_EVENT_DOWN) {
        press_down(e);
        return;
    }
    if (!v.have_press || e->point.id != v.press_id) {
        return;
    }
    if (e->type == TOUCH_EVENT_MOVE) {
        press_move(e);
    } else if (e->type == TOUCH_EVENT_UP) {
        press_up(e);
    }
}

/** @brief A frame: the IOmcu's version followed, the top held when the
 *         rows change with the language. */
static void tick(float dt_s)
{
    (void)dt_s;
    const upd_cu_view_t *cu = upd_screen_coproc();
    const bool known = cu != NULL && cu->known;
    const uint32_t ver = known ? ((uint32_t)cu->running.major << 16)
                                     | ((uint32_t)cu->running.minor << 8)
                                     | (uint32_t)cu->running.patch
                               : 0u;
    if (known != v.cu_known || ver != v.cu_version) {
        v.cu_known = known;
        v.cu_version = ver;
        about_screen_invalidate();
    }
    const int was = v.top;
    clamp_top();
    if (v.top != was) {
        about_screen_invalidate();
    }
}

/** @brief Touch events were lost: the press goes. */
static void cancel(void)
{
    drop_press();
    about_screen_invalidate();
}

/** @brief The screen's state at start: the top, no press. */
static void reset(void)
{
    memset(&v, 0, sizeof(v));
    v.hit = AB_HIT_NONE;
}

/** @brief Shown: at the top, nothing pressed. */
static void enter(void)
{
    drop_press();
    v.top = 0;
    about_screen_invalidate();
}

/** @brief Left: back on SETUP, PANEL's list shows as it was left. */
static void leave(void)
{
    drop_press();
    if (ui_router_leaving_to() == SCREEN_SETUP) {
        settings_screen_resume();
    }
}

/** @brief Nothing covers the screen. @return false. */
static bool covered(void) { return false; }

/** @brief The screen, for the router. */
static const ui_screen_t k_screen = {
    .title   = "ABOUT",
    .covered = covered,
    .reset   = reset,
    .enter   = enter,
    .leave   = leave,
    .tick    = tick,
    .event   = event,
    .cancel  = cancel,
    .render  = ab_render,
};

const ui_screen_t *about_screen(void) { return &k_screen; }

void about_screen_open(void)
{
    if (ui_router_current() == SCREEN_ABOUT) {
        enter();                    /* the router enters no screen twice */
    } else {
        ui_router_goto(SCREEN_ABOUT);
    }
}

/** @brief About rcbench tapped: ABOUT opens. */
static void row_tap(void) { about_screen_open(); }

/** @brief SETUP > PANEL's About rcbench. */
static const ui_setlist_custom_t k_row = {
    .label = TX_ABOUT_ROW,
    .help = TX_ABOUT_ROW_HELP,
    .tap = row_tap,
};

const ui_setlist_custom_t *about_setup_row(void) { return &k_row; }
