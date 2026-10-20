/*
 * What sense_page.c and sense_map.c share: the SENSE set-up's checks and
 * its taking, and the map's own helpers the SENSE page calls.  Not for any
 * other module.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sense_page.h"

#ifdef __cplusplus
extern "C" {
#endif

/* @p a / @p d to the nearest, halves away from zero; @p d positive. */
static inline int64_t div_round(int64_t a, int64_t d)
{
    return (a >= 0) ? (a + d / 2) / d : -((-a + d / 2) / d);
}

/* A signed register: two's complement, held to -32767 .. 32767. */
static inline uint16_t reg_i16(int64_t v)
{
    if (v > 32767) {
        v = 32767;
    } else if (v < -32767) {
        v = -32767;
    }
    return (uint16_t)(int16_t)v;
}

/* An unsigned register, held to 0 .. 65535. */
static inline uint16_t reg_u16(int64_t v)
{
    if (v < 0) {
        return 0u;
    }
    return (v > 65535) ? 65535u : (uint16_t)v;
}

/** A SENSE set-up @p next (LINK_SN_CONFIG_COUNT registers) judged as a
 *  write is: values, pins, addresses and channels; 0 or the refusal.  Not
 *  the bank's state: the caller asks that. */
uint8_t sense_cfg_check(const sense_page_t *p, const uint16_t *next,
                        const outputs_t *o, uint64_t taken);

/** @p next in force: the generation moves, and every reading of the set-up
 *  before it goes (sense_page_forget()). */
void sense_cfg_take(sense_page_t *p, const uint16_t *next);

/** Every register core 1 fills, on both pages and the map's, cleared. */
void sense_page_forget(sense_page_t *p);

/** Whether the map @p map fits an INA3221 at @p addr. */
bool sense_map_on_addr(const uint16_t *map, uint16_t addr);

/** Whether a channel the map reads watches an output channel or a
 *  programming port: the map in force's LINK_CAP_SERVO_SENSE. */
bool sense_map_watches(const sense_page_t *p);

/** Channel @p c's shunt in 0.1 mOhm, in the map in force or the derived
 *  one; 0 for a channel off. */
uint16_t sense_map_shunt(const sense_page_t *p, unsigned c);

/** Each channel's newest window in @p s against its SENSE_LIMIT limit,
 *  once a window: OVER and OVER_COUNT. */
void sense_limit_judge(sense_page_t *p, const sense_snap_t *s);

/** The map's kept registers as the schedule's INA3221 set-up. */
void sense_map_cfg(const uint16_t *map, sense_i3221_cfg_t *out);

/** The snapshot's per-part state into SENSE_MAP's read-only registers, and
 *  every channel's window into SENSE_WIN_A and SENSE_WIN_B. */
void sense_map_publish(sense_page_t *p, const sense_snap_t *s);

#ifdef __cplusplus
}
#endif
