/**
 * @file panel_devimg.h
 * @brief The images of the C6 radio, the M5Stack Dial, the power node, the
 *        balance node and the display node, fetched when an update's hold
 *        asks for one: from GitHub over the panel's own download path, or
 *        from the SD card's BENCH/UPDATES, into PSRAM (pseudo-static
 *        random-access memory), checked, and handed to the existing senders (panel_c6.c,
 *        panel_devupd.c).  shared/devupd/include/devimg.h holds the names,
 *        the checks and the rules.
 *
 * Every call but panel_devimg_get() and panel_devimg_busy() is the frame's,
 * on app_main.  A download, a card read and the checks run one at a time;
 * no output changes state for any of them.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "devimg.h"
#include "fw_image.h"
#include "fw_sha256.h"
#include "upd_fetch.h"
#include "wa_fetch.h"

/**
 * @brief Once a frame, after upd_service() and before c6_service(): a
 *        download that ended, a card read or a check that ended, and the
 *        result line of a failed one on its UPDATES step.
 * @param armed ARMED, an arm waiting or an ARM queued
 */
void devimg_service(bool armed);

/**
 * @brief A hold on @p id's update whose image is not in memory: start its
 *        way from where devimg_src() says, and owe the update to it.  A
 *        refusal (the gate, another image on its way, no memory for the
 *        task) puts its reason in the alert band.
 * @param id    the image
 * @param armed ARMED, an arm waiting or an ARM queued
 * @return true when the download or the card read started
 */
bool panel_devimg_request(devimg_id_t id, bool armed);

/**
 * @brief @p id's image once it arrived and passed its checks.  It stays
 *        unchanged until the panel restarts.  Any task.
 * @param id   the image
 * @param p    takes its bytes, may be NULL
 * @param n    takes their count, may be NULL
 * @param info takes its header information, may be NULL
 * @param sha  takes the SHA-256 of every byte, may be NULL
 * @return false while it is not in memory
 */
bool panel_devimg_get(devimg_id_t id, const uint8_t **p, uint32_t *n,
                      fw_image_info_t *info, uint8_t sha[FW_SHA256_BYTES]);

/**
 * @brief The release signature that came with @p id's image, from GitHub
 *        or the card: its DER bytes, which the panel turns into the raw
 *        form a device checks itself (bleota protocol 2, relsig.h).
 *        Any of the frame's tasks may call it once the image is READY.
 * @param id  the image
 * @param der takes the bytes
 * @param n   takes their count
 * @return false while the image is not in memory, or it came without one
 */
bool panel_devimg_sig(devimg_id_t id, uint8_t der[FW_SIG_MAX], uint8_t *n);

/**
 * @brief The version @p id's image of this release carries: the C6's
 *        esp_hosted version, else the panel's own.
 * @param id the image
 * @return the version
 */
fw_version_t panel_devimg_want(devimg_id_t id);

/**
 * @brief Whether a download, a card read or the checks run for any image.
 *        Any task.
 * @return true while one runs
 */
bool panel_devimg_busy(void);

/**
 * @brief Whether a download, a card read or the checks run for @p id.
 * @param id the image
 * @return true while one runs
 */
bool panel_devimg_running(devimg_id_t id);

/**
 * @brief Whether @p id's row shows its update as under way
 *        (devimg_pending()): on its way, or its start still owed.
 * @param id the image
 * @return true while one runs or is owed
 */
bool panel_devimg_pending(devimg_id_t id);

/**
 * @brief Whether an image other than @p id is pending
 *        (panel_devimg_pending()).
 * @param id the image left out
 * @return true while another runs or is owed
 */
bool panel_devimg_pending_other(devimg_id_t id);

/**
 * @brief The row's view of @p id's image: where the next hold takes it
 *        from, the file, and what runs.
 * @param id  the image
 * @param out takes the view
 */
void panel_devimg_view(devimg_id_t id, upd_fetch_t *out);

/**
 * @brief The update the hold owes to @p id's image (devimg_owed_step()).
 * @param id   the image
 * @param open its gate is open: the hold would start it now
 * @param wait its gate is shut by the radio going off after the download
 * @return what the caller does
 */
devimg_owed_t panel_devimg_owed(devimg_id_t id, bool open, bool wait);

/**
 * @brief CANCEL UPDATE while a device's image is on its way or its update
 *        is owed: the owed update no longer starts (devimg_cancel()), and
 *        a download ends at the next frame (upd_service()).  A card read
 *        and the checks end by themselves; their image stays for the next
 *        hold.
 * @return true when an image was on its way or an update was owed: the
 *         cancel is taken
 */
bool panel_devimg_cancel(void);

/**
 * @brief The row that owed an update to @p id now sends another image
 *        (the power node's board changed): the owed update no longer
 *        starts (devimg_cancel()).  A download, a card read or the checks
 *        under way go on; an image that passes stays for the next hold.
 * @param id the image; one out of range does nothing
 */
void panel_devimg_forget(devimg_id_t id);

/**
 * @brief Taken once by upd_service(): CANCEL UPDATE asked to end the
 *        download.
 * @return true once after panel_devimg_cancel()
 */
bool panel_devimg_take_cancel(void);

/**
 * @brief The gate a device's download takes: the check's, less a device's
 *        image on its way (panel_update.c).
 * @param armed ARMED, an arm waiting or an ARM queued
 * @return WA_WHY_NONE when a download may start
 */
wa_why_t upd_dev_gate(bool armed);
