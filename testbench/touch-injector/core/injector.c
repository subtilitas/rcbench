/**
 * @file
 * @brief The touch injector's commands and gestures (injector.h): the line
 *        parser, up to INJ_CONTACTS contacts at once, and the frames that
 *        carry them to the panel.
 *
 * SPDX-License-Identifier: MIT
 */

#include "injector.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/** @brief Tokens a line may hold: swipe's five numbers, its id= and itself. */
#define MAX_TOKENS 7

/** @brief Ids from 0 to INJ_ID_MAX: the size of injector_t.c. */
#define N_IDS ((unsigned)(INJ_ID_MAX + 1u))

/* ------------------------------------------------------------- replies */

static void reply(injector_t *in, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/** @brief Queue one reply line; a full queue drops its oldest. */
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

/* ---------------------------------------------------------- the contacts */

void injector_init(injector_t *in)
{
    memset(in, 0, sizeof(*in));
    gt911_emu_init(&in->emu, (uint16_t)INJ_RES_X, (uint16_t)INJ_RES_Y);
}

/** @brief A tap, hold or swipe under way, not yet released. */
static bool live(const inj_contact_t *c)
{
    return c->kind != INJ_G_NONE && c->kind != INJ_G_RELEASE;
}

/** @brief A gesture under way whose contact the panel has down. */
static bool pressing(const inj_contact_t *c)
{
    return live(c) && c->down_published && !c->lifting;
}

/** @brief Contact ids in use: gestures under way and lifts not yet read. */
static unsigned in_use(const injector_t *in)
{
    unsigned n = 0;
    for (unsigned id = 0; id < N_IDS; ++id) {
        if (in->c[id].kind != INJ_G_NONE) {
            ++n;
        }
    }
    return n;
}

/** @brief The id is free again; the last point stays for `status`. */
static void contact_end(inj_contact_t *c)
{
    const uint16_t px = c->px, py = c->py;
    memset(c, 0, sizeof(*c));
    c->px = px;
    c->py = py;
}

/**
 * @brief A gesture under way stops.
 *
 * A contact the panel may have seen down gets its lift in a frame, with no
 * reply of its own.  One never published needs none, and no frame is
 * withdrawn for it: a frame out carries only contacts put down before.
 *
 * @param c The contact.
 */
static void release_contact(inj_contact_t *c)
{
    if (!live(c)) {
        return;
    }
    if (c->down_published) {
        /* A frame still out is waited for, then the lift follows. */
        c->kind = INJ_G_RELEASE;
        return;
    }
    contact_end(c);
}

/** @brief Every contact ends: each gesture under way gets fail @p why. */
static void fail_all(injector_t *in, const char *why)
{
    for (unsigned id = 0; id < N_IDS; ++id) {
        inj_contact_t *c = &in->c[id];
        if (live(c)) {
            reply(in, "fail %s%s %s", inj_gesture_name(c->kind), c->tag, why);
            in->failed = true;
        }
        if (c->kind != INJ_G_NONE) {
            contact_end(c);
        }
    }
    in->pending = false;
    in->published = false;
}

/* --------------------------------------------------------- the commands */

/**
 * @brief A decimal integer, with an optional minus sign.
 *
 * Ten digits or more read as out of range rather than as a syntax error.
 *
 * @param s The text.
 * @param out The value.
 * @return False on anything else.
 */
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

/** @brief A numeric field: its name in `err range` and its maximum. */
typedef struct {
    const char *name;    /**< as `err range` names it                  */
    long        max;     /**< inclusive; the minimum is 0, or 1 for ms */
} field_t;

static const field_t k_x  = { "x",  (long)INJ_RES_X - 1 }; /**< @brief x */
static const field_t k_y  = { "y",  (long)INJ_RES_Y - 1 }; /**< @brief y */
static const field_t k_ms = { "ms", (long)INJ_MS_MAX };     /**< @brief ms */

/** @brief Parse @p tok against @p f; on failure the error is replied. */
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

/** @brief No id= on the line. */
#define ID_NONE  (-1)
/** @brief A bad id=; the error is replied. */
#define ID_BAD   (-2)

/**
 * @brief Take an id=N off the end of the line.
 * @param in The injector, for the error reply.
 * @param cmd The command, for `err syntax`.
 * @param tok The tokens.
 * @param ntok Their count; one less when an id= was taken.
 * @return The id, ID_NONE when the line has none, ID_BAD after an error.
 */
static int take_id(injector_t *in, const char *cmd, char **tok, int *ntok)
{
    if (*ntok < 2 || strncmp(tok[*ntok - 1], "id=", 3) != 0) {
        return ID_NONE;
    }
    long v = 0;
    if (!parse_int(tok[*ntok - 1] + 3, &v)) {
        reply(in, "err syntax %s", cmd);
        return ID_BAD;
    }
    if (v < 0 || v > (long)INJ_ID_MAX) {
        reply(in, "err range id");
        return ID_BAD;
    }
    --*ntok;
    return (int)v;
}

/** @brief tap, hold and swipe: the checks they share, then the start. */
static void cmd_gesture(injector_t *in, inj_gesture_t kind, char **tok,
                        int ntok)
{
    const char *cmd = inj_gesture_name(kind);
    const int id = take_id(in, cmd, tok, &ntok);
    if (id == ID_BAD) {
        return;
    }
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
    inj_contact_t *c = &in->c[id == ID_NONE ? 0 : id];
    if (c->kind != INJ_G_NONE || in->wiring_running) {
        reply(in, "err busy");
        return;
    }
    if (in_use(in) >= INJ_CONTACTS) {
        reply(in, "err full");
        return;
    }
    contact_end(c);
    c->kind = kind;
    if (id != ID_NONE) {
        (void)snprintf(c->tag, sizeof(c->tag), " id=%d", id);
    }
    in->failed = false;
    c->x0 = (uint16_t)v[0];
    c->y0 = (uint16_t)v[1];
    if (kind == INJ_G_SWIPE) {
        c->x1 = (uint16_t)v[2];
        c->y1 = (uint16_t)v[3];
    } else {
        c->x1 = c->x0;
        c->y1 = c->y0;
    }
    c->ms = (uint32_t)v[4];
    c->px = c->x0;
    c->py = c->y0;
    reply(in, "ok %s%s", cmd, c->tag);
}

/** @brief `status`: the state, the bus counts and contact 0. */
static void cmd_status(injector_t *in, uint32_t now_ms)
{
    const inj_contact_t *c = &in->c[0];
    const char *state = (in->state == INJ_LISTEN)
                            ? "listen" : inj_gesture_name(c->kind);
    char age[16];
    if (in->any_read) {
        (void)snprintf(age, sizeof(age), "%lu",
                       (unsigned long)(uint32_t)(now_ms - in->t_last_read));
    } else {
        (void)snprintf(age, sizeof(age), "none");
    }
    reply(in, "status %s reads=%lu acks=%lu last_read_ms=%s down=%d x=%u y=%u",
          state, (unsigned long)in->emu.reads, (unsigned long)in->emu.acks,
          age, pressing(c) ? 1 : 0, (unsigned)c->px, (unsigned)c->py);
}

/** @brief `contacts`: every id in use, its gesture, down and point. */
static void cmd_contacts(injector_t *in, int ntok)
{
    if (ntok != 1) {
        reply(in, "err syntax contacts");
        return;
    }
    /* "contacts n=5" and 5 entries of at most 22 characters: 122 of 127. */
    char line[INJ_REPLY_LEN];
    int at = snprintf(line, sizeof(line), "contacts n=%u", in_use(in));
    for (unsigned id = 0; id < N_IDS; ++id) {
        const inj_contact_t *c = &in->c[id];
        if (c->kind == INJ_G_NONE || at < 0 || (size_t)at >= sizeof(line)) {
            continue;
        }
        at += snprintf(line + at, sizeof(line) - (size_t)at, " %u=%s,%d,%u,%u",
                       id, inj_gesture_name(c->kind), pressing(c) ? 1 : 0,
                       (unsigned)c->px, (unsigned)c->py);
    }
    reply(in, "%s", line);
}

/** @brief `wiring`: ask the glue to listen, in LISTEN only. */
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

/** @brief `confirm scl=GP5`: go ACTIVE after an `ok` verdict. */
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

/** @brief `release [id=N]`: one contact, or every one without id=. */
static void cmd_release(injector_t *in, char **tok, int ntok)
{
    const int id = take_id(in, "release", tok, &ntok);
    if (id == ID_BAD) {
        return;
    }
    if (ntok != 1) {
        reply(in, "err syntax release");
        return;
    }
    const unsigned lo = (id == ID_NONE) ? 0u : (unsigned)id;
    const unsigned hi = (id == ID_NONE) ? N_IDS : (unsigned)id + 1u;
    for (unsigned k = lo; k < hi; ++k) {
        inj_contact_t *c = &in->c[k];
        if (live(c)) {
            reply(in, "fail %s%s released", inj_gesture_name(c->kind), c->tag);
            in->failed = true;
            release_contact(c);
        }
    }
    if (id == ID_NONE) {
        reply(in, "ok release");
    } else {
        reply(in, "ok release id=%d", id);
    }
}

/** @brief Split one line into tokens and run its command. */
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
        reply(in, "pong rcbench-touch %d addr=0x%02x res=%ux%u state=%s "
              "id=%s",
              INJ_PROTOCOL, (unsigned)GT911_EMU_ADDR, (unsigned)INJ_RES_X,
              (unsigned)INJ_RES_Y,
              in->state == INJ_LISTEN ? "listen" : "active",
              GT911_EMU_PRODUCT_ID);
    } else if (strcmp(cmd, "status") == 0) {
        cmd_status(in, now_ms);
    } else if (strcmp(cmd, "contacts") == 0) {
        cmd_contacts(in, ntok);
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
        cmd_release(in, tok, ntok);
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
        reply(in, "wiring %s scl=%s sda=%s tx=%lu named=%lu agree=%lu"
              " rate=%lu.%lu",
              ok ? "ok" : "swapped", ok ? "GP5" : "GP4", ok ? "GP4" : "GP5",
              (unsigned long)r->tx, (unsigned long)r->named,
              (unsigned long)r->agree_pct, rate10 / 10u, rate10 % 10u);
        break;
    }
    case INJ_WIRING_IDLE:
    case INJ_WIRING_LOW:
        reply(in, "wiring %s gp4=%u gp5=%u", inj_wiring_name(r->verdict),
              g4, g5);
        break;
    default:
        reply(in, "wiring noise edges4=%lu edges5=%lu tx=%lu named=%lu"
              " agree=%lu",
              (unsigned long)r->edges4, (unsigned long)r->edges5,
              (unsigned long)r->tx, (unsigned long)r->named,
              (unsigned long)r->agree_pct);
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
    for (unsigned id = 0; id < N_IDS; ++id) {
        release_contact(&in->c[id]);
    }
    in->r_count = 0;
    in->len = 0;
    in->overflow = false;
}

