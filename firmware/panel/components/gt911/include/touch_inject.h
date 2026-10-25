/**
 * @file
 * @brief The touch injector's side of the touch task, in a build with
 * RCBENCH_TOUCH_INJECTOR, the default (idf.py -D RCBENCH_TOUCH_INJECTOR=OFF
 * build leaves it out).
 *
 * The injector (testbench/touch-injector) answers as a GT911 at 0x14 on the
 * touch bus and reports the product ID TOUCH_MERGE_INJECT_PRODUCT ("9INJ"),
 * which no Goodix controller reports.  touch_init() asks for it once,
 * before the touch task starts.
 * One that does not answer then is off until the next start: 0x14 is not
 * read or probed again, and touch is the glass alone, as in a build without
 * the injector.  One that first answers later is not seen until the next
 * start.  While an injector that answered at start-up is
 * attached, the touch task reads it after the glass on every poll and
 * merges the two contact sets (touch_merge.h), and touch_age_ms() is the
 * older of the two ages.  One silent for TOUCH_MERGE_DETACH_MS (1000ms) is
 * detached: its age no longer counts, and the touch task reads its product
 * ID once every TOUCH_MERGE_PROBE_MS (1000ms) until it answers, which
 * re-attaches it.  The product ID decides, not the address
 * (touch_merge_role()):
 *
 *   - glass at 0x5D, "9INJ" at 0x14: the glass and the injector, merged;
 *   - glass at 0x5D, another GT911 at 0x14: the glass alone, 0x14 not read;
 *   - nothing at 0x5D, "9INJ" at 0x14: the injector alone, touch_init()
 *     maps it with the injector's map and never turns it;
 *   - nothing at 0x5D, another GT911 at 0x14: the glass, which latched
 *     0x14 at its reset, with the glass's mirrors, rotation and turn; no
 *     injector is looked for.
 *
 * The injector's contacts are panel pixels of the UI's area, mapped
 * unmirrored (touch_inject_map()) before they are merged with the glass's.
 * A build with RCBENCH_TOUCH_INJECTOR=OFF compiles none of this; it
 * addresses 0x14 only as the glass's alternate address, when nothing
 * answers at 0x5D, and reads whatever answers there as the glass.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#include "touch.h"
#include "touch_merge.h"
#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The injector's I2C address: the GT911's second strap address. */
#define TOUCH_INJECT_ADDR TOUCH_MERGE_INJECT_ADDR

/**
 * @brief Look for the injector on @p bus beside the controller touch_init()
 *        found.
 *
 * TOUCH_ROLE_INJECTOR: that controller is the injector, alone, and no
 * second handle is made.  The glass at TOUCH_INJECT_ADDR: nothing is looked
 * for.  The glass at 0x5D: 0x14 is probed, and attached only when it
 * reports TOUCH_MERGE_INJECT_PRODUCT.  Never fails touch: an absent
 * injector is the normal case.
 *
 * @param bus The touch bus.
 * @param glass_addr The address that controller answered on.
 * @param glass_role Its role (touch_merge_role()).
 * @param now_ms The clock.
 */
void touch_inject_attach(i2c_master_bus_handle_t bus, uint16_t glass_addr,
                         touch_role_t glass_role, uint32_t now_ms);

/**
 * @brief The map for the injector's contacts: panel pixels of the UI's
 *        BOARD_UI_H_RES x BOARD_UI_V_RES area, unmirrored.
 * @return The map; touch_init() also takes it for the injector alone
 *         (TOUCH_ROLE_INJECTOR).
 */
touch_map_t touch_inject_map(void);

/**
 * @brief Merge the glass's set with the injector's last one.  No bus traffic.
 * @param pts The glass's points in, the merged set out.
 * @param count Their count in and out.
 * @param fresh Whether the glass reported.
 * @return Whether there is a new merged frame.
 */
bool touch_inject_glass(touch_point_t *pts, int *count, bool fresh);

/**
 * @brief Read the injector when it is attached and due, detach it when it
 *        has been silent too long, and probe it when it is detached and due.
 * @param pts The merged set, written when this returns true.
 * @param count Its count.
 * @param now_ms The clock.
 * @return True when its contacts changed or it was detached.
 */
bool touch_inject_read(touch_point_t *pts, int *count, uint32_t now_ms);

/**
 * @brief Mark the UP of every injected contact the merge cut to make room
 *        for the glass's TOUCH_FLAG_NO_TAP (touch_merge_mark_cut()).
 * @param evts The tracker's events for the frame just published.
 * @param n How many.
 */
void touch_inject_mark(touch_event_t *evts, int n);

/**
 * @brief How old the injector's share of touch is.
 * @param now_ms The clock.
 * @return Milliseconds since the injector last answered; 0 when none is
 *         attached, and from TOUCH_MERGE_DETACH_MS on, when it counts as
 *         detached.
 */
uint32_t touch_inject_age_ms(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
