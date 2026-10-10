/*
 * KST servo programming protocol: frames and the reply decoder.
 *
 * The protocol runs half duplex on the servo's signal wire.  The line idles
 * low.  Bits are Manchester coded, most significant bit first: a 1 is a low
 * half-cell followed by a high half-cell, a 0 is high followed by low.  Every
 * frame starts with a 1 bit, so its first visible event is a rising edge.
 * The master sends 27 bits, the servo answers with 11.  There is no parity,
 * no checksum and no length field; the plausibility checks of the decoder
 * stand in for them.
 *
 * This file has no hardware access.  A frame is handed to a driver as a
 * sequence of half-cell levels to clock out at KST_HALF_CELL_NS each.  The
 * driver hands back the reply window as edge timestamps.
 *
 * Capture representation: edge timestamps, not fixed-rate samples.
 *   - A reply has at most 23 edges.  A capture is 200 bytes, against 2150
 *     bytes for the 8 ms window after a write sampled at 2 MHz.
 *   - The decoder does not depend on the driver's sampling rate.  A PIO
 *     (programmable input/output) program that counts cycles between edges
 *     and a DMA (direct memory access) sample buffer reduced to edges both
 *     fit.
 *   - The glitch rule is applied here, on the timestamps, so every driver
 *     gets the same rule.
 *
 * Measured on 1 programming card and 1 servo of unidentified model.  The
 * accept windows are set around that servo.
 *
 * The module includes nothing outside the C standard headers and holds no
 * global state.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KST_WIRE_H
#define KST_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- registers ------------------------------------------------------------ */

/** The servo decodes 5 address bits: registers 0x00 to 0x1F. */
#define KST_REG_COUNT 32u
#define KST_REG_MAX   0x1Fu

/* --- bit timing ----------------------------------------------------------- */

/** Transmit half-cell, 25.40 us.  The only rate the servo is proven to
 *  accept.  Required accuracy: 50 ns, edge jitter below 100 ns. */
#define KST_HALF_CELL_NS 25400u
/** The same half-cell in cycles of a 150 MHz system clock. */
#define KST_HALF_CELL_CYCLES_150MHZ 3810u
/** The servo's own cell, 51.15 us (half-cell 25.575 us, 0.7 % slower than
 *  the master).  The decoder predicts each mid-cell edge with it. */
#define KST_SERVO_CELL_NS      51150u
#define KST_SERVO_HALF_CELL_NS 25575u

#define KST_FRAME_BITS  27u   /**< master to servo */
#define KST_REPLY_BITS  11u   /**< servo to master */
/** The sync burst: 64 high pulses of one half-cell, one half-cell apart.
 *  Other counts are not tested. */
#define KST_SYNC_PULSES 64u

/** Longest level sequence: the sync burst, 64 highs and 63 lows. */
#define KST_FRAME_MAX_HALF_CELLS 127u
#define KST_FRAME_LEVEL_BYTES    16u

/* --- line handling, for the driver ---------------------------------------- */

/** After the last half-cell of a frame the driver drives low for 50 us,
 *  then switches the pad to input.  The earliest reply observed starts
 *  437 us after the last edge. */
#define KST_RELEASE_NS 50000u

/* --- reply windows, from the master's last edge to the first rising edge -- */

/** After a read frame and after a sync burst: measured 437 to 439 us. */
#define KST_READ_ACCEPT_MIN_NS 350000u
#define KST_READ_ACCEPT_MAX_NS 600000u
/** Capture length after a read frame or a sync burst, 1.5 ms. */
#define KST_READ_WINDOW_NS     1500000u
/** After a write frame: measured 3687 to 3714 us. */
#define KST_WRITE_ACCEPT_MIN_NS 3300000u
#define KST_WRITE_ACCEPT_MAX_NS 4200000u
/** Capture length after a write frame, 8 ms. */
#define KST_WRITE_WINDOW_NS     8000000u

/* --- decoder -------------------------------------------------------------- */

/** A level that lasts less than 1.5 us is not a level change. */
#define KST_GLITCH_NS 1500u
/** A mid-cell edge is searched within 8 us of its predicted time. */
#define KST_RX_SEARCH_NS 8000u
/** The two halves of a cell are compared 12.8 us before and after the
 *  mid-cell edge. */
#define KST_RX_CONFIRM_NS 12800u
/** Mean half-cell of a reply: 25.6 us +/- 5 %.  The servo's clock tolerance
 *  over temperature and supply is not measured. */
#define KST_RX_HALF_CELL_MIN_NS 24300u
#define KST_RX_HALF_CELL_MAX_NS 26900u
/** After a last bit 1 the line is low within 100 us of the nominal end.
 *  The servo releases the line there; the last high reads 32 us at a 1.4 V
 *  threshold and 59 us at 0.1 V. */
#define KST_RX_FINAL_EDGE_MAX_NS 100000u
/** The line stays low for 200 us after the reply. */
#define KST_RX_QUIET_AFTER_NS 200000u

