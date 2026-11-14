/**
 * @file
 * @brief The M5Stack Dial's wireless knob protocol over a custom BLE
 *        (Bluetooth Low Energy) GATT (generic attribute profile) service:
 *        the packets in both directions, the panel's rules for taking a
 *        turn (dk_track_t), the link's DEGRADED and LOST timing (dk_link_t)
 *        and the Dial's model of what its face shows (dk_face_t).
 *
 * Dial to panel: EVENT (the absolute detent counter, buttons, gesture,
 * touch, the Dial's ms clock, the ping echo), STATUS (battery, firmware,
 * link) and INFO (protocol and firmware version, name).  Panel to Dial: the
 * screen model, SCREEN, NAV, VALUE_META, VALUE, LIVE_MOTOR, LIVE_SERVO and
 * STATE.  Every packet is 20 bytes or less, little-endian, byte 0 its kind.
 * Every field is bounded on both ends: a decoder refuses a packet of a wrong
 * length, an unknown kind, a field out of its range or a reserved bit set,
 * and an encoder refuses to write one.
 *
 * The link is relative: an EVENT carries the counter, never a value, and the
 * panel moves its value by the difference to the last counter it took.
 *
 * Pure C: no clock read, no I/O, no heap.  Times are unsigned differences of
 * a free-running 32-bit ms clock.  M5Stack is the name of its owner, used
 * only to say which device this talks to (README.md, "Trademarks").
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DIALKNOB_H
#define DIALKNOB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The protocol's version, carried in INFO.  A change of any packet
 *         layout or rule raises it. */
#define DK_PROTOCOL 1u
/** @brief The longest packet in either direction, in bytes: one ATT
 *         (attribute protocol) payload at the default MTU of 23. */
#define DK_PKT_MAX 20u

/** @brief Packet kinds, byte 0 of every packet. @{ */
#define DK_K_EVENT      0x01u /**< Dial to panel, EVENT characteristic */
#define DK_K_STATUS     0x02u /**< Dial to panel, STATUS characteristic */
#define DK_K_INFO       0x03u /**< Dial to panel, INFO characteristic */
#define DK_K_SCREEN     0x10u /**< panel to Dial: the face and its title */
#define DK_K_NAV        0x11u /**< panel to Dial: one list row */
#define DK_K_VALUE_META 0x12u /**< panel to Dial: a value's unit and range */
#define DK_K_VALUE      0x13u /**< panel to Dial: the value */
#define DK_K_LIVE_MOTOR 0x14u /**< panel to Dial: MOTOR readings */
#define DK_K_LIVE_SERVO 0x15u /**< panel to Dial: SERVO readings */
#define DK_K_STATE      0x16u /**< panel to Dial: clock, steps, ARMED */
/** @} */

/** @brief Packet lengths in bytes; a packet of another length is refused.
 *         @{ */
#define DK_LEN_EVENT      12u /**< EVENT */
#define DK_LEN_STATUS     12u /**< STATUS */
#define DK_LEN_INFO       20u /**< INFO */
#define DK_LEN_SCREEN     20u /**< SCREEN */
#define DK_LEN_NAV        20u /**< NAV */
#define DK_LEN_VALUE_META 20u /**< VALUE_META */
#define DK_LEN_VALUE      11u /**< VALUE */
#define DK_LEN_LIVE_MOTOR 18u /**< LIVE_MOTOR */
#define DK_LEN_LIVE_SERVO 19u /**< LIVE_SERVO */
#define DK_LEN_STATE       9u /**< STATE */
/** @} */

/** @brief The rules' times, in ms. @{ */
#define DK_STALE_MS      300u   /**< an EVENT later than this after the one
                                     before is a new reference */
#define DK_KEEPALIVE_MS  200u   /**< the Dial's EVENT period while idle */
#define DK_STATUS_MS     1000u  /**< the Dial's STATUS period */
#define DK_STREAM_MS     200u   /**< the longest gap between two panel
                                     packets: STATE fills it */
#define DK_SCREEN_MS     5000u  /**< the panel repeats SCREEN this often */
#define DK_DEGRADED_MS   500u   /**< the panel: no EVENT for this long is
                                     DEGRADED, a notice */
#define DK_LOST_MS       1000u  /**< the panel: no EVENT for this long is
                                     LOST; LOST while ARMED is an error */
#define DK_DIAL_LOST_MS  500u   /**< the Dial: no panel packet for this long
                                     shows LINK LOST */
#define DK_DIAL_DROP_MS  1000u  /**< the Dial: LINK LOST held for 500ms ends
                                     the link, so the panel sees LOST */
#define DK_HOLD_MS       1000u  /**< a screen button press this long is a
                                     HOLD gesture */
