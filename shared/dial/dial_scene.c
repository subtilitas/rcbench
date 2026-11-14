/**
 * @file
 * @brief What each face shows: the panel's model and the Dial's state made
 *        into a scene of strings, colours and ring ends.  See
 *        include/dial_face.h.
 *
 * Full scales come from the panel's SCALE packet; one the panel has not
 * sent, or sent as 0, leaves its ring at the track.  A reading without its
 * valid bit shows "--" and no arc.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "dial_face.h"
#include "dial_text.h"
#include "ui_theme.h"

/** @brief The cell voltage of an empty and of a full LiPo cell, mV: the
 *         battery glyph's 0% and 100%. @{ */
#define BATT_EMPTY_MV 3300
#define BATT_FULL_MV  4200 /**< full */
/** @} */
/** @brief RSSI at or above which 4, 3 and 2 link bars light, dBm; below
 *         the last, 1. @{ */
#define RSSI_4 (-60)
#define RSSI_3 (-70) /**< 3 bars */
#define RSSI_2 (-80) /**< 2 bars */
/** @} */

/**
 * @brief A fixed-point number as text, rounded half away from zero.
 * @param out   the text
 * @param cap   its bytes
 * @param v     the value in units of 10^-din
 * @param din   its decimals, 0 to 4
 * @param dout  the decimals shown, 0 to din
 * @param plus  a '+' before a value above 0
 */
static void fixed(char *out, size_t cap, int32_t v, unsigned din,
                  unsigned dout, bool plus)
{
    static const uint32_t p10[5] = { 1u, 10u, 100u, 1000u, 10000u };
    uint32_t a = v < 0 ? (uint32_t)0 - (uint32_t)v : (uint32_t)v;
    const uint32_t drop = p10[din - dout];
    a = a / drop + (a % drop >= drop / 2u && drop > 1u ? 1u : 0u);
    const char *sign = (v < 0 && a != 0u) ? "-" : (plus && a != 0u) ? "+" : "";
    if (dout == 0u) {
        snprintf(out, cap, "%s%lu", sign, (unsigned long)a);
    } else {
        snprintf(out, cap, "%s%lu.%0*lu", sign, (unsigned long)(a / p10[dout]),
                 (int)dout, (unsigned long)(a % p10[dout]));
    }
}

/**
 * @brief An arc's end for a signed share, 0 below 0.
 * @param num the part
 * @param den the whole; 0 or below gives 0
 * @return the end angle
 */
static uint32_t end_of(int64_t num, int64_t den)
{
    if (num <= 0 || den <= 0) {
        return 0;
    }
    return gfx_ring_end((uint64_t)num, (uint64_t)den);
}

void dial_clock_text(uint16_t s, char *out, unsigned cap)
{
    if (s < 3600u) {
        snprintf(out, cap, "%u:%02u", (unsigned)(s / 60u),
                 (unsigned)(s % 60u));
    } else {
        snprintf(out, cap, "%uh%02u", (unsigned)(s / 3600u),
                 (unsigned)((s / 60u) % 60u));
    }
}

/**
 * @brief Set a body block.
 * @param s     the scene
 * @param i     its slot
 * @param text  the text
 * @param y     its middle row
 * @param font  the chain's first font, DIAL_FONT_*
 * @param color ui_color_id_t
 * @param lines the most lines
 * @param kind  DIAL_B_*
 */
static void block(dial_scene_t *s, int i, const char *text, int y,
                  uint8_t font, uint8_t color, uint8_t lines, uint8_t kind)
{
    dial_block_t *b = &s->block[i];
    dk_text_copy(b->text, sizeof b->text, text);
    b->y = (int16_t)y;
    b->font = font;
    b->color = color;
    b->lines = lines;
    b->kind = kind;
}

/**
 * @brief Set the press hints: one line at y 183, one at y 197, below the
 *        bottom row of cells and its clear rectangle.
 * @param s     the scene
 * @param a     the first, or NULL
 * @param b     the second, or NULL
 * @param color ui_color_id_t of the first; the second is TEXT_FAINT
 */
