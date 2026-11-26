/**
 * @file node_setup.h
 * @brief The display node's boot splash and its own SETUP without
 *        hardware: the screens, their taps, the timers and the edits.
 *        Pure C, under the host suite (test_node_setup); drawn by
 *        node_setup_view.c (tools/render_node.py).
 *
 * The splash.  After power-on the screen shows, for NS_SPLASH_MS (10s),
 * the product name, the node's role, its firmware and "JuWi made", with a
 * SETUP button and a bar of the time left.  A tap on SETUP opens SETUP;
 * no tap goes to the meter (node_view.h) when the 10s end.  SETUP is
 * offered only while no panel is linked: a link during the 10s goes to the
 * meter at once, and a tap on SETUP with a link stands changes nothing.
 *
 * SETUP.  Four rows and two buttons: NAME (the name keyboard), I2C SCAN
 * (the bus's answering addresses, node_scan.h), INA228 ADDRESS (AUTO or a
 * fixed address picked from the scan's INA228s), SHUNT (the keypad, 50 to
 * 10,000µΩ), BACK and SAVE.  The rows edit a copy; SAVE hands it to the
 * caller (NS_ACT_SAVE), BACK drops it (NS_ACT_LEAVE).  Both go to the
 * meter.  NS_IDLE_MS (60s) without a tap in SETUP is BACK; so is a link
 * that stands (the caller holds advertising off while SETUP is open, so a
 * link is not expected there).  Only taps act: no hold, no swipe.
 *
 * The keyboard types A to Z, 0 to 9, "-", "_", "." and SPACE, 16
 * characters at most, the characters rcnode_uname_ok() takes; OK drops the
 * spaces at either end.  An empty name is no name.  The keypad types up
 * to 5 digits; OK refuses a shunt outside 50 to 10,000µΩ and shows the
 * range in the warning colour.
 *
 * Texts: English, or German when the last panel linked wrote LANG German
 * (node_store.h); "JuWi made" is the same in both.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "node_scan.h"
#include "node_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Timing, ms. @{ */
#define NS_SPLASH_MS 10000u  /**< the splash, then the meter */
#define NS_IDLE_MS   60000u  /**< SETUP without a tap, then BACK */
/** @} */

/** @brief The screen shown. */
typedef enum {
    NS_SPLASH = 0,  /**< the boot splash */
    NS_METER,       /**< the meter (node_view.h) */
    NS_LIST,        /**< SETUP's rows */
    NS_NAME,        /**< NAME's keyboard */
    NS_SCAN,        /**< I2C SCAN's result */
    NS_ADDR,        /**< INA228 ADDRESS's choice */
    NS_SHUNT,       /**< SHUNT's keypad */
} ns_mode_t;

/** @brief What a step asks of the caller. */
typedef enum {
    NS_ACT_NONE = 0,  /**< nothing */
    NS_ACT_SCAN,      /**< run the scan, then ns_scan_done() */
    NS_ACT_SAVE,      /**< store edit (nst_setup_save()), restart the meter */
    NS_ACT_LEAVE,     /**< back to the meter, nothing stored */
} ns_act_t;

/** @brief The targets a tap can hit; the drawing's boxes (ns_box()). */
typedef enum {
    NS_T_NONE = 0,
    NS_T_SETUP,       /**< the splash's SETUP button */
    NS_T_ROW0,        /**< SETUP's NAME row; I2C SCAN, ADDRESS and SHUNT follow */
    NS_T_ROW3 = NS_T_ROW0 + 3,
    NS_T_LEFT,        /**< the bottom left button: BACK, or AGAIN on a scan */
    NS_T_RIGHT,       /**< the bottom right button: SAVE, or BACK on a scan */
    NS_T_KEY0,        /**< a keyboard's or keypad's first key */
    NS_T_KEY_LAST = NS_T_KEY0 + 43,
    NS_T_AUTO,        /**< the address's AUTO */
    NS_T_CHIP0,       /**< the first address chip of a scan or a choice */
    NS_T_CHIP_LAST = NS_T_CHIP0 + 35,
} ns_target_t;

