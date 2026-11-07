/**
 * @file upd_rig.h
 * @brief The UPDATES suites' rig: a settings store that survives, the
 *        screen's view, a fresh screen and the taps.  Taken by
 *        test_upd_screen.c and test_upd_settings.c.
 *
 * Only static inline helpers and the state they share.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdio.h>
#include <string.h>

#include "greatest.h"
#include "guard_canvas.h"

#include "fw_release.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_setlist.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "upd_c6.h"
#include "upd_screen.h"
#include "wa_fetch.h"
#include "wifi_screen.h"

/* Where a finger lands: UPDATES and the reminder in the body's
 * coordinates, PANEL UPDATE on the whole panel. */
#include "upd_spots.h"

#define TICK_S     (1.0f / 39.0f)
#define HOLD_TICKS ((int)(UI_HOLD_S / TICK_S) + 5)

/* ------------------------------------------------- a store that survives */

static float g_kept[SETTING_COUNT];
static bool  g_have_kept;
static int   g_saves;

static inline bool mem_load(float *values, int count)
{
    if (!g_have_kept) {
        return false;
    }
    memcpy(values, g_kept, (size_t)count * sizeof(float));
    return true;
}

static inline bool mem_save(const float *values, int count)
{
    memcpy(g_kept, values, (size_t)count * sizeof(float));
    g_have_kept = true;
    ++g_saves;
    return true;
}

static const settings_store_t k_store = { mem_load, mem_save, NULL, NULL };

/* ---------------------------------------------------------------- helpers */

static inline const ui_screen_t *upd(void) { return upd_screen(); }
static inline const ui_screen_t *rem(void) { return upd_remind_screen(); }

static upd_view_t g_v;

static inline void base_view(void)
{
    memset(&g_v, 0, sizeof(g_v));
    g_v.have_net = true;
    snprintf(g_v.net, sizeof(g_v.net), "%s", "workshop");
    snprintf(g_v.running, sizeof(g_v.running), "%s", "0.19.0");
}

static inline void set_newer(void)
{
    g_v.job = UPD_JOB_CHECK;
    g_v.phase = UPD_PH_ENDED;
    g_v.got = UPD_GOT_NEWER;
    snprintf(g_v.found, sizeof(g_v.found), "%s", "0.20.0");
}

static inline void push(void) { upd_screen_set(&g_v); }

static inline void fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    g_have_kept = false;
    g_saves = 0;
    settings_set_store(&k_store);
    settings_init();
    ui_router_init();
    upd()->reset();
    rem()->reset();
    base_view();
    push();
    (void)upd_screen_take();
}

static inline void ev(const ui_screen_t *scr, touch_event_type_t type, uint8_t id,
               int x, int y, uint8_t flags)
{
    touch_event_t e = { .type = type, .flags = flags,
                        .point = { .id = id, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    scr->event(&e);
}

static inline void tap(const ui_screen_t *scr, int x, int y)
{
    ev(scr, TOUCH_EVENT_DOWN, 1, x, y, 0);
    ev(scr, TOUCH_EVENT_UP, 1, x, y, 0);
}

static inline void ticks(const ui_screen_t *scr, int n)
{
    for (int i = 0; i < n; ++i) {
        scr->tick(TICK_S);
    }
}

static inline void router_tap(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/* SETTINGS in the footer opens the list: row @p row's ">" key tapped,
 * then the list closed by SAVE (@p save) or left open. */
static inline void list_step(int row, bool save)
{
    if (!upd()->covered()) {
        tap(upd(), UP_SET_X, UP_SET_Y);
    }
    CHECK(upd()->covered());
    const ui_setlist_t *sl = upd_screen_settings_list();
    const gfx_rect_t k = ui_setlist_row_part(sl, row, UI_SL_PART_PLUS);
    tap(upd(), k.x + k.w / 2, k.y + k.h / 2);
    if (save) {
        const gfx_rect_t sv = ui_setlist_strip_rect(2);
        tap(upd(), sv.x + sv.w / 2, sv.y + sv.h / 2);
    }
}

/* The home tag over the list. */
static inline void list_back(void)
{
    (void)upd()->back();
}
