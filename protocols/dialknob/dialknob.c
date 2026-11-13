/**
 * @file
 * @brief The packet codec of the Dial's knob protocol: every packet in both
 *        directions, its bounds, and the advertising data check.
 *
 * Each encoder writes the bytes and then runs its own decoder on them, so
 * an encoder never writes a packet the decoder refuses and the two hold one
 * set of bounds.  A decoder refuses a wrong length, a wrong kind, a field
 * out of its range, a reserved bit or byte that is not 0, a text field that
 * is not UTF-8 or holds a control character, and a text field with a byte
 * other than NUL after its first NUL.  A decoded packet encodes to the same
 * bytes.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "dialknob.h"

_Static_assert(DK_LEN_EVENT <= DK_PKT_MAX && DK_LEN_STATUS <= DK_PKT_MAX
               && DK_LEN_INFO <= DK_PKT_MAX && DK_LEN_SCREEN <= DK_PKT_MAX
               && DK_LEN_NAV <= DK_PKT_MAX
               && DK_LEN_VALUE_META <= DK_PKT_MAX
               && DK_LEN_VALUE <= DK_PKT_MAX
               && DK_LEN_LIVE_MOTOR <= DK_PKT_MAX
               && DK_LEN_LIVE_SERVO <= DK_PKT_MAX
               && DK_LEN_STATE <= DK_PKT_MAX,
               "every packet fits one ATT payload at MTU 23");
_Static_assert(4u + DK_TITLE_MAX + 1u == DK_LEN_SCREEN,
               "SCREEN: kind, seq, face, lang, flags, title");
_Static_assert(DK_ITVL_MIN >= 6u && DK_ITVL_MAX <= 3200u
               && DK_ITVL_MIN <= DK_ITVL_MAX,
               "the interval is 7.5ms to 4s");
_Static_assert(DK_SUPERVISION * 10u * 4u
               > (1u + DK_LATENCY_IDLE) * DK_ITVL_MAX * 5u * 2u,
               "the supervision timeout exceeds twice the idle latency's span");
_Static_assert(DK_DEGRADED_MS < DK_LOST_MS && DK_KEEPALIVE_MS < DK_STALE_MS
               && DK_STREAM_MS < DK_DIAL_LOST_MS,
               "a keepalive or a stream packet comes before any rule fires");

/** @brief Write a 16-bit value, least significant byte first.
 *  @param p the two bytes @param v the value */
static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

/** @brief Read a 16-bit value, least significant byte first.
 *  @param p the two bytes @return the value */
static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8));
}

/** @brief Write a 32-bit value, least significant byte first.
 *  @param p the four bytes @param v the value */
static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)(v & 0xFFFFu));
    put16(p + 2, (uint16_t)(v >> 16));
}

/** @brief Read a 32-bit value, least significant byte first.
 *  @param p the four bytes @return the value */
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)get16(p) | ((uint32_t)get16(p + 2) << 16);
}

/** @brief A signed 16-bit value from its two's complement bits.
 *  @param v the bits @return the value */
static int16_t s16(uint16_t v)
{
    return v < 0x8000u ? (int16_t)v : (int16_t)((int32_t)v - 0x10000);
}

/** @brief A signed 32-bit value from its two's complement bits.
 *  @param v the bits @return the value */
static int32_t s32(uint32_t v)
{
    return v < 0x80000000u ? (int32_t)v
                           : (int32_t)(v - 0x80000000u) - 0x7FFFFFFF - 1;
}

/** @brief A signed 8-bit value from its two's complement bits.
 *  @param v the bits @return the value */
static int8_t s8(uint8_t v)
{
    return v < 0x80u ? (int8_t)v : (int8_t)((int)v - 0x100);
}

/**
 * @brief Whether @p n bytes are UTF-8 without a control character: no
 *        overlong form, no surrogate, nothing above U+10FFFF, nothing below
 *        U+0020, no U+007F and nothing from U+0080 to U+009F.
 * @param s the bytes
 * @param n their count
 * @return true when they are
 */