/** @brief The keyboard's keys after its 40 characters. @{ */
#define NS_KB_CHARS  40                 /**< 4 rows of 10, DEL the last */
#define NS_KB_DEL    39                 /**< the 40th: DEL */
#define NS_KB_SPACE  NS_KB_CHARS        /**< SPACE */
#define NS_KB_CLR    (NS_KB_CHARS + 1)  /**< CLR */
#define NS_KB_CANCEL (NS_KB_CHARS + 2)  /**< CANCEL */
#define NS_KB_OK     (NS_KB_CHARS + 3)  /**< OK */
#define NS_KB_KEYS   (NS_KB_CHARS + 4)  /**< the count */
/** @} */

/** @brief The keypad's keys: 0 to 9, then these. @{ */
#define NS_KP_DEL    10  /**< DEL */
#define NS_KP_CLR    11  /**< CLR */
#define NS_KP_CANCEL 12  /**< CANCEL */
#define NS_KP_OK     13  /**< OK */
#define NS_KP_KEYS   14  /**< the count */
/** @} */

/** @brief The most chips a scan screen shows; past it a "+N". */
#define NS_SCAN_CHIPS 30
/** @brief The most chips the address choice shows. */
#define NS_ADDR_CHIPS 18

/** @brief The splash's, and SETUP's, state. */
typedef struct {
    uint8_t    mode;         /**< ns_mode_t */
    uint8_t    lang;         /**< RCNODE_LANG_* */
    uint8_t    fw[3];        /**< the firmware, MAJOR, MINOR, PATCH */
    uint32_t   t_start;      /**< the splash's start */
    uint32_t   t_touch;      /**< SETUP's last tap */
    nst_vals_t edit;         /**< the values SETUP edits */
    nscan_t    scan;         /**< the last scan */
    char       text[RCNODE_UNAME_MAX + 1u]; /**< the keyboard's text */
    char       num[6];       /**< the keypad's digits */
    bool       refused;      /**< the keypad's OK refused the value */
    uint16_t   rev;          /**< counts changes the drawing shows */
} ns_t;

/**
 * @brief The splash at power-on.
 * @param s      The state.
 * @param vals   The values in force: SETUP starts its copy from them.
 * @param lang   RCNODE_LANG_*.
 * @param fw     The firmware, 3 bytes; NULL is 0.0.0.
 * @param now_ms The node's ms clock.
 */
void ns_init(ns_t *s, const nst_vals_t *vals, uint8_t lang,
             const uint8_t fw[3], uint32_t now_ms);

/**
 * @brief Time passes: the splash ends at NS_SPLASH_MS or with a link;
 *        SETUP ends after NS_IDLE_MS without a tap or with a link.
 * @param s      The state.
 * @param now_ms The clock.
 * @param linked A panel is linked.
 * @return NS_ACT_LEAVE when SETUP ended; NS_ACT_NONE otherwise, the end
 *         of the splash included (mode is then NS_METER).
 */
ns_act_t ns_tick(ns_t *s, uint32_t now_ms, bool linked);

/**
 * @brief A tap at screen px @p x, @p y.
 * @param s      The state.
 * @param x      Screen px.
 * @param y      Screen px.
 * @param now_ms The clock.
 * @param linked A panel is linked: SETUP is not opened.
 * @return What the caller does.
 */
ns_act_t ns_tap(ns_t *s, int x, int y, uint32_t now_ms, bool linked);

/**
 * @brief The scan the caller ran for NS_ACT_SCAN.
 * @param s The state.  @param r Its result.
 */
void ns_scan_done(ns_t *s, const nscan_t *r);

/**
 * @brief Whether SETUP is open: one of its screens shows.
 * @param s The state.
 * @return true from the SETUP tap to SAVE, BACK or the idle end.
 */
bool ns_in_setup(const ns_t *s);

