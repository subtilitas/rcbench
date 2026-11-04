/*
 * What the suites of protocols/hc05's machine share: the sensor's address,
 * the action log's lookups, a hand-driven step and feed, and the runs to a
 * scan's AT+INQ and to ANY's AT+RESET.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HC05_RIG_H
#define HC05_RIG_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "greatest.h"

#include "hc05.h"
#include "hc05_sim.h"

#pragma GCC diagnostic ignored "-Wunused-function"

/** The address of the sensor in the cases, as +INQ prints it. */
#define SENSOR "98D3:31:FD5C81"
#define SENSOR_CMD "98D3,31,FD5C81"

/* The n-th action of kind @p kind in the sim's log, or NULL. */
static const sim_log_t *nth_act(const sim_t *s, hc05_act_kind_t kind,
                                unsigned n)
{
    for (unsigned i = 0; i < s->nlog; ++i) {
        if (s->log[i].act.kind == kind && n-- == 0u) {
            return &s->log[i];
        }
    }
    return NULL;
}

/* How many actions of kind @p kind the log holds. */
static unsigned count_act(const sim_t *s, hc05_act_kind_t kind)
{
    unsigned n = 0;
    for (unsigned i = 0; i < s->nlog; ++i) {
        n += s->log[i].act.kind == kind;
    }
    return n;
}

/* Hold the sim's command log to @p want, @p n commands. */
static void check_cmds(const sim_t *s, const char *const *want, unsigned n)
{
    CHECK_EQ(s->ncmd, n);
    for (unsigned i = 0; i < n && i < s->ncmd; ++i) {
        CHECK_STR_EQ(s->cmds[i], want[i]);
    }
}

/* Step @p h at @p now until it has no action; @return the last one's
 * kind, HC05_ACT_NONE when there was none. */
static hc05_act_kind_t step_all(hc05_t *h, uint32_t now, hc05_act_t *last)
{
    hc05_act_t a;
    hc05_act_kind_t kind = HC05_ACT_NONE;
    if (last != NULL) {
        memset(last, 0, sizeof *last);
    }
    while (hc05_step(h, now, &a)) {
        kind = a.kind;
        if (last != NULL) {
            *last = a;
        }
    }
    return kind;
}

/* Feed one line, CR LF added. */
static void feed_line(hc05_t *h, const char *line)
{
    hc05_feed(h, (const uint8_t *)line, strlen(line));
    hc05_feed(h, (const uint8_t *)"\r\n", 2u);
}

/* Whether @p a is a SEND of @p cmd plus CR LF. */
static bool is_send(const hc05_act_t *a, const char *cmd)
{
    const size_t n = strlen(cmd);
    return a->kind == HC05_ACT_SEND && a->len == n + 2u
           && memcmp(a->data, cmd, n) == 0 && a->data[n] == '\r'
           && a->data[n + 1u] == '\n';
}

/* Run @p op on a fresh 2.0 module with the sensor in range; @return the
 * time the run ended. */
static uint32_t run_op(hc05_t *h, sim_t *s, hc05_op_t op)
{
    sim_init(s, 2, HC05_BAUD_OLD);
    sim_add_dev(s, SENSOR, 0x1F00u, 0xFFC0u, "HC-06", 3000u);
    hc05_init(h);
    switch (op) {
    case HC05_OP_PROBE:
        CHECK(hc05_probe(h, 0));
        break;
    case HC05_OP_SCAN:
        CHECK(hc05_scan(h, 0));
        break;
    case HC05_OP_BIND:
        CHECK(hc05_bind(h, SENSOR, "1234", 0));
        break;
    default:
        CHECK(hc05_any(h, "1234", 0));
        break;
    }
    return sim_run(s, h, 0, 80000u);
}

/* ANY by hand up to its AT+RESET, sent at the returned time. */
static uint32_t any_to_reset(hc05_t *h, uint32_t t0)
{
    hc05_act_t a;
    hc05_init(h);
    CHECK(hc05_any(h, "1234", t0));
    (void)step_all(h, t0, NULL);
    for (int i = 0; i < 5; ++i) {
        (void)step_all(h, t0 + 100u, NULL);
        feed_line(h, "OK");
    }
    CHECK(step_all(h, t0 + 100u, &a) == HC05_ACT_SEND
          && is_send(&a, "AT+RESET"));
    return t0 + 100u;
}

/* A scan driven by hand up to its AT+INQ at @p t0 + 100; @return the
 * time AT+INQ went out. */
static uint32_t scan_to_inq(hc05_t *h, uint32_t t0)
{
    hc05_act_t a;
    hc05_init(h);
    CHECK(hc05_scan(h, t0));
    CHECK_EQ(step_all(h, t0, NULL), HC05_ACT_KEY);
    CHECK(step_all(h, t0 + 100u, &a) == HC05_ACT_SEND && is_send(&a, "AT"));
    feed_line(h, "OK");
    CHECK(step_all(h, t0 + 100u, &a) == HC05_ACT_SEND
          && is_send(&a, "AT+ROLE=1"));
    feed_line(h, "OK");
    CHECK(step_all(h, t0 + 100u, &a) == HC05_ACT_SEND
          && is_send(&a, "AT+INIT"));
    feed_line(h, "OK");
    CHECK(step_all(h, t0 + 100u, &a) == HC05_ACT_SEND
          && is_send(&a, "AT+INQM=1,9,9"));
    feed_line(h, "OK");
    CHECK(step_all(h, t0 + 100u, &a) == HC05_ACT_SEND
          && is_send(&a, "AT+INQ"));
    return t0 + 100u;
}

#endif