#define DK_FORGET_MS     10000u /**< a screen button press this long makes
                                     the Dial forget its bound panel */
#define DK_IDLE_MS       10000u /**< no turn for this long: the Dial asks
                                     for DK_LATENCY_IDLE */
/** @} */

/** @brief The encoder: detents a turn, and the quadrature counts (x4) of
 *         one detent. @{ */
#define DK_DETENTS_TURN  16u /**< detents a turn */
#define DK_COUNTS_DETENT 4u  /**< quadrature counts a detent */
/** @} */
/** @brief Quadrature counts from a detent's center that move to the
 *         next detent; a jitter of less never clicks. */
#define DK_DETENT_HYST   3
/** @brief The largest counter difference between two EVENTs that is a
 *         turn; a larger one is a glitch: one turn. */
#define DK_MAX_STEP      16

/** @brief The connection parameters the Dial asks for once the panel has
 *         subscribed.  Interval in 1.25ms units, supervision timeout in 10ms
 *         units.  The connect itself runs on the central's set. @{ */
#define DK_ITVL_MIN       9u   /**< 11.25ms */
#define DK_ITVL_MAX       12u  /**< 15ms */
#define DK_LATENCY_ACTIVE 0u   /**< peripheral latency while turning */
#define DK_LATENCY_IDLE   12u  /**< after DK_IDLE_MS: 13 x 15ms = 195ms */
#define DK_SUPERVISION    100u /**< 1,000ms */
#define DK_MTU            247u /**< the ATT MTU asked; every packet fits 23 */
/** @} */

/** @brief The Dial's advertising interval, in ms. @{ */
#define DK_ADV_UNBOUND_MS 100u /**< no bound panel: anyone may connect */
#define DK_ADV_BOUND_MS   500u /**< bound, not connected: the bound panel
                                    only */
/** @} */

/** @brief The 128-bit UUIDs (universally unique identifiers), base
 *         8F3C9D10-2A7B-4D6E-9B1A-5243424E00xx.  DK_UUID_LE(xx) is the 16
 *         bytes least significant first, as on the air and as NimBLE's
 *         BLE_UUID128_INIT() takes them. @{ */
#define DK_UUID_LE(xx) (xx), 0x00, 0x4E, 0x42, 0x43, 0x52, 0x1A, 0x9B, \
                       0x6E, 0x4D, 0x7B, 0x2A, 0x10, 0x9D, 0x3C, 0x8F
#define DK_UUID_SVC    0x00u /**< the service, in the advertising data */
#define DK_UUID_EVENT  0x01u /**< EVENT: NOTIFY */
#define DK_UUID_STATUS 0x02u /**< STATUS: NOTIFY, READ */
#define DK_UUID_SCREEN 0x03u /**< SCREEN: WRITE NO RESPONSE, every panel
                                  packet */
#define DK_UUID_INFO   0x04u /**< INFO: READ */
/** @} */

/** @brief Text fields' sizes in bytes on the wire; a field is UTF-8,
 *         NUL-padded, and holds no NUL terminator when full. @{ */
#define DK_TITLE_MAX 15u /**< SCREEN title */
#define DK_ITEM_MAX  15u /**< NAV item */
#define DK_UNIT_MAX  4u  /**< VALUE_META unit, "°C" is 3 bytes */
#define DK_NAME_MAX  12u /**< INFO name, printable ASCII */
/** @} */

/** @brief The bounds of the bounded fields. @{ */
#define DK_TOUCH_MAX     239u   /**< touch x and y, 0 to 239 */
#define DK_BATT_MIN_MV   2000u  /**< a battery reading, else 0 unknown */
#define DK_BATT_MAX_MV   5000u  /**< the highest battery reading */
#define DK_RSSI_NONE     127    /**< STATUS rssi: not known */
#define DK_RSSI_MIN      (-127) /**< the lowest RSSI, dBm */
#define DK_RSSI_MAX      20     /**< the highest RSSI, dBm */
#define DK_DECIMALS_MAX  4u     /**< VALUE_META decimals */
#define DK_THROTTLE_MAX  10000u /**< LIVE_MOTOR throttle, 0.01% */
#define DK_TEMP_MIN      (-40)  /**< LIVE_MOTOR °C */
#define DK_TEMP_MAX      125    /**< the highest temperature, °C */
#define DK_US_MIN        400u   /**< a pulse in us; 0 is none */
#define DK_US_MAX        2500u  /**< the longest pulse, us */
#define DK_DDEG_MAX      1800   /**< commanded angle, +-180.0° */
#define DK_CDEG_MAX      18000  /**< measured angle, +-180.00° */
#define DK_STEP_MIN      1u     /**< STATE steps, 0.1% and 0.1° */
#define DK_STEP_MAX      100u   /**< the largest STATE step */
/** @} */

