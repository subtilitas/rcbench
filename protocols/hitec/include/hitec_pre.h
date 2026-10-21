/**
 * @file hitec_pre.h
 * @brief Receive a D series answer by precharge and sample: the cycle
 *        arithmetic, the sampler's PIO program, the decoder and the run.
 *
 * Implements the signal-wire programming protocols of Hitec servos.  Hitec
 * is a name or trademark of its owner; it is used here only to say which
 * servos this module works with (README.md, "Legal notice and disclaimer").
 *
 * The D series answer needs a high level on the wire that the servo does
 * not make.  With a pull-up on the board (receive mode PULL-UP) the
 * receiver of hitec_pio.h reads the answer as it comes.  Without one
 * (receive mode PRECHARGE) the master makes the high level itself, a
 * sample period at a time: a second state machine drives the pad high for
 * a short precharge, releases it, waits, and samples the pad.  A servo
 * that pulls the line low pulls it down inside the wait; one that leaves
 * it released leaves it high.  The samples go to memory, a 32-bit word
 * every 32 samples, and this module decodes them as inverted 8N1 into the
 * hitec_capture_t the PULL-UP receiver gives.
 *
 * Measured on one D485HW at a 150 MHz system clock, 5 reads per setting:
 * every read passes with the sample 40 to 187 ns after the release and a
 * precharge of 6.7 to 200 ns; at 213 ns a precharge of 1 cycle fails; from
 * 267 ns no read passes; with no precharge every sample is low.  This
 * module takes the middle: a precharge of HITEC_PRE_CHARGE_NS and the
 * sample HITEC_PRE_SAMPLE_NS after the release, rounded to system clock
 * cycles.
 *
 * Sample words.  Sample k of a run is bit 31 - (k mod 32) of word k / 32:
 * the state machine shifts left and pushes every 32 samples.  A sample is
 * 1 for a high pad.  Sample k is taken k * period + charge + wait system
 * clock cycles after the state machine starts, through the input
 * synchroniser's HITEC_PIO_SYNC_SYS_CYCLES.
 *
 * The decoder.  Inverted 8N1 at 115 200 bit/s: the line rests low, a
 * start bit is high, a data bit low is a 1, the stop bit is low.  As the
 * PULL-UP receiver does, it first waits for the rest level, then for a
 * start bit; a start bit is the first high sample after a low one, and a
 * stop bit read low counts as that low sample.  The
 * middle of bit j lies floor((j + 1/2) samples-per-bit) samples after that
 * sample; a start bit whose middle reads low is a glitch, and the wait
 * goes on.  Every sample spent waiting counts against the window; the
 * samples inside a byte do not.  The decoder ends at max_rx bytes, at the
 * end of the window, or at the end of the samples.  Integer arithmetic
 * only; a sample word that holds only the level waited for is passed in
 * one step.
 *
 * The run.  A SEND goes out on the line's own state machine with no
 * receiver (hitec_pre_tx_xfer()); the sampler takes the pad when the
 * request has ended (hitec_pre_tx_done()).  A LISTEN samples from its
 * start.  A transaction with no receive window, or a LEVEL, runs as in
 * PULL-UP.  hitec_pre_step() feeds the words the sampler has written,
 * however many there are, so a caller steps it once per pass; an abort
 * keeps the bytes decoded so far.
 *
 * The module includes nothing outside the C standard headers, this folder's
 * headers and holds no global state.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HITEC_PRE_H
#define HITEC_PRE_H

#include <stdbool.h>
#include <stdint.h>

#include "hitec_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief How a D series line receives. */
typedef enum {
    HITEC_RX_PULL_UP = 0,   /**< released for the answer; a pull-up on the
                             *   board makes the high level (2.2 kOhm to the
                             *   servo supply on the bench). */
    HITEC_RX_PRECHARGE = 1, /**< no pull-up: precharge and sample, this
                             *   module. */
} hitec_rx_mode_t;

/**
 * @brief Samples a bit the period aims at: the period is the system clock
 *        cycles nearest to 1/8 of a D series bit.
 */
#define HITEC_PRE_SAMPLES_PER_BIT 8u
/**
 * @brief The precharge and the wait from the release to the sample the cycle
 *        counts aim at, ns: the middle of the passing window measured on a
 *        D485HW, a precharge of 6.7 to 200 ns and a sample 40 to 187 ns
 *        after the release.
 */
#define HITEC_PRE_CHARGE_NS 100u
#define HITEC_PRE_SAMPLE_NS 113u
/**
 * @brief The measured passing window, ns.
 *
 * A clock whose rounded cycles fall outside it is refused.
 */
