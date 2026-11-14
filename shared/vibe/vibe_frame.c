/**
 * @file
 * @brief The balance node's ADC stream split into channels, and its scale
 *        (vibe_frame.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "vibe_frame.h"

#include <string.h>

void vibe_frame_init(vibe_frame_t *f, const uint8_t *chan, size_t nch)
{
    if (f == NULL) {
        return;
    }
    memset(f, 0, sizeof(*f));
    if (chan == NULL) {
        return;
    }
    if (nch > VIBE_FRAME_CH_MAX) {
        nch = VIBE_FRAME_CH_MAX;
    }
    f->nch = (uint8_t)nch;
    memcpy(f->chan, chan, nch);
}

uint16_t vibe_frame_word(const uint8_t *b, uint8_t *chan)
{
    const uint32_t w = (uint32_t)b[0] | ((uint32_t)b[1] << 8)
                       | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    if (chan != NULL) {
        *chan = (uint8_t)((w >> VIBE_FRAME_CHAN_SHIFT) & VIBE_FRAME_CHAN_MASK);
    }
    return (uint16_t)(w & VIBE_FRAME_DATA_MASK);
}

vibe_split_t vibe_frame_split(vibe_frame_t *f, const uint8_t *b, size_t n,
                              int16_t *const out[], size_t cap)
{
    vibe_split_t r = { 0u, 0u, false };
    if (f == NULL || b == NULL || out == NULL || f->nch == 0u) {
        return r;
    }
    while (r.used + VIBE_FRAME_WORD <= n && r.rows < cap) {
        uint8_t ch = 0u;
        const uint16_t raw = vibe_frame_word(b + r.used, &ch);
        if (!f->synced) {
            if (ch != f->chan[0]) {
                ++f->skipped;
                r.used += VIBE_FRAME_WORD;
                continue;
            }
            f->synced = true;
            f->at = 0u;
        } else if (ch != f->chan[f->at]) {
            /* Conversions are missing: the round is lost and the words
             * from here on stand on another clock. */
            ++f->breaks;
            f->synced = false;
            f->at = 0u;
            r.broke = true;
            return r;
        }
        f->part[f->at] = (int16_t)raw;
        r.used += VIBE_FRAME_WORD;
        if (++f->at == f->nch) {
            for (uint8_t k = 0u; k < f->nch; ++k) {
                out[k][r.rows] = f->part[k];
            }
            ++r.rows;
            f->at = 0u;
        }
    }
    return r;
}

bool vibe_frame_whole(uint32_t seen, uint32_t before, uint32_t after)
{
    return before == seen && after == seen;
}

uint64_t vibe_frame_time(uint64_t done_us, uint32_t done_bytes, uint32_t at_bytes,
                         uint32_t ns_per_conv)
{
    const uint32_t ahead = done_bytes - at_bytes;
    if (ahead > UINT32_MAX / 2u) {
        return done_us;         /* past the newest frame: wrapped negative */
    }
    const uint64_t ns = (uint64_t)(ahead / VIBE_FRAME_WORD) * ns_per_conv;
    const uint64_t us = (ns + 500u) / 1000u;
    return us > done_us ? 0u : done_us - us;
}

void vibe_adc_cal_nominal(vibe_adc_cal_t *c)
{
    if (c == NULL) {
        return;
    }
    c->raw_lo = (uint16_t)VIBE_CAL_RAW_LO;
    c->raw_hi = (uint16_t)VIBE_CAL_RAW_HI;
    c->mv_lo = (uint16_t)((VIBE_CAL_RAW_LO * VIBE_ADC_NOMINAL_MV + 2048u) / 4096u);
    c->mv_hi = (uint16_t)((VIBE_CAL_RAW_HI * VIBE_ADC_NOMINAL_MV + 2048u) / 4096u);
}

bool vibe_adc_cal_ok(const vibe_adc_cal_t *c)
{
    return c != NULL && c->raw_hi > c->raw_lo && c->mv_hi > c->mv_lo;
}

/**
 * @brief a / b rounded to the nearest, for a signed a and b above 0.
 *
 * @param a  The dividend.
 * @param b  The divisor, above 0.
 * @return   The rounded quotient.
 */
static int64_t div_round(int64_t a, int64_t b)
{
    return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b);
}

uint16_t vibe_cal_counts(const vibe_adc_cal_t *c, uint32_t mv, uint16_t adc_max)
{
    if (!vibe_adc_cal_ok(c)) {
        return 0u;
    }
    const int64_t draw = (int64_t)c->raw_hi - c->raw_lo;
    const int64_t dmv = (int64_t)c->mv_hi - c->mv_lo;
    const int64_t raw = (int64_t)c->raw_lo
                        + div_round(((int64_t)mv - c->mv_lo) * draw, dmv);
    if (raw < 0) {
        return 0u;
    }
    return raw > adc_max ? adc_max : (uint16_t)raw;
}

uint32_t vibe_cal_mv(const vibe_adc_cal_t *c, uint32_t raw_x16)
{
    if (!vibe_adc_cal_ok(c)) {
        return 0u;
    }
    const int64_t draw = (int64_t)c->raw_hi - c->raw_lo;
    const int64_t dmv = (int64_t)c->mv_hi - c->mv_lo;
    const int64_t mv = (int64_t)c->mv_lo
                       + div_round(((int64_t)raw_x16 - 16 * (int64_t)c->raw_lo) * dmv,
                                   16 * draw);
    return mv < 0 ? 0u : (uint32_t)mv;
}

bool vibe_cfg_scale(vibe_cfg_t *cfg, const vibe_adc_cal_t *c, uint32_t uv_per_g,
                    uint32_t dc_min_mv, uint32_t dc_max_mv)
{
    if (cfg == NULL || !vibe_adc_cal_ok(c) || uv_per_g == 0u
        || dc_max_mv <= dc_min_mv) {
        return false;
    }
    /* µg a count = (dmv x 1,000µV) / draw x 10^6µg/g / uv_per_g; the
     * numerator stays under 2^63 for every 16-bit dmv. */
    const uint64_t num = (uint64_t)(c->mv_hi - c->mv_lo) * 1000000000ull * 65536ull;
    const uint64_t den = (uint64_t)(c->raw_hi - c->raw_lo) * uv_per_g;
    const uint64_t q16 = (num + den / 2u) / den;
    cfg->ug_per_count_q16 = q16 > UINT32_MAX ? UINT32_MAX : (uint32_t)q16;
    cfg->dc_min = vibe_cal_counts(c, dc_min_mv, cfg->adc_max);
    cfg->dc_max = vibe_cal_counts(c, dc_max_mv, cfg->adc_max);
    return true;
}
