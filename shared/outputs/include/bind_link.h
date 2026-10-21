/**
 * @file
 * @brief The panel's half of a binding on the link: writing the two output
 *        pages and reading them back.
 *
 * Both run over a link_port_t, so the sequences the panel sends are the
 * ones the host suite runs against the IOmcu's page rules and a
 * modelled bus.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "link_host.h"
#include "link_pages.h"
#include "link_port.h"
#include "out_bind.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What became of a binding written. */
typedef enum {
    BIND_WRITTEN = 0,   /**< every exchange acknowledged                   */
    BIND_NO_LINK,       /**< an exchange ended unanswered                  */
    BIND_REFUSED,       /**< the far end answered and would not have it    */
} bind_write_t;

/** @brief The first minor that serves SERVO's frame rate. */
#define LINK_MINOR_SERVO_RATE 1u

/** @brief What became of the SERVO rate reset that leads a binding. */
typedef enum {
    BIND_RATE_NOT_SENT = 0, /**< the IOmcu has no SERVO page (4.0)   */
    BIND_RATE_LANDED,       /**< the rate reset was acknowledged      */
    BIND_RATE_REFUSED,      /**< the far end answered and refused it  */
    BIND_RATE_NO_ANSWER,    /**< the exchange ended unanswered        */
} bind_rate_t;

/**
 * @brief Write a CHAN_CFG page @p cfg and an OUTPUTS page @p slots, each frame
 *        its own acknowledged exchange (link_write_acked()).
 *
 * First, to an IOmcu of minor 1 or later, SERVO's FRAME_HZ = 0: each
 * slot back at its own rate.  A refusal ends the sequence BIND_REFUSED and
 * no answer ends it BIND_NO_LINK, with no page written.  @p rate, when not
 * NULL, takes what became of it.
 *
 * Then, to an IOmcu whose protocol minor @p minor is LINK_MINOR_BIND
 * (10) or later: 8 frames to BIND_CFG, 8 to BIND_OUT, and LINK_BD_COMMIT
 * with the CRC (cyclic redundancy check) of the two pages, 17 exchanges.
 * The pages in force change at the commit and not before, both or neither,
 * so a sequence that ends BIND_NO_LINK or BIND_REFUSED before the commit is
 * taken leaves the binding in force as it was.  A commit whose
 * acknowledgement alone is lost ends BIND_NO_LINK with the binding written,
 * whole.
 *
 * To an older IOmcu: 8 frames to CHAN_CFG and 8 to OUTPUTS, 16
 * exchanges, each in force as it is acknowledged.  A sequence that stops
 * part way leaves the frames before it applied.
 *
 * The exchanges stop at the first one refused or unanswered.
 *
 * @param h The link host to write through.
 * @param port The link port the IOmcu answers on.
 * @param minor The IOmcu's protocol minor version.
 * @param cfg The CHAN_CFG page, LINK_CC_COUNT registers.
 * @param slots The OUTPUTS page, LINK_OS_COUNT registers.
 * @param rate Receives what became of the SERVO rate reset, set to
 *        BIND_RATE_NOT_SENT first. NULL discards it.
 * @return BIND_WRITTEN when every exchange was acknowledged. BIND_NO_LINK when
 *         an exchange was unanswered, or when @p h, @p port, @p cfg or @p slots
 *         is NULL. BIND_REFUSED when the far end answered and refused one.
 */
bind_write_t bind_link_write(link_host_t *h, const link_port_t *port,
                             uint16_t minor, const uint16_t *cfg,
                             const uint16_t *slots, bind_rate_t *rate);

/** @brief What a read of the two pages gave. */
typedef enum {
    /** @brief Both pages read and the binding model describes them; a page with
     *        nothing bound is this.
     */
    BIND_READ_OK = 0,
    /** @brief A page did not read: unanswered or refused.
     *
     * Nothing is known.
     */
    BIND_READ_NONE,
    /** @brief Both pages read and no binding this model can hold renders to
     *        them: a pin in two slots, a rate or driver no protocol entry has,
     *        a pin off the board or reserved, channels out of slot order.
     */
    BIND_READ_ODD,
    /** @brief Both pages read and this build has no pin map for the board. */
    BIND_READ_NO_BOARD,
} bind_read_t;

/** @brief One reading of the two output pages, and the binding it gives. */
typedef struct {
    bind_read_t state;  /**< what the reading found */
    /** BIND_READ_OK: the binding.  Otherwise nothing bound on @p board. */
    outbind_t   bind;
    /** @brief Whether the two arrays below hold pages read: every state except
     *        BIND_READ_NONE.
     */
    bool        pages;
    uint16_t    slots[LINK_OS_COUNT];
    uint16_t    cfg[LINK_CC_COUNT];
} bind_reading_t;

/**
 * @brief Whether a binding @p b may be written while the last reading is @p
 *        last and the IOmcu answering is board @p board.
 *
 * Never a binding made for another board: a pin index belongs to one board's
 * catalogue.
 *
 * BIND_READ_OK: yes.  BIND_READ_ODD: only a binding that holds no pin, the
 * one way out of pages no binding describes.  BIND_READ_NONE and
 * BIND_READ_NO_BOARD: no -- a binding built on pages nobody read would
 * replace whatever the IOmcu holds with what a screen happened to
 * show.
 *
 * @param last The state of the last reading.
 * @param board The board the IOmcu answers as.
 * @param b The binding to write.
 * @return true when @p b may be written, false when @p b is NULL, made for
 *         another board, or @p last does not allow it.
 */
bool bind_link_may_write(bind_read_t last, uint16_t board,
                         const outbind_t *b);

/**
 * @brief Tell the four states apart from two replies.
 *
 * @p slots and @p cfg are the OUTPUTS and CHAN_CFG pages read, or NULL for one
 * that did not read.
 *
 * @param out Receives the reading. NULL gives BIND_READ_NONE and nothing is
 *        stored.
 * @param board The board the IOmcu answers as.
 * @param slots The OUTPUTS page, LINK_OS_COUNT registers, or NULL.
 * @param cfg The CHAN_CFG page, LINK_CC_COUNT registers, or NULL.
 * @return BIND_READ_NONE when either page is NULL. BIND_READ_NO_BOARD when
 *         this build has no pin map for @p board. BIND_READ_OK when the pages
 *         render to a binding. BIND_READ_ODD otherwise.
 */
bind_read_t bind_link_classify(bind_reading_t *out, uint16_t board,
                               const uint16_t *slots, const uint16_t *cfg);

/**
 * @brief Read OUTPUTS and then CHAN_CFG, two exchanges, and classify them.
 *
 * The second is not sent when the first did not read.
 *
 * @param h The link host to read through.
 * @param port The link port the IOmcu answers on.
 * @param board The board the IOmcu answers as.
 * @param out Receives the reading. NULL gives BIND_READ_NONE.
 * @return The state from bind_link_classify(), or BIND_READ_NONE when @p h or
 *         @p port is NULL or an exchange did not return a data frame.
 */
bind_read_t bind_link_read(link_host_t *h, const link_port_t *port,
                           uint16_t board, bind_reading_t *out);

#ifdef __cplusplus
}
#endif
