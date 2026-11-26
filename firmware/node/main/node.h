/**
 * @file node.h
 * @brief The node firmware's parts and how they meet: the role (power or
 *        balance, chosen at build time), Bluetooth LE (Low Energy), the
 *        battery, sleep and the console.
 *
 * Tasks: `node` (node_main.c) runs the once-a-second step: battery,
 * status, console line, sleep policy, and the control writes.  NimBLE's host
 * task serves the GATT (generic attribute profile) server.  `ble_tx` sends
 * the STREAM packets the role queues.  The power role adds `pwr`, its
 * sampler; the balance role adds `bal`, which reads the ADC stream.  The console's REPL (read-eval-print loop) runs in its own task.
 * A board with a display (node_board.h) adds `disp` (node_disp.c).  The
 * node's own settings (name, INA228 address, shunt) are node_set.c's.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_board.h"
#include "node_scan.h"
#include "node_store.h"
#include "rcnode.h"

#if NODE_BOARD_DISPLAY
#include "driver/i2c_types.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The console's log tag. */
#define NODE_TAG "node"

/* ------------------------------------------------------------- the role */

/** @brief What a role does; node_pwr.c or node_bal.c defines node_role. */
typedef struct {
    uint8_t     role;  /**< RCNODE_ROLE_* */
    const char *word;  /**< "pwr" or "bal", for the console */
    /** @brief Start the role's sensor and tasks. */
    void (*start)(void);
    /**
     * @brief Take a control write the role owns (RUN, BLADES, RPM).
     * @param c The write.
     */
    void (*control)(const rcnode_ctl_t *c);
    /**
     * @brief Fill the role's fields of the status: sensor, errors, resets,
     *        and RCNODE_ST_SENSOR and RCNODE_ST_PACK in flags.
     * @param s The status.
     */
    void (*status)(rcnode_status_t *s);
    /**
     * @brief Fill the role's part of the info.
     * @param i The info.
     */
    void (*info)(rcnode_info_t *i);
    /**
     * @brief The role's part of the console's line.
     * @param buf The buffer.
     * @param n Its size.
     */
    void (*line)(char *buf, size_t n);
    /** @brief Whether something holds the node awake: a pack on the XT60.
     *  @return true when it does. */
    bool (*busy)(void);
    /** @brief Put the sensor in its lowest-current state before sleep. */
    void (*sleep)(void);
    /** @brief Whether the node's sleep wakes on the NODE_WAKE_S timer to
     *  check for its work; false: it sleeps until the RESET key. */
    bool timer_wake;
    /** @brief At a timer wake, before anything else is started: the
     *  check for a pack (node_policy_wake() decides).
     *  @param vbus_uv Receives the bus voltage, µV.
     *  @return true when the sensor answered. */
    bool (*wake)(int32_t *vbus_uv);
} node_role_t;

/** @brief The role built into this image. */
extern const node_role_t node_role;

/* ---------------------------------------------------------- Bluetooth */

/**
 * @brief Start NimBLE: the GATT server, the name and the advertising.
 *
 * @param role RCNODE_ROLE_*.
 */
void node_ble_start(uint8_t role);

/**
 * @brief Queue a STREAM packet for notification.  Without a subscriber the
 *        packet is dropped and not counted; a full queue or a stack without
 *        a buffer drops it and counts it in node_ble_dropped().
 *
 * @param b The packet.
 * @param n Its length, RCNODE_PKT_MAX or less.
 */
void node_ble_stream(const uint8_t *b, size_t n);

/**
 * @brief The next STREAM sequence number.
 *
 * @return The number, wrapping at 256.
 */
uint8_t node_ble_stream_seq(void);

/**
 * @brief Publish a status: kept for reads, notified when STATUS is
 *        subscribed, and queued on STREAM while STREAM is subscribed.
 *
 * The panel's central (protocols/bleuart) subscribes to one characteristic,
 * STREAM, so the status travels there as well; its seq is STATUS's own, not
 * node_ble_stream_seq()'s.
 *
 * @param s The status; its seq and t_node are written here.
 */
void node_ble_status(rcnode_status_t *s);

/** @brief Whether a central is connected.  @return true when one is. */
bool node_ble_linked(void);

/** @brief Whether STREAM notifications are on.  @return true when they are. */
bool node_ble_subscribed(void);

/** @brief STREAM packets not sent, wrapping.  @return The count. */
uint16_t node_ble_dropped(void);

/** @brief The link's RSSI (received signal strength indication).
 *  @param dbm Receives dBm.  @return true with a link. */
bool node_ble_rssi(int8_t *dbm);

/** @brief The advertised name, "" before the stack is up.  @return The name. */
const char *node_ble_name(void);

/** @brief End the link and stop advertising, before a sleep. */
void node_ble_stop(void);

/**
 * @brief Hold advertising off, or let it run again: the display node's
 *        SETUP holds it off, so no panel links while it is open.
 * @param on true holds it off.
 */
void node_ble_hold_adv(bool on);