static void hints(dial_scene_t *s, const char *a, const char *b,
                  uint8_t color)
{
    if (a != NULL) {
        dk_text_copy(s->hint[0].text, sizeof s->hint[0].text, a);
    }
    if (b != NULL) {
        dk_text_copy(s->hint[1].text, sizeof s->hint[1].text, b);
    }
    for (int i = 0; i < 2; ++i) {
        s->hint[i].y = (int16_t)(183 + 14 * i);
        s->hint[i].font = DIAL_FONT_FINE;
        s->hint[i].color = (uint8_t)(i == 0 ? color : UI_C_TEXT_FAINT);
        s->hint[i].lines = 1;
        s->hint[i].kind = DIAL_B_TEXT;
    }
}

/**
 * @brief Set a cell of the grid and its ring.
 * @param s     the scene
 * @param i     the cell, row by row
 * @param label its label
 * @param valid whether the reading is valid
 * @param value its value
 * @param unit  its unit
 * @param color ui_color_id_t of its type
 * @param end   its ring's end
 */
static void cell(dial_scene_t *s, int i, const char *label, bool valid,
                 const char *value, const char *unit, uint8_t color,
                 uint32_t end)
{
    dial_cell_t *c = &s->cell[i];
    dk_text_copy(c->label, sizeof c->label, label);
    dk_text_copy(c->value, sizeof c->value, valid ? value : "--");
    dk_text_copy(c->unit, sizeof c->unit, valid ? unit : "");
    c->color = color;
    c->used = true;
    s->ring[i].used = true;
    s->ring[i].color = color;
    s->ring[i].end = valid ? end : 0u;
}

/**
 * @brief The link bars, the battery and their dimming.
 * @param s   the scene
 * @param f   the model
 * @param v   the view
 * @param dim the link is lost or not up
 */
static void furniture(dial_scene_t *s, const dk_face_t *f,
                      const dial_face_view_t *v, bool dim)
{
    s->icons_dim = dim;
    s->bars = 0;
    if (!dim && v->ready && v->rssi != DK_RSSI_NONE) {
        s->bars = v->rssi >= RSSI_4 ? 4 : v->rssi >= RSSI_3 ? 3
                  : v->rssi >= RSSI_2 ? 2 : 1;
    }
    s->bars_color = (f->have_state
                     && (f->state.flags & DK_STA_DEGRADED) != 0u)
                        ? UI_C_WARN : UI_C_OK;
    s->batt_known = v->batt_mv != 0u;
    if (s->batt_known) {
        int pct = ((int)v->batt_mv - BATT_EMPTY_MV) * 100
                  / (BATT_FULL_MV - BATT_EMPTY_MV);
        s->batt_pct = (uint8_t)(pct < 0 ? 0 : pct > 100 ? 100 : pct);
    }
    s->usb = !s->batt_known && v->usb;
}

/**
 * @brief The title tag from the panel's SCREEN.
 * @param s the scene
 * @param f the model
 */
static void title(dial_scene_t *s, const dk_face_t *f)
{
    s->has_title = f->have_screen;
    if (f->have_screen) {
        dk_text_copy(s->title, sizeof s->title, f->screen.title);
    }
}

/**
 * @brief The pill: SAFE, or ARMED with the run clock when there is one.
 * @param s     the scene
 * @param armed the bench is ARMED
 * @param clock the run clock, or NULL
 */
static void pill(dial_scene_t *s, bool armed, const char *clock)
{
    s->pill = armed ? DIAL_PILL_ARMED : DIAL_PILL_SAFE;
    if (!armed) {
        dk_text_copy(s->pill_text, sizeof s->pill_text, "SAFE");
    } else if (clock != NULL) {
        snprintf(s->pill_text, sizeof s->pill_text, "ARMED %s", clock);
    } else {
        dk_text_copy(s->pill_text, sizeof s->pill_text, "ARMED");
    }
}

