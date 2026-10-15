/**
 * @file dxl_packet.h
 * @brief Protocol 2.0 packets of ROBOTIS Dynamixel servos: the CRC, the
 *        instruction packet encoder with byte stuffing and the packet
 *        parser with destuffing.
 *
 * Implements the published Protocol 2.0 of ROBOTIS Dynamixel servos.
 * ROBOTIS and Dynamixel are names or trademarks of their owner; they are
 * used here only to say which servos this module works with (README.md,
 * "Trademarks").
 *
 * A packet on the wire:
 *
 *   FF FF FD 00   header and reserved byte
 *   ID            0 to 252, 254 broadcast
 *   LEN_L LEN_H   bytes from the instruction through the CRC
 *   INSTR         instruction; 0x55 in a status packet
 *   ERR           status packets only
 *   PARAMS        n bytes
 *   CRC_L CRC_H   CRC-16 over every byte before it
 *
 * Byte stuffing: where FF FF FD occurs in the instruction, the ERR byte or
 * the parameters, an FD follows it on the wire.  Length and CRC count the
 * inserted byte.  The CRC is CRC-16 with polynomial 0x8005, initial value
 * 0, most significant bit first, no final XOR, over the stuffed bytes.
 *
 * The module includes nothing outside the C standard headers, uses no heap
 * and no floating point, and holds no state.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DXL_PACKET_H
#define DXL_PACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Highest ID of a single servo. */
#define DXL_ID_MAX       252u
/** The broadcast ID. 253 and 255 are no ID. */
#define DXL_ID_BROADCAST 254u

/** FF FF FD 00, ID, LEN_L, LEN_H. */
#define DXL_HEADER_SIZE  7u
#define DXL_CRC_SIZE     2u
/** The parameters of one packet after destuffing, the ERR byte not
 *  counted.  A choice of this module: Protocol 2.0 allows more. */
#define DXL_PARAM_MAX    128u
/** Instruction, ERR byte and parameters before stuffing. */
#define DXL_BODY_MAX     (2u + DXL_PARAM_MAX)
/** The longest packet on the wire: stuffing adds at most 1 byte per 3. */
#define DXL_WIRE_MAX \
    (DXL_HEADER_SIZE + DXL_BODY_MAX + DXL_BODY_MAX / 3u + DXL_CRC_SIZE)
/** Bytes of a status packet besides its parameters, before stuffing:
 *  header, instruction, ERR byte and CRC. */
#define DXL_STATUS_OVERHEAD (DXL_HEADER_SIZE + 2u + DXL_CRC_SIZE)
/** The longest read one packet asks for: its reply fits DXL_PARAM_MAX. */
#define DXL_READ_MAX     DXL_PARAM_MAX
/** The longest data of one WRITE or REG_WRITE: address (2) + data. */
#define DXL_WRITE_MAX    (DXL_PARAM_MAX - 2u)

/** The value dxl_factory_reset() takes as its confirmation. */
#define DXL_FACTORY_RESET_CONFIRM 0x46525354u /* "FRST" */

typedef enum {
    DXL_INS_PING           = 0x01,
    DXL_INS_READ           = 0x02,
    DXL_INS_WRITE          = 0x03,
    DXL_INS_REG_WRITE      = 0x04,
    DXL_INS_ACTION         = 0x05,
    DXL_INS_FACTORY_RESET  = 0x06,
    DXL_INS_REBOOT         = 0x08,
    DXL_INS_CLEAR          = 0x10,
    DXL_INS_BACKUP         = 0x20,
    DXL_INS_STATUS         = 0x55,
    DXL_INS_SYNC_READ      = 0x82,
    DXL_INS_SYNC_WRITE     = 0x83,
    DXL_INS_FAST_SYNC_READ = 0x8A,
    DXL_INS_BULK_READ      = 0x92,
    DXL_INS_BULK_WRITE     = 0x93,
    DXL_INS_FAST_BULK_READ = 0x9A,
} dxl_ins_t;

/** Factory Reset options. */
#define DXL_RESET_ALL            0xFFu
#define DXL_RESET_ALL_BUT_ID     0x01u
#define DXL_RESET_ALL_BUT_ID_BAUD 0x02u

/** The ERR byte of a status packet: bit 7 is the alert, bits 6..0 the
 *  error number. */
#define DXL_ERR_ALERT 0x80u
typedef enum {
    DXL_ERRNO_NONE        = 0,
    DXL_ERRNO_RESULT_FAIL = 1, /**< the instruction failed */
    DXL_ERRNO_INSTRUCTION = 2, /**< unknown instruction, or ACTION without REG_WRITE */
    DXL_ERRNO_CRC         = 3, /**< the servo found a CRC error */
    DXL_ERRNO_DATA_RANGE  = 4, /**< data out of the field's range */
    DXL_ERRNO_DATA_LENGTH = 5, /**< data shorter than the field */
    DXL_ERRNO_DATA_LIMIT  = 6, /**< data outside a limit field */
    DXL_ERRNO_ACCESS      = 7, /**< read-only field, or EEPROM with torque on */
} dxl_errno_t;

/** Error number of an ERR byte, bits 6..0. */
static inline uint8_t dxl_err_number(uint8_t err)
{
    return (uint8_t)(err & 0x7Fu);
}

/** The alert bit: the servo has a hardware error set (see dxl_table.h). */
static inline bool dxl_err_alert(uint8_t err)
{
    return (err & DXL_ERR_ALERT) != 0u;
}