/** Edges a capture holds.  A reply has at most 23; the rest is room for
 *  glitches, 2 edges each. */
#define KST_CAP_MAX_EDGES 48u

/* --- frames --------------------------------------------------------------- */

typedef enum {
    KST_FRAME_READ = 0,
    KST_FRAME_WRITE,
    KST_FRAME_SYNC,
} kst_frame_kind_t;

/**
 * A master frame as line levels, one per half-cell.
 *
 * The sequence starts with the first high half-cell and ends with the last
 * high half-cell.  The low half of the start bit and a trailing low are not
 * part of it: the line is low before and after.  The end of the last
 * half-cell is the frame's last edge, the origin of every reply time.
 */
typedef struct {
    uint8_t kind;                           /**< kst_frame_kind_t */
    uint8_t n_half;                         /**< half-cells to clock out */
    uint8_t level[KST_FRAME_LEVEL_BYTES];   /**< bit 7 of byte 0 first; 1 = high */
} kst_frame_t;

/** Read frame for register @p reg.  False, and no frame, for an address
 *  above KST_REG_MAX. */
bool kst_frame_read(uint8_t reg, kst_frame_t *out);

/** Write frame.  False, and no frame, for register 0x00 and for an address
 *  above KST_REG_MAX: neither is ever written. */
bool kst_frame_write(uint8_t reg, uint8_t value, kst_frame_t *out);

/** The sync burst that puts a servo into programming mode. */
void kst_frame_sync(kst_frame_t *out);

/** Level of half-cell @p index; false past the end. */
bool kst_frame_level(const kst_frame_t *frame, unsigned index);

/** Time the frame takes on the wire, in ns. */
uint32_t kst_frame_duration_ns(const kst_frame_t *frame);

/* --- reply capture -------------------------------------------------------- */

/**
 * The reply window as the driver saw it.
 *
 * Times are in ns from the frame's last edge.  The capture starts at
 * KST_RELEASE_NS and ends at @p window_ns.  @p edge_ns holds every level
 * change in ascending order; the driver applies no filter.
 */
typedef struct {
    uint32_t window_ns;                     /**< end of the capture */
    uint8_t  level0;                        /**< line level at the start, 0 or 1 */
    uint8_t  overflow;                      /**< 1: more edges than the buffer holds */
    uint8_t  n_edges;
    uint32_t edge_ns[KST_CAP_MAX_EDGES];
} kst_capture_t;

/** What the master sent, which decides the accept window and the flags. */
typedef enum {
    KST_EXPECT_READ = 0,
    KST_EXPECT_WRITE,
    KST_EXPECT_SYNC,
} kst_expect_t;

/** Result of decoding one reply window.  One code per failed check. */
typedef enum {
    KST_RX_OK = 0,
    KST_RX_NO_REPLY,     /**< no edge in the window */
    KST_RX_QUIET,        /**< an edge before the accept window */
    KST_RX_START_LEVEL,  /**< the line is high when the capture starts */
    KST_RX_LATE,         /**< first edge after the accept window */
    KST_RX_CELL,         /**< a cell without opposite halves */
    KST_RX_LENGTH,       /**< fewer or more than 11 cells */
    KST_RX_RATE,         /**< mean half-cell outside 24.3 to 26.9 us */
    KST_RX_FLAGS,        /**< flag bits or sync value not as expected */
    KST_RX_FINAL_EDGE,   /**< line still high 100 us after the nominal end */
    KST_RX_WINDOW,       /**< the capture ends before the quiet time does */
    KST_RX_OVERFLOW,     /**< the capture lost edges */
    KST_RX_CAPTURE,      /**< no capture, or edge times not ascending */
} kst_rx_t;

typedef struct {
    uint8_t  result;        /**< kst_rx_t */
    uint8_t  value;         /**< the 8 data bits; valid with KST_RX_OK */
    uint8_t  flags;         /**< the 2 bits after the start bit */
    uint8_t  glitches;      /**< levels shorter than KST_GLITCH_NS removed */
    uint32_t delay_ns;      /**< last master edge to first rising edge */
    uint32_t half_cell_ns;  /**< mean over the 10 cells, rounded to nearest */
} kst_reply_t;

/** Accept window and capture length for @p expect. */
uint32_t kst_reply_window_ns(kst_expect_t expect);

/**
 * Decode a reply window.
 *
 * The decoder tracks the mid-cell edge of each of the 10 cells after the
 * start cell and re-centres on every edge it finds.  It does not classify
 * run lengths: the last high of a reply is stretched by the release of the
 * line.  The value after a write is not a dependable echo; the caller reads
 * the register back.
 *
 * @p out is filled in every case; @p out->delay_ns and @p out->half_cell_ns
 * are 0 where the check that failed comes before they are known.
 */
kst_rx_t kst_reply_decode(const kst_capture_t *cap, kst_expect_t expect,
                          kst_reply_t *out);

#ifdef __cplusplus
}
#endif

#endif /* KST_WIRE_H */