#define HITEC_PRE_CHARGE_MIN_NS 7u
#define HITEC_PRE_CHARGE_MAX_NS 200u
#define HITEC_PRE_SAMPLE_MIN_NS 40u
#define HITEC_PRE_SAMPLE_MAX_NS 187u
/**
 * @brief System clocks the arithmetic takes: one instruction's 32 cycles hold
 *        the wait, and 1/8 of a bit holds the precharge, the wait and the
 *        sample.
 *
 * The bench ran at 150 MHz.
 */
#define HITEC_PRE_MIN_HZ  48000000u
#define HITEC_PRE_MAX_HZ 250000000u
/** @brief Longest window a run takes, us. */
#define HITEC_PRE_WINDOW_MAX_US 30000u
/**
 * @brief Sample words the sampler's buffer holds: the longest window and
 *        HITEC_RX_MAX bytes at every clock the arithmetic takes.
 */
#define HITEC_PRE_WORDS 928u
/** @brief Instructions of the longest sampler program. */
#define HITEC_PRE_PROGRAM_MAX 12u
/** @brief Bits of a frame the decoder samples: start, 8 data, stop. */
#define HITEC_PRE_FRAME_BITS 10u

/** @brief The cycle counts a system clock gives. */
typedef struct {
    uint32_t sys_hz;    /**< system clock, in Hz */
    uint32_t period;    /**< system clock cycles from one sample to the next */
    uint32_t charge;    /**< cycles the pad is driven high */
    uint32_t wait;      /**< cycles from the release to the sample */
    uint32_t spb_q16;   /**< samples a bit, times 65536, rounded */
    /**
     * Samples from a start bit's first high sample to the middle of bit
     * j: 0 the start bit, 1 to 8 the data, 9 the stop bit.
     */
    uint16_t mid[HITEC_PRE_FRAME_BITS];
} hitec_pre_timing_t;

/**
 * @brief The counts for a system clock of @p sys_hz.
 *
 * False, and @p out left as
 * it was, for a clock outside HITEC_PRE_MIN_HZ to HITEC_PRE_MAX_HZ or
 * one whose rounded precharge or wait falls outside the measured window.
 * At 150 MHz: a period of 163 cycles (1086.7 ns, 7.99 samples a bit), a
 * precharge of 15 cycles (100.0 ns) and the sample 17 cycles (113.3 ns)
 * after the release.
 *
 * @param sys_hz  System clock, in Hz.
 * @param out     Receives the cycle counts; left unchanged when false is
 *                returned.
 * @return true when the clock gives usable counts.
 */
bool hitec_pre_timing(uint32_t sys_hz, hitec_pre_timing_t *out);

/** @brief @p cycles system clock cycles of @p t in ns, rounded to nearest.
 *
 * @param t       Counts of the clock.
 * @param cycles  Cycle count.
 * @return cycles converted to ns, rounded to nearest.
 */
uint32_t hitec_pre_cycles_ns(const hitec_pre_timing_t *t, uint64_t cycles);

/**
 * @brief The sampler's program: precharge, release, wait, sample, then the rest
 *        of the period, as PIO instruction words: `set pindirs, 1
 *        [charge - 1]`, `set pindirs, 0 [wait - 1]`, `in pins, 1`, and `nop`
 *        with delays up to the period.
 *
 * The pad's SET and IN pin is the line; the output latch is high; autopush at
 * 32 bits, shifting left; no side-set.  The program wraps from its last word
 * to its first.  Returns the words written, 0 when @p max is too small.
 *
 * @param t      Counts of the clock.
 * @param words  Receives the instruction words.
 * @param max    Capacity of words, in words.
 * @return Words written; 0 when max is too small.
 */
unsigned hitec_pre_program(const hitec_pre_timing_t *t, uint16_t *words,
                           unsigned max);

/** @brief Samples a window of @p window_us waits, rounded to nearest.
 *
 * @param t          Counts of the clock.
 * @param window_us  Window, in us.
 * @return Number of samples in the window, rounded to nearest.
 */
uint32_t hitec_pre_window_samples(const hitec_pre_timing_t *t,
                                  uint32_t window_us);

/**
 * @brief Sample words a run with a window of @p window_us and @p max_rx bytes
 *        takes: the window, every byte from its start to its stop bit's middle,
 *        and one word.
 *
 * 0 when the window is above HITEC_PRE_WINDOW_MAX_US or the words would not
 * fit HITEC_PRE_WORDS.
 *
 * @param t          Counts of the clock.
 * @param window_us  Window, in us.
 * @param max_rx     Bytes to receive.
 * @return Sample words the run takes, or 0 when the window is too long or the
 *                words do not fit.
 */
