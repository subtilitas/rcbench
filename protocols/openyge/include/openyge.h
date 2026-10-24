/**
 * @file
 * @brief The OpenYGE ESC (electronic speed controller) protocol: framing,
 *        telemetry, status and parameters.
 *
 * Implements the OpenYGE telemetry and parameter protocol of YGE ESCs.  YGE
 * and OpenYGE are names or trademarks of their respective owners; they are
 * used here only to say which ESCs this module works with (NOTICE,
 * "Trademarks").
 *
 * Written from docs/OpenYGE.md, the specification of record.  Anything that
 * page marks as inferred is inferred here too, and each such place carries a
 * comment.
 *
 * Boundaries of this module:
 *
 *   - No UART (universal asynchronous receiver-transmitter) access.  Bytes
 *     in, decoded frames out, so the host suite feeds it truncated and
 *     corrupted frames without an ESC on the bench.
 *   - No struct cast over the receive buffer.  The wire is little-endian and
 *     packed; a host need be neither, and an unaligned 16-bit read is
 *     undefined behaviour on a strict-alignment target.  Every field is
 *     assembled byte by byte.
 *   - No parameter writes.  Reading is safe; writing is not until the
 *     parameter indices are confirmed.  Building a write request is here;
 *     deciding to send one is not.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_OPENYGE_H
#define RCBENCH_OPENYGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------- the wire */

#define OPENYGE_SYNC            0xA5u   /**< first byte of every frame        */
#define OPENYGE_VERSION         3u      /**< the version this speaks; a frame
                                             *   of version 3 or above is read
                                             *   with the 6-byte header */

#define OPENYGE_HDR_V3          6u      /**< header bytes, version 3 and up   */
#define OPENYGE_HDR_LEGACY      4u      /**< pre-v3: no seq, no device */
#define OPENYGE_CRC_BYTES       2u      /**< CRC trailer, little-endian       */

#define OPENYGE_TELEMETRY_BYTES 26u     /**< telemetry payload length         */
#define OPENYGE_CONTROL_BYTES   4u      /**< control payload length           */

/**
 * @brief The shortest possible frame: legacy header plus CRC (cyclic redundancy
 *        check).
 */
#define OPENYGE_MIN_FRAME       (OPENYGE_HDR_LEGACY + OPENYGE_CRC_BYTES)
#define OPENYGE_MAX_FRAME       140u    /**< largest frame, in bytes          */

/**
 * @brief CRC-16/XMODEM: polynomial 0x1021, no reflection, no final XOR, seed
 *        0x0000.
 *
 * The check value is over the 9 bytes of the ASCII string "123456789"; an
 * implementation at the far end is verified against it before it is trusted.
 */
#define OPENYGE_CRC_INIT        0x0000u
#define OPENYGE_CRC_CHECK       0x31C3u

/**
 * @brief Fold @p len bytes at @p data into @p crc and return the new
 *        accumulator.
 *
 * A frame's CRC is openyge_crc(OPENYGE_CRC_INIT, frame, total - 2), stored
 * little-endian in the frame's last 2 bytes.  Incremental: folding a buffer
 * in pieces gives what folding it whole gives.  A @p len of 0 returns
 * @p crc and reads nothing.
 *
 * @param crc   Accumulator from an earlier call, or OPENYGE_CRC_INIT.
 * @param data  Bytes to fold.
 * @param len   Number of bytes in @p data.
 * @return      The new accumulator.
 */
uint16_t openyge_crc(uint16_t crc, const void *data, size_t len);

/** @brief Address byte: bit 7 set means the sender is the master. */
#define OPENYGE_DEV_MASTER      0x80u
#define OPENYGE_DEV_ADDR_MASK   0x7Fu
#define OPENYGE_DEV_BROADCAST   0x00u

/**
 * @brief Temperatures travel as °C + 40, so −40 is representable in a uint8.
 */
#define OPENYGE_TEMP_OFFSET     40

/** @brief Frame type codes, the type byte of the header. */
typedef enum {
    OPENYGE_FT_TELE_AUTO        = 0x00, /**< ESC pushes, unsolicited */
    OPENYGE_FT_TELE_RESP        = 0x02, /**< ESC answers a request */
    OPENYGE_FT_TELE_REQ         = 0x03, /**< master asks */
    OPENYGE_FT_WRITE_PARAM_RESP = 0x04, /**< ESC acknowledges a write */
    OPENYGE_FT_WRITE_PARAM_REQ  = 0x05, /**< master writes one parameter */
} openyge_frame_type_t;

