/**
 * @file
 * @brief What sense_link.c and sense_link_view.c share and nobody else sees:
 *        the set-up's frame layout, the status register index, and three names
 *        the two files cross.
 *
 * Not part of the module's interface (sense_link.h).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SENSE_LINK_INT_H
#define SENSE_LINK_INT_H

#include <stdbool.h>
#include <stdint.h>

#include "sense_link.h"

/* The three frames of the set-up, each four registers from its first. */
#define FRAME_N  4u
#define BUS_AT   ((uint8_t)LINK_SN_ENABLE)
#define I228_AT  ((uint8_t)LINK_SN_I228_ADDR)
#define I3221_AT ((uint8_t)LINK_SN_I3221_ADDR)

/* A status register by its page number. */
#define ST(reg) ((unsigned)(reg) - (unsigned)LINK_SN_FLAGS)

/**
 * @brief The write owed now, or NONE, with its registers in @p regs.
 *
 * @param s    The link state.
 * @param regs Receives the registers of the write.
 * @return The write owed, or NONE, as sense_link_op_kind_t.
 */
sense_link_op_kind_t sense_link_write_owed(const sense_link_t *s,
                                           uint16_t *regs);

/**
 * @brief The registers a SENSE status read takes.
 *
 * @param s The link state.
 * @return SENSE_LINK_STATUS_COUNT when the AS5600 part is enabled or the
 *         INA3221 windows come from SERVO_WIN, else
 *         SENSE_LINK_STATUS_COUNT_V48.
 */
unsigned sense_link_status_count(const sense_link_t *s);

/* The ENABLE bit of each part, indexed by sense_link_part_t. */
extern const uint16_t sense_link_en_bit[2];

#endif /* SENSE_LINK_INT_H */
