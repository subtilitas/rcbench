/*
 * SETUP > SENSORS' list: which SENSORS rows show, the headings over them,
 * and what each fitted part is doing.  The list is SETUP's own list
 * renderer (settings_screen.c); this file decides its rows.
 *
 * The list: the heading INA3221 PARTS over 0x40 FITTED to 0x43 FITTED,
 * then per fitted part a heading "INA3221 0x4n" with the part's state on
 * its right, over its channels' WATCHES and SHUNT rows.  A part not fitted
 * has no heading and no rows.
 *
 * A row of a part the IOmcu does not read is marked as SETUP marks a value
 * the IOmcu does not hold: its edge and value in the warning colour and a
 * line in place of its help (sensors_rows_why()).  That is a part the
 * SENSE_MAP page refused, one with every channel off, one on the INA228's
 * address, and on an IOmcu older than 4.25 every fitted part but the one
 * SENSE reads.
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
 *  does not read. */
bool sensors_rows_unheld(setting_id_t id);

/** The line a marked row shows in place of its help. */
const char *sensors_rows_why(setting_id_t id);

#ifdef __cplusplus
}
#endif
