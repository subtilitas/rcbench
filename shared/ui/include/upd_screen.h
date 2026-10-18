/*
 * Panel updates from GitHub: SETUP's UPDATES screen and the reminder.
 *
 * UPDATES (SCREEN_UPDATES, from SETUP): CHECK FOR UPDATES asks GitHub once
 * which release is the newest, over the home network the panel keeps; the
 * answer stays on the screen.  REMINDER AT START switches the start-up
 * check on and off (SET_UPD_REMIND).  When a newer release was found, HOLD
 * 2 s: DOWNLOAD & INSTALL fetches and installs it.
 *
 * Its IOmcu section, under the buttons: the version the IOmcu
 * runs, the version of the image this panel carries, and HOLD 2 s: UPDATE
 * IOMCU, which starts the update the link-up's question starts, also
 * after that question was declined.  The hold is live only while
 * coproc_update_section() reads CU_SEC_READY; any other state greys it and
 * says why in the line above it.  coproc_update_drive() hands the section
 * in (upd_screen_set_coproc()) and drains the hold
 * (upd_screen_take_coproc()).
 *
 * The reminder (SCREEN_UPD_REMIND): the start-up check found a newer
 * release.  It opens on OVERVIEW only, with the bench SAFE and the supply
 * off, and never over the IOmcu's update question or its progress,
 * which come first.  IGNORE NOW closes it until the next start; NEVER
 * REMIND ME switches SET_UPD_REMIND off and asks for the save; HOLD 2 s:
 * DOWNLOAD & INSTALL is the same hold as on UPDATES, and the one
 * confirmation: no question follows the download.  Leaving it by the HOME
 * tag is IGNORE NOW.
 *
 * The screens command nothing.  The application hands them what to show
 * (upd_screen_set()) and drains what they ask for (upd_screen_take()), as
 * FILES & UPDATE does.  The enums mirror shared/webaccess's wa_fetch.h and
 * shared/fwupdate's fw_release.h; the application maps one onto the other
 * and test_upd_screen holds them equal.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_UPD_SCREEN_H
#define RCBENCH_UPD_SCREEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "coproc_update_screen.h"
#include "ui_screen.h"
#include "wifi_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** wa_job_t */
typedef enum {
    UPD_JOB_CHECK = 0,
    UPD_JOB_DOWNLOAD,
} upd_job_t;

/** wa_phase_t */
typedef enum {
    UPD_PH_IDLE = 0,
    UPD_PH_JOINING,
    UPD_PH_ASKING,
    UPD_PH_SUMS,
    UPD_PH_IMAGE,
    UPD_PH_CHECKING,
    UPD_PH_ENDED,
} upd_phase_t;

/** wa_got_t */
typedef enum {
    UPD_GOT_NONE = 0,
    UPD_GOT_NEWER,
    UPD_GOT_CURRENT,
    UPD_GOT_STAGED,
    UPD_GOT_NO_NET,
    UPD_GOT_BLOCKED,
    UPD_GOT_ABORTED,
    UPD_GOT_JOIN,
    UPD_GOT_RADIO,
    UPD_GOT_TIMEOUT,
    UPD_GOT_CONNECT,
    UPD_GOT_REFUSED,
    UPD_GOT_UNEXPECTED,
    UPD_GOT_SUMS,
    UPD_GOT_HASH,
    UPD_GOT_IMAGE,
    UPD_GOT_MEMORY,
} upd_got_t;

/** wa_why_t */
typedef enum {
    UPD_WHY_NONE = 0,
    UPD_WHY_ARMED,
    UPD_WHY_SUPPLY,
    UPD_WHY_RUN,
    UPD_WHY_STOP,
    UPD_WHY_FILES,
    UPD_WHY_COPROC,
    UPD_WHY_INSTALL,
    UPD_WHY_CANCEL,
    UPD_WHY_BUSY,
    UPD_WHY_DRAWING,
} upd_why_t;

/** fw_rel_err_t */
typedef enum {
    UPD_REL_OK = 0,
    UPD_REL_MISSING,
    UPD_REL_LONG,
    UPD_REL_CHAR,
    UPD_REL_PLACE,
    UPD_REL_VERSION,
    UPD_REL_STATUS,
    UPD_REL_HOPS,
    UPD_REL_HOST,
    UPD_REL_NO_LENGTH,
    UPD_REL_EMPTY,
    UPD_REL_TOO_BIG,
    UPD_REL_SHORT,
    UPD_REL_OVERRUN,
    UPD_REL_SUMS_BAD,
    UPD_REL_SUMS_NONE,
    UPD_REL_SUMS_TWICE,
} upd_rel_t;

