/*
 * SPDX-License-Identifier: MIT
 */

#include "injector.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MAX_TOKENS 7

/* ------------------------------------------------------------- replies */

static void reply(injector_t *in, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void reply(injector_t *in, const char *fmt, ...)
{
    if (in->r_count == INJ_REPLIES) {
        /* Full: the oldest goes, so the newest state is what is said. */
        in->r_head = (uint8_t)((in->r_head + 1u) % INJ_REPLIES);
        --in->r_count;
    }
    const unsigned slot = (in->r_head + in->r_count) % INJ_REPLIES;
    va_list ap;
    va_start(ap, fmt);
    /* The analyser cannot follow va_start through this wrapper and reports the
     * list as uninitialised; it is initialised on the line above. */
    /* NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized) */
    (void)vsnprintf(in->replies[slot], INJ_REPLY_LEN, fmt, ap);
    va_end(ap);
    ++in->r_count;
}

bool injector_take_reply(injector_t *in, char *out, size_t n)
{
    if (in->r_count == 0u || out == NULL || n == 0u) {
        return false;
    }
    (void)snprintf(out, n, "%s", in->replies[in->r_head]);
    in->r_head = (uint8_t)((in->r_head + 1u) % INJ_REPLIES);
    --in->r_count;
    return true;
}

const char *inj_gesture_name(inj_gesture_t g)
{
    switch (g) {
    case INJ_G_TAP:     return "tap";
    case INJ_G_HOLD:    return "hold";
    case INJ_G_SWIPE:   return "swipe";
    case INJ_G_RELEASE: return "release";
    default:            return "idle";
    }
}

/* ----------------------------------------------------------- the state */

void injector_init(injector_t *in)
{
    memset(in, 0, sizeof(*in));
    gt911_emu_init(&in->emu, (uint16_t)INJ_RES_X, (uint16_t)INJ_RES_Y);
}

static void gesture_end(injector_t *in)
{
    in->kind = INJ_G_NONE;
    in->releasing = false;
    in->pending = false;
    in->published = false;
    in->down_published = false;
    in->first_acked = false;
    in->frames = 0;
    in->slowed = false;
    in->end_published = false;
    in->end_acked = false;
}

static void gesture_start(injector_t *in, inj_gesture_t kind)
{
    gesture_end(in);
    in->kind = kind;
    in->failed = false;
}

/* A gesture under way stops.  A contact the panel may have seen down gets
 * its release frame, with no reply of its own; one never published needs
 * none. */
static void release_contact(injector_t *in)
{
    if (in->kind == INJ_G_NONE || in->kind == INJ_G_RELEASE) {
        return;
    }
    if (in->down_published) {
        /* A frame still out is waited for, then the release follows. */
        in->kind = INJ_G_RELEASE;
        return;
    }
    gesture_end(in);
    in->withdraw_due = true;
}

/* --------------------------------------------------------- the commands */

/* A decimal integer, with an optional minus sign; false on anything else.
 * Ten digits or more read as out of range rather than as a syntax error. */
static bool parse_int(const char *s, long *out)
{
    bool neg = false;
    if (*s == '-') {
        neg = true;
        ++s;
    }
    if (*s == '\0') {
        return false;
    }
    long v = 0;
    int digits = 0;
    for (; *s != '\0'; ++s) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        if (digits < 10) {
            v = v * 10 + (*s - '0');
        }
        ++digits;
    }
    if (digits >= 10) {
        v = 2147483647L;
    }
    *out = neg ? -v : v;
    return true;
}

typedef struct {
    const char *name;
    long        max;     /* inclusive; the minimum is 0, or 1 for ms */
} field_t;

static const field_t k_x  = { "x",  (long)INJ_RES_X - 1 };
static const field_t k_y  = { "y",  (long)INJ_RES_Y - 1 };
static const field_t k_ms = { "ms", (long)INJ_MS_MAX };

