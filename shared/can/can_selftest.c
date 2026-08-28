#include "can_selftest.h"

#include <string.h>

/*
 * The patterns, chosen for what they do to bit stuffing rather than for
 * looking varied.  CAN inserts a complementary bit after five of the same
 * polarity, so a bus that is marginal fails on long runs first -- and a test
 * sending only counting integers would pass on a bus that drops real traffic.
 */
static const uint8_t k_patterns[] = { 0x00u, 0xFFu, 0x55u, 0xAAu };
#define PATTERN_COUNT ((uint8_t)(sizeof(k_patterns) / sizeof(k_patterns[0])))

/* A probe from the initiator; the echo comes back as DATA so the two are
 * never confused for each other on a bus that hears its own traffic. */
static uint32_t probe_id(void)
{
    return link_can_id(LINK_CAN_PRIO_BULK, LINK_OP_READ, CAN_SELFTEST_PAGE,
                       0, 4);
}

static uint32_t echo_id(void)
{
    return link_can_id(LINK_CAN_PRIO_BULK, LINK_OP_DATA, CAN_SELFTEST_PAGE,
                       0, 4);
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
        return false;   /* not ours; somebody else's traffic */
    }

    if (f->dlc != 8) {
        ++st->corrupt;
        st->waiting = false;
        return true;
    }

    const uint16_t seq = (uint16_t)((uint16_t)f->data[0]
                                    | ((uint16_t)f->data[1] << 8));
    if (!st->waiting || seq != st->outstanding) {
        /* An echo of a probe already given up on.  Counted apart from
         * corruption, because a late bus and a wrong one want different
         * things done about them. */
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
     * Order matters here as it does in the link's own diagnosis.  A silent bus
     * also times out on every probe, and a corrupt one also loses some -- so
     * the checks run from the most fundamental fault outwards, and the first
     * one that fires is the one worth reporting.
     */
    if (st->echoed == 0u && st->corrupt == 0u) {
        return CAN_SELFTEST_SILENT;
    }
    if (st->corrupt > 0u) {
        return CAN_SELFTEST_CORRUPT;
    }
    if (st->timed_out > 0u || st->stale > 0u) {
        /*
         * Loss with no bus error is not the same fault as loss with one, and
         * the two send you to opposite ends of the bench.  A frame corrupted
         * on the wire is counted by whichever controller saw it go wrong; a
         * frame dropped because nobody read it in time is counted by nothing
         * at all and arrives perfectly.
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
        /* Cheapest to check first, and in the order a bench actually gets
         * them wrong. */
        return "CANH/CANL swapped? far end powered? both at the same bit rate?"
               " terminators fitted at both ends?";
    case CAN_SELFTEST_CORRUPT:
        return "sample point or bit timing; a missing terminator reflects";
    case CAN_SELFTEST_LOSSY:
        return "marginal timing, one terminator, or a bus longer than the rate";
    case CAN_SELFTEST_DROPPED:
        /* Deliberately says nothing about the wire: the absence of bus errors
         * is evidence the wire is fine. */
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
