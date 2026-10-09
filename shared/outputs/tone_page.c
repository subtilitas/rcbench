/*
 * The TONE link page.  See tone_page.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "tone_page.h"

#include <string.h>

#include "link_msg.h"

/* The beep numbers: 1 to 65535, then 1 again. */
#define SEQ_PERIOD 65535u

void tone_page_defaults(uint16_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, LINK_TN_CONFIG_COUNT * sizeof(uint16_t));
    cfg[LINK_TN_PIN]         = LINK_TN_DEFAULT_PIN;
    cfg[LINK_TN_F_MIN_HZ]    = LINK_TN_DEFAULT_F_MIN;
    cfg[LINK_TN_F_MAX_HZ]    = LINK_TN_DEFAULT_F_MAX;
    cfg[LINK_TN_SPLIT_PCT]   = LINK_TN_DEFAULT_SPLIT;
    cfg[LINK_TN_GAP_MS]      = LINK_TN_DEFAULT_GAP_MS;
    cfg[LINK_TN_MIN_PERIODS] = LINK_TN_DEFAULT_PERIODS;
}

void tone_page_init(tone_page_t *p)
{
    if (p != NULL) {
        memset(p, 0, sizeof(*p));
        tone_page_defaults(p->cfg);
        p->first = 1u;
    }
}

bool tone_page_enabled(const tone_page_t *p)
{
    return p != NULL && (p->cfg[LINK_TN_ENABLE] & LINK_TN_EN_TAP) != 0u;
}

uint8_t tone_page_pin(const tone_page_t *p)
{
    return p != NULL ? (uint8_t)p->cfg[LINK_TN_PIN] : 0u;
}

bool tone_page_wanted(const tone_page_t *p)
{
    return tone_page_enabled(p) && !p->refused;
}

uint64_t tone_page_pins(const tone_page_t *p)
{
    return tone_page_wanted(p) ? ((uint64_t)1u << p->cfg[LINK_TN_PIN]) : 0u;
}

/* The pins that are ADC inputs on either part the image may run on: GP26 to
 * GP29 of the RP2350A, GP40 to GP47 of the RP2354B.  Rated IOVDD + 0.5 V,
 * not fault tolerant. */
static bool adc_pin(uint16_t pin)
{
    return (pin >= 26u && pin <= 29u) || (pin >= 40u && pin <= 47u);
}

/* Whether @p pin may carry the tap: in the bank, no ADC pin, not another
 * page's, not reserved unless the tap already holds it, and no output's. */
static bool pin_free(const tone_page_t *p, const outputs_t *o, uint16_t pin,
                     uint64_t taken)
{
    if (pin > OUT_MAX_PIN || adc_pin(pin)) {
        return false;
    }
    const uint64_t bit = (uint64_t)1u << pin;
    if ((taken & bit) != 0u) {
        return false;
    }
    const bool ours = (tone_page_pins(p) & bit) != 0u;
    if (!ours && !outputs_pin_available(o, (uint8_t)pin)) {
        return false;
    }
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (o->slot[i].driver != OUT_DRIVER_NONE && o->slot[i].pin == pin) {
            return false;
        }
    }
    return true;
}

bool tone_page_pin_free(const tone_page_t *p, const outputs_t *o,
                        uint64_t taken)
{
    return p != NULL && o != NULL && pin_free(p, o, p->cfg[LINK_TN_PIN], taken);
}

/* The values themselves, whatever is enabled: a set-up kept in flash is one
 * a later enable can take without another write.  The detector's own
 * verdict is the last word: tone_init() refuses a gap shorter than the
 * lowest tone's period and any range the tick clock cannot resolve. */
static bool values_ok(tone_page_t *p, const uint16_t *c)
{
    if (c[LINK_TN_ENABLE] > LINK_TN_EN_TAP || c[LINK_TN_PIN] > OUT_MAX_PIN
        || c[LINK_TN_RESERVED_7] != 0u
        || c[LINK_TN_F_MIN_HZ] < LINK_TN_F_MIN_LO
        || c[LINK_TN_F_MIN_HZ] > LINK_TN_F_MIN_HI
        || c[LINK_TN_F_MAX_HZ] <= c[LINK_TN_F_MIN_HZ]
        || c[LINK_TN_F_MAX_HZ] > LINK_TN_F_MAX_HI
        || c[LINK_TN_SPLIT_PCT] > LINK_TN_SPLIT_MAX
        || c[LINK_TN_GAP_MS] < LINK_TN_GAP_MS_MIN
        || c[LINK_TN_GAP_MS] > LINK_TN_GAP_MS_MAX
        || c[LINK_TN_MIN_PERIODS] < LINK_TN_PERIODS_MIN
        || c[LINK_TN_MIN_PERIODS] > LINK_TN_PERIODS_MAX) {
        return false;
    }
    tone_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.f_min_hz    = c[LINK_TN_F_MIN_HZ];
    cmd.f_max_hz    = c[LINK_TN_F_MAX_HZ];
    cmd.split_pct   = c[LINK_TN_SPLIT_PCT];
    cmd.gap_ms      = c[LINK_TN_GAP_MS];
    cmd.min_periods = c[LINK_TN_MIN_PERIODS];
    tone_cfg_t cfg;
    tone_svc_cfg(&cmd, &cfg);
    return tone_init(&p->scratch, &cfg);
}

