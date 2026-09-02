/*
 * SPDX-License-Identifier: MIT
 */

#include "can_selftest.h"

#include <string.h>

/*
 * The payload patterns, chosen for their effect on bit stuffing.  CAN
 * (Controller Area Network) inserts a complementary bit after five bits of
 * the same polarity, so a marginal bus fails on long runs of one level first.
 * Counting integers alone do not exercise that.
 */
static const uint8_t k_patterns[] = { 0x00u, 0xFFu, 0x55u, 0xAAu };
#define PATTERN_COUNT ((uint8_t)(sizeof(k_patterns) / sizeof(k_patterns[0])))

/*
 * The status exchange shares the echo test's page; the identifier's offset
 * field tells them apart: 0 is an echo, 1 is a status.  One page keeps both
 * at the same lowest priority and under one filter.
 */
#define OFF_ECHO   0u
#define OFF_STATUS 1u

/* A probe is a READ from the initiator; the echo comes back as DATA, so a
 * node that hears its own traffic cannot mistake one for the other. */
static uint32_t probe_id(void)
{
    return link_can_id(LINK_CAN_PRIO_BULK, LINK_OP_READ, CAN_SELFTEST_PAGE,
                       OFF_ECHO, 4);
}

static uint32_t echo_id(void)
{
    return link_can_id(LINK_CAN_PRIO_BULK, LINK_OP_DATA, CAN_SELFTEST_PAGE,
                       OFF_ECHO, 4);
}

static uint32_t status_req_id(void)
{
    return link_can_id(LINK_CAN_PRIO_BULK, LINK_OP_READ, CAN_SELFTEST_PAGE,
                       OFF_STATUS, 4);
}

static uint32_t status_rsp_id(void)
{
    return link_can_id(LINK_CAN_PRIO_BULK, LINK_OP_DATA, CAN_SELFTEST_PAGE,
                       OFF_STATUS, 4);
}

bool can_selftest_status_request(link_can_frame_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->id  = status_req_id();
    out->dlc = 0;
    return true;
}

bool can_selftest_status_reply(const link_can_frame_t *in,
                               const can_remote_status_t *st,
                               link_can_frame_t *out)
{
    if (in == NULL || st == NULL || out == NULL
        || in->id != status_req_id() || in->dlc != 0) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->id  = status_rsp_id();
    out->dlc = 8;
    out->data[0] = st->up ? 1u : 0u;
    out->data[1] = st->tx_errors;
    out->data[2] = st->rx_errors;
    out->data[3] = st->flags;
    out->data[4] = (uint8_t)(st->echoes & 0xFFu);
    out->data[5] = (uint8_t)(st->echoes >> 8);
    out->data[6] = (uint8_t)(st->overflows & 0xFFu);
    out->data[7] = (uint8_t)(st->overflows >> 8);
    return true;
}

bool can_selftest_status_parse(const link_can_frame_t *in,
                               can_remote_status_t *out)
{
    if (in == NULL || out == NULL || in->id != status_rsp_id()
        || in->dlc != 8) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->up        = (in->data[0] != 0u);
    out->tx_errors = in->data[1];
    out->rx_errors = in->data[2];
    out->flags     = in->data[3];
    out->echoes    = (uint16_t)((uint16_t)in->data[4]
                                | ((uint16_t)in->data[5] << 8));
    out->overflows = (uint16_t)((uint16_t)in->data[6]
                                | ((uint16_t)in->data[7] << 8));
    return true;
}

/*
 * One probe may be in flight across either edge of the window, so a
 * difference of one or two is the measurement's own resolution rather than a
 * fault.  Anything more is frames that were answered and never arrived.
 */
#define RETURN_LOSS_SLACK 2u

uint16_t can_selftest_return_loss(uint16_t before, uint16_t after,
                                  uint32_t heard)
{
    /* Unsigned, so a counter that wrapped past 65535 still subtracts. */
    const uint16_t answered = (uint16_t)(after - before);
    if ((uint32_t)answered <= heard + RETURN_LOSS_SLACK) {
        return 0;
    }
    return (uint16_t)((uint32_t)answered - heard);
}

/* Sequence in the first two bytes, the pattern in the remaining six.  The
 * pattern is derived from the sequence, so a frame that comes back with a
 * payload from a different probe is detectable as corrupt rather than being
 * quietly accepted. */
static void fill(uint8_t *data, uint16_t seq)
{
    data[0] = (uint8_t)(seq & 0xFFu);
    data[1] = (uint8_t)(seq >> 8);
    const uint8_t p = k_patterns[seq % PATTERN_COUNT];
    for (size_t i = 2; i < 8; ++i) {
        data[i] = p;
    }
}

void can_selftest_init(can_selftest_t *st, uint32_t timeout_ms)
{
    if (st == NULL) {
        return;
    }
    memset(st, 0, sizeof(*st));
    st->timeout_ms = (timeout_ms == 0) ? 50u : timeout_ms;
}

