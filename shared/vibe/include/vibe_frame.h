/**
 * @file
 * @brief The balance node's ADC (analogue-to-digital converter) stream:
 *        a DMA (direct memory access) frame split into its channels, and
 *        the scale from the ADC's calibration and the sensor's
 *        sensitivity.
 *
 * Frames.  The ESP32-C6's ADC in continuous mode converts a pattern of
 * channels in turn, one conversion per trigger, and writes each result as
 * a 4-byte little-endian word: the raw count in bits 0 to 11, the channel
 * in bits 13 to 16 (ESP-IDF v6.1, hal/adc_types.h, adc_digi_output_data_t
 * type2 for the ESP32-C6).  vibe_frame_split() takes the words in the
 * pattern's order and writes one row per round: a sample of every entry.
 * A word of another channel than the one due is a break: conversions were
 * lost, so the sample clock has a gap.  The split stops in front of it;
 * the caller restarts what counts on the clock and calls again, and the
 * splitter waits for the pattern's first channel before it writes again.
 *
 * Scale.  Two points of the ADC's calibration curve give its slope in µV
 * a count; the sensor's sensitivity in µV/g turns that into µg a count for
 * vibe_cfg_t.  The same points turn the DC window and any threshold in mV
 * into counts.
 *
 * Pure C, no SDK.  Host-tested in test_vibe_frame.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vibe_demod.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The most entries in a pattern. */
#define VIBE_FRAME_CH_MAX 4u
/** @brief Bytes of one conversion in a frame (SOC_ADC_DIGI_RESULT_BYTES on the ESP32-C6). */
#define VIBE_FRAME_WORD 4u
/** @brief The raw count's bits in a word. */
#define VIBE_FRAME_DATA_MASK 0x0FFFu
/** @brief The channel's lowest bit in a word. */
#define VIBE_FRAME_CHAN_SHIFT 13u
/** @brief The channel's bits after the shift. */
#define VIBE_FRAME_CHAN_MASK 0x0Fu

/** @brief The splitter's state. */
typedef struct {
    uint8_t  nch;                      /**< entries in the pattern, 1 to VIBE_FRAME_CH_MAX */
    uint8_t  chan[VIBE_FRAME_CH_MAX];  /**< each entry's ADC channel, in conversion order */
    uint8_t  at;                       /**< the entry the next conversion belongs to */
    bool     synced;                   /**< a round is under way: the order holds */
    int16_t  part[VIBE_FRAME_CH_MAX];  /**< the open round's samples */
    uint32_t breaks;                   /**< conversions out of the pattern's order, wraps */
    uint32_t skipped;                  /**< conversions dropped while waiting for entry 0, wraps */
} vibe_frame_t;

/** @brief What one vibe_frame_split() call did. */
typedef struct {
    size_t rows;   /**< complete rounds written */
    size_t used;   /**< bytes taken, a multiple of VIBE_FRAME_WORD */
    bool   broke;  /**< it stopped in front of a conversion out of order */
} vibe_split_t;

/**
 * @brief Starts the splitter: waiting for entry 0, no break counted.
 *
 * @param f     The state.
 * @param chan  Each entry's ADC channel, in conversion order.
 * @param nch   Entries, 1 to VIBE_FRAME_CH_MAX; more are cut.
 */
void vibe_frame_init(vibe_frame_t *f, const uint8_t *chan, size_t nch);

/**
 * @brief The raw count and the channel of one word.
 *
 * @param b     The word's 4 bytes, little-endian.
 * @param chan  Receives the channel; may be NULL.
 * @return      The raw count, 0 to 4095.
 */
uint16_t vibe_frame_word(const uint8_t *b, uint8_t *chan);

