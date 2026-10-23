/**
 * @file
 * @brief Two touch controllers, one contact set: the glass's GT911 at 0x5D and a
 * touch injector at 0x14 (testbench/touch-injector), merged into the frame
 * the tracker sees.  Pure C; the panel compiles the I/O around it in a
 * build with RCBENCH_TOUCH_INJECTOR (the default), and the host suite tests
 * it.
 *
 * Rules:
 *
 *   - The glass's contacts come first, unchanged.  A frame holds at most
 *     TOUCH_MAX_POINTS contacts, so an injected contact is the one left out
 *     when the two together have more; a contact on the glass, STOP among
 *     them, never is.
 *   - The injected contact left out is cut: the newest first, then the last
 *     in the injector's report.  A cut contact stays out until the injector
 *     lifts it, so a finger lifting from the glass never brings it back as
 *     a new press.  The UP the tracker makes for it carries
 *     TOUCH_FLAG_NO_TAP (touch_merge_mark_cut()): it did not lift, and only
 *     a control that stops what the bench drives acts on it.
 *   - An injected contact's track id has TOUCH_MERGE_INJECT_ID set, so it
 *     never shares an id with a finger on the glass, whatever the two
 *     report and wherever they are: a finger and an injected contact on the
 *     same pixel are two contacts.
 *   - A source with no new report keeps its last set.
 *   - An injector that fails TOUCH_MERGE_FAIL_DROP reads in a row loses its
 *     contacts: the tracker then sends an UP for each, as for a finger the
 *     glass stops reporting.  After that it is asked once every
 *     TOUCH_MERGE_RETRY_MS, so a dead injector costs the glass's poll at
 *     most one failed read in that time.
 *   - Touch is as old as the older of the two sources
 *     (touch_merge_age()): an injector that stops answering is touch that
 *     stops answering, and the bench goes SAFE as it does for the glass.
 *   - An injector with no good read for TOUCH_MERGE_DETACH_MS is detached
 *     (touch_merge_expire()): its contacts are dropped, it is no longer
 *     read, and its age no longer counts (touch_merge_inject_age()), so
 *     the glass alone decides how old touch is.  By then touch has read as
 *     dead and the bench has disarmed; nothing arms again without a new
 *     arm.
 *   - The injector is looked for once, at start-up (touch_merge_start()).
 *     One that does not answer then is absent until the next start: it is
 *     never read, probed or counted, whatever the bus does later, and the
 *     glass is the only touch.
 *   - A detached injector is probed once every TOUCH_MERGE_PROBE_MS.  The
 *     first probe it answers re-attaches it with no contacts and an age
 *     of 0 (touch_merge_probe()).  Its age stays under
 *     TOUCH_MERGE_DETACH_MS whatever it does, so an injector that answers
 *     and falls silent by turns withholds touch for at most the span from
 *     touch reading as dead to the detach.
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

/** @brief Set in the track id of every injected contact. */
#define TOUCH_MERGE_INJECT_ID  0x80u

/** @brief Failed injector reads in a row before its contacts are dropped. */
#define TOUCH_MERGE_FAIL_DROP  3u

/** @brief Once dropped, the injector is asked again after this many ms. */
#define TOUCH_MERGE_RETRY_MS   100u

/** @brief An injector with no good read for this many ms is detached. */
#define TOUCH_MERGE_DETACH_MS  1000u

/** @brief A detached injector is probed at most once in this many ms. */
#define TOUCH_MERGE_PROBE_MS   1000u

/** @brief The two sources' last sets and the injector's health. */
typedef struct {
    touch_point_t glass[TOUCH_MAX_POINTS];  /**< the glass's last set      */
    uint8_t       glass_n;                  /**< its count                 */
    touch_point_t inject[TOUCH_MAX_POINTS]; /**< the injector's, ids marked */
    uint8_t       inject_n;                 /**< its count                 */
    bool          cut[TOUCH_MAX_POINTS];    /**< inject[i] is left out     */
    bool          shown[TOUCH_MAX_POINTS];  /**< inject[i] is in the frame */
    uint8_t       fails;        /**< failed injector reads in a row      */
    uint32_t      last_try_ms;  /**< when the injector was last asked    */
    bool          detached;     /**< silent for TOUCH_MERGE_DETACH_MS    */
    bool          absent;       /**< not found at start-up; never probed */
    uint32_t      ok_ms;        /**< the last good read or probe         */
} touch_merge_t;

/**
 * @brief Forget both sets: no contacts, the injector attached at 0 ms.
 * @param m The merge; NULL does nothing.
 */
void touch_merge_reset(touch_merge_t *m);

/**
 * @brief Reset, with the injector attached and answering at @p now_ms.
 * @param m The merge.
 * @param now_ms The clock.
 */
void touch_merge_attach(touch_merge_t *m, uint32_t now_ms);

/**
 * @brief The start-up look for the injector.
 *
 * An answer is touch_merge_attach().  No answer makes the injector absent
 * until the next start: nothing in this header attaches, reads or probes
 * it, and touch_merge_attached() stays false.
 *
 * @param m The merge.
 * @param answered True when 0x14 answered as a GT911.
 * @param now_ms The clock.
 */
