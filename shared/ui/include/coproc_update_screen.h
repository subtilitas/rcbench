/*
 * The IOmcu's firmware update, as the panel offers and shows it.
 *
 * Two views.  The question: which version the IOmcu runs, which one
 * this panel carries, whether that one is older, CANCEL and START.  START
 * takes the 2 s hold (UI_HOLD_S), the ARM gesture.  The progress: a full
 * screen without the band, like the bus-fault screen, with the step under
 * way and the blocks sent.  The bench cannot be armed while it shows: the
 * IOmcu refuses ARM for the whole update session.
 *
 * The screen decides nothing about the update.  The application offers it,
 * takes START or CANCEL from the latches, reports the step, and moves the
 * router away when the update has ended.
 *
 * SETUP > UPDATES starts the same update from its IOmcu section,
 * offered or declined: coproc_update_section() says whether it may and why
 * not, and coproc_update_drive() hands UPDATES that answer every frame,
 * takes its hold, opens the progress and returns to UPDATES at the end.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_COPROC_UPDATE_SCREEN_H
#define RCBENCH_COPROC_UPDATE_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CU_VIEW_QUESTION = 0,
    CU_VIEW_PROGRESS,
} cu_view_t;

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
    uint16_t major, minor, patch;
} cu_version_t;

/**
 * Whether @p carried is older than @p running: the question then says so,
 * and START still takes it after the hold.
 */
bool coproc_update_older(cu_version_t running, cu_version_t carried);

/**
 * Offer the update: the question view, with the version the IOmcu
 * runs and the one this panel carries.  Clears both latches.
 */
void coproc_update_screen_offer(cu_version_t running, cu_version_t carried);

/** The step under way and the blocks committed of @p blocks. */
void coproc_update_screen_progress(cu_step_t step, uint16_t block,
                                   uint16_t blocks);

cu_view_t coproc_update_screen_view(void);

/** True once after START's hold completed; the view is then PROGRESS. */
bool coproc_update_screen_take_start(void);

/** True once after CANCEL, or after the question was left another way. */
bool coproc_update_screen_take_cancel(void);

void coproc_update_screen_invalidate(void);

/** The control task's last look at the IOmcu's build. */
typedef enum {
    CU_CHECK_NONE = 0,    /**< no IOmcu checked since the start      */
    CU_CHECK_RUNNING,     /**< the build check runs                        */
    CU_CHECK_SAME,        /**< it runs the build this panel carries        */
    CU_CHECK_DIFFERENT,   /**< another build: offered, or declined         */
    CU_CHECK_USB,         /**< it takes no update over CAN                 */
    CU_CHECK_FAILED,      /**< the check ended without an answer           */
    CU_CHECK_UPDATING,    /**< an update, or the BUY of an image on trial  */
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
    CU_SEC_SAME,          /**< it runs the carried build: nothing to do    */
    CU_SEC_UNSAFE,        /**< ARMED, an arm waiting, the supply on        */
    CU_SEC_RADIO,         /**< the panel's radio is not wholly off         */
    CU_SEC_INSTALL,       /**< an install of a panel image is under way    */
    CU_SEC_READY,         /**< the hold starts the update                  */
} cu_sec_t;

/** The update as the application knows it, once a frame. */
typedef struct {
    bool         offered;       /**< the question waits for START or CANCEL */
    uint32_t     offer_gen;     /**< grows with each new offer              */
    bool         active;        /**< an update runs                         */
    cu_step_t    step;          /**< while active: the step and the blocks  */
    uint16_t     block;
    uint16_t     blocks;
    uint32_t     starts_taken;  /**< STARTs the update side has taken      */
    cu_version_t running;       /**< the version the IOmcu runs       */
    cu_version_t carried;       /**< the version this panel carries         */
    /* What UPDATES' section and the question's gate read.  All false is a
     * panel that may start an update and knows nothing of the IOmcu. */
    bool         have_image;    /**< this build carries an IOmcu image */
    bool         reachable;     /**< an IOmcu answers: the link is up,
                                     or one of another protocol major
                                     answers its identity                   */
    cu_check_t   check;         /**< the control task's last build check    */
    bool         radio_up;      /**< the panel's radio is not wholly off    */
    bool         installing;    /**< an install of a panel image under way  */
} cu_feed_t;

/** What the driver keeps between frames. */
typedef struct {
    uint32_t shown_gen;         /**< the offer the question opened for      */
    uint32_t starts_sent;       /**< STARTs handed to the update side       */
    bool     from_updates;      /**< the last update started from UPDATES;
                                     the question's opening clears it      */
} cu_driver_t;

/** What one frame hands to the update side. */
typedef struct {
    unsigned starts;            /**< STARTs given on the question           */
    bool     decline;           /**< the question was declined              */
} cu_hand_t;

/**
 * Whether UPDATES' hold may start the update now, and if not why: the
 * first of cu_sec_t's reasons that holds.  @p unsafe: ARMED, an arm
 * waiting, or the supply's output on or coming.  A START handed over and
 * not yet taken (@p d's starts_sent against @p f's starts_taken) is BUSY.
 *
 * The hold asks for what the question asks for: an IOmcu that runs
 * another build (CU_CHECK_DIFFERENT; the same version number with another
 * build is one), offered or declined, a bench that is not @p unsafe, the
 * radio wholly off and no panel install.  An older image is READY, as the
 * question offers it too.  One that runs the carried build is SAME: the
 * check found nothing to send, and the link-up offers nothing either.
 */
cu_sec_t coproc_update_section(const cu_driver_t *d, const cu_feed_t *f,
                               bool unsafe);

/**
 * The screen's half of the update, once a frame, whichever screen is
 * current: moves the router to the question, the progress and back, and
 * drains both latches.
 *
 * Leaving the question any way but START -- CANCEL, the HOME tag, an alert,
 * STOP, any jump of the router -- latches CANCEL on the way out, after the
 * router has left the screen; the latch is drained here on the next frame
 * and handed over as a decline while the offer stands.  The question
 * opens once per offer, on the overview of a bench that is not @p unsafe,
 * with the radio wholly off and no panel install under way.
 *
 * And UPDATES' section: its state (coproc_update_section()) and the two
 * versions go to upd_screen_set_coproc() every frame, and a completed hold
 * (upd_screen_take_coproc()) on a section still READY opens the progress
 * and hands over a START as the question's does.  That progress returns to
 * UPDATES when the update ends or its START was spent; the question's
 * returns to the menu.
 */
void coproc_update_drive(cu_driver_t *d, const cu_feed_t *f, bool unsafe,
                         cu_hand_t *out);

const ui_screen_t *coproc_update_screen(void);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_COPROC_UPDATE_SCREEN_H */
