/**
 * @file render_yge.c
 * @brief The views of programming for YGE ESCs for render_screen.c
 *        (render_yge.h).
 *
 * The session is fed what the control task would publish, by hand: no bench
 * and no ESC run behind these views.  The session is opened as MOTOR & ESC >
 * PROGRAM's row `YGE ESCs` opens it.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_yge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "link_pages.h"
#include "openyge_guard.h"
#include "openyge_map.h"
#include "openyge_session.h"
#include "port_view.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "yge_panel.h"

/** @brief What is fed. */
static yge_prog_feed_t s_f;

/** @brief A table a writable ESC holds: count 32, a 165 HVT on V1.03555. */
static const uint16_t k_table[32] = {
    32, 2, 74, 0, 0, 5, 5, 1, 1, 2, 1, 5712, 38019, 1, 4660, 0,
    0, 1100, 800, 20, 5, 12, 120, 5, 20, 0, 0, 12000, 0, 0, 0, 0,
};

/**
 * @brief A tap at a point, panel pixels.
 * @param x The x.
 * @param y The y.
 */
static void tap_at(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x, .y = (int16_t)y,
                                   .strength = 40 } };

    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    ui_router_tick(0.02f);
}

/**
 * @brief A tap on a control, body pixels.
 * @param r The control; one of no size ends the render.
 */
static void tap(gfx_rect_t r)
{
    if (r.w <= 0 || r.h <= 0) {
        fprintf(stderr, "render_yge: a tap names no control\n");
        exit(1);
    }
    tap_at(r.x + r.w / 2, UI_BAND_H + r.y + r.h / 2);
}

/** @brief The feed, three frames: the model's edges pass. */
static void feed(void)
{
    for (int i = 0; i < 3; ++i) {
        yge_panel_feed(&s_f);
    }
    ui_router_tick(0.02f);
}

/**
 * @brief The page as a free one: the IOmcu answers, nothing held.
 */
static void feed_free(void)
{
    memset(&s_f, 0, sizeof(s_f));
    s_f.now_ms = 1000u;
    s_f.rd.phase = YGE_LINK_READY;
    s_f.rd.minor = LINK_MINOR_YGE;
    s_f.rd.known = true;
    s_f.rd.port = 0xFFu;
    s_f.rd.gpio = 0xFFu;
    s_f.rd.writing = 0xFFu;
    feed();
}

/**
 * @brief The page held by this panel on port 1, GPIO 13, in a state.
 * @param state A link_yge_state_t.
 * @param line  A link_yge_line_t.
 * @param t_ms  Time in the state.
 */
static void feed_state(uint8_t state, uint8_t line, uint16_t t_ms)
{
    s_f.rd.state = state;
    s_f.rd.held = true;
    s_f.rd.ours = true;
    s_f.rd.port = 0u;
    s_f.rd.gpio = 13u;
    s_f.rd.line = line;
    s_f.rd.t_ms = t_ms;
    feed();
}

/**
 * @brief An ESC read, READY: its table in the copy.
 * @param flags LINK_YP_F_* beside HAVE_READ and TAKEN.
 * @param fw    Its firmware, 103555 for V1.03555.
 * @param why   Why it is read-only, an oy_ref_t.
 */
static void feed_read(uint16_t flags, uint32_t fw, uint8_t why)
{
    yge_link_image_t *im = &s_f.image;

    memset(im->value, 0, sizeof(im->value));
    memcpy(im->value, k_table, sizeof(k_table));
    im->value[OY_I_FW_LO] = (uint16_t)(fw & 0xFFFFu);
    im->value[OY_I_FW_HI] = (uint16_t)(fw >> 16);
    im->gen = 1u;
    im->reads = 1u;
    im->entries = 32u;
    im->read_crc = oy_guard_table_crc(im->value, 32u);
    s_f.rd.flags = (uint16_t)(flags | LINK_YP_F_HAVE_READ | LINK_YP_F_TAKEN
                              | LINK_YP_F_SUPPLY | LINK_YP_F_RAIL);
    s_f.rd.type = 5712u;
    s_f.rd.firmware = fw;
    s_f.rd.serial = 4660u;
    s_f.rd.entries = 32u;
    s_f.rd.reads = 1u;
    s_f.rd.read_crc = im->read_crc;
    s_f.rd.ro_why = why;
    s_f.rd.ses_state = (uint8_t)OY_ST_READY;
    feed_state(LINK_YGE_READY, LINK_YGE_LINE_LISTEN, 400u);
}