/** @brief EVENT buttons bits. @{ */
#define DK_BTN_SCREEN 0x01u /**< the screen button (GPIO42) is down */
#define DK_BTN_TOUCH  0x02u /**< a finger is on the glass: touch_x, y */
#define DK_BTN_STAMP  0x04u /**< the Stamp module's button (GPIO0) */
/** @} */

/** @brief EVENT gestures, one a packet. */
typedef enum {
    DK_G_NONE = 0,  /**< none */
    DK_G_PRESS = 1, /**< the screen button released before DK_HOLD_MS */
    DK_G_HOLD = 2,  /**< the screen button down for DK_HOLD_MS, sent once */
    DK_G_TAP = 3,   /**< a tap on the glass at touch_x, touch_y */
} dk_gesture_t;

/** @brief Dial to panel: one EVENT. */
typedef struct {
    uint8_t  seq;     /**< wraps at 256; a repeat is dropped */
    int16_t  counter; /**< detents since power-up, wrapping, clockwise
                           positive */
    uint8_t  buttons; /**< DK_BTN_* */
    uint8_t  gesture; /**< dk_gesture_t */
    uint8_t  touch_x; /**< 0 to 239 with DK_BTN_TOUCH or DK_G_TAP, else 0 */
    uint8_t  touch_y; /**< as touch_x */
    uint16_t ms;      /**< the Dial's ms clock, low 16 bits */
    uint16_t echo;    /**< low 16 bits of the last STATE's panel ms */
} dk_evt_t;

/** @brief STATUS flags. @{ */
#define DK_ST_USB          0x01u /**< a USB host reads the Dial's port */
#define DK_ST_CHARGING     0x02u /**< the charger reports charging */
#define DK_ST_BATT_UNKNOWN 0x04u /**< no battery reading: batt_mv is 0 */
#define DK_ST_PANEL_LOST   0x08u /**< no panel packet for DK_DIAL_LOST_MS */
/** @} */

/** @brief Dial to panel: STATUS, every DK_STATUS_MS and on a change. */
typedef struct {
    uint8_t  seq;      /**< wraps at 256, a count of its own */
    uint16_t batt_mv;  /**< DK_BATT_MIN_MV to DK_BATT_MAX_MV, 0 unknown */
    uint8_t  flags;    /**< DK_ST_* */
    uint8_t  fw[3];    /**< firmware major, minor, patch */
    uint16_t uptime_s; /**< seconds since power-up, wrapping */
    int8_t   rssi;     /**< the link's RSSI (received signal strength
                            indication) on the Dial, dBm; DK_RSSI_NONE */
    uint8_t  bad;      /**< panel packets the Dial refused, wrapping */
} dk_status_t;

/** @brief INFO flags. @{ */
#define DK_INFO_BOUND 0x01u /**< the Dial holds a bound panel */
#define DK_INFO_HOOKS 0x02u /**< a test build: the console injects input */
/** @} */
/** @brief INFO hardware: the M5Stack Dial v1.1. */
#define DK_HW_DIAL11 1u

/** @brief Dial to panel: INFO, read once after connect. */
typedef struct {
    uint8_t proto; /**< DK_PROTOCOL of the Dial's build */
    uint8_t fw[3]; /**< firmware major, minor, patch */
    uint8_t flags; /**< DK_INFO_* */
    uint8_t hw;    /**< DK_HW_* */
    char    name[DK_NAME_MAX + 1u]; /**< "DIAL-3A10", printable ASCII */
} dk_info_t;

/** @brief SCREEN face kinds: which face the Dial draws. */
typedef enum {
    DK_FACE_NAV = 1,   /**< a list: NAV packets */
    DK_FACE_VALUE = 2, /**< a value being set: VALUE_META, VALUE */
    DK_FACE_SERVO = 3, /**< SERVO: LIVE_SERVO */
    DK_FACE_MOTOR = 4, /**< MOTOR & ESC: LIVE_MOTOR */
    DK_FACE_OTHER = 5, /**< a screen the knob does nothing on */
    DK_FACE_HOME = 6,  /**< the overview */
} dk_face_kind_t;

/** @brief SCREEN languages. @{ */
#define DK_LANG_EN 0u /**< English */
#define DK_LANG_DE 1u /**< German */
/** @} */

/** @brief SCREEN flags. @{ */
#define DK_SCR_NAVIGATES 0x01u /**< the knob moves the panel's focus */
#define DK_SCR_SOUND     0x02u /**< the Dial clicks per detent */
/** @} */

