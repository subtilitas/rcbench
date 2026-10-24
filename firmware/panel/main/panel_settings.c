/**
 * @file panel_settings.c
 * @brief The settings the control task follows, handed across from
 *        app_main.
 *
 * Each value the settings screen writes is published into an atomic or
 * under a spinlock, beside the debt its write to the IOmcu is, and the
 * control task reads it from there.  settings_changed() is the settings
 * model's observer; bring_up() (panel_boot.c) publishes every value once.
 * Screen rotation goes to the display (apply_rotation()), whose flush
 * turns picture and touch together from the next frame.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>

#include "card_tree.h"
#include "display.h"
#include "endpoints_link.h"
#include "freertos/FreeRTOS.h"
#include "panel.h"
#include "panel_main.h"
#include "settings.h"
#include "settings_sensors.h"
#include "tone_link.h"

/*
 * A setting the far end keeps a copy of has changed.
 *
 * The settings screen runs on app_main and the link belongs to the control
 * task, so this publishes the value and records the debt; the write goes out
 * from there.  Motor poles is the only setting the IOmcu holds a copy
 * of.
 *
 * The value is stored before the flag, so a control task that sees the debt
 * sees at least this value and never an older one.
 */
void publish_poles(void)
{
    atomic_store(&s_poles_value,
                 (unsigned)settings_get_int(SET_MOTOR_POLES));
    atomic_store(&s_poles_owed, true);
}

/*
 * The throttle's pulse endpoints, Idle pulse and Full pulse, on their way to
 * the IOmcu.
 *
 * Published the way the pole count is, and for the same reason: the settings
 * screen writes the model from app_main and the control task owns the link,
 * so the values travel in atomics beside the debt rather than being read
 * from settings.c on the control task (endpoints_link.h).
 *
 * The time of the last edit travels too.  A key held on + or - steps the
 * value every few hundred milliseconds, and each write of CHAN_CFG is a
 * flash write at the far end; the debt is paid once the value has rested for
 * ENDPOINTS_SETTLE_MS.
 *
 * Its hold is raised at a link-up edge, control task only: the far end is
 * held disarmed until its throttle endpoints have been brought to the
 * settings.  A bench armed with no link -- the simulator, or a cable pulled
 * mid-run -- would otherwise arm an IOmcu that has just answered on
 * whatever range it kept in flash, and keep that range for the whole run.
 */
endpoints_debt_t s_endpoints;

void publish_endpoints(void)
{
    endpoints_publish(&s_endpoints,
                      (uint16_t)settings_get_int(SET_OUT_MIN_US),
                      (uint16_t)settings_get_int(SET_OUT_MAX_US), now_ms());
}

/* The PD mini's wiring, as the settings screen left it; see
 * s_pdmini_wiring. */
void publish_pdmini(void)
{
    const int tx = settings_get_int(SET_PDMINI_TX);
    const int rx = settings_get_int(SET_PDMINI_RX);
    const unsigned word =
        ((settings_get_int(SET_PDMINI_EN) != 0) ? (1u << 24) : 0u)
        | ((unsigned)((tx < 0) ? 0 : tx + 1) & 0xFFu) << 16
        | ((unsigned)((rx < 0) ? 0 : rx + 1) & 0xFFu) << 8
        | ((unsigned)settings_get_int(SET_PDMINI_BAUD) & 0xFFu);
    /* The count first: an ON stamped before it is dropped by the time the
     * new wiring can be seen. */
    atomic_fetch_add(&s_pdmini_edits, 1u);
    atomic_store(&s_pdmini_wiring, word);
}

/*
 * The phase tap's set-up, as SETUP INTERFACES left it, for the control
 * task's TONE writes (tone_link.h).  Seven values do not fit an atomic
 * word, so the copy is taken under a spinlock both ways, as the current
 * monitors' is.  Written by the settings observer on app_main, read once a
 * poll by the control task.
 */
static portMUX_TYPE  s_tone_mux = portMUX_INITIALIZER_UNLOCKED;
static tone_setup_t  s_tone_want;

