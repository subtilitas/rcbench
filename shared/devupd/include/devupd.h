/**
 * @file devupd.h
 * @brief The update of a wireless device (the M5Stack Dial, a sensor node)
 *        from the image this panel carries, over Bluetooth LE: the
 *        transfer engine that drives bleota's BEGIN, HASH, chunks and END
 *        (devupd_t), and SETUP > UPDATES' row for a device (devupd_row()).
 *
 * The engine is told the link's state, each control write's answer and
 * each notification; devupd_step() gives the next control write or chunk.
 * A chunk names an offset and a length of the image, so the caller writes
 * the image's own bytes with no copy.  The engine keeps no more than the
 * device's credit window (BLEOTA_WINDOW) in flight, rewinds to the
 * device's next offset when a CREDIT names one behind what was sent (a
 * chunk was lost), asks for a CREDIT with POLL after DEVUPD_POLL_MS
 * without one, and ends the transfer after DEVUPD_POLLS polls in a row go
 * unanswered.  A refusal, a failed control write, a lost link or a step
 * that runs past its time ends the transfer; the device keeps the image
 * it runs.  A new devupd_start() runs from the first byte.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development
 * Framework).  Times are differences of a free-running 32-bit ms clock.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_DEVUPD_H
#define RCBENCH_DEVUPD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bleota.h"
#include "fw_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @name Step limits, ms
 *  @{ */
#define DEVUPD_LINK_MS    30000u /**< start to the link up: the hub's busy
                                      answers, a retry after 7,000ms and the
                                      discovery fit */
#define DEVUPD_ANSWER_MS   3000u /**< BEGIN's HASH written to WAIT */
#define DEVUPD_CONFIRM_MS (BLEOTA_CONFIRM_MS + 5000u) /**< WAIT to CREDIT */
#define DEVUPD_POLL_MS     1000u /**< no CREDIT this long: POLL */
#define DEVUPD_POLLS          3u /**< polls in a row unanswered: ended */
#define DEVUPD_END_MS     15000u /**< END written to DONE */
#define DEVUPD_RESTART_MS  5000u /**< DONE to the device's disconnect */
/** @} */

/** @brief esp_chip_id_t of the ESP32-S3, the Dial's chip:
 *         ESP_CHIP_ID_ESP32S3, 0x0009, in ESP-IDF's esp_app_format.h. */
#define DEVUPD_CHIP_ESP32S3 0x0009u
/** @brief The project names the device images carry. @{ */
#define DEVUPD_PROJECT_DIAL "rcbench-dial"
#define DEVUPD_PROJECT_NODE "rcbench-node" /**< both node roles */
/** @} */
/** @brief The devices' application slots, bytes: firmware/dial and
 *         firmware/node, partitions.csv. @{ */
#define DEVUPD_SLOT_DIAL 0x300000u
#define DEVUPD_SLOT_NODE 0x180000u /**< each node role */
/** @} */

/** @brief Control writes the engine holds before the caller takes them. */
#define DEVUPD_CTRL_Q 4u

/** @brief The image a transfer sends. */
typedef struct {
    const uint8_t *img;      /**< its bytes */
    uint32_t       size;     /**< their count */
    uint8_t        kind;     /**< BLEOTA_KIND_* */
    fw_version_t   version;  /**< its version */
    uint8_t        sha[32];  /**< the SHA-256 of all its bytes */
    bool           reinstall; /**< BLEOTA_F_REINSTALL: the device runs this
                                   version */
} devupd_image_t;

/** @brief Where a transfer stands. */
typedef enum {
    DEVUPD_IDLE = 0, /**< none since the start */
    DEVUPD_LINK,     /**< waiting for the link */
    DEVUPD_BEGIN,    /**< BEGIN and HASH sent: waiting for the answer */
    DEVUPD_CONFIRM,  /**< the device waits for its confirmation */
    DEVUPD_SEND,     /**< sending chunks */
    DEVUPD_END,      /**< END sent: waiting for DONE */
    DEVUPD_RESTART,  /**< DONE: the device restarts */
    DEVUPD_DONE,     /**< installed */
    DEVUPD_FAILED,   /**< ended without an install: err says why */
} devupd_phase_t;

/** @brief Why a transfer ended without an install. */
typedef enum {
    DEVUPD_E_NONE = 0, /**< none */
    DEVUPD_E_LINK,     /**< no link within DEVUPD_LINK_MS */
    DEVUPD_E_LOST,     /**< the link dropped */
    DEVUPD_E_TIMEOUT,  /**< the device stopped answering */
    DEVUPD_E_WRITE,    /**< a control write was refused or not answered */
    DEVUPD_E_DEVICE,   /**< the device refused or ended it: dev_code */
    DEVUPD_E_ABORTED,  /**< devupd_abort() */
    DEVUPD_E_IMAGE,    /**< devupd_start() was given no image */
} devupd_err_t;

/** @brief What devupd_step() gives. */
typedef enum {
    DEVUPD_OUT_NONE = 0, /**< nothing now */
    DEVUPD_OUT_CTRL,     /**< write ctrl[0..len-1] to CTRL with response */
    DEVUPD_OUT_CHUNK,    /**< write the header of off and the image's bytes
                              off to off + n - 1 to DATA without response */
} devupd_out_kind_t;

/** @brief One write for the caller. */
typedef struct {
    uint8_t  kind;                   /**< devupd_out_kind_t */
    uint8_t  len;                    /**< CTRL: its length */
    uint8_t  ctrl[BLEOTA_CTRL_MAX];  /**< CTRL: its bytes */
    uint32_t off;                    /**< CHUNK: the first byte's offset */
    uint16_t n;                      /**< CHUNK: the image bytes */
} devupd_out_t;

