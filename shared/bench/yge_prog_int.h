/**
 * @file
 * @brief What yge_prog.c and yge_prog_file.c share.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_YGE_PROG_INT_H
#define RCBENCH_YGE_PROG_INT_H

#include "yge_prog.h"

/** @brief What the model's commands are part of. */
enum {
    YGE_PROG_FLOW_NONE = 0,
    YGE_PROG_FLOW_CONNECT, /**< OPEN, ENTER, READ                     */
    YGE_PROG_FLOW_LEAVE,   /**< LEAVE, CLOSE                          */
    YGE_PROG_FLOW_ONE,     /**< one command                           */
    YGE_PROG_FLOW_BACKUP,  /**< the card saves the backup, then WRITE */
};

/**
 * @brief A message.
 * @param p      the model
 * @param m      the message
 * @param detail its code
 * @return @p m
 */
yge_prog_msg_t yge_prog_say(yge_prog_t *p, yge_prog_msg_t m, uint16_t detail);

/**
 * @brief A command made, its pass offered.
 * @param p    the model
 * @param kind what it asks
 * @return it, to fill
 */
yge_prog_cmd_t *yge_prog_issue(yge_prog_t *p, yge_prog_cmd_kind_t kind);

/**
 * @brief Whether a write may go now: the gate of an operation on an ESC that
 *        answered, a table that is written, and a draft of the read copy as it
 *        stands.  Says nothing.
 * @param p        the model
 * @param n        the draft's values
 * @param reads    the read copy it was made from
 * @param read_crc and its CRC
 * @return YGE_PROG_MSG_NONE, or why not
 */
yge_prog_msg_t yge_prog_write_gate(const yge_prog_t *p, uint16_t n,
                                   uint16_t reads, uint16_t read_crc);

/**
 * @brief WRITE goes: the command, its draft and the read copy it was made
 *        from.
 * @param p        the model
 * @param draft    the draft
 * @param n        its values
 * @param reads    LINK_YP_READS
 * @param read_crc LINK_YP_READ_CRC
 */
void yge_prog_write_go(yge_prog_t *p, const uint16_t *draft, uint16_t n,
                       uint16_t reads, uint16_t read_crc);

#endif /* RCBENCH_YGE_PROG_INT_H */
