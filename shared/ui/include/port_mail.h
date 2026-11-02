/*
 * The programming ports between the panel's control task and its UI
 * task: the PROG page as the screens draw it (port_view.h), and the
 * operator's asks for a port's PIN or FUNC on the way back
 * (prog_link_ask()).
 *
 * The view crosses in two words under a generation count (a sequence
 * lock): the control task makes the count odd, stores both words and
 * makes it even again; the UI task takes a pair read between two equal,
 * even counts and keeps its last view when 8 tries in a row are torn.  A
 * frame never draws one read's pins with another's functions.
 *
 * An ask crosses as one word: its number (1 to 255), the link epoch of
 * the view it was made on, the port, the kind and the value.  The control
 * task hands a new number to prog_link_ask() once, and only in the epoch
 * it was made in.  Each link loss starts a new epoch and makes the last
 * ask LOST, whether it was handed on or still waited in the word, so an
 * ask made for one IOmcu is never written to the one that answers after
 * the loss.  How the ask stands crosses back in one word under the same
 * number.  The number wraps from 255 to 1, so a post first sets that word
 * to WAITING under its number: a final state the word kept from the
 * number's last use never stands for the new ask.
 *
 * One writer per side: port_mail_publish() and port_mail_lost() on the
 * control task, port_mail_post() and port_mail_to_view() on the UI task;
 * the ask's state word is the control task's but for that WAITING.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "prog_link.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    atomic_uint pins;       /**< port n's PIN register in byte n          */
    atomic_uint meta;       /**< functions, refusals, count, FUNC, epoch  */
    atomic_uint gen;        /**< odd while the two words above change     */
    atomic_uint ask;        /**< the UI's last ask, 0 for none            */
    atomic_uint ask_state;  /**< its number << 8 | prog_link_ask_state_t  */
    atomic_uint ask_wait;   /**< what it waits on: a prog_link_wait_t     */
    uint8_t     taken;      /**< control task: last number handed on      */
    unsigned    epoch;      /**< control task: one up at each loss        */
    unsigned    ui_epoch;   /**< UI task: the epoch of the view drawn     */
} port_mail_t;

/** Nothing known and no ask. */
#define PORT_MAIL_META_UNKNOWN 0x00FF7777u
#define PORT_MAIL_INIT { 0xFFFFFFFFu, PORT_MAIL_META_UNKNOWN, 0u, 0u, 0u, \
                         0u, 0u, 0u, 0u }

/**
 * Control task, after each service of the PROG page: hand on a new ask
 * from the UI (or mark it LOST when made in an older epoch, REFUSED when
 * prog_link_ask() refuses it), say how the ask handed on stands, and
 * publish the page as @p pl holds it.
 */
void port_mail_publish(port_mail_t *m, prog_link_t *pl);

/** Control task, on the link-loss edge: a new epoch, nothing known, and
 *  the last ask LOST unless it is already settled. */
void port_mail_lost(port_mail_t *m);

/**
 * UI task, port_view's ask hook: post ask @p seq (1 to 255) for port
 * @p port in the epoch of the view last taken.  False for number 0,
 * which stands for no ask.
 */
bool port_mail_post(port_mail_t *m, uint8_t seq, uint8_t port,
                    prog_link_ask_kind_t kind, uint16_t value);

/** UI task, once a frame: the view and the ask's state to port_view. */
void port_mail_to_view(port_mail_t *m);

#ifdef __cplusplus
}
#endif
