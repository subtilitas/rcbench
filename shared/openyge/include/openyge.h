/*
 * The OpenYGE ESC protocol: framing, telemetry, status and parameters.
 *
 * Written from docs/OpenYGE.md, which is the specification of record for this
 * project.  Anything that page marks as inferred is inferred here too, and the
 * places where that matters are commented rather than smoothed over.
 *
 * Three things this deliberately does not do:
 *
 *   - It does not touch a UART.  Bytes in, decoded frames out, the way the
 *     panel link's codec works, so the host suite can feed it truncated and
 *     corrupted frames long before there is an ESC on the bench.
 *   - It does not cast the receive buffer to a struct.  The wire is
 *     little-endian and packed; a host need be neither, and the reference
 *     implementation's unaligned 16-bit reads are undefined behaviour on a
 *     target that cares.  Everything here is assembled byte by byte.
 *   - It does not write parameters.  Reading is safe and writing is not, and
 *     until the indices are confirmed the write path has no business existing.
 *     Building a request is here; deciding to send one is not.
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

#define OPENYGE_SYNC            0xA5u
#define OPENYGE_VERSION         3u      /**< the version this speaks */

#define OPENYGE_HDR_V3          6u
#define OPENYGE_HDR_LEGACY      4u      /**< pre-v3: no seq, no device */
#define OPENYGE_CRC_BYTES       2u

#define OPENYGE_TELEMETRY_BYTES 26u
#define OPENYGE_CONTROL_BYTES   4u

/** The shortest thing that could be a frame: legacy header plus CRC. */
#define OPENYGE_MIN_FRAME       (OPENYGE_HDR_LEGACY + OPENYGE_CRC_BYTES)
#define OPENYGE_MAX_FRAME       140u

/*
 * CRC-16/XMODEM.  Same polynomial as the panel link's CCITT-FALSE and a
 * different seed, which is the whole difference -- so link_crc() serves both
 * and only the seed changes.  The check value is over "123456789" and is what
 * an implementation at the far end can be verified against before it is
 * trusted.
 */
#define OPENYGE_CRC_INIT        0x0000u
#define OPENYGE_CRC_CHECK       0x31C3u

/** Address byte: bit 7 set means the sender is the master. */
#define OPENYGE_DEV_MASTER      0x80u
#define OPENYGE_DEV_ADDR_MASK   0x7Fu
#define OPENYGE_DEV_BROADCAST   0x00u

/** Temperatures travel as °C + 40, so −40 is representable in a uint8. */
#define OPENYGE_TEMP_OFFSET     40

typedef enum {
    OPENYGE_FT_TELE_AUTO        = 0x00, /**< ESC pushes, unsolicited */
    OPENYGE_FT_TELE_RESP        = 0x02, /**< ESC answers a request */
    OPENYGE_FT_TELE_REQ         = 0x03, /**< master asks */
    OPENYGE_FT_WRITE_PARAM_RESP = 0x04, /**< ESC acknowledges a write */
    OPENYGE_FT_WRITE_PARAM_REQ  = 0x05, /**< master writes one parameter */
} openyge_frame_type_t;

/* ------------------------------------------------------------ decoding */

typedef struct {
    uint8_t version;
    uint8_t type;
    uint8_t seq;      /**< zero on a legacy frame, which carries none */
    uint8_t device;   /**< zero on a legacy frame */
    bool    legacy;   /**< the header was four bytes, not six */
} openyge_header_t;

typedef struct {
    openyge_header_t hdr;
    uint8_t payload[OPENYGE_MAX_FRAME];
    uint8_t payload_len;
} openyge_frame_t;

typedef struct {
    uint8_t  buf[OPENYGE_MAX_FRAME];
    uint8_t  len;
    /* Counters worth showing on the analyser screen: the difference between a
     * quiet line, a noisy one and a wrong baud rate is visible here and
     * nowhere else. */
    uint32_t frames;
    uint32_t crc_errors;
    uint32_t resyncs;
    uint32_t rejected;   /**< CRC held, contents impossible */
} openyge_decoder_t;

void openyge_decoder_reset(openyge_decoder_t *d);

/**
 * Feed one received byte; returns true when @p out holds a whole good frame.
 *
 * Resynchronising, and for the reason the panel link's decoder is: noise
 * carrying a plausible sync in front of a real frame must not swallow the real
 * one. Every sync byte in the buffer is a candidate and the earliest complete
 * candidate wins.
 */
bool openyge_decode_byte(openyge_decoder_t *d, uint8_t byte,
                         openyge_frame_t *out);

/* ------------------------------------------------------------ telemetry */

typedef struct {
    float    volts;
    float    amps;
    uint16_t consumption_mah;
    uint32_t erpm;
    int8_t   pwm_pct;        /**< output duty */
    int8_t   throttle_pct;   /**< input setpoint */
    float    bec_volts;
    float    bec_amps;
    int16_t  temp_c;
    int16_t  bec_temp_c;
    int16_t  cap_temp_c;
    int16_t  aux_temp_c;
    uint8_t  status1;
    uint8_t  status2;        /**< undocumented: carried, never interpreted */
    uint16_t pidx;
    uint16_t pdata;
} openyge_telemetry_t;

/** True if this frame is one an ESC sends, i.e. one carrying telemetry. */
bool openyge_frame_has_telemetry(const openyge_frame_t *f);

