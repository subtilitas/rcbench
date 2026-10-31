/**
 * @file ew_map.h
 * @brief The settings of an ESC (electronic speed controller) as data: which
 *        byte of the page is which field, in which layouts, with which values.
 *
 * The tables are pure data and hold no state.  A byte no field names is kept
 * as it was read: unused bytes, markers, tags, melody and tune areas.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EW_MAP_H
#define EW_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The firmwares. */
typedef enum {
    EW_FW_NONE = 0, /**< not identified, or refused   */
    EW_FW_S,        /**< BLHeli_S                     */
    EW_FW_BJ,       /**< Bluejay                      */
    EW_FW_AM,       /**< AM32                         */
    EW_FWS
} ew_fw_t;

/** @brief Bytes read and written of a BLHeli_S page. */
#define EW_MAP_LEN_S  0x70u
/** @brief Bytes read and written of a Bluejay page. */
#define EW_MAP_LEN_BJ 255u
/** @brief Bytes read and written of an AM32 page. */
#define EW_MAP_LEN_AM 192u
/** @brief The most bytes a page holds in this module. */
#define EW_PAGE_MAX 256u

/** @brief What may be done with a field in a layout that is written. */
typedef enum {
    EW_ACC_EDIT = 0, /**< a write may change it                         */
    EW_ACC_SHOW,     /**< shown; a write refuses any change             */
} ew_access_t;

/** @brief Field flags. */
#define EW_FF_WARN 0x01u /**< the warning before a write names this field */

/**
 * @brief One field.
 *
 * Its values are @c lo to @c hi, and @c alt when @c has_alt is set.  With
 * @c labels, the value @c raw reads labels[raw - lo].  Otherwise it reads
 * (raw x mul + add) / div, with @c dec decimal places, then @c unit.
 */
typedef struct {
    const char *name;          /**< English in every language              */
    const char *const *labels; /**< the names of lo to hi, or NULL         */
    const char *unit;          /**< the unit, "" for none                  */
    const char *alt_label;     /**< the name of @c alt                     */
    int32_t  mul;              /**< see above; 0 reads as 1                */
    int32_t  add;              /**< see above                              */
    uint16_t div;              /**< see above; 0 reads as 1                */
    uint8_t  off;              /**< the byte in the page                   */
    uint8_t  access;           /**< an ew_access_t                         */
    uint8_t  flags;            /**< EW_FF_*                                */
    uint8_t  lo;               /**< the lowest value                       */
    uint8_t  hi;               /**< the highest value                      */
    uint8_t  alt;              /**< one more value                         */
    bool     has_alt;          /**< @c alt is a value                      */
    uint8_t  dec;              /**< decimal places                         */
    uint8_t  from;             /**< the first layout that has it           */
    uint8_t  to;               /**< the last layout that has it            */
} ew_field_t;

/**
 * @brief A firmware's table.
 * @param fw    the firmware
 * @param count takes its row count; 0 for EW_FW_NONE
 * @return the rows, NULL for no table
 */
const ew_field_t *ew_map_table(uint8_t fw, size_t *count);

/**
 * @brief The field at a byte of a page.
 * @param fw     the firmware
 * @param layout the page's layout or revision
 * @param off    the byte
 * @return the field, NULL when no field names the byte in this layout
 */
const ew_field_t *ew_map_find(uint8_t fw, uint8_t layout, uint8_t off);

/**
 * @brief Row @p i among the fields a layout has.
 * @param fw     the firmware
 * @param layout the page's layout or revision
 * @param i      the row, from 0
 * @return the field, NULL past the last
 */
const ew_field_t *ew_map_nth(uint8_t fw, uint8_t layout, size_t i);

/**
 * @brief Whether a layout is one this module writes: BLHeli_S 33, Bluejay 208,
 *        AM32 3 and 4.
 * @param fw     the firmware
 * @param layout the layout or revision
 * @return true when a write is possible
 */
bool ew_map_writable(uint8_t fw, uint8_t layout);

/**
 * @brief Bytes read and written of this firmware's page.
 * @param fw the firmware
 * @return the length, 0 for EW_FW_NONE
 */
uint16_t ew_map_len(uint8_t fw);

/**
 * @brief Whether @p raw is a value of the field.
 * @param f   the field, may be NULL
 * @param raw the byte
 * @return true when an edit may set it
 */
bool ew_field_ok(const ew_field_t *f, uint8_t raw);

/**
 * @brief A byte as the operator reads it: "15deg", "NORMAL". A value that
 *        is none of the field's reads `raw 17`.
 * @param f    the field
 * @param raw  the byte
 * @param out  takes the text, NUL terminated
 * @param size bytes at @p out
 * @return the characters written, without the NUL; 0 when size is 0
 */
size_t ew_field_text(const ew_field_t *f, uint8_t raw, char *out, size_t size);

/** @brief What ew_image_check() found. */
typedef enum {
    EW_IMG_OK = 0,        /**< every change is an edit the map allows       */
    EW_IMG_NOT_EDITABLE,  /**< a byte no editable field names has changed   */
    EW_IMG_VALUE,         /**< an editable field holds no value of its own  */
} ew_img_t;

/**
 * @brief Compare a draft with the page read: only editable fields may differ,
 *        and only to one of their values.
 * @param fw     the firmware
 * @param layout the page's layout or revision
 * @param orig   the page as read
 * @param draft  the draft
 * @param n      bytes of both
 * @param off    takes the first offending byte, may be NULL
 * @return EW_IMG_OK, or what is wrong
 */
ew_img_t ew_image_check(uint8_t fw, uint8_t layout, const uint8_t *orig,
                        const uint8_t *draft, size_t n, uint16_t *off);

/** @brief BLHeli_S's table: ew_map_table() is the way in. */
extern const ew_field_t ew_map_s[];
/** @brief Rows of ew_map_s. */
extern const size_t ew_map_s_count;
/** @brief Bluejay's table. */
extern const ew_field_t ew_map_bj[];
/** @brief Rows of ew_map_bj. */
extern const size_t ew_map_bj_count;
/** @brief AM32's table. */
extern const ew_field_t ew_map_am[];
/** @brief Rows of ew_map_am. */
extern const size_t ew_map_am_count;

#ifdef __cplusplus
}
#endif

#endif /* EW_MAP_H */
