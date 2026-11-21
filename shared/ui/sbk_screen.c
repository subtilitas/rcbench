/**
 * @file
 * @brief LOAD SETTINGS: the state, the touches and the screen's table.  See
 *        sbk_screen.h; sbk_draw.c draws it.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sbk_screen.h"

#include <string.h>

#include "sbk_ctl.h"
#include "settings_screen.h"
#include "touch_types.h"
#include "ui_text.h"
#include "sbk_int.h"

sbk_view_t sbk_v;

/** @brief Short name for the screen's state in this file. */
#define v sbk_v

void sbk_screen_invalidate(void) { v.drawn_mask = 0u; }

gfx_rect_t sbk_screen_part(sbk_part_t part)
{
    switch (part) {
    case SBK_PART_HOLD:
        return gfx_rect_make(SBK_SX, SBK_HOLD_Y, SBK_SW, SBK_HOLD_H);
    case SBK_PART_BACK:
        return gfx_rect_make(SBK_SX, SBK_BACK_Y, SBK_SW, SBK_BACK_H);
    case SBK_PART_LIST:
        return gfx_rect_make(SBK_LX, SBK_HEAD_Y, SBK_LW,
                             SBK_FLOOR - SBK_HEAD_Y);
    default:
        return gfx_rect_make(0, 0, 0, 0);
    }
}

gfx_rect_t sbk_screen_row(int i)
{
    if (i < 0 || i >= SBK_ROWS) {
        return gfx_rect_make(0, 0, 0, 0);
    }
    return gfx_rect_make(SBK_LX, SBK_ROW_Y + i * SBK_ROW_P, SBK_LW,
                         SBK_ROW_H);
}

int sbk_screen_chosen(void) { return v.chosen; }

bool sbk_screen_offer(void) { return v.offer; }

int sbk_hold_why(void)
{
    uint8_t n = 0u;
    const sb_entry_t *f = sbk_files(&n);
    if (sbk_load_state() != SBK_LOAD_IDLE) {
        return TX_SBK_HEAD_READING;
    }
    if (sbk_armed()) {
        return TX_SBK_WHY_ARMED;
    }
    if (sbk_list_state() != SBK_LIST_READY || v.chosen < 0
        || v.chosen >= (int)n) {
        return TX_SBK_WHY_PICK;
    }
    return (f[v.chosen].err == SF_OK) ? TX_COUNT : TX_SBK_WHY_BAD;
}

bool sbk_screen_hold_ready(void) { return sbk_hold_why() == TX_COUNT; }

/** @brief Whether the file rows take a press: the list shows. */
static bool rows_live(void)
{
    return sbk_load_state() == SBK_LOAD_IDLE;
}

/** @brief The control at a point of the body. */
static int hit_at(int x, int y)
{
    if (gfx_rect_contains(sbk_screen_part(SBK_PART_BACK), x, y)) {
        return SBK_HIT_BACK;
    }
    if (sbk_screen_hold_ready()
        && gfx_rect_contains(sbk_screen_part(SBK_PART_HOLD), x, y)) {
        return SBK_HIT_HOLD;
    }
    uint8_t n = 0u;
    (void)sbk_files(&n);
    for (int i = 0; rows_live() && i < (int)n && i < SBK_ROWS; ++i) {
        if (gfx_rect_contains(sbk_screen_row(i), x, y)) {
            return SBK_HIT_ROW0 + i;
        }
    }
    return SBK_HIT_NONE;
}

/** @brief The press is over. */
static void drop_press(void)
{
    v.hit = SBK_HIT_NONE;
    v.have_press = false;
}

/** @brief BACK or SKIP: the screen is left for its parent. */
static void go_back(void)
{
    ui_router_goto(ui_router_parent(SCREEN_SET_LOAD));
}

