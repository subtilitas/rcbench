/*
 * Two touch controllers, one contact set: the glass's GT911 at 0x5D and a
 * touch injector at 0x14 (testbench/touch-injector), merged into the frame
 * the tracker sees.  Pure C; the panel compiles the I/O around it only in a
 * build with RCBENCH_TOUCH_INJECTOR, and the host suite tests it.
 *
 * Rules:
 *
 *   - The glass's contacts come first, unchanged.  A frame holds at most
 *     TOUCH_MAX_POINTS contacts, so an injected contact is the one left out
 *     when the two together have more; a contact on the glass, STOP among
 *     them, never is.
 *   - An injected contact's track id has TOUCH_MERGE_INJECT_ID set, so it
 *     never shares an id with a finger on the glass.
 *   - A source with no new report keeps its last set.
 *   - An injector that fails TOUCH_MERGE_FAIL_DROP reads in a row loses its
 *     contacts: the tracker then sends an UP for each, as for a finger the
 *     glass stops reporting.  After that it is asked once every
 *     TOUCH_MERGE_RETRY_MS, so a dead injector costs the glass's poll at
 *     most one failed read in that time.
 *   - Touch is as old as the older of the two sources
 *     (touch_merge_age()): an injector that stops answering is touch that
 *     stops answering, and the bench goes SAFE as it does for the glass.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Set in the track id of every injected contact. */
#define TOUCH_MERGE_INJECT_ID  0x80u

/** Failed injector reads in a row before its contacts are dropped. */
#define TOUCH_MERGE_FAIL_DROP  3u

/** Once dropped, the injector is asked again after this many ms. */
#define TOUCH_MERGE_RETRY_MS   100u

typedef struct {
    touch_point_t glass[TOUCH_MAX_POINTS];
    uint8_t       glass_n;
    touch_point_t inject[TOUCH_MAX_POINTS];
    uint8_t       inject_n;
    uint8_t       fails;        /**< failed injector reads in a row      */
    uint32_t      last_try_ms;  /**< when the injector was last asked    */
} touch_merge_t;

void touch_merge_reset(touch_merge_t *m);

/**
 * The glass reported a new contact set of @p n points (0 when every finger
 * lifted).  A count outside 0..TOUCH_MAX_POINTS is clamped.
 */
void touch_merge_glass(touch_merge_t *m, const touch_point_t *pts, int n);

/**
 * Whether the injector is due to be asked at @p now_ms: on every poll while
 * it answers, and every TOUCH_MERGE_RETRY_MS once it has failed
 * TOUCH_MERGE_FAIL_DROP times in a row.
 */
bool touch_merge_inject_due(const touch_merge_t *m, uint32_t now_ms);

/**
 * The injector answered at @p now_ms.  @p fresh false means it had no new
 * report and @p pts is not read.  Returns true when the merged set changed.
 */
bool touch_merge_inject(touch_merge_t *m, const touch_point_t *pts, int n,
                        bool fresh, uint32_t now_ms);

/**
 * An injector read failed at @p now_ms.  Returns true when this failure
 * dropped injected contacts, which changes the merged set.
 */
bool touch_merge_inject_failed(touch_merge_t *m, uint32_t now_ms);

/**
 * The merged set into @p out: the glass's contacts, then the injector's,
 * at most @p max in all.  Returns the number written.
 */
int touch_merge_frame(const touch_merge_t *m, touch_point_t *out, int max);

/** How old touch is: the older of the two sources' ages, in ms. */
static inline uint32_t touch_merge_age(uint32_t glass_age_ms,
                                       uint32_t inject_age_ms)
{
    return glass_age_ms > inject_age_ms ? glass_age_ms : inject_age_ms;
}

#ifdef __cplusplus
}
#endif