/** @brief Whether STATE says ARMED. @param f the model @return ARMED */
static bool state_armed(const dk_face_t *f)
{
    return f->have_state && (f->state.flags & DK_STA_ARMED) != 0u;
}

/**
 * @brief MOTOR & ESC: throttle, speed, voltage, current, power, ESC
 *        temperature.
 * @param s the scene
 * @param f the model
 */
static void motor(dial_scene_t *s, const dk_face_t *f)
{
    const uint8_t l = s->lang;
    const dk_live_motor_t *m = &f->motor;
    const bool have = f->have_motor;
    const dk_scale_t *sc = &f->scale;
    const bool fs = f->have_scale;
    const bool armed = have ? (m->flags & DK_LIVE_ARMED) != 0u
                            : state_armed(f);
    char t[32];
    char clock[8];

    if (have) {
        dial_clock_text(m->run_s, clock, sizeof clock);
    }
    pill(s, armed, have ? clock : NULL);
    s->dim = !armed;

    fixed(t, sizeof t, m->thr_cpct, 2u, m->thr_cpct % 100u == 0u ? 0u : 1u,
          false);
    cell(s, 0, dial_text(l, DIAL_L_THROTTLE), have, t, "%", UI_C_ACCENT,
         end_of(m->thr_cpct, DK_THROTTLE_MAX));
    snprintf(t, sizeof t, "%u", (unsigned)m->rpm);
    cell(s, 1, dial_text(l, DIAL_L_RPM), have && (m->valid & DK_MV_RPM),
         t, "rpm", UI_C_RPM, fs ? end_of(m->rpm, sc->rpm) : 0u);
    fixed(t, sizeof t, m->mv, 3u, m->mv < 10000u ? 2u : 1u, false);
    cell(s, 2, dial_text(l, DIAL_L_VOLT), have && (m->valid & DK_MV_VOLT),
         t, "V", UI_C_VOLT, fs ? end_of(m->mv, sc->mv) : 0u);
    fixed(t, sizeof t, m->ca, 2u, (m->ca < 1000 && m->ca > -1000) ? 2u : 1u,
          false);
    cell(s, 3, dial_text(l, DIAL_L_CURR), have && (m->valid & DK_MV_CURR),
         t, "A", UI_C_CURR,
         fs ? end_of(m->ca < 0 ? -m->ca : m->ca, sc->ca) : 0u);
    fixed(t, sizeof t, m->dw, 1u, m->dw < 1000u ? 1u : 0u, false);
    cell(s, 4, dial_text(l, DIAL_L_PWR), have && (m->valid & DK_MV_POWER),
         t, "W", UI_C_POWER,
         fs ? end_of((int64_t)m->dw * 10000, (int64_t)sc->mv * sc->ca) : 0u);
    snprintf(t, sizeof t, "%d", (int)m->esc_c);
    cell(s, 5, dial_text(l, DIAL_L_ESC), have && (m->valid & DK_MV_ESC_T),
         t, "\xC2\xB0" "C", UI_C_LILAC, end_of(m->esc_c, DIAL_TEMP_FS_C));
    if (armed) {
        hints(s, dial_text(l, DIAL_PRESS_ZERO), dial_text(l, DIAL_HOLD_DISARM),
              UI_C_TEXT_FAINT);
    } else {
        hints(s, dial_text(l, DIAL_ARM_PANEL), NULL, UI_C_WARN);
    }
}

/**
 * @brief SERVO: commanded and measured angle, pulse, the rail's voltage,
 *        current and power.
 * @param s the scene
 * @param f the model
 */
