/*
 * Fetching a board's photograph: the order of the transactions, without the
 * link underneath them.
 *
 * The panel asks the coprocessor what its picture is, then asks for it a
 * block at a time -- thousands of round trips for two hundred kilobytes. The
 * sequence is small and the failures are all quiet ones: a block skipped, a
 * reply for a block nobody asked for, a picture that arrives whole and wrong.
 * Those are worth a test, and the task it runs in cannot be one.
 *
 * So the transport is passed in. On the panel it is the CAN (Controller Area
 * Network) link; in the suite it is a device answering out of memory, which
 * is how the order can be interrupted on purpose and the result inspected.
 *
 * Nothing here loops without a bound: a step does the number of blocks it is
 * given and returns. The caller owns how much of its own time to spend,
 * because on the panel that caller also beats the safety line.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_ART_FETCH_H
#define RCBENCH_ART_FETCH_H

#include <stdbool.h>
#include <stdint.h>

#include "link_artxfer.h"

#ifdef __cplusplus
extern "C" {
#endif

/** How the sequence reaches the far end. */
typedef struct {
    /**
     * Read @p count registers of @p page. False when nothing answered, or
     * the reply was a refusal rather than data -- the two are the same to a
     * sequence that cannot go on either way.
     */
    bool (*read_page)(void *ctx, uint8_t page, uint8_t count, uint16_t *regs);
    /** Write one register. False when it was refused or went unanswered. */
    bool (*write_reg)(void *ctx, uint8_t page, uint8_t reg, uint16_t value);
    void *ctx;
} art_transport_t;

typedef enum {
    ART_FETCH_IDLE = 0,  /**< nothing under way                            */
    ART_FETCH_RUNNING,   /**< more blocks to ask for                       */
    ART_FETCH_DONE,      /**< whole, and it is the picture it claimed      */
    ART_FETCH_FAILED,    /**< art_fetch_why() says what stopped it         */
} art_fetch_state_t;

typedef struct {
    link_artxfer_t    x;
    art_fetch_state_t state;
    const char       *why;
} art_fetch_t;

/**
 * What the far end says its picture is.
 *
 * False when it has none, when the page is not there at all -- a coprocessor
 * built before it -- or when what it says cannot be a picture. The caller
 * needs @p bytes before it can find room, which is why this is separate from
 * the start.
 */
bool art_fetch_meta(const art_transport_t *t, uint16_t *meta_regs,
                    uint32_t *bytes);

/** Get ready to assemble into @p buf. False when the metadata cannot
 *  describe a picture that fits. */
bool art_fetch_begin(art_fetch_t *f, const uint16_t *meta_regs, uint8_t *buf,
                     uint32_t cap);

/**
 * Ask for up to @p blocks of it.
 *
 * Returns what to do next. DONE means the whole payload arrived and matched
 * the checksum the far end declared, so the buffer is the picture; FAILED
 * means it did not, and nothing should be kept.
 */
art_fetch_state_t art_fetch_step(art_fetch_t *f, const art_transport_t *t,
                                 unsigned blocks);

/** What the picture is, once it is whole. */
uint16_t art_fetch_width(const art_fetch_t *f);
uint16_t art_fetch_height(const art_fetch_t *f);
uint16_t art_fetch_crc(const art_fetch_t *f);
uint32_t art_fetch_bytes(const art_fetch_t *f);

/** Why it stopped, for a log line. Never NULL. */
const char *art_fetch_why(const art_fetch_t *f);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ART_FETCH_H */
