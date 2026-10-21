/**
 * @file
 * @brief The panel's half of the SENSE, SERVO_SENSE and SERVO_WIN link pages:
 *        what the last reads hold, handed to the screens, the log and the band
 *        -- the servo windows, the meter, the ESC figures and totals, the
 *        encoder and the parts' addresses and identities -- and the rows of
 *        SETUP the page does not hold.
 *
 * The state machine that decides what
 * is read and written is in sense_link.c.  See sense_link.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_link.h"
#include "sense_link_int.h"

#include <stddef.h>
#include <string.h>

/**
 * @brief SERVO_SENSE's last read: the channels whose window holds readings, bit
 *        n-1 for CHn.
 *
 * @param s The link state.
 * @return Bit n-1 set for CHn with readings.
 */
static uint8_t servo_ok(const sense_link_t *s)
{
    uint8_t ok = 0u;
    if (!s->have_servo) {
        return 0u;
    }
    for (unsigned ch = 1u; ch <= LINK_SS_CHANNELS; ++ch) {
        if ((s->servo[LINK_SS_CH_FLAGS] & LINK_SS_CH_VALID(ch)) != 0u) {
            ok |= (uint8_t)(1u << (ch - 1u));
        }
    }
    return ok;
}

/**
 * @brief Whether SERVO_SENSE's last read holds a window not taken yet.
 *
 * @param s The link state.
 * @return true when such a window is waiting.
 */
static bool servo_window_new(const sense_link_t *s)
{
    return servo_ok(s) != 0u
           && !(s->window_taken && s->servo[LINK_SS_WINDOW] == s->window_last);
}

unsigned sense_link_windows(const sense_link_t *s)
{
    if (s == NULL) {
        return 0u;
    }
    if (sense_link_win_on(s)) {
        return s->winq_n;
    }
    return servo_window_new(s) ? 1u : 0u;
}

/**
 * @brief Channel index @p i of SERVO_SENSE's last read into @p b.
 *
 * @param s The link state.
 * @param i The channel index, from 0.
 * @param b Receives the channel figures.
 */
static void servo_channel(const sense_link_t *s, size_t i, bench_state_t *b)
{
    const uint16_t *c = &s->servo[i * (size_t)LINK_SS_CH_STRIDE];
    b->servo_mean_ma[i] = (int16_t)c[LINK_SS_CH_MEAN_MA];
    b->servo_max_ma[i]  = (int16_t)c[LINK_SS_CH_MAX_MA];
    b->servo_min_mv[i]  = c[LINK_SS_CH_MIN_MV];
}

void sense_link_win_bench(const sense_link_t *s, const sense_link_win_t *w,
                          bench_state_t *b)
{
    if (s == NULL || w == NULL || b == NULL) {
        return;
    }
    b->servo_new    = true;
    b->servo_window = w->number;
    b->servo_ok     = (w->current || w->voltage) ? 0x01u : 0u;
    /* A window can close with one quantity and not the other: the one it
     * lacks reads 0 on the page and is no reading. */
    b->servo_no_current = (uint8_t)(w->current ? 0u : 0x01u);
    b->servo_no_voltage = (uint8_t)(w->voltage ? 0u : 0x01u);
    b->servo_mean_ma[0] = w->mean_ma;
    b->servo_max_ma[0]  = w->max_ma;
    b->servo_min_mv[0]  = w->min_mv;
    /* CH2 and CH3 have no ring: theirs is SERVO_SENSE's one window, on
     * the row of the same number. */
    const uint8_t others = (s->have_servo
                            && s->servo[LINK_SS_WINDOW] == w->number)
                               ? (uint8_t)(servo_ok(s) & 0x06u) : 0u;
    b->servo_ok |= others;
    for (size_t i = 1u; i < LINK_SS_CHANNELS; ++i) {
        if ((others & (1u << i)) != 0u) {
            servo_channel(s, i, b);
        } else {
            b->servo_mean_ma[i] = 0;
            b->servo_max_ma[i]  = 0;
            b->servo_min_mv[i]  = 0u;
        }
    }
}