bool can_selftest_probe(can_selftest_t *st, uint32_t now_ms,
                        link_can_frame_t *out)
{
    if (st == NULL || out == NULL || st->waiting) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->id  = probe_id();
    out->dlc = 8;
    fill(out->data, st->seq);

    st->outstanding = st->seq;
    st->waiting     = true;
    st->sent_ms     = now_ms;
    ++st->sent;
    ++st->seq;
    return true;
}

bool can_selftest_rx(can_selftest_t *st, const link_can_frame_t *f,
                     uint32_t rtt_us)
{
    if (st == NULL || f == NULL || f->id != echo_id()) {
        return false;   /* not this test's traffic */
    }

    if (f->dlc != 8) {
        ++st->corrupt;
        st->waiting = false;
        return true;
    }

    const uint16_t seq = (uint16_t)((uint16_t)f->data[0]
                                    | ((uint16_t)f->data[1] << 8));
    if (!st->waiting || seq != st->outstanding) {
        /* An echo of a probe that has already timed out.  Counted apart
         * from corruption: a late bus and a corrupting bus need different
         * fixes. */
        ++st->stale;
        return true;
    }

    uint8_t want[8];
    fill(want, st->outstanding);
    if (memcmp(f->data, want, 8) != 0) {
        ++st->corrupt;
    } else {
        ++st->echoed;
        if (st->echoed == 1u || rtt_us < st->rtt_min_us) {
            st->rtt_min_us = rtt_us;
        }
        if (rtt_us > st->rtt_max_us) {
            st->rtt_max_us = rtt_us;
        }
    }
    st->waiting = false;
    return true;
}

bool can_selftest_tick(can_selftest_t *st, uint32_t now_ms)
{
    if (st == NULL || !st->waiting) {
        return false;
    }
    if ((uint32_t)(now_ms - st->sent_ms) < st->timeout_ms) {
        return false;
    }
    st->waiting = false;
    ++st->timed_out;
    return true;
}

can_selftest_verdict_t can_selftest_verdict(const can_selftest_t *st)
{
    if (st == NULL || st->sent < CAN_SELFTEST_MIN_PROBES) {
        return CAN_SELFTEST_RUNNING;
    }
    /*
     * The checks run from the most fundamental fault outwards, and the first
     * that fires is reported: a silent bus also times out on every probe, and
     * a corrupt one also loses some.
     *
     * Silent means nothing came back at all, late echoes included.  A bus
     * whose every echo arrives past the timeout has echoed == 0 and
     * corrupt == 0 but a non-zero stale count, and the stale count is the
     * evidence that frames cross.
     */
    if (st->echoed == 0u && st->corrupt == 0u && st->stale == 0u) {
        return CAN_SELFTEST_SILENT;
    }
    if (st->corrupt > 0u) {
        return CAN_SELFTEST_CORRUPT;
    }
    if (st->timed_out > 0u || st->stale > 0u) {
        /*
         * Loss with no bus error is a different fault from loss with one.  A
         * frame corrupted on the wire is counted by the controller that saw
         * it go wrong; a frame dropped because nobody read it in time arrives
         * intact and is counted by nothing.
         */
        return (st->bus_errors == 0u) ? CAN_SELFTEST_DROPPED
                                      : CAN_SELFTEST_LOSSY;
    }
    return CAN_SELFTEST_OK;
}

const char *can_selftest_text(can_selftest_verdict_t v)
{
    switch (v) {
    case CAN_SELFTEST_OK:      return "every probe came back intact";
    case CAN_SELFTEST_SILENT:  return "no probe came back";
    case CAN_SELFTEST_CORRUPT: return "probes come back altered";
    case CAN_SELFTEST_LOSSY:   return "probes cross, and not all of them";
    case CAN_SELFTEST_DROPPED: return "probes go missing without a bus error";
    case CAN_SELFTEST_RUNNING:
    default:                   return "running";
    }
}

const char *can_selftest_hint(can_selftest_verdict_t v)
{
    switch (v) {
    case CAN_SELFTEST_SILENT:
        /* In the order that costs least to check. */
        return "CANH/CANL swapped? far end powered? both at the same bit rate?"
               " terminators fitted at both ends?";
    case CAN_SELFTEST_CORRUPT:
        return "sample point or bit timing; a missing terminator reflects";
    case CAN_SELFTEST_LOSSY:
        return "marginal timing, one terminator, or a bus longer than the rate";
    case CAN_SELFTEST_DROPPED:
        /* Says nothing about the wire: the absence of bus errors is
         * evidence the wire is fine. */
        return "a receive buffer overran -- something stopped reading in time,"
               " not a wiring fault";
    case CAN_SELFTEST_OK:
    case CAN_SELFTEST_RUNNING:
    default:
        return "";
    }
}

bool can_selftest_echo(const link_can_frame_t *in, link_can_frame_t *out)
{
    if (in == NULL || out == NULL || in->id != probe_id() || in->dlc != 8) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->id  = echo_id();
    out->dlc = in->dlc;
    memcpy(out->data, in->data, in->dlc);
    return true;
}
