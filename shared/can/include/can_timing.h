/*
 * CAN (Controller Area Network) bit timing: a wanted bit rate turned into
 * segment counts.
 *
 * A CAN controller divides its source clock into time quanta and splits each
 * bit into one sync quantum plus two programmable segments.  A bit rate that
 * is a fraction of a percent off, or a sample point in the wrong place, works
 * on a short cable with one other node and logs errors when the bus is
 * longer, colder or busier.
 *
 * The arithmetic is here, in pure C with tests, rather than in a table of
 * register values.  Two controllers use it: the panel's TWAI (Two-Wire
 * Automotive Interface, the ESP32-S3's CAN controller) and the coprocessor's
 * XL2515.  They have different clocks and segment limits and the same
 * arithmetic.
 *
 * The rule this module enforces: the bit rate comes out exact.  CAN has no
 * framing to resynchronise against beyond its own bit stuffing, and two nodes
 * that disagree by 1% agree on short frames and fail on long ones.  A rate
 * that cannot be hit exactly is reported as impossible; an 8 MHz crystal
 * cannot make 1 Mbit/s.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_CAN_TIMING_H
#define RCBENCH_CAN_TIMING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What a particular controller can be asked for.
 *
 * Segment counts are in time quanta and exclude the sync quantum, which is
 * always one.
 */
typedef struct {
    uint32_t clock_hz;   /**< the clock the prescaler divides */
    uint16_t div_min;    /**< smallest prescaler divisor, as an actual divisor */
    uint16_t div_max;
    uint16_t div_step;   /**< 1, or 2 where the controller doubles internally */
    uint8_t  tseg1_min;  /**< propagation + phase 1 */
    uint8_t  tseg1_max;
    uint8_t  tseg2_min;  /**< phase 2 */
    uint8_t  tseg2_max;
    uint8_t  sjw_max;
    uint8_t  tq_min;     /**< total quanta a bit may have, sync included */
    uint8_t  tq_max;
} can_timing_limits_t;

typedef struct {
    uint16_t div;        /**< the prescaler divisor actually used */
    uint8_t  tseg1;
    uint8_t  tseg2;
    uint8_t  sjw;
    uint8_t  tq;         /**< 1 + tseg1 + tseg2 */
    uint32_t bitrate_hz; /**< achieved, and equal to what was asked */
    uint16_t sample_permille; /**< where in the bit it samples, 0..1000 */
} can_timing_t;

/**
 * The sample point CiA (CAN in Automation) recommends for rates above
 * 800 kbit/s, in permille.  Later is better for propagation delay and worse
 * for oscillator tolerance; 87.5% is the usual compromise.
 */
#define CAN_SAMPLE_POINT_DEFAULT 875

/**
 * The sample point this link uses, at both ends.
 *
 * The coprocessor's XL2515 has one way to make 1 Mbit/s from a 16 MHz
 * crystal: the smallest divisor and 8 quanta per bit, the fewest a bit may
 * have, which puts the sample point at 75%.  The panel has slack and matches
 * the coprocessor, so both ends sample the bit in the same place.
 */
#define CAN_SAMPLE_POINT_LINK 750

/**
 * Solve for the segmentation closest to @p target_permille that hits
 * @p bitrate exactly.
 *
 * False when no combination does, which means the source clock cannot make
 * the rate.
 */
bool can_timing_solve(const can_timing_limits_t *lim, uint32_t bitrate,
                      uint16_t target_permille, can_timing_t *out);

/** The highest rate this controller can reach exactly, or 0 if none. */
uint32_t can_timing_max_bitrate(const can_timing_limits_t *lim);

/* ------------------------------------------------- the two controllers */

/** The coprocessor's XL2515 (MCP2515-compatible), given its crystal. */
void can_timing_limits_mcp2515(can_timing_limits_t *out, uint32_t crystal_hz);

/** The panel's TWAI, off the 80 MHz APB (advanced peripheral bus) clock. */
void can_timing_limits_twai(can_timing_limits_t *out);

/* --------------------------------------------------- MCP2515 registers */

#define MCP2515_CNF1 0x2A
#define MCP2515_CNF2 0x29
#define MCP2515_CNF3 0x28

/**
 * Encode a solution into CNF1..CNF3.
 *
 * @p cnf receives three bytes in register order CNF1, CNF2, CNF3.
 *
 * tseg1 is split into propagation and phase-1 segments here, because that
 * split is the MCP2515's business: both are 1..8 quanta, and the bus does
 * not care where the boundary falls.  Returns false if the solution cannot
 * be expressed; a tseg1 of 17 or more has no legal split.
 */
bool mcp2515_encode_timing(const can_timing_t *t, uint8_t cnf[3]);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_CAN_TIMING_H */
