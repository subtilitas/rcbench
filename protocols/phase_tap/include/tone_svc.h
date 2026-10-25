/**
 * @file
 * @brief The tone service: what core 1 runs for the phase tap each 1ms tick.
 *
 * The PIO (programmable input/output) program stamps the edges of one motor
 * phase and applies the capture's hold-off (tone.h, tone_holdoff_t); a DMA
 * (direct memory access) channel collects its words in a ring
 * (edge_ring.h).  A pass takes the words written since the last one, feeds
 * the detector (tone.c) with them, advances it to the present less the
 * hold-off, and hands back the beeps it finished and a status.
 *
 * Core 0 gives the service an order (tone_cmd_t) built from its set-up.
 * A new set-up generation restarts the detector; a new capture
 * generation also starts the ring reader again, because the DMA writes from
 * slot 0 of a ring the counter has started over in.  A pass under an order with run clear does
 * nothing and reports not running.
 *
 * The present tick.  The caller passes the ticks elapsed since the capture
 * started, from the 1MHz timer (tone_svc_ticks()); the words' own counts
 * are exact, and this only places them among the 2^31 ticks a count
 * repeats in.  The detector is advanced no further than the present less
 * TONE_SVC_MARGIN_US, which covers the timer's offset from the counter
 * (about 2us), a word in the FIFO or in flight to the ring (under 1us),
 * and the pass's own delay, so that no word stamped before the horizon is
 * still to come.
 *
 * Pure C, no SDK.  Host-tested in test_tone_svc.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edge_ring.h"
#include "tone.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The system clock the capture is built for, and the tick it gives: one
 *        count per 4 cycles.
 */
#define TONE_SVC_SYS_HZ   150000000u  /**< system clock, Hz                   */
#define TONE_SVC_TICK_HZ  (TONE_SVC_SYS_HZ / 4u)  /**< the capture's tick, Hz */
/** @brief The hold-off the capture applies, 8us. */
#define TONE_SVC_HOLD_NS  8000u
/** @brief How far behind the present the detector is advanced. */
#define TONE_SVC_MARGIN_US 20u
/**
 * @brief The capture ring, in 32-bit words: 16kB, 42.7ms at 96,000 edges/s.
 */
#define TONE_RING_WORDS   4096u
/** @brief Edges taken in one pass. */
#define TONE_SVC_BATCH    64u
/** @brief Beeps handed back in one pass. */
#define TONE_SVC_BEEPS    TONE_BEEP_QUEUE

/** @brief The order from core 0. */
typedef struct {
    uint16_t gen;            /**< moves with each change of set-up          */
    uint16_t cap_gen;        /**< moves each time the capture starts again;
                                  its counter and ring begin anew          */
    bool     run;            /**< the capture runs and the ring is filling  */
    uint32_t f_min_hz,                      /**< lowest tone, Hz                           */
             f_max_hz,                      /**< highest tone, Hz                          */
             split_pct,                     /**< split, percent                            */
             gap_ms,                        /**< gap, ms                                   */
             min_periods;                   /**< periods a beep needs                      */
/**
 * @brief f_min_hz, f_max_hz: tone range, Hz; split_pct: percent; gap_ms: ms;
 *        min_periods: periods a beep needs.
 */
} tone_cmd_t;

/** @brief A finished beep, in the TONE page's units. */
typedef struct {
    uint32_t start_ms;       /**< first rise, ms since the capture started  */
    uint16_t len_dms;        /**< first rise to last edge, 0.1ms, clipped   */
    uint16_t freq_dhz;       /**< 0.1Hz, clipped                            */
    uint16_t bursts;         /**< clipped                                   */
    uint16_t carrier_hhz;    /**< 100Hz steps, clipped; 0 unchopped         */
    uint8_t  flags;          /**< tone_beep_flag_t                          */
} tone_rec_t;

