/**
 * @file inj_rig.h
 * @brief The touch injector's core driven as the RP2350 build drives it,
 * with a simulated panel that reads the emulator every 10 ms exactly as
 * firmware/panel/components/gt911/gt911.c does: the status byte, the points
 * when the status says ready, then the acknowledge.
 *
 * Only static inline helpers and the state they use.
 *
 * Used by:
 * - test_inj_emu.c
 * - test_inj_multi.c
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "injector.h"

static injector_t inj;
static uint32_t   now;

/** @brief One point of a frame the simulated panel read. */
typedef struct {
    uint8_t  id;   /**< track ID */
    uint16_t x;    /**< column   */
    uint16_t y;    /**< row      */
} seen_pt_t;

/** @brief What the simulated panel saw: one entry per frame it read. */
typedef struct {
    uint32_t  t;                    /**< when it read the frame        */
    uint8_t   n;                    /**< points                        */
    uint16_t  x, y;                 /**< the first point               */
    seen_pt_t pt[GT911_EMU_POINTS]; /**< every point                   */
    uint8_t   raw[GT911_EMU_POINTS * GT911_EMU_STRIDE]; /**< the bytes */
} seen_t;

#define SEEN_MAX 4096

static seen_t   seen[SEEN_MAX];
static int      nseen;
static bool     panel_on;
static uint32_t panel_period = 10;

/* -------------------------------------------------------- a GT911 master */

/** @brief The master writes @p n bytes from @p reg on. */
static inline void m_write(uint16_t reg, const uint8_t *data, int n)
{
    gt911_emu_rx(&inj.emu, (uint8_t)(reg >> 8));
    gt911_emu_rx(&inj.emu, (uint8_t)(reg & 0xFFu));
    for (int i = 0; i < n; ++i) {
        gt911_emu_rx(&inj.emu, data[i]);
    }
    gt911_emu_finish(&inj.emu);
}

/** @brief The master reads @p n bytes from @p reg on. */
static inline void m_read(uint16_t reg, uint8_t *out, int n)
{
    gt911_emu_rx(&inj.emu, (uint8_t)(reg >> 8));
    gt911_emu_rx(&inj.emu, (uint8_t)(reg & 0xFFu));
    gt911_emu_finish(&inj.emu);   /* the repeated START */
    for (int i = 0; i < n; ++i) {
        out[i] = gt911_emu_tx(&inj.emu);
    }
    gt911_emu_finish(&inj.emu);
}

/** @brief gt911_read(): status, points if ready, acknowledge. */
static inline void panel_poll(void)
{
    uint8_t st = 0;
    m_read(GT911_EMU_REG_STATUS, &st, 1);
    if ((st & 0x80u) == 0u) {
        return;
    }
    const int n = st & 0x0F;
    seen_t *s = &seen[nseen < SEEN_MAX - 1 ? nseen++ : nseen];
    memset(s, 0, sizeof(*s));
    s->t = now;
    s->n = (uint8_t)n;
    if (n > 0 && n <= (int)GT911_EMU_POINTS) {
        m_read(GT911_EMU_REG_POINT1, s->raw, n * (int)GT911_EMU_STRIDE);
        for (int i = 0; i < n; ++i) {
            const uint8_t *p = &s->raw[i * (int)GT911_EMU_STRIDE];
            s->pt[i].id = p[0];
            s->pt[i].x = (uint16_t)(p[1] | (p[2] << 8));
            s->pt[i].y = (uint16_t)(p[3] | (p[4] << 8));
        }
        s->x = s->pt[0].x;
        s->y = s->pt[0].y;
    }
    const uint8_t zero = 0;
    m_write(GT911_EMU_REG_STATUS, &zero, 1);
}

/** @brief The point of contact @p id in frame @p s, or NULL. */
static inline const seen_pt_t *seen_id(const seen_t *s, uint8_t id)
{
    for (int i = 0; i < s->n; ++i) {
        if (s->pt[i].id == id) {
            return &s->pt[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------ the host */

static char replies[64][INJ_REPLY_LEN];
static int  nreplies;

/** @brief Take every reply the injector has queued. */
static inline void drain(void)
{
    char line[INJ_REPLY_LEN];
    while (injector_take_reply(&inj, line, sizeof(line))) {
        if (nreplies < 64) {
            snprintf(replies[nreplies++], INJ_REPLY_LEN, "%s", line);
        }
    }
}

/** @brief Send @p line and its LF. */
static inline void send(const char *line)
{
    injector_input(&inj, line, strlen(line), now);
    injector_input(&inj, "\n", 1, now);
    drain();
}

/** @brief The newest reply. */
static inline const char *last(void)
{
    return nreplies > 0 ? replies[nreplies - 1] : "";
}

/** @brief The first reply that starts with @p prefix, or NULL. */
static inline const char *reply_with(const char *prefix)
{
    for (int k = 0; k < nreplies; ++k) {
        if (strncmp(replies[k], prefix, strlen(prefix)) == 0) {
            return replies[k];
        }
    }
    return NULL;
}

/** @brief Run @p ms milliseconds: the injector every 1 ms, the panel every
 *  panel_period ms while it is on. */
static inline void run(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; ++i) {
        ++now;
        if (panel_on && now % panel_period == 0u) {
            panel_poll();
        }
        injector_poll(&inj, now);
        drain();
    }
}

/** @brief Run until a reply starts with @p prefix, for at most @p ms. */
static inline bool run_until(const char *prefix, uint32_t ms)
{
    for (uint32_t i = 0; i < ms; ++i) {
        if (reply_with(prefix) != NULL) {
            return true;
        }
        run(1);
    }
    return false;
}

/** @brief A reset injector at @p t0, the panel off. */
static inline void fresh(uint32_t t0)
{
    injector_init(&inj);
    now = t0;
    nseen = 0;
    nreplies = 0;
    panel_on = false;
    panel_period = 10;
}

/** @brief An ok verdict and the confirm. */
static inline void confirm_ok(void)
{
    inj_line_result_t r = { .verdict = INJ_WIRING_OK, .tx = 50,
                            .agree_pct = 100, .end_state = 3,
                            .rate_ksps = 24500 };
    send("wiring");
    CHECK(injector_take_wiring(&inj));
    injector_wiring_done(&inj, &r);
    drain();
    send("confirm scl=GP5");
    CHECK_STR_EQ(last(), "ok confirm");
    CHECK(injector_take_attach(&inj));
    nreplies = 0;
}

/** @brief An active injector with the panel polling every 10 ms. */
static inline void active(uint32_t t0)
{
    fresh(t0);
    confirm_ok();
    panel_on = true;
}
