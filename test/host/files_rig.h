/**
 * @file files_rig.h
 * @brief FILES as the files suites drive it: a card in memory behind the
 *        host card task (files_host_io.h), the screen on one canvas, touch
 *        helpers, and the card most cases start from.
 *
 * Only static inline helpers and the state they share.
 *
 * Used by:
 * - test_files_screen.c
 * - test_files_ops.c
 * - test_files_layout.c
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "greatest.h"

#include <stdlib.h>
#include <string.h>

#include "card_tree.h"
#include "files_host_io.h"
#include "files_screen.h"
#include "files_spots.h"
#include "fm_fake.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"

#define W UI_PANEL_W
#define H UI_PANEL_H

static fh_t g_h;
static gfx_color_t *s_fb __attribute__((unused));
static gfx_canvas_t s_c __attribute__((unused));

/** @brief The screen under test. */
static inline const ui_screen_t *screen(void)
{
    return files_screen();
}

/**
 * @brief The card most cases start from: the bench's folders, a log of
 *        each kind, the programmer's record, the fault log, a hidden file
 *        and a note in the root.
 */
static inline void card_default(void)
{
    fk_card_t *c = &g_h.card;
    fk_dir(c, "BENCH/LOGS");
    fk_dir(c, "BENCH/ESC/CONF");
    fk_dir(c, "BENCH/KST/CONF");
    fk_dir(c, "USER/LOGS");
    fk_file(c, "BENCH/LOGS/SERVO001.CSV", NULL, 4096u);
    fk_file(c, "BENCH/RCBENCH.LOG", NULL, 812u);
    fk_file(c, "BENCH/KST/CONF/KST.TXT", NULL, 1200u);
    fk_file(c, "BENCH/KST/CONF/MG90S.TXT", NULL, 1873u);
    fk_file(c, "USER/LOGS/ATEST005.CSV", NULL, 15104u);
    fk_file(c, "USER/LOGS/ATEST005.TXT", NULL, 2210u);
    fk_file(c, "USER/LOGS/LIMIT006.TXT", NULL, 1873u);
    fk_file(c, "NOTES.TXT", NULL, 512u);
    fk_file(c, ".DS_Store", NULL, 6148u);
}

/** @brief Time passes until the card task has answered and nothing waits. */
static inline void settle(void)
{
    for (int i = 0; i < 12; ++i) {
        screen()->tick(0.016f);
    }
}

/** @brief Draw the screen into the canvas. */
static inline void draw(void)
{
    screen()->render(&s_c, 0);
}

/**
 * @brief A fresh screen over a fresh card task, its card left empty: the
 *        settings at their defaults, a SAFE bench, FILES entered.
 */
static inline void fresh_bare(void)
{
    if (s_fb == NULL) {
        s_fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(s_fb);
    }
    memset(s_fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&s_c, s_fb, W, UI_BODY_H, W);
    settings_init();
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    fh_free(&g_h);
    fh_init(&g_h);
    const files_io_t io = fh_files_io(&g_h);
    files_screen_set_io(&io);
    ui_router_init();
    const ui_bench_status_t st = { .link_up = true };
    ui_router_set_status(&st);
}

/** @brief fresh_bare(), the default card, FILES entered and both panes
 *         read. */
static inline void fresh(void)
{
    fresh_bare();
    card_default();
    screen()->enter();
    settle();
}

/** @brief Hand the screen one event of contact @p id at (@p x, @p y). */
static inline void send_id(int type, int id, int x, int y)
{
    touch_event_t e = {
        .type = (touch_event_type_t)type,
        .point = { .id = (uint8_t)id, .x = (int16_t)x, .y = (int16_t)y },
    };
    screen()->event(&e);
}

/** @brief One event of contact 1. */
static inline void send(int type, int x, int y)
{
    send_id(type, 1, x, y);
}

/** @brief A tap of contact 1, then the card task's answers. */
static inline void tap(int x, int y)
{
    send(TOUCH_EVENT_DOWN, x, y);
    send(TOUCH_EVENT_UP, x, y);
    settle();
    draw();
}

/** @brief A tap on footer button @p b. */
static inline void tap_foot(int b)
{
    tap(FS_SPOT_FOOT_CX(b), FS_SPOT_FOOT_CY);
}

/**
 * @brief The row of @p name on pane @p i's page.
 * @return its index, -1 when it is not there
 */