/* ------------------------------------------------------------ decoding */

typedef struct {
    uint8_t version;  /**< protocol version byte                             */
    uint8_t type;     /**< one of openyge_frame_type_t                       */
    uint8_t seq;      /**< zero on a legacy frame, which carries none */
    uint8_t device;   /**< zero on a legacy frame */
    bool    legacy;   /**< the header was four bytes, not six */
} openyge_header_t;

/** @brief One decoded frame: header and payload. */
typedef struct {
    openyge_header_t hdr;
    uint8_t payload[OPENYGE_MAX_FRAME];  /**< payload bytes, after the header */
    uint8_t payload_len;                 /**< payload length in bytes         */
} openyge_frame_t;

/** @brief Byte buffer and counters of the resynchronising decoder. */
typedef struct {
    uint8_t  buf[OPENYGE_MAX_FRAME];
    uint8_t  len;
    /* Counters for the analyser screen: a quiet line, a noisy one and a
     * wrong baud rate are distinguished here.  crc_errors and rejected count
     * a failing sync candidate once, when it reaches the front of the
     * buffer; bytes dropped ahead of a candidate count under resyncs. */
    uint32_t frames;
    uint32_t crc_errors;
    uint32_t resyncs;
    uint32_t rejected;   /**< CRC held, contents impossible */
} openyge_decoder_t;

/**
 * @brief Clear a decoder, buffer and counters.
 *
 * @param d  Decoder to clear; NULL does nothing.
 */
void openyge_decoder_reset(openyge_decoder_t *d);

/**
 * @brief Feed one received byte; returns true when @p out holds a whole good
 *        frame.
 *
 * Resynchronising: noise carrying a plausible sync byte in front of a real
 * frame must not swallow the real one.  Every sync byte in the buffer is a
 * candidate and the earliest complete candidate wins.
 *
 * @param d     Decoder state.
 * @param byte  Received byte.
 * @param out   Receives the frame when true is returned.
 * @return      true when a whole good frame was decoded.
 */
bool openyge_decode_byte(openyge_decoder_t *d, uint8_t byte,
                         openyge_frame_t *out);

/* ------------------------------------------------------------ telemetry */

typedef struct {
    float    volts;          /**< battery volts, from 10mV counts           */
    float    amps;           /**< battery amps, from 10mA counts            */
    uint16_t consumption_mah; /**< consumed charge, mAh                    */
    uint32_t erpm;           /**< electrical rpm, the wire value times 10   */
    int8_t   pwm_pct;        /**< output duty */
    int8_t   throttle_pct;   /**< input setpoint */
    float    bec_volts;      /**< BEC volts, from mV                        */
    float    bec_amps;       /**< BEC amps, from mA                         */
    int16_t  temp_c;         /**< ESC temperature, °C          */
    int16_t  bec_temp_c;     /**< BEC temperature, °C          */
    int16_t  cap_temp_c;     /**< capacitor temperature, °C    */
    int16_t  aux_temp_c;     /**< auxiliary temperature, °C    */
    uint8_t  status1;        /**< status byte, see openyge_status_decode() */
    uint8_t  status2;        /**< undocumented: carried, never interpreted */
    uint16_t pidx;           /**< parameter index in this frame             */
    uint16_t pdata;          /**< parameter value in this frame             */
} openyge_telemetry_t;

/**
 * @brief True if this frame is one an ESC sends, i.e. one carrying telemetry.
 *
 * @param f  Decoded frame.
 * @return   true for an ESC frame with a telemetry-length payload.
 */
bool openyge_frame_has_telemetry(const openyge_frame_t *f);

/**
 * @brief Parse the telemetry payload.
 *
 * False if the frame does not carry one.
 *
 * @param f    Decoded frame.
 * @param out  Receives the telemetry.
 * @return     true when parsed; false if the frame does not carry telemetry or
 *             @p out is NULL.
 */
bool openyge_telemetry_parse(const openyge_frame_t *f,
                             openyge_telemetry_t *out);

/**
 * @brief Parse the 4-byte control payload of a request.
 *
 * @param f      Decoded frame.
 * @param index  Receives the parameter index; NULL to skip.
 * @param param  Receives the parameter value; NULL to skip.
 * @return       true when parsed; false for a telemetry frame or a payload of
 *               the wrong length.
 */
