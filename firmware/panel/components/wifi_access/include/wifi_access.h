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
#include "fm_io.h"
#include "fm_rules.h"
#include "fw_upload.h"
#include "wa_api.h"
#include "wa_app.h"
#include "wa_fetch.h"
#include "wa_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The access point's channel, 2.4GHz band. */
#define WIFI_ACCESS_AP_CHANNEL 6u

/** What the screen draws, as a copy. */
typedef struct {
    wa_t            w;  /**< The session's state. */
    wa_conf_t       conf;  /**< The kept settings. */
    bool            save_failed;  /**< the settings' last write failed */
    /** @brief The name the access point and the page show. */
    char            name[WA_NAME_MAX];
    /**
     * @brief The access point's channel; 0 when the session is not an access
     *        point.
     */
    uint8_t         channel;
    bool            scanned;  /**< Whether a scan has run. */
    wa_scan_t       scan;  /**< The scan's result. */
    fw_up_state_t   up;  /**< The upload's state. */
    fw_err_t        up_err;  /**< The upload's error. */
    uint32_t        received;  /**< Bytes of the image received. */
    uint32_t        total;  /**< Bytes the upload announced. */
    fw_image_info_t info;      /**< of a staged image         */
    fw_offer_t      offer;     /**< of a staged image         */
    /** @brief Milliseconds before the idle time-out ends the session. */
    uint32_t        idle_left_ms;
    bool            down;      /**< the radio is wholly off   */
} wifi_access_state_t;

/**
 * @brief Read the kept home network and set up the task.  @p slot is the
 *        largest image taken, @p running the build that runs.  The radio
 *        stays off. On a failure nothing is kept, and no other call of this
 *        file may be made: the panel runs without Wi-Fi until the next start.
 * @param slot the largest image taken, bytes
 * @param running the build that runs
 * @return ESP_OK; otherwise the error that stopped the set-up
 */
esp_err_t wifi_access_init(uint32_t slot, const fw_image_info_t *running);

/**
 * @brief The page's language.
 * @param de true for German, false for English
 */
void wifi_access_set_german(bool de);

/**
 * @brief Start in @p mode unless @p gate says why not; see wa_start().
 * @param mode what the session is for
 * @param gate wa_gate()'s answer
 * @return WA_OFF_NONE when the radio comes up; otherwise the reason it stays
 *         off (wa_start())
 */
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

/**
 * @brief The session's state, as a copy.
 * @param out takes the state
 */
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

/**
 * @brief Keep a home network; false when wa_conf_set_net() refuses it.
 * @param ssid the network's name
 * @param pass its password
 * @return true when the network was kept
 */
bool wifi_access_save_net(const char *ssid, const char *pass);
/** FORGET: no home network, the access code on. */
void wifi_access_forget(void);
/**
 * @brief Switch the access code the page asks for on or off.
 * @param on true to ask for the code
 */
void wifi_access_set_code(bool on);

/**
 * @brief Keep @p value as HOTSPOT PASSWORD or WEB CODE, or "" for RANDOM.
 *        Radio sessions from the next start take it; the store is written on
 *        a frame that may write flash, as for the rest of what is kept.
 * @param which Which of the two.
 * @param value The value, terminated.
 * @return false when wa_kept_check() refuses it: what stood stands.
 */
bool wifi_access_set_kept(wa_kept_t which, const char *value);

/** Drop a received or staged image.  Nothing while one is taken. */
void wifi_access_discard(void);

/**
 * @brief Take the staged image for the installer: it stays where it is, and
 *        nothing drops it, until wifi_access_release().  False when none
 *        waits.
 * @param img takes the image, which stays until wifi_access_release()
 * @param bytes takes its size
 * @param info takes its header information
 * @return true when an image was taken
 */
bool wifi_access_take(const uint8_t **img, uint32_t *bytes,
                      fw_image_info_t *info);
/** After a write that failed: the image is dropped. */
void wifi_access_release(void);

