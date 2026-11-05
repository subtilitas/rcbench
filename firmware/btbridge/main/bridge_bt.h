/**
 * @file bridge_bt.h
 * @brief The bridge's Bluetooth side on Bluedroid Classic: the GAP
 *        (generic access profile) inquiry and name reads, SPP (serial port
 *        profile) discovery and the initiator's link, and the bytes the
 *        link carries.
 *
 * The stack's callbacks run in its own task.  They copy each event into
 * the queue given to bridge_bt_start() and touch nothing else, except a
 * link's received bytes: those go to the UART at once while forwarding is
 * on.  The bridge task takes the events with bridge_bt_event() and runs
 * the emulator's actions with bridge_bt_perform(); the emulator is called
 * from that task only.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "hc05emu.h"

/** @brief What a stack callback reports. */
typedef enum {
    BT_EV_FOUND = 0,     /**< an inquiry result */
    BT_EV_DISC_STARTED,  /**< the inquiry began */
    BT_EV_DISC_STOPPED,  /**< the inquiry ended or was cancelled */
    BT_EV_NAME,          /**< a remote name read ended */
    BT_EV_SDP,           /**< the SPP service search ended */
    BT_EV_CL_INIT,       /**< a connect began, its handle given */
    BT_EV_OPEN,          /**< a connect ended */
    BT_EV_CLOSE,         /**< a link closed, or a connect failed */
    BT_EV_CONG,          /**< the link's congestion changed */
} bridge_ev_kind_t;

/** @brief One stack event, copied out of its callback. */
typedef struct {
    bridge_ev_kind_t kind;           /**< what */
    bool     ok;                     /**< NAME, SDP, CL_INIT, OPEN: success */
    uint8_t  addr[HC05EMU_ADDR_LEN]; /**< FOUND, NAME, OPEN: the device */
    uint32_t cod;                    /**< FOUND: class of device */
    int16_t  rssi;                   /**< FOUND: dBm, INT16_MAX unknown */
    bool     named;                  /**< FOUND: name holds one */
    char     name[HC05EMU_NAME_MAX]; /**< FOUND, NAME: the name, cut */
    uint8_t  scn;                    /**< SDP: the SPP channel found */
    uint32_t handle;                 /**< CL_INIT, OPEN, CLOSE, CONG */
    bool     cong;                   /**< CONG: congested */
} bridge_ev_t;

/**
 * @brief Start the controller in BR/EDR mode, Bluedroid with legacy PIN
 *        pairing, GAP and SPP, the bridge neither discoverable nor
 *        connectable.
 * @param q    The queue the callbacks post bridge_ev_t into.
 * @param uart The UART a link's received bytes are written to.
 * @return true when every part started.
 */
bool bridge_bt_start(QueueHandle_t q, uart_port_t uart);

/**
 * @brief Run one radio action of the emulator: INQ, NAME, PAIR, CONNECT
 *        or STOP.  A call that the stack refuses at once is reported to
 *        the emulator as failed.
 * @param e      The emulator.
 * @param a      The action.
 * @param now_ms The clock.
 */
void bridge_bt_perform(hc05emu_t *e, const hc05emu_act_t *a, uint32_t now_ms);

/**
 * @brief Take one stack event in the bridge task.
 * @param e      The emulator.
 * @param ev     The event.
 * @param now_ms The clock.
 */
void bridge_bt_event(hc05emu_t *e, const bridge_ev_t *ev, uint32_t now_ms);

/**
 * @brief Send UART bytes over the open link.  While the link is congested
 *        they wait in a 512-byte buffer; bytes beyond it, and bytes with no
 *        link, are dropped.
 * @param b The bytes.
 * @param n Their count.
 */
void bridge_bt_write(const uint8_t *b, size_t n);

/** @brief Let a link's received bytes reach the UART, or hold them back.
 *  @param on true in data mode with the link open. */
void bridge_bt_forward(bool on);