/* ------------------------------------------------------------ frames */

/** @brief The point @p num / @p den of the way from @p a to @p b. */
static uint16_t lerp(uint16_t a, uint16_t b, uint32_t num, uint32_t den)
{
    const long d = (long)b - (long)a;
    return (uint16_t)((long)a + d * (long)num / (long)den);
}

/** @brief The magnitude of @p v. */
static long labs_(long v) { return v < 0 ? -v : v; }

/**
 * @brief Where a swipe's next frame puts its contact at @p now_ms.
 * @param c The swipe.
 * @param now_ms The clock.
 * @param x The column.
 * @param y The row.
 * @return Whether the 100px step limit shortened this step.
 */
static bool swipe_point(const inj_contact_t *c, uint32_t now_ms, uint16_t *x,
                        uint16_t *y)
{
    uint16_t tx = c->x0, ty = c->y0;
    if (c->first_acked) {
        const uint32_t el = now_ms - c->t_first;
        if (el >= c->ms) {
            tx = c->x1;
            ty = c->y1;
        } else {
            tx = lerp(c->x0, c->x1, el, c->ms);
            ty = lerp(c->y0, c->y1, el, c->ms);
        }
    }
    const long dx = (long)tx - (long)c->px;
    const long dy = (long)ty - (long)c->py;
    const long dist = labs_(dx) + labs_(dy);
    bool slowed = false;
    if (c->down_published && dist > (long)INJ_SWIPE_STEP_PX) {
        /* Under the panel's 120px jump split (touch_map.h). */
        tx = (uint16_t)((long)c->px + dx * (long)INJ_SWIPE_STEP_PX / dist);
        ty = (uint16_t)((long)c->py + dy * (long)INJ_SWIPE_STEP_PX / dist);
        slowed = true;
    }
    *x = tx;
    *y = ty;
    return slowed;
}