/**
 * @brief Start @p job (wa_fetch_start()) for release @p want (DOWNLOAD only)
 *        on channel @p ch (CHECK: which releases it looks at) unless @p gate
 *        says why not.  WA_GOT_NONE: the radio comes up in WA_MODE_FETCH and
 *        the job runs; otherwise it has ended at once.  A DOWNLOAD drops an
 *        image the page left staged first; while one is taken, or the last
 *        job's task has not ended yet, it is BLOCKED for BUSY. Refused on a
 *        session another screen runs: the caller starts no job while WI-FI is
 *        open.
 * @param job what to fetch
 * @param want the release to download; DOWNLOAD only
 * @param ch the channel CHECK looks at
 * @param gate the gate's answer
 * @return WA_GOT_NONE when the job runs; otherwise how it ended at once
 */
wa_got_t wifi_access_fetch_start(wa_job_t job, fw_version_t want,
                                 wa_channel_t ch, wa_why_t gate);

/**
 * @brief Once a frame, after wifi_access_tick(): @p abort as wa_fetch_tick()
 *        takes it.  Moves the job on, ends it, switches the radio off when it
 *        has ended, and starts the task that talks to GitHub once joined.
 * @param abort the reason to end the job, as wa_fetch_tick() takes it
 */
void wifi_access_fetch_tick(wa_why_t abort);

/**
 * @brief The job, as a copy.
 * @param out takes the job
 */
void wifi_access_fetch_get(wa_fetch_t *out);

/**
 * @brief Whether a home network is kept, and its name into @p ssid of @p n
 *         bytes (empty for none).
 * @param ssid takes the network's name; empty for none
 * @param n bytes in @p ssid
 * @return true when a home network is kept
 */
bool wifi_access_home(char *ssid, size_t n);

/**
 * @brief The card the page's file API reads (wa_api.h, wifi_files.c) and
 *        the bench its rules read, set once at start-up.  Before it, the
 *        API answers NO_CARD.
 * @param io     the card FILES uses; kept
 * @param bench  fills the bench as it stands; called from the page
 *               server's task; kept
 */
void wifi_access_set_files(const fm_io_t *io,
                           void (*bench)(fm_bench_t *out, void *ctx));

/**
 * @brief The log folders SETUP > SD CARD names, which the file API's rules
 *        keep (fm_rules.h), set before a session starts.  From any task.
 * @param user_logs  USER LOG FOLDER; NULL or "" is CARD_USER_LOGS
 * @param auto_logs  AUTO LOG FOLDER; NULL or "" is CARD_AUTO_LOGS
 */
void wifi_access_set_log_dirs(const char *user_logs, const char *auto_logs);

/**
 * @brief SETUP > SD CARD > Browser writes: what a browser needs to write
 *        to the card (wa_api_may_write()).  Once a frame; until the first
 *        call no browser write is taken.  From any task.
 * @param mode  the setting
 */
void wifi_access_set_writes(wa_writes_t mode);

/**
 * @brief The hold on WI-FI allowed browser writes (true), or BROWSER
 *        WRITES OFF ended them (false).  The radio going off ends them too.
 *        From any task.
 * @param open  allowed
 */
void wifi_access_writes_allow(bool open);

/**
 * @brief Whether the hold allowed browser writes, and how many the page
 *        made this radio session.  From any task.
 * @param written  takes the count; may be NULL
 * @return true while the hold's window is open
 */
bool wifi_access_writes(uint32_t *written);

/**
 * @brief The card's job lock FILES' task holds while it runs a request
 *        (panel_files.c), set once at start-up.  A browser write takes it
 *        around its card job, waiting at most 2000ms; without it set every
 *        write answers PANEL_BUSY.
 * @param take  takes the lock within @p wait_ms; false when it could not
 * @param give  gives it back; @p wrote when the card changed
 */
void wifi_access_set_card_lock(bool (*take)(uint32_t wait_ms),
                               void (*give)(bool wrote));

/**
 * @brief The words and colours the page's file manager classes a text
 *        with, the panel viewer's (wa_app.h), set once at start-up.
 *        Before it, the page shows a text uncoloured.
 * @param hl  kept; NULL for none
 */
void wifi_access_set_hl(const wa_hl_t *hl);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WIFI_ACCESS_H */