bool sense_link_take_window(sense_link_t *s, bench_state_t *b)
{
    if (s == NULL || b == NULL) {
        return false;
    }
    if (sense_link_win_on(s)) {
        sense_link_win_t w;
        if (!sense_link_take_win(s, &w)) {
            return false;
        }
        sense_link_win_bench(s, &w, b);
        return true;
    }
    if (!servo_window_new(s)) {
        return false;
    }
    const uint16_t window = s->servo[LINK_SS_WINDOW];
    s->window_taken = true;
    s->window_last  = window;
    b->servo_new    = true;
    b->servo_window = window;
    b->servo_ok     = servo_ok(s);
    b->servo_no_current = 0u;
    b->servo_no_voltage = 0u;
    for (size_t i = 0u; i < LINK_SS_CHANNELS; ++i) {
        servo_channel(s, i, b);
    }
    return true;
}

/**
 * @brief Whether the page holds what the panel asks of it.
 *
 * With the map in
 * force the INA3221's enable bit and frame are the map's mirror, which the
 * panel does not ask: they are left out.
 *
 * @param s The link state.
 * @return true when the page holds the set-up asked.
 */
static bool held_as_asked(const sense_link_t *s)
{
    uint16_t want[LINK_SN_CONFIG_COUNT];
    memcpy(want, s->want, sizeof(want));
    if (s->map) {
        want[LINK_SN_ENABLE] =
            (uint16_t)((want[LINK_SN_ENABLE] & ~LINK_SN_EN_I3221)
                       | (s->held[LINK_SN_ENABLE] & LINK_SN_EN_I3221));
        memcpy(&want[I3221_AT], &s->held[I3221_AT],
               FRAME_N * sizeof(uint16_t));
    }
    return memcmp(want, s->held, sizeof(want)) == 0;
}

void sense_link_meter(const sense_link_t *s, sense_link_meter_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (s == NULL) {
        return;
    }
    out->page   = s->page;
    /* With the map FAST on any channel; without, CH1 of the one part,
     * the channel an older IOmcu reads at 1 kHz. */
    out->wanted = s->want_set && s->want_i3221 && !s->want_unwatched
                  && (s->map
                      || ((s->want[LINK_SN_I3221_CHANNELS] & 0x01u) != 0u
                          && !s->want_off_ch1));
    uint16_t regs[FRAME_N];
    out->held = out->wanted && s->known
                && (s->held[LINK_SN_ENABLE] & LINK_SN_EN_I3221) != 0u
                && (s->map
                    || (s->held[LINK_SN_I3221_CHANNELS] & 0x01u) != 0u)
                && !s->refused_bus && !s->refused_i228 && !s->refused_i3221
                && held_as_asked(s) && (!s->map || s->map_held)
                && sense_link_write_owed(s, regs) == SENSE_LINK_OP_NONE;
    out->setups    = s->setups;
    out->status    = s->have_status;
    out->status_ms = s->status_ms;
    out->flags     = sense_link_flags(s);
    out->resets_read = s->have_status && s->win_page
                       && sense_link_status_count(s) == SENSE_LINK_STATUS_COUNT;
    out->resets    = out->resets_read
                         ? LINK_SN_RESETS_I3221(s->status[ST(LINK_SN_RESETS)])
                         : 0u;
    out->win       = sense_link_win_on(s) && s->win_have && !s->win_stands;
    out->win_valid = out->win && s->win_valid;
    out->win_ms    = s->win_moved_ms;
}

uint16_t sense_link_flags(const sense_link_t *s)
{
    return (s != NULL && s->have_status) ? s->status[ST(LINK_SN_FLAGS)] : 0u;
}

uint32_t sense_link_reads(const sense_link_t *s)
{
    return (s != NULL) ? s->status_reads : 0u;
}

