/**
 * @file
 * @brief The command protocol of a GM861 QR scanner module and the
 *        sequencer that finds the module, sets it and watches it.
 *
 * GM861 is a product name of its maker, named here to describe
 * compatibility only.  The facts are from the maker's "GM861/GM861-LED Bar
 * Code Reader Module User Manual" V1.2.4 (2025-07), chapter 10 and
 * appendix B.
 *
 * Frames.  A request is 7E 00, type, length, address high, address low,
 * data, CRC high, CRC low; types 0x07 read zones, 0x08 write zones, 0x09
 * save the zones to flash, 0x0A heartbeat.  An answer is 02 00 00, length,
 * data, CRC; the manual prints the heartbeat's answer with 03, and either head
 * is taken for every answer.  The CRC is CRC-CCITT:
 * polynomial 0x1021, start value 0, most significant bit first, no final
 * inversion, over the bytes after 7E 00 (request) or 02 00 (answer).  A
 * frame with a wrong CRC, or one not complete 400ms after 7E 00, gets no
 * answer.  A write changes the module's RAM; the save frame stores every
 * zone in flash.
 *
 * Sequence (gm861_step()), at start, after LOST and after gm861_recheck():
 * -# wait GM861_START_MS after the first step: the module starts in 250ms;
 * -# RX low for GM861_LEVEL_MS (the pin's pull-down against a module that
 *    idles high): NOT CONNECTED, again after GM861_RETRY_MS;
 * -# probe: read zone 0x0000 at each rate of gm861_rates() in turn,
 *    GM861_ANSWER_MS for each answer; no answer at any: NO ANSWER, again
 *    after GM861_RETRY_MS;
 * -# read zones 0x0000 to 0x0060 and zone 0x00B0; zone 0x000D bits 1-0 at
 *    01 or 11 (USB output) shows USB OUTPUT while the bench writes 00;
 * -# write each zone that differs from gm861_zone_want(), one frame a zone,
 *    read the zones back, and send one save frame if any zone was written.
 *    A zone still wrong after 2 passes: NO ANSWER with gm861_bad_zone()
 *    naming it, held until gm861_recheck() or gm861_init(), so a module
 *    that refuses a setting does not take a flash write every few seconds;
 * -# READY: a heartbeat every GM861_HEARTBEAT_MS; GM861_LOST_BEATS missed
 *    answers in a row: LOST, and the sequence starts again at step 2.
 * A request in steps 3 to 5 that gets no answer is sent once more; a
 * second miss ends the pass in NO ANSWER, again after GM861_RETRY_MS.
 *
 * The caller owns the UART.  It feeds the module's bytes to a scan_line_t
 * framer and hands each SCAN_OUT_FRAME to gm861_answer().  After each
 * gm861_step() it opens the UART at gm861_baud() if that changed, then
 * sends the bytes the step returned.
 *
 * Pure C, no SDK.  Host-tested in test_gm861 against a simulated module.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @name Timing, in ms
 *  @{ */
#define GM861_START_MS      300u    /**< wait after the first step; the module starts in 250ms */
#define GM861_LEVEL_MS      200u    /**< RX low this long: no module */
#define GM861_ANSWER_MS     300u    /**< wait for an answer, plus the frames' time on the wire */
#define GM861_RETRY_MS      5000u   /**< NOT CONNECTED and NO ANSWER try again after this */
#define GM861_HEARTBEAT_MS  10000u  /**< heartbeat period while READY (the manual's interval) */
/** @} */

/** @brief Heartbeats missed in a row that make a READY module LOST. */
#define GM861_LOST_BEATS 3u
/** @brief Rates the probe tries. */
#define GM861_RATES 8u
/** @brief Zones read in one frame: 0x0000 to 0x0060. */
#define GM861_ZONES 97u
/** @brief The zone outside the block that the bench holds: data cut-out. */
#define GM861_ZONE_CUT 0x00B0u
/** @brief Bytes of every request frame this module builds. */
#define GM861_REQ_LEN 9u
/** @brief gm861_bad_zone() with no zone to name. */
#define GM861_NO_ZONE 0xFFFFu

/** @name Request types
 *  @{ */
#define GM861_T_READ      0x07u  /**< read zones */
#define GM861_T_WRITE     0x08u  /**< write zones */
#define GM861_T_SAVE      0x09u  /**< save every zone to flash */
#define GM861_T_HEARTBEAT 0x0Au  /**< heartbeat */
/** @} */