/** @brief What the service reports each pass. */
typedef struct {
    uint16_t gen;            /**< the set-up generation it ran under        */
    uint16_t cap_gen;        /**< and the capture generation                */
    bool     running;        /**< an order with run was taken               */
    bool     overrun;        /**< a lap or a FIFO overflow since the start  */
    bool     beep;           /**< a run of bursts is under way              */
    bool     tone;           /**< the last window held a tone               */
    uint16_t window;         /**< that window's number, modulo 65536        */
    uint16_t win_freq_dhz;              /**< the last window's frequency, 0.1Hz */
    uint16_t win_periods;               /**< the last window's periods */
    uint32_t lost;           /**< beeps dropped by the detector's queue     */
    uint32_t glitches;                  /**< glitches counted */
} tone_status_t;

/** @brief State of the tone service: the detector, the ring reader and the
 *         generations last seen. */
typedef struct {
    tone_t         det;
    tone_holdoff_t hold;     /**< only its horizon is used                  */
    edge_ring_t    ring;                /**< the ring of edges */
    uint16_t gen,                           /**< the set-up generation last seen      */
             cap_gen;                       /**< the capture generation last seen     */
    bool           active;              /**< whether the capture runs */
    /** @brief Whether a lap or a FIFO overflow happened since the start */
    bool           overrun;
    /**
     * @brief Passes still to discard after a FIFO overflow: the one that sees
     *        it and the next.
     */
    uint8_t        discard;
    /**
     * @brief One pass's words as edges.
     *
     * Here, not on the stack: core 1's stack is small.
     */
    tone_edge_t    batch[TONE_SVC_BATCH];
} tone_svc_t;

/**
 * @brief Clear the service state.
 *
 * @param s  Service state; cleared to zero.
 */
void tone_svc_init(tone_svc_t *s);

/**
 * @brief @p us microseconds after the capture started, in ticks.
 *
 * @param us  Time, microseconds.
 * @return    Ticks of the capture's clock.
 */
uint64_t tone_svc_ticks(uint64_t us);

/**
 * @brief One pass.
 *
 * @p ring is the capture ring (TONE_RING_WORDS words), @p wr the slot the DMA
 * writes next, @p now the present in ticks since the capture started, @p
 * fifo_overrun true when the state machine dropped a word (its FIFO was full):
 * the words read in that pass and in the next are discarded, the beep under way
 * ends and the status shows the overrun. The caller reads the flag before @p
 * wr, so the next pass's boundary lies after the flag was cleared; words the
 * FIFO still held at the drop reach the ring before that boundary unless the
 * DMA stalls for a whole pass, 1ms, and a stall that long overflows the FIFO
 * again.  The beeps finished go to @p rec, at most TONE_SVC_BEEPS; the count is
 * returned.  @p st is always written.
 *
 * @param s            Service state.
 * @param cmd          Order from core 0.
 * @param ring         Capture ring, TONE_RING_WORDS words.
 * @param wr           Slot the DMA writes next.
 * @param now          Present, in ticks since the capture started.
 * @param fifo_overrun true when the state machine dropped a word.
 * @param rec          Receives the finished beeps; NULL to take none.
 * @param st           Receives the status; always written.
 * @return             Number of beeps written to @p rec.
 */
size_t tone_svc_step(tone_svc_t *s, const tone_cmd_t *cmd,
                     const uint32_t *ring, uint32_t wr, uint64_t now,
                     bool fifo_overrun, tone_rec_t *rec, tone_status_t *st);

/**
 * @brief A detector beep in the page's units, ticks of TONE_SVC_TICK_HZ.
 *
 * @param b  Beep from the detector.
 * @param r  Receives the beep in the page's units.
 */
void tone_svc_rec(const tone_beep_t *b, tone_rec_t *r);

/**
 * @brief The detector's configuration for an order: the defaults of tone.h at
 *        the capture's tick, with the order's range, split, gap and minimum.
 *
 * @param cmd  Order from core 0.
 * @param c    Receives the detector settings.
 */
void tone_svc_cfg(const tone_cmd_t *cmd, tone_cfg_t *c);

#ifdef __cplusplus
}
#endif
