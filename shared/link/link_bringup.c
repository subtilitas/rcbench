/**
 * @file
 * @brief Bring-up of the link: status replies, round-trip times and the
 *        diagnosis.
 *
 * SPDX-License-Identifier: MIT
 */

#include "link_bringup.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "link_pages.h"

/**
 * @brief The shortfall that counts as a fault: fewer replies than 9/10 of the
 * polls.
 *
 * Below that shortfall a link is working and occasionally missing, which is
 * INTERMITTENT.
 */
#define SHORTFALL_NUM 9
#define SHORTFALL_DEN 10

/**
 * @brief Fewer replies than this and there is nothing to draw conclusions from.
 */
#define ENOUGH_POLLS 4u

/**
 * @brief Test whether fewer replies came back than the shortfall ratio allows.
 *
 * @param got The replies received.
 * @param of The polls sent.
 * @return true when got is below 9/10 of of.
 */
static bool well_short(uint32_t got, uint32_t of)
{
    /* got < of * 9/10, without overflowing on the multiply. */
    return (uint64_t)got * SHORTFALL_DEN < (uint64_t)of * SHORTFALL_NUM;
}

uint8_t link_bringup_status_count(uint16_t minor)
{
    return (minor >= LINK_MINOR_REFUSED) ? (uint8_t)LINK_ST_COUNT
                                         : (uint8_t)LINK_ST_COUNT_V420;
}

