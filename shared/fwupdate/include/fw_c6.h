/**
 * @file fw_c6.h
 * @brief The update of the board's ESP32-C6 radio from the image this panel
 *        carries: the image's check, SETUP > UPDATES' C6 RADIO row and its
 *        gate, and the steps of the update over SDIO (Secure Digital
 *        input/output).
 *
 * The C6 runs esp_hosted's co-processor firmware ("network_adapter").  The
 * panel carries the image the release builds from the same esp_hosted
 * version as the panel's host side (2.13.0), and sends it through
 * esp_hosted's OTA (over-the-air) calls: begin, write in chunks, end, and on
 * a C6 that runs 2.6.0 or later, activate.  The C6 writes the slot it does
 * not run from; its end call checks the image (the appended SHA-256); the
 * slot it starts from changes at the activate call (2.6.0 and later) or at
 * the end call (older firmware).  A refusal, an error or a step that is not
 * answered before its limit ends the update with nothing more sent, and the
 * C6 keeps the firmware it runs.
 *
 * The driver here issues one step at a time and is told each step's
 * answer; the panel runs each step on a task of its own
 * (firmware/panel/main/panel_c6.c) and times it here.  An answer that
 * arrives after its step's limit, or for an update that has ended, is
 * ignored.  Times are differences of a free-running 32-bit ms clock.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FW_C6_H
#define RCBENCH_FW_C6_H

#include <stdbool.h>
#include <stdint.h>

#include "fw_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The project name esp_hosted's co-processor build carries. */
#define FW_C6_PROJECT      "network_adapter"
/** @brief One application slot of the C6, ota_0 and ota_1 of esp_hosted
 *         2.13.0's slave/partitions.esp32c6.csv: 1920KiB. */
#define FW_C6_SLOT_BYTES   (1920u * 1024u)
/** @brief One esp_hosted_slave_ota_write(), bytes: the chunk esp_hosted's
 *         own examples send. */
#define FW_C6_CHUNK        1400u
/** @brief One WRITE step: 4 chunks. */
#define FW_C6_RUN_BYTES    (4u * FW_C6_CHUNK)

/** @name Step limits, ms
 *  Each is above esp_hosted's own response time-out for the calls of the
 *  step (rpc_wrap.c: 15s for begin, 5s for the others), so the call's own
 *  error normally ends a step first.  LINK covers c6_link_up() on a C6 that
 *  does not answer, about 25s, and its version request, 1s.
 *  @{ */
#define FW_C6_LINK_MS      30000u  /**< LINK: link-up and version       */
#define FW_C6_BEGIN_MS     20000u  /**< BEGIN: the C6 erases its slot    */
#define FW_C6_WRITE_MS     10000u  /**< WRITE: one step of 4 chunks      */
#define FW_C6_END_MS       10000u  /**< END and CLOSE: the C6's check    */
#define FW_C6_ACTIVATE_MS  10000u  /**< ACTIVATE                         */
/** @} */

/** @brief The first C6 firmware with the activate call: 2.6.0. */
#define FW_C6_ACTIVATE_MAJOR 2u
#define FW_C6_ACTIVATE_MINOR 6u  /**< see FW_C6_ACTIVATE_MAJOR */

/* ------------------------------------------------------------- the image */

/**
 * @brief Checks the carried C6 image: an ESP32-C6 application of project
 *        FW_C6_PROJECT, MAJOR.MINOR.PATCH version, at most FW_C6_SLOT_BYTES,
 *        whose segments add up and whose appended SHA-256 matches.
 *
 * @param img   the image; NULL with @p bytes 0 is no image
 * @param bytes its length
 * @param out   what the image says about itself, written on FW_OK
 * @return FW_OK or the first check that failed (fw_image_check_as())
 */
fw_err_t fw_c6_image_check(const uint8_t *img, uint32_t bytes,
                           fw_image_info_t *out);

/* ---------------------------------------------------------- UPDATES' row */

/** @brief What the C6 RADIO row shows. */
typedef enum {
    FW_C6_ROW_READY = 0,   /**< the hold is live                          */
    FW_C6_ROW_NO_IMAGE,    /**< this build carries no C6 image            */
    FW_C6_ROW_BAD_IMAGE,   /**< the carried image fails fw_c6_image_check() */
    FW_C6_ROW_UPDATING,    /**< an update runs                            */
    FW_C6_ROW_LINK_FAILED, /**< the link to the C6 failed since start-up  */
    FW_C6_ROW_SAME,        /**< the C6 reports the carried version        */
    FW_C6_ROW_BLOCKED,     /**< the bench does not allow it now: see why  */
} fw_c6_row_t;