/** @brief What the bench knows of the module. */
typedef enum {
    GM861_OFF = 0,        /**< SETUP's scanner row is OFF: nothing is sent */
    GM861_NOT_CONNECTED,  /**< RX reads low against the pull-down */
    GM861_NO_ANSWER,      /**< RX idles high, no answer at any rate, or a zone refused */
    GM861_USB_OUTPUT,     /**< answers; zone 0x000D bits 1-0 were not 00 and are being written */
    GM861_SETTING,        /**< reading and writing zones */
    GM861_READY,          /**< the zones read back as wanted */
    GM861_LOST            /**< was READY; GM861_LOST_BEATS heartbeats without an answer */
} gm861_state_t;

/** @brief The read mode the bench sets in zone 0x0000 bits 1-0. */
typedef enum {
    GM861_MODE_SENSING = 0,  /**< 11: the module reads when the picture changes */
    GM861_MODE_CONTINUOUS    /**< 10: the light stays on, a read every 1.0s */
} gm861_mode_t;

/** @brief Where the sequencer is.  Private. */
typedef enum {
    GM861_P_START = 0,  /**< waiting GM861_START_MS */
    GM861_P_LEVEL,      /**< watching RX */
    GM861_P_WAIT,       /**< waiting for the next try */
    GM861_P_PROBE,      /**< reading zone 0x0000 at each rate */
    GM861_P_READ,       /**< reading the block */
    GM861_P_READ_CUT,   /**< reading zone 0x00B0 */
    GM861_P_WRITE,      /**< writing the zones that differ */
    GM861_P_SAVE,       /**< saving to flash */
    GM861_P_BEAT,       /**< READY, heartbeats */
    GM861_P_RESET,      /**< sending the factory reset */
    GM861_P_HELD        /**< a zone refused: waiting for gm861_recheck() */
} gm861_phase_t;

/** @brief The sequencer.  Fields are private; read them through the functions. */
typedef struct {
    gm861_state_t state;               /**< what gm861_state() reports */
    gm861_phase_t phase;               /**< where the sequence is */
    gm861_mode_t  mode;                /**< the read mode wanted */
    bool          started;             /**< the first step has been taken */
    bool          sent;                /**< a request is out and its answer is due */
    bool          answered;            /**< the answer to the request arrived */
    bool          usb;                 /**< this pass found zone 0x000D at USB output */
    bool          verify;              /**< this read is the read back after writes */
    bool          kick;                /**< start again at step 2 on the next step */
    bool          ack;                 /**< the answer due carries data 0x00 */
    uint8_t       expect;              /**< data bytes of the answer due */
    uint8_t       tries;               /**< sends of the request out */
    uint8_t       probed;              /**< rates tried in this probe */
    uint8_t       passes;              /**< write passes of this sequence */
    uint8_t       rate;                /**< index into gm861_rates() */
    uint8_t       missed;              /**< heartbeats missed in a row */
    uint8_t       wrote;               /**< zones written in this sequence */
    uint16_t      at;                  /**< the zone the write pass is at */
    uint16_t      bad_zone;            /**< the zone refused, or GM861_NO_ZONE */
    uint32_t      t0;                  /**< when the phase or the request began */
    uint32_t      due;                 /**< when the answer is late, or the next try is due */
    uint8_t       zones[GM861_ZONES];  /**< zones 0x0000 to 0x0060 as last read */
    uint8_t       cut;                 /**< zone 0x00B0 as last read */
    uint8_t       found[GM861_ZONES];  /**< zones 0x0000 to 0x0060 as read before the first write */
    uint8_t       found_cut;           /**< zone 0x00B0 as read before the first write */
    uint8_t       answer[GM861_ZONES]; /**< the data of the last answer */
    uint8_t       answer_len;          /**< bytes in @c answer */
    uint32_t      writes;              /**< zone writes sent, wraps */
    uint32_t      saves;               /**< save frames sent, wraps */
} gm861_t;

/**
 * @brief The manual's CRC-CCITT: polynomial 0x1021, start 0, MSB first.
 * @param p Bytes; NULL with @p n 0 gives 0.
 * @param n Number of bytes.
 * @return The CRC.
 */
uint16_t gm861_crc(const uint8_t *p, size_t n);

