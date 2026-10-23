/**
 * @file
 * @brief The frame window and the table of lows behind the panel's console
 *        lines (perf_watch.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "perf_watch.h"

#include <string.h>

void perf_window_start(perf_window_t *w, int screen)
{
    memset(w, 0, sizeof(*w));
    w->screen = screen;
}

void perf_window_add(perf_window_t *w, int screen, uint32_t draw_us,
                     uint32_t wait_us)
{
    if (screen != w->screen) {
        perf_window_start(w, screen);
    }
    if (w->frames == 0u || draw_us < w->draw_min_us) {
        w->draw_min_us = draw_us;
    }
    if (w->frames == 0u || wait_us < w->wait_min_us) {
        w->wait_min_us = wait_us;
    }
    if (draw_us > w->draw_max_us) {
        w->draw_max_us = draw_us;
    }
    if (wait_us > w->wait_max_us) {
        w->wait_max_us = wait_us;
    }
    w->draw_sum_us += draw_us;
    w->wait_sum_us += wait_us;
    ++w->frames;
}

/**
 * @brief @p sum over @p n, rounded to the nearest whole number.
 *
 * @param sum The sum, us.
 * @param n   The count; 0 gives 0.
 * @return The mean, us.
 */
static uint32_t mean(uint64_t sum, uint32_t n)
{
    return (n == 0u) ? 0u : (uint32_t)((sum + n / 2u) / n);
}

uint32_t perf_window_draw_mean_us(const perf_window_t *w)
{
    return mean(w->draw_sum_us, w->frames);
}

uint32_t perf_window_wait_mean_us(const perf_window_t *w)
{
    return mean(w->wait_sum_us, w->frames);
}

void perf_lows_init(perf_lows_t *t)
{
    memset(t, 0, sizeof(*t));
}

bool perf_lows_note(perf_lows_t *t, const char *name, uint32_t least)
{
    char key[PERF_LOWS_NAME];
    const char *src = (name != NULL) ? name : "";
    size_t n = strlen(src);
    if (n > PERF_LOWS_NAME - 1u) {
        n = PERF_LOWS_NAME - 1u;
    }
    memcpy(key, src, n);
    key[n] = '\0';
    for (uint32_t i = 0; i < t->count; ++i) {
        perf_low_t *r = &t->rows[i];
        if (strcmp(r->name, key) == 0) {
            if (least < r->least) {
                r->least = least;
                return true;
            }
            return false;
        }
    }
    if (t->count >= PERF_LOWS_MAX) {
        ++t->dropped;
        return false;
    }
    perf_low_t *r = &t->rows[t->count++];
    memcpy(r->name, key, n + 1u);
    r->least = least;
    return true;
}