/** @brief The node's own name changed: the GAP name and the scan response
 *  take it; advertising that runs restarts with it. */
void node_ble_rename(void);

/* ------------------------------------------------- the node's settings */

/**
 * @brief Read the settings from NVS (node_store.h), before the role starts.
 * @param power The role keeps a shunt and an INA228 address.
 */
void node_set_start(bool power);

/** @brief The values in force.  @param out Receives them. */
void node_set_vals(nst_vals_t *out);

/** @brief The last panel's language.  @return RCNODE_LANG_*. */
uint8_t node_set_lang(void);

/**
 * @brief The display node's SETUP saved: stored, applied (the part set up
 *        again, the name advertised), CONFIG sent when asked for.
 * @param v The values.
 * @return NST_CH_* of what changed.
 */
unsigned node_set_setup_save(const nst_vals_t *v);

/**
 * @brief A panel's write the settings own: CONFIG, NAME, I228, SHUNT, and
 *        LANG, whose language is kept.  From the `node` task.
 * @param c The write.
 */
void node_set_control(const rcnode_ctl_t *c);

/** @brief The link ended: CONFIG is no longer sent. */
void node_set_link_end(void);

/** @brief The shunt in force, for INFO.  @return µΩ. */
uint16_t node_set_shunt(void);

#if !CONFIG_RCNODE_ROLE_BALANCE && !CONFIG_RCNODE_ROLE_SERVO
/**
 * @brief A new shunt for `pwr`: the part set up again at its next tick.
 * @param uohm The shunt, µΩ.
 */
void node_pwr_set_shunt(uint16_t uohm);

/**
 * @brief A new address setting for `pwr`: the search starts again.
 * @param setting RCNODE_I228_AUTO or 0x40 to 0x4F.
 */
void node_pwr_set_addr(uint8_t setting);

/**
 * @brief The power role's bus for the display node's I2C SCAN: every
 *        address of 0x08 to 0x77 probed, the INA228s found by their IDs.
 * @param out Receives the result.
 */
void node_pwr_scan(nscan_t *out);
#endif

/**
 * @brief A control write the node itself handles (SLEEP, OFF), or any write
 *        for the role, handed from the host task to the `node` task.
 *
 * @param c The write.
 */
void node_control_post(const rcnode_ctl_t *c);

/* ------------------------------------------------------------ battery */

/** @brief Set up the ADC (analogue-to-digital converter) on the divider's
 *  pin: a one-shot read on the power node (node_bat.c), the calibration of
 *  the stream's battery channel on the balance node (node_bal.c). */
void node_bat_init(void);

/**
 * @brief Read the cell: 16 samples averaged on the power node, the mean of
 *        the stream's battery samples since the last read on the balance
 *        node.
 *
 * @return mV; NODE_BAT_ERR (node_policy.h) when the ADC or its calibration
 *         fails.
 */
uint16_t node_bat_read(void);

/* ---------------------------------------------------------- the node */

/** @brief The console's hold: true keeps the node awake.  @param on The hold. */
void node_stay(bool on);

/** @brief Whether the console holds the node awake.  @return The hold. */
bool node_staying(void);

/** @brief Ask the `node` task to sleep now (the console's `sleep`). */
void node_sleep_now(void);

/** @brief Ask the `node` task to switch off now (the console's `off`). */
void node_off_now(void);

/** @brief The status as last published.  @param s Receives it. */
void node_last_status(rcnode_status_t *s);

/** @brief Start the console's REPL on USB Serial/JTAG. */
void node_console_start(void);

/** @brief The node's µs clock, 64 bits.  @return µs since the start. */
uint64_t node_us(void);

#if NODE_BOARD_DISPLAY
/* ---------------------------------------------------------- the display */

/** @brief The power role's I2C bus, which the touch controller shares.
 *  @return The bus; NULL when it did not start. */
i2c_master_bus_handle_t node_pwr_bus(void);

/** @brief Start `disp`: the display, the touch and the frames
 *         (node_disp.c). */
void node_disp_start(void);

/**
 * @brief Take a closed window for the screen's values; from `pwr`.
 * @param w The window.
 */
void node_disp_window(const rcnode_pwr_win_t *w);

/**
 * @brief LANG from the panel: the screen's language while linked.
 * @param lang RCNODE_LANG_*; another value is English.
 */
void node_disp_lang(uint8_t lang);

/** @brief Set up the ILI9341V and its SPI bus, the backlight off.
 *  @return true when the display took its set-up. */
bool node_lcd_init(void);

/** @brief Switch the backlight.  @param on On, else off. */
void node_lcd_backlight(bool on);

/**
 * @brief Send a rectangle of the frame to the display.
 * @param fb The frame, 320 x 240 RGB565.
 * @param x  The left edge.
 * @param y  The top edge.
 * @param w  The width.
 * @param h  The height.
 */
void node_lcd_flush_rect(const uint16_t *fb, int x, int y, int w, int h);
#endif

#ifdef __cplusplus
}
#endif