static bool utf8_ok(const uint8_t *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        const uint8_t b = s[i];
        uint32_t cp;
        size_t more;
        if (b < 0x80u) {
            if (b < 0x20u || b == 0x7Fu) {
                return false;
            }
            ++i;
            continue;
        }
        if (b >= 0xC2u && b <= 0xDFu) {
            cp = b & 0x1Fu;
            more = 1;
        } else if (b >= 0xE0u && b <= 0xEFu) {
            cp = b & 0x0Fu;
            more = 2;
        } else if (b >= 0xF0u && b <= 0xF4u) {
            cp = b & 0x07u;
            more = 3;
        } else {
            return false;
        }
        if (n - i - 1u < more) {
            return false;
        }
        for (size_t k = 1; k <= more; ++k) {
            const uint8_t c = s[i + k];
            if ((c & 0xC0u) != 0x80u) {
                return false;
            }
            cp = (cp << 6) | (c & 0x3Fu);
        }
        if ((more == 2u && cp < 0x800u) || (more == 3u && cp < 0x10000u)
            || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)
            || cp < 0xA0u) {
            return false;
        }
        i += more + 1u;
    }
    return true;
}

/**
 * @brief Read a NUL-padded text field.
 * @param p   the field
 * @param n   its bytes
 * @param out n + 1 bytes: the text, NUL-terminated
 * @return false when the text is not UTF-8, holds a control character, or a
 *         byte after its first NUL is not NUL
 */
