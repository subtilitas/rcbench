/*
 * The register file of a GT911 touch controller as an I2C (Inter-Integrated
 * Circuit) target sees it: the bytes a master writes and reads, one at a
 * time, and the frame handshake.  Pure C.  The RP2350 build calls the three
 * byte functions from the I2C interrupt; the host suite calls them as a
 * master would.
 *
 * Register pointer: 16 bit, big endian, set by the first two bytes of a
 * write, and incremented by every byte read or written after it.  A read
 * with no pointer write before it continues from the current pointer.
 *
 * Served (GT911 Programming Guide Rev.10, sections 2 and 3):
 *
 *   0x8040-0x8041  command and ESD check: what was last written
 *   0x8047         config version 0x41
 *   0x8048-0x804B  X and Y output maximum, little endian
 *   0x804C         touch number 5
 *   0x8140-0x8143  product ID "911" and 0x00
 *   0x8144-0x8145  firmware version 0xE001
 *   0x8146-0x8149  X and Y resolution, little endian
 *   0x814E         status: bit 7 frame ready, bits 3..0 point count
 *   0x814F-0x8156  point 1: track ID, X, Y, size, reserved
 *   anything else  0x00
 *
 * A write of any value to 0x814E acknowledges the frame: the status reads
 * 0x00 until the next frame is published.  Writes elsewhere are ignored.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GT911_EMU_ADDR        0x14u

#define GT911_EMU_REG_COMMAND 0x8040u
#define GT911_EMU_REG_PRODUCT 0x8140u
#define GT911_EMU_REG_STATUS  0x814Eu
#define GT911_EMU_REG_POINT1  0x814Fu

#define GT911_EMU_READY       0x80u

/** Contact size the emulator reports, in the GT911's units. */
#define GT911_EMU_SIZE        24u

/** One frame: no contact, or one contact at (x, y). */
typedef struct {
    bool     down;
    uint16_t x;
    uint16_t y;
} gt911_emu_frame_t;

typedef struct {
    uint16_t res_x;
    uint16_t res_y;

    uint8_t  command[2];     /* 0x8040, 0x8041                          */
    uint16_t ptr;
    uint8_t  wbytes;         /* bytes written in the current write       */
    bool     reading;        /* the current transfer is a read           */
    bool     busy;           /* a transfer has started and not finished  */

    uint8_t  status;         /* 0x814E                                   */
    uint8_t  point[8];       /* 0x814F..0x8156                           */

    /* Counted by the byte functions, read by the injector.  Each wraps. */
    uint32_t reads;          /* read transfers                           */
    uint32_t acks;           /* writes to 0x814E                         */
    uint32_t product_reads;  /* reads that start at 0x8140               */
} gt911_emu_t;

void gt911_emu_init(gt911_emu_t *e, uint16_t res_x, uint16_t res_y);

/** The master wrote @p b. */
void gt911_emu_rx(gt911_emu_t *e, uint8_t b);

/** The master reads a byte: the one at the pointer, which then moves on. */
uint8_t gt911_emu_tx(gt911_emu_t *e);

/** A STOP, a repeated START or an abort ended the transfer. */
void gt911_emu_finish(gt911_emu_t *e);

/** The byte at @p reg, as a read would return it, without side effects. */
uint8_t gt911_emu_peek(const gt911_emu_t *e, uint16_t reg);

/**
 * Make @p f the frame the master reads next.  Refused (false) while a
 * transfer is under way or the previous frame is not yet acknowledged, so a
 * master never reads the status of one frame and the points of another.
 */
bool gt911_emu_publish(gt911_emu_t *e, const gt911_emu_frame_t *f);

/**
 * Withdraw an unacknowledged frame: the status reads 0x00.  Refused (false)
 * while a transfer is under way.
 */
bool gt911_emu_withdraw(gt911_emu_t *e);

#ifdef __cplusplus
}
#endif
