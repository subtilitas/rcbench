/*
 * SETUP > SENSORS' list: which SENSORS rows show, the headings over them,
 * and what each fitted part is doing.  The list is SETUP's own list
 * renderer (settings_screen.c); this file decides its rows.
 *
 * The list: the heading INA3221 PARTS over 0x40 FITTED to 0x43 FITTED,
 * then per fitted part a heading "INA3221 0x4n" with the part's state on
 * its right, over its channels' WATCHES, SHUNT and LIMIT rows.  A part not fitted
 * has no heading and no rows.
 *
 * A row of a part the IOmcu does not read is marked as SETUP marks a value
 * the IOmcu does not hold: its edge and value in the warning colour and a
 * line in place of its help (sensors_rows_why()).  That is a part the
 * SENSE_MAP page refused, one with every channel off, one on the INA228's
 * address, and on an IOmcu older than 4.25 every fitted part but the one
 * SENSE reads.  A LIMIT row is marked as well while the IOmcu does not hold
 * its limit: owed, refused, or any limit on an IOmcu older than 4.27.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "sense_map_link.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The parts' states, sense_map_link_status()'s word: 4 bits a part, part 0
 * lowest.  A change repaints SETUP.  0 at start: every part
 * SENS_PART_UNKNOWN.
 */
void sensors_rows_set_status(uint16_t word);

/** Part @p a's state as last set. */
sens_part_t sensors_rows_status(int a);

/** The limits' marks, sense_limit_link_unheld()'s 12 bits, and
 *  sense_limit_link_status() in bits 12 to 15.  A change repaints SETUP. */
void sensors_rows_set_limits(uint16_t word);

/** Whether SETUP lists @p id: any id that is not a SENSORS row, a FITTED
 *  row, and the channel rows of a fitted part. */
bool sensors_rows_listed(setting_id_t id);

/** The part whose heading stands above @p id, 0 to 3: @p id is the first
 *  row of a fitted part.  -1 otherwise. */
int sensors_rows_heading(setting_id_t id);

/** Part @p a's heading, "INA3221 0x4n", into @p buf. */
const char *sensors_rows_title(int a, char *buf, size_t n);

/** Part @p a's state in words, and the colour it is drawn in. */
const char *sensors_rows_state(int a);
gfx_color_t sensors_rows_state_color(int a);

/** Whether the row of @p id is marked: a SENSORS row of a part the IOmcu
 *  does not read, and a LIMIT row the IOmcu does not hold
 *  (sensors_rows_set_limits()). */
bool sensors_rows_unheld(setting_id_t id);

/** The line a marked row shows in place of its help. */
const char *sensors_rows_why(setting_id_t id);

/** Channel @p c (0 to 11) as the screens name it, "0x41 CH2", into @p buf;
 *  "" for no channel. */
const char *sensors_rows_channel(int c, char *buf, size_t n);

/** The same over SERVO's plot and on the TEST card: "INA 0x41 CH2", or
 *  "INA3221" with none; into @p buf. */
const char *sensors_rows_short(char *buf, size_t n);

/** The servo rail's INA3221 meter as the reports and the LIMIT RUN line
 *  name it: the channel watching the tester's target, "INA3221 0x41 CH2",
 *  or "INA3221" with none; into @p buf. */
const char *sensors_rows_meter(char *buf, size_t n);

/** The lowest channel with a shunt on a fitted part whose WATCH is the
 *  page code @p code (LINK_SM_WATCH_*), 0 to 11; -1 for none. */
int sensors_rows_watcher(uint16_t code);

/** Each programming port's WATCHED BY line on OUTPUTS, from the settings
 *  as they stand (outputs_screen_set_port_watch()). */
void sensors_rows_publish_watch(void);

#ifdef __cplusplus
}
#endif
