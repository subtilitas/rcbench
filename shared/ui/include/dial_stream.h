/**
 * @file
 * @brief The panel's stream to the M5Stack Dial: the screen model the frame
 *        task fills (dial_model_t) and the scheduler that turns it into
 *        protocols/dialknob's panel packets, one at a time, at the rates of
 *        docs/Dial.md.  Pure C, no clock read.
 *
 * Which packet goes next, first match:
 *
 * | Packet | When |
 * | --- | --- |
 * | SCREEN | the face, the title, the language or the flags changed, or DK_SCREEN_MS (5,000ms) after the last |
 * | VALUE_META | a value is shown and its unit, range, step or decimals changed |
 * | VALUE | the value or its flags changed: in the frame that changed it |
 * | SCALE | on a bench face: the first since the face was entered or the reset, or a full scale changed and DK_STREAM_MS (200ms) passed since the last |
 * | NAV | a list is shown and a row of the three changed, one row a packet |
 * | LIVE_MOTOR, LIVE_SERVO | on a bench face, DIAL_LIVE_MS (50ms, 20Hz) after the last |
 * | STATE | ARMED, DEGRADED or a step changed, or DK_STREAM_MS (200ms) without any packet |
 *
 * dial_stream_reset() at each connect sends the whole model again: every
 * row counts as changed.  A field out of its packet's range is clamped
 * before the packet is written, so every packet the stream writes decodes.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dialknob.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The LIVE packets' period, ms: 20Hz. */
#define DIAL_LIVE_MS 50u

/** @brief What the Dial shows, as the frame task last saw the panel. */
typedef struct {
    uint8_t         face;    /**< dk_face_kind_t; 0 is no model yet */
    uint8_t         lang;    /**< DK_LANG_* */
    uint8_t         flags;   /**< DK_SCR_* */
    char            title[DK_TITLE_MAX + 1u]; /**< the screen's name */
    bool            armed;   /**< the bench is ARMED */
    uint8_t         thr_tenths; /**< a detent's throttle, 0.1% */
    uint8_t         srv_tenths; /**< a detent's horn angle, 0.1° */
    bool            has_value; /**< meta and value hold the knob's value */
    dk_value_meta_t meta;    /**< its unit, range, step; seq unused */
    dk_value_t      value;   /**< the value; seq unused */
    bool            has_motor; /**< motor holds MOTOR & ESC's readings */
    dk_live_motor_t motor;   /**< seq unused */
    bool            has_servo; /**< servo holds SERVO's readings */
    dk_live_servo_t servo;   /**< seq unused */
    bool            has_scale; /**< scale holds the readings' full scales */
    dk_scale_t      scale;   /**< seq unused; a field 0 is not known */
    bool            has_nav; /**< nav holds the focused list's rows */
    dk_nav_t        nav[3];  /**< by slot; seq and slot unused */
} dial_model_t;

/** @brief The scheduler's state.  Its members are private but for the
 *         tests. */
typedef struct {
    uint8_t         seq;          /**< the last packet's seq */
    bool            any;          /**< a packet went since the reset */
    uint32_t        t_any;        /**< when the last went */
    bool            screen_ok;    /**< screen holds what went */
    dk_screen_t     screen;       /**< the last SCREEN, seq 0 */
    uint32_t        t_screen;     /**< when it went */
    bool            meta_ok;      /**< meta holds what went */
    dk_value_meta_t meta;         /**< the last VALUE_META, seq 0 */
    bool            value_ok;     /**< value holds what went */
    dk_value_t      value;        /**< the last VALUE, seq 0 */
    bool            scale_ok;     /**< scale holds what went */
    dk_scale_t      scale;        /**< the last SCALE, seq 0 */
    uint32_t        t_scale;      /**< when it went */
    uint8_t         nav_ok;       /**< bit per slot: nav[slot] went */
    dk_nav_t        nav[3];       /**< the last NAV rows, seq 0 */
    uint32_t        t_live;       /**< when the last LIVE went */
    bool            live_any;     /**< a LIVE went since the reset */
    bool            state_ok;     /**< state holds what went */
    dk_state_t      state;        /**< the last STATE, seq 0, ms 0 */
    uint32_t        state_ms;     /**< the panel ms of the last STATE */
    bool            state_sent;   /**< a STATE went since the reset */
    uint32_t        sent[8];      /**< packets by kind, SCREEN to SCALE */
    uint32_t        clamped;      /**< packets with a field clamped */
} dial_stream_t;

/** @brief Clear, counts included; the next packets send everything.
 *  @param st The scheduler; NULL does nothing. */
void dial_stream_init(dial_stream_t *st);

/** @brief A new connection: the whole model again, the counts kept.
 *  @param st The scheduler; NULL does nothing. */
void dial_stream_reset(dial_stream_t *st);

/**
 * @brief The next packet the model asks for at @p now_ms.
 * @param st       The scheduler.
 * @param m        The model.
 * @param now_ms   The clock; STATE carries it.
 * @param degraded The panel marks the link DEGRADED.
 * @param out      Receives the packet, DK_PKT_MAX bytes.
 * @return Its length, 0 when nothing is due (or for NULL).
 */
size_t dial_stream_next(dial_stream_t *st, const dial_model_t *m,
                        uint32_t now_ms, bool degraded, uint8_t *out);

/**
 * @brief Clamp the model's fields into their packets' ranges.
 * @param m The model; NULL does nothing.
 * @return Whether a field was out of its range.
 */
bool dial_model_clamp(dial_model_t *m);

#ifdef __cplusplus
}
#endif
