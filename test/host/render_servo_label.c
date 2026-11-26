/**
 * @file render_servo_label.c
 * @brief The SERVO LABEL views (render_servo_label.h).
 *
 * The card holds 7 labelled servos, KST_X10-00007 the highest, so SERVO
 * ID's list shows NONE, the 6 highest and TYPE ID, 8 rows, and NEW ID
 * reads NEXT KST_X10-00008.  KST_X10-00005 is active, picked.
 *
 * - servo-label-rows: the hub's list scrolled to the block.
 * - servo-label-dut: the hub's list from TRAVEL down, the rows above it.
 * - servo-label-pick: SERVO ID's choice list open.
 * - servo-label-type: TYPE ID's keyboard, KST_X10-0000A typed.
 * - servo-label-refused: the same, KST_X10-0000 refused on OK: the
 *   serial's line.
 * - servo-label-print: the block with the T50M Pro bound and idle: PRINT
 *   names it.
 * - servo-label-printing: KST_X10-00005's label in SENDING, 3 of 5 frames.
 * - servo-label-printed: the label printed, 292 left: PRINT AGAIN.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_servo_label.h"

#include <stdio.h>
#include <string.h>

#include "print_ctl.h"
#include "servo_active.h"
#include "servo_label_rows.h"
#include "servo_screen.h"
#include "settings.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_setlist.h"
#include "ui_textkey.h"

/** @brief The hub list's SERVO LABEL heading: the profile's 14 rows
 *         above it. */
#define LABEL_ROW 14

/** @brief The card's ids: 7 servos, the highest first. */
static bool card_ids(servo_ids_t *out)
{
    static const char *const k_dirs[] = {
        "KST_X10-00001", "KST_X10-00002", "KST_X10-00003", "KST_X10-00004",
        "KST_X10-00005", "KST_X10-00006", "KST_X10-00007",
    };
    servo_ids_begin(out);
    for (size_t i = 0; i < sizeof(k_dirs) / sizeof(k_dirs[0]); ++i) {
        servo_ids_visit(out, k_dirs[i], true);
    }
    return true;
}

/** @brief NEW ID's request: the views make none. */
static bool new_id(const char *prefix)
{
    (void)prefix;
    return true;
}

/** @brief The views' card. */
static const servo_label_io_t k_io = { .ids = card_ids, .new_id = new_id };

/**
 * @brief A tap of contact 1 at (@p x, @p y) of the body, through the
 *        router.
 */
static void tap_body(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)(y + UI_BAND_H),
                                   .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/** @brief A tap in the middle of @p r, a body rectangle. */
static void tap_rect(gfx_rect_t r)
{
    tap_body(r.x + r.w / 2, r.y + r.h / 2);
}

/** @brief @p text typed on the list's keyboard, key by key. */
static void type(const char *text)
{
    const ui_textkey_t *k = &servo_screen_list()->tk;
    for (const char *p = text; *p != '\0'; ++p) {
        for (int key = 0; key < UI_TEXTKEY_CHARS; ++key) {
            const char *l = ui_textkey_label(key);
            if (l != NULL && l[0] == *p && l[1] == '\0') {
                tap_rect(ui_textkey_key_rect(k, key));
                break;
            }
        }
    }
}

/**
 * @brief The PRINT views: the T50M Pro bound, and its view for @p view.
 * @param view servo-label-print, -printing or -printed.
 */
static void pose_print(const char *view)
{
    settings_set_text(SET_TEXT_PRN_ADDR, "A4:93:40:30:FA:71 P");
    settings_set_text(SET_TEXT_PRN_NAME, "T0205C2608247368");
    print_view_t v;
    memset(&v, 0, sizeof(v));
    v.bound = true;
    v.left = 293;
    v.w_mm = 40u;
    v.h_mm = 30u;
    v.gap_mm = 3u;
    if (strcmp(view, "servo-label-print") != 0) {
        v.kind = PRINT_REQ_LABEL;
        v.linked = true;
        v.jobs = 1u;
        snprintf(v.id, sizeof(v.id), "KST_X10-00005");
        v.phase = PRINT_SENDING;
        v.frame = 3u;
        v.frames = 5u;
    }
    if (strcmp(view, "servo-label-printed") == 0) {
        v.phase = PRINT_DONE;
        v.left = 292;
        servo_active_set_printed();
    }
    v.rev = 1u;
    print_ctl_show(&v);
}

bool render_servo_label_view(const char *view)
{
    return strncmp(view, "servo-label-", 12) == 0;
}

void render_servo_label_pose(const char *view)
{
    if (!render_servo_label_view(view)) {
        return;
    }
    servo_label_set_io(&k_io);
    settings_set_text(SET_TEXT_SERVO_PREFIX, "KST_X10");
    (void)servo_active_set("KST_X10-00005", SERVO_ACTIVE_PICK, false);
    ui_setlist_t *sl = (ui_setlist_t *)servo_screen_list();
    /* The block's heading at the pane's top. */
    ui_setlist_scroll_to(sl, LABEL_ROW + SVL_ROW_PRINT);
    ui_setlist_scroll_to(sl, LABEL_ROW);
    if (strcmp(view, "servo-label-rows") == 0) {
        return;
    }
    if (strncmp(view, "servo-label-print", 17) == 0) {
        pose_print(view);
        return;
    }
    if (strcmp(view, "servo-label-dut") == 0) {
        ui_setlist_scroll_to(sl, 0);
        ui_setlist_scroll_to(sl, LABEL_ROW - 1);
        ui_setlist_scroll_to(sl, LABEL_ROW - 7);   /* TRAVEL at the top */
        return;
    }
    const gfx_rect_t id = ui_setlist_row_rect(sl, LABEL_ROW + SVL_ROW_ID);
    tap_body(id.x + 60, id.y + id.h / 2);
    if (strcmp(view, "servo-label-pick") == 0) {
        return;
    }
    tap_rect(ui_setlist_choice_rect(sl, servo_label_choices() - 1));
    if (strcmp(view, "servo-label-type") == 0) {
        type("0000A");
        return;
    }
    type("0000");
    tap_rect(ui_textkey_key_rect(&sl->tk, UI_TK_OK));
}
