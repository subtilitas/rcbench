/*
 * The bench the overlay suites run on: a canvas, the overlay open with the
 * link down or on the SERVO screen, touches and ticks fed through either,
 * the helpers that drive a connection, an edit, a write, the file list and
 * the keyboards, and the readers of what the overlay shows.  The servo, the
 * coprocessor's port, the link dispatch and the panel's link module are
 * kst_rig.h.
 *
 * For test_kst_panel.c, test_kst_panel_files.c, test_kst_panel_nocard.c and
 * test_kst_panel_limrun.c.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "kst_limits.h"
#include "kst_panel.h"
#include "kst_reg.h"
#include "kst_rig.h"
#include "pdmini.h"
#include "sense_link.h"
#include "servo_limrun.h"
#include "servo_screen.h"
#include "servo_sim.h"
#include "servo_source.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_workbench.h"

#include "servo_spots.h"

#define W 800
#define H 480

/* The SERVO screen's left card, as servo_screen.c hands it over. */
static const gfx_rect_t AREA = { 6, 6, 488, 420 };

/* PROGRAM over the SERVO screen's right card, and the strip's LIVE and
 * SETTINGS tabs, all in the strip's row (servo_spots.h). */
#define PROG_X SV_PROGRAM_X
#define SETB_X SV_TAB_SETTINGS_X
#define LIVE_X SV_TAB_LIVE_X
#define BTN_Y  SV_TAB_Y
#define ARM_X  648
#define ARM_Y  404

enum { PG_SERVO = 0, PG_TUNE, PG_PULSE, PG_FAILSAFE, PG_LOCKED };

static gfx_color_t *fb;
static gfx_canvas_t cv;
static const ui_screen_t *scr;      /* set while a case drives the screen */

#define M (kst_panel_model())

static inline void show(void)
{
    kst_panel_render(&cv);
}

static inline void canvas(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
}

/* The overlay alone, open, the link down. */
static inline void fresh(void)
{
    canvas();
    scr = NULL;
    settings_set_store(NULL);
    settings_init();
    rig_fresh();
    kst_panel_init(AREA);
    kst_panel_open();
}

/* The same on the SERVO screen, the overlay closed. */
static inline void fresh_screen(void)
{
    canvas();
    settings_set_store(NULL);
    settings_init();
    supply_screen()->reset();
    supply_screen_settings_loaded();
    scr = servo_screen();
    scr->reset();
    rig_fresh();
    scr->enter();
}

static inline void ev(int x, int y, touch_event_type_t t, uint8_t id)
{
    const touch_event_t e = { .type = t,
                              .point = { .id = id, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };

    if (scr != NULL) {
        scr->event(&e);
    } else {
        kst_panel_event(&e);
    }
}

static inline void tick(float dt)
{
    if (scr != NULL) {
        scr->tick(dt);
    } else {
        kst_panel_tick(dt);
    }
}

static inline int cx(gfx_rect_t r) { return r.x + r.w / 2; }
static inline int cy(gfx_rect_t r) { return r.y + r.h / 2; }

static inline void tap(gfx_rect_t r)
{
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 1);
    ev(cx(r), cy(r), TOUCH_EVENT_UP, 1);
    show();
}

static inline void key(ui_key_t k)
{
    tap(ui_keypad_key_rect(kst_panel_keypad(), k));
}

static inline void ticks(float secs)
{
    for (int i = 0; i < (int)(secs * 39.0f + 0.5f); ++i) {
        tick(1.0f / 39.0f);
    }
}

/* Hold a warning page's button for @p secs and let go. */
static inline void hold(float secs)
{
    const gfx_rect_t r = kst_panel_apply_rect();

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 1);
    ticks(secs);
    show();
    ev(cx(r), cy(r), TOUCH_EVENT_UP, 1);
    show();
}

static inline gfx_rect_t val(int page, int row, int col)
{
    return kst_panel_value_rect(page, row, col);
}

static inline void settle(void)
{
    CHECK(rig_settle());
    tick(0.02f);
    show();
}

static inline void link_up(void)
{
    CHECK(rig_link_up(LINK_PROTOCOL_MINOR));
    show();
}

/* The first line of the message shown, "" for none. */
static inline const char *said(void)
{
    static char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];

    return kst_panel_message_lines(kst_prog_message(M), t) > 0 ? t[0] : "";
}

