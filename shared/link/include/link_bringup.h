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
#include <stddef.h>
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
    /** @brief The IOmcu's CAN receive error counter (REC, 0 to 255) at the
     *         status read: it rises with bad frames and falls with good
     *         ones, a level rather than a count. */
    uint32_t dev_crc_errors;
    /** @brief The IOmcu's CAN transmit error counter (TEC, 0 to 255), a
     *         level as dev_crc_errors. */
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
 * Flags, identity, the round-trip figures and the IOmcu's two CAN error
 * counters are taken from @p now: have_status, have_refused,
 * have_identity, proto_major, proto_minor, rt_samples, rt_min_us,
 * rt_max_us, rt_mean_us, dev_crc_errors and dev_resyncs.  The error
 * counters are levels that good frames lower, so a difference of two
 * readings can be 0 while errors keep coming.
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

/* ------------------------------------------------------ console report */

/**
 * @brief Bytes of one line of the console report, its NUL included: the
 *        longest line, the window's with the IOmcu's counts, is 167
 *        characters with every counter at 4,294,967,295.
 */
#define LINK_REPORT_LINE 176

/** @brief The most lines one console report prints. */
#define LINK_REPORT_LINES 7

/**
 * @brief The CAN controller's own counters, for the report's bus line.
 */
typedef struct {
    bool     have;        /**< the controller runs and was read          */
    bool     off;         /**< bus off: this end has stopped transmitting */
    uint32_t tx_errors;   /**< transmit error counter                    */
    uint32_t rx_errors;   /**< receive error counter                     */
    uint32_t bus_errors;  /**< bus errors since the driver started       */
} link_report_bus_t;

/**
 * @brief What one console report prints: the window since the previous
 *        report, the totals since start-up and the controller.
 *
 * Numbers rather than text, so the task that keeps the counters hands a
 * report on in under 300 bytes and the task that prints it formats it.
 */
typedef struct {
    link_bringup_t    win;    /**< the window (link_bringup_window())    */
    link_bringup_t    total;  /**< the counters since start-up           */
    uint32_t          win_s;  /**< the window's length, s                */
    link_report_bus_t bus;    /**< the controller's counters             */
} link_report_in_t;

/**
 * @brief Line @p i of the console report of @p r, and whether it is a
 *        warning.
 *
 * The lines, in order: the window's diagnosis (link_bringup_diagnose()
 * of r->win), what to check when it is not LINK_DIAG_OK (a warning), the
 * window's counts, the panel's totals, the IOmcu's totals, the round trip
 * when r->total has samples, and the bus.  At most LINK_REPORT_LINES; no
 * line is longer than LINK_REPORT_LINE - 1 characters.
 *
 * @param r     The report.
 * @param i     The line, from 0.
 * @param out   Receives the line, cut to @p size - 1 characters; "" past
 *              the last line.  May be NULL with @p size 0.
 * @param size  Bytes of @p out.
 * @param warn  Receives true for a warning line; may be NULL.
 * @return      The line's length as snprintf() counts it, which is @p size
 *              or more for a line cut; 0 past the last line and for a NULL
 *              @p r.
 */
size_t link_report_line(const link_report_in_t *r, unsigned i, char *out,
                        size_t size, bool *warn);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_BRINGUP_H */