static void servo(dial_scene_t *s, const dk_face_t *f)
{
    const uint8_t l = s->lang;
    const dk_live_servo_t *v = &f->servo;
    const bool have = f->have_servo;
    const dk_scale_t *sc = &f->scale;
    const bool fs = f->have_scale;
    const bool armed = have ? (v->flags & DK_LIVE_ARMED) != 0u
                            : state_armed(f);
    const bool cmd = have && v->cmd_us != 0u;
    const bool meas = have && (v->flags & DK_LIVE_MEAS) != 0u;
    const bool rail = have && (v->flags & DK_LIVE_RAIL) != 0u;
    const int64_t tr = fs ? sc->travel : 0;
    char t[32];

    pill(s, armed, NULL);
    s->dim = !armed;

    fixed(t, sizeof t, v->cmd_ddeg, 1u, 1u, true);
    cell(s, 0, dial_text(l, DIAL_L_COMMANDED), cmd, t, "\xC2\xB0",
         UI_C_ACCENT, end_of(v->cmd_ddeg + tr, 2 * tr));
    fixed(t, sizeof t, v->meas_cdeg, 2u, 1u, true);
    cell(s, 1, dial_text(l, DIAL_L_MEASURED), meas, t, "\xC2\xB0",
         UI_C_TEAL, end_of(v->meas_cdeg + 10 * tr, 20 * tr));
    snprintf(t, sizeof t, "%u", (unsigned)v->cmd_us);
    cell(s, 2, dial_text(l, DIAL_L_PULSE), cmd, t, "us", UI_C_LILAC,
         fs ? end_of((int64_t)v->cmd_us - sc->us_min,
                     (int64_t)sc->us_max - sc->us_min) : 0u);
    fixed(t, sizeof t, v->rail_mv, 3u, v->rail_mv < 10000u ? 2u : 1u, false);
    cell(s, 3, dial_text(l, DIAL_L_VOLT), rail, t, "V", UI_C_VOLT,
         fs ? end_of(v->rail_mv, sc->rail_mv) : 0u);
    fixed(t, sizeof t, v->rail_ca, 2u,
          (v->rail_ca < 1000 && v->rail_ca > -1000) ? 2u : 1u, false);
    cell(s, 4, dial_text(l, DIAL_L_CURR), rail, t, "A", UI_C_CURR,
         fs ? end_of(v->rail_ca < 0 ? -v->rail_ca : v->rail_ca, sc->rail_ca)
            : 0u);
    fixed(t, sizeof t, v->rail_dw, 1u, v->rail_dw < 1000u ? 1u : 0u, false);
    cell(s, 5, dial_text(l, DIAL_L_PWR), rail, t, "W", UI_C_POWER,
         fs ? end_of((int64_t)v->rail_dw * 10000,
                     (int64_t)sc->rail_mv * sc->rail_ca) : 0u);
    if (armed) {
        hints(s, dial_text(l, DIAL_PRESS_CENTER),
              dial_text(l, DIAL_HOLD_DISARM), UI_C_TEXT_FAINT);
    } else {
        hints(s, dial_text(l, DIAL_ARM_PANEL), NULL, UI_C_WARN);
    }
}

/**
 * @brief A list: the row before, the focused row, the row after, the
 *        place in the list as rim ticks or, for a long list, a ring.
 * @param s the scene
 * @param f the model
 */