static inline unsigned msg(void)
{
    return kst_prog_message(M)->id;
}

static inline uint16_t draft(kst_field_id_t id)
{
    return kst_field_get(kst_prog_draft(M), id);
}

static inline uint16_t servo(kst_field_id_t id)
{
    const kst_image_t img = rig_servo();

    return kst_field_get(&img, id);
}

/* The link up, CONNECT, the servo read. */
static inline void connected(void)
{
    link_up();
    CHECK_EQ(kst_panel_editor(), 0);
    tap(val(PG_SERVO, 0, 0));
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_have_servo(M));
}

/* ENABLE WRITE TO SERVO through its page. */
static inline void enable(void)
{
    tap(kst_panel_tab_rect(PG_SERVO));
    tap(val(PG_SERVO, 3, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    hold(2.1f);
    CHECK(kst_prog_write_enabled(M));
}

/* A value as a confirmed edit leaves it. */
static inline void edit(kst_field_id_t id, uint16_t raw)
{
    if (kst_prog_edit(M, id, raw) == KST_PROG_EDIT_SOFT) {
        kst_prog_soft_confirm(M);
    }
    CHECK_EQ(draft(id), raw);
}

/* Three changed values: dead band, boost and soft start. */
static inline void edits(void)
{
    edit(KST_F_DEAD_BAND, (uint16_t)(servo(KST_F_DEAD_BAND) + 2u));
    edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 3u));
    edit(KST_F_SOFT_START, (uint16_t)(1u - servo(KST_F_SOFT_START)));
    CHECK_EQ(kst_prog_changes(M), 3);
}

/* Any line of the open warning page reads @p s. */
static inline bool warns(const char *s)
{
    for (int i = 0; kst_panel_warning_line(i) != NULL; ++i) {
        if (strcmp(kst_panel_warning_line(i), s) == 0) {
            return true;
        }
    }
    return false;
}

static inline int choices(void)
{
    int n = 0;

    while (kst_panel_choice_label(n) != NULL) {
        ++n;
    }
    return n;
}

/* The entry of the open list that reads @p s, -1 for none. */
static inline int choice(const char *s)
{
    for (int i = 0; kst_panel_choice_label(i) != NULL; ++i) {
        if (strcmp(kst_panel_choice_label(i), s) == 0) {
            return i;
        }
    }
    return -1;
}

/* The link goes after the first register of a write of edits(). */
static inline void cut_write(void)
{
    connected();
    enable();
    edits();
    tap(val(PG_SERVO, 4, 0));
    hold(2.1f);
    for (unsigned i = 0; i < 30000u && g_rig.sim.n_writes == 0u; ++i) {
        rig_frame();
    }
    CHECK(g_rig.sim.n_writes > 0u);
    kst_link_lost(&g_rig.link);
    g_rig.safe = false;
    rig_frames(200);
    show();
}

static inline void come_back(void)
{
    g_rig.safe = true;
    CHECK(rig_link_up(LINK_PROTOCOL_MINOR));
    tick(0.02f);
    show();
}

/* A key of the text keyboard by its index. */
static inline void tkey(int key)
{
    tap(ui_textkey_key_rect(kst_panel_textkey(), key));
}

/* FILES on the SERVO tab, and what the card answers. */
static inline void files(void)
{
    tap(kst_panel_tab_rect(PG_SERVO));
    tap(val(PG_SERVO, 1, 1));
    settle();
}

/* FILES, the entry that saves: the keyboard is open on the preset name. */
static inline void save_entry(void)
{
    files();
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK_EQ(choice(TR(KP_FILE_SAVE)), 0);
    tap(kst_panel_choice_rect(0));
    CHECK_EQ(kst_panel_editor(), 4);
}

/* The lines of the message shown. */
static inline int lines(char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE])
{
    return kst_panel_message_lines(kst_prog_message(M), t);
}

/* The card is pulled, and the link module sees it. */
static inline void card_out(void)
{
    g_rig.card = false;
    rig_frame();
}

/* The card is put in, and the link module sees it. */
static inline void card_in(void)
{
    g_rig.card = true;
    rig_frame();
}

/* A tap on the SERVO screen at (@p x, @p y), drawn. */
static inline void screen_tap(int x, int y)
{
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    ev(x, y, TOUCH_EVENT_UP, 1);
    scr->tick(0.02f);
    scr->render(&cv, 0);
}