bool sense_link_esc(const sense_link_t *s, uint32_t now_ms, bool *v_ok,
                    float *volts, bool *i_ok, float *amps)
{
    if (v_ok == NULL || volts == NULL || i_ok == NULL || amps == NULL) {
        return false;
    }
    *v_ok  = false;
    *i_ok  = false;
    *volts = 0.0f;
    *amps  = 0.0f;
    if (s == NULL || !s->have_status
        || (uint32_t)(now_ms - s->status_ms) >= SENSE_LINK_STALE_MS) {
        return false;
    }
    const uint16_t f = s->status[ST(LINK_SN_ESC_FLAGS)];
    *v_ok  = (f & LINK_SN_ESC_VOLTAGE_OK) != 0u;
    *i_ok  = (f & LINK_SN_ESC_CURRENT_OK) != 0u;
    *volts = (float)s->status[ST(LINK_SN_ESC_VOLTAGE_CV)] / 100.0f;
    *amps  = (float)s->status[ST(LINK_SN_ESC_CURRENT_CA)] / 100.0f;
    return true;
}

bool sense_link_totals(const sense_link_t *s, uint32_t now_ms,
                       int32_t *charge_cmah, uint32_t *energy_cwh)
{
    if (s == NULL || charge_cmah == NULL || energy_cwh == NULL
        || !s->have_status
        || (uint32_t)(now_ms - s->status_ms) >= SENSE_LINK_TOTALS_MS) {
        return false;
    }
    const uint32_t c = (uint32_t)s->status[ST(LINK_SN_I228_CHARGE_LO)]
                       | ((uint32_t)s->status[ST(LINK_SN_I228_CHARGE_HI)]
                          << 16);
    *charge_cmah = (int32_t)c;
    *energy_cwh  = (uint32_t)s->status[ST(LINK_SN_I228_ENERGY_LO)]
                   | ((uint32_t)s->status[ST(LINK_SN_I228_ENERGY_HI)] << 16);
    return true;
}

bool sense_link_enc_on(const sense_link_t *s)
{
    return s != NULL && s->known
           && (s->held[LINK_SN_ENABLE] & LINK_SN_EN_AS5600) != 0u;
}

/**
 * @brief Whether the last SENSE read is fresh at @p now_ms and holds a reading
 *        of the encoder's registers from a part that answers; its flags into @p
 *        flags.
 *
 * @param s      The link state.
 * @param now_ms The time, in ms.
 * @param flags  Receives the encoder flags.
 * @return true when the reading is fresh and from a part that answers.
 */
static bool enc_read(const sense_link_t *s, uint32_t now_ms, uint16_t *flags)
{
    if (!sense_link_enc_on(s) || !s->have_status
        || (uint32_t)(now_ms - s->status_ms) >= SENSE_LINK_STALE_MS) {
        return false;
    }
    const uint16_t f = s->status[ST(LINK_SN_AS5600_FLAGS)];
    if ((f & LINK_SN_ENC_ONLINE) == 0u || (f & LINK_SN_ENC_VALID) == 0u) {
        return false;
    }
    *flags = f;
    return true;
}

bool sense_link_enc(const sense_link_t *s, uint32_t now_ms,
                    sense_link_enc_t *out)
{
    uint16_t f = 0u;
    /* STATUS MD clear: no magnet, and RAW ANGLE is not a position
     * (as5600.h).  ML and MH leave it one. */
    if (out == NULL || !enc_read(s, now_ms, &f)
        || (f & LINK_SN_ENC_MD) == 0u) {
        return false;
    }
    out->raw      = s->status[ST(LINK_SN_AS5600_ANGLE)];
    out->samples  = s->status[ST(LINK_SN_AS5600_SAMPLES)];
    out->still_ms = s->status[ST(LINK_SN_AS5600_STILL_MS)];
    out->taken_ms = s->status_ms;
    out->weak     = (f & LINK_SN_ENC_ML) != 0u;
    out->strong   = (f & LINK_SN_ENC_MH) != 0u;
    return true;
}

bool sense_link_enc_no_magnet(const sense_link_t *s, uint32_t now_ms)
{
    uint16_t f = 0u;
    return enc_read(s, now_ms, &f) && (f & LINK_SN_ENC_MD) == 0u;
}

