/*
 * The SENSE_MAP, SENSE_WIN_A and SENSE_WIN_B link pages.  See sense_map.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_map.h"

#include <stddef.h>
#include <string.h>

#include "ina3221.h"
#include "link_msg.h"
#include "link_pages.h"
#include "sense_page_int.h"

_Static_assert(LINK_SM_PARTS == SENSE_I3221_PARTS, "the map's parts");
_Static_assert(LINK_SM_CHANNELS == SENSE_CHANNELS, "the map's channels");
_Static_assert(LINK_SX_ENTRIES + LINK_SX_PER_PAGE * LINK_SX_E_STRIDE
               == LINK_SX_COUNT, "SENSE_WIN is its header and six entries");
_Static_assert(2u * LINK_SX_PER_PAGE == LINK_SM_CHANNELS,
               "two SENSE_WIN pages carry every channel");

static bool fitted(const uint16_t *m, unsigned a)
{
    return (m[LINK_SM_ENABLE] & (1u << a)) != 0u;
}

/* The map SENSE's one part makes: what a read shows and a first write
 * starts from. */
static void derive(const sense_page_t *p, uint16_t *m)
{
    memset(m, 0, LINK_SM_KEPT * sizeof(uint16_t));
    m[LINK_SM_FAST] = LINK_SM_FAST_NONE;
    const uint16_t *c = p->sense;
    const uint16_t addr = c[LINK_SN_I3221_ADDR];
    if (addr < INA3221_ADDR_MIN || addr > INA3221_ADDR_MAX) {
        return;                 /* the page's checks keep it in range */
    }
    const unsigned a = addr - INA3221_ADDR_MIN;
    const uint16_t ch = c[LINK_SN_I3221_CHANNELS];
    for (unsigned k = 0; k < INA3221_CHANNELS; ++k) {
        if ((ch & (1u << k)) != 0u) {
            m[LINK_SM_SHUNT(SENSE_CH(a, k + 1u))] =
                c[LINK_SN_I3221_SHUNT_DMOHM];
        }
    }
    if ((c[LINK_SN_ENABLE] & LINK_SN_EN_I3221) != 0u) {
        m[LINK_SM_ENABLE] = (uint16_t)(1u << a);
        if ((ch & 1u) != 0u) {
            m[LINK_SM_FAST] = (uint16_t)SENSE_CH(a, 1u);
        }
    }
}

/* The kept registers in force, or derived into @p buf. */
static const uint16_t *kept_regs(const sense_page_t *p, uint16_t *buf)
{
    if (p->map_set) {
        return p->map;
    }
    derive(p, buf);
    return buf;
}

static bool watch_ok(uint16_t w)
{
    return w == LINK_SM_WATCH_NONE
           || (w >= LINK_SM_WATCH_OUT(0u) && w <= LINK_SM_WATCH_OUT(7u))
           || (w >= LINK_SM_WATCH_PORT(0u)
               && w < LINK_SM_WATCH_PORT(LINK_PG_PORTS_MAX))
           || w == LINK_SM_WATCH_SUPPLY;
}

bool sense_map_on_addr(const uint16_t *map, uint16_t addr)
{
    return addr >= INA3221_ADDR_MIN && addr <= INA3221_ADDR_MAX
           && fitted(map, addr - INA3221_ADDR_MIN);
}

/* The map's own rules, whatever the bus. */
static bool map_ok(const uint16_t *m, const uint16_t *sense)
{
    if (m[LINK_SM_ENABLE] > LINK_SM_FIT_ALL) {
        return false;
    }
    for (unsigned c = 0; c < LINK_SM_CHANNELS; ++c) {
        const uint16_t r = m[LINK_SM_SHUNT(c)];
        if (r != 0u && (r < LINK_SM_DMOHM_MIN || r > LINK_SM_DMOHM_MAX)) {
            return false;
        }
        if (!watch_ok(m[LINK_SM_WATCH(c)])) {
            return false;
        }
    }
    for (unsigned a = 0; a < LINK_SM_PARTS; ++a) {
        if (fitted(m, a) && m[LINK_SM_SHUNT(SENSE_CH(a, 1u))] == 0u
            && m[LINK_SM_SHUNT(SENSE_CH(a, 2u))] == 0u
            && m[LINK_SM_SHUNT(SENSE_CH(a, 3u))] == 0u) {
            return false;       /* a part with no channel */
        }
    }
    const uint16_t f = m[LINK_SM_FAST];
    if (f != LINK_SM_FAST_NONE
        && (f >= LINK_SM_CHANNELS || !fitted(m, f / INA3221_CHANNELS)
            || m[LINK_SM_SHUNT(f)] == 0u)) {
        return false;
    }
    return (sense[LINK_SN_ENABLE] & LINK_SN_EN_I228) == 0u
           || !sense_map_on_addr(m, sense[LINK_SN_I228_ADDR]);
}