/**
 * @brief Splits frame bytes into rows, one sample of every entry a row.
 *
 * Words before the first conversion of entry 0 are skipped (counted in
 * vibe_frame_t::skipped).  A word of another channel than the one due ends
 * the call in front of it with @c broke set: the open round is dropped,
 * the break counted, and the splitter waits for entry 0 again, so the next
 * call skips forward from that word.  A call also ends when @p cap rows are
 * written.  Bytes past the last whole word are not taken.
 *
 * @param f    The state.
 * @param b    The frame's bytes.
 * @param n    How many.
 * @param out  One array per entry, each with room for @p cap samples.
 * @param cap  Rows that fit.
 * @return     Rows written, bytes taken, and whether it broke.
 */
vibe_split_t vibe_frame_split(vibe_frame_t *f, const uint8_t *b, size_t n,
                              int16_t *const out[], size_t cap);

/** @brief Two points of an ADC's calibration curve. */
typedef struct {
    uint16_t raw_lo;  /**< a raw count */
    uint16_t raw_hi;  /**< a higher raw count */
    uint16_t mv_lo;   /**< the voltage at raw_lo, mV */
    uint16_t mv_hi;   /**< the voltage at raw_hi, mV, above mv_lo */
} vibe_adc_cal_t;

/** @brief The raw counts vibe_adc_cal_nominal() and the node read the
 *         calibration at.  @{ */
#define VIBE_CAL_RAW_LO 1000u  /**< 0.81V nominal */
#define VIBE_CAL_RAW_HI 3000u  /**< 2.42V nominal */
/** @} */

/** @brief The ESP32-C6's nominal range at 12dB attenuation: 0 to 3300mV
 *         over 4096 counts (its datasheet v1.6, table 5-6, ATTEN3). */
#define VIBE_ADC_NOMINAL_MV 3300u

/**
 * @brief The nominal curve, for a chip without calibration: 0 to
 *        VIBE_ADC_NOMINAL_MV over 4096 counts, read at VIBE_CAL_RAW_LO and
 *        VIBE_CAL_RAW_HI.
 *
 * @param c  Receives the points.
 */
void vibe_adc_cal_nominal(vibe_adc_cal_t *c);

/**
 * @brief Whether two points make a rising curve.
 *
 * @param c  The points.
 * @return   true for raw_hi above raw_lo and mv_hi above mv_lo.
 */
bool vibe_adc_cal_ok(const vibe_adc_cal_t *c);

/**
 * @brief The raw count at a voltage, on the straight line through the two
 *        points, rounded and held to 0 .. @p adc_max.
 *
 * @param c        The points; vibe_adc_cal_ok().
 * @param mv       The voltage, mV.
 * @param adc_max  The ADC's top count.
 * @return         The count; 0 for points that are not ok.
 */
uint16_t vibe_cal_counts(const vibe_adc_cal_t *c, uint32_t mv, uint16_t adc_max);

/**
 * @brief The voltage at a count in 1/16 count (vibe_report_t::dc_x16), on
 *        the same line, rounded, 0 at the least.
 *
 * @param c       The points; vibe_adc_cal_ok().
 * @param raw_x16 The count x 16.
 * @return        mV; 0 for points that are not ok.
 */
uint32_t vibe_cal_mv(const vibe_adc_cal_t *c, uint32_t raw_x16);

/**
 * @brief Sets the demodulator's scale and DC window from the ADC's curve.
 *
 * ug_per_count_q16 = slope (µV a count) x 10^6 / @p uv_per_g, x 65,536,
 * rounded; dc_min and dc_max are the counts at @p dc_min_mv and
 * @p dc_max_mv.  The other fields stay.
 *
 * @param cfg        The settings to change.
 * @param c          The ADC's points.
 * @param uv_per_g   The sensor's sensitivity, µV/g, above 0.
 * @param dc_min_mv  The lowest DC for VIBE_F_DC_OK, mV.
 * @param dc_max_mv  The highest, mV, above dc_min_mv.
 * @return           false, and @p cfg unchanged, for points that are not
 *                   ok, a sensitivity of 0 or a window that is empty.
 */
bool vibe_cfg_scale(vibe_cfg_t *cfg, const vibe_adc_cal_t *c, uint32_t uv_per_g,
                    uint32_t dc_min_mv, uint32_t dc_max_mv);

#ifdef __cplusplus
}
#endif