typedef struct {
    /* What may run. */
    bool        unavailable;   /**< Wi-Fi could not be set up at start    */
    bool        have_net;
    char        net[WIFI_SSID_MAX + 1];
    upd_why_t   block;         /**< why a check may not start now          */
    upd_why_t   dl_block;      /**< why a download may not start now       */
    char        running[WIFI_VER_MAX + 1];

    /* The last job. */
    upd_job_t   job;
    upd_phase_t phase;
    upd_got_t   got;
    upd_why_t   why;           /**< BLOCKED, ABORTED                       */
    wifi_join_t join;          /**< JOIN                                   */
    upd_rel_t   rel;           /**< UNEXPECTED, SUMS                       */
    int         status;        /**< REFUSED, and SUMS by status            */
    uint32_t    budget_s;      /**< TIMEOUT: the budget that ran out       */
    char        found[WIFI_VER_MAX + 1];   /**< NEWER, CURRENT; DOWNLOAD's */
    uint32_t    left_s;        /**< while busy                             */
    uint32_t    done;          /**< IMAGE: bytes received of total         */
    uint32_t    total;
} upd_view_t;

/** What the screens ask for: bits of upd_screen_take(). */
#define UPD_REQ_CHECK     0x01u   /**< CHECK FOR UPDATES                  */
#define UPD_REQ_CANCEL    0x02u   /**< CANCEL a check                     */
#define UPD_REQ_DOWNLOAD  0x04u   /**< the 2 s hold completed             */

/** Hand the screens what to show. */
void upd_screen_set(const upd_view_t *v);

/** UPDATES' IOmcu section. */
typedef struct {
    cu_sec_t     sec;
    bool         known;      /**< running is a version an IOmcu named */
    bool         have_image; /**< carried is an image this panel holds     */
    cu_version_t running;
    cu_version_t carried;
} upd_cu_view_t;

/** Hand UPDATES its IOmcu section; a hold the state no longer
 *  allows ends unfired. */
void upd_screen_set_coproc(const upd_cu_view_t *v);

/** What the section shows, for the driver and the tests. */
const upd_cu_view_t *upd_screen_coproc(void);

/** True once after HOLD 2 s: UPDATE IOMCU completed. */
bool upd_screen_take_coproc(void);

/** The section's state line for @p v, and its colour. */
void upd_cu_text(const upd_cu_view_t *v, const char **line,
                 gfx_color_t *col);

/** What is showing, for the application and the tests. */
const upd_view_t *upd_screen_view(void);

/** The requests since the last call, as UPD_REQ_* bits; cleared. */
unsigned upd_screen_take(void);

/** Whether a job runs: joining to checking. */
bool upd_busy(const upd_view_t *v);

/**
 * The line that says how the last job ended, or what it does now: into
 * @p out of @p n bytes; empty for none.  @p second gets the line under it,
 * or empty.  Both screens and the download's progress draw these.
 */
void upd_result_text(const upd_view_t *v, char *out, size_t n,
                     char *second, size_t n2);

/** Repaint on the next frame. */
void upd_screen_invalidate(void);

const ui_screen_t *upd_screen(void);
const ui_screen_t *upd_remind_screen(void);

/* ------------------------------------------------------ the driver */

/**
 * The start-up check waits for a moment the bench allows it, and gives up
 * after this long from the panel's start: a check is a start-up thing, not
 * one that comes up an hour into a session.
 */
#define UPD_START_WAIT_MS (10u * 60u * 1000u)

/** What the application knows once a frame. */
typedef struct {
    uint32_t now_ms;
    bool     remind;       /**< SET_UPD_REMIND                             */
    bool     available;    /**< Wi-Fi was set up at start                  */
    bool     have_net;
    bool     gate_open;    /**< SAFE, supply off, no log run, nothing else */
    bool     files_open;   /**< FILES & UPDATE is open                     */
    bool     coproc;       /**< the IOmcu's update is offered or runs */
    bool     unsafe;       /**< ARMED, an arm waiting, the supply on        */
    bool     newer;        /**< the start-up check found a newer release   */
} upd_feed_t;

typedef struct {
    bool     started;      /**< start_ms is set                             */
    uint32_t start_ms;     /**< the first drive                             */
    bool     check_done;   /**< the start-up check started or gave up       */
    bool     dismissed;    /**< answered at this start                      */
} upd_driver_t;

/**
 * Whether the start-up check starts now: once per start, with the reminder
 * on, a home network kept, Wi-Fi available, the gate open, FILES & UPDATE
 * closed, the IOmcu's update neither offered nor running, the router
 * past the splash, and within UPD_START_WAIT_MS of the first drive.  True
 * once; the caller starts the check.
 */
bool upd_startup_due(upd_driver_t *d, const upd_feed_t *f);

/**
 * The reminder, once a frame, whichever screen is current.  Opens it on
 * OVERVIEW when the start-up check found a newer release, the reminder is
 * on, it was not answered at this start, and the bench is not unsafe and
 * the IOmcu's update not offered or running.  Takes it down again,
 * unanswered, for any of those changing under it.  Drains its answers.
 */
void upd_remind_drive(upd_driver_t *d, const upd_feed_t *f);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_UPD_SCREEN_H */
