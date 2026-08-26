/*
 * A half-duplex wire between the two ends of the link, with the faults a real
 * one has.
 *
 * The point is not to simulate RS485.  It is that the panel's poll loop and
 * the coprocessor's answer loop are the two halves of one conversation, and
 * every interesting failure is a property of the conversation rather than of
 * either half: a reply that never comes, a reply that comes corrupted, a reply
 * that comes late enough that the host has stopped waiting for it.  None of
 * those can be tested against a mock of the other end, because a mock agrees
 * with whatever the test expects.
 */
#ifndef RCBENCH_FAKE_WIRE_H
#define RCBENCH_FAKE_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FAKE_WIRE_CAP 512

typedef struct {
    uint8_t buf[FAKE_WIRE_CAP];
    size_t  len;
    size_t  read;

    /* Faults, applied on write.  -1 disables. */
    int  corrupt_byte;   /**< flip a bit in the n'th byte written          */
    int  truncate_after; /**< drop everything past the n'th byte written   */
    bool deaf;           /**< swallow everything: the far end is not there */

    unsigned long bytes_written;
} fake_wire_t;

void   fake_wire_reset(fake_wire_t *w);
void   fake_wire_write(fake_wire_t *w, const uint8_t *data, size_t n);
/** Read up to @p max bytes; returns how many. */
size_t fake_wire_read(fake_wire_t *w, uint8_t *out, size_t max);
/** Bytes waiting to be read. */
size_t fake_wire_pending(const fake_wire_t *w);

#endif /* RCBENCH_FAKE_WIRE_H */