void touch_merge_start(touch_merge_t *m, bool answered, uint32_t now_ms);

/**
 * @brief Whether the injector is attached: false once detached, until a
 *        probe.
 * @param m The merge.
 * @return Whether it is attached.
 */
bool touch_merge_attached(const touch_merge_t *m);

/**
 * @brief Detach the injector when its last good read is
 *        TOUCH_MERGE_DETACH_MS or more old.  Detaching drops its contacts.
 * @param m The merge.
 * @param now_ms The clock.
 * @return True when this call detached it.
 */
bool touch_merge_expire(touch_merge_t *m, uint32_t now_ms);

/**
 * @brief Whether a detached injector is due to be probed:
 *        TOUCH_MERGE_PROBE_MS after the detach and after every probe.
 *        Never for an absent one.
 * @param m The merge.
 * @param now_ms The clock.
 * @return Whether to probe.
 */
bool touch_merge_probe_due(const touch_merge_t *m, uint32_t now_ms);

/**
 * @brief A detached injector was probed.
 * @param m The merge.
 * @param answered True when it answered as a GT911.
 * @param now_ms The clock.
 * @return True when this re-attached it.
 */
bool touch_merge_probe(touch_merge_t *m, bool answered, uint32_t now_ms);

/**
 * @brief The glass reported a new contact set (0 points when every finger
 *        lifted).  Injected contacts the frame no longer has room for are
 *        cut.
 * @param m The merge.
 * @param pts The points, in panel pixels.
 * @param n How many; a count outside 0..TOUCH_MAX_POINTS is clamped.
 */
void touch_merge_glass(touch_merge_t *m, const touch_point_t *pts, int n);

/**
 * @brief Whether the injector is due to be asked: on every poll while it
 *        answers, and every TOUCH_MERGE_RETRY_MS once it has failed
 *        TOUCH_MERGE_FAIL_DROP times in a row.
 * @param m The merge.
 * @param now_ms The clock.
 * @return Whether to read it.
 */
bool touch_merge_inject_due(const touch_merge_t *m, uint32_t now_ms);

/**
 * @brief The injector answered at @p now_ms.
 *
 * A contact it reports keeps its cut by track id; a new one is cut when
 * the frame has no room for it.  A cut contact the injector lifts is
 * forgotten, and its id is free again.
 *
 * @param m The merge.
 * @param pts The points, in panel pixels; not read when @p fresh is false.
 * @param n How many.
 * @param fresh False when it had no new report.
 * @param now_ms The clock.
 * @return True when the merged set changed.
 */
bool touch_merge_inject(touch_merge_t *m, const touch_point_t *pts, int n,
                        bool fresh, uint32_t now_ms);

/**
 * @brief An injector read failed.
 * @param m The merge.
 * @param now_ms The clock.
 * @return True when this failure dropped injected contacts, which changes
 *         the merged set.
 */
bool touch_merge_inject_failed(touch_merge_t *m, uint32_t now_ms);

/**
 * @brief The merged set: the glass's contacts, then the injector's that are
 *        not cut, TOUCH_MAX_POINTS at most.
 * @param m The merge.
 * @param out Where the set goes.
 * @param max The room in @p out.
 * @return The number written.
 */
int touch_merge_frame(const touch_merge_t *m, touch_point_t *out, int max);

/**
 * @brief Mark the UP of every cut injected contact TOUCH_FLAG_NO_TAP.
 *
 * Called on the tracker's events for the frame touch_merge_frame() gave.
 * A contact cut to make room for the glass's did not lift; its UP ends the
 * press as TOUCH_FLAG_NO_TAP says.  Every other event is left as it is.
 *
 * @param m The merge.
 * @param evts The events.
 * @param n How many.
 * @return How many UPs were marked.
 */
int touch_merge_mark_cut(const touch_merge_t *m, touch_event_t *evts, int n);

/**
 * @brief How old touch is: the older of the two sources' ages.
 * @param glass_age_ms The glass's age, in ms.
 * @param inject_age_ms The injector's share (touch_merge_inject_age()).
 * @return The older, in ms.
 */
static inline uint32_t touch_merge_age(uint32_t glass_age_ms,
                                       uint32_t inject_age_ms)
{
    return glass_age_ms > inject_age_ms ? glass_age_ms : inject_age_ms;
}

/**
 * @brief The injector's share of touch age.
 * @param attached Whether it is attached.
 * @param ok_ms Its last good read.
 * @param now_ms The clock.
 * @return The time since @p ok_ms, in ms, while it is attached and that
 *         time is under TOUCH_MERGE_DETACH_MS; 0 otherwise, so the glass
 *         alone decides.
 */
static inline uint32_t touch_merge_inject_age(bool attached, uint32_t ok_ms,
                                              uint32_t now_ms)
{
    const uint32_t age = (uint32_t)(now_ms - ok_ms);
    return (attached && age < TOUCH_MERGE_DETACH_MS) ? age : 0u;
}

#ifdef __cplusplus
}
#endif