/* SENSE's set-up with the map mirrored into ENABLE bit 1 and registers 8
 * to 10.  With no part fitted only the bit clears. */
static void mirror(const uint16_t *m, const uint16_t *sense, uint16_t *next)
{
    memcpy(next, sense, LINK_SN_CONFIG_COUNT * sizeof(uint16_t));
    next[LINK_SN_ENABLE] &= (uint16_t)~LINK_SN_EN_I3221;
    if (m[LINK_SM_ENABLE] == 0u) {
        return;
    }
    unsigned a = 0u;
    if (m[LINK_SM_FAST] != LINK_SM_FAST_NONE) {
        a = m[LINK_SM_FAST] / INA3221_CHANNELS;
    } else {
        while (!fitted(m, a)) {
            ++a;
        }
    }
    uint16_t ch = 0u;
    uint16_t shunt = 0u;
    for (unsigned k = INA3221_CHANNELS; k-- > 0u;) {
        const uint16_t r = m[LINK_SM_SHUNT(SENSE_CH(a, k + 1u))];
        if (r != 0u) {
            ch |= (uint16_t)(1u << k);
            shunt = r;          /* the lowest channel's last: CH1 first */
        }
    }
    next[LINK_SN_ENABLE] |= LINK_SN_EN_I3221;
    next[LINK_SN_I3221_ADDR]         = (uint16_t)(INA3221_ADDR_MIN + a);
    next[LINK_SN_I3221_SHUNT_DMOHM]  = shunt;
    next[LINK_SN_I3221_CHANNELS]     = ch;
}

/* Whether two maps differ in what core 1 reads: ENABLE, FAST, a shunt. */
static bool bus_differs(const uint16_t *a, const uint16_t *b)
{
    return memcmp(a, b, (size_t)LINK_SM_WATCH_0 * sizeof(uint16_t)) != 0;
}

void sense_map_read(const sense_page_t *p, uint8_t off, uint8_t n,
                    uint16_t *out)
{
    if (p == NULL || out == NULL
        || (unsigned)off + (unsigned)n > (unsigned)LINK_SM_COUNT) {
        return;
    }
    uint16_t buf[LINK_SM_KEPT];
    const uint16_t *k = kept_regs(p, buf);
    for (uint8_t i = 0; i < n; ++i) {
        const unsigned r = (unsigned)off + i;
        uint16_t v = (r < LINK_SM_KEPT) ? k[r] : p->map[r];
        if (r == (unsigned)LINK_SM_FLAGS) {
            v |= p->map_set ? LINK_SM_IN_FORCE : LINK_SM_DERIVED;
        }
        out[i] = v;
    }
}