bool openyge_control_parse(const openyge_frame_t *f, uint16_t *index,
                           uint16_t *param);

/* ------------------------------------------------------------- encoding */

/**
 * @brief Build a frame.
 *
 * Returns its length, or 0 if it would not fit or the type is not one this
 * speaks.
 *
 * The length byte is written from what was assembled, after the bounds
 * check, so a frame that does not fit leaves the buffer untouched.
 *
 * @param out          Buffer to write the frame into.
 * @param cap          Size of @p out in bytes.
 * @param type         One of openyge_frame_type_t.
 * @param device       Device address, without the master bit.
 * @param seq          Sequence number.
 * @param payload      Payload bytes.
 * @param payload_len  Payload length; must match the type's payload size.
 * @return             Length of the frame, or 0 if it would not fit or the
 *                     type is not one this speaks.
 */
size_t openyge_encode(uint8_t *out, size_t cap, uint8_t type, uint8_t device,
                      uint8_t seq, const void *payload, size_t payload_len);

/**
 * @brief A telemetry request: the control payload, both words zero.
 *
 * @param out     Buffer to write the frame into.
 * @param cap     Size of @p out in bytes.
 * @param device  Device address, without the master bit.
 * @param seq     Sequence number.
 * @return        Length of the frame, or 0 if it does not fit.
 */
size_t openyge_build_telemetry_request(uint8_t *out, size_t cap,
                                       uint8_t device, uint8_t seq);

/**
 * @brief A single parameter write.
 *
 * One parameter per frame; there is no page write.
 *
 * @param out     Buffer to write the frame into.
 * @param cap     Size of @p out in bytes.
 * @param device  Device address, without the master bit.
 * @param seq     Sequence number.
 * @param index   Parameter index.
 * @param value   Value to write.
 * @return        Length of the frame, or 0 if it does not fit.
 */
size_t openyge_build_param_write(uint8_t *out, size_t cap, uint8_t device,
                                 uint8_t seq, uint16_t index, uint16_t value);

/* ---------------------------------------------------------------- status */

/** @brief Which device the warning bits are talking about. */
typedef enum {
    OPENYGE_SUBJECT_ESC = 0,
    OPENYGE_SUBJECT_BEC,
} openyge_subject_t;

/**
 * @brief The motor states.
 *
 * Reserved values are not listed and must not be mapped onto a neighbour; show
 * the number.
 */
typedef enum {
    OPENYGE_ST_DISARMED          = 0x0,
    OPENYGE_ST_POWER_CUT         = 0x1,
    OPENYGE_ST_FAST_START        = 0x2,
    OPENYGE_ST_ALIGN_FOR_POS     = 0x4,
    OPENYGE_ST_BRAKING_NORM_FINI = 0x6,
    OPENYGE_ST_BRAKING_SYNC_FINI = 0x7,
    OPENYGE_ST_STARTING          = 0x8,
    OPENYGE_ST_BRAKING_NORM      = 0x9,
    OPENYGE_ST_BRAKING_SYNC      = 0xA,
    OPENYGE_ST_WINDMILLING       = 0xC,
    OPENYGE_ST_RUNNING_NORM      = 0xE,
} openyge_state_t;

/** @brief Decoded status byte. */
typedef struct {
    uint8_t state;              /**< the low nibble, whatever it is */
    bool    state_known;        /**< false for a reserved value */

    /*
     * The overloaded combination.  0x80|0x40 would read as a BEC (battery
     * eliminator circuit) over-current, which cannot occur, so that exact
     * value means the ESC reports a dirty input signal instead.  While this
     * is set, the subject and the warning flags below carry no meaning.
     */
    bool    setpoint_noise;

    openyge_subject_t subject;  /**< who the warnings are about */
    bool    warn_undervoltage;
    bool    warn_overtemp;
    bool    warn_overcurrent;

    /*
     * A warning bit alone is a caution; it is a fault only in combination
     * with the state.  Over-voltage has no bit of its own: it is the absence
     * of warnings while the power is cut.  A decoder that reports flags
     * without the state misses that condition.
     */
    bool    fault_overvoltage;
    bool    fault_undervoltage;
    bool    fault_overtemp;
    bool    fault_overcurrent;
    bool    any_fault;
} openyge_status_t;

/**
 * @brief Decode the status byte of a telemetry frame.
 *
 * @param status1  The status byte.
 * @param out      Receives the decoded status; NULL does nothing.
 */
