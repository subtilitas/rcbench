/**
 * @file
 * @brief The frame window, the table of lows and the line schedule behind
 *        the panel's console lines (perf_watch.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "perf_watch.h"

#include <string.h>

_Static_assert(PERF_LOWS_MAX <= 64u, "perf_lows_t::due has 64 bits");

void perf_window_start(perf_window_t *w, int screen)
{
    memset(w, 0, sizeof(*w));
    w->screen = screen;
}

/**
 * @brief Add @p us to @p s, the window's frame number @p n (0 first).
 *
 * @param s  The span.
 * @param n  Frames added to the window before this one.
 * @param us The figure, us.
 */
static void span_add(perf_span_t *s, uint32_t n, uint32_t us)
{
    if (n == 0u || us < s->min_us) {
        s->min_us = us;
    }
    if (us > s->max_us) {
        s->max_us = us;
    }
    s->sum_us += us;
}

void perf_window_add(perf_window_t *w, int screen, uint32_t draw_us,
                     uint32_t wait_us, uint32_t turn_us)
{
    if (screen != w->screen) {
        perf_window_start(w, screen);
    }
    span_add(&w->draw, w->frames, draw_us);
    span_add(&w->wait, w->frames, wait_us);
    span_add(&w->turn, w->frames, turn_us);
    ++w->frames;
}

uint32_t perf_span_mean_us(const perf_span_t *s, uint32_t frames)
{
    return (frames == 0u)
               ? 0u
               : (uint32_t)((s->sum_us + frames / 2u) / frames);
}

void perf_lows_init(perf_lows_t *t)
{
    memset(t, 0, sizeof(*t));
}

void perf_name_copy(char dst[PERF_LOWS_NAME], const char *name)
{
    size_t n = 0u;
    if (name != NULL) {
        while (n < PERF_LOWS_NAME - 1u && name[n] != '\0') {
            dst[n] = name[n];
            ++n;
        }
    }
    dst[n] = '\0';
}

bool perf_lows_note(perf_lows_t *t, const char *name, uint32_t least)
{
    char key[PERF_LOWS_NAME];
    perf_name_copy(key, name);
    const size_t n = strlen(key);
    for (uint32_t i = 0; i < t->count; ++i) {
        perf_low_t *r = &t->rows[i];
        if (strcmp(r->name, key) == 0) {
            if (least < r->least) {
                r->least = least;
                t->due |= (uint64_t)1u << i;
                return true;
            }
            return false;
        }
    }
    if (t->count >= PERF_LOWS_MAX) {
        ++t->dropped;
        return false;
    }
    t->due |= (uint64_t)1u << t->count;
    perf_low_t *r = &t->rows[t->count++];
    memcpy(r->name, key, n + 1u);
    r->least = least;
    return true;
}

int perf_lows_take(perf_lows_t *t)
{
    for (uint32_t i = 0; i < t->count; ++i) {
        const uint64_t bit = (uint64_t)1u << i;
        if ((t->due & bit) != 0u) {
            t->due &= ~bit;
            return (int)i;
        }
    }
    return -1;
}

perf_line_t perf_line_due(uint32_t frames)
{
    switch (frames % PERF_WINDOW_FRAMES) {
    case 0u:
        return PERF_LINE_RATE;
    case 1u:
        return PERF_LINE_WINDOW;
    default:
        return PERF_LINE_LOW;
    }
}