void publish_tone(void)
{
    const tone_setup_t w = {
        .enable      = settings_get_bool(SET_TONE_EN),
        .pin         = (uint8_t)settings_get_int(SET_TONE_PIN),
        .f_min_hz    = (uint16_t)settings_get_int(SET_TONE_F_MIN),
        .f_max_hz    = (uint16_t)settings_get_int(SET_TONE_F_MAX),
        .split_pct   = (uint8_t)settings_get_int(SET_TONE_SPLIT),
        .gap_ms      = (uint8_t)settings_get_int(SET_TONE_GAP),
        .min_periods = (uint8_t)settings_get_int(SET_TONE_PERIODS),
    };
    taskENTER_CRITICAL(&s_tone_mux);
    s_tone_want = w;
    taskEXIT_CRITICAL(&s_tone_mux);
}

tone_setup_t tone_wanted(void)
{
    taskENTER_CRITICAL(&s_tone_mux);
    const tone_setup_t w = s_tone_want;
    taskEXIT_CRITICAL(&s_tone_mux);
    return w;
}

/*
 * AUTO LOGS on SETUP > SD CARD, for the control task, which decides whether
 * a run is recorded (log_follow_runs()).  Written by the settings observer
 * on app_main.
 */
atomic_bool s_log_auto = true;

void publish_log_auto(void)
{
    atomic_store(&s_log_auto, settings_get_bool(SET_LOG_AUTO));
}

/*
 * AUTO LOG FOLDER and USER LOG FOLDER, for the logger: the folder in force
 * when card_folder_check() takes it, the default folder otherwise, so a
 * store that holds a folder no longer allowed writes to the default.
 * Written by the text observer on app_main.
 */
void publish_log_folder(setting_text_id_t id)
{
    char dir[CARD_DIR_MAX];
    if (id == SET_TEXT_LOG_AUTO_DIR) {
        log_set_folder(LOG_FOLDER_AUTO,
                       card_folder_use(settings_text(id), CARD_AUTO_LOGS,
                                       dir, sizeof(dir)));
    } else if (id == SET_TEXT_LOG_USER_DIR) {
        log_set_folder(LOG_FOLDER_USER,
                       card_folder_use(settings_text(id), CARD_USER_LOGS,
                                       dir, sizeof(dir)));
    }
}

/* Hitec D receive to the control task (s_hitec_rx_want), at the load and
 * at every edit, so the first write after a link-up is the kept mode. */
void publish_hitec_rx(void)
{
    atomic_store(&s_hitec_rx_want, (unsigned)settings_get_int(SET_HITEC_RX));
}

/**
 * @brief Screen rotation to the display.  The next flip takes it: the
 *        picture turns in the flush, and touch follows the frame on the
 *        glass (display.h).  At the load, before display_init(), the
 *        display keeps it for its start.
 */
static void apply_rotation(void)
{
    display_set_turned(settings_get_int(SET_SCREEN_ROT) != 0);
}

void settings_changed(setting_id_t id)
{
    if (id == SET_SCREEN_ROT) {
        apply_rotation();
    } else if (id == SET_LOG_AUTO) {
        publish_log_auto();
    } else if (id == SET_MOTOR_POLES) {
        publish_poles();
    } else if (id == SET_OUT_MIN_US || id == SET_OUT_MAX_US) {
        publish_endpoints();
    } else if (id == SET_PDMINI_EN || id == SET_PDMINI_TX
               || id == SET_PDMINI_RX || id == SET_PDMINI_BAUD) {
        publish_pdmini();
    } else if (id == SET_HITEC_RX) {
        publish_hitec_rx();
    } else if (settings_sensors_row(id)) {
        publish_sense();          /* SETUP > SENSORS */
    } else {
        switch (id) {
        case SET_INA228_EN:  case SET_INA228_ADDR:  case SET_INA228_UOHM:
        case SET_INA228_MAX_A:
        case SET_SENSE_SDA:  case SET_SENSE_SCL:
        case SET_ENC_EN:     case SET_SERVO_TARGET:
            publish_sense();
            break;
        case SET_TONE_EN:    case SET_TONE_PIN:   case SET_TONE_F_MIN:
        case SET_TONE_F_MAX: case SET_TONE_SPLIT: case SET_TONE_GAP:
        case SET_TONE_PERIODS:
            publish_tone();
            break;
        default:
            break;
        }
    }
}