static void nav(dial_scene_t *s, const dk_face_t *f)
{
    const uint8_t l = s->lang;
    const char *row[3] = { "", "", "" };
    unsigned index = 0;
    unsigned count = 0;
    bool any = false;
    char pos[16] = "";

    for (unsigned k = 0; k < 3u; ++k) {
        if ((f->have_nav & (1u << k)) != 0u) {
            row[k] = f->nav[k].item;
            index = f->nav[k].index;
            count = f->nav[k].count;
            any = true;
        }
    }
    if (any && count == 0u) {
        row[DK_SLOT_FOCUS] = dial_text(l, DIAL_EMPTY);
    }
    if (count > 0u) {
        snprintf(pos, sizeof pos, "%u / %u", index + 1u, count);
    }
    block(s, 0, row[DK_SLOT_PREV], 87, DIAL_FONT_SMALL, UI_C_TEXT_FAINT, 1,
          DIAL_B_TEXT);
    block(s, 1, row[DK_SLOT_FOCUS], 118, DIAL_FONT_BIG, UI_C_TEXT, 1,
          DIAL_B_FOCUS);
    block(s, 2, row[DK_SLOT_NEXT], 149, DIAL_FONT_SMALL, UI_C_TEXT_FAINT, 1,
          DIAL_B_TEXT);
    block(s, 3, pos, 166, DIAL_FONT_SMALL, UI_C_TEXT_DIM, 1, DIAL_B_TEXT);
    if (count > DIAL_TICKS_MAX) {
        s->ring[2].used = true;
        s->ring[2].color = UI_C_ACCENT;
        s->ring[2].end = gfx_ring_end(index + 1u, count);
    } else {
        s->ticks = (uint8_t)count;
        s->tick_cur = (uint8_t)index;
    }
    hints(s, dial_text(l, DIAL_PRESS_OPEN), dial_text(l, DIAL_HOLD_BACK),
          UI_C_TEXT_FAINT);
}

/** @brief A format of the faces' words in the face's language:
 *         tools/check_formats.py swaps it for the English literal and holds
 *         each call to its arguments. */
#define DF(id) dial_text(l, (id))

/**
 * @brief A value being set: the number with its unit, its range and step,
 *        the value on entry, one ring from min to max.
 * @param s the scene
 * @param f the model
 */
static void value(dial_scene_t *s, const dk_face_t *f)
{
    const uint8_t l = s->lang;
    const bool have = f->have_meta && f->have_value;
    const bool limit = have && (f->value.flags & DK_VAL_LIMIT) != 0u;
    const bool set = !f->have_value || (f->value.flags & DK_VAL_SET) != 0u;
    char num[24] = "--";
    char line[DIAL_TXT] = "";
    char was[DIAL_TXT] = "";

    if (have) {
        /* An int32 with 3 decimals is 12 characters: the range line is at
         * most 29 of DIAL_TXT, the step line 41.  Step and was are two
         * parts, two spaces apart: a line too wide drops its last part
         * whole, never a number's digits (dial_draw.c). */
        const dk_value_meta_t *m = &f->meta;
        char a[24];
        char b[24];
        dk_value_text(f->value.value, m->decimals, "", num, sizeof num);
        dk_value_text(m->min, m->decimals, "", a, sizeof a);
        dk_value_text(m->max, m->decimals, "", b, sizeof b);
        snprintf(line, sizeof line, DF(DIAL_RANGE), a, b);
        dk_value_text(m->step, m->decimals, "", a, sizeof a);
        dk_value_text(f->value.was, m->decimals, "", b, sizeof b);
        snprintf(was, sizeof was, DF(DIAL_STEP_WAS), a, b);
        s->ring[0].used = true;
        s->ring[0].color = UI_C_ACCENT;
        s->ring[0].end = end_of((int64_t)f->value.value - m->min,
                                (int64_t)m->max - m->min);
    }
    block(s, 0, num, 112, DIAL_FONT_NUM, limit ? UI_C_WARN : UI_C_TEXT, 1,
          DIAL_B_NUMBER);
    if (have) {
        dk_text_copy(s->block[0].unit, sizeof s->block[0].unit, f->meta.unit);
    }
    block(s, 1, line, 146, DIAL_FONT_SMALL,
          limit ? UI_C_WARN : UI_C_TEXT_DIM, 1, DIAL_B_TEXT);
    block(s, 2, was, 164, DIAL_FONT_FINE, UI_C_TEXT_FAINT, 1, DIAL_B_TEXT);
    if (set) {
        hints(s, dial_text(l, DIAL_PRESS_KEEP), dial_text(l, DIAL_HOLD_CANCEL),
              UI_C_TEXT_FAINT);
    } else {
        hints(s, dial_text(l, DIAL_PRESS_SET), dial_text(l, DIAL_HOLD_BACK),
              UI_C_TEXT_FAINT);
    }
}

