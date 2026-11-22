/**
 * @file upd_fetch.h
 * @brief How UPDATES shows where the C6 radio's, the Dial's and the sensor
 *        nodes' update takes its image from (devimg.h): the hold's label
 *        for the SD card, the label while the image is on its way, the
 *        card's line that names the file, and the line of a failed
 *        attempt with its code.
 *
 * The C6 RADIO row (upd_c6.h) and each device's row (upd_dev.h) carry one
 * upd_fetch_t, which the panel fills each frame.  All of it zero is a hold
 * that downloads: the label stays the row's own.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_UPD_FETCH_H
#define RCBENCH_UPD_FETCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "devimg.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What runs for the row's image now. */
typedef enum {
    UPD_FETCH_IDLE = 0, /**< nothing                                */
    UPD_FETCH_NET,      /**< the download from GitHub               */
    UPD_FETCH_CHECK,    /**< the downloaded file's checks           */
    UPD_FETCH_CARD,     /**< the SD card's read and the checks      */
} upd_fetch_run_t;

/** @brief Where a row's image comes from, and what runs for it. */
typedef struct {
    uint8_t src;      /**< devimg_src_t: where the next hold takes it  */
    uint8_t run;      /**< upd_fetch_run_t                              */
    uint8_t percent;  /**< UPD_FETCH_NET: the share received, 0-100    */
    /** @brief The file: its name in the release for DEVIMG_SRC_NET and
     *         RAM, its path on the card for DEVIMG_SRC_CARD; empty for
     *         none. */
    char    file[DEVIMG_PATH_MAX];
} upd_fetch_t;

/** @brief The three kinds of a failed attempt, as devimg_failed() codes
 *         them. */
typedef enum {
    UPD_FETCH_FAIL_NET = 0, /**< D: the download                    */
    UPD_FETCH_FAIL_CARD,    /**< S: the card's read                 */
    UPD_FETCH_FAIL_CHECK,   /**< V: the image's checks              */
} upd_fetch_fail_t;

/**
 * @brief The control's label from the image's side: while a download, a
 *        card read or the checks run, what runs; for a hold that reads the
 *        card, that it does.
 * @param f   the row's image
 * @param out takes the label
 * @param n   bytes of @p out
 * @return true when @p out holds a label; false: the row's own label
 *         stands
 */
bool upd_fetch_label(const upd_fetch_t *f, char *out, size_t n);

/**
 * @brief The card's line naming the file the hold takes, or the one in
 *        memory.
 * @param f   the row's image
 * @param out takes the line, empty for none
 * @param n   bytes of @p out
 */
void upd_fetch_line(const upd_fetch_t *f, char *out, size_t n);

/**
 * @brief A failed attempt's line, as the card shows it on the step:
 *        "Download failed (D09): No connection to GitHub."
 * @param kind   which part failed
 * @param code   devimg_slot_t.code
 * @param reason the reason's text
 * @param out    takes the line
 * @param n      bytes of @p out
 */
void upd_fetch_fail_line(upd_fetch_fail_t kind, const char *code,
                         const char *reason, char *out, size_t n);

/**
 * @brief The reason's text of a card read that gave no image.
 * @param e    the card's answer
 * @param path the file's path on the card
 * @param out  takes the text
 * @param n    bytes of @p out
 */
void upd_fetch_card_text(devimg_card_t e, const char *path, char *out,
                         size_t n);

/**
 * @brief The reason's text of an image its checks refused.
 * @param e    devimg_verify()'s answer
 * @param have the version the image carries, when e is DEVIMG_E_VERSION
 * @param want the version the release has
 * @param out  takes the text
 * @param n    bytes of @p out
 */
void upd_fetch_check_text(devimg_err_t e, const char *have, const char *want,
                          char *out, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_UPD_FETCH_H */