/**
 * @brief The choice labelled @p label in the list open, tapped.
 * @param label The label.
 */
static void choose(const char *label)
{
    for (int i = 0; yge_panel_choice_label(i) != NULL; ++i) {
        if (strcmp(yge_panel_choice_label(i), label) == 0) {
            tap(yge_panel_choice_rect(i));
            return;
        }
    }
    fprintf(stderr, "render_yge: no choice %s\n", label);
    exit(1);
}

/**
 * @brief Types @p text on the keypad open, then OK.
 * @param text Digits and a dot.
 */
static void kp_type(const char *text)
{
    static const ui_key_t k_digit[10] = { UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3,
                                          UI_KEY_4, UI_KEY_5, UI_KEY_6, UI_KEY_7,
                                          UI_KEY_8, UI_KEY_9 };
    const ui_keypad_t *k = yge_panel_keypad();

    for (const char *p = text; *p != '\0'; ++p) {
        tap(ui_keypad_key_rect(k, *p == '.' ? UI_KEY_DOT : k_digit[*p - '0']));
    }
    tap(ui_keypad_key_rect(k, UI_KEY_OK));
}

/**
 * @brief The draft's edits of the CHECK view: BEC 8.0V, DIRECTION REVERSED,
 *        and 10 pinion teeth more.
 */
static void edits(void)
{
    tap(yge_panel_tab_rect(4));
    tap(yge_panel_value_rect(2));
    kp_type("22");
    tap(yge_panel_tab_rect(1));
    tap(yge_panel_value_rect(3));
    choose("REVERSED");
    tap(yge_panel_value_rect(1));
    kp_type("8");
}

/**
 * @brief A finger on the hold for 2.2s, and off.
 */
static void hold_down_2s(void)
{
    const gfx_rect_t r = yge_panel_hold_rect();
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(r.y + r.h / 2 + UI_BAND_H),
                                   .strength = 40 } };

    ui_router_event(&e);
    for (int i = 0; i < 110; ++i) {
        ui_router_tick(0.02f);
    }
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    feed();
}

/**
 * @brief The views of a read ESC.
 * @param rest The view's name after `motor-yge-`.
 */
static void read_views(const char *rest)
{
    static const char *const k_tabs[] = { "id", "mode", "protect", "start",
                                          "gov", "stick", "raw" };

    feed_read(LINK_YP_F_WRITABLE, 103555u, 0u);
    for (int t = 0; t < 7; ++t) {
        if (strcmp(rest, k_tabs[t]) == 0) {
            tap(yge_panel_tab_rect(t));
            return;
        }
    }
    if (strcmp(rest, "choice") == 0) {
        tap(yge_panel_tab_rect(1));
        tap(yge_panel_value_rect(0));
    } else if (strcmp(rest, "number") == 0) {
        tap(yge_panel_tab_rect(2));
        tap(yge_panel_value_rect(2));
    } else if (strcmp(rest, "check") == 0) {
        edits();
    } else if (strcmp(rest, "leave") == 0) {
        edits();
        tap(yge_panel_back_rect());
    } else if (strcmp(rest, "card") == 0) {
        /* The hold done, the card not answered: the write waits for it. */
        edits();
        hold_down_2s();
        (void)yge_panel_card_look();
        feed();
    } else if (strcmp(rest, "writing") == 0) {
        s_f.rd.flags |= LINK_YP_F_WRITING;
        s_f.rd.ses_state = (uint8_t)OY_ST_WRITE;
        s_f.rd.writing = 2u;
        feed_state(LINK_YGE_RUN, LINK_YGE_LINE_LISTEN, 1400u);
    } else if (strcmp(rest, "files") == 0) {
        static yge_card_req_t s_req;
        static yge_card_done_t s_done;

        tap(yge_panel_action_rect(1));
        if (!yge_panel_card_take(&s_req)) {
            fprintf(stderr, "render_yge: no request for the card\n");
            exit(1);
        }
        memset(&s_done, 0, sizeof(s_done));
        s_done.op = s_req.op;
        s_done.id = s_req.id;
        s_done.count = 2u;
        s_done.total = 2u;
        snprintf(s_done.names[0], sizeof(s_done.names[0]), "YGE002-5712");
        snprintf(s_done.names[1], sizeof(s_done.names[1]), "YGE001-5712");
        yge_panel_card_done(&s_done);
        feed();
    }
}

