/**
 * @file
 * @brief What esc_prog.c and esc_prog_file.c share.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_ESC_PROG_INT_H
#define RCBENCH_ESC_PROG_INT_H

#include "esc_prog.h"

/** @brief What the model's commands are part of. */
enum {
    ESC_PROG_FLOW_NONE = 0,
    ESC_PROG_FLOW_CONNECT, /**< OPEN, ENTER, READ */
    ESC_PROG_FLOW_LEAVE,   /**< LEAVE, CLOSE      */
    ESC_PROG_FLOW_ONE,     /**< one command       */
};

/**
 * @brief A message.
 * @param p      the model
 * @param m      the message
 * @param detail its code
 * @return @p m
 */
esc_prog_msg_t esc_prog_say(esc_prog_t *p, esc_prog_msg_t m, uint16_t detail);

/**
 * @brief A command made, its pass offered.
 * @param p    the model
 * @param kind what it asks
 * @return it, to fill
 */
esc_prog_cmd_t *esc_prog_issue(esc_prog_t *p, esc_prog_cmd_kind_t kind);

#endif /* RCBENCH_ESC_PROG_INT_H */