uint16_t sense_link_enc_magnet(const sense_link_t *s)
{
    return (s != NULL) ? s->enc_magnet : 0u;
}

uint8_t sense_link_addr(const sense_link_t *s, sense_link_part_t part)
{
    if (s == NULL || !s->known) {
        return 0u;
    }
    return (uint8_t)s->held[(part == SENSE_LINK_INA228) ? LINK_SN_I228_ADDR
                                                        : LINK_SN_I3221_ADDR];
}

uint16_t sense_link_id(const sense_link_t *s, sense_link_part_t part)
{
    if (s == NULL || !s->have_status) {
        return 0u;
    }
    return s->status[(part == SENSE_LINK_INA228) ? ST(LINK_SN_I228_ID)
                                                 : ST(LINK_SN_I3221_ID)];
}

uint8_t sense_link_found(const sense_link_t *s, sense_link_part_t part)
{
    if (s == NULL || !s->have_status || !s->known) {
        return 0u;
    }
    const uint16_t present = s->status[ST(LINK_SN_PRESENT)];
    const bool i228 = part == SENSE_LINK_INA228;
    const unsigned last = i228 ? LINK_SN_I228_ADDR_MAX : LINK_SN_I3221_ADDR_MAX;
    const sense_link_part_t other = i228 ? SENSE_LINK_INA3221
                                         : SENSE_LINK_INA228;
    const bool other_on = (s->held[LINK_SN_ENABLE]
                           & sense_link_en_bit[other]) != 0u;
    const unsigned mine = sense_link_addr(s, part);
    const unsigned theirs = sense_link_addr(s, other);
    for (unsigned a = 0x40u; a <= last; ++a) {
        /* Every fitted INA3221 answers where it is meant to. */
        const bool fitted = a <= LINK_SN_I3221_ADDR_MAX
                            && ((s->want_parts >> (a - 0x40u)) & 1u) != 0u;
        if ((present & (1u << (a - 0x40u))) == 0u || a == mine
            || (other_on && a == theirs) || fitted) {
            continue;
        }
        return (uint8_t)a;
    }
    return 0u;
}

uint16_t sense_link_event_id(const sense_link_t *s, sense_link_part_t part)
{
    return (s != NULL && (unsigned)part < 2u) ? s->ev_id[part] : 0u;
}

uint8_t sense_link_event_found(const sense_link_t *s, sense_link_part_t part)
{
    return (s != NULL && (unsigned)part < 2u) ? s->ev_found[part] : 0u;
}

uint8_t sense_link_clipped_channel(const sense_link_t *s)
{
    return (s != NULL) ? s->clipped_ch : 0u;
}

uint16_t sense_link_i3221_dmohm(const sense_link_t *s)
{
    return (s != NULL) ? s->held[LINK_SN_I3221_SHUNT_DMOHM] : 0u;
}

uint8_t sense_link_i3221_addr(const sense_link_t *s)
{
    return (s != NULL && s->known) ? (uint8_t)s->held[LINK_SN_I3221_ADDR]
                                   : 0u;
}

int sense_link_sda(const sense_link_t *s)
{
    return (s != NULL) ? (int)s->want_sda : -1;
}

int sense_link_scl(const sense_link_t *s)
{
    return (s != NULL) ? (int)s->want_scl : -1;
}

/* ------------------------------------------- the set-up and the ring */

