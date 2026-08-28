/*
 * CAN bit timing: turning a wanted bit rate into segment counts.
 *
 * Every CAN controller divides a source clock into time quanta and then splits
 * each bit into a fixed sync quantum plus two programmable segments.  Getting
 * that wrong does not fail cleanly.  A node whose bit rate is a fraction of a
 * percent off, or whose sample point sits in the wrong place, works on a short
 * bench cable with one other node and then starts logging errors when the bus
 * gets longer, colder or busier -- which is the worst possible time to find out.
 *
 * So the arithmetic lives here, in pure C with tests, rather than in a table of
 * magic register values copied from an application note.  Two controllers use
 * it: the panel's TWAI and the coprocessor's XL2515.  They have different
 * clocks and different segment limits and exactly the same arithmetic.
 *
 * THE ONE RULE THIS ENFORCES: the bit rate must come out **exact**.  Not close.
 * CAN has no framing to resynchronise against beyond its own bit stuffing, and
 * two nodes that disagree by a percent will agree on short frames and fall out
 * on long ones.  A rate that cannot be hit exactly is reported as impossible,
 * which is a far better answer than one that is nearly right -- and it is how
 * you find out that an 8 MHz crystal cannot do 1 Mbit/s before you have soldered
 * anything.
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
 * always exactly one and is not programmable on any controller worth using.
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
 * The CiA-recommended sample point for rates above 800 kbit/s, in permille.
 *
 * Later is better for propagation delay and worse for oscillator tolerance.
 * 87.5% is the usual compromise and what most tools default to.
 */
#define CAN_SAMPLE_POINT_DEFAULT 875

/**
 * Solve for the segmentation closest to @p target_permille that hits
 * @p bitrate exactly.
 *
 * False when no combination does. That is a real answer and usually means the
 * source clock is wrong for the rate rather than that the arguments were.
 */
bool can_timing_solve(const can_timing_limits_t *lim, uint32_t bitrate,
                      uint16_t target_permille, can_timing_t *out);

/** The highest rate this controller can reach exactly, or 0 if none. */
uint32_t can_timing_max_bitrate(const can_timing_limits_t *lim);

/* ------------------------------------------------- the two controllers */

/** The coprocessor's XL2515 (MCP2515-compatible), given its crystal. */
void can_timing_limits_mcp2515(can_timing_limits_t *out, uint32_t crystal_hz);

/** The panel's TWAI, off the 80 MHz APB clock. */
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
 * The split of tseg1 into propagation and phase-1 segments happens here
 * because it is the MCP2515's business and not the arithmetic's: both are
 * 1..8 quanta, the bus does not care where the boundary falls, and only this
 * controller has to be told. Returns false if the solution cannot be
 * expressed -- a tseg1 of 17 or more has no legal split.
 */
bool mcp2515_encode_timing(const can_timing_t *t, uint8_t cnf[3]);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_CAN_TIMING_H */
