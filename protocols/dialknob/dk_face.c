/**
 * @file
 * @brief The Dial's side: the model of the panel's screen, built from the
 *        panel's packets, so a face is drawn from one struct.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "dialknob.h"

/** @brief Every part's have_ flag cleared, the counts kept.
 *  @param f the model */
static void empty(dk_face_t *f)
{
    f->have_screen = false;
    f->have_nav = 0u;
    f->have_meta = false;
    f->have_value = false;
    f->have_motor = false;
    f->have_servo = false;
    f->have_state = false;
}

void dk_face_init(dk_face_t *f)
{
    if (f != NULL) {
        memset(f, 0, sizeof(*f));
    }
}

void dk_face_link_reset(dk_face_t *f)
{
    if (f != NULL) {
        empty(f);
        f->any = false;
        f->changed = 0xFFu;
    }
}

/**
 * @brief Take a SCREEN: another face or title empties the parts of the
 *        screen before.
 * @param f the model
 * @param s the screen
 */
static void take_screen(dk_face_t *f, const dk_screen_t *s)
{
    if (!f->have_screen || f->screen.face != s->face
        || strcmp(f->screen.title, s->title) != 0) {
        f->have_nav = 0u;
        f->have_meta = false;
        f->have_value = false;
        f->changed |= DK_CH_NAV | DK_CH_META | DK_CH_VALUE;
    }
    f->screen = *s;
    f->have_screen = true;
    f->changed |= DK_CH_SCREEN;
}

/**
 * @brief Take a NAV row: a row of another list position empties the rows
 *        of the position before.
 * @param f the model
 * @param n the row
 */
static void take_nav(dk_face_t *f, const dk_nav_t *n)
{
    for (unsigned s = 0; s < 3u; ++s) {
        if ((f->have_nav & (1u << s)) != 0u
            && (f->nav[s].index != n->index || f->nav[s].count != n->count)) {
            f->have_nav = 0u;
            break;
        }
    }
    f->nav[n->slot] = *n;
    f->have_nav = (uint8_t)(f->have_nav | (1u << n->slot));
    f->changed |= DK_CH_NAV;
}

bool dk_face_feed(dk_face_t *f, const uint8_t *buf, size_t len,
                  uint32_t now_ms)
{
    dk_panel_pkt_t p;
    uint8_t seq;
    if (f == NULL) {
        return false;
    }
    if (!dk_panel_decode(buf, len, &p)) {
        ++f->bad;
        return false;
    }
    seq = buf[1];
    if (f->any && seq == f->seq) {
        ++f->repeats;
        return false;
    }
    if (f->any) {
        f->skipped += (uint8_t)(seq - f->seq - 1u);
    }
    f->any = true;
    f->seq = seq;
    f->last_ms = now_ms;
    ++f->packets;
    switch (p.kind) {
    case DK_K_SCREEN:
        take_screen(f, &p.u.screen);
        break;
    case DK_K_NAV:
        take_nav(f, &p.u.nav);
        break;
    case DK_K_VALUE_META:
        f->meta = p.u.meta;
        f->have_meta = true;
        f->changed |= DK_CH_META;
        break;
    case DK_K_VALUE:
        f->value = p.u.value;
        f->have_value = true;
        f->changed |= DK_CH_VALUE;
        break;
    case DK_K_LIVE_MOTOR:
        f->motor = p.u.motor;
        f->have_motor = true;
        f->changed |= DK_CH_MOTOR;
        break;
    case DK_K_LIVE_SERVO:
        f->servo = p.u.servo;
        f->have_servo = true;
        f->changed |= DK_CH_SERVO;
        break;
    default: /* DK_K_STATE: dk_panel_decode() takes no other kind */
        f->state = p.u.state;
        f->have_state = true;
        f->changed |= DK_CH_STATE;
        break;
    }
    return true;
}

bool dk_face_lost(const dk_face_t *f, uint32_t now_ms)
{
    return f == NULL || !f->any || now_ms - f->last_ms >= DK_DIAL_LOST_MS;
}

bool dk_face_drop(const dk_face_t *f, uint32_t now_ms)
{
    return f != NULL && f->any && now_ms - f->last_ms >= DK_DIAL_DROP_MS;
}

/**
 * @brief a / 4 rounded toward minus infinity.
 * @param a the dividend
 * @return the quotient
 */
static int32_t floor4(int32_t a)
{
    return a >= 0 ? a / 4 : -((-(a + 1)) / 4) - 1;
}

int32_t dk_detent_follow(int32_t detent, int32_t count)
{
    /* Up: the largest d with count >= 4(d - 1) + HYST, the count past the
     * point that moved to d.  Down likewise, mirrored. */
    const int32_t center = detent * (int32_t)DK_COUNTS_DETENT;
    if (count - center >= DK_DETENT_HYST) {
        return floor4(count - DK_DETENT_HYST) + 1;
    }
    if (center - count >= DK_DETENT_HYST) {
        return -(floor4(-count - DK_DETENT_HYST) + 1);
    }
    return detent;
}
