/**
 * @file
 * @brief The balance node's synchronous demodulator: one vibration channel
 *        and one index pulse per revolution in, a report per window out.
 *
 * Input.  Samples of the vibration channel at a fixed rate (10kHz in the
 * balance node), pushed in blocks of any length, and the index edges as
 * positions on the same sample clock in 1/256 sample (Q8).  Position 0 is
 * the first sample pushed after vibe_demod_init(); positions wrap at 2^32
 * Q8 units, 2^24 samples (1,677s at 10kHz), and every comparison is a
 * modular difference.
 *
 * Units.  The span from one index edge to the next is a revolution.  Each
 * sample is given the rotor angle interpolated linearly between the two
 * edges.  Per revolution the module forms, for the harmonics 1x, 2x and the
 * blade-pass (blades x 1x), the sums of x cos(h theta) and x sin(h theta)
 * and the sums of cos and sin themselves, so the revolution's DC is
 * removed exactly.  A window adds the sums of the revolutions that closed
 * in it and turns them into a magnitude and a phase.  The demodulation
 * spans all of the window's revolutions at once, so the edge error of a
 * sample grid that does not divide a revolution falls to the order of
 * 1/(samples in the window), not 1/(samples in a revolution).
 *
 * Revolution checks.  A revolution counts when it holds every sample of
 * its span, lasts VIBE_REV_MIN to VIBE_REV_MAX samples, and its period is
 * within 1/8 of the last counted one.  Without a counted period (at start
 * and after a loss) two periods in a row within 1/8 of each other lock it;
 * with one, three in a row lock a new one.  A missing pulse gives one
 * revolution of twice the period, an extra pulse two short ones: both are
 * refused and counted in vibe_report_t::rejected.  No edge for
 * VIBE_REV_MAX samples is a lost index (VIBE_F_INDEX_LOST).
 *
 * No-index mode.  With an rpm set (vibe_demod_set_rpm()), edges are
 * ignored and the module places its own every few turns of that rpm:
 * units of G turns, G the fewest whole turns that hold 256 samples.  The
 * magnitude is the root mean square of the units' magnitudes, so an rpm
 * that is off by a little loses little; the phase reads 0.
 *
 * Arithmetic.  Integer only: a 1,024-entry cosine table in Q15, angles as
 * 32-bit fractions of a turn, 64-bit sums, a CORDIC (coordinate rotation
 * digital computer) for angle and magnitude, an integer square root.  The
 * ESP32-C6 is RV32IMAC: no floating-point unit, single or double.  No
 * float, no double, no libm.
 *
 * Pure C, no SDK.  Host-tested in test_vibe.
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

/** @brief The most samples one revolution or one unit holds. */
#define VIBE_REV_MAX 4096u
/** @brief The fewest samples a counted revolution holds. */
#define VIBE_REV_MIN 10u
/** @brief The revolutions the quality figure is taken over, at most. */
#define VIBE_QUAL_REVS 16u
/** @brief The index edges waiting for their samples, at most. */
#define VIBE_EDGE_QUEUE 16u
/** @brief The highest blade count; the blade-pass is off at 0. */
#define VIBE_BLADES_MAX 12u
/** @brief The samples a no-index unit holds at least. */
#define VIBE_UNIT_MIN 256u

/** @name Report flags (vibe_report_t::flags) */
/** @{ */
#define VIBE_F_VALID        0x0001u /**< at least one revolution counted: the magnitudes hold */
#define VIBE_F_INDEX_SEEN   0x0002u /**< an index edge arrived in the window */
#define VIBE_F_INDEX_STEADY 0x0004u /**< revolutions counted and none refused or lost */
#define VIBE_F_INDEX_LOST   0x0008u /**< VIBE_REV_MAX samples passed without an edge */
#define VIBE_F_NO_INDEX     0x0010u /**< no-index mode: rpm from the panel, phase 0 */
#define VIBE_F_CLIPPED      0x0020u /**< a sample at 0 or at the ADC's top */
#define VIBE_F_DC_OK        0x0040u /**< the window's DC inside [dc_min, dc_max] */
#define VIBE_F_BLADE_OK     0x0080u /**< the blade-pass is below half the sample rate */
#define VIBE_F_QUALITY      0x0100u /**< quality_pct holds: 2 revolutions or more behind it */
#define VIBE_F_EDGE_DROPPED 0x0200u /**< an edge out of order or past a full queue */
#define VIBE_F_RPM_RANGE    0x0400u /**< no-index mode: the rpm gives a revolution outside VIBE_REV_MIN..VIBE_REV_MAX */
#define VIBE_F_RESTARTED    0x0800u /**< the mode or the blade count changed in the window: its sums hold only the revolutions after the change */
/** @} */

