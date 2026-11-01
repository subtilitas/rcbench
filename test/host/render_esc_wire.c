/**
 * @file render_esc_wire.c
 * @brief The ESC signal-wire session's views for render_screen.c (render_esc_wire.h).
 *
 * The session is fed what the control task would publish, by hand: no bench and no
 * ESC run behind these views.  MOTOR & ESC > PROGRAM's second row opens it.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_esc_wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esc_wire_panel.h"
#include "ew_id.h"
#include "ew_map.h"
#include "link_pages.h"
#include "motor_screen.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_layout.h"
#include "ui_screen.h"

/** @brief What is fed. */
static esc_prog_feed_t s_f;

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
        fprintf(stderr, "render_esc_wire: a tap names no control\n");
        exit(1);
    }
    tap_at(r.x + r.w / 2, UI_BAND_H + r.y + r.h / 2);
}

/** @brief The feed, three frames: the model's edges pass. */
static void feed(void)
{
    for (int i = 0; i < 3; ++i) {
        esc_wire_panel_feed(&s_f);
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
    s_f.rd.phase = ESC_LINK_READY;
    s_f.rd.minor = LINK_MINOR_ESC_WIRE;
    s_f.rd.known = true;
    s_f.rd.gpio = 0xFFu;
    feed();
}

/**
 * @brief The page held by this panel, in a state.
 * @param state A link_esc_state_t.
 * @param line  A link_esc_line_t.
 * @param t_ms  Time in the state.
 */
static void feed_state(uint8_t state, uint8_t line, uint16_t t_ms)
{
    s_f.rd.state = state;
    s_f.rd.held = true;
    s_f.rd.ours = true;
    s_f.rd.gpio = 0u;
    s_f.rd.line = line;
    s_f.rd.t_ms = t_ms;
    feed();
}

/**
 * @brief A read ESC, READY: the page's identity and a page of defaults.
 * @param fw     An ew_fw_t.
 * @param layout The layout or revision.
 * @param flags  LINK_EW_F_* beside HAVE_READ.
 * @param why    Why it is not written.
 */
static void feed_read(uint8_t fw, uint8_t layout, uint16_t flags, uint8_t why)
{
    esc_link_image_t *im = &s_f.image;
    const uint16_t len = ew_map_len(fw);
    const ew_field_t *f;

    s_f.rd.fw = fw;
    s_f.rd.layout = layout;
    s_f.rd.why = why;
    s_f.rd.len = len;
    s_f.rd.reads = 1u;
    s_f.rd.read_crc = 0x1234u;
    s_f.rd.signature = 0xE8B2u;
    s_f.rd.cpu = fw == (uint8_t)EW_FW_AM ? EW_CPU_ARM
                 : fw == (uint8_t)EW_FW_E32 ? EW_CPU_TEXT : EW_CPU_EFM8;
    s_f.rd.flags = (uint16_t)(flags | LINK_EW_F_HAVE_READ | LINK_EW_F_IN_MODE);
    im->gen = 1u;
    im->reads = 1u;
    im->read_crc = 0x1234u;
    im->len = len;
    memset(im->page, 0xFF, sizeof(im->page));
    if (fw == (uint8_t)EW_FW_E32) {
        for (size_t i = 0u; i < EW_E32_KEYS; ++i) {
            ew_e32_put(im->page, i, ew_map_e32[i].lo);
        }
        snprintf(im->name, sizeof(im->name), "BOARD_F051");
        snprintf(im->music, sizeof(im->music), "C4 E4 G4 C5");
    } else {
        for (size_t i = 0u; (f = ew_map_nth(fw, layout, i)) != NULL; ++i) {
            im->page[f->off] = f->lo;
        }
        if (fw == (uint8_t)EW_FW_AM) {
            im->page[0] = 1u;
            im->page[1] = layout;
            snprintf(im->name, sizeof(im->name), "EXAMPLE_F421");
        } else {
            memcpy(&im->page[0x40], "#S_H_50A#       ", 16u);
            memcpy(&im->page[0x50], "#BLHELI$EFM8B21#", 16u);
            im->page[0] = 16u;
            im->page[1] = 7u;
            im->page[2] = layout;
        }
    }
    feed_state(LINK_ESC_READY, LINK_ESC_LINE_UP, 400u);
}

/**
 * @brief The WRITE page, for values of the tab shown changed through the draft.
 */
static void open_write(void)
{
    tap(esc_wire_panel_action_rect(2));
}

/**
 * @brief Changes the row: its choice's first entry, or the keypad's lowest.
 * @param row The row.
 */
static void change_row(int row)
{
    tap(esc_wire_panel_value_rect(row));
    if (esc_wire_panel_keypad()->open) {
        tap(ui_keypad_key_rect(esc_wire_panel_keypad(), UI_KEY_0));
        tap(ui_keypad_key_rect(esc_wire_panel_keypad(), UI_KEY_OK));
        if (esc_wire_panel_keypad()->open) {
            tap(ui_keypad_key_rect(esc_wire_panel_keypad(), UI_KEY_CANCEL));
        }
    } else {
        tap(esc_wire_panel_choice_rect(0));
    }
}

/**
 * @brief A finger on the hold for 2.2s, and off.
 */
static void hold_down_2s(void)
{
    const gfx_rect_t r = esc_wire_panel_hold_rect();
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
    ui_router_tick(0.02f);
}

/**
 * @brief The card's task answers the request out: 2 files for a list, a line for a load.
 * @param result An esc_card_result_t.
 */
static void card_answer(uint8_t result)
{
    static esc_card_req_t s_req;
    static esc_card_done_t s_done;

    if (!esc_wire_panel_card_take(&s_req)) {
        fprintf(stderr, "render_esc_wire: no request for the card\n");
        exit(1);
    }
    memset(&s_done, 0, sizeof(s_done));
    s_done.op = s_req.op;
    s_done.id = s_req.id;
    s_done.result = result;
    if (s_req.op == (uint8_t)ESC_CARD_LIST) {
        s_done.count = 2u;
        s_done.total = 2u;
        snprintf(s_done.names[0], sizeof(s_done.names[0]), "ESCW002");
        snprintf(s_done.names[1], sizeof(s_done.names[1]), "ESCW001");
    } else if (s_req.op == (uint8_t)ESC_CARD_LOAD) {
        s_done.len = 2u;
        snprintf(s_done.text, sizeof(s_done.text), "x\n");
    }
    esc_wire_panel_card_done(&s_done);
    feed();
}

void render_esc_wire_view(const char *view)
{
    const esc_wire_target_t targets[2] = { { 0u, 0u },
                                           { LINK_EW_TARGET_PORT | 1u, 14u } };

    supply_screen_put(7.4f, 1.0f);
    esc_wire_panel_set_targets(targets, 2u, true);
    feed_free();
    if (strcmp(view, "motor-wire-notarget") == 0) {
        esc_wire_panel_set_targets(NULL, 0u, true);
    }
    /* PROGRAM's second row. */
    tap_at(279, UI_BAND_H + 83 + 56);
    feed();
    if (strcmp(view, "motor-wire-target") == 0) {
        tap(esc_wire_panel_value_rect(0));
    } else if (strcmp(view, "motor-wire-hand") == 0) {
        tap(esc_wire_panel_value_rect(1));
        tap(esc_wire_panel_choice_rect(1));
    } else if (strcmp(view, "motor-wire-keypad") == 0) {
        tap(esc_wire_panel_value_rect(2));
    } else if (strcmp(view, "motor-wire-entering") == 0) {
        feed_state(LINK_ESC_POWER_ON, LINK_ESC_LINE_HIGH, 1800u);
    } else if (strcmp(view, "motor-wire-handwait") == 0) {
        feed_state(LINK_ESC_HAND_WAIT, LINK_ESC_LINE_HIGH, 4200u);
    } else if (strcmp(view, "motor-wire-stopped") == 0) {
        s_f.rd.flags = LINK_EW_F_STOPPED | LINK_EW_F_FAULT;
        s_f.rd.result = LINK_ESC_RESULT_SESSION;
        feed_state(LINK_ESC_OPEN, LINK_ESC_LINE_PARK, 100u);
    } else if (strcmp(view, "motor-wire-stopped-band") == 0) {
        s_f.rd.flags = LINK_EW_F_STOPPED;
        s_f.stopped = true;
        feed_state(LINK_ESC_OPEN, LINK_ESC_LINE_PARK, 100u);
    } else if (strcmp(view, "motor-wire-held-stop") == 0) {
        s_f.rd.flags = LINK_EW_F_STOPPED;
        feed_state(LINK_ESC_OPEN, LINK_ESC_LINE_PARK, 100u);
        s_f.rd.flags = 0u;
        feed_state(LINK_ESC_OPEN, LINK_ESC_LINE_PARK, 100u);
    } else if (strcmp(view, "motor-wire-held") == 0) {
        s_f.rd.flags = 0u;
        feed_state(LINK_ESC_OPEN, LINK_ESC_LINE_PARK, 100u);
        esc_wire_panel_model()->msg = ESC_PROG_MSG_REFUSED;
        esc_wire_panel_model()->detail = LINK_ESC_REF_NO_SUPPLY;
    } else if (strcmp(view, "motor-wire-held-hand") == 0) {
        tap(esc_wire_panel_value_rect(1));
        tap(esc_wire_panel_choice_rect(1));
        s_f.rd.flags = 0u;
        feed_state(LINK_ESC_OPEN, LINK_ESC_LINE_PARK, 100u);
        esc_wire_panel_model()->msg = ESC_PROG_MSG_ABORTED;
    } else if (strcmp(view, "motor-wire-not-back") == 0) {
        s_f.rd.result = LINK_ESC_RESULT_NOT_BACK;
        feed_state(LINK_ESC_NOT_BACK, LINK_ESC_LINE_PARK, 100u);
        esc_wire_panel_model()->msg = ESC_PROG_MSG_NOT_BACK;
    } else if (strcmp(view, "motor-wire-release") == 0) {
        s_f.rd.state = LINK_ESC_OPEN;
        s_f.rd.held = true;
        s_f.rd.ours = false;
        s_f.rd.gpio = 13u;
        feed();
        tap(esc_wire_panel_action_rect(0));
    } else if (strcmp(view, "motor-wire-other") == 0) {
        s_f.rd.state = LINK_ESC_OPEN;
        s_f.rd.held = true;
        s_f.rd.ours = false;
        s_f.rd.gpio = 13u;
        feed();
    } else if (strncmp(view, "motor-wire-am", 13) == 0) {
        feed_read(EW_FW_AM, 4u, LINK_EW_F_WRITABLE, 0u);
        const char *rest = view + 13;

        if (strcmp(rest, "-motor") == 0) {
            tap(esc_wire_panel_tab_rect(1));
        } else if (strcmp(rest, "-brake") == 0) {
            tap(esc_wire_panel_tab_rect(2));
        } else if (strcmp(rest, "-input") == 0) {
            tap(esc_wire_panel_tab_rect(3));
        } else if (strcmp(rest, "-protect") == 0) {
            tap(esc_wire_panel_tab_rect(4));
        } else if (strcmp(rest, "-choice") == 0) {
            tap(esc_wire_panel_tab_rect(1));
            tap(esc_wire_panel_value_rect(6));
        } else if (strcmp(rest, "-number") == 0) {
            tap(esc_wire_panel_tab_rect(1));
            tap(esc_wire_panel_value_rect(0));
        } else if (strcmp(rest, "-write") == 0) {
            /* TEMPERATURE LIMIT and INPUT TYPE, second-look fields. */
            tap(esc_wire_panel_tab_rect(4));
            tap(esc_wire_panel_value_rect(3));
            tap(esc_wire_panel_choice_rect(0));
            tap(esc_wire_panel_tab_rect(3));
            tap(esc_wire_panel_value_rect(1));
            tap(esc_wire_panel_choice_rect(2));
            open_write();
        } else if (strcmp(rest, "-long") == 0 || strcmp(rest, "-last") == 0) {
            /* Every field at its highest, then the rows of 4 tabs moved off it:
             * more than a page, second-look fields first. */
            const ew_field_t *fd;

            for (size_t i = 0u; (fd = ew_map_nth(EW_FW_AM, 4u, i)) != NULL; ++i) {
                if (fd->access == (uint8_t)EW_ACC_EDIT) {
                    s_f.image.page[fd->off] = fd->hi;
                }
            }
            s_f.image.gen = 2u;
            feed();
            for (int tab = 1; tab <= 4; ++tab) {
                tap(esc_wire_panel_tab_rect(tab));
                for (int row = 0; row < 12 && esc_wire_panel_value_rect(row).w > 0; ++row) {
                    change_row(row);
                }
            }
            open_write();
            while (strcmp(rest, "-last") == 0 && esc_wire_panel_hold_rect().w == 0) {
                tap(esc_wire_panel_next_rect());
            }
        } else if (strcmp(rest, "-card") == 0) {
            /* The hold done, the card not answered. */
            tap(esc_wire_panel_tab_rect(4));
            tap(esc_wire_panel_value_rect(3));
            tap(esc_wire_panel_choice_rect(0));
            open_write();
            hold_down_2s();
        } else if (strcmp(rest, "-leave") == 0) {
            tap(esc_wire_panel_back_rect());
        } else if (strcmp(rest, "-running") == 0) {
            s_f.rd.flags |= LINK_EW_F_WRITING;
            feed_state(LINK_ESC_RUN, LINK_ESC_LINE_SEND, 2600u);
        }
    } else if (strcmp(view, "motor-wire-erased") == 0) {
        feed_read(EW_FW_AM, 0u, LINK_EW_F_ERASED, EW_WHY_ERASED);
        esc_wire_panel_model()->msg = ESC_PROG_MSG_READ_DONE;
    } else if (strcmp(view, "motor-wire-no-backup") == 0) {
        feed_read(EW_FW_AM, 4u, LINK_EW_F_WRITABLE, 0u);
        tap(esc_wire_panel_tab_rect(4));
        tap(esc_wire_panel_value_rect(3));
        tap(esc_wire_panel_choice_rect(0));
        open_write();
        hold_down_2s();
        card_answer(ESC_CARD_DONE_NO_CARD);
    } else if (strcmp(view, "motor-wire-files") == 0) {
        feed_read(EW_FW_AM, 4u, LINK_EW_F_WRITABLE, 0u);
        tap(esc_wire_panel_action_rect(1));
        card_answer(ESC_CARD_DONE_OK);
    } else if (strcmp(view, "motor-wire-restore") == 0) {
        feed_read(EW_FW_AM, 0u, LINK_EW_F_ERASED, EW_WHY_ERASED);
        tap(esc_wire_panel_action_rect(0));
        card_answer(ESC_CARD_DONE_OK);
        tap(esc_wire_panel_choice_rect(0));
        card_answer(ESC_CARD_DONE_OK);
    } else if (strcmp(view, "motor-wire-erased-leave") == 0) {
        feed_read(EW_FW_AM, 0u, LINK_EW_F_ERASED, EW_WHY_ERASED);
        tap(esc_wire_panel_back_rect());
    } else if (strcmp(view, "motor-wire-s") == 0) {
        feed_read(EW_FW_S, 33u, LINK_EW_F_WRITABLE, 0u);
        tap(esc_wire_panel_tab_rect(1));
    } else if (strcmp(view, "motor-wire-bj") == 0) {
        feed_read(EW_FW_BJ, 208u, LINK_EW_F_WRITABLE, 0u);
        tap(esc_wire_panel_tab_rect(1));
    } else if (strcmp(view, "motor-wire-readonly") == 0) {
        feed_read(EW_FW_BJ, 204u, 0u, EW_WHY_OLD_LAYOUT);
    } else if (strcmp(view, "motor-wire-readonly-rows") == 0) {
        feed_read(EW_FW_BJ, 204u, 0u, EW_WHY_OLD_LAYOUT);
        tap(esc_wire_panel_tab_rect(1));
    } else if (strcmp(view, "motor-wire-hex") == 0) {
        feed_read(EW_FW_S, 34u, 0u, EW_WHY_OLD_LAYOUT);
        tap(esc_wire_panel_tab_rect(1));
    } else if (strncmp(view, "motor-wire-e32", 14) == 0) {
        feed_read(EW_FW_E32, 17u, LINK_EW_F_WRITABLE, 0u);
        const char *rest = view + 14;

        if (strcmp(rest, "-leave") == 0) {
            tap(esc_wire_panel_back_rect());
        } else if (rest[0] == '-' && rest[1] >= '1' && rest[1] <= '6') {
            tap(esc_wire_panel_tab_rect(rest[1] - '0'));
        }
    }
    ui_router_tick(0.02f);
}