/** Parse the telemetry payload. False if the frame does not carry one. */
bool openyge_telemetry_parse(const openyge_frame_t *f,
                             openyge_telemetry_t *out);

/** Parse the 4-byte control payload of a request. */
bool openyge_control_parse(const openyge_frame_t *f, uint16_t *index,
                           uint16_t *param);

/* ------------------------------------------------------------- encoding */

/**
 * Build a frame. Returns its length, or 0 if it would not fit or the type is
 * not one this speaks.
 *
 * Length is written once, at the end, from what was actually assembled --
 * never set before a bounds check that might still fail. The reference gets
 * that wrong and transmits a length claiming more than it wrote.
 */
size_t openyge_encode(uint8_t *out, size_t cap, uint8_t type, uint8_t device,
                      uint8_t seq, const void *payload, size_t payload_len);

/** A telemetry request: the control payload, both words zero. */
size_t openyge_build_telemetry_request(uint8_t *out, size_t cap,
                                       uint8_t device, uint8_t seq);

/** A single parameter write. One parameter per frame; there is no page write. */
size_t openyge_build_param_write(uint8_t *out, size_t cap, uint8_t device,
                                 uint8_t seq, uint16_t index, uint16_t value);

/* ---------------------------------------------------------------- status */

/** Which device the warning bits are talking about. */
typedef enum {
    OPENYGE_SUBJECT_ESC = 0,
    OPENYGE_SUBJECT_BEC,
} openyge_subject_t;

/* The motor states.  Reserved values are not listed and must not be mapped
 * onto a neighbour; show the number. */
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

typedef struct {
    uint8_t state;              /**< the low nibble, whatever it is */
    bool    state_known;        /**< false for a reserved value */

    /*
     * The overloaded case.  0x80|0x40 reads as "BEC over-current", which
     * cannot happen, so that exact combination means the ESC is reporting a
     * dirty *input signal* instead.  When this is set the warning flags below
     * and the subject are meaningless.
     */
    bool    setpoint_noise;

    openyge_subject_t subject;  /**< who the warnings are about */
    bool    warn_undervoltage;
    bool    warn_overtemp;
    bool    warn_overcurrent;

    /*
     * A warning bit alone is a caution; it is a fault only in combination
     * with the state.  Over-voltage has no bit of its own at all -- it is the
     * *absence* of warnings while the power is cut -- so a decoder that
     * reports flags without the state both cries wolf and misses the one
     * condition that has no flag.
     */
    bool    fault_overvoltage;
    bool    fault_undervoltage;
    bool    fault_overtemp;
    bool    fault_overcurrent;
    bool    any_fault;
} openyge_status_t;

void openyge_status_decode(uint8_t status1, openyge_status_t *out);

/** A short name for a motor state, or "?" for a reserved value. */
const char *openyge_state_name(uint8_t state);

/* ------------------------------------------------------------ parameters */

/** The bitmap that tracks which indices have been seen is 64 bits wide. */
#define OPENYGE_MAX_PARAMS 64

/** Indices this project relies on.  Everything else is carried, not used --
 *  and even these want confirming before anything is written. */
#define OPENYGE_P_COUNT        0
#define OPENYGE_P_MOTOR_POLES  20
#define OPENYGE_P_PINION_TEETH 21
#define OPENYGE_P_MAIN_TEETH   22

typedef struct {
    uint16_t value[OPENYGE_MAX_PARAMS];
    uint64_t seen;            /**< bit per index */
    /* Parameter 0, once it has arrived.  Zero means "not yet": a count of
     * zero is not a table anyone could have, so it needs no separate flag. */
    uint16_t count;
    bool     writes_pending;  /**< table withdrawn until every index re-read */
} openyge_params_t;

void openyge_params_reset(openyge_params_t *p);

/**
 * Record one (index, value) pair from a telemetry frame.
 *
 * Ignored entirely while writes are outstanding: a frame already in flight
 * from before a write would otherwise land on top of the new value and the
 * ESC would look like it had ignored the write.
 */
void openyge_params_observe(openyge_params_t *p, uint16_t index,
                            uint16_t value);

/** Mark writes outstanding, which withdraws the table until it is re-read. */
void openyge_params_begin_writes(openyge_params_t *p);
void openyge_params_end_writes(openyge_params_t *p);

/** True once every index 0..count-1 has been seen and no write is pending. */
bool openyge_params_complete(const openyge_params_t *p);

/** Read one. False unless the whole table is complete: a half-read table is
 *  not the ESC's settings, and presenting it as such is the trap. */
bool openyge_params_get(const openyge_params_t *p, uint16_t index,
                        uint16_t *out);

/** 0..1, for a progress bar during the ~1.6 s it takes to fill. */
float openyge_params_progress(const openyge_params_t *p);

/**
 * Mechanical and head RPM from eRPM and the gear train.
 *
 * False unless the table is complete and the pole count is sane. Head speed
 * needs the pinion and main gear too; when those are absent or zero the head
 * figure is left alone and only the motor figure is written.
 */
bool openyge_motor_rpm(const openyge_params_t *p, uint32_t erpm,
                       float *motor_rpm);
bool openyge_head_rpm(const openyge_params_t *p, uint32_t erpm,
                      float *head_rpm);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_OPENYGE_H */