uint32_t hitec_pre_words(const hitec_pre_timing_t *t, uint32_t window_us,
                         uint8_t max_rx);

/**
 * @brief The level of sample @p k of @p words: 1 high.
 *
 * Samples are packed 32 to a word, the first sample in the most significant
 * bit.
 *
 * @param words  Sample words, from sample 0.
 * @param k      Sample index.
 * @return 1 when sample @p k is high, 0 when it is low.
 */
static inline unsigned hitec_pre_sample(const uint32_t *words, uint32_t k)
{
    return (unsigned)((words[k >> 5] >> (31u - (k & 31u))) & 1u);
}

/* --- the decoder ---------------------------------------------------------- */

/** @brief Phase of the decoder. */
typedef enum {
    HITEC_PRE_WAIT_REST = 0,  /**< waiting for the low rest level */
    HITEC_PRE_WAIT_START,     /**< waiting for a start bit */
    HITEC_PRE_IN_BYTE,        /**< a start bit seen, its samples not all in */
    HITEC_PRE_DEC_DONE,       /**< decoding has ended */
} hitec_pre_dec_phase_t;

/** @brief Why the decoder ended. */
typedef enum {
    HITEC_PRE_END_NONE = 0,   /**< the decoder has not ended */
    HITEC_PRE_END_FULL,       /**< max_rx bytes */
    HITEC_PRE_END_WINDOW,     /**< the window's waiting is over */
    HITEC_PRE_END_SHORT,      /**< the samples ended first */
} hitec_pre_end_t;

/** @brief Decoder state for one receive. */
typedef struct {
    uint32_t window;          /**< waiting samples the window allows */
    uint32_t at;              /**< the next sample to look at */
    uint32_t waited;          /**< waiting samples so far */
    uint32_t start[HITEC_RX_MAX]; /**< each byte's first high sample */
    uint8_t  value[HITEC_RX_MAX];  /**< decoded byte values */
    uint16_t framing;         /**< bit k: byte k's stop bit read high */
    uint8_t  n;               /**< bytes decoded so far */
    uint8_t  max_rx;          /**< bytes at which the decoder ends */
    uint8_t  phase;           /**< hitec_pre_dec_phase_t */
    uint8_t  end;             /**< hitec_pre_end_t */
} hitec_pre_dec_t;

/**
 * @brief Prepare @p d for a window of @p window_us and up to @p max_rx bytes
 *  (1 to HITEC_RX_MAX).
 *
 * @param d          Decoder to prepare.
 * @param t          Counts of the clock.
 * @param window_us  Window, in us.
 * @param max_rx     Bytes to receive, 1 to HITEC_RX_MAX.
 */
void hitec_pre_dec_init(hitec_pre_dec_t *d, const hitec_pre_timing_t *t,
                        uint32_t window_us, uint8_t max_rx);

/**
 * @brief Decode the samples of the first @p n_words words of @p words that it
 * has not seen.  @p words is the whole buffer from sample 0; a call may
 * pass the same words again with more after them.  Returns true once the
 * decoder has ended (max_rx bytes or the window).  A start bit whose
 * samples are not all in waits for the next call.
 *
 * @param d        Decoder state.
 * @param t        Counts of the clock.
 * @param words    Sample words, from sample 0.
 * @param n_words  Words of the buffer that are written.
 * @return true once the decoder has ended.
 */
bool hitec_pre_dec_feed(hitec_pre_dec_t *d, const hitec_pre_timing_t *t,
                        const uint32_t *words, uint32_t n_words);

/**
 * @brief No more samples come: a decoder that has not ended ends with
 *  HITEC_PRE_END_SHORT, dropping a byte whose samples are not all in.
 *
 * @param d  Decoder state.
 */
void hitec_pre_dec_close(hitec_pre_dec_t *d);

/* --- the run -------------------------------------------------------------- */

/** @brief What a transaction asks of the sampler, hitec_pre_begin(). */
typedef enum {
/** @brief No receive window, or a LEVEL: the line runs it as in PULL-UP. */
    HITEC_PRE_PLAN_LINE = 0,
/** @brief A SEND with a window: the request on the line, then the sampler. */
    HITEC_PRE_PLAN_SEND,
/** @brief A LISTEN: the sampler from the start. */
    HITEC_PRE_PLAN_LISTEN,
    /**
     * @brief Not one the sampler runs: a LISTEN that answers, a window
     *        above HITEC_PRE_WINDOW_MAX_US, a classic transaction, or not
     *        valid.
     */
    HITEC_PRE_PLAN_REFUSE,
} hitec_pre_plan_t;