/** @brief The harmonics the module demodulates. */
enum {
    VIBE_H_1X = 0,  /**< once per revolution: imbalance */
    VIBE_H_2X,      /**< twice per revolution: misalignment */
    VIBE_H_BLADE,   /**< blades times per revolution: the blade-pass */
    VIBE_H_COUNT    /**< how many */
};

/** @brief One report: a window's results. */
typedef struct {
    uint32_t seq;          /**< 0 for the first window, +1 each window, wraps */
    uint32_t end_sample;   /**< samples pushed when the window closed, wraps at 2^32 */
    uint16_t flags;        /**< VIBE_F_* */
    uint8_t  revs;         /**< revolutions (no-index: units) counted, saturates at 255 */
    uint8_t  rejected;     /**< revolutions refused, saturates at 255 */
    uint32_t rpm_x10;      /**< rpm in 0.1rpm: from the counted periods, or the rpm set */
    uint16_t spread_pm;    /**< (longest - shortest period) / mean, in 0.1% */
    uint32_t mag_ug[VIBE_H_COUNT];     /**< amplitude of each harmonic, in ug (1e-6g) */
    uint16_t phase_cdeg[VIBE_H_COUNT]; /**< phase of each harmonic, 0.01deg, 0 to 35,999 */
    uint32_t rms_ug;       /**< root mean square of all the window's samples after the DC, in ug */
    uint16_t dc_x16;       /**< the window's mean, in 1/16 ADC count */
    uint8_t  quality_pct;  /**< |mean 1x vector| / mean |1x vector| over the last revolutions, 0 to 100 */
    uint8_t  quality_revs; /**< revolutions behind quality_pct, at most VIBE_QUAL_REVS */
} vibe_report_t;

/**
 * @brief Called once for each window closed.
 *
 * @param r    The report; valid for the call only.
 * @param ctx  The context given to vibe_demod_init().
 */
typedef void (*vibe_report_fn)(const vibe_report_t *r, void *ctx);

/** @brief The demodulator's fixed settings. */
typedef struct {
    uint32_t fs_hz;            /**< samples a second of the vibration channel, 1,000 to 100,000 */
    uint32_t window;           /**< samples per report, 256 to 65,535 */
    uint32_t ug_per_count_q16; /**< ug per ADC count, x 65,536 */
    int32_t  edge_shift_q8;    /**< added to every edge, Q8 samples: +128 for an index channel sampled half a sample after the vibration channel */
    uint8_t  blades;           /**< blade count for the blade-pass, 0 (off) to VIBE_BLADES_MAX */
    uint16_t adc_max;          /**< the ADC's top count; a sample at 0 or here is clipped */
    uint16_t dc_min;           /**< lowest DC, counts, for VIBE_F_DC_OK */
    uint16_t dc_max;           /**< highest DC, counts, for VIBE_F_DC_OK */
} vibe_cfg_t;

/** @brief A counted revolution's 1x vector, kept for the quality figure. */
typedef struct {
    int32_t i_q8;   /**< in-phase amplitude, ADC counts x 256 */
    int32_t q_q8;   /**< quadrature amplitude, ADC counts x 256 */
    uint32_t mag_q8; /**< its length, ADC counts x 256 */
} vibe_vec_t;

/** @brief Sums over a set of samples for each harmonic. */
typedef struct {
    int64_t xc[VIBE_H_COUNT]; /**< sum of x cos(h theta), counts x 2^15 */
    int64_t xs[VIBE_H_COUNT]; /**< sum of x sin(h theta), counts x 2^15 */
    int64_t c[VIBE_H_COUNT];  /**< sum of cos(h theta), x 2^15 */
    int64_t s[VIBE_H_COUNT];  /**< sum of sin(h theta), x 2^15 */
    int64_t x;                /**< sum of x, counts */
    uint32_t n;               /**< samples */
    uint32_t n_blade;         /**< samples whose unit had the blade-pass below half the rate */
    int64_t x_blade;          /**< sum of x over those, counts */
    uint64_t pw[VIBE_H_COUNT]; /**< no-index: sum of each unit's squared amplitude, counts^2 x 2^16 */
    uint32_t units;           /**< units in pw */
    uint32_t units_blade;     /**< units in pw[VIBE_H_BLADE] */
} vibe_sums_t;

