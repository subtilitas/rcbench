/**
 * @file exbus_telem.h
 * @brief EX telemetry as an EX Bus reply carries it: the block check, the
 * text, data and message packets, a table of a device's sensors and the
 * values as text.
 *
 * Implements the EX telemetry protocol of JETI devices.  JETI and EX Bus are
 * names or trademarks of their respective owner; they are used here only to
 * say which devices this module works with (README.md, "Legal notice and
 * disclaimer").
 *
 * Source: "JETI Telemetry communication protocol", JETI model, version 1.07
 * of 2015-01-14 (S2 in the module README), sections "Protocol EX", "Data
 * types", "Textual specification", "Message string specification" and
 * "Checksum".
 *
 * A block, as it follows SUB_LEN in a reply:
 *
 *   0xNF      the EX mark: low nibble 0xF, high nibble any value
 *   T:2 L:6   packet kind (0 text, 1 data, 2 message) and L, the bytes
 *             that follow this one, the CRC8 included
 *   4 bytes   serial number: manufacturer ID, then device ID, each
 *             little-endian
 *   1 byte    reserved
 *   items     one text item, data items or one message
 *   CRC8      polynomial 0x07, initial 0, over T:L and every byte up to
 *             the CRC8
 *
 * A data item is a byte with the identifier in its high nibble and the data
 * type in its low nibble, followed by the value.  Identifier 0 says the
 * identifier is the next byte.  The value is little-endian; the top 3 bits
 * of its last byte are the sign (1 negative) and the position of the
 * decimal point (0 to 3), and the bits below are the magnitude.  Types 0,
 * 1, 4 and 8 are numbers of 1, 2, 3 and 4 bytes; 5 (3 bytes) is a time or a
 * date and 9 (4 bytes) a coordinate.  The other types are reserved; their
 * sizes follow from the type's group (2, 3: 2 bytes; 6, 7: 3; 10, 11: 4;
 * 12 to 15: 5), so the parser keeps them as raw bytes and reads on.
 *
 * The module includes nothing outside the C standard headers and holds no
 * global state.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EXBUS_TELEM_H
#define EXBUS_TELEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "exbus_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Packet kinds, the top 2 bits of the second byte. */
#define EXBUS_TM_TEXT     0u
#define EXBUS_TM_DATA     1u
#define EXBUS_TM_MESSAGE  2u

/** Bytes of a block that are not items: mark, kind and length, serial
 *  number, reserved byte, CRC8. */
#define EXBUS_TM_FRAME    8u
/** The longest label, unit and message the length fields encode. */
#define EXBUS_TM_LABEL_MAX   31u
#define EXBUS_TM_UNIT_MAX     7u
#define EXBUS_TM_MESSAGE_MAX 31u
/** The longest value: 5 bytes (types 12 to 15). */
#define EXBUS_TM_VALUE_MAX    5u
/** Data items one block holds at most: 20 bytes of items, 2 bytes each. */
#define EXBUS_TM_MAX_VALUES  10u
/** Identifiers a table keeps, identifier 0 (the device's name) included. */
#define EXBUS_TM_MAX_SENSORS 32u

typedef enum {
    EXBUS_TM_OK = 0,
    EXBUS_TM_ERR_ARG,       /**< a NULL argument */
    EXBUS_TM_ERR_SHORT,     /**< fewer than 8 bytes */
    EXBUS_TM_ERR_TOO_LONG,  /**< more than EXBUS_TM_BLOCK_MAX bytes */
    EXBUS_TM_ERR_MARK,      /**< the first byte's low nibble is not 0xF */
    EXBUS_TM_ERR_LENGTH,    /**< L + 2 is not the block's length */
    EXBUS_TM_ERR_CRC,       /**< the CRC8 does not match */
    EXBUS_TM_ERR_KIND,      /**< packet kind 3 */
    EXBUS_TM_ERR_ITEM,      /**< an item runs into the CRC8, or bytes are
                                 left after a text or a message */
} exbus_tm_err_t;

/** How a value reads. */
typedef enum {
    EXBUS_TM_NUMBER = 0,  /**< types 0, 1, 4, 8: value / 10^decimals */
    EXBUS_TM_TIME,        /**< type 5, lowest decimal-point bit 0 */
    EXBUS_TM_DATE,        /**< type 5, lowest decimal-point bit 1 */
    EXBUS_TM_LATITUDE,    /**< type 9, lowest decimal-point bit 0 */
    EXBUS_TM_LONGITUDE,   /**< type 9, lowest decimal-point bit 1 */
    EXBUS_TM_RAW,         /**< a reserved type: the bytes only */
} exbus_tm_form_t;

