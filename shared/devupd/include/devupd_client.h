/**
 * @file devupd_client.h
 * @brief The panel's Bluetooth LE client of a device update: a bleuart
 *        machine bound to the device, with bleota's service as its profile,
 *        and the transfer engine (devupd.h) on it.
 *
 * The profile: bleota's service, CTRL as the STREAM (its notifications)
 * and the CMD (its writes, with response), DATA named so its value handle
 * is found (bleu_t.chr_val[DEVUPD_CHR_DATA]).  A start binds the device's
 * address and switches the machine on; once the link is up, subscribed
 * and its MTU agreed, the engine runs.  Its control writes go out through
 * the machine (bleu_send()), its chunks through the caller, which writes
 * the image's bytes to DATA without response (devupd_client_chunk()).  At
 * the end, installed or not, the machine is switched off and the link
 * ends.
 *
 * The caller runs it as one client of the hub (bleuart_hub.h): every
 * event the hub routes to its owner into devupd_client_event(), every
 * action of devupd_client_step() to the hub.
 *
 * Pure C, no ESP-IDF.  Every function of one client is called from one
 * task.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_DEVUPD_CLIENT_H
#define RCBENCH_DEVUPD_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#include "bleuart.h"
#include "devupd.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The profile entries. @{ */
#define DEVUPD_CHR_CTRL_STREAM 0u /**< CTRL, notifications */
#define DEVUPD_CHR_CTRL_CMD    1u /**< CTRL, writes */
#define DEVUPD_CHR_DATA        2u /**< DATA */
/** @} */

/** @brief The connection parameters an update asks for, 1.25ms units:
 *         15 to 30ms, latency 0, supervision 4,000ms.  Shorter than the
 *         IMU's 30 to 50ms so a connection event carries more chunks; the
 *         rate this gives is not measured. @{ */
#define DEVUPD_ITVL_MIN 12u /**< 15ms */
#define DEVUPD_ITVL_MAX 24u /**< 30ms */
/** @} */

/** @brief bleota's service as a bleuart profile. */
extern const bleu_profile_t devupd_profile;

/** @brief One update client. */
typedef struct {
    bleu_t       b;         /**< the machine */
    devupd_t     e;         /**< the transfer */
    bool         on;        /**< the machine is switched on */
    bool         up;        /**< the engine was told the link is up */
    bool         wr_wait;   /**< a control write's answer is due */
    bool         have_pend; /**< pend holds a chunk for the caller */
    devupd_out_t pend;      /**< that chunk */
} devupd_client_t;

/**
 * @brief Clear, off, with the owner @p owner.
 * @param c     the client
 * @param owner the hub's owner of its tokens
 * @return false for NULL or an owner out of range
 */
bool devupd_client_init(devupd_client_t *c, uint8_t owner);

/**
 * @brief Start an update of the device at @p addr.
 * @param c    the client
 * @param addr the device's address
 * @param im   the image
 * @param now  the clock
 * @return false while one runs, or for NULL
 */
bool devupd_client_start(devupd_client_t *c, const bleu_addr_t *addr,
                         const devupd_image_t *im, uint32_t now);

/**
 * @brief End a running update; the link ends.
 * @param c   the client
 * @param now the clock
 */
void devupd_client_abort(devupd_client_t *c, uint32_t now);

/**
 * @brief Take one event the hub routed to this client.
 * @param c   the client
 * @param e   the event
 * @param now the clock
 */
void devupd_client_event(devupd_client_t *c, const bleu_evt_t *e,
                         uint32_t now);

/**
 * @brief Run the engine, send its control writes, and give the machine's
 *        next action for the hub.
 * @param c   the client
 * @param now the clock
 * @param out the action
 * @return true when @p out holds one
 */
bool devupd_client_step(devupd_client_t *c, uint32_t now, bleu_act_t *out);

/**
 * @brief The next chunk to write to DATA without response.
 * @param c      the client
 * @param now    the clock
 * @param out    the chunk: its offset and length in the image
 * @param conn   the connection
 * @param handle DATA's value handle
 * @return true when @p out holds one
 */
bool devupd_client_chunk(devupd_client_t *c, uint32_t now, devupd_out_t *out,
                         uint16_t *conn, uint16_t *handle);

/**
 * @brief A chunk the host stack did not take: sent again later.
 * @param c   the client
 * @param out the chunk
 */
void devupd_client_unsend(devupd_client_t *c, const devupd_out_t *out);

#ifdef __cplusplus
}
#endif
#endif
