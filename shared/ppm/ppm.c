/*
 * PPM frame layout.  See ppm.h for the model.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ppm.h"

static const ppm_cfg_t k_default = {
    .mark_us     = PPM_DEFAULT_MARK_US,
    .frame_us    = PPM_DEFAULT_FRAME_US,
    .sync_min_us = PPM_SYNC_MIN_US,
};

/* A zero field means "the default for it", so a caller can set one thing. */
static ppm_cfg_t resolve(const ppm_cfg_t *cfg)
{
    ppm_cfg_t c = (cfg != NULL) ? *cfg : k_default;
    if (c.mark_us == 0u) {
        c.mark_us = PPM_DEFAULT_MARK_US;
    }
    if (c.frame_us == 0u) {
        c.frame_us = PPM_DEFAULT_FRAME_US;
    }
    if (c.sync_min_us == 0u) {
        c.sync_min_us = PPM_SYNC_MIN_US;
    }
    return c;
}

uint32_t ppm_min_frame_us(uint8_t channels, const ppm_cfg_t *cfg)
{
    if (channels == 0u || channels > PPM_MAX_CHANNELS) {
        return 0u;
    }
    const ppm_cfg_t c = resolve(cfg);
    return (uint32_t)channels * OUT_CEILING_US
           + (uint32_t)c.mark_us + (uint32_t)c.sync_min_us;
}

size_t ppm_frame(const uint16_t *channel_us, uint8_t channels,
                 const ppm_cfg_t *cfg, uint16_t *runs, size_t max_runs)
{
    if (channel_us == NULL || runs == NULL) {
        return 0u;
    }
    if (channels == 0u || channels > PPM_MAX_CHANNELS) {
        return 0u;
    }
    const size_t needed = ((size_t)channels + 1u) * 2u;
    if (max_runs < needed) {
        return 0u;
    }
    const ppm_cfg_t c = resolve(cfg);

    /*
     * Nothing is written until the whole frame is known to fit.  A frame
     * emitted up to the point it failed would be a short frame, which a
     * receiver reads as a valid frame with fewer channels rather than as an
     * error.
     */
    uint32_t spent = 0u;
    for (uint8_t i = 0; i < channels; ++i) {
        const uint16_t ch = channel_us[i];
        if (ch < OUT_FLOOR_US || ch > OUT_CEILING_US) {
            return 0u;
        }
        if (ch <= c.mark_us) {
            return 0u;   /* the mark would run past the channel it starts */
        }
        spent += ch;
    }
    spent += c.mark_us;                       /* the terminating mark */
    if (spent + c.sync_min_us > c.frame_us) {
        return 0u;
    }
    const uint32_t sync = (uint32_t)c.frame_us - spent;

    for (uint8_t i = 0; i < channels; ++i) {
        runs[(size_t)i * 2u]        = c.mark_us;
        runs[(size_t)i * 2u + 1u]   = (uint16_t)(channel_us[i] - c.mark_us);
    }
    runs[needed - 2u] = c.mark_us;
    runs[needed - 1u] = (uint16_t)sync;
    return needed;
}
