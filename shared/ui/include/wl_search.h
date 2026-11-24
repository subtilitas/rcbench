/**
 * @file
 * @brief SEARCH: the search screen of SETUP > WIRELESS, one for the Dial,
 *        the power node and the balance node.
 *
 * A tap on SCAN FOR DIAL or SCAN FOR NODE opens it and asks for a scan of
 * BLEU_SCAN_MS (10s); a tap on BOUND DIAL or BOUND NODE opens it without
 * one, on the last scan's list.  A scan the device's entry refuses (ARMED,
 * a scan running, Bluetooth not ready) says why on the band, as the row
 * did, and the screen opens on the last list.
 *
 * The screen shows, left, the scan's state (SCANNING with the seconds
 * left, FOUND n, NONE FOUND, STOPPED BY ARM or NO SCAN YET) over the
 * devices the scan found, up to BLEU_FOUND_MAX (8), each with its name or
 * address and its RSSI, the bound one marked; right, the device's kind,
 * its link state, the bound device and SCAN AGAIN.  The home tag returns
 * to SETUP > WIRELESS on the device's entry.
 *
 * A tap on a found device opens the bind question in place of the list: the
 * device's name, address and RSSI, CANCEL and BIND.  BIND asks the
 * entry's bind of that device by its index in the list.  It is refused,
 * with the band's text and no bind asked, while a scan runs, and when the
 * list does not hold the device at the index the tap took; the
 * question closes either way.  After a bind the screen stays and marks
 * the device as the bound one once the controller has bound it.
 *
 * UNBIND, under SCAN AGAIN, is a 2s hold (ui_hold_t): held to UI_HOLD_S it
 * asks the entry's unbind once, which forgets the bound device, ends its
 * link and keeps the empty address in SETUP with a save asked.  It takes
 * the hold only while the view names a bound device; with none it is
 * drawn grey and a tap says NOTHING IS BOUND on the band.  A bound device
 * that goes while the finger holds (another bind, the device switched
 * OFF) ends the hold without an unbind.  The bench's state does not
 * refuse it: an unbind while ARMED acts on what uses the device as its
 * lost link does (the Dial: an error, SAFE; the power node read by a run:
 * the dropout rule; the balance node: stale readings).
 *
 * The screen keeps no copy of the list: each frame it reads the entry's
 * view (wls_device_t's view()).  A view that changes (RSSI, the scan's
 * progress, the link) repaints it and never closes the question or drops
 * a press.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "ui_focus.h"
#include "ui_screen.h"
#include "ui_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Sizes of a found device, the views' (BLEU_ADDR_TEXT_MAX,
 *         BLEU_NAME_MAX, BLEU_FOUND_MAX). @{ */
#define WLS_ADDR_MAX  20u /**< "A0:B1:C2:7F:3A:10 R" + NUL */
#define WLS_NAME_MAX  24u /**< a name from a scan + NUL */
#define WLS_FOUND_MAX 8u  /**< devices a scan lists */
#define WLS_WORD_MAX  32u /**< the link state word + NUL */
/** @} */

/** @brief A device a scan found. */
typedef struct {
    char   addr[WLS_ADDR_MAX]; /**< its address with its type */
    char   name[WLS_NAME_MAX]; /**< its name, "" when none came */
    int8_t rssi;               /**< dBm of its last report, 127 none */
} wls_dev_t;

/** @brief What the screen shows of a device's entry. */
typedef struct {
    bool        scanning;    /**< a scan runs */
    uint8_t     scan_left_s; /**< its time left, s */
    bool        scan_done;   /**< a scan ended since the device's ON */
    bool        scan_cut;    /**< that scan was stopped by ARMED */
    uint8_t     found_n;     /**< the devices it found */
    wls_dev_t   found[WLS_FOUND_MAX]; /**< strongest first */
    char        bound_addr[WLS_ADDR_MAX]; /**< the bound one, "" none */
    char        bound_name[WLS_NAME_MAX]; /**< its name, "" none */
    char        word[WLS_WORD_MAX];       /**< the link state in words */
    gfx_color_t word_ink;                 /**< their colour */
} wls_view_t;

