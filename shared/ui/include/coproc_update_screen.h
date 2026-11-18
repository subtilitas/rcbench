/**
 * @file
 * @brief The IOmcu's firmware update, as the panel offers and shows it.
 *
 * The offer: a link-up's build check that finds another build opens
 * SETUP > UPDATES, whose step 2 (IOMCU) holds the update's 2s hold
 * (UI_HOLD_S), the ARM gesture.  SKIP THIS STEP there, or the home tag
 * that leaves UPDATES, declines it.  The progress: a full screen without
 * the band, like the bus-fault screen, with the step under way and the
 * blocks sent.  The bench cannot be armed while it shows: the IOmcu
 * refuses ARM for the whole update session.  It returns to UPDATES.
 *
 * The screen decides nothing about the update.  The application offers it,
 * takes the START and the decline coproc_update_drive() hands over,
 * reports the step, and the driver moves the router away when the update
 * has ended.
 *
 * UPDATES' IOmcu step and SINGLE UPDATES' IOmcu section start the same
 * update, offered or declined: coproc_update_section() says whether it
 * may and why not, and coproc_update_drive() hands UPDATES that answer
 * every frame, takes its hold, opens the progress and returns to UPDATES
 * at the end.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_COPROC_UPDATE_SCREEN_H
#define RCBENCH_COPROC_UPDATE_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "ui_screen.h"
#include "ui_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The step the progress view names. */
typedef enum {
    CU_STEP_PREPARE = 0,  /**< hashing the image before the first block */
    CU_STEP_SEND,         /**< blocks going out                          */
    CU_STEP_VERIFY,       /**< the IOmcu hashes what it wrote      */
    CU_STEP_RESTART,      /**< it restarts into the new slot             */
    CU_STEP_BUY,          /**< and this panel keeps the new image        */
} cu_step_t;

/** A firmware version: major, minor, patch. */
typedef struct {
    uint16_t major,              /**< the major number */
             minor,              /**< the minor number */
             patch;              /**< the patch number */
} cu_version_t;

/**
 * @brief Whether @p carried is older than @p running: UPDATES' IOmcu step
 * then says so in the warning colour, and its hold still takes it.
 *
 * @param running the version the IOmcu runs
 * @param carried the version this panel carries
 * @return true when it is
 */
bool coproc_update_older(cu_version_t running, cu_version_t carried);

/**
 * @brief The versions the progress names: the one the IOmcu runs and the
 *        one this panel carries.  coproc_update_drive() sets them at the
 *        start; the renderer and the tests set them here.
 *
 * @param running the version the IOmcu runs
 * @param carried the version this panel carries
 */
void coproc_update_screen_versions(cu_version_t running,
                                   cu_version_t carried);

/**
 * @brief The step under way and the blocks committed of @p blocks.
 *
 * @param step the step
 * @param block the blocks committed
 * @param blocks the blocks the update has
 */
void coproc_update_screen_progress(cu_step_t step, uint16_t block,
                                   uint16_t blocks);

/** @brief Marks the whole screen to be drawn again. */
void coproc_update_screen_invalidate(void);

/**
 * @brief Whether an offer has opened UPDATES since the panel's start: the
 *        update run's own opening (upd_run.h) is then spent, so UPDATES
 *        opens once when both are due.
 *
 * @return true after the first opening for an offer, until the router's
 *         reset at the next start
 */
bool coproc_update_opened(void);

/** The control task's last look at the IOmcu's build. */
typedef enum {
    CU_CHECK_NONE = 0,    /**< no IOmcu checked since the start      */
    CU_CHECK_RUNNING,     /**< the build check runs                        */
    CU_CHECK_SAME,        /**< it runs the build this panel carries        */
    CU_CHECK_DIFFERENT,   /**< another build: offered, or declined         */
    CU_CHECK_USB,         /**< it takes no update over CAN                 */
    CU_CHECK_FAILED,      /**< the check ended without an answer           */
    CU_CHECK_UPDATING,    /**< an update, or the BUY of an image on trial  */
    CU_CHECK_NO_BOARD,    /**< it reports a board this panel carries no
                               image for                                    */
} cu_check_t;

