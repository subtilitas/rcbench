/*
 * SPDX-License-Identifier: MIT
 */

#include "link_bringup.h"

#include <stddef.h>

#include "link_pages.h"

/*
 * The shortfall that counts as a fault: fewer replies than 9/10 of the polls.
 * Below that shortfall a link is working and occasionally missing, which is
 * INTERMITTENT.
 */
#define SHORTFALL_NUM 9
#define SHORTFALL_DEN 10

/* Fewer replies than this and there is nothing to draw conclusions from. */
#define ENOUGH_POLLS 4u

static bool well_short(uint32_t got, uint32_t of)
{
    /* got < of * 9/10, without overflowing on the multiply. */
    return (uint64_t)got * SHORTFALL_DEN < (uint64_t)of * SHORTFALL_NUM;
}

void link_bringup_add_rtt(link_bringup_t *b, uint32_t us)
{
    if (b == NULL) {
        return;
    }
    if (b->rt_samples == 0) {
        b->rt_min_us  = us;
        b->rt_max_us  = us;
        b->rt_mean_us = us;
        b->rt_samples = 1;
        return;
    }
    if (us < b->rt_min_us) {
        b->rt_min_us = us;
    }
    if (us > b->rt_max_us) {
        b->rt_max_us = us;
    }
    /*
     * A running mean rather than a sum: a microsecond sum overflows 32 bits
     * after about 1 hour of polling.  Integer, biased low by at most 1 us per
     * sample.
     */
    ++b->rt_samples;
    b->rt_mean_us += ((int32_t)us - (int32_t)b->rt_mean_us)
                     / (int32_t)b->rt_samples;
}

link_diag_t link_bringup_diagnose(const link_bringup_t *b)
{
    if (b == NULL || b->polls < ENOUGH_POLLS) {
        return LINK_DIAG_OK;   /* nothing has been asked yet */
    }

    /*
     * Silence first.  Everything else in this list assumes something came
     * back, and a silent link also shows a timeout per poll; reporting those
     * is reporting a consequence of the fault rather than the fault.
     */
    if (b->replies == 0) {
        return LINK_DIAG_SILENT;
    }

    /* Then whether the thing answering is the thing expected.  A protocol
     * skew can produce every other symptom on this list as a side effect. */
    if (b->have_identity && b->proto_major != LINK_PROTOCOL_MAJOR) {
        return LINK_DIAG_PROTOCOL_MISMATCH;
    }

    /*
     * The comparison only both ends together can make: the coprocessor
     * decoded the requests and the panel did not hear the answers.  That is
     * the return path (the coprocessor's transmit path or the panel's
     * receiver), which the panel's own numbers cannot show.
     */
    if (b->have_status && !well_short(b->dev_frames, b->polls)
        && well_short(b->replies, b->polls)) {
        return LINK_DIAG_REPLIES_LOST;
    }

    /* Corruption at either end. Counted before staleness because a corrupt
     * reply is also a late one once it has been thrown away. */
    if (b->rx_crc_errors > 0 || (b->have_status && b->dev_crc_errors > 0)) {
        return LINK_DIAG_CORRUPT;
    }

    /* Answers that arrived after the panel had moved on. */
    if (b->mismatches > 0) {
        return LINK_DIAG_STALE;
    }

    if (well_short(b->replies, b->polls)) {
        return LINK_DIAG_INTERMITTENT;
    }
    return LINK_DIAG_OK;
}

const char *link_diag_text(link_diag_t d)
{
    switch (d) {
    case LINK_DIAG_SILENT:            return "no reply to any poll";
    case LINK_DIAG_PROTOCOL_MISMATCH: return "answering, wrong protocol";
    case LINK_DIAG_REPLIES_LOST:      return "requests land, answers do not";
    case LINK_DIAG_CORRUPT:           return "frames arrive corrupt";
    case LINK_DIAG_STALE:             return "answers arrive too late";
    case LINK_DIAG_INTERMITTENT:      return "works, and not every time";
    case LINK_DIAG_OK:
    default:                          return "link healthy";
    }
}

const char *link_diag_hint(link_diag_t d)
{
    switch (d) {
    case LINK_DIAG_SILENT:
        /* In the order that costs least to check. */
        return "iomcu powered? CANH/CANL swapped? same bit rate? terminators?";
    case LINK_DIAG_PROTOCOL_MISMATCH:
        return "flash both ends from the same tree";
    case LINK_DIAG_REPLIES_LOST:
        return "the far end hears us and we do not hear it: its transmit path";
    case LINK_DIAG_CORRUPT:
        return "bit timing or sample point disagreeing between the two ends";
    case LINK_DIAG_STALE:
        return "a reply slower than the poll timeout, or a stalled far end";
    case LINK_DIAG_INTERMITTENT:
        return "marginal timing, or a poll period tighter than the round trip";
    case LINK_DIAG_OK:
    default:
        return "";
    }
}