/**
 * @brief Build a request frame of one data byte.
 * @param out  GM861_REQ_LEN bytes.
 * @param type GM861_T_READ, GM861_T_WRITE, GM861_T_SAVE or GM861_T_HEARTBEAT.
 * @param addr The zone address; 0 for save and heartbeat.
 * @param data The count to read (0 means 256), the value to write, or 0.
 * @return GM861_REQ_LEN.
 */
size_t gm861_request(uint8_t out[GM861_REQ_LEN], uint8_t type, uint16_t addr,
                     uint8_t data);

/**
 * @brief Check an answer frame: head 02 00 or 03 00, type 00, length, CRC.
 * @param frame The frame from its first byte.
 * @param len   Its length.
 * @param data  Receives where the data starts; may be NULL.
 * @return The data length, or -1 for a frame that is not a good answer.
 */
int gm861_answer_data(const uint8_t *frame, size_t len, const uint8_t **data);

/**
 * @brief The rates the probe tries, in its order, in baud.
 * @return GM861_RATES rates: 9600, 57600, 115200, 38400, 19200, 14400, 4800, 1200.
 */
const uint32_t *gm861_rates(void);

/**
 * @brief The value the bench wants in a zone, given the value found.
 * @param addr  The zone.
 * @param found The value read from the module.
 * @param mode  The read mode wanted.
 * @return @p found with the bench's bits set; @p found for a zone the bench does not hold.
 */
uint8_t gm861_zone_want(uint16_t addr, uint8_t found, gm861_mode_t mode);

/**
 * @brief Start the sequence: state NOT CONNECTED until the module is seen.
 * @param g    The sequencer; NULL does nothing.
 * @param mode The read mode to set.
 */
void gm861_init(gm861_t *g, gm861_mode_t mode);

/**
 * @brief Stop: state OFF, nothing more is sent until gm861_init().
 * @param g The sequencer; NULL does nothing.
 */
void gm861_stop(gm861_t *g);

/**
 * @brief Read the zones again and put back what differs, from step 2.
 *
 * Also ends the hold after a zone refused.  Does nothing while OFF.
 *
 * @param g The sequencer; NULL does nothing.
 */
void gm861_recheck(gm861_t *g);

/**
 * @brief Change the read mode; a READY module is rechecked.
 * @param g    The sequencer; NULL does nothing.
 * @param mode The read mode.
 */
void gm861_set_mode(gm861_t *g, gm861_mode_t mode);

/**
 * @brief Send the factory reset (zone 0x00D9 = 0x50), then run the sequence.
 *
 * Does nothing while OFF.
 *
 * @param g The sequencer; NULL does nothing.
 */
void gm861_factory_reset(gm861_t *g);

/**
 * @brief One step of the sequence; call it every few ms.
 * @param g       The sequencer; NULL sends nothing.
 * @param now_ms  The millisecond clock.
 * @param rx_high The RX pin reads high (a module idles high).
 * @param tx      Receives the bytes to send.
 * @param tx_max  Room in @p tx; less than GM861_REQ_LEN sends nothing.
 * @return Bytes to send, 0 or GM861_REQ_LEN.
 */
size_t gm861_step(gm861_t *g, uint32_t now_ms, bool rx_high, uint8_t *tx,
                  size_t tx_max);

/**
 * @brief Take an answer frame the framer handed out.
 * @param g     The sequencer; NULL does nothing.
 * @param frame The frame (scan_line_frame()).
 * @param len   Its length.
 */
void gm861_answer(gm861_t *g, const uint8_t *frame, size_t len);

/**
 * @brief What the bench knows of the module.
 * @param g The sequencer; NULL is OFF.
 * @return The state.
 */
gm861_state_t gm861_state(const gm861_t *g);

/**
 * @brief The rate to open the UART at; changes while probing.
 * @param g The sequencer; NULL gives 9600.
 * @return The rate in baud.
 */
uint32_t gm861_baud(const gm861_t *g);

/**
 * @brief The zone that read back wrong twice.
 * @param g The sequencer.
 * @return The zone, or GM861_NO_ZONE.
 */
uint16_t gm861_bad_zone(const gm861_t *g);

/**
 * @brief Zones 0x0000 to 0x0060 as read before the first write of the last pass.
 * @param g   The sequencer.
 * @param cut Receives zone 0x00B0 as read; may be NULL.
 * @return GM861_ZONES bytes.
 */
const uint8_t *gm861_found(const gm861_t *g, uint8_t *cut);

#ifdef __cplusplus
}
#endif
