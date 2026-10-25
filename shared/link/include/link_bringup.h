/**
 * @file
 * @brief Both ends' link counters turned into one diagnosis.
 *
 * Wrong pins, a mismatched bit rate, a missing terminator and a firmware skew
 * all present as a link that does not work.  The counters read from both
 * ends and compared tell them apart: an IOmcu that decoded 100 requests
 * while the panel heard no answers is a return-path fault, which the panel's
 * own numbers cannot show.
 *
 * Pure arithmetic over two structs, so every diagnosis has a host test that
 * constructs the fault.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_LINK_BRINGUP_H
#define RCBENCH_LINK_BRINGUP_H

#include <stdbool.h>
#include <stdint.h>

#include "link_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The link counters of both ends, and the round-trip statistics.
 */
typedef struct {
    /* The panel's view: what it asked, and what came back. */
    uint32_t polls;
    uint32_t replies;
    uint32_t timeouts;
    uint32_t mismatches;    /**< answers to questions nobody is still asking */
    uint32_t nacks;
    uint32_t rx_crc_errors; /**< frames this end received corrupt; 0 on CAN */
    uint32_t rx_resyncs;

    /* The IOmcu's view of itself, read from the STATUS page.  Absent
     * until a status read succeeds, which is itself informative. */
    bool     have_status;
    uint32_t dev_frames;    /**< requests it decoded */
    uint32_t dev_crc_errors;
    uint32_t dev_resyncs;
    /**
     * @brief The last status read carried LINK_ST_REFUSED_LO and _HI: protocol
     * 4.21 and later.
     */
    bool     have_refused;
    /**
     * @brief CAN frames it refused to decode since its power-up, modulo 2^32; 0
     * while have_refused is false.
     */
    uint32_t dev_refused;

    /* Identity, read once at link-up. */
    bool     have_identity;
    uint16_t proto_major;
    uint16_t proto_minor;

    /* Round trip: request sent to reply accepted. */
    uint32_t rt_samples;
    uint32_t rt_min_us;
    uint32_t rt_max_us;
    uint32_t rt_mean_us;
} link_bringup_t;

/**
 * @brief The most fundamental fault a link shows.
 */
typedef enum {
    LINK_DIAG_OK = 0,
    /** @brief Nothing came back at all. */
    LINK_DIAG_SILENT,
    /** @brief It answers, but it is not speaking this protocol. */
    LINK_DIAG_PROTOCOL_MISMATCH,
    /**
     * @brief Requests arrive and answers do not: the return path, not the link.
     */
    LINK_DIAG_REPLIES_LOST,
    /**
     * @brief Frames arrive corrupt.
     *
     * Bit timing, noise, or termination.
     */
    LINK_DIAG_CORRUPT,
    /**
     * @brief Answers arrive too late to be wanted.
     *
     * A slow far end, or a timeout tighter than the round trip.
     */
    LINK_DIAG_STALE,
    /** @brief It works, and not every time. */
    LINK_DIAG_INTERMITTENT,
} link_diag_t;

/**
 * @brief Name the most fundamental thing that is wrong.
 *
 * Most fundamental, not most numerous: a silent link also has a hundred
 * timeouts, and reporting the timeouts is reporting a consequence. The order
 * the checks run in *is* the diagnosis.
 *
 * @param b The counters of both ends.
 * @return LINK_DIAG_OK before four polls have been made, else the first fault
 *       the checks find.
 */
link_diag_t link_bringup_diagnose(const link_bringup_t *b);

/**
 * @brief The counts of one report window: what happened between two
 * snapshots, for link_bringup_diagnose() to judge the recent link rather than
 * the whole session.
 *
 * Each counter of @p out is now - prev.  A counter that is lower in @p now
 * than in @p prev counts from 0 again -- an IOmcu that restarted, or a
 * 16-bit status register that wrapped -- and its window count is the value
 * in @p now.  A restarted counter that has passed its previous figure by the
 * time of @p now is not told from one that kept counting, and its window
 * count is short by the previous figure.
 *
 * Flags, identity and the round-trip figures are taken from @p now:
 * have_status, have_refused, have_identity, proto_major, proto_minor,
 * rt_samples, rt_min_us, rt_max_us and rt_mean_us.
 *
 * @param now The counters at this report.
 * @param prev The counters at the previous report; NULL for the first
 *        report, which makes the window everything since the counters
 *        started (the same as an all-zero @p prev).
 * @param out Receives the window; may be the same object as @p now.
 */
void link_bringup_window(const link_bringup_t *now,
                         const link_bringup_t *prev, link_bringup_t *out);

/**
 * @brief One line naming the fault.
 *
 * @param d The fault.
 * @return The text for d, one line.
 */
const char *link_diag_text(link_diag_t d);

/**
 * @brief One line naming what to check, in the order worth checking it.
 *
 * @param d The fault.
 * @return The hint for d, one line; empty for LINK_DIAG_OK.
 */
const char *link_diag_hint(link_diag_t d);

/* ------------------------------------------------------------ status page */

/**
 * @brief How many STATUS registers to read from an IOmcu of protocol minor @p
 * minor: LINK_ST_COUNT from LINK_MINOR_REFUSED, LINK_ST_COUNT_V420 before it.
 *
 * An older page refuses the longer read with LINK_NACK_BAD_RANGE, which would
 * lose the counters it does have.
 *
 * @param minor The protocol minor the IOmcu reports.
 * @return LINK_ST_COUNT or LINK_ST_COUNT_V420.
 */
uint8_t link_bringup_status_count(uint16_t minor);

/**
 * @brief Take a STATUS reply read from register 0 into @p b and the fault
 * bitmap into @p faults (either may be NULL).
 *
 * False, and nothing taken, for anything but a DATA reply from offset 0
 * with at least LINK_ST_COUNT_V420 registers.  A reply that ends before
 * LINK_ST_REFUSED_HI clears have_refused and dev_refused: an older
 * IOmcu counts no refusals, and a count is not carried over from an
 * IOmcu that answered before it.
 *
 * @param b The counters to fill; may be NULL.
 * @param st The STATUS reply.
 * @param faults Receives the fault bitmap; may be NULL.
 * @return true when the reply was taken, else false.
 */
bool link_bringup_take_status(link_bringup_t *b, const link_msg_t *st,
                              uint16_t *faults);

/* ------------------------------------------------------- round-trip stats */

/**
 * @brief Fold one round-trip measurement in.
 *
 * @param b The counters to update; NULL is ignored.
 * @param us The round trip, in microseconds.
 */
void link_bringup_add_rtt(link_bringup_t *b, uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_BRINGUP_H */
