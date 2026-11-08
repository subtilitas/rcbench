/**
 * @file render_rudder.c
 * @brief SERVO's RUDDER views for tools/render_ui.py (render_rudder.h).
 *
 * The AS5600 is on with its center at count 2048 and reads through
 * servo_screen_encoder() as the panel hands it over.  ZERO is taken at
 * count 2048; the surface then goes to -24.6° and +25.5°, which MAX holds,
 * and comes back to +12.3°.  LENGTH is 120mm, or not set in
 * servo-rudder-nolength.  servo-rudder-armed is ARMED with 2000us tapped.
 * servo-rudder-85 then goes to -85.0°, which MAX takes as its negative side.
 * The servo-rudder views are on RUDDER's screen, the servo-rudder-auto
 * views on RUDDER AUTO's (render_screen.c shows it).  servo-rudder-auto is
 * SAFE on the bench's outputs: READY's PORT row red, the hold grey.
 * servo-rudder-auto-safe is the same, the view help-servo-rudder-auto
 * opens SERVO's guide over (render_help.c); help-servo-rudder opens it
 * over servo-rudder.  servo-rudder-auto-settings is its SETTINGS list.
 * The -rbmod views are on the rbmod carrier, the target PROG PORT 1 (GPIO
 * 13), the INA3221 at 0x40 reading the ports' CH3, PORT PWM and the PROG
 * 5V off: servo-rudder-auto-rbmod SAFE with the hold offered, PROG 5V,
 * PORT PWM and ARM amber; servo-rudder-auto-start-rbmod after the hold,
 * the sequence waiting for the PROG 5V's read-back; and
 * servo-rudder-auto-failed-rbmod 1100ms later, the read-back failed, the
 * bench SAFE.  The servo-rudder-angle views tap RUDDER's ANGLE row with the
 * IMU on and Bluetooth LE running: servo-rudder-angle with two sensors
 * found, servo-rudder-angle-many with six, a public and a random address
 * of the same last bytes among them, in two columns of entries on two
 * lines, servo-rudder-angle-scan with a scan under way, and
 * servo-rudder-angle-none with the AS5600 off in SETUP and a scan that
 * found none.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_rudder.h"

#include <stdio.h>
#include <string.h>

#include "imu_screen.h"
#include "servo_screen.h"
#include "servo_spots.h"
#include "servo_test.h"
#include "link_pages.h"
#include "prog_choice.h"
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

/**
 * @brief A sensor of the IMU's found list.
 * @param d    the device
 * @param addr its address, colons
 * @param name its name
 * @param rssi its RSSI, dBm
 */
static void sensor(imu_dev_t *d, const char *addr, const char *name,
                   int8_t rssi)
{
    snprintf(d->addr, sizeof(d->addr), "%s", addr);
    snprintf(d->name, sizeof(d->name), "%s", name);
    d->rssi = rssi;
    d->flags = IMU_DEV_UUID_18F0;
}

/**
 * @brief The servo-rudder-angle views: the IMU's view handed over, then
 *        RUDDER's ANGLE row tapped.
 * @param view the view
 */
static void angle_list(const char *view)
{
    imu_view_t v;
    memset(&v, 0, sizeof(v));
    v.link = IMU_LINK_READY;
    snprintf(v.version, sizeof(v.version), "2.13.0");
    settings_set(SET_IMU_EN, 1.0f);
    if (strcmp(view, "servo-rudder-angle") == 0) {
        v.found_n = 2u;
        sensor(&v.found[0], "00:0C:BF:08:19:29", "HC-06", -50);
        sensor(&v.found[1], "D4:2B:7E:51:0C:A3", "BWT901CL", -71);
    } else if (strcmp(view, "servo-rudder-angle-many") == 0) {
        static const char *const k_addr[6] = {
            "00:0C:BF:08:19:29", "D4:2B:7E:51:19:29", "D4:2B:7E:51:0C:A3",
            "00:0C:BF:0A:44:17", "00:0C:BF:0A:51:06", "C8:47:8C:12:7E:90",
        };
        static const char *const k_name[6] = {
            "HC-06", "HC-06", "BWT901CL", "", "HC-06", "WT901BLE68",
        };
        v.found_n = 6u;
        for (int i = 0; i < 6; ++i) {
            sensor(&v.found[i], k_addr[i], k_name[i], (int8_t)(-48 - 7 * i));
        }
        v.found[1].addr_type = 1u;
        v.found[5].addr_type = 1u;
    } else if (strcmp(view, "servo-rudder-angle-scan") == 0) {
        v.link = IMU_LINK_SCANNING;
    } else {
        settings_set(SET_ENC_EN, 0.0f);
        v.err = IMU_ERR_NONE_FOUND;
    }
    imu_screen_set(&v);
    tap_body(826, 96 + 30 + 15);            /* READY's ANGLE row */
    imu_cmd_t c;
    while (imu_screen_take(&c)) { }
}

bool render_rudder_view(const char *view)
{
    return strncmp(view, "servo-rudder", 12) == 0;
}

bool render_rudder_armed(const char *view)
{
    return strcmp(view, "servo-rudder-armed") == 0;
}

void render_rudder_pose(const char *view)
{
    if (!render_rudder_view(view)) {
        return;
    }
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTER, 2048.0f);
    settings_set(SET_RUDDER_LEN_MM,
                 strcmp(view, "servo-rudder-nolength") == 0 ? 0.0f : 120.0f);
    servo_screen_set_link(true);
    /* RUDDER's screen or RUDDER AUTO's, as the view's screen
     * (render_screen.c) has it shown. */
    reading(2048u, 1000u);
    tap_body(140, SV_RUD_ZERO_Y);           /* ZERO */
    reading(2048u - 280u, 1040u);           /* -24.6deg */
    reading(2048u + 290u, 1080u);           /* +25.5deg */
    reading(2048u + 140u, 1120u);           /* +12.3deg */
    if (strcmp(view, "servo-rudder-85") == 0) {
        reading(2048u - 967u, 1160u);       /* -85.0deg */
    }
    if (strcmp(view, "servo-rudder-armed") == 0) {
        servo_screen_set_armed(true);
        tap_body(438, 204);                 /* 2000us */
    }
    if (strstr(view, "servo-rudder-auto") == view
        && strstr(view, "-rbmod") != NULL) {
        prog_choice_set_far_minor(LINK_MINOR_CARRIER_PWM);
        settings_set(SET_SERVO_TARGET, 1.0f);
        settings_set(SET_SERVO_PORT_PWM, 0.0f);
        servo_screen_set_drive(1u, 13u, false);
    }
    if (strcmp(view, "servo-rudder-auto-settings") == 0) {
        tap_body(SV_FOOT_SET_X, SV_FOOT_Y);
    }
    if (strstr(view, "servo-rudder-angle") == view) {
        angle_list(view);
    }
    servo_cmd_t c;
    while (servo_screen_take(&c)) { }
}