/** @brief Why the hold is grey while the row is FW_C6_ROW_BLOCKED. */
typedef enum {
    FW_C6_WHY_NONE = 0,
    FW_C6_WHY_ARMED,       /**< ARMED, or an arm waiting                  */
    FW_C6_WHY_SUPPLY,      /**< the supply's output on or coming          */
    FW_C6_WHY_RUN,         /**< a log run is open                         */
    FW_C6_WHY_DRAWING,     /**< the board drawing is being fetched        */
    FW_C6_WHY_IOMCU,       /**< the IOmcu's update runs                   */
    FW_C6_WHY_INSTALL,     /**< the panel's own download or install runs  */
    FW_C6_WHY_RADIO,       /**< Wi-Fi is on, or a session asks for it     */
    FW_C6_WHY_ENDING,      /**< a step of an ended update is still in a call */
} fw_c6_why_t;

/** @brief What the application knows for the row, once a frame. */
typedef struct {
    bool         have_image;  /**< this build carries a C6 image          */
    bool         image_ok;    /**< and it passed fw_c6_image_check()      */
    fw_version_t carried;     /**< its version                            */
    bool         known;       /**< the C6 reported its version            */
    fw_version_t running;     /**< that version                           */
    bool         link_failed; /**< c6_link_failed()                       */
    bool         updating;    /**< fw_c6_ota_active()                     */
    bool         ending;      /**< the step task is still in a call       */
    bool         armed;       /**< ARMED or an arm waiting                */
    bool         supply;      /**< the supply's output on or coming       */
    bool         run;         /**< a log run open                         */
    bool         drawing;     /**< the board drawing's fetch              */
    bool         iomcu;       /**< the IOmcu's update                     */
    bool         installing;  /**< the panel's download or install        */
    bool         radio_up;    /**< !wifi_access_down()                    */
} fw_c6_feed_t;

/**
 * @brief The row's state, in this order: no image, a bad image, an update
 *        running, a failed link, the carried version reported, then the
 *        bench's reasons in fw_c6_why_t's order; else READY.
 *
 * A C6 that reported no version is offered the update: the shipped
 * firmware does not answer the version request.
 *
 * @param f   the feed; NULL is FW_C6_ROW_NO_IMAGE
 * @param why receives the reason for FW_C6_ROW_BLOCKED, else FW_C6_WHY_NONE;
 *            may be NULL
 * @return the row's state
 */
fw_c6_row_t fw_c6_row(const fw_c6_feed_t *f, fw_c6_why_t *why);

/* ------------------------------------------------------------ the update */

/** @brief A step the panel runs on the C6. */
typedef enum {
    FW_C6_ACT_NONE = 0,
    FW_C6_ACT_LINK,      /**< c6_link_up(), then c6_link_version()        */
    FW_C6_ACT_BEGIN,     /**< esp_hosted_slave_ota_begin()                */
    FW_C6_ACT_WRITE,     /**< esp_hosted_slave_ota_write() over offset, len,
                              FW_C6_CHUNK at a time                       */
    FW_C6_ACT_END,       /**< esp_hosted_slave_ota_end(): the C6's check   */
    FW_C6_ACT_ACTIVATE,  /**< esp_hosted_slave_ota_activate()              */
    FW_C6_ACT_CLOSE,     /**< esp_hosted_slave_ota_end() after a failed
                              write: releases the C6's OTA handle; an
                              incomplete image fails its check            */
} fw_c6_act_t;

/** @brief One step to run, from fw_c6_ota_next(). */
typedef struct {
    fw_c6_act_t act;      /**< what to run                                */
    uint32_t    gen;      /**< the update it belongs to: its answer's gen */
    uint32_t    offset;   /**< WRITE: first byte of the image             */
    uint32_t    len;      /**< WRITE: bytes, at most FW_C6_RUN_BYTES      */
} fw_c6_step_t;

/** @brief Where an update is. */
typedef enum {
    FW_C6_PH_IDLE = 0,
    FW_C6_PH_LINK,
    FW_C6_PH_BEGIN,
    FW_C6_PH_WRITE,
    FW_C6_PH_END,
    FW_C6_PH_ACTIVATE,
    FW_C6_PH_CLOSE,
    FW_C6_PH_ENDED,
} fw_c6_phase_t;