static bool get_text(const uint8_t *p, size_t n, char *out)
{
    size_t len = 0;
    while (len < n && p[len] != 0u) {
        ++len;
    }
    for (size_t i = len; i < n; ++i) {
        if (p[i] != 0u) {
            return false;
        }
    }
    if (!utf8_ok(p, len)) {
        return false;
    }
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

/**
 * @brief Write a text into a NUL-padded field, cut at the last whole UTF-8
 *        character that fits.
 * @param p the field
 * @param n its bytes
 * @param s the text; NULL is ""
 */
static void put_text(uint8_t *p, size_t n, const char *s)
{
    size_t len = 0;
    memset(p, 0, n);
    if (s == NULL) {
        return;
    }
    while (len <= n && s[len] != '\0') {
        ++len;
    }
    if (len > n) {
        len = n;
        while (len > 0u && ((uint8_t)s[len] & 0xC0u) == 0x80u) {
            --len;
        }
    }
    memcpy(p, s, len);
}

/**
 * @brief The common end of every encoder: the bytes are kept when their
 *        own decoder takes them, else cleared.
 * @param out the packet
 * @param len its length
 * @param ok  whether the decoder took it
 * @return len, or 0
 */
static size_t finish(uint8_t *out, size_t len, bool ok)
{
    if (!ok) {
        memset(out, 0, len);
        return 0;
    }
    return len;
}

/** @brief Whether a packet has the length and kind asked.
 *  @param buf the bytes @param len their count @param want the length
 *  @param kind the kind @return true when both match */
static bool head_ok(const uint8_t *buf, size_t len, size_t want, uint8_t kind)
{
    return buf != NULL && len == want && buf[0] == kind;
}

/** @brief Whether a pulse is 0 (none) or within DK_US_MIN to DK_US_MAX.
 *  @param us the pulse @return true when it is */
static bool us_ok(uint16_t us)
{
    return us == 0u || (us >= DK_US_MIN && us <= DK_US_MAX);
}

/** @brief Whether a value lies within lo to hi.
 *  @param v the value @param lo the lowest @param hi the highest
 *  @return true when it does */
static bool in(int32_t v, int32_t lo, int32_t hi)
{
    return v >= lo && v <= hi;
}

/* --- EVENT ---------------------------------------------------------------- */

bool dk_evt_decode(const uint8_t *buf, size_t len, dk_evt_t *out)
{
    dk_evt_t e;
    bool touched;
    if (!head_ok(buf, len, DK_LEN_EVENT, DK_K_EVENT) || out == NULL) {
        return false;
    }
    e.seq = buf[1];
    e.counter = s16(get16(buf + 2));
    e.buttons = buf[4];
    e.gesture = buf[5];
    e.touch_x = buf[6];
    e.touch_y = buf[7];
    e.ms = get16(buf + 8);
    e.echo = get16(buf + 10);
    touched = (e.buttons & DK_BTN_TOUCH) != 0u || e.gesture == DK_G_TAP;
    if ((e.buttons & (uint8_t)~(DK_BTN_SCREEN | DK_BTN_TOUCH | DK_BTN_STAMP))
            != 0u
        || e.gesture > DK_G_TAP || e.touch_x > DK_TOUCH_MAX
        || e.touch_y > DK_TOUCH_MAX
        || (!touched && (e.touch_x != 0u || e.touch_y != 0u))) {
        return false;
    }
    *out = e;
    return true;
}

size_t dk_evt_encode(const dk_evt_t *e, uint8_t *out)
{
    dk_evt_t chk;
    if (e == NULL || out == NULL) {
        return 0;
    }
    out[0] = DK_K_EVENT;
    out[1] = e->seq;
    put16(out + 2, (uint16_t)e->counter);
    out[4] = e->buttons;
    out[5] = e->gesture;
    out[6] = e->touch_x;
    out[7] = e->touch_y;
    put16(out + 8, e->ms);
    put16(out + 10, e->echo);
    return finish(out, DK_LEN_EVENT,
                  dk_evt_decode(out, DK_LEN_EVENT, &chk));
}

/* --- STATUS --------------------------------------------------------------- */

bool dk_status_decode(const uint8_t *buf, size_t len, dk_status_t *out)
{
    dk_status_t s;
    const uint8_t known = DK_ST_USB | DK_ST_CHARGING | DK_ST_BATT_UNKNOWN
                          | DK_ST_PANEL_LOST;
    if (!head_ok(buf, len, DK_LEN_STATUS, DK_K_STATUS) || out == NULL) {
        return false;
    }
    s.seq = buf[1];
    s.batt_mv = get16(buf + 2);
    s.flags = buf[4];
    s.fw[0] = buf[5];
    s.fw[1] = buf[6];
    s.fw[2] = buf[7];
    s.uptime_s = get16(buf + 8);
    s.rssi = s8(buf[10]);
    s.bad = buf[11];
    if ((s.flags & (uint8_t)~known) != 0u) {
        return false;
    }
    if ((s.flags & DK_ST_BATT_UNKNOWN) != 0u
            ? s.batt_mv != 0u
            : (s.batt_mv < DK_BATT_MIN_MV || s.batt_mv > DK_BATT_MAX_MV)) {
        return false;
    }
    if (s.rssi != DK_RSSI_NONE && !in(s.rssi, DK_RSSI_MIN, DK_RSSI_MAX)) {
        return false;
    }
    *out = s;
    return true;
}

size_t dk_status_encode(const dk_status_t *s, uint8_t *out)
{
    dk_status_t chk;
    if (s == NULL || out == NULL) {
        return 0;
    }
    out[0] = DK_K_STATUS;
    out[1] = s->seq;
    put16(out + 2, s->batt_mv);
    out[4] = s->flags;
    out[5] = s->fw[0];
    out[6] = s->fw[1];
    out[7] = s->fw[2];
    put16(out + 8, s->uptime_s);
    out[10] = (uint8_t)s->rssi;
    out[11] = s->bad;
    return finish(out, DK_LEN_STATUS,
                  dk_status_decode(out, DK_LEN_STATUS, &chk));
}

/* --- INFO ----------------------------------------------------------------- */

bool dk_info_decode(const uint8_t *buf, size_t len, dk_info_t *out)
{
    dk_info_t i;
    if (!head_ok(buf, len, DK_LEN_INFO, DK_K_INFO) || out == NULL) {
        return false;
    }
    i.proto = buf[1];
    i.fw[0] = buf[2];
    i.fw[1] = buf[3];
    i.fw[2] = buf[4];
    i.flags = buf[5];
    i.hw = buf[6];
    if (i.proto == 0u
        || (i.flags & (uint8_t)~(DK_INFO_BOUND | DK_INFO_HOOKS)) != 0u
        || i.hw != DK_HW_DIAL11 || buf[7] != 0u
        || !get_text(buf + 8, DK_NAME_MAX, i.name)) {
        return false;
    }
    for (size_t k = 0; i.name[k] != '\0'; ++k) {
        if ((uint8_t)i.name[k] > 0x7Eu) {
            return false;   /* ASCII only */
        }
    }
    *out = i;
    return true;
}

size_t dk_info_encode(const dk_info_t *i, uint8_t *out)
{
    dk_info_t chk;
    if (i == NULL || out == NULL) {
        return 0;
    }
    out[0] = DK_K_INFO;
    out[1] = i->proto;
    out[2] = i->fw[0];
    out[3] = i->fw[1];
    out[4] = i->fw[2];
    out[5] = i->flags;
    out[6] = i->hw;
    out[7] = 0u;
    put_text(out + 8, DK_NAME_MAX, i->name);
    return finish(out, DK_LEN_INFO, dk_info_decode(out, DK_LEN_INFO, &chk));
}

/* --- SCREEN --------------------------------------------------------------- */

bool dk_screen_decode(const uint8_t *buf, size_t len, dk_screen_t *out)
{
    dk_screen_t s;
    if (!head_ok(buf, len, DK_LEN_SCREEN, DK_K_SCREEN) || out == NULL) {
        return false;
    }
    s.seq = buf[1];
    s.face = buf[2];
    s.lang = buf[3];
    s.flags = buf[4];
    if (s.face < DK_FACE_NAV || s.face > DK_FACE_HOME || s.lang > DK_LANG_DE
        || (s.flags & (uint8_t)~(DK_SCR_NAVIGATES | DK_SCR_SOUND)) != 0u
        || !get_text(buf + 5, DK_TITLE_MAX, s.title)) {
        return false;
    }
    *out = s;
    return true;
}

size_t dk_screen_encode(const dk_screen_t *s, uint8_t *out)
{
    dk_screen_t chk;
    if (s == NULL || out == NULL) {
        return 0;
    }
    out[0] = DK_K_SCREEN;
    out[1] = s->seq;
    out[2] = s->face;
    out[3] = s->lang;
    out[4] = s->flags;
    put_text(out + 5, DK_TITLE_MAX, s->title);
    return finish(out, DK_LEN_SCREEN,
                  dk_screen_decode(out, DK_LEN_SCREEN, &chk));
}

/* --- NAV ------------------------------------------------------------------ */

bool dk_nav_decode(const uint8_t *buf, size_t len, dk_nav_t *out)
{
    dk_nav_t n;
    if (!head_ok(buf, len, DK_LEN_NAV, DK_K_NAV) || out == NULL) {
        return false;
    }
    n.seq = buf[1];
    n.index = buf[2];
    n.count = buf[3];
    n.slot = buf[4];
    if (n.slot > DK_SLOT_NEXT
        || (n.count == 0u ? n.index != 0u : n.index >= n.count)
        || !get_text(buf + 5, DK_ITEM_MAX, n.item)) {
        return false;
    }
    *out = n;
    return true;
}

size_t dk_nav_encode(const dk_nav_t *n, uint8_t *out)
{
    dk_nav_t chk;
    if (n == NULL || out == NULL) {
        return 0;
    }
    out[0] = DK_K_NAV;
    out[1] = n->seq;
    out[2] = n->index;
    out[3] = n->count;
    out[4] = n->slot;
    put_text(out + 5, DK_ITEM_MAX, n->item);
    return finish(out, DK_LEN_NAV, dk_nav_decode(out, DK_LEN_NAV, &chk));
}

/* --- VALUE_META ----------------------------------------------------------- */

bool dk_value_meta_decode(const uint8_t *buf, size_t len,
                          dk_value_meta_t *out)
{
    dk_value_meta_t m;
    if (!head_ok(buf, len, DK_LEN_VALUE_META, DK_K_VALUE_META)
        || out == NULL) {
        return false;
    }
    m.seq = buf[1];
    m.decimals = buf[6];
    m.min = s32(get32(buf + 8));
    m.max = s32(get32(buf + 12));
    m.step = s32(get32(buf + 16));
    if (!get_text(buf + 2, DK_UNIT_MAX, m.unit)
        || m.decimals > DK_DECIMALS_MAX || buf[7] != 0u || m.min > m.max
        || m.step < 1) {
        return false;
    }
    *out = m;
    return true;
}

size_t dk_value_meta_encode(const dk_value_meta_t *m, uint8_t *out)
{
    dk_value_meta_t chk;
    if (m == NULL || out == NULL) {
        return 0;
    }
    out[0] = DK_K_VALUE_META;
    out[1] = m->seq;
    put_text(out + 2, DK_UNIT_MAX, m->unit);
    out[6] = m->decimals;
    out[7] = 0u;
    put32(out + 8, (uint32_t)m->min);
    put32(out + 12, (uint32_t)m->max);
    put32(out + 16, (uint32_t)m->step);
    return finish(out, DK_LEN_VALUE_META,
                  dk_value_meta_decode(out, DK_LEN_VALUE_META, &chk));
}

/* --- VALUE ---------------------------------------------------------------- */

bool dk_value_decode(const uint8_t *buf, size_t len, dk_value_t *out)
{
    dk_value_t v;
    if (!head_ok(buf, len, DK_LEN_VALUE, DK_K_VALUE) || out == NULL) {
        return false;
    }
    v.seq = buf[1];
    v.value = s32(get32(buf + 2));
    v.was = s32(get32(buf + 6));
    v.flags = buf[10];
    if ((v.flags & (uint8_t)~(DK_VAL_SET | DK_VAL_LIMIT)) != 0u) {
        return false;
    }
    *out = v;
    return true;
}

size_t dk_value_encode(const dk_value_t *v, uint8_t *out)
{
    dk_value_t chk;
    if (v == NULL || out == NULL) {
        return 0;
    }
    out[0] = DK_K_VALUE;
    out[1] = v->seq;
    put32(out + 2, (uint32_t)v->value);
    put32(out + 6, (uint32_t)v->was);
    out[10] = v->flags;
    return finish(out, DK_LEN_VALUE,
                  dk_value_decode(out, DK_LEN_VALUE, &chk));
}

/* --- LIVE_MOTOR ----------------------------------------------------------- */

bool dk_live_motor_decode(const uint8_t *buf, size_t len,
                          dk_live_motor_t *out)
{
    dk_live_motor_t m;
    const uint8_t flags = DK_LIVE_ARMED | DK_LIVE_DRIVING | DK_LIVE_FAULT;
    const uint8_t valid = DK_MV_RPM | DK_MV_VOLT | DK_MV_CURR | DK_MV_POWER
                          | DK_MV_ESC_T | DK_MV_MOT_T;
    if (!head_ok(buf, len, DK_LEN_LIVE_MOTOR, DK_K_LIVE_MOTOR)
        || out == NULL) {
        return false;
    }
    m.seq = buf[1];
    m.flags = buf[2];
    m.valid = buf[3];
    m.thr_cpct = get16(buf + 4);
    m.rpm = get16(buf + 6);
    m.mv = get16(buf + 8);
    m.ca = s16(get16(buf + 10));
    m.dw = get16(buf + 12);
    m.esc_c = s8(buf[14]);
    m.mot_c = s8(buf[15]);
    m.run_s = get16(buf + 16);
    if ((m.flags & (uint8_t)~flags) != 0u || (m.valid & (uint8_t)~valid) != 0u
        || m.thr_cpct > DK_THROTTLE_MAX
        || !in(m.esc_c, DK_TEMP_MIN, DK_TEMP_MAX)
        || !in(m.mot_c, DK_TEMP_MIN, DK_TEMP_MAX)) {
        return false;
    }
    /* A field without its valid bit is 0, so a packet has one form. */
    if (((m.valid & DK_MV_RPM) == 0u && m.rpm != 0u)
        || ((m.valid & DK_MV_VOLT) == 0u && m.mv != 0u)
        || ((m.valid & DK_MV_CURR) == 0u && m.ca != 0)
        || ((m.valid & DK_MV_POWER) == 0u && m.dw != 0u)
        || ((m.valid & DK_MV_ESC_T) == 0u && m.esc_c != 0)
        || ((m.valid & DK_MV_MOT_T) == 0u && m.mot_c != 0)) {
        return false;
    }
    *out = m;
    return true;
}

size_t dk_live_motor_encode(const dk_live_motor_t *m, uint8_t *out)
{
    dk_live_motor_t chk;
    if (m == NULL || out == NULL) {
        return 0;
    }
    out[0] = DK_K_LIVE_MOTOR;
    out[1] = m->seq;
    out[2] = m->flags;
    out[3] = m->valid;
    put16(out + 4, m->thr_cpct);
    put16(out + 6, m->rpm);
    put16(out + 8, m->mv);
    put16(out + 10, (uint16_t)m->ca);
    put16(out + 12, m->dw);
    out[14] = (uint8_t)m->esc_c;
    out[15] = (uint8_t)m->mot_c;
    put16(out + 16, m->run_s);
    return finish(out, DK_LEN_LIVE_MOTOR,
                  dk_live_motor_decode(out, DK_LEN_LIVE_MOTOR, &chk));
}

/* --- LIVE_SERVO ----------------------------------------------------------- */

bool dk_live_servo_decode(const uint8_t *buf, size_t len,
                          dk_live_servo_t *out)
{
    dk_live_servo_t s;
    const uint8_t flags = DK_LIVE_ARMED | DK_LIVE_DRIVING | DK_LIVE_MEAS
                          | DK_LIVE_FB | DK_LIVE_RAIL;
    if (!head_ok(buf, len, DK_LEN_LIVE_SERVO, DK_K_LIVE_SERVO)
        || out == NULL) {
        return false;
    }
    s.seq = buf[1];
    s.flags = buf[2];
    s.cmd_us = get16(buf + 3);
    s.cmd_ddeg = s16(get16(buf + 5));
    s.meas_cdeg = s16(get16(buf + 7));
    s.rail_mv = get16(buf + 9);
    s.rail_ca = s16(get16(buf + 11));
    s.rail_dw = get16(buf + 13);
    s.fb_us = get16(buf + 15);
    s.fb_ca = s16(get16(buf + 17));
    if ((s.flags & (uint8_t)~flags) != 0u || !us_ok(s.cmd_us)
        || !in(s.cmd_ddeg, -DK_DDEG_MAX, DK_DDEG_MAX)
        || !in(s.meas_cdeg, -DK_CDEG_MAX, DK_CDEG_MAX)) {
        return false;
    }
    if ((s.flags & DK_LIVE_MEAS) == 0u && s.meas_cdeg != 0) {
        return false;
    }
    if ((s.flags & DK_LIVE_FB) != 0u
            ? (s.fb_us < DK_US_MIN || s.fb_us > DK_US_MAX)
            : (s.fb_us != 0u || s.fb_ca != 0)) {
        return false;
    }
    if ((s.flags & DK_LIVE_RAIL) == 0u
        && (s.rail_mv != 0u || s.rail_ca != 0 || s.rail_dw != 0u)) {
        return false;
    }
    *out = s;
    return true;
}

size_t dk_live_servo_encode(const dk_live_servo_t *s, uint8_t *out)
{
    dk_live_servo_t chk;
    if (s == NULL || out == NULL) {
        return 0;
    }
    out[0] = DK_K_LIVE_SERVO;
    out[1] = s->seq;
    out[2] = s->flags;
    put16(out + 3, s->cmd_us);
    put16(out + 5, (uint16_t)s->cmd_ddeg);
    put16(out + 7, (uint16_t)s->meas_cdeg);
    put16(out + 9, s->rail_mv);
    put16(out + 11, (uint16_t)s->rail_ca);
    put16(out + 13, s->rail_dw);
    put16(out + 15, s->fb_us);
    put16(out + 17, (uint16_t)s->fb_ca);
    return finish(out, DK_LEN_LIVE_SERVO,
                  dk_live_servo_decode(out, DK_LEN_LIVE_SERVO, &chk));
}

/* --- STATE ---------------------------------------------------------------- */

bool dk_state_decode(const uint8_t *buf, size_t len, dk_state_t *out)
{
    dk_state_t s;
    if (!head_ok(buf, len, DK_LEN_STATE, DK_K_STATE) || out == NULL) {
        return false;
    }
    s.seq = buf[1];
    s.panel_ms = get32(buf + 2);
    s.thr_step = buf[6];
    s.srv_step = buf[7];
    s.flags = buf[8];
    if (s.thr_step < DK_STEP_MIN || s.thr_step > DK_STEP_MAX
        || s.srv_step < DK_STEP_MIN || s.srv_step > DK_STEP_MAX
        || (s.flags & (uint8_t)~(DK_STA_ARMED | DK_STA_DEGRADED)) != 0u) {
        return false;
    }
    *out = s;
    return true;
}

size_t dk_state_encode(const dk_state_t *s, uint8_t *out)
{
    dk_state_t chk;
    if (s == NULL || out == NULL) {
        return 0;
    }
    out[0] = DK_K_STATE;
    out[1] = s->seq;
    put32(out + 2, s->panel_ms);
    out[6] = s->thr_step;
    out[7] = s->srv_step;
    out[8] = s->flags;
    return finish(out, DK_LEN_STATE,
                  dk_state_decode(out, DK_LEN_STATE, &chk));
}

/* --- any panel packet ----------------------------------------------------- */

bool dk_panel_decode(const uint8_t *buf, size_t len, dk_panel_pkt_t *out)
{
    dk_panel_pkt_t p;
    bool ok;
    if (buf == NULL || len == 0u || out == NULL) {
        return false;
    }
    p.kind = buf[0];
    switch (p.kind) {
    case DK_K_SCREEN:
        ok = dk_screen_decode(buf, len, &p.u.screen);
        break;
    case DK_K_NAV:
        ok = dk_nav_decode(buf, len, &p.u.nav);
        break;
    case DK_K_VALUE_META:
        ok = dk_value_meta_decode(buf, len, &p.u.meta);
        break;
    case DK_K_VALUE:
        ok = dk_value_decode(buf, len, &p.u.value);
        break;
    case DK_K_LIVE_MOTOR:
        ok = dk_live_motor_decode(buf, len, &p.u.motor);
        break;
    case DK_K_LIVE_SERVO:
        ok = dk_live_servo_decode(buf, len, &p.u.servo);
        break;
    case DK_K_STATE:
        ok = dk_state_decode(buf, len, &p.u.state);
        break;
    default:
        ok = false;
        break;
    }
    if (ok) {
        *out = p;
    }
    return ok;
}

/* --- advertising data ----------------------------------------------------- */

bool dk_adv_has_service(const uint8_t *ad, size_t len)
{
    static const uint8_t svc[16] = { DK_UUID_LE(DK_UUID_SVC) };
    size_t i = 0;
    if (ad == NULL) {
        return false;
    }
    while (i < len) {
        const size_t n = ad[i];
        if (n == 0u || n > len - i - 1u) {
            return false;   /* the end, or a length past it */
        }
        if (ad[i + 1u] == 0x06u || ad[i + 1u] == 0x07u) {
            for (size_t k = 1; k + 16u <= n; k += 16u) {
                if (memcmp(ad + i + 1u + k, svc, 16) == 0) {
                    return true;
                }
            }
        }
        i += n + 1u;
    }
    return false;
}

/* --- text --------------------------------------------------------------- */

size_t dk_text_copy(char *dst, size_t cap, const char *src)
{
    size_t len = 0;
    if (dst == NULL || cap == 0u) {
        return 0;
    }
    if (src != NULL) {
        while (len < cap && src[len] != '\0') {
            ++len;
        }
        if (len == cap) {           /* longer than the field: cut */
            len = cap - 1u;
            while (len > 0u && ((uint8_t)src[len] & 0xC0u) == 0x80u) {
                --len;
            }
        }
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
    return len;
}

/* --- value text ----------------------------------------------------------- */

size_t dk_value_text(int32_t value, uint8_t decimals, const char *unit,
                     char *out, size_t cap)
{
    char digits[16];
    size_t nd = 0;
    size_t pos = 0;
    size_t ulen;
    /* The magnitude as an unsigned number: INT32_MIN has no positive int32. */
    uint32_t mag = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    if (out == NULL || cap == 0u) {
        return 0;
    }
    if (decimals > DK_DECIMALS_MAX) {
        decimals = DK_DECIMALS_MAX;
    }
    if (unit == NULL) {
        unit = "";
    }
    ulen = strlen(unit);
    do {
        digits[nd++] = (char)('0' + (char)(mag % 10u));
        mag /= 10u;
    } while (mag != 0u);
    while (nd <= decimals) {
        digits[nd++] = '0';     /* at least one digit before the point */
    }
    if ((value < 0 ? 1u : 0u) + nd + (decimals > 0u ? 1u : 0u) + ulen + 1u
        > cap) {
        out[0] = '\0';
        return 0;
    }
    if (value < 0) {
        out[pos++] = '-';
    }
    while (nd > 0u) {
        if (nd == decimals) {
            out[pos++] = '.';
        }
        out[pos++] = digits[--nd];
    }
    memcpy(out + pos, unit, ulen);
    pos += ulen;
    out[pos] = '\0';
    return pos;
}