/** @brief The lift of contact @p c was read: done, unless it was released. */
static void finish(injector_t *in, inj_contact_t *c, uint32_t now_ms)
{
    if (c->kind != INJ_G_RELEASE) {
        const uint32_t took = c->first_acked ? now_ms - c->t_first : 0u;
        if (c->kind == INJ_G_SWIPE) {
            reply(in, "done swipe%s frames=%lu ms=%lu slowed=%d", c->tag,
                  (unsigned long)c->frames, (unsigned long)took,
                  c->slowed ? 1 : 0);
        } else {
            reply(in, "done %s%s frames=%lu ms=%lu",
                  inj_gesture_name(c->kind), c->tag,
                  (unsigned long)c->frames, (unsigned long)took);
        }
    }
    contact_end(c);
}

/** @brief The panel acknowledged the frame out at @p now_ms. */
static void frame_read(injector_t *in, uint32_t now_ms)
{
    for (unsigned id = 0; id < N_IDS; ++id) {
        inj_contact_t *c = &in->c[id];
        if (c->kind == INJ_G_NONE || !c->in_frame) {
            continue;
        }
        c->in_frame = false;
        ++c->frames;
        if (c->lifting) {
            finish(in, c, now_ms);
        } else if (c->down_published) {
            if (!c->first_acked) {
                c->first_acked = true;
                c->t_first = now_ms;
            }
            if (c->end_published) {
                c->end_acked = true;
            }
        }
    }
}