/**
 * UPDATES' IOmcu section: whether its hold may start the update, and
 * if not why.  coproc_update_section() picks the first that holds, in this
 * order.  Only CU_SEC_READY takes the hold.
 */
typedef enum {
    CU_SEC_NO_IMAGE = 0,  /**< this build carries no IOmcu image     */
    CU_SEC_BUSY,          /**< an update runs, or a START is not yet taken */
    CU_SEC_NO_LINK,       /**< no IOmcu answers                      */
    CU_SEC_UNKNOWN,       /**< it answers; its build was not looked at yet */
    CU_SEC_CHECKING,      /**< the build check runs                        */
    CU_SEC_FAILED,        /**< the build check ended without an answer     */
    CU_SEC_USB,           /**< it takes no update over CAN                 */
    CU_SEC_NO_BOARD,      /**< it reports a board this panel carries no
                               image for                                    */
    CU_SEC_SAME,          /**< it runs the carried build: nothing to do    */
    CU_SEC_UNSAFE,        /**< ARMED, an arm waiting, the supply on        */
    CU_SEC_RADIO,         /**< the panel's radio is not wholly off         */
    CU_SEC_INSTALL,       /**< an install of a panel image is under way    */
    CU_SEC_READY,         /**< the hold starts the update                  */
} cu_sec_t;

/** The update as the application knows it, once a frame. */
typedef struct {
    bool         offered;       /**< the offer waits for a START or a
                                     decline                                */
    uint32_t     offer_gen;     /**< grows with each new offer              */
    bool         active;        /**< an update runs                         */
    cu_step_t    step;          /**< while active: the step and the blocks  */
    uint16_t     block;         /**< the blocks committed                   */
    uint16_t     blocks;        /**< the blocks the update has              */
    uint32_t     starts_taken;  /**< STARTs the update side has taken      */
    cu_version_t running;       /**< the version the IOmcu runs             */
    cu_version_t carried;       /**< the version this panel carries         */
    /* What UPDATES' IOmcu step and the offer's opening read.  All false is
     * a panel that may start an update and knows nothing of the IOmcu. */
    bool         have_image;    /**< this build carries an IOmcu image */
    bool         reachable;     /**< an IOmcu answers: the link is up,
                                     or one of another protocol major
                                     answers its identity                   */
    cu_check_t   check;         /**< the control task's last build check    */
    bool         radio_up;      /**< the panel's radio is not wholly off    */
    bool         installing;    /**< an install of a panel image under way  */
    bool         run_waits;     /**< an update run waits to open UPDATES
                                     (upd_run.h): the band line is the
                                     run's, and the offer neither speaks
                                     nor opens                              */
} cu_feed_t;

/** @brief What keeps an offer from opening UPDATES, in the order of
 *         cu_sec_t's reasons. */
typedef enum {
    CU_WAIT_NONE = 0,  /**< nothing: it opens on the menu              */
    CU_WAIT_UNSAFE,    /**< ARMED, an arm waiting, the supply on        */
    CU_WAIT_RADIO,     /**< the panel's radio is not wholly off        */
    CU_WAIT_INSTALL,   /**< an install of a panel image is under way   */
} cu_wait_t;

/** What the driver keeps between frames. */
typedef struct {
    uint32_t shown_gen;         /**< the offer UPDATES showed: opened for
                                     it, or current while it stood         */
    uint32_t starts_sent;       /**< STARTs handed to the update side       */
    uint32_t said_gen;          /**< the offer the band line was said for   */
    uint8_t  said_wait;         /**< the cu_wait_t it named                 */
} cu_driver_t;

/**
 * @brief What keeps @p f's offer from opening UPDATES now.
 * @param f      The update as the application knows it this frame.
 * @param unsafe ARMED, an arm waiting, or the supply's output on or coming.
 * @return The first reason that holds, CU_WAIT_NONE for none.
 */
cu_wait_t coproc_update_wait(const cu_feed_t *f, bool unsafe);