/** @brief Phase of a run. */
typedef enum {
    HITEC_PRE_IDLE = 0,       /**< no run, or one the line runs alone */
    HITEC_PRE_TX,             /**< the request is on the line */
    HITEC_PRE_SAMPLE,         /**< the sampler has the pad */
    HITEC_PRE_DONE,           /**< the capture is ready */
} hitec_pre_phase_t;

/** @brief State of one transaction on the sampler. */
typedef struct {
    hitec_pre_dec_t dec;      /**< decoder for the samples */
    uint32_t words;           /**< sample words the sampler takes */
    uint32_t window_ns;       /**< as the transaction asked */
    uint8_t  phase;           /**< hitec_pre_phase_t */
    uint8_t  aborted;         /**< set by hitec_pre_abort() */
    uint8_t  lost;            /**< samples went missing: overflow */
} hitec_pre_run_t;

/**
 * @brief Plan @p x on a PRECHARGE line.
 *
 * For HITEC_PRE_PLAN_SEND the run is in
 * HITEC_PRE_TX, for HITEC_PRE_PLAN_LISTEN in HITEC_PRE_SAMPLE; otherwise
 * it is idle.
 *
 * @param r  Run state to set up.
 * @param t  Counts of the clock.
 * @param x  Transaction to plan.
 * @return The plan of the transaction.
 */
hitec_pre_plan_t hitec_pre_begin(hitec_pre_run_t *r,
                                 const hitec_pre_timing_t *t,
                                 const hitec_xfer_t *x);

/**
 * @brief The transaction the line sends for a HITEC_PRE_PLAN_SEND: @p x
 *        with no window and no byte to receive.
 *
 * @param x    Transaction the plan was made for.
 * @param out  Receives x with no window and no byte to receive.
 */
void hitec_pre_tx_xfer(const hitec_xfer_t *x, hitec_xfer_t *out);

/**
 * @brief The request has ended: HITEC_PRE_TX to HITEC_PRE_SAMPLE.
 *
 * Nothing in another phase.
 *
 * @param r  Run state.
 */
void hitec_pre_tx_done(hitec_pre_run_t *r);

/**
 * @brief Feed the first @p n_words sample words.
 *
 * @p all_in: the sampler has
 * written every word it takes (r->words) or stopped.  Returns true when
 * the run is in HITEC_PRE_DONE: the decoder ended, or every word is in.
 * Nothing outside HITEC_PRE_SAMPLE.
 *
 * @param r        Run state.
 * @param t        Counts of the clock.
 * @param words    Sample words, from sample 0.
 * @param n_words  Words of the buffer that are written.
 * @param all_in   true when every word the run takes is written.
 * @return true when the run is in HITEC_PRE_DONE.
 */
bool hitec_pre_step(hitec_pre_run_t *r, const hitec_pre_timing_t *t,
                    const uint32_t *words, uint32_t n_words, bool all_in);

/**
 * @brief Cut the run in any phase but idle: what @p n_words words hold is
 * decoded, a byte not whole is dropped, and the run is done, aborted.
 * Nothing on an idle or a done run.
 *
 * @param r        Run state.
 * @param t        Counts of the clock.
 * @param words    Sample words, from sample 0.
 * @param n_words  Words of the buffer that are written.
 */
void hitec_pre_abort(hitec_pre_run_t *r, const hitec_pre_timing_t *t,
                     const uint32_t *words, uint32_t n_words);

/**
 * @brief Samples went missing (the sampler stalled) or the sampler ran past its
 *  time: the capture is marked overflowed.  The run is done.  After
 *  hitec_pre_abort() it marks the aborted capture overflowed as well.
 *
 * @param r  Run state.
 */
void hitec_pre_lost(hitec_pre_run_t *r);

/**
 * @brief The capture of a done run.
 *
 * @p offset_ns: the sampler's start from the receiver's time 0 (the release and
 * one bit time after a SEND, the start of a LISTEN), negative when it started
 * before.  A start bit's time is the middle between its first high sample and
 * the sample before it.
 *
 * @param r          Done run state.
 * @param t          Counts of the clock.
 * @param offset_ns  Offset of the sampler start from the receiver time 0, in
 *                   ns.
 * @param out        Receives the capture.
 */
void hitec_pre_capture(const hitec_pre_run_t *r, const hitec_pre_timing_t *t,
                       int32_t offset_ns, hitec_capture_t *out);

#ifdef __cplusplus
}
#endif

#endif /* HITEC_PRE_H */