/** @brief The demodulator's state.  Opaque: read it through the functions. */
typedef struct {
    vibe_cfg_t cfg;               /**< the settings */
    vibe_report_fn fn;            /**< the report callback */
    void *ctx;                    /**< its context */
    int16_t buf[VIBE_REV_MAX];    /**< the open unit's samples */
    uint32_t nbuf;                /**< samples in buf; buf[0] sits at pos_q8 - 256 nbuf */
    uint32_t pos_q8;              /**< position of the next sample, Q8 */
    uint32_t samples;             /**< samples pushed, wraps */
    bool open;                    /**< a unit is open: t0_q8 holds */
    uint32_t t0_q8;               /**< the open unit's start, Q8 */
    uint32_t q[VIBE_EDGE_QUEUE];  /**< edges waiting for their samples, Q8, shifted */
    uint32_t qn;                  /**< edges in q */
    bool have_last;               /**< last_q8 holds */
    uint32_t last_q8;             /**< the last edge taken, Q8, shifted */
    uint32_t rpm_x10;             /**< no-index mode's rpm, 0 in index mode */
    uint32_t vend_q8;             /**< no-index: the open unit's end, Q8 */
    uint32_t u_turns;             /**< no-index: the open unit's turns */
    bool rpm_bad;                 /**< no-index: the rpm gives a turn outside VIBE_REV_MIN..VIBE_REV_MAX */
    bool ref_ok;                  /**< ref_q8 holds */
    uint32_t ref_q8;              /**< the last counted period, Q8 */
    uint32_t cand_q8;             /**< the period a new lock is building on */
    uint32_t cand_n;              /**< periods in a row on cand_q8 */
    vibe_vec_t ring[VIBE_QUAL_REVS]; /**< the last revolutions' 1x vectors */
    uint32_t ring_n;              /**< entries in ring */
    uint32_t ring_at;             /**< the next entry written */
    /* The window. */
    uint32_t win_left;            /**< samples until the window closes */
    uint32_t seq;                 /**< the next report's seq */
    int32_t dc_ref;               /**< a reference subtracted before the rms sums, counts */
    int64_t sd;                   /**< sum of x - dc_ref */
    uint64_t sdd;                 /**< sum of (x - dc_ref)^2 */
    uint32_t nd;                  /**< samples in sd */
    bool clipped;                 /**< a clipped sample */
    bool edge_seen;               /**< an edge arrived */
    bool edge_dropped;            /**< an edge was dropped */
    bool lost;                    /**< the index was lost */
    bool blade_alias;             /**< a unit's blade-pass at or above half the rate */
    bool restarted;               /**< the sums were cleared by a change of mode or blade count */
    uint32_t revs;                /**< units counted */
    uint32_t rejected;            /**< revolutions refused */
    uint64_t t_sum_q8;            /**< sum of the counted periods, Q8 */
    uint32_t t_min_q8;            /**< shortest counted period */
    uint32_t t_max_q8;            /**< longest counted period */
    vibe_sums_t w;                /**< the window's sums */
} vibe_demod_t;

/**
 * @brief Fills @p cfg with the balance node's settings: 10kHz, 250ms
 *        windows, a 12-bit ADC, 1g = 436 counts (330mV/g at 3.3V over a
 *        3.1V range), no blade-pass, no edge shift, DC from 1.0V to 2.3V.
 *
 * @param cfg  The settings to fill.
 */
void vibe_cfg_default(vibe_cfg_t *cfg);

/**
 * @brief Starts the demodulator: index mode, no unit open, position 0.
 *
 * @param d    The state.
 * @param cfg  The settings; copied.  Out-of-range values are clamped.
 * @param fn   Called for each report; NULL drops them.
 * @param ctx  Passed to @p fn.
 */