/**
 * @brief The band line of an offer: that the IOmcu runs another build, and
 *        what the opening of UPDATES waits for.
 * @param w What it waits for.
 * @return The alert's text ID: ALERT_CU_OFFER for CU_WAIT_NONE.
 */
ui_text_id_t coproc_update_offer_text(cu_wait_t w);

/** What one frame hands to the update side. */
typedef struct {
    unsigned starts;            /**< STARTs given by UPDATES' holds         */
    bool     decline;           /**< the offer UPDATES showed was declined  */
} cu_hand_t;

/**
 * @brief Whether UPDATES' hold may start the update now, and if not why: the
 * first of cu_sec_t's reasons that holds.  @p unsafe: ARMED, an arm
 * waiting, or the supply's output on or coming.  A START handed over and
 * not yet taken (@p d's starts_sent against @p f's starts_taken) is BUSY.
 *
 * The hold asks for another build (CU_CHECK_DIFFERENT; the same version
 * number with another build is one), offered or declined, a bench that is
 * not @p unsafe, the radio wholly off and no panel install: the
 * conditions under which an offer opens UPDATES.  An older image is
 * READY.  One that runs the carried build is SAME: the check found
 * nothing to send, and the link-up offers nothing either.
 *
 * @param d the update side's state
 * @param f the control task's feed
 * @param unsafe true when the bench is ARMED, an arm waits, or the supply's
 *   output is on or coming
 * @return the first reason that holds
 */
cu_sec_t coproc_update_section(const cu_driver_t *d, const cu_feed_t *f,
                               bool unsafe);

/**
 * @brief The screen's half of the update, once a frame, whichever screen is
 *        current: opens UPDATES for an offer, moves the router to the
 *        progress and back, and drains UPDATES' latches.
 *
 * An offer opens UPDATES once, on the overview of a bench that is not
 * @p unsafe, with the radio wholly off and no panel install under way,
 * and not while an update run waits to open it (cu_feed_t.run_waits).
 * UPDATES current while an offer stands shows it as well: it does not
 * open UPDATES again later.  A new offer brings back an IOmcu step that
 * was skipped, and a new offer while UPDATES shows ends a hold under way
 * on that step: its versions may have changed.
 *
 * Only an operator's act declines: SKIP THIS STEP on UPDATES' IOmcu step
 * (upd_screen_take_skip_coproc()), or the home tag that leaves UPDATES
 * (upd_screen_take_left_cu()).  Either is drained here on the next frame
 * and handed over as a decline while the offer UPDATES showed stands; one
 * given for an offer replaced since (another offer_gen, the withdrawal
 * between two frames) declines nothing.  A screen the panel forces over
 * UPDATES (the fault screen of a lost link, PANEL UPDATE's download)
 * declines nothing: the offer stands, and a new offer opens UPDATES
 * again.  STOP declines nothing.
 *
 * The band line of an offer UPDATES has not shown: once per offer, on the
 * first frame a screen with a band shows, ALERT_CU_OFFER, or the line that
 * names what the opening waits for (coproc_update_offer_text()); again
 * each time it waits for something else.  While an update run waits to
 * open UPDATES its own line stands, and the offer's counts as said.
 *
 * And UPDATES' IOmcu step: its state (coproc_update_section()) and the two
 * versions go to upd_screen_set_coproc() every frame, and a completed hold
 * (upd_screen_take_coproc()) on a section still READY opens the progress
 * and hands over a START.  The progress returns to UPDATES when the
 * update ends or its START was spent.
 *
 * @param d      What the driver keeps between frames.
 * @param f      The update as the application knows it this frame.
 * @param unsafe ARMED, an arm waiting, or the supply's output on or coming.
 * @param out    What this frame hands to the update side.
 */
void coproc_update_drive(cu_driver_t *d, const cu_feed_t *f, bool unsafe,
                         cu_hand_t *out);

/**
 * @brief Gives the screen's table.
 *
 * @return the table
 */
const ui_screen_t *coproc_update_screen(void);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_COPROC_UPDATE_SCREEN_H */
