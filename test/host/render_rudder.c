/**
 * @file render_rudder.c
 * @brief SERVO's RUDDER views for tools/render_ui.py (render_rudder.h).
 *
 * The AS5600 is on with its centre at count 2048 and reads through
 * servo_screen_encoder() as the panel hands it over.  ZERO is taken at
 * count 2048; the surface then goes to -24.6° and +25.5°, which MAX holds,
 * and comes back to +12.3°.  LENGTH is 120mm, or not set in
 * servo-rudder-nolength.  servo-rudder-armed is ARMED with 2000us tapped.
 * servo-rudder-auto is ARMED with AUTO's box shown: no KST servo on the
 * programming port, which its first line names.  help-servo-rudder is the
 * task's guide page over the same card, SAFE.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_rudder.h"

#include <string.h>

#include "servo_screen.h"
#include "servo_spots.h"
#include "servo_test.h"
#include "settings.h"
#include "ui_layout.h"
#include "ui_screen.h"

/**
 * @brief A tap of contact 1 at (@p x, @p y) of the body, through the
 *        router.
 * @param x the body's x
 * @param y the body's y
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

/**
 * @brief The AS5600's reading at count @p raw, as the panel hands it over.
 * @param raw the count, 0 to 4095
 * @param at_ms its time
 */
static void reading(uint16_t raw, uint32_t at_ms)
{
    const servo_test_enc_t e = { .valid = true, .raw = raw,
                                 .taken_ms = at_ms };
    servo_screen_encoder(&e);
}

bool render_rudder_view(const char *view)
{
    return strncmp(view, "servo-rudder", 12) == 0
           || strncmp(view, "help-servo-rudder", 17) == 0;
}

bool render_rudder_armed(const char *view)
{
    return strcmp(view, "servo-rudder-armed") == 0
           || strcmp(view, "servo-rudder-auto") == 0;
}

void render_rudder_pose(const char *view)
{
    if (!render_rudder_view(view)) {
        return;
    }
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, 2048.0f);
    settings_set(SET_RUDDER_LEN_MM,
                 strcmp(view, "servo-rudder-nolength") == 0 ? 0.0f : 120.0f);
    servo_screen_set_link(true);
    tap_body(SV_TAB_RUDDER_X, SV_TAB_Y);
    reading(2048u, 1000u);
    tap_body(140, 312);                     /* ZERO */
    reading(2048u - 280u, 1040u);           /* -24.6deg */
    reading(2048u + 290u, 1080u);           /* +25.5deg */
    reading(2048u + 140u, 1120u);           /* +12.3deg */
    if (strcmp(view, "servo-rudder-armed") == 0) {
        servo_screen_set_armed(true);
        tap_body(438, 252);                 /* 2000us */
    }
    if (strcmp(view, "servo-rudder-auto") == 0) {
        servo_screen_set_armed(true);
        tap_body(SV_RUD_AUTO_X, SV_RUD_ZERO_Y);   /* AUTO */
    }
    servo_cmd_t c;
    while (servo_screen_take(&c)) { }
}
