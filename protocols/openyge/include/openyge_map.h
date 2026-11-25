/**
 * @file openyge_map.h
 * @brief The parameter table of an OpenYGE ESC (electronic speed controller),
 *        one field per index: name, kind, display scale, access and the
 *        values a write may carry.
 *
 * Written from docs/OpenYGE.md, section 5.3, with the writable sets of that
 * table.  Access follows its Access column: identity, telemetry and raw
 * fields are read-only; CHECK and edit fields are writable within their
 * set.  A value read outside the set is shown and written back as read, so
 * the set only judges a value that differs from the one read.
 *
 * Indices 0 to 31 have a field.  An index from 32 to 63 has none and is raw.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef OPENYGE_MAP_H
#define OPENYGE_MAP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Indices with a field of their own: 0 to 31. */
#define OY_MAP_FIELDS 32u
/** @brief The parameter count of the table this map describes (index 0). */
#define OY_MAP_COUNT 32u
/** @brief The lowest firmware a write goes to, as indices 12-13 carry it:
 *         103547 = V1.03547, the first with parameter access. */
#define OY_FW_MIN_WRITE 103547u

/** @brief Index of the ESC mode. */
#define OY_I_MODE      1u
/** @brief Index of the BEC (battery eliminator circuit) voltage. */
#define OY_I_BEC       2u
/** @brief Index of the ESC type code. */
#define OY_I_TYPE      11u
/** @brief Index of the low word of the firmware version. */
#define OY_I_FW_LO     12u
/** @brief Index of the high word of the firmware version. */
#define OY_I_FW_HI     13u
/** @brief Index of the low word of the serial number. */
#define OY_I_SERIAL_LO 14u
/** @brief Index of the high word of the serial number. */
#define OY_I_SERIAL_HI 15u
/** @brief Index of the telemetry type. */
#define OY_I_TELEMETRY 25u
/** @brief Index of the flags. */
#define OY_I_FLAGS     26u
/** @brief Bit of index 26 that raises the upper end of index 2. */
#define OY_FLAG_BEC12V 0x0008u

/** @brief Who may change a field (docs/OpenYGE.md 5.3, column Access). */
typedef enum {
    OY_ACC_IDENTITY = 0, /**< read-only, never written                      */
    OY_ACC_TELEMETRY,    /**< read-only: a change ends this wire            */
    OY_ACC_RAW,          /**< meaning not known: shown as a number, kept    */
    OY_ACC_CHECK,        /**< writable; a wrong value can damage, so marked */
    OY_ACC_EDIT,         /**< writable                                      */
} oy_access_t;

/** @brief How a field's value reads. */
typedef enum {
    OY_KIND_NUMBER = 0,  /**< a number, raw / 10^decimals in the unit       */
    OY_KIND_CHOICE,      /**< a code with a name per value                  */
    OY_KIND_FLAGS,       /**< a bit field                                   */
    OY_KIND_U32_LO,      /**< the low word of a 32-bit value                */
    OY_KIND_U32_HI,      /**< the high word of a 32-bit value               */
} oy_kind_t;

/** @brief One field of the table. */
typedef struct {
    const char *key;        /**< the field's name, English in every language */
    uint8_t     access;     /**< an oy_access_t                              */
    uint8_t     kind;       /**< an oy_kind_t                                */
    uint8_t     decimals;   /**< display: the raw value / 10^decimals        */
    uint8_t     n_names;    /**< entries at @c names                         */
    const char *unit;       /**< display unit, "" for none                   */
    uint16_t    lo;         /**< lowest writable value                       */
    uint16_t    hi;         /**< highest writable value                      */
    uint16_t    hi_flag;    /**< index 2: the highest while 26 bit 3 is set  */
    uint16_t    edit_bits;  /**< flags: the bits a write may change          */
    uint32_t    set;        /**< bit v set: v is writable (values below 32);
                                 0 for the whole range @c lo to @c hi        */
    uint16_t    check_bits; /**< flags: the writable bits marked CHECK       */
    const char *const *names; /**< name per value, NULL in a gap; NULL none  */
} oy_field_t;

/**
 * @brief The field of an index.
 * @param index the parameter index
 * @return the field, or NULL for an index of 32 and above
 */
const oy_field_t *oy_map_field(uint16_t index);

/**
 * @brief Whether a write may change this index at all: CHECK or edit.
 * @param index the parameter index
 * @return true for a writable field
 */
bool oy_map_writable(uint16_t index);

/**
 * @brief Whether a writable index may take @p value in place of @p was.
 *
 * Index 2 takes up to its @c hi_flag while bit 3 of @p flags is set.  Index
 * 26 takes a value that differs from @p was in its @c edit_bits only.
 *
 * @param index the parameter index
 * @param value the value to write
 * @param was   the value as read
 * @param flags index 26 of the table being written
 * @return true when the value is in the field's writable set
 */
bool oy_map_value_ok(uint16_t index, uint16_t value, uint16_t was,
                     uint16_t flags);

/**
 * @brief The name of a value of a choice field.
 * @param index the parameter index
 * @param value the value
 * @return the name, or NULL when the field has none for it
 */
const char *oy_map_choice_name(uint16_t index, uint16_t value);

/**
 * @brief The model name of an ESC type code (index 11).
 * @param code the type code
 * @return the name, or NULL for a code not in the table
 */
const char *oy_map_type_name(uint16_t code);

/**
 * @brief A 32-bit value from its two words: the firmware version from
 *        indices 12 and 13, the serial number from 14 and 15.
 * @param lo the low word, index 12 or 14
 * @param hi the high word, index 13 or 15
 * @return the value; 103555 for firmware V1.03555
 */
uint32_t oy_map_u32(uint16_t lo, uint16_t hi);

#ifdef __cplusplus
}
#endif

#endif /* OPENYGE_MAP_H */
