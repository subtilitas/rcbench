/**
 * @file
 * @brief The capture's ring: two arrays of one index, the states the PIO
 *        (programmable input/output) capture pushed and the microsecond
 *        stamps a second DMA (direct memory access) channel wrote after
 *        each, read into the decoder.
 *
 * The DMA writes event i's state to states[i] and then its stamp to
 * stamps[i], endlessly, both write addresses wrapping on the ring.  The
 * caller passes the index the stamp channel writes next: every event before
 * it is whole.
 *
 * A lap.  The ring holds PASS_RING_EVENTS events; a reader that falls that
 * far behind (a flash window stalls it for up to 42ms) finds unread events
 * overwritten.  pass_ring_drain() keeps the last event it read; the DMA
 * reaches that slot before any unread one, so the slot holding something
 * else shows the lap.  It checks before and after it reads, so a lap during
 * the read shows as well.  A ring that has just filled is taken as lapped.
 * On a lap the reader drops what is in the ring, starts after the newest
 * event, and restarts the decoder (pass_dec_restart()): the figures of that
 * stretch are not known, a measurement gap and not an error.
 *
 * Before the first event is read the slot expected is the ring's last,
 * holding 0 and 0: the capture clears the ring before it starts.  An event
 * of state 0 at stamp 0 there is taken as a lap.
 *
 * Pure C, no SDK (software development kit).  Host-tested in
 * test_pass_decode.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_PASS_RING_H
#define RCBENCH_PASS_RING_H

#include <stdbool.h>
#include <stdint.h>

#include "pass_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Events the ring holds: 8 bytes each, 64KiB in all. */
#define PASS_RING_EVENTS 8192u

/** @brief The reader. */
typedef struct {
    uint32_t rd;          /**< the next event to read */
    uint32_t last_state;  /**< the event before rd as read */
    uint32_t last_stamp;  /**< and its stamp */
} pass_ring_t;

/** @brief What one drain found. */
typedef struct {
    uint32_t events;  /**< events decoded */
    bool     lapped;  /**< the DMA overwrote unread events */
} pass_drain_t;

/**
 * @brief A reader at the start of a cleared ring.
 *
 * @param r  The reader.
 */
void pass_ring_init(pass_ring_t *r);

/**
 * @brief The events written since the last call, into the decoder, then
 *        its lost signals as of @p now_us.
 *
 * @param r       The reader.
 * @param states  The states, PASS_RING_EVENTS.
 * @param stamps  The stamps, PASS_RING_EVENTS.
 * @param head    The index the stamp channel writes next; read before
 *                @p now_us.
 * @param now_us  The microsecond timer, read after @p head.
 * @param d       The decoder.
 * @return        Events decoded and whether the ring lapped.
 */
pass_drain_t pass_ring_drain(pass_ring_t *r,
                             const volatile uint32_t *states,
                             const volatile uint32_t *stamps,
                             uint32_t head, uint32_t now_us, pass_dec_t *d);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PASS_RING_H */
