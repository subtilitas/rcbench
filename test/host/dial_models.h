/**
 * @file
 * @brief The Dial faces' fixed models: the panel's packets for each face,
 *        fed through dk_face_feed() as the Dial takes them, with the
 *        Dial's own state beside them.  The figures are the approved
 *        mockups'.  Shared by test_dial_face, test_dial_fit and
 *        render_dial (the committed images).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string.h>

#include "dial_face.h"
#include "dialknob.h"

/** @brief The faces with a fixed model, in the committed images' order. */
typedef enum {
    DM_NAVIGATION = 0,
    DM_VALUE_SET,
    DM_SERVO_LIVE,
    DM_MOTOR_ARMED,
    DM_MOTOR_SAFE,
    DM_LINK_LOST,
    DM_LINK_ENDED,
    DM_WAITING,
    DM_BIND,
    DM_OTHER,
    DM_UPDATE_ASK,
    DM_UPDATE_SEND,
    DM_COUNT
} dial_model_id_t;

/** @brief The models' names: the image files are dial-<name>.png. */
static const char *const k_dial_model_name[DM_COUNT] = {
    "navigation", "value-set", "servo-live", "motor-armed", "motor-safe",
    "link-lost", "link-ended", "waiting", "bind", "other", "update-ask",
    "update-send",
};

/** @brief A model and the view beside it. */
typedef struct {
    dk_face_t        face; /**< the panel's model */
    dial_face_view_t view; /**< the Dial's state */
    uint8_t          seq;  /**< the next packet's seq */
} dial_model_t;

/** @brief Feed one encoded packet. @param m the model
 *  @param b the bytes @param n their count @return taken */
static inline bool dm_feed(dial_model_t *m, const uint8_t *b, size_t n)
{
    return n != 0u && dk_face_feed(&m->face, b, n, 1000u);
}

/** @brief Feed a SCREEN. @param m the model @param face dk_face_kind_t
 *  @param lang DK_LANG_* @param title the title @return taken */
static inline bool dm_screen(dial_model_t *m, uint8_t face, uint8_t lang,
                             const char *title)
{
    dk_screen_t s;
    uint8_t b[DK_LEN_SCREEN];
    memset(&s, 0, sizeof s);
    s.seq = m->seq++;
    s.face = face;
    s.lang = lang;
    s.flags = DK_SCR_SOUND;
    dk_text_copy(s.title, sizeof s.title, title);
    const size_t n = dk_screen_encode(&s, b);
    return dm_feed(m, b, n);
}

/** @brief Feed a STATE. @param m the model @param flags DK_STA_*
 *  @return taken */
static inline bool dm_state(dial_model_t *m, uint8_t flags)
{
    dk_state_t s = { m->seq++, 123456u, 10, 10, flags };
    uint8_t b[DK_LEN_STATE];
    const size_t n = dk_state_encode(&s, b);
    return dm_feed(m, b, n);
}

/** @brief Feed a NAV row. @param m the model @param index the focused
 *  index @param count the rows @param slot DK_SLOT_* @param item its text
 *  @return taken */
static inline bool dm_nav(dial_model_t *m, uint8_t index, uint8_t count,
                          uint8_t slot, const char *item)
{
    dk_nav_t n;
    uint8_t b[DK_LEN_NAV];
    memset(&n, 0, sizeof n);
    n.seq = m->seq++;
    n.index = index;
    n.count = count;
    n.slot = slot;
    dk_text_copy(n.item, sizeof n.item, item);
    const size_t len = dk_nav_encode(&n, b);
    return dm_feed(m, b, len);
}

/** @brief Feed a VALUE_META and a VALUE. @param m the model
 *  @param unit the unit @param dec decimals @param min the lowest
 *  @param max the highest @param step the step @param value the value
 *  @param was the value on entry @param flags DK_VAL_* @return taken */
static inline bool dm_value(dial_model_t *m, const char *unit, uint8_t dec,
                            int32_t min, int32_t max, int32_t step,
                            int32_t value, int32_t was, uint8_t flags)
{
    dk_value_meta_t meta;
    dk_value_t v = { 0, value, was, flags };
    uint8_t b[DK_PKT_MAX];
    memset(&meta, 0, sizeof meta);
    meta.seq = m->seq++;
    dk_text_copy(meta.unit, sizeof meta.unit, unit);
    meta.decimals = dec;
    meta.min = min;
    meta.max = max;
    meta.step = step;
    const size_t n = dk_value_meta_encode(&meta, b);
    if (!dm_feed(m, b, n)) {
        return false;
    }
    v.seq = m->seq++;
    const size_t k = dk_value_encode(&v, b);
    return dm_feed(m, b, k);
}

/** @brief Feed a LIVE_MOTOR. @param m the model @param lm the readings,
 *  seq set here @return taken */
static inline bool dm_motor(dial_model_t *m, dk_live_motor_t lm)
{
    uint8_t b[DK_LEN_LIVE_MOTOR];
    lm.seq = m->seq++;
    const size_t n = dk_live_motor_encode(&lm, b);
    return dm_feed(m, b, n);
}

/** @brief Feed a LIVE_SERVO. @param m the model @param ls the readings,
 *  seq set here @return taken */
static inline bool dm_servo(dial_model_t *m, dk_live_servo_t ls)
{
    uint8_t b[DK_LEN_LIVE_SERVO];
    ls.seq = m->seq++;
    const size_t n = dk_live_servo_encode(&ls, b);
    return dm_feed(m, b, n);
}