/** @brief A transfer.  The members are private but for the tests and the
 *         console line. */
typedef struct {
    devupd_image_t im;        /**< the image */
    uint8_t  phase;           /**< devupd_phase_t */
    uint8_t  err;             /**< devupd_err_t */
    uint8_t  dev_code;        /**< DEVUPD_E_DEVICE: BLEOTA_E_* */
    bool     up;              /**< the link stands */
    uint16_t chunk;           /**< image bytes a chunk carries */
    uint32_t t;               /**< the phase's start */
    uint32_t t_credit;        /**< the last CREDIT, or the last POLL */
    uint32_t sent;            /**< the next offset to send */
    uint32_t next;            /**< the device's next offset */
    uint32_t limit;           /**< the device's limit */
    uint16_t secs;            /**< CONFIRM: seconds the device gave */
    uint8_t  polls;           /**< polls in a row unanswered */
    bool     ctrl_busy;       /**< a control write waits for its answer */
    uint32_t t_ctrl;          /**< when it went */
    uint8_t  q[DEVUPD_CTRL_Q][BLEOTA_CTRL_MAX]; /**< control writes due */
    uint8_t  q_len[DEVUPD_CTRL_Q]; /**< their lengths */
    uint8_t  q_head;          /**< the first of them */
    uint8_t  q_n;             /**< their count */
    uint32_t chunks;          /**< chunks handed out */
    uint32_t rewinds;         /**< CREDITs that named an offset behind */
    uint32_t polls_sent;      /**< POLLs sent */
} devupd_t;

/** @brief Clear: idle.  @param e the transfer; NULL does nothing */
void devupd_init(devupd_t *e);

/**
 * @brief Start a transfer: it waits for the link (devupd_link()).
 * @param e   the transfer
 * @param im  the image, copied
 * @param now the clock
 * @return false while one runs (devupd_busy()); a NULL or empty image
 *         ends it at once with DEVUPD_E_IMAGE
 */
bool devupd_start(devupd_t *e, const devupd_image_t *im, uint32_t now);

/**
 * @brief The link to the device came up or dropped.
 * @param e   the transfer
 * @param up  it stands, subscribed to CTRL
 * @param mtu the ATT MTU it agreed, 0 for none
 * @param now the clock
 */
void devupd_link(devupd_t *e, bool up, uint16_t mtu, uint32_t now);

/**
 * @brief The answer to the last control write.
 * @param e      the transfer
 * @param status 0 for taken, else the host stack's error
 * @param now    the clock
 */
void devupd_written(devupd_t *e, uint16_t status, uint32_t now);

/**
 * @brief A notification of CTRL.
 * @param e   the transfer
 * @param b   its bytes
 * @param n   their count
 * @param now the clock
 */
void devupd_notify(devupd_t *e, const uint8_t *b, size_t n, uint32_t now);

/**
 * @brief Run out the times and give the next write.
 * @param e        the transfer
 * @param now      the clock
 * @param can_ctrl the caller can send a control write now
 * @param out      the write
 * @return true when @p out holds one
 */
bool devupd_step(devupd_t *e, uint32_t now, bool can_ctrl, devupd_out_t *out);

/**
 * @brief A chunk the host stack did not take: it is sent again.
 * @param e   the transfer
 * @param out the chunk devupd_step() gave
 */
void devupd_unsend(devupd_t *e, const devupd_out_t *out);

/**
 * @brief End a running transfer.  The caller ends the link, and the device
 *        drops what it took on the disconnect.
 * @param e   the transfer
 * @param now the clock
 */
void devupd_abort(devupd_t *e, uint32_t now);

/**
 * @brief Whether a transfer runs, from the start to the device's restart:
 *        the bench refuses ARM while it does.
 * @param e the transfer
 * @return true from DEVUPD_LINK to DEVUPD_RESTART
 */
bool devupd_busy(const devupd_t *e);

/**
 * @brief The share of the image the device took, 0 to 100.
 * @param e the transfer
 * @return the share
 */
unsigned devupd_percent(const devupd_t *e);

/* --------------------------------------------------------------- the row */

/** @brief What a device's row on UPDATES offers. */
typedef enum {
    DEVUPD_ROW_NO_IMAGE = 0, /**< this panel carries no image for it */
    DEVUPD_ROW_BAD_IMAGE,    /**< the carried image fails its check */
    DEVUPD_ROW_NOT_LINKED,   /**< the device is not connected */
    DEVUPD_ROW_ARMED,        /**< the bench is ARMED */
    DEVUPD_ROW_BUSY,         /**< another update runs */
    DEVUPD_ROW_READY,        /**< the hold is live: another version */
    DEVUPD_ROW_REINSTALL,    /**< the hold is live: the carried version */
    DEVUPD_ROW_UPDATING,     /**< the transfer runs */
} devupd_row_t;

/** @brief What the row is decided from. */
typedef struct {
    bool         have_image; /**< an image is carried */
    bool         image_ok;   /**< it passed its check */
    bool         linked;     /**< the device is connected */
    bool         known;      /**< it reported its version */
    fw_version_t running;    /**< that version */
    fw_version_t carried;    /**< the carried image's version */
    bool         armed;      /**< the bench is ARMED or an arm waits */
    bool         other;      /**< another update runs or waits */
    bool         busy;       /**< this device's transfer runs */
} devupd_row_in_t;

/**
 * @brief What the row offers, the first of: updating, no image, a failed
 *        image, ARMED, another update, not connected or no version, then
 *        REINSTALL for the carried version and READY for another.
 * @param in what the row is decided from
 * @return the row
 */
devupd_row_t devupd_row(const devupd_row_in_t *in);

#ifdef __cplusplus
}
#endif
#endif