/* A new set-up: what core 1 said under the old one is gone, so no read
 * shows a tone or a flag before core 1 has run under the new one.  The
 * ring and the counters are the capture's and stay. */
static void forget_status(tone_page_t *p)
{
    p->running = false;
    p->overrun = false;
    p->beep = false;
    p->tone = false;
    p->window = 0u;
    p->win_freq_dhz = 0u;
    p->win_periods = 0u;
    p->seen_lost = 0u;
    p->seen_glitches = 0u;
}

uint8_t tone_page_write(tone_page_t *p, uint8_t off, uint8_t n,
                        const uint16_t *in, const outputs_t *o,
                        uint64_t taken)
{
    if (p == NULL || in == NULL || o == NULL) {
        return LINK_NACK_BAD_RANGE;
    }
    if ((unsigned)off + (unsigned)n > (unsigned)LINK_TN_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    if (n == 0u) {
        return 0u;
    }
    if (off == (uint8_t)LINK_TN_EVT_SEL && n == 1u) {
        p->evt_sel = in[0];
        return 0u;
    }
    /* The set-up is registers 0 to 7; the rest is read only. */
    if ((unsigned)off + (unsigned)n > (unsigned)LINK_TN_FLAGS) {
        return LINK_NACK_READ_ONLY;
    }
    uint16_t next[LINK_TN_FLAGS];
    memset(next, 0, sizeof(next));
    memcpy(next, p->cfg, sizeof(p->cfg));
    for (uint8_t i = 0; i < n; ++i) {
        next[off + i] = in[i];
    }
    if (memcmp(next, p->cfg, sizeof(p->cfg)) == 0 && !p->refused
        && next[LINK_TN_RESERVED_7] == 0u) {
        return 0u;                  /* the set-up in force: nothing moves */
    }
    if (!values_ok(p, next)) {
        return LINK_NACK_BAD_VALUE;
    }
    if ((next[LINK_TN_ENABLE] & LINK_TN_EN_TAP) != 0u
        && !pin_free(p, o, next[LINK_TN_PIN], taken)) {
        return LINK_NACK_BAD_VALUE;
    }
    memcpy(p->cfg, next, sizeof(p->cfg));
    p->refused = false;
    ++p->gen;
    forget_status(p);
    return 0u;
}

void tone_page_restore(tone_page_t *p, const uint16_t *cfg,
                       const outputs_t *o, uint64_t taken)
{
    if (p == NULL || cfg == NULL || o == NULL) {
        return;
    }
    uint16_t next[LINK_TN_FLAGS];
    memset(next, 0, sizeof(next));
    memcpy(next, cfg, LINK_TN_CONFIG_COUNT * sizeof(uint16_t));
    if (!values_ok(p, next)) {
        tone_page_defaults(next);
    }
    /* The saved pin is judged before it is installed: once the set-up is in
     * the page, the page owns the pin and the reservations are not asked. */
    const bool refused = (next[LINK_TN_ENABLE] & LINK_TN_EN_TAP) != 0u
                         && !pin_free(p, o, next[LINK_TN_PIN], taken);
    memcpy(p->cfg, next, sizeof(p->cfg));
    ++p->gen;
    forget_status(p);
    p->refused = refused;
}

void tone_page_revert(tone_page_t *p, const uint16_t *cfg, bool refused)
{
    if (p == NULL || cfg == NULL) {
        return;
    }
    memcpy(p->cfg, cfg, sizeof(p->cfg));
    p->refused = refused;
    ++p->gen;
    forget_status(p);
}

void tone_page_refuse(tone_page_t *p)
{
    if (p != NULL && tone_page_enabled(p)) {
        p->refused = true;
        ++p->gen;
        forget_status(p);
    }
}

void tone_page_capture(tone_page_t *p)
{
    if (p != NULL) {
        ++p->cap_gen;
        p->first = p->n + 1u;
        forget_status(p);
    }
}

void tone_page_recapture(tone_page_t *p)
{
    if (p != NULL) {
        ++p->cap_gen;
        forget_status(p);
    }
}

uint8_t tone_page_slots_check(const tone_page_t *p, const uint16_t *slots)
{
    if (slots == NULL) {
        return LINK_NACK_BAD_VALUE;
    }
    const uint64_t held = tone_page_pins(p);
    for (unsigned s = 0; s < LINK_OUT_SLOTS; ++s) {
        const uint16_t *r = &slots[(size_t)s * LINK_OS_STRIDE];
        if (r[LINK_OS_DRIVER] != 0u && r[LINK_OS_PIN] <= OUT_MAX_PIN
            && (held & ((uint64_t)1u << r[LINK_OS_PIN])) != 0u) {
            return LINK_NACK_BAD_VALUE;
        }
    }
    return 0u;
}

void tone_page_cmd(const tone_page_t *p, bool run, tone_cmd_t *cmd)
{
    if (p == NULL || cmd == NULL) {
        return;
    }
    memset(cmd, 0, sizeof(*cmd));
    cmd->gen         = p->gen;
    cmd->cap_gen     = p->cap_gen;
    cmd->run         = run && tone_page_wanted(p);
    cmd->f_min_hz    = p->cfg[LINK_TN_F_MIN_HZ];
    cmd->f_max_hz    = p->cfg[LINK_TN_F_MAX_HZ];
    cmd->split_pct   = p->cfg[LINK_TN_SPLIT_PCT];
    cmd->gap_ms      = p->cfg[LINK_TN_GAP_MS];
    cmd->min_periods = p->cfg[LINK_TN_MIN_PERIODS];
}

void tone_page_publish(tone_page_t *p, const tone_status_t *st,
                       const tone_rec_t *rec, size_t n)
{
    if (p == NULL || st == NULL || st->gen != p->gen
        || st->cap_gen != p->cap_gen) {
        return;
    }
    p->running = st->running;
    if (!st->running) {
        p->overrun = p->beep = p->tone = false;
        p->window = p->win_freq_dhz = p->win_periods = 0u;
        return;
    }
    p->overrun = st->overrun;
    p->beep = st->beep;
    p->tone = st->tone;
    p->window = st->window;
    p->win_freq_dhz = st->win_freq_dhz;
    p->win_periods = st->win_periods;
    p->lost += st->lost - p->seen_lost;
    p->glitches += st->glitches - p->seen_glitches;
    p->seen_lost = st->lost;
    p->seen_glitches = st->glitches;
    tone_page_beeps(p, st->gen, st->cap_gen, rec, n);
}

void tone_page_beeps(tone_page_t *p, uint16_t gen, uint16_t cap_gen,
                     const tone_rec_t *rec, size_t n)
{
    if (p == NULL || rec == NULL || gen != p->gen || cap_gen != p->cap_gen) {
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        p->ring[p->n % LINK_TN_RING] = rec[i];
        ++p->n;
    }
}

void tone_page_dropped(tone_page_t *p, uint32_t n)
{
    if (p != NULL) {
        p->lost += n;
    }
}

/* Beep index @p i (a count from 1) as the number the wire carries. */
static uint16_t seq_of(uint32_t i)
{
    return i == 0u ? 0u : (uint16_t)((i - 1u) % SEQ_PERIOD + 1u);
}

/* The ring's entry for beep number @p sel, or NULL if it is not there. */
static const tone_rec_t *find(const tone_page_t *p, uint16_t sel)
{
    if (sel == 0u) {
        return NULL;
    }
    for (uint32_t k = 0; k < LINK_TN_RING && k < p->n; ++k) {
        const uint32_t i = p->n - k;
        if (i < p->first) {
            break;
        }
        if (seq_of(i) == sel) {
            return &p->ring[(i - 1u) % LINK_TN_RING];
        }
    }
    return NULL;
}

void tone_page_read(const tone_page_t *p, uint8_t off, uint8_t n,
                    uint16_t *out)
{
    if (p == NULL || out == NULL
        || (unsigned)off + (unsigned)n > (unsigned)LINK_TN_COUNT) {
        return;
    }
    uint16_t r[LINK_TN_COUNT];
    memset(r, 0, sizeof(r));
    memcpy(r, p->cfg, sizeof(p->cfg));
    uint16_t f = 0u;
    if (p->running)       { f |= LINK_TN_RUNNING; }
    if (p->refused)       { f |= LINK_TN_PIN_REFUSED; }
    if (p->overrun)       { f |= LINK_TN_OVERRUN; }
    if (p->beep)          { f |= LINK_TN_BEEP; }
    if (p->tone)          { f |= LINK_TN_TONE; }
    r[LINK_TN_FLAGS]        = f;
    r[LINK_TN_WINDOW]       = p->window;
    r[LINK_TN_WIN_FREQ_DHZ] = p->win_freq_dhz;
    r[LINK_TN_WIN_PERIODS]  = p->win_periods;
    r[LINK_TN_BEEP_HEAD]    = seq_of(p->n);
    r[LINK_TN_EVT_SEL]      = p->evt_sel;
    const tone_rec_t *e = find(p, p->evt_sel);
    if (e != NULL) {
        r[LINK_TN_EVT_SEQ]         = p->evt_sel;
        r[LINK_TN_EVT_START_LO]    = (uint16_t)(e->start_ms & 0xFFFFu);
        r[LINK_TN_EVT_START_HI]    = (uint16_t)(e->start_ms >> 16);
        r[LINK_TN_EVT_LEN_DMS]     = e->len_dms;
        r[LINK_TN_EVT_FREQ_DHZ]    = e->freq_dhz;
        r[LINK_TN_EVT_BURSTS]      = e->bursts;
        r[LINK_TN_EVT_CARRIER_HHZ] = e->carrier_hhz;
        r[LINK_TN_EVT_FLAGS]       = e->flags;
    }
    r[LINK_TN_LOST]         = (uint16_t)(p->lost & 0xFFFFu);
    r[LINK_TN_GLITCHES]     = (uint16_t)(p->glitches & 0xFFFFu);
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = r[off + i];
    }
}
