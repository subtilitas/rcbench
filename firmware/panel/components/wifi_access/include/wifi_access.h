/**
 * @file
 * @brief The panel's Wi-Fi: the radio, the page's HTTP (hypertext transfer
 *        protocol) server, the mDNS (multicast domain name system) name and
 *        the NVS (non-volatile storage) entry of the home network.
 *
 * What the radio may do and when is shared/webaccess (wa_session.h), which
 * the host suite holds; this file carries it out.  The radio is off at every
 * start and comes up only on wifi_access_start().  Nothing about it touches
 * the bench: the link and the heartbeat run as before while it is up.
 *
 * One task brings the radio up and down, so no caller waits for it: the
 * UI (user interface) loop asks, and reads what is (wifi_access_get()).
 * An image the page receives is kept in PSRAM (pseudo-static random-access
 * memory) until the operator installs or drops it; the page never starts a
 * write.
 *
 * The same radio carries the panel's own requests to GitHub (wa_fetch.h):
 * the release check and the download of a release's image, each on the
 * kept home network, in WA_MODE_FETCH, without page or name.  A download
 * leaves its image staged where an upload leaves one, for the same
 * installer.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WIFI_ACCESS_H
#define RCBENCH_WIFI_ACCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "fw_image.h"
#include "fw_upload.h"
#include "wa_fetch.h"
#include "wa_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The access point's channel, 2.4GHz band. */
#define WIFI_ACCESS_AP_CHANNEL 6u

/** What the screen draws, as a copy. */
typedef struct {
    wa_t            w;
    wa_conf_t       conf;
    bool            save_failed;  /**< the settings' last write failed */
    char            name[WA_NAME_MAX];
    uint8_t         channel;
    bool            scanned;
    wa_scan_t       scan;
    fw_up_state_t   up;
    fw_err_t        up_err;
    uint32_t        received;
    uint32_t        total;
    fw_image_info_t info;      /**< of a staged image         */
    fw_offer_t      offer;     /**< of a staged image         */
    uint32_t        idle_left_ms;
    bool            down;      /**< the radio is wholly off   */
} wifi_access_state_t;

/**
 * Read the kept home network and set up the task.  @p slot is the largest
 * image taken, @p running the build that runs.  The radio stays off.
 * On a failure nothing is kept, and no other call of this file may be
 * made: the panel runs without Wi-Fi until the next start.
 */
esp_err_t wifi_access_init(uint32_t slot, const fw_image_info_t *running);

/** The page's language. */
void wifi_access_set_german(bool de);

/** Start in @p mode unless @p gate says why not; see wa_start(). */
wa_off_t wifi_access_start(wa_mode_t mode, wa_off_t gate);

/**
 * @brief Radio off for @p why; see wa_close().
 * @param why The reason.
 */
void wifi_access_close(wa_off_t why);

/**
 * @brief Once a frame: the gate, STOP pressed since the last call, the
 *        time-outs; and a change to what is kept written when @p may_write.
 * @param gate      wa_gate()'s answer.
 * @param stop      STOP pressed since the last call.
 * @param may_write Flash may be written now.
 * @return Whether this call switched the radio off or ended its linger
 *         (wa_tick()).
 */
bool wifi_access_tick(wa_off_t gate, bool stop, bool may_write);

void wifi_access_get(wifi_access_state_t *out);

/**
 * @brief Whether the radio is wholly off and no start of it is asked for;
 *        from any task, without the lock.
 *
 * A start asked for counts as up from the call on, a close until the
 * station or access point is stopped and its interface is gone.  A GitHub
 * fetch task still in a name lookup then does not hold it up: it sends
 * nothing while the radio stays off, and no fetch job starts before it
 * ends.
 *
 * @return true when the radio is off and nothing asks for it
 */
bool wifi_access_down(void);

/**
 * @brief The radio goes off without waiting for the clock's NTP answer:
 *        a session that ended and lingers (wa_lingers()) stops lingering;
 *        one that runs goes on.  From any task, without the lock.
 */
void wifi_access_end_linger(void);

/** Keep a home network; false when wa_conf_set_net() refuses it. */
bool wifi_access_save_net(const char *ssid, const char *pass);
/** FORGET: no home network, the access code on. */
void wifi_access_forget(void);
void wifi_access_set_code(bool on);

/** Drop a received or staged image.  Nothing while one is taken. */
void wifi_access_discard(void);

/**
 * Take the staged image for the installer: it stays where it is, and
 * nothing drops it, until wifi_access_release().  False when none waits.
 */
bool wifi_access_take(const uint8_t **img, uint32_t *bytes,
                      fw_image_info_t *info);
/** After a write that failed: the image is dropped. */
void wifi_access_release(void);

/**
 * Start @p job (wa_fetch_start()) for release @p want (DOWNLOAD only) on
 * channel @p ch (CHECK: which releases it looks at) unless @p gate says
 * why not.  WA_GOT_NONE: the radio comes up in
 * WA_MODE_FETCH and the job runs; otherwise it has ended at once.  A
 * DOWNLOAD drops an image the page left staged first; while one is taken,
 * or the last job's task has not ended yet, it is BLOCKED for BUSY.
 * Refused on a session another screen runs: the caller starts no job
 * while WI-FI is open.
 */
wa_got_t wifi_access_fetch_start(wa_job_t job, fw_version_t want,
                                 wa_channel_t ch, wa_why_t gate);

/**
 * Once a frame, after wifi_access_tick(): @p abort as wa_fetch_tick()
 * takes it.  Moves the job on, ends it, switches the radio off when it has
 * ended, and starts the task that talks to GitHub once joined.
 */
void wifi_access_fetch_tick(wa_why_t abort);

/** The job, as a copy. */
void wifi_access_fetch_get(wa_fetch_t *out);

/** Whether a home network is kept, and its name into @p ssid of @p n
 *  bytes (empty for none). */
bool wifi_access_home(char *ssid, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WIFI_ACCESS_H */