/** @brief What the next frame does with one contact. */
typedef struct {
    bool     wants;   /**< the frame is published for it   */
    bool     lift;    /**< it is left out: lifted          */
    bool     slowed;  /**< swipe: the step was shortened   */
    uint16_t x;       /**< its column when not lifted      */
    uint16_t y;       /**< its row when not lifted         */
} plan_t;

/** @brief Plan contact @p c's part in a frame published at @p now_ms. */
static plan_t plan(const inj_contact_t *c, uint32_t now_ms)
{
    plan_t p = { .x = c->px, .y = c->py };
    switch (c->kind) {
    case INJ_G_RELEASE:
        p.wants = true;
        p.lift = true;
        break;
    case INJ_G_SWIPE:
        p.wants = true;
        if (c->end_acked) {
            p.lift = true;
        } else {
            p.slowed = swipe_point(c, now_ms, &p.x, &p.y);
        }
        break;
    default:
        /* One frame puts the contact down and the panel keeps it, as it
         * keeps any contact while the controller has nothing new; the
         * lift goes out at the deadline, not a frame after it.  Frames
         * other contacts need carry it where it is. */
        if (!c->down_published) {
            p.wants = true;
            p.x = c->x0;
            p.y = c->y0;
        } else if (c->first_acked && now_ms - c->t_first >= c->ms) {
            p.wants = true;
            p.lift = true;
        }
        break;
    }
    return p;
}

/** @brief Publish the next frame when a contact needs one. */
static void publish_next(injector_t *in, uint32_t now_ms)
{
    plan_t p[N_IDS];
    gt911_emu_frame_t f = { .n = 0 };
    bool any = false;
    for (unsigned id = 0; id < N_IDS; ++id) {
        const inj_contact_t *c = &in->c[id];
        if (c->kind == INJ_G_NONE) {
            p[id] = (plan_t){ .lift = true };
            continue;
        }
        p[id] = plan(c, now_ms);
        any = any || p[id].wants;
        if (!p[id].lift && f.n < GT911_EMU_POINTS) {
            f.pt[f.n++] = (gt911_emu_point_t){ .id = (uint8_t)id,
                                               .x = p[id].x, .y = p[id].y };
        }
    }
    if (!any || !gt911_emu_publish(&in->emu, &f)) {
        return;   /* nothing to say, or a transfer is under way */
    }
    in->pending = true;
    in->published = true;
    in->t_pub = now_ms;
    for (unsigned id = 0; id < N_IDS; ++id) {
        inj_contact_t *c = &in->c[id];
        if (c->kind == INJ_G_NONE || !p[id].wants) {
            continue;
        }
        c->in_frame = true;
        if (p[id].lift) {
            c->lifting = true;
            continue;
        }
        c->down_published = true;
        c->px = p[id].x;
        c->py = p[id].y;
        c->slowed = c->slowed || p[id].slowed;
        if (c->kind == INJ_G_SWIPE && c->px == c->x1 && c->py == c->y1) {
            c->end_published = true;
        }
    }
}

/** @brief A gesture under way has a contact the panel was given. */
static bool any_down(const injector_t *in)
{
    for (unsigned id = 0; id < N_IDS; ++id) {
        if (live(&in->c[id]) && in->c[id].down_published) {
            return true;
        }
    }
    return false;
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
        fail_all(in, "panel_reset");
        in->withdraw_due = true;
    }
    if (e->acks != in->seen_acks) {
        in->seen_acks = e->acks;
        if (in->pending) {
            in->pending = false;
            frame_read(in, now_ms);
        }
    }
    if (in->withdraw_due && !in->pending && gt911_emu_withdraw(e)) {
        in->withdraw_due = false;
    }
    if (in_use(in) == 0u) {
        /* Idle: the next gesture's first frame goes out at once. */
        in->published = false;
        return;
    }
    if (in->pending) {
        if (now_ms - in->t_pub >= INJ_ACK_TIMEOUT_MS) {
            fail_all(in, "no_master");
            in->withdraw_due = !gt911_emu_withdraw(e);
        }
        return;
    }
    if (any_down(in) && now_ms - in->t_last_read >= INJ_ACK_TIMEOUT_MS) {
        /* A contact is down and the panel has stopped reading. */
        fail_all(in, "no_master");
        in->withdraw_due = !gt911_emu_withdraw(e);
        return;
    }
    if (in->published && now_ms - in->t_pub < INJ_REPORT_MS) {
        return;
    }
    publish_next(in, now_ms);
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
    for (unsigned id = 0; id < N_IDS; ++id) {
        if (pressing(&in->c[id])) {
            return INJ_LED_DOWN;
        }
    }
    if (in->any_read && now_ms - in->t_last_read < INJ_LIVE_MS) {
        return INJ_LED_POLLED;
    }
    return INJ_LED_OFF;
}