/** One data item. */
typedef struct {
    uint8_t id;          /**< 1 to 15, or the byte after identifier 0 */
    uint8_t type;        /**< data type, 0 to 15 */
    uint8_t size;        /**< value bytes, 1 to 5 */
    uint8_t form;        /**< exbus_tm_form_t */
    uint8_t raw[EXBUS_TM_VALUE_MAX];  /**< the value bytes as sent */
    uint8_t decimals;    /**< NUMBER: 0 to 3 */
    uint8_t negative;    /**< NUMBER: the sign; LATITUDE: south;
                              LONGITUDE: west */
    uint8_t part[3];     /**< TIME: hours, minutes, seconds; DATE: year
                              less 2000, month, day */
    int32_t value;       /**< NUMBER: the signed magnitude; LATITUDE and
                              LONGITUDE: the 29 value bits as sent, whose
                              encoding the specification does not give */
} exbus_tm_value_t;

/** One block as exbus_tm_parse() read it. */
typedef struct {
    uint8_t  kind;            /**< EXBUS_TM_TEXT, _DATA or _MESSAGE */
    uint8_t  reserved;        /**< the reserved byte as sent */
    uint16_t manufacturer;    /**< upper part of the serial number */
    uint16_t device;          /**< lower part */
    /* EXBUS_TM_DATA */
    uint8_t  n_values;
    exbus_tm_value_t values[EXBUS_TM_MAX_VALUES];
    /* EXBUS_TM_TEXT: the label and unit of one identifier; identifier 0
     * names the device.  Bytes as sent, NUL-terminated. */
    uint8_t  text_id;
    uint8_t  label_len;
    uint8_t  unit_len;
    char     label[EXBUS_TM_LABEL_MAX + 1u];
    char     unit[EXBUS_TM_UNIT_MAX + 1u];
    /* EXBUS_TM_MESSAGE */
    uint8_t  msg_type;        /**< 0 to 255 */
    uint8_t  msg_class;       /**< 0 to 7: 0 information to 4 error */
    uint8_t  msg_len;
    char     message[EXBUS_TM_MESSAGE_MAX + 1u];  /**< UTF-8 as sent */
} exbus_tm_packet_t;

/** CRC8 with polynomial 0x07, initial value 0, of @p n bytes. */
uint8_t exbus_crc8(const uint8_t *data, size_t n);

/** Read one block of @p n bytes, from the 0xNF byte to the CRC8.  @p out
 *  holds the packet on EXBUS_TM_OK and is cleared otherwise. */
exbus_tm_err_t exbus_tm_parse(const uint8_t *block, size_t n,
                              exbus_tm_packet_t *out);

/** One identifier of a device as the table knows it. */
typedef struct {
    uint8_t  id;
    uint8_t  has_label;
    uint8_t  has_value;
    uint8_t  label_len;
    uint8_t  unit_len;
    char     label[EXBUS_TM_LABEL_MAX + 1u];
    char     unit[EXBUS_TM_UNIT_MAX + 1u];
    exbus_tm_value_t value;
    uint32_t value_us;        /**< the time of the last value */
    uint32_t updates;         /**< values taken */
} exbus_tm_sensor_t;

/**
 * The sensors of one device: labels and units from text packets, the last
 * value of each identifier from data packets, the last message.  Labels
 * arrive over successive replies, so an identifier may have a value before
 * it has a label.  A packet from another serial number clears the table.
 */
typedef struct {
    uint8_t  has_device;
    uint16_t manufacturer;
    uint16_t device;
    uint8_t  n;               /**< sensors in use */
    exbus_tm_sensor_t sensors[EXBUS_TM_MAX_SENSORS];
    uint8_t  has_message;
    uint8_t  msg_type;
    uint8_t  msg_class;
    uint8_t  msg_len;
    char     message[EXBUS_TM_MESSAGE_MAX + 1u];
    uint32_t message_us;
    uint32_t packets;         /**< packets taken */
    uint32_t full;            /**< identifiers dropped: no room */
    uint32_t device_changes;  /**< clears by another serial number */
} exbus_tm_table_t;

void exbus_tm_table_init(exbus_tm_table_t *t);

/** Take @p p, received at @p now_us, into @p t. */
void exbus_tm_table_update(exbus_tm_table_t *t, const exbus_tm_packet_t *p,
                           uint32_t now_us);

/** The sensor with identifier @p id, or NULL. */
const exbus_tm_sensor_t *exbus_tm_table_find(const exbus_tm_table_t *t,
                                             uint8_t id);

/**
 * @p v as text into @p buf of @p size bytes, always NUL-terminated when
 * @p size is not 0.  NUMBER "-12.34"; TIME "12:05:09"; DATE "2026-10-15";
 * LATITUDE "N raw 1234" or "S raw ..."; LONGITUDE "E raw ..." or "W raw
 * ..."; RAW "raw 0A 1B".  Returns the characters the whole text has,
 * without the NUL, also when it did not fit.
 */
size_t exbus_tm_format(const exbus_tm_value_t *v, char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* EXBUS_TELEM_H */