/**
 * @brief The view's page, fed and tapped there.
 * @param view The view.
 */
static void view_up(const char *view)
{
    port_view_t v;

    port_view_unknown(&v);
    v.count = 4u;
    v.pin[0] = 13u;
    v.pin[1] = 14u;
    v.pin[2] = 15u;
    v.pin[3] = 16u;
    port_view_set(&v);
    supply_screen_put(7.4f, 1.0f);
    feed_free();
    yge_pages.open();
    feed();
    if (strcmp(view, "motor-yge-port") == 0) {
        tap(yge_panel_value_rect(0));
    } else if (strcmp(view, "motor-yge-hand") == 0) {
        tap(yge_panel_value_rect(1));
        choose("BY HAND");
    } else if (strcmp(view, "motor-yge-boot") == 0) {
        tap(yge_panel_value_rect(4));
    } else if (strcmp(view, "motor-yge-entering") == 0) {
        s_f.rd.ses_state = (uint8_t)OY_ST_WAIT;
        s_f.rd.flags = LINK_YP_F_SUPPLY | LINK_YP_F_RAIL;
        feed_state(LINK_YGE_RUN, LINK_YGE_LINE_PARK, 1800u);
    } else if (strcmp(view, "motor-yge-handwait") == 0) {
        tap(yge_panel_value_rect(1));
        choose("BY HAND");
        s_f.rd.flags = LINK_YP_F_BY_HAND;
        feed_state(LINK_YGE_HAND_WAIT, LINK_YGE_LINE_PARK, 4200u);
    } else if (strcmp(view, "motor-yge-stopped") == 0) {
        s_f.rd.flags = LINK_YP_F_STOPPED | LINK_YP_F_FAULT;
        s_f.rd.result = LINK_YGE_RESULT_SESSION;
        s_f.rd.ses = OY_SILENT;
        feed_state(LINK_YGE_OPEN, LINK_YGE_LINE_PARK, 100u);
    } else if (strcmp(view, "motor-yge-held") == 0) {
        s_f.rd.flags = 0u;
        feed_state(LINK_YGE_OPEN, LINK_YGE_LINE_PARK, 100u);
    } else if (strcmp(view, "motor-yge-other") == 0) {
        s_f.rd.state = LINK_YGE_OPEN;
        s_f.rd.held = true;
        s_f.rd.ours = false;
        s_f.rd.port = 2u;
        s_f.rd.gpio = 15u;
        feed();
    } else if (strcmp(view, "motor-yge-not-back") == 0) {
        s_f.rd.result = LINK_YGE_RESULT_NOT_BACK;
        feed_state(LINK_YGE_NOT_BACK, LINK_YGE_LINE_PARK, 100u);
    } else if (strcmp(view, "motor-yge-readonly") == 0) {
        feed_read(0u, 103546u, OY_REF_RO_FIRMWARE);
    } else if (strncmp(view, "motor-yge-", 10) == 0
               && strcmp(view, "motor-yge-connect") != 0) {
        read_views(view + 10);
    }
    ui_router_tick(0.02f);
}

/** @brief The views of a finger left on a hold, and the page each is on. */
static const struct {
    const char *view;
    const char *page;
} k_press[] = {
    { "motor-yge-fill-connect", "motor-yge-connect" },
    { "motor-yge-fill-write", "motor-yge-check" },
    { "motor-yge-fill-leave", "motor-yge-leave" },
};

void render_yge_view(const char *view)
{
    for (size_t i = 0u; i < sizeof(k_press) / sizeof(k_press[0]); ++i) {
        if (strcmp(view, k_press[i].view) == 0) {
            view_up(k_press[i].page);
            const gfx_rect_t r = yge_panel_hold_rect();
            const touch_event_t e = {
                .type = TOUCH_EVENT_DOWN,
                .point = { .id = 1, .x = (int16_t)(r.x + r.w / 2),
                           .y = (int16_t)(r.y + r.h / 2 + UI_BAND_H),
                           .strength = 40 }
            };

            ui_router_event(&e);
            for (int k = 0; k < 50; ++k) {
                ui_router_tick(0.02f);
            }
            return;
        }
    }
    view_up(view);
}