void vibe_demod_init(vibe_demod_t *d, const vibe_cfg_t *cfg,
                     vibe_report_fn fn, void *ctx);

/**
 * @brief Sets the blade count for the blade-pass, from the next revolution.
 *
 * A new count clears the window's blade-pass sums, so the window reports
 * the blade-pass of the new count only (VIBE_F_RESTARTED).
 *
 * @param d       The state.
 * @param blades  0 (off) to VIBE_BLADES_MAX; larger is clamped.
 */
void vibe_demod_set_blades(vibe_demod_t *d, uint8_t blades);

/**
 * @brief Chooses the mode: index (0) or no-index at @p rpm_x10.
 *
 * Switching between the modes drops the open revolution, the period lock
 * and the quality history, and clears the window's harmonic sums, rpm and
 * revolution counts, so the window reports the new mode only
 * (VIBE_F_RESTARTED); its rms and DC cover the whole window.  A new rpm in
 * no-index mode applies from the next unit.
 *
 * @param d        The state.
 * @param rpm_x10  rpm in 0.1rpm, 0 for index mode.
 */
void vibe_demod_set_rpm(vibe_demod_t *d, uint32_t rpm_x10);

/**
 * @brief Hands in one index edge.
 *
 * Edges come in order.  @p stamp_q8 is on the vibration channel's sample
 * clock before cfg.edge_shift_q8 is added.  An edge whose samples are
 * already pushed is taken at once; a later one waits for them, at most
 * VIBE_EDGE_QUEUE of them (vibe_demod_feed() has no such limit).  An edge
 * not after the previous one, or past a full queue, is dropped
 * (VIBE_F_EDGE_DROPPED).  Ignored in no-index mode, except that it sets
 * VIBE_F_INDEX_SEEN.
 *
 * @param d         The state.
 * @param stamp_q8  The edge's position, Q8 samples.
 */
void vibe_demod_edge(vibe_demod_t *d, uint32_t stamp_q8);

/**
 * @brief Pushes vibration samples; closes the windows they complete.
 *
 * A revolution counts in the window that is open when its closing edge is
 * taken.  With vibe_demod_feed(), or with each edge handed in before its
 * samples, that is the window holding the first sample after the edge.  An
 * edge handed in after its samples is taken at once, in the window open
 * then: if the window holding the edge has closed, the revolution counts
 * in a later one.
 *
 * @param d  The state.
 * @param x  The samples, ADC counts 0 to cfg.adc_max.
 * @param n  How many.
 */
void vibe_demod_push(vibe_demod_t *d, const int16_t *x, size_t n);

/**
 * @brief Pushes a block of samples with the index edges found in it.
 *
 * The same as vibe_demod_edge() for each edge with vibe_demod_push() of
 * the samples before it in between, so the queue holds at most an edge
 * that the shift moves past the block's end.  The reports do not depend
 * on how a run is cut into blocks.
 *
 * @param d      The state.
 * @param x      The samples.
 * @param n      How many.
 * @param edges  The edges, in order, as for vibe_demod_edge().
 * @param ne     How many.
 */
void vibe_demod_feed(vibe_demod_t *d, const int16_t *x, size_t n,
                     const uint32_t *edges, size_t ne);

/**
 * @brief The position of the next sample to be pushed.
 *
 * @param d  The state.
 * @return   Q8 samples.
 */
uint32_t vibe_demod_pos_q8(const vibe_demod_t *d);

/**
 * @brief Angle and length of a vector, by CORDIC.
 *
 * @param x    The in-phase part.
 * @param y    The quadrature part.
 * @param mag  Its length, rounded down; may be NULL.
 * @return     The angle from +x towards +y, as a fraction of a turn x 2^32.
 */
uint32_t vibe_atan2(int64_t x, int64_t y, uint64_t *mag);

/**
 * @brief Cosine and sine of an angle, by CORDIC.
 *
 * @param turn  The angle as a fraction of a turn x 2^32.
 * @param c     Its cosine x 2^30.
 * @param s     Its sine x 2^30.
 */
void vibe_cossin(uint32_t turn, int32_t *c, int32_t *s);

/**
 * @brief The integer square root.
 *
 * @param v  The value.
 * @return   floor(sqrt(v)).
 */
uint32_t vibe_isqrt64(uint64_t v);

#ifdef __cplusplus
}
#endif