/** A packet after parsing and destuffing. */
typedef struct {
    uint8_t  id;
    uint8_t  instr;
    uint8_t  error;    /**< the ERR byte of a status packet; 0 otherwise */
    uint16_t n_params; /**< parameters, after the ERR byte in a status packet */
    uint16_t wire_len; /**< bytes on the wire, header through CRC */
    uint8_t  params[DXL_PARAM_MAX];
} dxl_packet_t;

/** Result of dxl_parse(). */
typedef enum {
    DXL_RX_OK = 0,
    DXL_RX_MORE,          /**< no whole packet in the bytes yet */
    DXL_RX_ERR_ARG,       /**< a NULL argument */
    DXL_RX_ERR_LENGTH,    /**< Length below 3, or below 4 for a status packet */
    DXL_RX_ERR_OVERSIZED, /**< Length beyond DXL_WIRE_MAX, or more than
                               DXL_PARAM_MAX parameters after destuffing */
    DXL_RX_ERR_CRC,       /**< the CRC does not fit */
    DXL_RX_ERR_STUFFING,  /**< FF FF FD in the body not followed by FD */
    DXL_RX_ERR_ID,        /**< ID 253 or 255 */
} dxl_rx_t;

/** CRC-16 of @p n bytes, continued from @p crc (0 for a new packet). */
uint16_t dxl_crc(uint16_t crc, const uint8_t *p, size_t n);

/** Bytes @p n bytes of body take on the wire after stuffing. */
size_t dxl_stuffed_size(const uint8_t *body, size_t n);

/**
 * An instruction packet of any instruction but FACTORY_RESET and STATUS,
 * stuffed, with its CRC, into @p out.  Returns the bytes written; 0, with
 * nothing to send, for an ID other than 0 to 252 and 254, more than
 * DXL_PARAM_MAX parameters, a packet longer than @p cap, a NULL @p out, or
 * NULL @p params with @p n above 0.
 */
size_t dxl_encode(uint8_t id, uint8_t instr, const uint8_t *params, size_t n,
                  uint8_t *out, size_t cap);

/** A status packet, as a servo sends it: for a servo model and the tests.
 *  ID 0 to 252 only.  Returns the bytes written, 0 when refused. */
size_t dxl_encode_status(uint8_t id, uint8_t err, const uint8_t *params,
                         size_t n, uint8_t *out, size_t cap);

/* The instructions a tester sends.  Each returns the bytes written, 0 when
 * refused; see dxl_encode() for the common refusals. */

size_t dxl_ping(uint8_t id, uint8_t *out, size_t cap);
/** @p len 1 to DXL_READ_MAX; @p id not the broadcast ID. */
size_t dxl_read(uint8_t id, uint16_t addr, uint16_t len, uint8_t *out,
                size_t cap);
/** @p n 1 to DXL_WRITE_MAX. */
size_t dxl_write(uint8_t id, uint16_t addr, const uint8_t *data, size_t n,
                 uint8_t *out, size_t cap);
/** As dxl_write(); the servo holds the write until ACTION. */
size_t dxl_reg_write(uint8_t id, uint16_t addr, const uint8_t *data, size_t n,
                     uint8_t *out, size_t cap);
size_t dxl_action(uint8_t id, uint8_t *out, size_t cap);
size_t dxl_reboot(uint8_t id, uint8_t *out, size_t cap);
/**
 * FACTORY_RESET, sent only when asked for by name: @p confirm must be
 * DXL_FACTORY_RESET_CONFIRM, @p option one of DXL_RESET_*, and @p id a
 * single servo (0 to 252).  dxl_encode() refuses the instruction.
 */
size_t dxl_factory_reset(uint8_t id, uint8_t option, uint32_t confirm,
                         uint8_t *out, size_t cap);
/** SYNC_READ of @p len bytes at @p addr from @p n_ids servos (each 0 to
 *  252), sent to the broadcast ID; the replies come in the order of the
 *  IDs. */
size_t dxl_sync_read(uint16_t addr, uint16_t len, const uint8_t *ids,
                     size_t n_ids, uint8_t *out, size_t cap);
/** SYNC_WRITE of @p len bytes at @p addr: @p data holds @p len bytes per
 *  ID, in the order of @p ids.  No servo answers. */
size_t dxl_sync_write(uint16_t addr, uint16_t len, const uint8_t *ids,
                      const uint8_t *data, size_t n_ids, uint8_t *out,
                      size_t cap);

/**
 * Find and check the first packet in @p n bytes of @p buf.
 *
 * Bytes before a header FF FF FD 00 are skipped.  On DXL_RX_OK, @p out holds
 * the packet and @p used the bytes up to its last CRC byte.  On
 * DXL_RX_MORE, @p used is the bytes before a possible header start: the
 * caller may drop them and append more.  On DXL_RX_ERR_CRC and
 * DXL_RX_ERR_STUFFING it is the bytes up to the end of the packet, on
 * DXL_RX_ERR_LENGTH, DXL_RX_ERR_OVERSIZED and DXL_RX_ERR_ID the bytes
 * through the header.  @p used may be NULL.
 *
 * The CRC is checked before destuffing.  A FAST_SYNC_READ or FAST_BULK_READ
 * reply, which is not stuffed and holds several servos, is not parsed.
 */
dxl_rx_t dxl_parse(const uint8_t *buf, size_t n, dxl_packet_t *out,
                   size_t *used);

/* Little-endian fields. */
static inline uint16_t dxl_get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t dxl_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}

static inline void dxl_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static inline void dxl_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)(v >> 24);
}

#ifdef __cplusplus
}
#endif

#endif /* DXL_PACKET_H */