bool link_bringup_take_status(link_bringup_t *b, const link_msg_t *st,
                              uint16_t *faults)
{
    if (st == NULL || st->op != LINK_OP_DATA || st->offset != 0u
        || st->count < LINK_ST_COUNT_V420 || st->count > LINK_MAX_REGS) {
        return false;
    }
    if (b != NULL) {
        b->dev_frames = (uint32_t)st->regs[LINK_ST_FRAMES_LO]
                        | ((uint32_t)st->regs[LINK_ST_FRAMES_HI] << 16);
        b->dev_crc_errors = st->regs[LINK_ST_CRC_ERRORS];
        b->dev_resyncs    = st->regs[LINK_ST_RESYNCS];
        b->have_status    = true;
        b->have_refused   = st->count > (uint8_t)LINK_ST_REFUSED_HI;
        b->dev_refused    = b->have_refused
            ? ((uint32_t)st->regs[LINK_ST_REFUSED_LO]
               | ((uint32_t)st->regs[LINK_ST_REFUSED_HI] << 16))
            : 0u;
    }
    if (faults != NULL) {
        *faults = st->regs[LINK_ST_FAULTS];
    }
    return true;
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
     * after about 1 hour of polling.  Integer, biased low by at most 1us per
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
     * The comparison only both ends together can make: the IOmcu
     * decoded the requests and the panel did not hear the answers.  That is
     * the return path (the IOmcu's transmit path or the panel's
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

/**
 * @brief One counter's count since the previous snapshot.
 *
 * @param now The counter at this snapshot.
 * @param prev The counter at the previous snapshot.
 * @return now - prev, or now when the counter went backwards (it counts
 *         from 0 again).
 */
static uint32_t since(uint32_t now, uint32_t prev)
{
    return (now >= prev) ? now - prev : now;
}

void link_bringup_window(const link_bringup_t *now,
                         const link_bringup_t *prev, link_bringup_t *out)
{
    if (now == NULL || out == NULL) {
        return;
    }
    link_bringup_t w = *now;   /* flags, identity, round trip */
    if (prev != NULL) {
        w.polls          = since(now->polls, prev->polls);
        w.replies        = since(now->replies, prev->replies);
        w.timeouts       = since(now->timeouts, prev->timeouts);
        w.mismatches     = since(now->mismatches, prev->mismatches);
        w.nacks          = since(now->nacks, prev->nacks);
        w.rx_crc_errors  = since(now->rx_crc_errors, prev->rx_crc_errors);
        w.rx_resyncs     = since(now->rx_resyncs, prev->rx_resyncs);
        w.dev_frames     = since(now->dev_frames, prev->dev_frames);
        /* dev_crc_errors and dev_resyncs stay as in now: the IOmcu fills
         * them with its CAN controller's receive and transmit error
         * counters, which fall with good frames and rise with bad ones. */
        w.dev_refused    = since(now->dev_refused, prev->dev_refused);
    }
    *out = w;
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

/* ------------------------------------------------------ console report */

/** @brief The console report's lines, in the order they print. */
typedef enum {
    LINE_DIAG = 0,   /**< the window's diagnosis                        */
    LINE_CHECK,      /**< what to check, for a fault                    */
    LINE_WINDOW,     /**< the window's counts                           */
    LINE_PANEL,      /**< the panel's totals                            */
    LINE_IOMCU,      /**< the IOmcu's totals                            */
    LINE_ROUND_TRIP, /**< the round trip, with samples                  */
    LINE_BUS,        /**< the CAN controller                            */
} report_line_t;

/**
 * @brief Write one line of the report.
 *
 * @param r     The report.
 * @param k     The line.
 * @param d     The window's diagnosis.
 * @param out   Receives it; may be NULL with @p size 0.
 * @param size  Bytes of @p out.
 * @return      snprintf()'s count.
 */
static int format_line(const link_report_in_t *r, report_line_t k,
                       link_diag_t d, char *out, size_t size)
{
    const link_bringup_t *w = &r->win;
    const link_bringup_t *t = &r->total;
    switch (k) {
    case LINE_DIAG:
        return snprintf(out, size, "LINK %s", link_diag_text(d));
    case LINE_CHECK:
        return snprintf(out, size, "  check: %s", link_diag_hint(d));
    case LINE_WINDOW:
        if (w->have_status) {
            return snprintf(out, size,
                            "  last %" PRIu32 "s  polls %" PRIu32
                            " replies %" PRIu32 " timeouts %" PRIu32
                            " stale %" PRIu32 " nack %" PRIu32
                            "  iomcu frames %" PRIu32 " crc %" PRIu32
                            " resync %" PRIu32 " refused %" PRIu32,
                            r->win_s, w->polls, w->replies, w->timeouts,
                            w->mismatches, w->nacks, w->dev_frames,
                            w->dev_crc_errors, w->dev_resyncs,
                            w->dev_refused);
        }
        return snprintf(out, size,
                        "  last %" PRIu32 "s  polls %" PRIu32
                        " replies %" PRIu32 " timeouts %" PRIu32
                        " stale %" PRIu32 " nack %" PRIu32,
                        r->win_s, w->polls, w->replies, w->timeouts,
                        w->mismatches, w->nacks);
    case LINE_PANEL:
        return snprintf(out, size,
                        "  panel  polls %" PRIu32 " replies %" PRIu32
                        " timeouts %" PRIu32 " stale %" PRIu32
                        " nack %" PRIu32 " crc %" PRIu32 " resync %" PRIu32,
                        t->polls, t->replies, t->timeouts, t->mismatches,
                        t->nacks, t->rx_crc_errors, t->rx_resyncs);
    case LINE_IOMCU:
        if (t->have_status) {
            return snprintf(out, size,
                            "  iomcu  frames %" PRIu32 " crc %" PRIu32
                            " resync %" PRIu32 " refused %" PRIu32,
                            t->dev_frames, t->dev_crc_errors,
                            t->dev_resyncs, t->dev_refused);
        }
        return snprintf(out, size, "  iomcu  never answered a status read");
    case LINE_ROUND_TRIP:
        /* As measured: CAN arbitrates rather than taking turns, so there
         * is no turnaround allowance to compare it against. */
        return snprintf(out, size,
                        "  round trip min %" PRIu32 " avg %" PRIu32
                        " max %" PRIu32 " us",
                        t->rt_min_us, t->rt_mean_us, t->rt_max_us);
    case LINE_BUS:
    default:
        if (r->bus.have) {
            return snprintf(out, size,
                            "  bus    tx errors %" PRIu32 " rx errors %" PRIu32
                            " bus errors %" PRIu32 "%s",
                            r->bus.tx_errors, r->bus.rx_errors,
                            r->bus.bus_errors, r->bus.off ? " -- BUS OFF" : "");
        }
        /* Zeros would read as a healthy bus: a controller that never
         * started is another diagnosis than one with no errors. */
        return snprintf(out, size, "  bus    the controller is not running");
    }
}

size_t link_report_line(const link_report_in_t *r, unsigned i, char *out,
                        size_t size, bool *warn)
{
    if (out != NULL && size > 0u) {
        out[0] = '\0';
    }
    if (warn != NULL) {
        *warn = false;
    }
    if (r == NULL) {
        return 0u;
    }
    const link_diag_t d = link_bringup_diagnose(&r->win);
    report_line_t k = LINE_DIAG;
    unsigned n = 0u;
    for (int j = (int)LINE_DIAG; j <= (int)LINE_BUS; ++j) {
        const report_line_t kj = (report_line_t)j;
        if ((kj == LINE_CHECK && d == LINK_DIAG_OK)
            || (kj == LINE_ROUND_TRIP && r->total.rt_samples == 0u)) {
            continue;
        }
        if (n++ == i) {
            k = kj;
            break;
        }
    }
    if (n <= i) {
        return 0u;   /* past the last line */
    }
    if (warn != NULL) {
        *warn = k == LINE_CHECK;
    }
    const int len = format_line(r, k, d, out, size);
    return (len > 0) ? (size_t)len : 0u;
}