/** @brief Panel to Dial: SCREEN, on every screen change and every
 *         DK_SCREEN_MS. */
typedef struct {
    uint8_t seq;   /**< the panel's packet count, wrapping */
    uint8_t face;  /**< dk_face_kind_t */
    uint8_t lang;  /**< DK_LANG_* */
    uint8_t flags; /**< DK_SCR_* */
    char    title[DK_TITLE_MAX + 1u]; /**< the panel screen's name */
} dk_screen_t;

/** @brief NAV slots: the three rows a move sends. @{ */
#define DK_SLOT_PREV  0u /**< the row before the focused one */
#define DK_SLOT_FOCUS 1u /**< the focused row */
#define DK_SLOT_NEXT  2u /**< the row after it */
/** @} */

/** @brief Panel to Dial: NAV, one row of a list; three a move. */
typedef struct {
    uint8_t seq;   /**< as SCREEN */
    uint8_t index; /**< the focused row's index, below count */
    uint8_t count; /**< rows; 0 is an empty list with index 0 */
    uint8_t slot;  /**< DK_SLOT_* */
    char    item[DK_ITEM_MAX + 1u]; /**< the row's text, "" past an end */
} dk_nav_t;

/** @brief Panel to Dial: VALUE_META, on entry to a value.  Values are
 *         integers scaled by 10^decimals. */
typedef struct {
    uint8_t seq;      /**< as SCREEN */
    char    unit[DK_UNIT_MAX + 1u]; /**< "%", "°C", "ms"; "" none */
    uint8_t decimals; /**< 0 to DK_DECIMALS_MAX */
    int32_t min;      /**< at most max */
    int32_t max;      /**< at least min */
    int32_t step;     /**< at least 1 */
} dk_value_meta_t;

/** @brief VALUE flags. @{ */
#define DK_VAL_SET   0x01u /**< the knob sets the value (SET mode) */
#define DK_VAL_LIMIT 0x02u /**< the last turn was refused at a limit */
/** @} */

/** @brief Panel to Dial: VALUE, in the frame the value changed. */
typedef struct {
    uint8_t seq;   /**< as SCREEN */
    int32_t value; /**< scaled as VALUE_META */
    int32_t was;   /**< the value on entry */
    uint8_t flags; /**< DK_VAL_* */
} dk_value_t;

/** @brief LIVE flags. @{ */
#define DK_LIVE_ARMED   0x01u /**< the bench is ARMED */
#define DK_LIVE_DRIVING 0x02u /**< the output is driven */
#define DK_LIVE_FAULT   0x04u /**< MOTOR: a fault is shown */
#define DK_LIVE_MEAS    0x04u /**< SERVO: meas_cdeg is valid */
#define DK_LIVE_FB      0x08u /**< SERVO: fb_us and fb_ca are valid */
#define DK_LIVE_RAIL    0x10u /**< SERVO: rail_* are valid */
/** @} */

/** @brief LIVE_MOTOR valid bits: a field without its bit is 0. @{ */
#define DK_MV_RPM   0x01u /**< rpm */
#define DK_MV_VOLT  0x02u /**< mv */
#define DK_MV_CURR  0x04u /**< ca */
#define DK_MV_POWER 0x08u /**< dw */
#define DK_MV_ESC_T 0x10u /**< esc_c */
#define DK_MV_MOT_T 0x20u /**< mot_c */
/** @} */

/** @brief Panel to Dial: LIVE_MOTOR at 20Hz on MOTOR & ESC. */
typedef struct {
    uint8_t  seq;      /**< as SCREEN */
    uint8_t  flags;    /**< DK_LIVE_ARMED, _DRIVING, _FAULT */
    uint8_t  valid;    /**< DK_MV_* */
    uint16_t thr_cpct; /**< throttle commanded, 0.01%, 0 to 10,000 */
    uint16_t rpm;      /**< rpm */
    uint16_t mv;       /**< voltage, mV */
    int16_t  ca;       /**< current, 10mA */
    uint16_t dw;       /**< power, 0.1W */
    int8_t   esc_c;    /**< ESC °C, -40 to 125 */
    int8_t   mot_c;    /**< motor °C, -40 to 125 */
    uint16_t run_s;    /**< the run clock, s */
} dk_live_motor_t;

