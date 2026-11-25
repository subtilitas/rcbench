/**
 * @file openyge_guard.h
 * @brief The one gate every frame to an OpenYGE ESC (electronic speed
 *        controller) passes right before the driver.
 *
 * What goes out: a telemetry request with a zero control payload, and a
 * parameter write of a draft oy_guard_approve() has judged against the
 * table read last.  Nothing else.
 *
 * | Refused                                                    | Reason                  |
 * |------------------------------------------------------------|-------------------------|
 * | a frame type other than TELE_REQ and WRITE_PARAM_REQ        | OY_REF_TYPE             |
 * | a write with no complete table this session                 | OY_REF_NO_TABLE         |
 * | a draft from another read (read number or CRC)              | OY_REF_OTHER_READ       |
 * | index 0, 11 to 15                                           | OY_REF_IDENTITY         |
 * | index 25, the telemetry type                                | OY_REF_TELEMETRY        |
 * | an index the map does not mark CHECK or edit                | OY_REF_NOT_EDITABLE     |
 * | an index at or above the count                              | OY_REF_PAST_COUNT       |
 * | a value outside the field's writable set                    | OY_REF_VALUE            |
 * | a change to a bit of index 26 other than bits 0 and 1       | OY_REF_FLAG_BITS        |
 * | a count other than 32                                       | OY_REF_RO_COUNT         |
 * | a legacy header (protocol version below 3)                  | OY_REF_RO_LEGACY        |
 * | firmware below OY_FW_MIN_WRITE                              | OY_REF_RO_FIRMWARE      |
 *
 * No function here touches a wire.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef OPENYGE_GUARD_H
#define OPENYGE_GUARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "openyge.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Why the guard refused. */
typedef enum {
    OY_REF_NONE = 0,       /**< allowed                                      */
    OY_REF_ARG,            /**< a NULL, or a draft of another length         */
    OY_REF_FORM,           /**< not a whole v3 request: length, sync,
                                version, master bit or CRC                   */
    OY_REF_TYPE,           /**< a frame type not on the allow-list           */
    OY_REF_REQUEST,        /**< a telemetry request with a payload not zero  */
    OY_REF_NO_TABLE,       /**< no complete table this session               */
    OY_REF_OTHER_READ,     /**< a draft of another read                      */
    OY_REF_RO_LEGACY,      /**< read-only: a legacy header                   */
    OY_REF_RO_COUNT,       /**< read-only: a count other than 32             */
    OY_REF_RO_FIRMWARE,    /**< read-only: firmware below OY_FW_MIN_WRITE    */
    OY_REF_IDENTITY,       /**< index 0 or 11 to 15                          */
    OY_REF_TELEMETRY,      /**< index 25                                     */
    OY_REF_NOT_EDITABLE,   /**< an index the map keeps read-only             */
    OY_REF_PAST_COUNT,     /**< an index at or above the count               */
    OY_REF_VALUE,          /**< a value outside the writable set             */
    OY_REF_FLAG_BITS,      /**< index 26: a bit other than 0 and 1 changed   */
    OY_REF_NOT_APPROVED,   /**< a write of no approved draft, or of a value
                                the approved draft does not hold             */
} oy_ref_t;

/** @brief The guard's memory of the session. */
typedef struct {
    uint16_t orig[OPENYGE_MAX_PARAMS];  /**< the table read last            */
    uint16_t draft[OPENYGE_MAX_PARAMS]; /**< the approved draft             */
    uint16_t count;    /**< parameters of the table read last, 0 for none   */
    uint16_t read_no;  /**< counts the complete reads; wraps at 65535       */
    uint16_t read_crc; /**< CRC-16/XMODEM of the table read last            */
    uint8_t  ro;       /**< an oy_ref_t: why the table read last is
                            read-only; OY_REF_NONE when it is writable or
                            none was read                                   */
    bool     table;    /**< a complete table was read this session          */
    bool     approved; /**< @c draft holds a judged draft                   */
} oy_guard_t;

/**
 * @brief A new session: no table, nothing approved, the read number kept.
 * @param g the guard
 */
void oy_guard_init(oy_guard_t *g);

/**
 * @brief A complete table was read: it becomes the table every draft is
 *        judged against, the read number counts up, and an approval ends.
 * @param g      the guard
 * @param table  the values, index 0 first
 * @param count  entries at @p table, 1 to OPENYGE_MAX_PARAMS
 * @param legacy the frames came with a legacy header
 */
void oy_guard_table(oy_guard_t *g, const uint16_t *table, uint16_t count,
                    bool legacy);

/**
 * @brief The CRC-16/XMODEM over a table, each value little-endian.
 * @param table the values
 * @param count entries at @p table
 * @return the CRC
 */
uint16_t oy_guard_table_crc(const uint16_t *table, uint16_t count);

/**
 * @brief Judge a draft before a write: every index it changes against its
 *        field, and the draft against the read it says it comes from.
 * @param g        the guard
 * @param draft    the table to write, @c count entries
 * @param n        entries at @p draft
 * @param read_no  the read number the draft was made from
 * @param read_crc that read's CRC
 * @param bad      takes the first refused index, may be NULL
 * @return OY_REF_NONE when the draft may go; the draft is then approved
 */
oy_ref_t oy_guard_approve(oy_guard_t *g, const uint16_t *draft, size_t n,
                          uint16_t read_no, uint16_t read_crc, uint16_t *bad);

/**
 * @brief The write ended: no draft stands approved.
 * @param g the guard
 */
void oy_guard_done(oy_guard_t *g);

/**
 * @brief Judge one frame right before the driver sends it.
 * @param g the guard
 * @param f the frame's bytes
 * @param n their count
 * @return OY_REF_NONE when it may go
 */
oy_ref_t oy_guard_frame(const oy_guard_t *g, const uint8_t *f, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* OPENYGE_GUARD_H */
