/*
 * Both ends' link counters turned into one diagnosis.
 *
 * Wrong pins, a mismatched bit rate, a missing terminator and a firmware skew
 * all present as a link that does not work.  The counters read from both
 * ends and compared tell them apart: a coprocessor that decoded 100 requests
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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* The panel's view: what it asked, and what came back. */
    uint32_t polls;
    uint32_t replies;
    uint32_t timeouts;
    uint32_t mismatches;    /**< answers to questions nobody is still asking */
    uint32_t nacks;
    uint32_t rx_crc_errors; /**< frames this end received corrupt; 0 on CAN */
    uint32_t rx_resyncs;

    /* The coprocessor's view of itself, read from the STATUS page.  Absent
     * until a status read succeeds, which is itself informative. */
    bool     have_status;
    uint32_t dev_frames;    /**< requests it decoded */
    uint32_t dev_crc_errors;
    uint32_t dev_resyncs;

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

typedef enum {
    LINK_DIAG_OK = 0,
    /** Nothing came back at all. */
    LINK_DIAG_SILENT,
    /** It answers, but it is not speaking this protocol. */
    LINK_DIAG_PROTOCOL_MISMATCH,
    /** Requests arrive and answers do not: the return path, not the link. */
    LINK_DIAG_REPLIES_LOST,
    /** Frames arrive corrupt. Bit timing, noise, or termination. */
    LINK_DIAG_CORRUPT,
    /** Answers arrive too late to be wanted. A slow far end, or a
     *  timeout tighter than the round trip. */
    LINK_DIAG_STALE,
    /** It works, and not every time. */
    LINK_DIAG_INTERMITTENT,
} link_diag_t;

/**
 * Name the most fundamental thing that is wrong.
 *
 * Most fundamental, not most numerous: a silent link also has a hundred
 * timeouts, and reporting the timeouts is reporting a consequence. The order
 * the checks run in *is* the diagnosis.
 */
link_diag_t link_bringup_diagnose(const link_bringup_t *b);

/** One line naming the fault. */
const char *link_diag_text(link_diag_t d);

/** One line naming what to check, in the order worth checking it. */
const char *link_diag_hint(link_diag_t d);

/* ------------------------------------------------------- round-trip stats */

/** Fold one round-trip measurement in. */
void link_bringup_add_rtt(link_bringup_t *b, uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_BRINGUP_H */
