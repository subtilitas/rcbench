/**
 * @file
 * @brief The Guition JC3636K518C's encoder, pure: the two lines GPIO8 (A)
 *        and GPIO7 (B) decoded as quadrature into detents.
 *
 * One detent pulses both lines, one after the other, so the pair is a
 * quadrature A/B signal.  The firmware takes an interrupt on every edge of
 * either line, reads both levels and hands the pair to dk518_enc_edge();
 * the input task calls dk518_enc_poll() every 5ms.  A pair is
 * (A << 1) | B, 1 for a high line; both lines idle high on their
 * pull-ups.
 *
 * - Direction: each change of one line is a quarter step of the gray code
 *   11 > 01 > 00 > 10 > 11, +1 that way (A falls first) and -1 the other
 *   way.  Clockwise is positive; which way A leads on the device is not
 *   run on the device yet.  A change of both lines at once (an edge the
 *   interrupt missed) moves no step and takes the new pair.
 * - The home rest: the first pair that stays DK518_REST_MS (30ms) without
 *   an edge.  Arriving at it with a nonzero sum of quarter steps counts
 *   the detents at once.
 * - Any other pair that stays DK518_REST_MS without an edge is a rest as
 *   well: a nonzero sum counts there, on the poll that finds it quiet.
 *   An encoder with 2 edges a detent rests at 11 and at 00; one with 4
 *   rests at 11 only.
 * - The count: one detent, the sum's sign.  The edges a detent has on
 *   this device are not measured yet: the firmware prints the trace of
 *   each detent (dk518_enc_last()) to measure them.
 * - Bounce and a half turn that goes back sum to 0 and count nothing.
 *   The trace runs from the last count or rest, through any return home
 *   with a sum of 0.
 *
 * Limits: a hand that holds the knob between two detents for 30ms or more
 * makes that pair a rest, so on a 4-edge encoder that detent counts twice.
 * On a 2-edge encoder a turn that passes its middle rest without a 30ms
 * pause counts its two detents as one.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DK518_ENC_H
#define DK518_ENC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A pair quiet this long is a rest, ms. */
#define DK518_REST_MS 30u
/** @brief Pairs a detent's trace keeps; edges past them are counted. */
#define DK518_TRACE_MAX 16u

/** @brief One counted detent's trace. */
typedef struct {
    int32_t  detents;                /**< what it counted, CW positive */
    uint8_t  n;                      /**< pairs in @p pair */
    uint16_t edges;                  /**< edges since the last rest */
    uint8_t  pair[DK518_TRACE_MAX];  /**< the pairs, the start first */
    uint32_t ms;                     /**< from the first edge to the count */
} dk518_trace_t;

/** @brief The decode. */
typedef struct {
    bool          primed;   /**< a first pair was taken */
    uint8_t       pair;     /**< the pair now */
    bool          home_ok;  /**< the home rest is known */
    uint8_t       home;     /**< the home rest */
    bool          quiet;    /**< the pair now was taken as a rest */
    int32_t       sum;      /**< quarter steps since the last rest */
    uint32_t      last_ms;  /**< the last edge */
    uint32_t      first_ms; /**< the first edge since the last rest */
    dk518_trace_t run;      /**< the trace under way */
    dk518_trace_t last;     /**< the last counted detent's trace */
    bool          fresh;    /**< @p last is new since dk518_enc_last() */
} dk518_enc_t;

/** @brief Start with no pair.  @param e the decode */
void dk518_enc_init(dk518_enc_t *e);

/**
 * @brief One edge: the pair both lines read after it.
 * @param e    the decode
 * @param pair (A << 1) | B, 0 to 3; higher bits are ignored
 * @param now  the time of the edge, ms
 * @return detents counted by this edge, clockwise positive
 */
int32_t dk518_enc_edge(dk518_enc_t *e, uint8_t pair, uint32_t now);

/**
 * @brief The poll: a pair quiet for DK518_REST_MS becomes a rest.
 * @param e   the decode
 * @param now the time, ms
 * @return detents counted at that rest, clockwise positive
 */
int32_t dk518_enc_poll(dk518_enc_t *e, uint32_t now);

/**
 * @brief The last counted detent's trace, once.
 * @param e   the decode
 * @param out the trace
 * @return true when a detent was counted since the last call
 */
bool dk518_enc_last(dk518_enc_t *e, dk518_trace_t *out);

/**
 * @brief A trace as text: "CW 4 edges 11>01>00>10>11".
 * @param t   the trace
 * @param out the text
 * @param n   its size
 */
void dk518_enc_text(const dk518_trace_t *t, char *out, unsigned n);

#ifdef __cplusplus
}
#endif

#endif /* DK518_ENC_H */
