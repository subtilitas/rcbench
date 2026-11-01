/**
 * @file ew_guard.h
 * @brief The one gate every outgoing frame and line passes. A settings-only
 *        host sends what this allows and nothing else.
 *
 * ew_guard_frame() judges one frame of protocol A against what was sent
 * before it.  An erase and a program need a page ew_guard_approve() has
 * judged, and the data frame has to hold exactly that page.  No function here
 * touches a wire.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EW_GUARD_H
#define EW_GUARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ew_id.h"
#include "ew_map.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The longest line of the text interface, characters, its LF left out. */
#define EW_LINE_MAX 1023u

/** @brief Why the guard refused. */
typedef enum {
    EW_REF_NONE = 0,       /**< allowed                                     */
    EW_REF_ARG,            /**< a NULL, or a frame of no bytes              */
    EW_REF_NO_CONTACT,     /**< only the first contact goes before a contact */
    EW_REF_COMMAND,        /**< a command not on the list                   */
    EW_REF_FORM,           /**< the frame's length or fixed bytes are wrong */
    EW_REF_CRC,            /**< the frame's CRC does not fit                */
    EW_REF_AM_ADDRESS,     /**< AM32: a low address that is not special     */
    EW_REF_NO_ADDRESS,     /**< a read, erase or program with no address    */
    EW_REF_ERASE_AM,       /**< AM32 takes no erase                         */
    EW_REF_FIRST_PAGE,     /**< erase or program in the first flash page    */
    EW_REF_BOOTLOADER,     /**< erase or program in the bootloader area     */
    EW_REF_NOT_SETTINGS,   /**< erase or program outside the settings start */
    EW_REF_NOT_APPROVED,   /**< an erase or data with no page judged        */
    EW_REF_COUNT,          /**< announced data of another length            */
    EW_REF_DATA,           /**< a data frame that is not the judged page    */
    EW_REF_NO_DATA,        /**< a program with no data sent                 */
    EW_REF_LEAVE_ARG,      /**< a leave that does not leave this firmware   */
    EW_REF_HALF_WRITTEN,   /**< a leave while a judged page is being written */
    EW_REF_NOT_WRITABLE,   /**< a page of a layout that is not written      */
    EW_REF_AM_BYTE0,       /**< AM32: byte 0 is not 0x01                    */
    EW_REF_AM_REVISION,    /**< AM32: a revision not known                  */
    EW_REF_AM_LENGTH,      /**< AM32: not exactly 192 bytes                 */
    EW_REF_CHANGED_BYTE,   /**< a byte no editable field names has changed  */
    EW_REF_VALUE,          /**< an editable field holds no value of its own */
    EW_REF_RESTORE,        /**< a restore other than a named AM32 on an erased
                                page */
    EW_REF_LINE,           /**< a line of the text interface that is refused */
} ew_refuse_t;

/** @brief The guard's memory of the session. */
typedef struct {
    uint8_t        cpu;       /**< an ew_cpu_t; EW_CPU_NONE before a contact */
    bool           addr_set;  /**< an address stands                         */
    bool           data_sent; /**< the judged page went out                  */
    uint16_t       addr;      /**< the address                               */
    uint16_t       base;      /**< the settings start                        */
    uint16_t       page;      /**< EFM8: the flash page, bytes               */
    uint16_t       boot;      /**< EFM8: the bootloader's address            */
    uint16_t       announced; /**< bytes announced and not yet sent          */
    uint16_t       len;       /**< bytes of the judged page                  */
    const uint8_t *approved;  /**< the judged page, NULL for none            */
} ew_guard_t;

/**
 * @brief Before a first contact: nothing but the first contact goes.
 * @param g the guard
 */
void ew_guard_init(ew_guard_t *g);

/**
 * @brief A first contact was answered: the frames of this processor go.
 * @param g the guard
 * @param c the contact
 */
void ew_guard_target(ew_guard_t *g, const ew_contact_t *c);

/**
 * @brief Judge a page before a write.  @p draft has to stay valid and
 *        unchanged in what the map protects until ew_guard_done().
 * @param g        the guard
 * @param id       what identification found
 * @param orig     the page as read
 * @param draft    the page to write
 * @param n        its length
 * @param off      takes the first offending byte for EW_REF_CHANGED_BYTE and
 *                 EW_REF_VALUE, may be NULL
 * @return EW_REF_NONE when the page may go
 */
ew_refuse_t ew_guard_approve(ew_guard_t *g, const ew_ident_t *id,
                             const uint8_t *orig, const uint8_t *draft,
                             size_t n, uint16_t *off);

/**
 * @brief Judge a whole backup page for an AM32 whose settings are erased:
 *        the one write onto a page that holds nothing.
 * @param g     the guard
 * @param id    what identification found
 * @param orig  the page as read
 * @param page  the backup
 * @param n     its length
 * @return EW_REF_NONE when the page may go
 */
ew_refuse_t ew_guard_approve_restore(ew_guard_t *g, const ew_ident_t *id,
                                     const uint8_t *orig, const uint8_t *page,
                                     size_t n);

/**
 * @brief The write ended and the page is as intended: no judged page stands.
 * @param g the guard
 */
void ew_guard_done(ew_guard_t *g);

/**
 * @brief Judge one frame of protocol A before it goes out.  A frame allowed
 *        is counted as sent.
 * @param g the guard
 * @param f the frame's bytes
 * @param n their count
 * @return EW_REF_NONE when it may go
 */
ew_refuse_t ew_guard_frame(ew_guard_t *g, const uint8_t *f, size_t n);

/**
 * @brief Judge one line of the text interface: `info`, `show`, `get <key>`,
 *        `set <key> <value>` other than `music`, `save` or `help`, up to
 *        EW_LINE_MAX characters and its LF, with no control character.
 * @param line the bytes, the LF last
 * @param n    their count
 * @return EW_REF_NONE, or EW_REF_ARG or EW_REF_LINE
 */
ew_refuse_t ew_guard_line(const uint8_t *line, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* EW_GUARD_H */