/* Parse @p tok against @p f; on failure the error is replied. */
static bool field(injector_t *in, const char *cmd, const char *tok,
                  const field_t *f, long *out)
{
    long v = 0;
    if (!parse_int(tok, &v)) {
        reply(in, "err syntax %s", cmd);
        return false;
    }
    const long min = (f == &k_ms) ? 1 : 0;
    if (v < min || v > f->max) {
        reply(in, "err range %s", f->name);
        return false;
    }
    *out = v;
    return true;
}

/* tap, hold and swipe: the checks they share, then the start. */
static void cmd_gesture(injector_t *in, inj_gesture_t kind, char **tok,
                        int ntok)
{
    const char *cmd = inj_gesture_name(kind);
    int want_min = 3, want_max = 4;
    if (kind == INJ_G_HOLD) {
        want_min = want_max = 4;
    } else if (kind == INJ_G_SWIPE) {
        want_min = want_max = 6;
    }
    if (ntok < want_min || ntok > want_max) {
        reply(in, "err syntax %s", cmd);
        return;
    }
    long v[5] = { 0, 0, 0, 0, (long)INJ_TAP_MS };
    const field_t *const tap_fields[] = { &k_x, &k_y, &k_ms };
    const field_t *const swipe_fields[] = { &k_x, &k_y, &k_x, &k_y, &k_ms };
    const field_t *const *fields = (kind == INJ_G_SWIPE) ? swipe_fields
                                                         : tap_fields;
    for (int i = 1; i < ntok; ++i) {
        long *dst = (kind == INJ_G_SWIPE || i < 3) ? &v[i - 1] : &v[4];
        if (!field(in, cmd, tok[i], fields[i - 1], dst)) {
            return;
        }
    }
    if (in->state != INJ_ACTIVE) {
        reply(in, "err unconfirmed");
        return;
    }
    if (in->kind != INJ_G_NONE || in->wiring_running) {
        reply(in, "err busy");
        return;
    }
    gesture_start(in, kind);
    in->x0 = (uint16_t)v[0];
    in->y0 = (uint16_t)v[1];
    if (kind == INJ_G_SWIPE) {
        in->x1 = (uint16_t)v[2];
        in->y1 = (uint16_t)v[3];
    } else {
        in->x1 = in->x0;
        in->y1 = in->y0;
    }
    in->ms = (uint32_t)v[4];
    in->px = in->x0;
    in->py = in->y0;
    reply(in, "ok %s", cmd);
}

static void cmd_status(injector_t *in, uint32_t now_ms)
{
    const char *state = (in->state == INJ_LISTEN)
                            ? "listen" : inj_gesture_name(in->kind);
    const bool down = in->kind != INJ_G_NONE && in->kind != INJ_G_RELEASE
                      && in->down_published && !in->releasing;
    char age[16];
    if (in->any_read) {
        (void)snprintf(age, sizeof(age), "%lu",
                       (unsigned long)(uint32_t)(now_ms - in->t_last_read));
    } else {
        (void)snprintf(age, sizeof(age), "none");
    }
    reply(in, "status %s reads=%lu acks=%lu last_read_ms=%s down=%d x=%u y=%u",
          state, (unsigned long)in->emu.reads, (unsigned long)in->emu.acks,
          age, down ? 1 : 0, (unsigned)in->px, (unsigned)in->py);
}

static void cmd_wiring(injector_t *in, int ntok)
{
    if (ntok != 1) {
        reply(in, "err syntax wiring");
    } else if (in->state != INJ_LISTEN) {
        reply(in, "err state");
    } else if (in->wiring_running) {
        reply(in, "err busy");
    } else {
        in->wiring_running = true;
        in->wiring_wanted = true;
        reply(in, "ok wiring");
    }
}

static void cmd_confirm(injector_t *in, char **tok, int ntok)
{
    if (ntok != 2 || strcmp(tok[1], "scl=GP5") != 0) {
        reply(in, "err syntax confirm");
    } else if (in->state != INJ_LISTEN) {
        reply(in, "err state");
    } else if (in->wiring_running) {
        reply(in, "err busy");
    } else if (in->wiring.verdict == INJ_WIRING_SWAPPED) {
        reply(in, "err swapped");
    } else if (in->wiring.verdict != INJ_WIRING_OK) {
        reply(in, "err noverdict");
    } else {
        in->state = INJ_ACTIVE;
        in->attach_wanted = true;
        /* What the emulator counted while nothing answered is nobody's. */
        in->seen_reads = in->emu.reads;
        in->seen_acks = in->emu.acks;
        in->seen_product = in->emu.product_reads;
        reply(in, "ok confirm");
    }
}