/** @brief Feed a SCALE. @param m the model @param sc the full scales,
 *  seq set here @return taken */
static inline bool dm_scale(dial_model_t *m, dk_scale_t sc)
{
    uint8_t b[DK_LEN_SCALE];
    sc.seq = m->seq++;
    const size_t n = dk_scale_encode(&sc, b);
    return dm_feed(m, b, n);
}

/** @brief The mockups' full scales: rpm the run's peak, 30V and 80A for
 *  the motor; 45° of travel, 500 to 2,500us, 8.4V and 5A for the servo. */
static inline dk_scale_t dm_scales(void)
{
    dk_scale_t sc = { 0, 14591, 30000, 8000, 450, 500, 2500, 8400, 500 };
    return sc;
}

/**
 * @brief Build a fixed model.
 * @param m    the model
 * @param id   dial_model_id_t
 * @param lang DK_LANG_*
 */
static inline void dial_model(dial_model_t *m, int id, uint8_t lang)
{
    const bool de = lang == DK_LANG_DE;
    memset(m, 0, sizeof *m);
    dk_face_init(&m->face);
    m->seq = 1;
    m->view.ready = true;
    m->view.bound = true;
    m->view.name = "DIAL-3A10";
    m->view.addr = "A0:B1:C2:7F:3A:10";
    m->view.rssi = -64;
    m->view.batt_mv = 3920;
    switch (id) {
    case DM_NAVIGATION:
        dm_screen(m, DK_FACE_NAV, lang, "SETUP");
        dm_nav(m, 3, 9, DK_SLOT_PREV, "OUTPUTS");
        dm_nav(m, 3, 9, DK_SLOT_FOCUS, de ? "SENSOREN" : "SENSORS");
        dm_nav(m, 3, 9, DK_SLOT_NEXT, "PANEL");
        dm_state(m, 0);
        break;
    case DM_VALUE_SET:
        dm_screen(m, DK_FACE_VALUE, lang, de ? "DIMMEN NACH" : "DIM AFTER");
        dm_value(m, "min", 0, 0, 60, 1, 5, 5, DK_VAL_SET);
        dm_state(m, 0);
        break;
    case DM_SERVO_LIVE: {
        const dk_live_servo_t ls = {
            0, DK_LIVE_ARMED | DK_LIVE_DRIVING | DK_LIVE_MEAS | DK_LIVE_RAIL,
            1625, 125, 1230, 6020, 41, 25, 0, 0 };
        dm_screen(m, DK_FACE_SERVO, lang, "SERVO LIVE");
        dm_scale(m, dm_scales());
        dm_servo(m, ls);
        dm_state(m, DK_STA_ARMED);
        break;
    }
    case DM_MOTOR_ARMED: {
        const dk_live_motor_t lm = {
            0, DK_LIVE_ARMED | DK_LIVE_DRIVING,
            DK_MV_RPM | DK_MV_VOLT | DK_MV_CURR | DK_MV_POWER | DK_MV_ESC_T,
            4200, 12584, 22400, 1830, 4100, 41, 0, 42 };
        dm_screen(m, DK_FACE_MOTOR, lang, "MOTOR & ESC");
        dm_scale(m, dm_scales());
        dm_motor(m, lm);
        dm_state(m, DK_STA_ARMED);
        m->view.rssi = -55;
        m->view.batt_mv = 3800;
        break;
    }
    case DM_MOTOR_SAFE: {
        const dk_live_motor_t lm = {
            0, 0,
            DK_MV_RPM | DK_MV_VOLT | DK_MV_CURR | DK_MV_POWER | DK_MV_ESC_T,
            0, 0, 22600, 0, 0, 29, 0, 0 };
        dm_screen(m, DK_FACE_MOTOR, lang, "MOTOR & ESC");
        dm_scale(m, dm_scales());
        dm_motor(m, lm);
        dm_state(m, 0);
        m->view.rssi = -55;
        m->view.batt_mv = 3800;
        break;
    }
    case DM_LINK_LOST:
        dm_screen(m, DK_FACE_MOTOR, lang, "MOTOR & ESC");
        dm_state(m, DK_STA_ARMED);
        m->view.lost = true;
        break;
    case DM_LINK_ENDED:
        /* LINK LOST held for 500ms: the Dial ended the link. */
        dm_screen(m, DK_FACE_MOTOR, lang, "MOTOR & ESC");
        dm_state(m, DK_STA_ARMED);
        m->view.lost = true;
        m->view.ready = false;
        break;
    case DM_WAITING:
        /* No panel packet yet: the language is the renderer's last. */
        m->view.lost = true;
        break;
    case DM_UPDATE_ASK:
    case DM_UPDATE_SEND:
        /* The update client's link: no panel packet, the Dial asks for
         * its press, or takes the image at 42%. */
        m->view.ready = false;
        m->view.upd = id == DM_UPDATE_ASK ? DIAL_UPD_ASK : DIAL_UPD_SEND;
        m->view.upd_secs = 27u;
        m->view.upd_pct = 42u;
        m->view.upd_from[1] = 24u;
        m->view.upd_to[1] = 25u;
        break;
    case DM_BIND:
        m->view.ready = false;
        m->view.bound = false;
        m->view.rssi = DK_RSSI_NONE;
        m->view.batt_mv = 0;
        m->view.usb = true;
        break;
    default:
        dm_screen(m, DK_FACE_OTHER, lang, "FILES");
        dm_state(m, 0);
        break;
    }
}