uint8_t sense_map_write(sense_page_t *p, uint8_t off, uint8_t n,
                        const uint16_t *in, const outputs_t *o,
                        uint64_t taken)
{
    if (p == NULL || in == NULL || o == NULL
        || (unsigned)off + (unsigned)n > (unsigned)LINK_SM_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    if (n == 0u) {
        return 0u;
    }
    if ((unsigned)off + (unsigned)n > (unsigned)LINK_SM_KEPT) {
        return LINK_NACK_READ_ONLY;
    }
    uint16_t was[LINK_SM_KEPT];
    memcpy(was, kept_regs(p, was), sizeof(was));
    uint16_t next[LINK_SM_KEPT];
    memcpy(next, was, sizeof(next));
    for (uint8_t i = 0; i < n; ++i) {
        next[off + i] = in[i];
    }
    const bool same = memcmp(next, was, sizeof(next)) == 0;
    if (same && p->map_set) {
        return 0u;              /* the map in force: nothing moves */
    }
    /* A change is not taken mid-run: the bus set up again is readings
     * missing from it.  The derived map written as it reads changes
     * nothing on the bus and is taken whatever the bank does. */
    if (!same && outputs_driving(o)) {
        return LINK_NACK_BAD_VALUE;
    }
    if (!map_ok(next, p->sense)) {
        return LINK_NACK_BAD_VALUE;
    }
    uint16_t cfg[LINK_SN_CONFIG_COUNT];
    mirror(next, p->sense, cfg);
    const bool sense_moves =
        memcmp(cfg, p->sense, sizeof(cfg)) != 0;
    if (sense_moves) {
        const uint8_t nack = sense_cfg_check(p, cfg, o, taken);
        if (nack != 0u) {
            return nack;
        }
    }
    const bool new_bus = sense_moves || bus_differs(next, was);
    memcpy(p->map, next, sizeof(next));
    p->map_set = true;
    if (new_bus) {
        sense_cfg_take(p, cfg);
    }
    return 0u;
}

void sense_xwin_read(const sense_page_t *p, unsigned page, uint8_t off,
                     uint8_t n, uint16_t *out)
{
    if (p == NULL || out == NULL || page > 1u
        || (unsigned)off + (unsigned)n > (unsigned)LINK_SX_COUNT) {
        return;
    }
    memcpy(out, &p->xwin[page][off], (size_t)n * sizeof(uint16_t));
}

uint16_t sense_map_shunt(const sense_page_t *p, unsigned c)
{
    uint16_t buf[LINK_SM_KEPT];
    return kept_regs(p, buf)[LINK_SM_SHUNT(c)];
}

bool sense_map_watches(const sense_page_t *p)
{
    for (unsigned c = 0; c < LINK_SM_CHANNELS; ++c) {
        const uint16_t w = p->map[LINK_SM_WATCH(c)];
        if (fitted(p->map, c / INA3221_CHANNELS)
            && p->map[LINK_SM_SHUNT(c)] != 0u
            && w >= LINK_SM_WATCH_OUT(0u)
            && w < LINK_SM_WATCH_PORT(LINK_PG_PORTS_MAX)) {
            return true;
        }
    }
    return false;
}

void sense_map_kept(const sense_page_t *p, uint16_t *kept, uint16_t *set)
{
    if (p == NULL || kept == NULL || set == NULL) {
        return;
    }
    memcpy(kept, kept_regs(p, kept), LINK_SM_KEPT * sizeof(uint16_t));
    *set = p->map_set ? 1u : 0u;
}

uint8_t sense_map_restore(sense_page_t *p, const uint16_t *kept,
                          uint16_t set, const outputs_t *o, uint64_t taken)
{
    if (set == 0u) {
        return 0u;
    }
    return sense_map_write(p, 0u, (uint8_t)LINK_SM_KEPT, kept, o, taken);
}

void sense_map_cfg(const uint16_t *map, sense_i3221_cfg_t *out)
{
    memset(out, 0, sizeof(*out));
    out->fitted = (uint8_t)(map[LINK_SM_ENABLE] & LINK_SM_FIT_ALL);
    for (unsigned c = 0; c < SENSE_CHANNELS; ++c) {
        out->shunt_uohm[c] = (uint32_t)map[LINK_SM_SHUNT(c)] * 100u;
    }
    out->fast = (map[LINK_SM_FAST] < SENSE_CHANNELS)
                    ? (uint8_t)map[LINK_SM_FAST] : SENSE_FAST_NONE;
}

/* ------------------------------------------------------------ publish */

static uint16_t entry_flags(const sense_window_t *w)
{
    uint16_t f = 0u;
    if (w->n_i > 0u) {
        f |= LINK_SX_E_CURRENT;
    }
    if (w->n_v > 0u) {
        f |= LINK_SX_E_VOLTAGE;
    }
    if (w->clip_hi) {
        f |= LINK_SX_E_CLIP_HI;
    }
    if (w->clip_lo) {
        f |= LINK_SX_E_CLIP_LO;
    }
    return f;
}

void sense_map_publish(sense_page_t *p, const sense_snap_t *s)
{
    uint16_t online = 0u;
    uint16_t flags = 0u;
    for (unsigned a = 0; a < LINK_SM_PARTS; ++a) {
        if (s->i3221[a] == SENSE_PART_ONLINE) {
            online |= (uint16_t)LINK_SM_ONLINE(a);
        }
        if (s->i3221[a] == SENSE_PART_WRONG_ID) {
            flags |= (uint16_t)LINK_SM_ID_WRONG(a);
        }
        p->map[LINK_SM_ID(a)] = s->i3221_die[a];
    }
    p->map[LINK_SM_FLAGS] = (uint16_t)(online | flags);
    for (unsigned page = 0; page < 2u; ++page) {
        uint16_t *r = p->xwin[page];
        memset(r, 0, LINK_SX_COUNT * sizeof(uint16_t));
        r[LINK_SX_FLAGS] = online;
        if (!s->have_win) {
            continue;
        }
        r[LINK_SX_WINDOW] = s->win[0].number;
        r[LINK_SX_FLAGS] |= LINK_SX_HAVE;
        for (unsigned k = 0; k < LINK_SX_PER_PAGE; ++k) {
            const sense_window_t *w = &s->win[page * LINK_SX_PER_PAGE + k];
            r[LINK_SX_E(k, LINK_SX_E_MEAN_MA)] =
                reg_i16(div_round(w->i_mean_ua, 1000));
            r[LINK_SX_E(k, LINK_SX_E_MAX_MA)] =
                reg_i16(div_round(w->i_max_ua, 1000));
            r[LINK_SX_E(k, LINK_SX_E_MEAN_MV)] =
                reg_u16(div_round(w->v_mean_uv, 1000));
            r[LINK_SX_E(k, LINK_SX_E_MIN_MV)] =
                reg_u16(div_round(w->v_min_uv, 1000));
            r[LINK_SX_E(k, LINK_SX_E_FLAGS)] = entry_flags(w);
        }
    }
}
