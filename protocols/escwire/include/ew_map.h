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
    EW_FW_E32,      /**< ESCape32                     */
    EW_FWS
} ew_fw_t;

/** @brief Bytes read and written of a BLHeli_S page. */
#define EW_MAP_LEN_S  0x70u
/** @brief Bytes read and written of a Bluejay page. */
#define EW_MAP_LEN_BJ 255u
/** @brief Bytes read and written of an AM32 page. */
#define EW_MAP_LEN_AM 192u
/** @brief The numeric keys of ESCape32: every key but `music`. */
#define EW_E32_KEYS 46u
/** @brief Bytes of an ESCape32 page: this module's picture of its keys, each a
 *         16-bit integer, low byte first, in the order of ew_map_e32. */
#define EW_MAP_LEN_E32 92u
/** @brief The ESCape32 revision this module writes. */
#define EW_E32_REVISION 17u
/** @brief The most characters of ESCape32's `music` value. */
#define EW_E32_MUSIC_MAX 255u
/** @brief The value of a key a read did not bring. */
#define EW_KEY_NONE INT16_MIN
/** @brief The most bytes a page holds in this module. */
#define EW_PAGE_MAX 256u

/** @brief What may be done with a field in a layout that is written. */
typedef enum {
    EW_ACC_EDIT = 0, /**< a write may change it                         */
    EW_ACC_SHOW,     /**< shown; a write refuses any change             */
} ew_access_t;

/** @brief Field flags. */
#define EW_FF_WARN 0x01u /**< the warning before a write names this field */
#define EW_FF_WARN_ZERO 0x02u /**< with EW_FF_WARN: only a change to 0 does  */

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
 * @param count takes its row count; 0 for EW_FW_NONE and for ESCape32, whose
 *              keys are no bytes: ew_map_e32 holds them
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
 *        AM32 3 and 4, ESCape32 17.
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
 *        and only to one of their values.  On ESCape32 @p off is the low byte
 *        of the key, a key changed with its floor changed may not be below it,
 *        and a length other than EW_MAP_LEN_E32 is EW_IMG_NOT_EDITABLE, with
 *        @p off the shorter of the two.
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

/** @brief The groups of ESCape32's keys. */
typedef enum {
    EW_KG_MOTOR = 0, /**< motor and output stage */
    EW_KG_THROTTLE,  /**< throttle input         */
    EW_KG_TELEMETRY, /**< telemetry              */
    EW_KG_PROTECT,   /**< protection             */
    EW_KG_SOUND,     /**< sound and lights       */
    EW_KGS
} ew_key_group_t;

/**
 * @brief One numeric key of the text interface.
 *
 * Its values are @c lo to @c hi in steps of @c step from @c lo, and @c alt
 * when @c has_alt is set.  With @c labels, the value @c v reads labels[v - lo].
 * Otherwise it reads v x mul with @c dec decimal places, then @c unit.
 */
typedef struct {
    const char *key;           /**< as the wire spells it                  */
    const char *const *labels; /**< the names of lo to hi, or NULL         */
    const char *unit;          /**< the unit, "" for none                  */
    const char *alt_label;     /**< the name of @c alt                     */
    const char *floor;         /**< a key this one is not below, or NULL   */
    int32_t  mul;              /**< see above; 0 reads as 1                */
    int16_t  lo;               /**< the lowest value                       */
    int16_t  hi;               /**< the highest value                      */
    int16_t  alt;              /**< one more value                         */
    uint16_t step;             /**< see above; 0 reads as 1                */
    bool     has_alt;          /**< @c alt is a value                      */
    uint8_t  dec;              /**< decimal places                         */
    uint8_t  flags;            /**< EW_FF_*                                */
    uint8_t  group;            /**< an ew_key_group_t                      */
} ew_key_t;

/**
 * @brief A key by its spelling.
 * @param s    the characters, no NUL needed
 * @param n    their count
 * @param slot takes its place in ew_map_e32, may be NULL
 * @return the key, NULL for `music` and any word that is no key
 */
const ew_key_t *ew_key_find(const char *s, size_t n, size_t *slot);

/**
 * @brief Whether @p v is a value of the key.
 * @param k the key, may be NULL
 * @param v the value
 * @return true when an edit may set it
 */
bool ew_key_ok(const ew_key_t *k, int16_t v);

/**
 * @brief A value as the operator reads it.  A value that is none of the key's
 *        reads `raw 17`; EW_KEY_NONE reads as no text.
 * @param k    the key
 * @param v    the value
 * @param out  takes the text, NUL terminated
 * @param size bytes at @p out
 * @return the characters written, without the NUL; 0 when size is 0
 */
size_t ew_key_text(const ew_key_t *k, int16_t v, char *out, size_t size);

/**
 * @brief A value out of an ESCape32 page.
 * @param page the page
 * @param slot the key's place, under EW_E32_KEYS
 * @return the value
 */
int16_t ew_e32_get(const uint8_t *page, size_t slot);

/**
 * @brief A value into an ESCape32 page.
 * @param page the page
 * @param slot the key's place, under EW_E32_KEYS
 * @param v    the value
 */
void ew_e32_put(uint8_t *page, size_t slot, int16_t v);

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
/** @brief ESCape32's keys, in the order of the wire's key numbers. */
extern const ew_key_t ew_map_e32[EW_E32_KEYS];

#ifdef __cplusplus
}
#endif

#endif /* EW_MAP_H */
