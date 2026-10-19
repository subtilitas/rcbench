/*
 * The rig of the settings suites: the memory stores, the observers, the
 * panel geometry of SETUP, the hub and gesture helpers.
 *
 * Taps are in panel coordinates, because ui_router_event strips the band on
 * the way in; the geometry below adds UI_BAND_H to the screen's own rows.
 *
 * Functions are `static inline`, so a suite that does not call one compiles
 * clean under -Werror=unused-function.  A variable no function here
 * touches carries `unused`.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "greatest.h"

#include <math.h>
#include <stdlib.h>

#include "iomcu_pins.h"
#include "link_pages.h"
#include "out_bind.h"
#include "outputs.h"
#include "sense_link.h"
#include "sense_page.h"
#include "settings.h"
#include "settings_screen.h"
#include "tone_link.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "touch_feed.h"

#define W 800
#define H 480

/* Geometry mirrored from settings_screen.c; if the layout moves these move
 * with it, and the test names say what they aim at.  The hub's tiles come
 * from settings_hub_slot(). */
#define LIST_X   8
#define LIST_Y   (6 + UI_BAND_H)
#define LIST_H   348
#define ROW_PITCH 58
#define ROW_H    54
#define PLUS_X   726
#define MINUS_X  556
#define BTN_W    44
/* The strip under the list and the tiles: RESET CATEGORY under the
 * second column, SAVE under the third. */
#define STRIP_Y  (360 + UI_BAND_H)
#define STRIP_H  34
#define RESET_X  272
#define RESET_Y  STRIP_Y
#define RESET_H  STRIP_H
#define SAVE_X   536
#define SAVE_Y   STRIP_Y
#define SAVE_H   STRIP_H

static gfx_color_t *s_fb;
static gfx_canvas_t s_c;

/* --------------------------------------------------------- memory store */

static float s_saved[SETTING_COUNT];
static bool  s_has_saved;
static int   s_save_calls;

static inline bool mem_load(float *values, int count)
{
    if (!s_has_saved) {
        return false;
    }
    for (int i = 0; i < count && i < SETTING_COUNT; ++i) {
        values[i] = s_saved[i];
    }
    return true;
}

/* True when the medium took every value; the refusing store below is what
 * a panel with unusable NVS (non-volatile storage) looks like. */
static inline bool mem_save(const float *values, int count)
{
    for (int i = 0; i < count && i < SETTING_COUNT; ++i) {
        s_saved[i] = values[i];
    }
    s_has_saved = true;
    ++s_save_calls;
    return true;
}

static const settings_store_t s_mem_store __attribute__((unused)) =
    { mem_load, mem_save, NULL, NULL };

/* A store that is asked and answers no.  It writes nothing, so a later load
 * returns what was there before the refused save. */
static inline bool refuse_save(const float *values, int count)
{
    (void)values;
    (void)count;
    ++s_save_calls;
    return false;
}

static const settings_store_t s_refusing_store __attribute__((unused)) =
    { mem_load, refuse_save, NULL, NULL };

/* A store that keeps the strings too. */
static char s_saved_text[SETTING_TEXT_COUNT][SETTINGS_TEXT_MAX];
static bool s_has_text;

static inline bool mem_load_text(char (*texts)[SETTINGS_TEXT_MAX], int count)
{
    if (!s_has_text) {
        return false;
    }
    for (int i = 0; i < count && i < SETTING_TEXT_COUNT; ++i) {
        memcpy(texts[i], s_saved_text[i], SETTINGS_TEXT_MAX);
    }
    return true;
}

static inline bool mem_save_text(const char (*texts)[SETTINGS_TEXT_MAX], int count)
{
    for (int i = 0; i < count && i < SETTING_TEXT_COUNT; ++i) {
        memcpy(s_saved_text[i], texts[i], SETTINGS_TEXT_MAX);
    }
    s_has_text = true;
    return true;
}

static const settings_store_t s_text_store __attribute__((unused)) =
    { mem_load, mem_save, mem_load_text, mem_save_text };

static int s_observed;
static setting_id_t s_last_observed;

static inline void observer(setting_id_t id)
{
    ++s_observed;
    s_last_observed = id;
}

/* The pole count is the one setting the IOmcu keeps a copy of, so a
 * notification for it is what the panel turns into a write.  Counted apart
 * from the rest, because settings_init() notifies every id in one sweep. */
static int s_poles_seen;
static int s_poles_value;

static inline void poles_observer(setting_id_t id)
{
    ++s_observed;
    s_last_observed = id;
    if (id == SET_MOTOR_POLES) {
        ++s_poles_seen;
        s_poles_value = settings_get_int(SET_MOTOR_POLES);
    }
}

static inline void fresh_model(void)
{
    s_has_saved = false;
    s_save_calls = 0;
    s_observed = 0;
    settings_set_store(NULL);
    settings_set_observer(NULL);
    settings_set_text_observer(NULL);
    settings_init();
}

static int s_seen_ids[SETTING_COUNT];

static inline void every_id_observer(setting_id_t id)
{
    if (id >= 0 && id < SETTING_COUNT) {
        ++s_seen_ids[id];
    }
}

/* The middle of the hub's tile @p index, in panel coordinates. */
static inline int tile_x(int index)
{
    gfx_rect_t r = gfx_rect_make(0, 0, 0, 0);
    (void)settings_hub_slot(index, &r);
    return r.x + r.w / 2;
}

static inline int tile_y(int index)
{
    gfx_rect_t r = gfx_rect_make(0, 0, 0, 0);
    (void)settings_hub_slot(index, &r);
    return UI_BAND_H + r.y + r.h / 2;
}