/** @brief Panel to Dial: LIVE_SERVO at 20Hz on SERVO. */
typedef struct {
    uint8_t  seq;       /**< as SCREEN */
    uint8_t  flags;     /**< DK_LIVE_ARMED, _DRIVING, _MEAS, _FB, _RAIL */
    uint16_t cmd_us;    /**< commanded pulse, 0 or 400 to 2,500us */
    int16_t  cmd_ddeg;  /**< commanded angle, 0.1° */
    int16_t  meas_cdeg; /**< measured angle, 0.01°, with DK_LIVE_MEAS */
    uint16_t rail_mv;   /**< rail voltage, mV, with DK_LIVE_RAIL */
    int16_t  rail_ca;   /**< rail current, 10mA, with DK_LIVE_RAIL */
    uint16_t rail_dw;   /**< rail power, 0.1W, with DK_LIVE_RAIL */
    uint16_t fb_us;     /**< feedback position, 400 to 2,500us, with
                             DK_LIVE_FB */
    int16_t  fb_ca;     /**< feedback current, 10mA, with DK_LIVE_FB */
} dk_live_servo_t;

/** @brief STATE flags. @{ */
#define DK_STA_ARMED    0x01u /**< the bench is ARMED */
#define DK_STA_DEGRADED 0x02u /**< the panel marks the link DEGRADED */
/** @} */

/** @brief Panel to Dial: STATE every DK_STREAM_MS when nothing else went. */
typedef struct {
    uint8_t  seq;      /**< as SCREEN */
    uint32_t panel_ms; /**< the panel's ms clock; the Dial echoes its low
                            16 bits in EVENT */
    uint8_t  thr_step; /**< the Dial's throttle step, 0.1%, 1 to 100 */
    uint8_t  srv_step; /**< the Dial's servo step, 0.1°, 1 to 100 */
    uint8_t  flags;    /**< DK_STA_* */
} dk_state_t;

/** @brief Any panel packet, decoded. */
typedef struct {
    uint8_t kind; /**< DK_K_SCREEN to DK_K_STATE */
    union {
        dk_screen_t     screen; /**< DK_K_SCREEN */
        dk_nav_t        nav;    /**< DK_K_NAV */
        dk_value_meta_t meta;   /**< DK_K_VALUE_META */
        dk_value_t      value;  /**< DK_K_VALUE */
        dk_live_motor_t motor;  /**< DK_K_LIVE_MOTOR */
        dk_live_servo_t servo;  /**< DK_K_LIVE_SERVO */
        dk_state_t      state;  /**< DK_K_STATE */
    } u; /**< by kind */
} dk_panel_pkt_t;

/**
 * @brief Encode an EVENT.
 * @param e   the event
 * @param out DK_LEN_EVENT bytes
 * @return DK_LEN_EVENT, or 0 when a field is out of its range
 */
size_t dk_evt_encode(const dk_evt_t *e, uint8_t *out);
/**
 * @brief Decode an EVENT.
 * @param buf the bytes
 * @param len their count
 * @param out the event; unchanged on false
 * @return true when the packet is an EVENT with every field in range
 */
bool dk_evt_decode(const uint8_t *buf, size_t len, dk_evt_t *out);

/** @brief Encode a STATUS. @param s the status @param out DK_LEN_STATUS
 *         bytes @return DK_LEN_STATUS, or 0 when a field is out of range */
size_t dk_status_encode(const dk_status_t *s, uint8_t *out);
/** @brief Decode a STATUS. @param buf the bytes @param len their count
 *         @param out the status; unchanged on false @return true when valid */
bool dk_status_decode(const uint8_t *buf, size_t len, dk_status_t *out);

/** @brief Encode an INFO. @param i the info @param out DK_LEN_INFO bytes
 *         @return DK_LEN_INFO, or 0 when a field is out of range */
size_t dk_info_encode(const dk_info_t *i, uint8_t *out);
/** @brief Decode an INFO. @param buf the bytes @param len their count
 *         @param out the info; unchanged on false @return true when valid */
bool dk_info_decode(const uint8_t *buf, size_t len, dk_info_t *out);

/** @brief Encode a SCREEN.  A title that fills all DK_TITLE_MAX + 1 bytes
 *         of its field without a NUL is cut at the last whole UTF-8
 *         character within DK_TITLE_MAX bytes; dk_text_copy() fills the
 *         field from a longer text.
 *         @param s the screen @param out DK_LEN_SCREEN bytes
 *         @return DK_LEN_SCREEN, or 0 when a field is out of range */
size_t dk_screen_encode(const dk_screen_t *s, uint8_t *out);
/** @brief Decode a SCREEN. @param buf the bytes @param len their count
 *         @param out the screen; unchanged on false @return true when valid */
bool dk_screen_decode(const uint8_t *buf, size_t len, dk_screen_t *out);

/** @brief Encode a NAV; the item is cut as a SCREEN title.
 *         @param n the row @param out DK_LEN_NAV bytes
 *         @return DK_LEN_NAV, or 0 when a field is out of range */