static void cmd_release(injector_t *in, int ntok)
{
    if (ntok != 1) {
        reply(in, "err syntax release");
        return;
    }
    if (in->kind != INJ_G_NONE && in->kind != INJ_G_RELEASE) {
        reply(in, "fail %s released", inj_gesture_name(in->kind));
        in->failed = true;
        release_contact(in);
    }
    reply(in, "ok release");
}

static void run_line(injector_t *in, char *line, uint32_t now_ms)
{
    char *tok[MAX_TOKENS];
    int ntok = 0;
    for (char *p = line; *p != '\0';) {
        while (*p == ' ') {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        if (ntok == MAX_TOKENS) {
            ntok = MAX_TOKENS + 1;   /* too many: a syntax error */
            break;
        }
        tok[ntok++] = p;
        while (*p != ' ' && *p != '\0') {
            ++p;
        }
        if (*p == ' ') {
            *p++ = '\0';
        }
    }
    if (ntok == 0) {
        return;
    }
    const char *cmd = tok[0];
    if (ntok > MAX_TOKENS) {
        reply(in, "err syntax %s", cmd);
    } else if (strcmp(cmd, "ping") == 0) {
        reply(in, "pong rcbench-touch %d addr=0x%02x res=%ux%u state=%s",
              INJ_PROTOCOL, (unsigned)GT911_EMU_ADDR, (unsigned)INJ_RES_X,
              (unsigned)INJ_RES_Y,
              in->state == INJ_LISTEN ? "listen" : "active");
    } else if (strcmp(cmd, "status") == 0) {
        cmd_status(in, now_ms);
    } else if (strcmp(cmd, "wiring") == 0) {
        cmd_wiring(in, ntok);
    } else if (strcmp(cmd, "confirm") == 0) {
        cmd_confirm(in, tok, ntok);
    } else if (strcmp(cmd, "tap") == 0) {
        cmd_gesture(in, INJ_G_TAP, tok, ntok);
    } else if (strcmp(cmd, "hold") == 0) {
        cmd_gesture(in, INJ_G_HOLD, tok, ntok);
    } else if (strcmp(cmd, "swipe") == 0) {
        cmd_gesture(in, INJ_G_SWIPE, tok, ntok);
    } else if (strcmp(cmd, "release") == 0) {
        cmd_release(in, ntok);
    } else {
        reply(in, "err unknown %.16s", cmd);
    }
}

void injector_input(injector_t *in, const char *bytes, size_t n,
                    uint32_t now_ms)
{
    for (size_t i = 0; i < n; ++i) {
        const char ch = bytes[i];
        if (ch == '\n') {
            if (in->overflow) {
                reply(in, "err toolong");
            } else {
                in->line[in->len] = '\0';
                run_line(in, in->line, now_ms);
            }
            in->len = 0;
            in->overflow = false;
        } else if (ch == '\r' || in->overflow) {
            continue;   /* CR of a CRLF, or the rest of a line too long */
        } else if (in->len < INJ_LINE_MAX) {
            in->line[in->len++] = ch;
        } else {
            in->overflow = true;
        }
    }
}

/* ------------------------------------------------------------- wiring */

bool injector_take_wiring(injector_t *in)
{
    const bool want = in->wiring_wanted;
    in->wiring_wanted = false;
    return want;
}

void injector_wiring_done(injector_t *in, const inj_line_result_t *r)
{
    in->wiring = *r;
    in->wiring_running = false;
    const unsigned g4 = (r->end_state & INJ_LINE_GP4) ? 1u : 0u;
    const unsigned g5 = (r->end_state & INJ_LINE_GP5) ? 1u : 0u;
    const unsigned long rate10 = (unsigned long)(r->rate_ksps / 100u);
    switch (r->verdict) {
    case INJ_WIRING_OK:
    case INJ_WIRING_SWAPPED: {
        const bool ok = r->verdict == INJ_WIRING_OK;
        reply(in, "wiring %s scl=%s sda=%s tx=%lu agree=%lu rate=%lu.%lu",
              ok ? "ok" : "swapped", ok ? "GP5" : "GP4", ok ? "GP4" : "GP5",
              (unsigned long)r->tx, (unsigned long)r->agree_pct,
              rate10 / 10u, rate10 % 10u);
        break;
    }
    case INJ_WIRING_IDLE:
    case INJ_WIRING_LOW:
        reply(in, "wiring %s gp4=%u gp5=%u", inj_wiring_name(r->verdict),
              g4, g5);
        break;
    default:
        reply(in, "wiring noise edges4=%lu edges5=%lu tx=%lu agree=%lu",
              (unsigned long)r->edges4, (unsigned long)r->edges5,
              (unsigned long)r->tx, (unsigned long)r->agree_pct);
        break;
    }
}

bool injector_take_attach(injector_t *in)
{
    const bool want = in->attach_wanted;
    in->attach_wanted = false;
    return want;
}

void injector_host_closed(injector_t *in)
{
    release_contact(in);
    in->r_count = 0;
    in->len = 0;
    in->overflow = false;
}

/* ------------------------------------------------------------ frames */

static uint16_t lerp(uint16_t a, uint16_t b, uint32_t num, uint32_t den)
{
    const long d = (long)b - (long)a;
    return (uint16_t)((long)a + d * (long)num / (long)den);
}

static long labs_(long v) { return v < 0 ? -v : v; }

/* Where the swipe's next frame goes at @p now_ms. */
static void swipe_point(injector_t *in, uint32_t now_ms, uint16_t *x,
                        uint16_t *y)
{
    uint16_t tx = in->x0, ty = in->y0;
    if (in->first_acked) {
        const uint32_t el = now_ms - in->t_first;
        if (el >= in->ms) {
            tx = in->x1;
            ty = in->y1;
        } else {
            tx = lerp(in->x0, in->x1, el, in->ms);
            ty = lerp(in->y0, in->y1, el, in->ms);
        }
    }
    const long dx = (long)tx - (long)in->px;
    const long dy = (long)ty - (long)in->py;
    const long dist = labs_(dx) + labs_(dy);
    if (in->down_published && dist > (long)INJ_SWIPE_STEP_PX) {
        /* Under the panel's 120 px jump split (touch_map.h). */
        tx = (uint16_t)((long)in->px + dx * (long)INJ_SWIPE_STEP_PX / dist);
        ty = (uint16_t)((long)in->py + dy * (long)INJ_SWIPE_STEP_PX / dist);
        in->slowed = true;
    }
    *x = tx;
    *y = ty;
}

static void finish(injector_t *in, uint32_t now_ms)
{
    if (in->kind != INJ_G_RELEASE) {
        const uint32_t took = in->first_acked ? now_ms - in->t_first : 0u;
        if (in->kind == INJ_G_SWIPE) {
            reply(in, "done swipe frames=%lu ms=%lu slowed=%d",
                  (unsigned long)in->frames, (unsigned long)took,
                  in->slowed ? 1 : 0);
        } else {
            reply(in, "done %s frames=%lu ms=%lu", inj_gesture_name(in->kind),
                  (unsigned long)in->frames, (unsigned long)took);
        }
    }
    gesture_end(in);
}

void injector_poll(injector_t *in, uint32_t now_ms)
{
    gt911_emu_t *e = &in->emu;

    if (e->reads != in->seen_reads) {
        in->seen_reads = e->reads;
        in->any_read = true;
        in->t_last_read = now_ms;
    }
    if (e->product_reads != in->seen_product) {
        /* The panel is starting: it must not find a contact down. */
        in->seen_product = e->product_reads;
        in->seen_acks = e->acks;
        if (in->kind != INJ_G_NONE && in->kind != INJ_G_RELEASE) {
            reply(in, "fail %s panel_reset", inj_gesture_name(in->kind));
            in->failed = true;
        }
        gesture_end(in);
        in->withdraw_due = true;
    }
    if (e->acks != in->seen_acks) {
        in->seen_acks = e->acks;
        if (in->pending) {
            in->pending = false;
            ++in->frames;
            if (in->releasing) {
                finish(in, now_ms);
            } else if (in->down_published) {
                if (!in->first_acked) {
                    in->first_acked = true;
                    in->t_first = now_ms;
                }
                if (in->end_published) {
                    in->end_acked = true;
                }
            }
        }
    }
    if (in->withdraw_due && !in->pending && gt911_emu_withdraw(e)) {
        in->withdraw_due = false;
    }
    if (in->kind == INJ_G_NONE) {
        return;
    }
    if (in->pending) {
        if (now_ms - in->t_pub >= INJ_ACK_TIMEOUT_MS) {
            if (in->kind != INJ_G_RELEASE) {
                reply(in, "fail %s no_master", inj_gesture_name(in->kind));
                in->failed = true;
            }
            gesture_end(in);
            in->withdraw_due = !gt911_emu_withdraw(e);
        }
        return;
    }
    if (in->down_published && in->kind != INJ_G_RELEASE
        && now_ms - in->t_last_read >= INJ_ACK_TIMEOUT_MS) {
        /* A contact is down and the panel has stopped reading. */
        reply(in, "fail %s no_master", inj_gesture_name(in->kind));
        in->failed = true;
        gesture_end(in);
        in->withdraw_due = !gt911_emu_withdraw(e);
        return;
    }
    if (in->published && now_ms - in->t_pub < INJ_REPORT_MS) {
        return;
    }

    bool release_now;
    switch (in->kind) {
    case INJ_G_RELEASE: release_now = true; break;
    case INJ_G_SWIPE:   release_now = in->end_acked; break;
    default:
        /* One frame puts the contact down and the panel keeps it, as it
         * keeps any contact while the controller has nothing new; the
         * release goes out at the deadline, not a poll after it. */
        if (in->down_published && !in->first_acked) {
            return;
        }
        release_now = in->first_acked && now_ms - in->t_first >= in->ms;
        if (in->first_acked && !release_now) {
            return;
        }
        break;
    }
    gt911_emu_frame_t f = { .down = !release_now, .x = in->px, .y = in->py };
    if (!release_now) {
        if (in->kind == INJ_G_SWIPE) {
            swipe_point(in, now_ms, &f.x, &f.y);
        } else {
            f.x = in->x0;
            f.y = in->y0;
        }
    }
    if (!gt911_emu_publish(e, &f)) {
        return;   /* a transfer is under way: the next poll tries again */
    }
    in->pending = true;
    in->published = true;
    in->t_pub = now_ms;
    if (release_now) {
        in->releasing = true;
    } else {
        in->down_published = true;
        in->px = f.x;
        in->py = f.y;
        if (f.x == in->x1 && f.y == in->y1) {
            in->end_published = true;
        }
    }
}

/* -------------------------------------------------------------- the LED */

inj_led_t injector_led(const injector_t *in, uint32_t now_ms)
{
    if (in->state == INJ_LISTEN) {
        switch (in->wiring.verdict) {
        case INJ_WIRING_SWAPPED:
        case INJ_WIRING_LOW:
        case INJ_WIRING_NOISE:
            return INJ_LED_WIRING_BAD;
        default:
            return INJ_LED_LISTEN;
        }
    }
    if (in->failed) {
        return INJ_LED_FAILED;
    }
    if (in->kind != INJ_G_NONE && in->kind != INJ_G_RELEASE
        && in->down_published && !in->releasing) {
        return INJ_LED_DOWN;
    }
    if (in->any_read && now_ms - in->t_last_read < INJ_LIVE_MS) {
        return INJ_LED_POLLED;
    }
    return INJ_LED_OFF;
}