/**
 * @brief The splash's time left.
 * @param s The state.  @param now_ms The clock.
 * @return ms, 0 past NS_SPLASH_MS or off the splash.
 */
uint32_t ns_splash_left(const ns_t *s, uint32_t now_ms);

/**
 * @brief The target under a point on the screen shown.
 * @param s The state.  @param x Screen px.  @param y Screen px.
 * @return ns_target_t; NS_T_NONE for none.
 */
int ns_hit(const ns_t *s, int x, int y);

/**
 * @brief A target's box on the screen shown.
 * @param s   The state.
 * @param t   ns_target_t.
 * @param out Receives the box.
 * @return false when the screen has no such target.
 */
bool ns_box(const ns_t *s, int t, gfx_rect_t *out);

/**
 * @brief The address chip @p i of the screen shown: the scan's answering
 *        addresses, or on the address choice the INA228s found and the
 *        fixed address in force.
 * @param s The state.  @param i The chip.
 * @return The address; 0 past the last.
 */
uint8_t ns_chip_addr(const ns_t *s, int i);

/**
 * @brief The address chips the screen shown has.
 * @param s The state.
 * @return The count, NS_SCAN_CHIPS or NS_ADDR_CHIPS at most.
 */
int ns_chips(const ns_t *s);

/**
 * @brief A keyboard key's label: the character, or SPACE, CLR, CANCEL,
 *        OK, DEL in the state's language.
 * @param s The state.  @param key 0 to NS_KB_KEYS - 1.
 * @return The label; "" out of range.
 */
const char *ns_kb_label(const ns_t *s, int key);

/**
 * @brief A keypad key's label.
 * @param s The state.  @param key 0 to NS_KP_KEYS - 1.
 * @return The label; "" out of range.
 */
const char *ns_kp_label(const ns_t *s, int key);

/** @brief The texts the screens draw, by language. */
typedef enum {
    NS_TX_ROLE = 0,  /**< "POWER NODE" */
    NS_TX_FW,        /**< "FIRMWARE %u.%u.%u" */
    NS_TX_CREDIT,    /**< "JuWi made" */
    NS_TX_SETUP,     /**< "SETUP" */
    NS_TX_TITLE,     /**< "NODE SETUP" */
    NS_TX_NAME,      /**< "NAME" */
    NS_TX_SCAN,      /**< "I2C SCAN" */
    NS_TX_ADDR,      /**< "INA228 ADDRESS" */
    NS_TX_SHUNT,     /**< "SHUNT" */
    NS_TX_BACK,      /**< "BACK" */
    NS_TX_SAVE,      /**< "SAVE" */
    NS_TX_AGAIN,     /**< "AGAIN" */
    NS_TX_NONE,      /**< "NONE": no name */
    NS_TX_TAP_SCAN,  /**< "TAP TO SCAN" */
    NS_TX_FOUND,     /**< "%u FOUND" */
    NS_TX_AUTO,      /**< "AUTO" */
    NS_TX_NO_ANSWER, /**< "NO ADDRESS ANSWERS" */
    NS_TX_NO_INA,    /**< "NO INA228 FOUND" */
    NS_TX_LEGEND,    /**< "INA228" beside its colour */
    NS_TX_RANGE,     /**< "50-10000uΩ" */
    NS_TX_MORE,      /**< "+%u MORE" */
    NS_TX_COUNT
} ns_text_t;

/**
 * @brief A text in @p lang.
 * @param lang RCNODE_LANG_*; another value is English.
 * @param t    ns_text_t.
 * @return The text; "" out of range.
 */
const char *ns_text(uint8_t lang, int t);

/**
 * @brief A row's value as SETUP's list shows it: the name or NONE, the
 *        scan's count or TAP TO SCAN, AUTO or the address, the shunt.
 * @param s   The state.
 * @param row 0 to 3.
 * @param buf Receives the text.
 * @param n   Its size.
 */
void ns_row_value(const ns_t *s, int row, char *buf, size_t n);

#ifdef __cplusplus
}
#endif