/**
 * @brief A firmware update over Bluetooth LE: the two versions, then the
 *        press that accepts and the hold that refuses with the seconds
 *        left, or the share taken on the progress ring.  The panel's
 *        faces wait: its own link is held off for the update.
 * @param s the scene
 * @param f the model
 * @param v the view
 */
static void update(dial_scene_t *s, const dk_face_t *f,
                   const dial_face_view_t *v)
{
    const uint8_t l = s->lang;
    char line[DIAL_TXT];
    s->face = DIAL_FACE_UPDATE;
    s->deco = DIAL_DECO_WAIT;
    s->has_title = true;
    dk_text_copy(s->title, sizeof s->title, DF(DIAL_UPD_TITLE));
    furniture(s, f, v, true);
    snprintf(line, sizeof line, DF(DIAL_UPD_FROM_TO), (unsigned)v->upd_from[0],
             (unsigned)v->upd_from[1], (unsigned)v->upd_from[2],
             (unsigned)v->upd_to[0], (unsigned)v->upd_to[1],
             (unsigned)v->upd_to[2]);
    block(s, 0, line, 92, DIAL_FONT_SMALL, UI_C_TEXT_DIM, 1, DIAL_B_TEXT);
    if (v->upd == DIAL_UPD_ASK) {
        block(s, 1, DF(DIAL_UPD_PRESS), 128, DIAL_FONT_MID, UI_C_ACCENT, 2,
              DIAL_B_TEXT);
        snprintf(line, sizeof line, DF(DIAL_UPD_SECS),
                 (unsigned)v->upd_secs);
        block(s, 2, line, 162, DIAL_FONT_SMALL, UI_C_TEXT, 1, DIAL_B_TEXT);
        hints(s, DF(DIAL_UPD_HOLD), NULL, UI_C_WARN);
        return;
    }
    const unsigned pct = v->upd == DIAL_UPD_DONE ? 100u
                         : v->upd_pct > 100u ? 100u : v->upd_pct;
    snprintf(line, sizeof line, "%u", pct);
    block(s, 1, line, 124, DIAL_FONT_NUM, UI_C_TEXT, 1, DIAL_B_NUMBER);
    dk_text_copy(s->block[1].unit, sizeof s->block[1].unit, "%");
    block(s, 2, v->upd == DIAL_UPD_DONE ? DF(DIAL_UPD_RESTART)
                                        : DF(DIAL_UPD_KEEP_ON),
          162, DIAL_FONT_SMALL, UI_C_TEXT, 1, DIAL_B_TEXT);
    s->ring[0].used = true;
    s->ring[0].color = UI_C_ACCENT;
    s->ring[0].end = end_of((int64_t)pct, 100);
}