size_t dk_nav_encode(const dk_nav_t *n, uint8_t *out);
/** @brief Decode a NAV. @param buf the bytes @param len their count
 *         @param out the row; unchanged on false @return true when valid */
bool dk_nav_decode(const uint8_t *buf, size_t len, dk_nav_t *out);

/** @brief Encode a VALUE_META; the unit is cut as a SCREEN title.
 *         @param m the meta @param out DK_LEN_VALUE_META bytes
 *         @return DK_LEN_VALUE_META, or 0 when a field is out of range */
size_t dk_value_meta_encode(const dk_value_meta_t *m, uint8_t *out);
/** @brief Decode a VALUE_META. @param buf the bytes @param len their count
 *         @param out the meta; unchanged on false @return true when valid */
bool dk_value_meta_decode(const uint8_t *buf, size_t len,
                          dk_value_meta_t *out);

/** @brief Encode a VALUE. @param v the value @param out DK_LEN_VALUE bytes
 *         @return DK_LEN_VALUE, or 0 when a flag is out of range */
size_t dk_value_encode(const dk_value_t *v, uint8_t *out);
/** @brief Decode a VALUE. @param buf the bytes @param len their count
 *         @param out the value; unchanged on false @return true when valid */
bool dk_value_decode(const uint8_t *buf, size_t len, dk_value_t *out);

/** @brief Encode a LIVE_MOTOR. @param m the readings
 *         @param out DK_LEN_LIVE_MOTOR bytes
 *         @return DK_LEN_LIVE_MOTOR, or 0 when a field is out of range */
size_t dk_live_motor_encode(const dk_live_motor_t *m, uint8_t *out);
/** @brief Decode a LIVE_MOTOR. @param buf the bytes @param len their count
 *         @param out the readings; unchanged on false
 *         @return true when valid */
bool dk_live_motor_decode(const uint8_t *buf, size_t len,
                          dk_live_motor_t *out);

/** @brief Encode a LIVE_SERVO. @param s the readings
 *         @param out DK_LEN_LIVE_SERVO bytes
 *         @return DK_LEN_LIVE_SERVO, or 0 when a field is out of range */
size_t dk_live_servo_encode(const dk_live_servo_t *s, uint8_t *out);
/** @brief Decode a LIVE_SERVO. @param buf the bytes @param len their count
 *         @param out the readings; unchanged on false
 *         @return true when valid */
bool dk_live_servo_decode(const uint8_t *buf, size_t len,
                          dk_live_servo_t *out);

/** @brief Encode a STATE. @param s the state @param out DK_LEN_STATE bytes
 *         @return DK_LEN_STATE, or 0 when a field is out of range */
size_t dk_state_encode(const dk_state_t *s, uint8_t *out);
/** @brief Decode a STATE. @param buf the bytes @param len their count
 *         @param out the state; unchanged on false @return true when valid */
bool dk_state_decode(const uint8_t *buf, size_t len, dk_state_t *out);

/**
 * @brief Decode any panel packet by its kind byte.
 * @param buf the bytes, as written to SCREEN
 * @param len their count
 * @param out the packet; unchanged on false
 * @return true when the packet is a panel kind and valid
 */
bool dk_panel_decode(const uint8_t *buf, size_t len, dk_panel_pkt_t *out);

/**
 * @brief Whether advertising data lists the Dial's service in a 128-bit
 *        UUID list (AD types 0x06 and 0x07).
 * @param ad  the AD (advertising data) structures
 * @param len their bytes; a length past the end stops the walk
 * @return true when the service UUID is listed
 */
bool dk_adv_has_service(const uint8_t *ad, size_t len);

/**
 * @brief Copy a text into a field, cut at the last whole UTF-8 character
 *        that fits, NUL-terminated.
 * @param dst the field
 * @param cap its bytes, the NUL included: sizeof of a dk_*_t text field
 * @param src the text; NULL is ""
 * @return the bytes copied, without the NUL
 */
size_t dk_text_copy(char *dst, size_t cap, const char *src);

/**
 * @brief Write a scaled value with its unit and no space: "-12.50%".
 * @param value    the integer, scaled by 10^decimals
 * @param decimals 0 to DK_DECIMALS_MAX; more is taken as the maximum
 * @param unit     "" or the unit; NULL is ""
 * @param out      the text, NUL-terminated
 * @param cap      bytes at out; 24 hold every int32 with a 4-byte unit
 * @return the length written, or 0 when it does not fit (out is "")
 */
size_t dk_value_text(int32_t value, uint8_t decimals, const char *unit,
                     char *out, size_t cap);

/* ------------------------------------------------------------------------ */
/* The panel's side: taking a turn.                                          */

