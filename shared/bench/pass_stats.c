/**
 * @file
 * @brief The IOmcu main loop's pass times; see pass_stats.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "pass_stats.h"

#include <string.h>

void pass_stats_init(pass_stats_t *s)
{
    memset(s, 0, sizeof(*s));
}

void pass_stats_pass(pass_stats_t *s, uint32_t now_us)
{
    if (s->started) {
        const uint32_t len = now_us - s->start_us;
        if (len > s->fig.max_us) {
            s->fig.max_us = len;
        }
        if (len > PASS_STATS_OVER_US) {
            ++s->fig.over;
        }
    }
    s->started  = true;
    s->start_us = now_us;
}

void pass_stats_section(pass_stats_t *s, pass_sec_t sec, uint32_t start_us,
                        uint32_t end_us)
{
    if ((unsigned)sec >= (unsigned)PASS_SEC_COUNT) {
        return;
    }
    const uint32_t len = end_us - start_us;
    if (len > s->fig.sec_max_us[sec]) {
        s->fig.sec_max_us[sec] = len;
    }
}

void pass_stats_take(pass_stats_t *s, pass_figures_t *out)
{
    *out = s->fig;
    memset(&s->fig, 0, sizeof(s->fig));
}