void dial_scene_build(dial_scene_t *s, const dk_face_t *f,
                      const dial_face_view_t *v, uint8_t lang_last)
{
    const char *name;
    memset(s, 0, sizeof *s);
    if (f == NULL || v == NULL) {
        return;
    }
    name = v->name != NULL ? v->name : "";
    s->lang = f->have_screen ? f->screen.lang : lang_last;
    const uint8_t l = s->lang;

    if (v->upd != DIAL_UPD_NONE) {
        update(s, f, v);
        return;
    }

    if (!v->bound && !v->ready) {
        s->face = DIAL_FACE_BIND;
        s->deco = DIAL_DECO_BIND;
        s->has_title = true;
        dk_text_copy(s->title, sizeof s->title, dial_text(l, DIAL_NOT_BOUND));
        furniture(s, f, v, true);
        block(s, 0, name, 89, DIAL_FONT_BIG, UI_C_ACCENT, 1, DIAL_B_TEXT);
        block(s, 1, v->addr != NULL ? v->addr : "", 111, DIAL_FONT_FINE,
              UI_C_TEXT_DIM, 1, DIAL_B_TEXT);
        block(s, 2, dial_text(l, DIAL_ON_THE_PANEL), 131, DIAL_FONT_SMALL,
              UI_C_TEXT, 1, DIAL_B_TEXT);
        block(s, 3, "SETUP > WIRELESS", 149, DIAL_FONT_SMALL, UI_C_TEXT_DIM,
              1, DIAL_B_TEXT);
        block(s, 4, dial_text(l, DIAL_SCAN_TAP), 165, DIAL_FONT_FINE,
              UI_C_TEXT_DIM, 1, DIAL_B_TEXT);
        hints(s, dial_text(l, DIAL_ADVERTISING), NULL, UI_C_TEXT_FAINT);
        return;
    }
    if (v->lost && f->any) {
        /* The panel's stream stopped after a packet on this link.  From
         * 500ms (DK_DIAL_LOST_MS) the Dial sends no EVENT, so the panel's
         * clock runs out and an ARMED bench goes SAFE; at 1,000ms
         * (DK_DIAL_DROP_MS) the Dial ends the link, and the panel has
         * seen LOST. */
        s->face = DIAL_FACE_LOST;
        s->deco = DIAL_DECO_DASHED;
        title(s, f);
        s->title_dim = true;
        furniture(s, f, v, true);
        block(s, 0, dial_text(l, DIAL_LINK_LOST), 104, DIAL_FONT_BIG,
              UI_C_DANGER, 2, DIAL_B_TEXT);
        block(s, 1, dial_text(l, v->ready ? DIAL_NO_TURN_SENT
                                          : DIAL_BENCH_SAFE),
              142, DIAL_FONT_SMALL, UI_C_TEXT, 1, DIAL_B_TEXT);
        block(s, 2, dial_text(l, v->ready ? DIAL_BENCH_GOES_SAFE
                                          : DIAL_OUTPUTS_OFF),
              160, DIAL_FONT_SMALL, UI_C_TEXT, 1, DIAL_B_TEXT);
        hints(s, dial_text(l, DIAL_ARM_PANEL), NULL, UI_C_WARN);
        return;
    }
    if (v->lost || !f->have_screen) {
        /* No panel packet on this link yet, or packets but no SCREEN.
         * Only while lost does the Dial send no EVENT; STATE, when it
         * came, gives the pill. */
        s->face = DIAL_FACE_WAITING;
        s->deco = DIAL_DECO_WAIT;
        s->has_title = name[0] != '\0';
        s->title_dim = true;
        dk_text_copy(s->title, sizeof s->title, name);
        furniture(s, f, v, true);
        if (f->have_state) {
            pill(s, state_armed(f), NULL);
        }
        block(s, 0, dial_text(l, DIAL_WAITING), 106, DIAL_FONT_MID,
              UI_C_TEXT, 2, DIAL_B_TEXT);
        if (v->lost) {
            block(s, 1, dial_text(l, DIAL_MOVES_NOTHING), 154,
                  DIAL_FONT_SMALL, UI_C_TEXT_DIM, 2, DIAL_B_TEXT);
        }
        return;
    }

    title(s, f);
    furniture(s, f, v, false);
    switch (f->screen.face) {
    case DK_FACE_NAV:
        s->face = DIAL_FACE_NAV;
        nav(s, f);
        break;
    case DK_FACE_VALUE:
        s->face = DIAL_FACE_VALUE;
        value(s, f);
        break;
    case DK_FACE_MOTOR:
        s->face = DIAL_FACE_READING;
        motor(s, f);
        break;
    case DK_FACE_SERVO:
        s->face = DIAL_FACE_READING;
        servo(s, f);
        break;
    default:
        if (f->screen.face == DK_FACE_HOME && f->have_nav != 0u) {
            s->face = DIAL_FACE_NAV;
            nav(s, f);
            break;
        }
        s->face = DIAL_FACE_OTHER;
        pill(s, state_armed(f), NULL);
        block(s, 0, dial_text(l, DIAL_NOTHING_HERE), 124, DIAL_FONT_BODY,
              UI_C_TEXT_DIM, 3, DIAL_B_TEXT);
        break;
    }
}