/** @brief What dk_track_feed() made of an EVENT. */
typedef enum {
    DK_TAKE_MOVE = 0, /**< the detents are applied (0 for no turn) */
    DK_TAKE_REF,      /**< the first after a (re)connect: a reference */
    DK_TAKE_STALE,    /**< later than DK_STALE_MS: a reference */
    DK_TAKE_GLITCH,   /**< over DK_MAX_STEP detents: a reference */
    DK_TAKE_REPEAT,   /**< the last packet's seq again: dropped */
} dk_take_t;

/** @brief The panel's turn tracker, one per Dial link.  The counts run
 *         from dk_track_init() and survive dk_track_reset(). */
typedef struct {
    bool     have_ref; /**< a reference is held */
    uint8_t  seq;      /**< the last packet's seq */
    int16_t  counter;  /**< the reference counter */
    uint32_t last_ms;  /**< the last packet's arrival */
    uint32_t taken;    /**< packets that moved by a difference */
    uint32_t refs;     /**< references after a (re)connect */
    uint32_t stale;    /**< stale references */
    uint32_t glitches; /**< glitch references */
    uint32_t repeats;  /**< repeated packets dropped */
    uint32_t skipped;  /**< seq numbers skipped, summed */
} dk_track_t;

/** @brief Clear the tracker and its counts. @param t the tracker */
void dk_track_init(dk_track_t *t);
/** @brief Drop the reference, at every connect and disconnect: the next
 *         packet sets one and moves nothing.  @param t the tracker */
void dk_track_reset(dk_track_t *t);
/**
 * @brief Take one decoded EVENT.
 *
 * Rules, in order: the first packet after dk_track_reset() sets the
 * reference, whatever its seq; a packet whose seq repeats the last one is
 * dropped; a packet more
 * than DK_STALE_MS after the one before sets the reference; a difference of
 * more than DK_MAX_STEP detents is a glitch and sets the reference; else
 * the difference is the turn.  Skipped seq numbers are counted and lose no
 * motion: the counter is absolute.  The gesture of every packet but a
 * repeat is the caller's to act on.
 *
 * @param t      the tracker
 * @param e      the event
 * @param now_ms the arrival time
 * @param why    what was made of it; may be NULL
 * @return the detents to apply, -DK_MAX_STEP to DK_MAX_STEP; 0 but for
 *         DK_TAKE_MOVE
 */
int32_t dk_track_feed(dk_track_t *t, const dk_evt_t *e, uint32_t now_ms,
                      dk_take_t *why);

/* ------------------------------------------------------------------------ */
/* The panel's side: the link's state.                                       */

/** @brief The link's state on the panel. */
typedef enum {
    DK_LINK_DOWN = 0, /**< no Dial in use: never up since dk_link_off() */
    DK_LINK_CONNECTING, /**< connected, no EVENT yet: no loss clock runs;
                             a discovery that never ends is the central's
                             own timeout */
    DK_LINK_UP,       /**< packets within DK_DEGRADED_MS */
    DK_LINK_DEGRADED, /**< none for DK_DEGRADED_MS: a notice */
    DK_LINK_LOST,     /**< a disconnect, or none for DK_LOST_MS */
} dk_link_state_t;

/** @brief The panel's view of one Dial link. */
typedef struct {
    uint8_t  state;     /**< dk_link_state_t */
    bool     connected; /**< between connect and disconnect */
    bool     loss;      /**< a loss not yet taken by dk_link_loss_error() */
    uint32_t last_ms;   /**< the last packet */
    uint32_t losses;    /**< UP or DEGRADED to LOST, counted */
} dk_link_t;

/** @brief DOWN, no loss pending, counts cleared. @param l the link */
void dk_link_init(dk_link_t *l);
/** @brief The Dial is switched off or unbound on the panel: DOWN, and a
 *         pending loss is dropped. @param l the link */
void dk_link_off(dk_link_t *l);
/** @brief The connection is up: CONNECTING, no clock runs until the
 *         first EVENT.
 *         @param l the link @param now_ms the time */
void dk_link_connect(dk_link_t *l, uint32_t now_ms);
/** @brief A packet from the Dial: UP, the clock starts again.
 *         @param l the link
 *         @param now_ms its arrival; ignored while not connected */
void dk_link_rx(dk_link_t *l, uint32_t now_ms);
/** @brief The connection ended: LOST at once, and a loss when the link
 *         was UP or DEGRADED. @param l the link */
void dk_link_disconnect(dk_link_t *l);
/**
 * @brief Move the state by the time since the last packet.
 * @param l      the link
 * @param now_ms the time
 * @return the state, dk_link_state_t
 */