/** @brief Takes a touch event. */
static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (v.have_press) {
            return;
        }
        v.hit = hit_at(x, y);
        if (v.hit != SBK_HIT_NONE) {
            v.have_press = true;
            v.press_id = evt->point.id;
            if (v.hit == SBK_HIT_HOLD) {
                ui_hold_begin(&v.hold, TX_HOLD_HINT_SBK_LOAD);
            }
        }
        sbk_screen_invalidate();
        return;
    }
    if (!v.have_press || evt->point.id != v.press_id) {
        return;
    }
    const gfx_rect_t hold = sbk_screen_part(SBK_PART_HOLD);
    if (evt->type == TOUCH_EVENT_MOVE) {
        if (v.hit == SBK_HIT_HOLD && !gfx_rect_contains(hold, x, y)
            && ui_hold_leave(&v.hold)) {
            drop_press();
            sbk_screen_invalidate();
        }
        return;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return;
    }
    const int hit = v.hit;
    drop_press();
    sbk_screen_invalidate();
    if (hit == SBK_HIT_HOLD) {
        (void)ui_hold_end(&v.hold, evt, hold);
        return;
    }
    if (!touch_event_is_tap_up(evt) || hit_at(x, y) != hit) {
        return;
    }
    if (hit == SBK_HIT_BACK) {
        go_back();
    } else if (hit >= SBK_HIT_ROW0) {
        v.chosen = hit - SBK_HIT_ROW0;
    }
}

/** @brief A frame: the hold, and a press the bench took the hold from. */
static void tick(float dt_s)
{
    const int why = sbk_hold_why();
    if (v.hold.down || v.hold.flash_ms > 0 || why != v.drawn_why) {
        v.drawn_why = why;
        sbk_screen_invalidate();
    }
    ui_hold_flash_step(&v.hold, dt_s);
    if (!sbk_screen_hold_ready()) {
        if (v.hit == SBK_HIT_HOLD && ui_hold_left(&v.hold)) {
            drop_press();
            sbk_screen_invalidate();
        }
        return;
    }
    if (ui_hold_tick(&v.hold, dt_s)) {
        sbk_ask_load(v.chosen);
        ui_hold_reached(&v.hold);
        sbk_screen_invalidate();
    }
}

/** @brief Touch events were lost: the press goes. */
static void cancel(void)
{
    if (v.hit == SBK_HIT_HOLD) {
        (void)ui_hold_left(&v.hold);
    }
    drop_press();
    sbk_screen_invalidate();
}

/** @brief The screen's state at start: nothing chosen, nothing held. */
static void reset(void)
{
    memset(&v, 0, sizeof(v));
    v.chosen = -1;
    v.hit = SBK_HIT_NONE;
    ui_hold_reset(&v.hold);
}

/** @brief Shown: the list read anew, or the offer's file chosen. */
static void enter(void)
{
    drop_press();
    ui_hold_reset(&v.hold);
    sbk_load_clear();
    if (v.offer) {
        v.chosen = sbk_offer_index();
    } else {
        v.chosen = -1;
        sbk_ask_list();
    }
    sbk_screen_invalidate();
}

/** @brief Left: the offer is answered; back on SETUP the list of SD CARD
 *         shows as it was left. */
static void leave(void)
{
    drop_press();
    ui_hold_reset(&v.hold);
    if (v.offer) {
        v.offer = false;
        sbk_offer_answered();
    }
    if (ui_router_leaving_to() == SCREEN_SETUP) {
        settings_screen_resume();
    }
}

/** @brief Nothing covers the screen. @return false. */
static bool covered(void) { return false; }

/** @brief The screen, for the router. */
static const ui_screen_t k_screen = {
    .title   = "BACKUP",
    .covered = covered,
    .reset   = reset,
    .enter   = enter,
    .leave   = leave,
    .tick    = tick,
    .event   = event,
    .cancel  = cancel,
    .render  = sbk_render,
};

const ui_screen_t *sbk_screen(void) { return &k_screen; }

void sbk_screen_open(bool offer)
{
    v.offer = offer;
    if (ui_router_current() == SCREEN_SET_LOAD) {
        enter();                    /* the router enters no screen twice */
    } else {
        ui_router_goto(SCREEN_SET_LOAD);
    }
}