void openyge_status_decode(uint8_t status1, openyge_status_t *out);

/**
 * @brief A short name for a motor state, or "?" for a reserved value.
 *
 * @param state  State value; only the low nibble is used.
 * @return       The name, or "?" for a reserved value.
 */
const char *openyge_state_name(uint8_t state);

/* ------------------------------------------------------------ parameters */

/**
 * @brief The bitmap that tracks which indices have been seen is 64 bits wide.
 */
#define OPENYGE_MAX_PARAMS 64

/**
 * @brief Indices this project relies on.
 *
 * Every other index is carried, not used.  These are unconfirmed too; confirm
 * them before anything is written.
 */
#define OPENYGE_P_COUNT        0
#define OPENYGE_P_MOTOR_POLES  20
#define OPENYGE_P_PINION_TEETH 21
#define OPENYGE_P_MAIN_TEETH   22

typedef struct {
    uint16_t value[OPENYGE_MAX_PARAMS];  /**< value per index                 */
    uint64_t seen;            /**< bit per index */
    /* Parameter 0.  Zero means the count has not arrived: a table of zero
     * parameters cannot exist, so no separate flag is needed. */
    uint16_t count;
    bool     writes_pending;  /**< table withdrawn until every index re-read */
} openyge_params_t;

/**
 * @brief Clear the parameter cache.
 *
 * @param p  Cache to clear; NULL does nothing.
 */
void openyge_params_reset(openyge_params_t *p);

/**
 * @brief Record one (index, value) pair from a telemetry frame.
 *
 * Ignored while writes are outstanding: a frame already in flight from
 * before a write carries the old value and would overwrite the new one.
 *
 * @param p      Cache to update.
 * @param index  Parameter index, below OPENYGE_MAX_PARAMS; other values are
 *               ignored.
 * @param value  Parameter value.
 */
void openyge_params_observe(openyge_params_t *p, uint16_t index,
                            uint16_t value);

/**
 * @brief Mark writes outstanding, which withdraws the table until it is
 *        re-read.
 *
 * @param p  Cache to update; NULL does nothing.
 */
void openyge_params_begin_writes(openyge_params_t *p);
/**
 * @brief Clear the outstanding-writes mark.
 *
 * @param p  Cache to update; NULL does nothing.
 */
void openyge_params_end_writes(openyge_params_t *p);

/**
 * @brief True once every index 0..count-1 has been seen and no write is
 *        pending.
 *
 * @param p  Cache to test.
 * @return   true when the whole table is in the cache.
 */
bool openyge_params_complete(const openyge_params_t *p);

/**
 * @brief Read one.
 *
 * False unless the whole table is complete: a half-read table is not the ESC's
 * settings.
 *
 * @param p      Cache to read.
 * @param index  Parameter index, below the table count.
 * @param out    Receives the value.
 * @return       true when the value was read.
 */
bool openyge_params_get(const openyge_params_t *p, uint16_t index,
                        uint16_t *out);

/**
 * @brief 0..1, for a progress bar.
 *
 * The table fills in about 1.6s at 20Hz and  32 parameters.
 *
 * @param p  Cache to read.
 * @return   0..1; 0 while writes are pending, or while the count is 0 or
 *           above OPENYGE_MAX_PARAMS.
 */
float openyge_params_progress(const openyge_params_t *p);

/**
 * @brief Mechanical and head RPM (revolutions per minute) from eRPM (electrical
 *        RPM) and the gear train.
 *
 * False unless the table is complete and the pole count is at least 2.  Head
 * speed needs the pinion and main gear teeth too; when either is zero the
 * head figure is not written.
 *
 * @param p          Cache with the pole count.
 * @param erpm       Electrical rpm.
 * @param motor_rpm  Receives the motor rpm.
 * @return           true when the value was written.
 */
bool openyge_motor_rpm(const openyge_params_t *p, uint32_t erpm,
                       float *motor_rpm);
/**
 * @brief Head rpm from eRPM and the gear train.
 *
 * @param p        Cache with the pole count and both gear teeth counts.
 * @param erpm     Electrical rpm.
 * @param head_rpm Receives the head rpm.
 * @return         true when the value was written.
 */
bool openyge_head_rpm(const openyge_params_t *p, uint32_t erpm,
                      float *head_rpm);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_OPENYGE_H */
