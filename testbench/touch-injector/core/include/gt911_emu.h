/**
 * @file
 * @brief The register file of a GT911 touch controller as an I2C (Inter-Integrated
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
 *   0x814F-0x8176  points 1 to 5, 8 bytes each: track ID, X, Y, size,
 *                  reserved
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

/** @brief The I2C address the emulator answers at. */
#define GT911_EMU_ADDR        0x14u

/** @name Registers
 *  @{ */
#define GT911_EMU_REG_COMMAND 0x8040u  /**< command, then the ESD check  */
#define GT911_EMU_REG_PRODUCT 0x8140u  /**< product ID "911"             */
#define GT911_EMU_REG_STATUS  0x814Eu  /**< ready flag and point count   */
#define GT911_EMU_REG_POINT1  0x814Fu  /**< the first point's track ID   */
/** @} */

/** @brief The status byte's frame-ready bit. */
#define GT911_EMU_READY       0x80u

/** @brief Contact size the emulator reports, in the GT911's units. */
#define GT911_EMU_SIZE        24u

/** @brief Points in one frame: the GT911's 5. */
#define GT911_EMU_POINTS      5u

/** @brief Bytes per point in the register file. */
#define GT911_EMU_STRIDE      8u

/** @brief One contact of a frame, in the resolution the emulator reports. */
typedef struct {
    uint8_t  id;   /**< track ID, the same for a contact while it is down */
    uint16_t x;    /**< column                                           */
    uint16_t y;    /**< row                                              */
} gt911_emu_point_t;

/** @brief One frame: the contacts down, 0 to GT911_EMU_POINTS of them. */
typedef struct {
    uint8_t           n;                      /**< contacts down        */
    gt911_emu_point_t pt[GT911_EMU_POINTS];   /**< the first n are read */
} gt911_emu_frame_t;

/** @brief The register file and the transfer under way. */
typedef struct {
    uint16_t res_x;          /**< X resolution, 0x8048 and 0x8146        */
    uint16_t res_y;          /**< Y resolution, 0x804A and 0x8148        */

    uint8_t  command[2];     /**< 0x8040, 0x8041                         */
    uint16_t ptr;            /**< the register pointer                   */
    uint8_t  wbytes;         /**< bytes written in the current write     */
    bool     reading;        /**< the current transfer is a read         */
    bool     busy;           /**< a transfer has started, not finished   */

    uint8_t  status;         /**< 0x814E                                 */
    /** @brief 0x814F..0x8176: GT911_EMU_POINTS points of GT911_EMU_STRIDE
     *         bytes each. */
    uint8_t  point[GT911_EMU_POINTS * GT911_EMU_STRIDE];

    /* Counted by the byte functions, read by the injector.  Each wraps. */
    uint32_t reads;          /**< read transfers                         */
    uint32_t acks;           /**< writes to 0x814E                       */
    uint32_t product_reads;  /**< reads that start at 0x8140             */
} gt911_emu_t;

/**
 * @brief Clear the register file and set the resolution it reports.
 * @param e The emulator.
 * @param res_x Columns.
 * @param res_y Rows.
 */
void gt911_emu_init(gt911_emu_t *e, uint16_t res_x, uint16_t res_y);

/**
 * @brief The master wrote @p b.
 * @param e The emulator.
 * @param b The byte.
 */
void gt911_emu_rx(gt911_emu_t *e, uint8_t b);

/**
 * @brief The master reads a byte: the one at the pointer, which then moves on.
 * @param e The emulator.
 * @return The byte.
 */
uint8_t gt911_emu_tx(gt911_emu_t *e);

/**
 * @brief A STOP, a repeated START or an abort ended the transfer.
 * @param e The emulator.
 */
void gt911_emu_finish(gt911_emu_t *e);

/**
 * @brief The byte at @p reg, as a read would return it, without side effects.
 * @param e The emulator.
 * @param reg The register.
 * @return The byte.
 */
uint8_t gt911_emu_peek(const gt911_emu_t *e, uint16_t reg);

/**
 * @brief Make @p f the frame the master reads next.
 *
 * Refused while a transfer is under way or the previous frame is not yet
 * acknowledged, so a master never reads the status of one frame and the
 * points of another.  Refused too for more than GT911_EMU_POINTS points.
 * Point slots past f->n read 0x00.
 *
 * @param e The emulator.
 * @param f The frame.
 * @return Whether it is published.
 */
bool gt911_emu_publish(gt911_emu_t *e, const gt911_emu_frame_t *f);

/**
 * @brief Withdraw an unacknowledged frame: the status reads 0x00.
 * @param e The emulator.
 * @return False while a transfer is under way, and nothing is withdrawn.
 */
bool gt911_emu_withdraw(gt911_emu_t *e);

#ifdef __cplusplus
}
#endif