/** @brief How an update ended. */
typedef enum {
    FW_C6_RES_NONE = 0,
    FW_C6_RES_UPDATED,   /**< the C6 took the image; the panel restarts   */
    FW_C6_RES_SAME,      /**< the C6 reports the carried version: nothing sent */
    FW_C6_RES_NO_LINK,   /**< the C6 did not answer at link-up            */
    FW_C6_RES_REFUSED,   /**< the begin call failed: no OTA over SDIO      */
    FW_C6_RES_NO_ANSWER, /**< a step passed its limit                      */
    FW_C6_RES_WRITE,     /**< a write failed                               */
    FW_C6_RES_VERIFY,    /**< the end call failed: the C6's image check    */
    FW_C6_RES_ACTIVATE,  /**< the activate call failed                     */
} fw_c6_res_t;

/** @brief The update's state. */
typedef struct {
    fw_c6_phase_t phase;      /**< where it is                             */
    fw_c6_res_t   result;     /**< once ENDED                              */
    int32_t       error;      /**< the failing call's error, 0 for none    */
    fw_c6_phase_t failed_in;  /**< the phase that ended it, for the log    */
    uint32_t      gen;        /**< counts the updates started              */
    uint32_t      total;      /**< the image's bytes                       */
    uint32_t      done;       /**< bytes the C6 took                       */
    fw_version_t  carried;    /**< the image's version                     */
    bool          known;      /**< LINK: the C6 reported its version       */
    fw_version_t  running;    /**< LINK: that version                      */
    bool          issued;     /**< the phase's step is out                 */
    uint32_t      since_ms;   /**< when it went out                        */
    uint32_t      step_len;   /**< WRITE: its length                       */
    fw_c6_res_t   close_for;  /**< CLOSE: the result after it              */
} fw_c6_ota_t;

/**
 * @brief Nothing started.
 * @param o the update
 */
void fw_c6_ota_init(fw_c6_ota_t *o);

/**
 * @brief Starts an update of an image of @p total bytes, version
 *        @p carried, at @p now_ms: the LINK step is next.
 * @param o       the update
 * @param total   the image's bytes, 1 to FW_C6_SLOT_BYTES
 * @param carried its version
 * @param now_ms  now
 * @return false, and nothing changes, while one runs or for @p total out
 *         of range
 */
bool fw_c6_ota_start(fw_c6_ota_t *o, uint32_t total, fw_version_t carried,
                     uint32_t now_ms);

/**
 * @brief The next step to run, once: the phase's step is marked out at
 *        @p now_ms and its limit runs from there.
 * @param o      the update
 * @param now_ms now
 * @param step   receives the step
 * @return true when a step is to be run now
 */
bool fw_c6_ota_next(fw_c6_ota_t *o, uint32_t now_ms, fw_c6_step_t *step);

/**
 * @brief The answer to the step out.
 * @param o       the update
 * @param gen     the step's gen; another update's is ignored
 * @param err     0 for success, else the call's error
 * @param known   LINK: the C6 reported its version
 * @param running LINK: that version
 * @return true when the answer was taken
 */
bool fw_c6_ota_answer(fw_c6_ota_t *o, uint32_t gen, int32_t err, bool known,
                      fw_version_t running);

/**
 * @brief Time passed: a step out for its phase's limit or longer ends the
 *        update FW_C6_RES_NO_ANSWER (CLOSE: the result it closes for).
 * @param o      the update
 * @param now_ms now
 */
void fw_c6_ota_tick(fw_c6_ota_t *o, uint32_t now_ms);

/**
 * @brief A phase's step limit.
 * @param ph the phase
 * @return the limit in ms; 0 for IDLE and ENDED
 */
uint32_t fw_c6_ota_limit_ms(fw_c6_phase_t ph);

/**
 * @brief Whether an update runs: from the start to its end.
 * @param o the update; NULL is false
 * @return true between fw_c6_ota_start() and ENDED
 */
bool fw_c6_ota_active(const fw_c6_ota_t *o);

/**
 * @brief The share of the image the C6 took.
 * @param o the update; NULL is 0
 * @return 0 to 100
 */
unsigned fw_c6_ota_percent(const fw_c6_ota_t *o);

/**
 * @brief Whether the C6 running @p v has the activate call (2.6.0 or later).
 * @param known whether the C6 reported a version; false is no
 * @param v     the version
 * @return true from FW_C6_ACTIVATE_MAJOR.FW_C6_ACTIVATE_MINOR.0 on
 */
bool fw_c6_has_activate(bool known, fw_version_t v);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_C6_H */
