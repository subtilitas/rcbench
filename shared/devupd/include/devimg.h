/**
 * @file devimg.h
 * @brief The images the panel sends to the C6 radio, the M5Stack Dial, the
 *        power node and the balance node, fetched when an update asks for
 *        one: their names in a release, the SD card's fallback, their checks
 *        and the state of one image from the hold to the update's start.
 *
 * The panel image carries none of them.  Each is a file of the release the
 * panel runs (tag vMAJOR.MINOR.PATCH of RCBENCH_VERSION_STRING):
 *
 *   C6 radio      rcbench-c6-ESPHOSTED.bin, ESPHOSTED its esp_hosted version
 *   Dial          rcbench-dial-VERSION.bin
 *   power node    rcbench-node-pwr-VERSION.bin
 *   balance node  rcbench-node-bal-VERSION.bin
 *
 * An update's hold takes its image from one of three places
 * (devimg_src()): the memory, when an earlier hold fetched it; GitHub,
 * through the panel's own download path (wa_run_device()), checked against
 * the release's SHA256SUMS; or the SD card, the same file name in
 * DEVIMG_DIR.  A download that failed sends the next hold to the card; a
 * card read that failed sends the next hold to GitHub again.  Without a
 * home network every hold reads the card.  Wherever it came from, the
 * image passes devimg_verify() before a sender gets it.
 *
 * A failure is named by a code of a letter and two digits
 * (devimg_failed()): D and the download's wa_got_t, S and the card's
 * devimg_card_t, V and devimg_verify()'s devimg_err_t.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework);
 * the panel's end is firmware/panel/main/panel_devimg.c.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_DEVIMG_H
#define RCBENCH_DEVIMG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fw_image.h"
#include "fw_sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The images, in UPDATES' order. */
typedef enum {
    DEVIMG_C6 = 0, /**< the board's ESP32-C6 radio, over SDIO */
    DEVIMG_DIAL,   /**< the M5Stack Dial, over Bluetooth LE */
    DEVIMG_PNODE,  /**< the power node, over Bluetooth LE */
    DEVIMG_BAL,    /**< the balance node, over Bluetooth LE */
    DEVIMG_COUNT,  /**< the count */
} devimg_id_t;

/** @brief The SD card's folder for the images, from the card's root. */
#define DEVIMG_DIR      "BENCH/UPDATES"
/** @brief The checksums file the card's folder may hold beside them. */
#define DEVIMG_SUMS     "SHA256SUMS"
/** @brief Bytes of a file name, its terminator included. */
#define DEVIMG_NAME_MAX 40u
/** @brief Bytes of a card path, DEVIMG_DIR "/" and the name. */
#define DEVIMG_PATH_MAX (sizeof(DEVIMG_DIR) + DEVIMG_NAME_MAX)
/** @brief Bytes of a failure's code: a letter, two digits, a terminator. */
#define DEVIMG_CODE_MAX 4u
/** @brief From the image's arrival, how long the update it was fetched
 *         for may wait for its gate to open, ms: the radio going off after
 *         the download and its time request. */
#define DEVIMG_OWED_MS  20000u

/**
 * @brief The image's file name in release @p v's assets.
 * @param id  the image
 * @param v   the C6's esp_hosted version for DEVIMG_C6, else the panel's
 * @param out takes the name
 * @param n   bytes of @p out, DEVIMG_NAME_MAX is enough
 * @return false for an @p id out of range or a name that does not fit
 */
bool devimg_name(devimg_id_t id, fw_version_t v, char *out, size_t n);

/**
 * @brief The image's path on the card, DEVIMG_DIR "/" devimg_name().
 * @param id  the image
 * @param v   as devimg_name()
 * @param out takes the path
 * @param n   bytes of @p out, DEVIMG_PATH_MAX is enough
 * @return false as devimg_name()
 */
bool devimg_card_path(devimg_id_t id, fw_version_t v, char *out, size_t n);

/**
 * @brief The most bytes the image may have: its device's slot.
 * @param id the image
 * @return the slot's bytes, 0 for an @p id out of range
 */
uint32_t devimg_slot(devimg_id_t id);

/** @brief What devimg_verify() refuses. */
typedef enum {
    DEVIMG_OK = 0,
    DEVIMG_E_SIZE,    /**< empty, or over the device's slot           */
    DEVIMG_E_HASH,    /**< its SHA-256 is not the one given            */
    DEVIMG_E_CHECK,   /**< fw_image_check_as() refused it: see why     */
    DEVIMG_E_ROLE,    /**< the role in its head names another device   */
    DEVIMG_E_VERSION, /**< it is another version than the one wanted   */
    DEVIMG_E_ID,      /**< an image number out of range                */
} devimg_err_t;

/**
 * @brief Every check an image passes before a sender gets it.
 *
 * Size against the slot; the SHA-256 of every byte against @p sha_want when
 * one is given; the ESP-IDF application checks for the device's chip and
 * project (fw_c6_image_check() for the C6, fw_image_check_as() for the
 * others), which hold the image's own appended SHA-256; for a Dial or a
 * node the role in its head (bleota_head_check()); and its version against
 * @p want.
 *
 * @param id       the image
 * @param img      its bytes
 * @param n        their count
 * @param want     the version it must carry
 * @param sha_want the SHA-256 it must have, NULL for none to check
 * @param info     takes its header information, may be NULL
 * @param sha      takes the SHA-256 of every byte, may be NULL
 * @param why      takes fw_image_check_as()'s refusal for DEVIMG_E_CHECK,
 *                 may be NULL
 * @return DEVIMG_OK, or the first check that refused it
 */