uint8_t dk_link_step(dk_link_t *l, uint32_t now_ms);
/**
 * @brief Take a loss: LOST while ARMED is an error, and the bench goes SAFE.
 *
 * Each move from UP or DEGRADED to LOST is taken once.  A loss taken while
 * SAFE is no error and is not kept for a later ARM.
 *
 * @param l     the link; call dk_link_step() first
 * @param armed whether the bench is ARMED now
 * @return true once per loss that happened while armed
 */
bool dk_link_loss_error(dk_link_t *l, bool armed);

/* ------------------------------------------------------------------------ */
/* The Dial's side: what the face shows.                                     */

/** @brief dk_face_t.changed bits: what a packet changed since the caller
 *         last cleared them. @{ */
#define DK_CH_SCREEN 0x01u /**< screen */
#define DK_CH_NAV    0x02u /**< nav */
#define DK_CH_META   0x04u /**< meta */
#define DK_CH_VALUE  0x08u /**< value */
#define DK_CH_MOTOR  0x10u /**< motor */
#define DK_CH_SERVO  0x20u /**< servo */
#define DK_CH_STATE  0x40u /**< state */
/** @} */

/** @brief The Dial's model of the panel's screen, built from its packets.
 *         Parts are valid while their have_ flag is set. */
typedef struct {
    dk_screen_t     screen;      /**< the last SCREEN */
    dk_nav_t        nav[3];      /**< the rows by slot */
    dk_value_meta_t meta;        /**< the last VALUE_META */
    dk_value_t      value;       /**< the last VALUE */
    dk_live_motor_t motor;       /**< the last LIVE_MOTOR */
    dk_live_servo_t servo;       /**< the last LIVE_SERVO */
    dk_state_t      state;       /**< the last STATE */
    bool     have_screen;        /**< screen is valid */
    uint8_t  have_nav;           /**< bit per slot: nav[slot] valid */
    bool     have_meta;          /**< meta is valid */
    bool     have_value;         /**< value is valid */
    bool     have_motor;         /**< motor is valid */
    bool     have_servo;         /**< servo is valid */
    bool     have_state;         /**< state is valid */
    bool     any;                /**< a packet came since the reset */
    uint8_t  seq;                /**< the last packet's seq */
    uint8_t  changed;            /**< DK_CH_*, the caller clears them */
    uint32_t last_ms;            /**< the last packet's arrival */
    uint32_t packets;            /**< packets taken */
    uint32_t bad;                /**< packets refused by the decoder */
    uint32_t repeats;            /**< packets dropped as a repeated seq */
    uint32_t skipped;            /**< seq numbers skipped, summed */
} dk_face_t;

/** @brief An empty model, counts cleared. @param f the model */
void dk_face_init(dk_face_t *f);
/** @brief A new connection: the model is emptied and the seq is learnt
 *         again; the counts are kept. @param f the model */
void dk_face_link_reset(dk_face_t *f);
/**
 * @brief Take one panel packet.
 *
 * A SCREEN whose face or title differs empties nav, meta and value.  A
 * NAV whose index or count differs from the focused row empties the other
 * slots.
 *
 * @param f      the model
 * @param buf    the bytes written to SCREEN
 * @param len    their count
 * @param now_ms the arrival time
 * @return true when taken; false when refused (counted in bad) or a repeat
 */
bool dk_face_feed(dk_face_t *f, const uint8_t *buf, size_t len,
                  uint32_t now_ms);
/**
 * @brief Whether the Dial shows LINK LOST.
 * @param f      the model
 * @param now_ms the time
 * @return true when no packet came since the reset or none for
 *         DK_DIAL_LOST_MS
 */
bool dk_face_lost(const dk_face_t *f, uint32_t now_ms);
/**
 * @brief Whether the Dial ends the link: a panel packet came on this
 *        connection and none since for DK_DIAL_DROP_MS, so LINK LOST has
 *        held for 500ms.  The panel then sees the disconnect, LOST, which
 *        while ARMED is its error.  A connection that has had no panel
 *        packet yet, while the panel discovers the service, is not ended.
 * @param f      the model
 * @param now_ms the time
 * @return true when the link is to end
 */
bool dk_face_drop(const dk_face_t *f, uint32_t now_ms);

/**
 * @brief The encoder's detent from its quadrature count, with hysteresis:
 *        the detent moves only when the count is DK_DETENT_HYST or more
 *        counts from the current detent's center (DK_COUNTS_DETENT x the
 *        detent), so an encoder that rests between two counts never
 *        clicks.
 * @param detent the current detent
 * @param count  the accumulated quadrature count, signed as the turn
 * @return the new detent
 */
int32_t dk_detent_follow(int32_t detent, int32_t count);

#ifdef __cplusplus
}
#endif

#endif /* DIALKNOB_H */