/** @brief A device's entry as the screen asks it. */
typedef struct {
    int          entry;         /**< its WL_ENTRY_*, for the way back */
    ui_text_id_t kind;          /**< its name: DIAL, POWER NODE, ... */
    ui_text_id_t bind_scanning; /**< the band's text for a bind behind a
                                     running scan */
    ui_text_id_t bind_gone;     /**< the band's text for a bind of a
                                     device the list does not hold */
    /** @brief The entry's view, into a zeroed @p out. */
    void (*view)(wls_view_t *out);
    /** @brief A scan, with the entry's refusals on the band. */
    void (*scan)(void);
    /** @brief The bind of found device @p i, queued for the controller. */
    void (*bind)(uint8_t i);
    /** @brief The unbind of the bound device, queued for the
     *         controller. */
    void (*unbind)(void);
} wls_device_t;

/** @brief The screen's parts a test or the guide aims at. */
typedef enum {
    WLS_PART_AGAIN = 0, /**< SCAN AGAIN */
    WLS_PART_CANCEL,    /**< the question's CANCEL */
    WLS_PART_BIND,      /**< the question's BIND */
    WLS_PART_BOX,       /**< the question's box */
    WLS_PART_LIST,      /**< the list's area, every row's */
    WLS_PART_CARD,      /**< the device's kind, state and bound device */
    WLS_PART_UNBIND,    /**< UNBIND, the 2s hold */
    WLS_PART_COUNT
} wls_part_t;

/**
 * @brief Open the screen for @p dev, from SETUP > WIRELESS.
 * @param dev  The device's entry; NULL does nothing.
 * @param scan Ask for a scan after the screen comes up, with the
 *             entry's refusals on the band.
 */
void wl_search_open(const wls_device_t *dev, bool scan);

/**
 * @brief Found device @p i of a view, for a device entry's view().
 * @param v    The view.
 * @param i    The row, 0 to WLS_FOUND_MAX - 1; past it nothing is put.
 * @param addr Its address, NULL for "".
 * @param name Its name, NULL for "".
 * @param rssi Its RSSI, dBm; 127 for none.
 */
void wl_search_put(wls_view_t *v, unsigned i, const char *addr,
                   const char *name, int8_t rssi);

/**
 * @brief The screen, for the router.
 * @return Its table.
 */
const ui_screen_t *wl_search_screen(void);

/**
 * @brief The device the screen shows.
 * @return The entry, NULL before the first wl_search_open().
 */
const wls_device_t *wl_search_device(void);

/**
 * @brief The screen's view of its device, as it reads it now.
 * @param out Receives it; zeroed with no device.
 */
void wl_search_view(wls_view_t *out);

/**
 * @brief Whether the bind question is open.
 * @return True while it shows.
 */
bool wl_search_asking(void);

/**
 * @brief The device the open question binds.
 * @return Its copy as the tap took it; NULL with no question.
 */
const wls_dev_t *wl_search_asked(void);

/**
 * @brief Whether UNBIND takes its hold: the view names a bound device and
 *        no question is open.
 * @return True while it is drawn live.
 */
bool wl_search_unbind_ready(void);

/**
 * @brief Found device @p i's row, in the body's coordinates.
 * @param i The row, 0 to WLS_FOUND_MAX - 1.
 * @return Its rectangle; an empty one at (0, 0) out of range.
 */
gfx_rect_t wl_search_row_rect(int i);

/**
 * @brief A part of the screen, in the body's coordinates.
 * @param part The part.
 * @return Its rectangle; an empty one at (0, 0) out of range.
 */
gfx_rect_t wl_search_part(wls_part_t part);

/**
 * @brief The scan's state line over the list: SCANNING with the seconds
 *        left, FOUND n, NONE FOUND, STOPPED BY ARM or NO SCAN YET.
 * @param buf Takes it.
 * @param n   Bytes at @p buf.
 * @return @p buf; "" with no buffer.
 */
const char *wl_search_state_line(char *buf, size_t n);

/**
 * @brief A found device's line: its name, else its address, and its RSSI.
 * @param d   The device.
 * @param buf Takes it.
 * @param n   Bytes at @p buf.
 * @return @p buf; "" with no buffer or no device.
 */
const char *wl_search_dev_line(const wls_dev_t *d, char *buf, size_t n);

/** @brief Repaint the screen on the next frame of each framebuffer. */
void wl_search_invalidate(void);

/**
 * @brief The Dial's focus items (ui_focus.h): the found devices and SCAN
 *        AGAIN, or the question's CANCEL and BIND.
 * @param out Receives them.
 * @return True.
 */
bool wl_search_focus(ui_focus_list_t *out);

#ifdef __cplusplus
}
#endif
