/**
 * @file
 * @brief Fetching a board's drawing: the order of the transactions, without the
 * link underneath them.
 *
 * The panel asks the IOmcu what its drawing is, then asks for it a
 * block at a time. The sequence is small and the failures are all quiet
 * ones: a block skipped, a reply for a block nobody asked for, a drawing
 * that arrives whole and wrong. Those are worth a test, and the task it
 * runs in cannot be one.
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

#include "art_store.h"
#include "link_artxfer.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief How the sequence reaches the far end. */
typedef struct {
    /**
     * Read @p count registers of @p page. False when nothing answered, or
     * the reply was a refusal rather than data -- the two are the same to a
     * sequence that cannot go on either way.
     */
    bool (*read_page)(void *ctx, uint8_t page, uint8_t count, uint16_t *regs);
    /**
     * @brief Write one register.  False when it was refused or went unanswered.
     */
    bool (*write_reg)(void *ctx, uint8_t page, uint8_t reg, uint16_t value);
    void *ctx;
} art_transport_t;

/** @brief States of a fetch. */
typedef enum {
    ART_FETCH_IDLE = 0,  /**< nothing under way                            */
    ART_FETCH_RUNNING,   /**< more blocks to ask for                       */
    ART_FETCH_DONE,      /**< whole, and it is the drawing it claimed      */
    ART_FETCH_FAILED,    /**< art_fetch_why() says what stopped it         */
} art_fetch_state_t;

/** @brief State of one fetch. */
typedef struct {
    link_artxfer_t    x;
    art_fetch_state_t state;
    const char       *why;
} art_fetch_t;

/**
 * @brief What the far end says its drawing is: LINK_AW_COUNT registers into
 * @p meta_regs.
 *
 * False when it has none, when the page is not there at all -- an IOmcu
 * built before it -- or when what it says cannot be a drawing. Those are
 * different things to somebody reading a log, so @p why is set to which; it
 * may be NULL, and it is never left pointing at nothing.
 *
 * Everything that can be checked without knowing where the drawing would go
 * is checked here, and @p bytes is left zero when any of it fails: the
 * caller finds room from that number, and a page that cannot be a drawing
 * should not size an allocation first and be refused second.
 *
 * @param t Transport to the far end.
 * @param meta_regs Receives LINK_AW_COUNT registers.
 * @param bytes Receives the drawing size in bytes; zero on failure.
 * @param why Receives the reason on failure; may be NULL.
 * @return true when the far end describes a drawing.
 */
bool art_fetch_meta(const art_transport_t *t, uint16_t *meta_regs,
                    uint32_t *bytes, const char **why);

/**
 * @brief The cache entry @p meta_regs describe for @p board: the content key a
 * kept drawing has to match field for field (art_store_read_same()) before
 * it is shown.  False, with @p out zeroed, when the registers cannot
 * describe a drawing.
 *
 * @param meta_regs Registers from art_fetch_meta().
 * @param board Board the drawing is for.
 * @param out Receives the cache entry.
 * @return true when the registers describe a drawing.
 */
bool art_fetch_key(const uint16_t *meta_regs, uint16_t board,
                   art_entry_t *out);

/**
 *
 * @param f Fetch state to start.
 * @param meta_regs Registers from art_fetch_meta().
 * @param buf Buffer to assemble the drawing into.
 * @param cap Size of @p buf in bytes.
 * @return true when the metadata describes a drawing that fits in @p buf.
 */
bool art_fetch_begin(art_fetch_t *f, const uint16_t *meta_regs, uint8_t *buf,
                     uint32_t cap);

/**
 * @brief Ask for up to @p blocks of it.
 *
 * Returns what to do next. DONE means the whole payload arrived and matched
 * the CRC and the hash the far end declared, so the buffer is the drawing;
 * FAILED means it did not, and nothing should be kept.
 *
 * @param f Fetch state.
 * @param t Transport to the far end.
 * @param blocks Most blocks to ask for in this step.
 * @return The state after this step.
 */
art_fetch_state_t art_fetch_step(art_fetch_t *f, const art_transport_t *t,
                                 unsigned blocks);

/**
 * @brief The format code of the drawing.
 *
 * @param f  Fetch state.
 * @return  The value from the fetched metadata; 0 when @p f is NULL.
 */
uint16_t art_fetch_format(const art_fetch_t *f);
/**
 * @brief The width of the drawing.
 *
 * @param f  Fetch state.
 * @return  The value from the fetched metadata; 0 when @p f is NULL.
 */
uint16_t art_fetch_width(const art_fetch_t *f);
/**
 * @brief The height of the drawing.
 *
 * @param f  Fetch state.
 * @return  The value from the fetched metadata; 0 when @p f is NULL.
 */
uint16_t art_fetch_height(const art_fetch_t *f);
/**
 * @brief The CRC of the drawing.
 *
 * @param f  Fetch state.
 * @return  The value from the fetched metadata; 0 when @p f is NULL.
 */
uint16_t art_fetch_crc(const art_fetch_t *f);
/**
 * @brief The hash of the drawing.
 *
 * @param f  Fetch state.
 * @return  The value from the fetched metadata; 0 when @p f is NULL.
 */
uint32_t art_fetch_hash(const art_fetch_t *f);
/**
 * @brief The size of the drawing in bytes.
 *
 * @param f  Fetch state.
 * @return  The value from the fetched metadata; 0 when @p f is NULL.
 */
uint32_t art_fetch_bytes(const art_fetch_t *f);
/**
 * @brief Why it stopped, for a log line.
 *
 * Never NULL.
 *
 * @param f  Fetch state.
 * @return  The reason text.
 */
const char *art_fetch_why(const art_fetch_t *f);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ART_FETCH_H */