static inline int row_of(int i, const char *name)
{
    const fm_tree_t *t = files_screen_pane(i);
    for (int r = 0; r < t->held; ++r) {
        if (strcmp(t->page[r].name, name) == 0) {
            return r;
        }
    }
    return -1;
}

/** @brief A tap on row @p r on screen of pane @p i. */
static inline void tap_row(int i, int r)
{
    tap(FS_SPOT_ROW_CX(i), FS_SPOT_ROW_CY(r));
}

/**
 * @brief Select @p name in pane @p i by a tap on its row, which is on
 *        screen in the cases' folders; the pane is the active one then.
 * @return true when the row was there
 */
static inline bool pick(int i, const char *name)
{
    const int r = row_of(i, name);
    if (r < 0 || r >= FS_SPOT_ROWS) {
        return false;
    }
    if (files_screen_pane(i)->selected == r && files_screen_active() == i) {
        return true;
    }
    if (files_screen_active() != i) {
        tap_row(i, r);          /* the pane active, the row selected */
        if (files_screen_pane(i)->selected == r) {
            return true;
        }
    }
    tap_row(i, r);
    return files_screen_pane(i)->selected == r;
}

/** @brief Pane @p i into @p name, a folder on its page: two taps. */
static inline bool open_dir(int i, const char *name)
{
    if (!pick(i, name)) {
        return false;
    }
    tap_row(i, row_of(i, name));
    return true;
}

/** @brief The home tag, as the router asks the screen. */
static inline bool home(void)
{
    const bool r = screen()->back();
    settle();
    draw();
    return r;
}

/* ------------------------------------------------------- the keyboard */

#include "ui_textkey.h"

/** @brief The layer the keyboard shows while tk_type() types: 0 lower
 *         case, 1 capitals, 2 marks, as ui_textkey_layer_t; 0 between
 *         calls. */
static int g_tk_layer __attribute__((unused));

/** @brief A key of the name keyboard tapped: the rectangle of a keyboard
 *         opened over FILES' area, FS_TK_AREA (8, 4, 1008 x 488). */
static inline void tk_key(int key)
{
    ui_textkey_t k;
    memset(&k, 0, sizeof(k));
    ui_textkey_open_file(&k, gfx_rect_make(8, 4, 1008, 488), "", "");
    const gfx_rect_t r = ui_textkey_key_rect(&k, key);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/** @brief Type @p text on the name keyboard, switching layers as needed,
 *         and leave it on the small letters. */
static inline void tk_type(const char *text)
{
    static const char *const layers[3] = {
        "1234567890" "qwertyuiop" "asdfghjkl-" " zxcvbnm  ",
        "1234567890" "QWERTYUIOP" "ASDFGHJKL-" " ZXCVBNM  ",
        "!\"#$%&'()*" "+,-./:;<=>" "?@[\\]^_`{|" "}~        ",
    };
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p == ' ') {
            tk_key(UI_TK_SPACE);
            continue;
        }
        int want = -1;
        int key = -1;
        for (int l = 0; l < 3 && key < 0; ++l) {
            const int on = (l + g_tk_layer) % 3;   /* the layer shown first */
            for (int i = 0; i < UI_TK_DEL; ++i) {
                if (layers[on][i] == *p && !(on != 2 && i == UI_TK_SHIFT)) {
                    want = on;
                    key = i;
                    break;
                }
            }
        }
        if (key < 0) {
            continue;
        }
        while (g_tk_layer != want) {
            if (want == 2 || g_tk_layer == 2) {
                tk_key(UI_TK_LAYER);
                g_tk_layer = (g_tk_layer == 2) ? 0 : 2;
            } else {
                tk_key(UI_TK_SHIFT);
                g_tk_layer = (g_tk_layer == 1) ? 0 : 1;
            }
        }
        tk_key(key);
    }
    /* Back to the small letters: a keyboard that stays open keeps its
     * layer, and the next typing starts from there. */
    if (g_tk_layer == 2) {
        tk_key(UI_TK_LAYER);
    } else if (g_tk_layer == 1) {
        tk_key(UI_TK_SHIFT);
    }
    g_tk_layer = 0;
}

/** @brief CLR, @p text typed, OK. */
static inline void tk_name(const char *text)
{
    tk_key(UI_TK_CLR);
    tk_type(text);
    tk_key(UI_TK_OK);
}
