/*
 * The panel's half of the IOmcu update: finding out whether the
 * IOmcu runs the image the panel carries, sending it, and buying it
 * once it has started.
 *
 * Stepwise.  Each link_uph_step() does one thing -- one block of 64 bytes
 * (8 data frames and a COMMIT), one read, one command, or 8 KiB of a hash --
 * and returns, so the caller runs its own loop between steps.  The wire is
 * a link_port_t, so the host suite runs every step against a simulated
 * IOmcu over the real link layer.
 *
 * Only the frozen registers are used: IDENTITY 0 to 6 and the two update
 * pages.  An IOmcu of another protocol major is checked and updated
 * the same way.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_LINK_UPDATE_HOST_H
#define RCBENCH_LINK_UPDATE_HOST_H

#include <stdbool.h>
#include <stdint.h>

#include "link_host.h"
#include "link_pages.h"
#include "link_port.h"
#include "link_sha256.h"
#include "link_update_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Image bytes hashed in one step. */
#define LINK_UPH_HASH_STEP   8192u
/** How long exchanges may go unanswered in a row before the link counts
 *  as gone: an IOmcu that restarts answers again well inside it.  An
 *  unanswered exchange takes LINK_HOST_TIMEOUT_MS (1000 ms) at the panel. */
#define LINK_UPH_LOST_MS     4000u
/** Starts from block 0 after the IOmcu lost the session (a restart,
 *  link silence) before the update gives up. */
#define LINK_UPH_RESTARTS    3u
/** Refusals of the same block before the update gives up. */
#define LINK_UPH_BLOCK_TRIES 3u
/** How long the store's gate may stay shut while a command waits for it. */
#define LINK_UPH_BUSY_MS     10000u
/** How long a hash at the IOmcu may take: 2036 KiB at 1 KiB a pass. */
#define LINK_UPH_HASH_MS     10000u
/** After ACTIVATE: nothing asked for this long, so the old image has
 *  restarted before identity is read. */
#define LINK_UPH_SETTLE_MS   500u
/** After ACTIVATE: the new image has to answer and be bought inside this,
 *  well inside the boot ROM's 16.7 s TBYB (try before you buy) watchdog. */
#define LINK_UPH_CONFIRM_MS  12000u

typedef enum {
    LINK_UPH_IDLE = 0,
    LINK_UPH_CHECK_HASH,  /**< this end's build hash                     */
    LINK_UPH_CHECK_ASK,   /**< the IOmcu asked to hash its slot     */
    LINK_UPH_CHECK_WAIT,  /**< and its answer awaited                     */
    LINK_UPH_PREPARE,     /**< the SHA-256 of the image as it is sent     */
    LINK_UPH_BEGIN,
    LINK_UPH_SEND,        /**< a block, then its COMMIT                   */
    LINK_UPH_RESYNC,      /**< where the IOmcu stands, after a miss */
    LINK_UPH_FINISH,
    LINK_UPH_VERIFY,
    LINK_UPH_ACTIVATE,
    LINK_UPH_RESTARTING,  /**< the IOmcu restarts into the new slot */
    LINK_UPH_BUY,
    LINK_UPH_DONE,        /**< result says how it ended                   */
} link_uph_phase_t;

typedef enum {
    LINK_UPH_NONE = 0,     /**< not ended yet                             */
    LINK_UPH_SAME,         /**< check: it runs this end's build           */
    LINK_UPH_DIFFERENT,    /**< check: it runs another build              */
    LINK_UPH_TRIAL,        /**< check: an image on trial waits for BUY    */
    LINK_UPH_NEEDS_USB,    /**< no update pages, or no slots              */
    LINK_UPH_NO_ANSWER,    /**< unanswered for LINK_UPH_LOST_MS           */
    LINK_UPH_REFUSED,      /**< refused; error holds the link_upd_err_t   */
    LINK_UPH_BAD_DIGEST,   /**< the slot's SHA-256 is not the image's     */
    LINK_UPH_BUSY,         /**< the store's gate stayed shut              */
    LINK_UPH_RESTARTED_OUT, /**< LINK_UPH_RESTARTS starts from block 0    */
    LINK_UPH_NOT_STARTED,  /**< no new image on trial after the restart   */
    LINK_UPH_UPDATED,      /**< the new image runs and is bought          */
} link_uph_result_t;

typedef struct {
    const uint8_t *img;
    uint32_t       len;
    int32_t        flag_at;   /**< the TBYB flag's byte, or -1            */

    link_uph_phase_t phase;
    uint8_t  result;          /**< a link_uph_result_t                    */
    uint8_t  error;           /**< the IOmcu's link_upd_err_t       */
    bool     missing;         /**< the last exchange went unanswered      */
    uint8_t  restarts;
    uint8_t  block_tries;
    uint8_t  after_resync;    /**< the phase a RESYNC was entered from    */
    bool     aborted;         /**< check: a session found was ended       */

    uint8_t  build[LINK_SHA256_BYTES];   /**< this end's build hash       */
    uint8_t  expect[LINK_SHA256_BYTES];  /**< the image as sent           */
    uint8_t  digest[LINK_SHA256_BYTES];  /**< what the IOmcu said   */
    link_image_hash_t hash;

    uint16_t block;           /**< the block to send next                 */
    uint16_t blocks;
    uint16_t slot;            /**< LINK_UPD_SLOT before the restart        */
    uint16_t block_tried;     /**< the block block_tries counts for        */
    bool     busy;            /**< waiting for the gate since busy_ms     */
    uint32_t busy_ms;
    uint32_t since_ms;        /**< when the phase's wait began            */
    uint32_t missing_ms;      /**< when the exchanges stopped being answered */

    bool     have_ident;
    uint16_t ident[LINK_ID_COUNT]; /**< identity read after the restart    */
} link_uph_t;

/**
 * Find out whether the IOmcu runs the build in @p img: this end's
 * build hash, then the IOmcu's over its running slot.  Ends SAME,
 * DIFFERENT, TRIAL, NEEDS_USB, NO_ANSWER or REFUSED.
 *
 * A session found in the IOmcu -- this panel's own, from before it
 * restarted or lost the link -- is ended with ABORT first: nobody sends its
 * blocks any more, and it would hold ARM.  An ACTIVATE found is waited out
 * for LINK_UPH_HASH_MS: the restart it leads to ends it.
 */
void link_uph_check(link_uph_t *u, const uint8_t *img, uint32_t len,
                    uint32_t now_ms);

/**
 * Send @p img to the slot that is not running, check it, start it and buy
 * it.  Ends UPDATED or with the reason it stopped.
 */
void link_uph_update(link_uph_t *u, const uint8_t *img, uint32_t len,
                     uint32_t now_ms);

/** Buy an image found on trial.  Ends UPDATED, NO_ANSWER or REFUSED. */
void link_uph_confirm(link_uph_t *u, uint32_t now_ms);

/** One step; returns the phase it leaves the update in. */
link_uph_phase_t link_uph_step(link_uph_t *u, link_host_t *h,
                               const link_port_t *port);

/** Blocks committed, in thousandths of the image: 0 to 1000. */
uint16_t link_uph_permille(const link_uph_t *u);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_UPDATE_HOST_H */
