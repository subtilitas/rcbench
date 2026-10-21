/*
 * The bench log's INA3221 columns with SENSE_MAP in force (protocol 4.25):
 * one group of three per channel that watches something, named after what
 * it watches: FAST's channel first, then the others in channel order, at
 * most LOG_COLS_GROUPS of them.  The reader takes 24 columns
 * (LOG_MAX_COLUMNS), and the bench file's other columns are 14 of them, so
 * a channel past the third watching one is not logged.
 *
 * A channel takes a group when its part is enabled, its shunt is not 0
 * and its WATCH is not NONE.  The group is "<target> current (A);
 * <target> max (A); <target> voltage (V)": the window's mean and highest
 * current and its lowest bus voltage.  <target> is "out N" for output
 * channel N, "port N" for programming port N and "supply" for the supply
 * rail.  Two channels that watch the same target each name their channel
 * after it, "out 3 0x41 ch2", so no two columns share a name.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_COLS_MAX     LINK_SM_CHANNELS
/** The groups a file carries: 14 columns and 3 groups of 3 fill the 24
 *  the reader takes. */
#define LOG_COLS_GROUPS  3u
/** "supply 0x43 ch3" and its terminator. */
#define LOG_COL_NAME_MAX 20u

typedef struct {
    uint8_t n;                               /**< groups                 */
    uint8_t ch[LOG_COLS_MAX];                /**< each group's channel   */
    char    name[LOG_COLS_MAX][LOG_COL_NAME_MAX];
} log_cols_t;

/** The groups SENSE_MAP registers 0 to 25 @p map make, into @p out; none
 *  for a NULL map. */
void log_cols_from_map(const uint16_t *map, log_cols_t *out);

#ifdef __cplusplus
}
#endif
