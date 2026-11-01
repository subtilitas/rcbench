/*
 * The text interface of the simulated ESC in ew_sim.h: 38400baud, no echo, a
 * line limit of 1023 characters, a framing error or an overlong line resetting
 * the ESC, `set` clipping into RAM and `save` copying RAM to flash.  Written
 * from the same specification as this module, so it shares its misreadings.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EW_SIM_TEXT_H
#define EW_SIM_TEXT_H

#include <stdio.h>
#include <stdlib.h>

/* The time with the line high after a reset before the command line opens. */
#define SIM_TEXT_ENTRY_US 2000000u

static void sim_say(sim_t *e, const uint8_t *p, size_t n);

static void sim_text_say(sim_t *e, const char *s)
{
    sim_say(e, (const uint8_t *)s, strlen(s));
}

/* The generic defaults of the 46 numeric keys, in table order. */
static void sim_text_defaults(int16_t *v)
{
    static const int16_t k_defaults[EW_E32_KEYS] = {
        1, 1, 0, 0, 16, 0, 8, 24, 48, 1, 100, 15, 0, 30, 0, 0,
        0, 0, 100, 0, 0, 0, 1000, 1500, 2000, 100, 3200, 0, 0, 0,
        0, 0, 14, 0, 0,
        0, 0, 0, 0, 0, 0, 0,
        25, 50, 0, 0,
    };

    memcpy(v, k_defaults, sizeof(k_defaults));
}

/* The ESC resets: back in its entry state. */
static void sim_text_reset(sim_t *e, uint32_t at)
{
    e->resets++;
    memcpy(e->ram, e->saved, sizeof(e->ram));
    e->open_at = at + SIM_TEXT_ENTRY_US;
}

static void sim_text_line_state(sim_t *e, ew_line_t level)
{
    const bool low = level == EW_LINE_LOW || level == EW_LINE_PARK;

    if (low && !e->low && (int32_t)(e->now - e->open_at) >= 0) {
        e->resets++;
        memcpy(e->ram, e->saved, sizeof(e->ram));
    }
    if (!low && e->low) {
        e->open_at = e->now + SIM_TEXT_ENTRY_US;
    }
    e->low = low;
}

static void sim_text_show_lines(sim_t *e)
{
    char line[32];

    for (size_t i = 0; i < EW_E32_KEYS; ++i) {
        if (i == 42u) {
            sim_text_say(e, "music: ");
            sim_text_say(e, e->music);
            sim_text_say(e, "\n");
        }
        if ((int)i == e->show_drop) {
            continue;
        }
        (void)snprintf(line, sizeof(line), "%s: %d\n", ew_map_e32[i].key,
                       e->ram[i]);
        sim_text_say(e, line);
    }
    if (e->show_extra != NULL) {
        sim_text_say(e, e->show_extra);
    }
}

static void sim_text_set(sim_t *e, char *args)
{
    char *blank = strchr(args, ' ');
    size_t slot = 0u;

    if (blank == NULL) {
        sim_text_say(e, "ERROR\n");
        return;
    }
    const size_t key_n = (size_t)(blank - args);
    const ew_key_t *k = ew_key_find(args, key_n, &slot);
    char *end = NULL;
    long v = strtol(blank + 1, &end, 10);

    if (k == NULL || end == blank + 1 || *end != '\0') {
        sim_text_say(e, "ERROR\n");
        return;
    }
    v = v < k->lo && !(k->has_alt && v == k->alt) ? k->lo : v;
    v = v > k->hi ? k->hi : v;
    if ((int)slot == e->clip_slot && v > e->clip_to) {
        v = e->clip_to;
    }
    e->ram[slot] = (int16_t)v;
    e->sets++;

    char reply[40];

    (void)snprintf(reply, sizeof(reply), "%s: %ld\nOK\n", k->key, v);
    sim_text_say(e, reply);
}

/* One line as the ESC reads it, its LF taken off. */
static void sim_text_command(sim_t *e, char *s, uint8_t fault)
{
    if (strcmp(s, "reset") == 0 || strncmp(s, "throt", 5u) == 0
        || strcmp(s, "play") == 0 || strcmp(s, "beep") == 0) {
        e->forbidden++;
        sim_text_say(e, "OK\n");
    } else if (fault == F_TEXT_ERROR) {
        sim_text_say(e, "ERROR\n");
    } else if (strcmp(s, "info") == 0) {
        sim_text_say(e, e->banner);
        sim_text_say(e, "\nTemp: 25C, ext 31C\nVolt: 16.1V\nCurr: 0.25A\n"
                        "Csum: 12mAh\nERPM: 0\nOK\n");
    } else if (strcmp(s, "show") == 0) {
        sim_text_show_lines(e);
        sim_text_say(e, "OK\n");
    } else if (strncmp(s, "set ", 4u) == 0) {
        sim_text_set(e, s + 4);
    } else if (strcmp(s, "save") == 0) {
        memcpy(e->saved, e->ram, sizeof(e->saved));
        e->saves++;
        if (fault == F_CORRUPT) {
            e->ram[e->fail_byte]++;
        }
        if (e->drift_slot >= 0) {
            e->ram[e->drift_slot]++;
        }
        sim_text_say(e, "OK\n");
    } else {
        sim_text_say(e, "ERROR\n");
    }
}

/* A transaction as the ESC hears it. */
static void sim_text_heard(sim_t *e, const ew_xfer_t *x, uint8_t fault)
{
    char line[EW_LINE_MAX + 2u];
    const uint32_t bit_us = x->rate == (uint8_t)EW_RATE_TEXT ? 26u : 52u;
    const uint32_t end = e->now + (uint32_t)x->n_tx * 10u * bit_us;

    if ((int32_t)(e->now - e->open_at) < 0 || e->low) {
        if (x->rate != (uint8_t)EW_RATE_TEXT) {
            sim_text_reset(e, end);
        }
        return;
    }
    if (x->rate != (uint8_t)EW_RATE_TEXT) {
        /* Eight zero bytes at half the rate are a framing error. */
        sim_text_reset(e, end);
        return;
    }
    if (x->n_tx > EW_LINE_MAX + 1u || x->tx[x->n_tx - 1u] != '\n') {
        sim_text_reset(e, end);
        return;
    }
    memcpy(line, x->tx, x->n_tx);
    line[x->n_tx - 1u] = '\0';
    if (e->mute_word != NULL
        && strncmp(line, e->mute_word, strlen(e->mute_word)) == 0) {
        if (e->mute_heard) {
            sim_text_command(e, line, fault);
            e->n_rx = 0u;
        }
        return;
    }
    sim_text_command(e, line, fault);
}

/* An ESC of the text interface, powered with the line parked. */
static void sim_init_e32(sim_t *e, const char *banner)
{
    memset(e, 0, sizeof(*e));
    e->kind = SIM_E32;
    e->line = EW_LINE_PARK;
    e->low = true;
    e->clip_slot = -1;
    e->show_drop = -1;
    e->drift_slot = -1;
    (void)snprintf(e->banner, sizeof(e->banner), "%s", banner);
    (void)snprintf(e->music, sizeof(e->music), "dfa#");
    sim_text_defaults(e->saved);
    memcpy(e->ram, e->saved, sizeof(e->ram));
}

#endif /* EW_SIM_TEXT_H */