uint16_t sense_link_unheld(const sense_link_t *s)
{
    if (s == NULL || !s->up || !s->want_set) {
        return 0u;
    }
    /* The parts as SETUP has them, against ENABLE as the page holds it. */
    const uint16_t asked = (uint16_t)(
        (s->want_i228 ? SENSE_LINK_ROW_I228 : 0u)
        | (s->want_i3221 ? SENSE_LINK_ROW_I3221 : 0u)
        | (s->want_enc ? SENSE_LINK_ROW_ENC : 0u));
    if (!s->page) {
        return asked;
    }
    if (!s->known) {
        return 0u;
    }
    const uint16_t en = s->held[LINK_SN_ENABLE];
    const uint16_t on = (uint16_t)(
        (((en & LINK_SN_EN_I228) != 0u) ? SENSE_LINK_ROW_I228 : 0u)
        | (((en & LINK_SN_EN_I3221) != 0u) ? SENSE_LINK_ROW_I3221 : 0u)
        | (((en & LINK_SN_EN_AS5600) != 0u) ? SENSE_LINK_ROW_ENC : 0u));
    uint16_t rows = (uint16_t)(asked ^ on);
    if (s->want[LINK_SN_SDA_PIN] != s->held[LINK_SN_SDA_PIN]
        || s->want[LINK_SN_SCL_PIN] != s->held[LINK_SN_SCL_PIN]) {
        rows |= SENSE_LINK_ROW_PINS;
    }
    static const struct { uint8_t reg; uint16_t row; } k_rows[] = {
        { LINK_SN_I228_ADDR,         SENSE_LINK_ROW_I228_ADDR },
        { LINK_SN_I228_SHUNT_UOHM,   SENSE_LINK_ROW_I228_SHUNT },
        { LINK_SN_I228_MAX_DA,       SENSE_LINK_ROW_I228_MAX },
        { LINK_SN_I3221_ADDR,        SENSE_LINK_ROW_I3221_ADDR },
        { LINK_SN_I3221_SHUNT_DMOHM, SENSE_LINK_ROW_I3221_SHUNT },
        { LINK_SN_I3221_CHANNELS,    SENSE_LINK_ROW_I3221_CH },
    };
    for (size_t i = 0u; i < sizeof(k_rows) / sizeof(k_rows[0]); ++i) {
        if (s->want[k_rows[i].reg] != s->held[k_rows[i].reg]) {
            rows |= k_rows[i].row;
        }
    }
    if (s->map) {
        /* The INA3221's rows are SENSORS', which SENSE_MAP marks. */
        rows &= (uint16_t)~(SENSE_LINK_ROW_I3221 | SENSE_LINK_ROW_I3221_ADDR
                            | SENSE_LINK_ROW_I3221_SHUNT
                            | SENSE_LINK_ROW_I3221_CH);
    }
    return rows;
}

void sense_link_set_map(sense_link_t *s, bool map)
{
    if (s != NULL) {
        s->map = map;
    }
}

void sense_link_set_map_held(sense_link_t *s, bool held)
{
    if (s != NULL) {
        s->map_held = held;
    }
}

void sense_link_reread(sense_link_t *s)
{
    if (s != NULL && s->page) {
        s->known     = false;
        s->caps_owed = true;
    }
}

bool sense_link_take_caps(sense_link_t *s, uint16_t *caps)
{
    if (s == NULL || caps == NULL || !s->caps_new) {
        return false;
    }
    *caps = s->caps;
    s->caps_new = false;
    return true;
}

bool sense_link_take_win(sense_link_t *s, sense_link_win_t *out)
{
    if (s == NULL || out == NULL || s->winq_n == 0u) {
        return false;
    }
    *out = s->winq[0];
    --s->winq_n;
    memmove(&s->winq[0], &s->winq[1], s->winq_n * sizeof(s->winq[0]));
    return true;
}

uint32_t sense_link_win_lost(const sense_link_t *s)
{
    return (s != NULL) ? s->win_lost : 0u;
}

bool sense_link_held_pins(const sense_link_t *s, int *sda, int *scl)
{
    const bool known = s != NULL && s->up && (!s->page || s->known);
    const bool on = known && s->page
                    && (s->held[LINK_SN_ENABLE]
                        & (LINK_SN_EN_I228 | LINK_SN_EN_I3221
                           | LINK_SN_EN_AS5600)) != 0u;
    if (sda != NULL) {
        *sda = on ? (int)s->held[LINK_SN_SDA_PIN] : -1;
    }
    if (scl != NULL) {
        *scl = on ? (int)s->held[LINK_SN_SCL_PIN] : -1;
    }
    return known;
}