devimg_err_t devimg_verify(devimg_id_t id, const uint8_t *img, uint32_t n,
                           fw_version_t want, const uint8_t *sha_want,
                           fw_image_info_t *info,
                           uint8_t sha[FW_SHA256_BYTES], fw_err_t *why);

/** @brief Why a card read gave no image. */
typedef enum {
    DEVIMG_CARD_OK = 0,
    DEVIMG_CARD_NONE,    /**< no card is mounted                       */
    DEVIMG_CARD_NO_FILE, /**< the file is not in DEVIMG_DIR            */
    DEVIMG_CARD_READ,    /**< a read failed or ended short             */
    DEVIMG_CARD_SIZE,    /**< empty, or over the device's slot         */
    DEVIMG_CARD_MEMORY,  /**< no memory for it                         */
    DEVIMG_CARD_SUMS,    /**< DEVIMG_SUMS is there and out of form, or
                              names the file twice                     */
} devimg_card_t;

/** @brief Where the next hold takes the image from. */
typedef enum {
    DEVIMG_SRC_NET = 0, /**< GitHub, the release the panel runs */
    DEVIMG_SRC_CARD,    /**< the SD card's DEVIMG_DIR           */
    DEVIMG_SRC_RAM,     /**< the memory: an earlier hold got it */
} devimg_src_t;

/** @brief Where one image stands. */
typedef enum {
    DEVIMG_IDLE = 0, /**< none in memory, none on the way     */
    DEVIMG_FETCH,    /**< the download runs                   */
    DEVIMG_CARD,     /**< the card read and the checks run    */
    DEVIMG_VERIFY,   /**< a downloaded file's checks run      */
    DEVIMG_READY,    /**< checked, in memory, kept to the restart */
} devimg_st_t;

/** @brief What a waiting update does next (devimg_owed_step()). */
typedef enum {
    DEVIMG_OWED_NONE = 0, /**< no update waits for this image       */
    DEVIMG_OWED_WAIT,     /**< the image or the gate is still owed  */
    DEVIMG_OWED_FIRE,     /**< start the update now                 */
    DEVIMG_OWED_DROP,     /**< the gate stayed shut: the update does not
                               start; the image stays for the next hold */
} devimg_owed_t;

/** @brief One image: where it stands, its last failure, the update owed. */
typedef struct {
    devimg_st_t st;          /**< where it stands                       */
    bool        from_card;   /**< the attempt under way, or the last,
                                  reads the card                        */
    bool        net_failed;  /**< the last attempt was a download, and
                                  it failed: the next hold reads the card */
    bool        owed;        /**< the hold that started it waits for it
                                  to start the update                   */
    uint32_t    owed_ms;     /**< READY with owed: when it arrived, ms  */
    char        code[DEVIMG_CODE_MAX]; /**< the last failure's code since
                                  the last start, "" for none           */
} devimg_slot_t;

/**
 * @brief No image, no failure.
 * @param s the image's state
 */
void devimg_init(devimg_slot_t *s);

/**
 * @brief Where the next hold takes the image from.
 * @param s        the image's state
 * @param have_net a home network is kept
 * @return RAM when READY; CARD without a home network or after a failed
 *         download; NET otherwise
 */
devimg_src_t devimg_src(const devimg_slot_t *s, bool have_net);

/**
 * @brief Whether a download, a card read or the checks run.
 * @param s the image's state
 * @return true for FETCH, CARD and VERIFY
 */
bool devimg_busy(const devimg_slot_t *s);

/**
 * @brief A hold starts the image's way from @p src: FETCH for NET, CARD for
 *        CARD; the update is owed.  The last failure's code clears.
 *        Nothing changes while one runs, or for RAM.
 * @param s   the image's state
 * @param src devimg_src()'s answer
 * @return true when the state moved
 */
bool devimg_begin(devimg_slot_t *s, devimg_src_t src);

/**
 * @brief The download ended with the file in memory: its checks follow.
 * @param s the image's state; nothing changes outside FETCH
 */
void devimg_fetched(devimg_slot_t *s);

/**
 * @brief The image arrived and passed its checks at @p now_ms.
 * @param s      the image's state; nothing changes outside CARD and VERIFY
 * @param now_ms the clock, ms
 */
void devimg_ready(devimg_slot_t *s, uint32_t now_ms);

/**
 * @brief The attempt under way failed: no image, the update not owed, the
 *        code @p kind and @p n, and the next hold's place: the card after a
 *        download, GitHub after a card read.
 * @param s    the image's state; nothing changes unless one runs
 * @param kind 'D' download, 'S' card, 'V' the checks
 * @param n    the reason's number, 0 to 99
 */
void devimg_failed(devimg_slot_t *s, char kind, unsigned n);

/**
 * @brief The update owed once the image is READY: start it when its gate
 *        is open, wait while @p wait holds and DEVIMG_OWED_MS has not
 *        passed since the arrival, else drop it.  FIRE and DROP clear owed.
 * @param s      the image's state
 * @param open   the update's gate is open: its hold would start it now
 * @param wait   the gate is shut for a reason that clears by itself (the
 *               radio going off after the download)
 * @param now_ms the clock, ms
 * @return what the caller does
 */
devimg_owed_t devimg_owed_step(devimg_slot_t *s, bool open, bool wait,
                               uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_DEVIMG_H */