/* The middle of the home tag, in panel coordinates. */
static inline int home_x(void)
{
    const gfx_rect_t r = ui_home_tag_rect(ui_router_title(SCREEN_SETUP));
    return r.x + r.w / 2;
}

static inline int home_y(void)
{
    const gfx_rect_t r = ui_home_tag_rect(ui_router_title(SCREEN_SETUP));
    return r.y + r.h / 2;
}

static inline void tap(int x, int y);

/* SETUP's tiles: the home tag from a list, nothing from the tiles. */
static inline void to_hub(void)
{
    if (settings_screen_category() >= 0) {
        tap(home_x(), home_y());
    }
}

/* The list of @p cat, opened by its tile as the panel does.  The tiles
 * start with the categories, in their order. */
static inline void open_category(setting_cat_t cat)
{
    to_hub();
    tap(tile_x((int)cat), tile_y((int)cat));
}

/* SETUP on its tiles, the model at its defaults. */
static inline void fresh_hub(void)
{
    if (!s_fb) {
        s_fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(s_fb);
    }
    memset(s_fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&s_c, s_fb, W, H, W);
    fresh_model();
    /* The bench screen is set up by ui_router_init, which reset()s every
     * screen. */
    ui_router_init();
    ui_router_goto(SCREEN_SETUP);
}

/*
 * The list the screen tests open: INTERFACES, SETUP's longest, with a
 * number row of a wide range (INA228 SHUNT, 50 to 20,000 uOhm).  ESC /
 * BENCH's rows, which these tests opened before, are MOTOR & ESC >
 * SETTINGS' (test_motor_workbench).
 */
#define LIST_CAT SET_CAT_IFACE

/* SETUP on LIST_CAT's list, opened from its tile. */
static inline void fresh_screen(void)
{
    fresh_hub();
    open_category(LIST_CAT);
}

static inline void tap(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x, .y = (int16_t)y } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/* The middle of the row of the @p index-th setting of the open list, which
 * lies below the headings above it (INTERFACES has four). */
static inline int row_y(int index)
{
    const int cat = settings_screen_category();
    const int row = settings_screen_row_of(cat >= 0 ? (setting_cat_t)cat
                                                    : LIST_CAT, index);
    return LIST_Y + (row >= 0 ? row : index) * ROW_PITCH + ROW_H / 2;
}

/* The mean of the SAVE button's face, as the panel would show it. */
static inline unsigned save_face(void)
{
    ui_router_render(&s_c, 0);
    unsigned long sum = 0;
    for (int y = SAVE_Y + 6; y < SAVE_Y + SAVE_H - 6; ++y) {
        for (int x = SAVE_X + 6; x < SAVE_X + 200; ++x) {
            sum += (unsigned long)s_fb[y * W + x];
        }
    }
    return (unsigned)(sum & 0xffffffffUL);
}

/*
 * The cases below put finger frames through the tracker and the router
 * (touch_feed.h), one report every REPORT_S with the frame's tick after it,
 * and compare every setting before and after.  Coordinates are the
 * panel's.  XL is on a row's label, XM and XP are the centres of the "-"
 * and the "+" column.
 */
#define REPORT_S 0.010f
#define XL       400
#define XM       (MINUS_X + BTN_W / 2)
#define XP       (PLUS_X + BTN_W / 2)
/* The key the gesture tests press: INA228 max current, a number of
 * 0.1 A steps on row 3 of INTERFACES, in view with the list at its
 * top. */
#define KEY_ID   SET_INA228_MAX_A

static float s_before[SETTING_COUNT];

static inline void snap(void)
{
    for (int i = 0; i < SETTING_COUNT; ++i) {
        s_before[i] = settings_get((setting_id_t)i);
    }
}

/* How many settings differ from the last snap(). */
static inline int changed(void)
{
    int n = 0;
    for (int i = 0; i < SETTING_COUNT; ++i) {
        n += (settings_get((setting_id_t)i) != s_before[i]) ? 1 : 0;
    }
    return n;
}

/* Frames with nothing new on the glass. */
static inline void idle(float seconds)
{
    const int n = (int)lroundf(seconds / REPORT_S);
    for (int i = 0; i < n; ++i) {
        ui_router_tick(REPORT_S);
    }
}

/* SETUP on @p cat at its defaults, the list at its top. */
static inline void gesture_screen(setting_cat_t cat)
{
    fresh_screen();
    feed_reset();
    feed_tick_per_report(REPORT_S);
    if (cat != LIST_CAT) {
        feed_tap(FEED_LONE, home_x(), home_y());
        feed_tap(FEED_LONE, tile_x((int)cat), tile_y((int)cat));
    }
    snap();
}

static inline void swipe(int x0, int y0, int x1, int y1, int step)
{
    finger(FEED_LONE, x0, y0);
    glide(FEED_LONE, x1, y1, step);
    lift(FEED_LONE);
}

/* The row of @p id in its category, or -1. */
static inline int row_of(setting_id_t id)
{
    setting_id_t ids[64];
    const int n = settings_in_category(settings_def(id)->cat, ids, 64);
    for (int i = 0; i < n; ++i) {
        if (ids[i] == id) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------ held values */

static bool s_hold_on;
static int  s_hold_asked;

/* What the servo programmer holds while it has the port. */
static inline bool port_hold(setting_id_t id)
{
    ++s_hold_asked;
    return s_hold_on
        && (id == SET_SERVO_TARGET || id == SET_SERVO_PORT_PWM);
}
