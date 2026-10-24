/**
 * @file
 * @brief SETUP > SENSORS: the INA3221 parts' rows of the schema, their
 *        migration from the one INA3221 of 0.21, and the map they make.  See
 *        settings_sensors.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_sensors.h"
#include "settings_opts.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/** @brief A WATCHES row's options: list watch of ui_text.def. */
#define WATCH_OPTS                                                         \
    .options = settings_opts_watch, .option_count = (uint8_t)SENS_W_COUNT

/* The rows' index in k_rows: an id less the first row's. */
#define AT(id) ((int)(id) - (int)SET_INA_FIT_0)
#define N_ROWS (SET_INA_CH_LAST - SET_INA_FIT_0 + 1)

#define FIT_ROW(a)                                                         \
    [AT(SET_INA_FIT(a))] = {                                               \
        "ina_fit_" #a, "",                                                 \
        SET_CAT_SENSORS, SET_TYPE_BOOL, 0, 1, 1, 0, NULL, 0, 0.0f }

/*
 * A channel's rows: WATCHES, SHUNT and LIMIT.  The shunt's range is the
 * page's, 5.0 to 1000.0mOhm, and 0 switches the channel off; 100.0mOhm
 * is the DAOKAI module's 0.1Ohm, which reads to 1.638A.  0x40 CH1
 * watches OUT 1 by default (owner decision 1, 2026-10-20).  The limit's
 * range is SENSE_LIMIT's, 10 to 32767mA, in 10mA steps on the arrows,
 * 0 (OFF) for none.
 */
#define CH_ROWS(a, ch, watch_def)                                          \
    [AT(SET_INA_W(3 * (a) + (ch) - 1))] = {                                \
        "ina_w_" #a #ch, "",                                               \
        SET_CAT_SENSORS, SET_TYPE_ENUM, 0, 0, 1, (watch_def), WATCH_OPTS,  \
        0.0f },                                                            \
    [AT(SET_INA_SH(3 * (a) + (ch) - 1))] = {                               \
        "ina_sh_" #a #ch, "mΩ",                                            \
        SET_CAT_SENSORS, SET_TYPE_FLOAT, 0.0f, 1000.0f, 0.1f, 100.0f,      \
        NULL, 0, 5.0f },                                                   \
    [AT(SET_INA_LIM(3 * (a) + (ch) - 1))] = {                              \
        "ina_lim_" #a #ch, "mA",                                           \
        SET_CAT_SENSORS, SET_TYPE_INT, 0, 32767, 10, 0, NULL, 0, 10.0f }

#define PART_ROWS(a, w1)                                                   \
    CH_ROWS(a, 1, w1), CH_ROWS(a, 2, SENS_W_NONE), CH_ROWS(a, 3, SENS_W_NONE)

static const setting_def_t k_rows[N_ROWS] = {
    FIT_ROW(0), FIT_ROW(1), FIT_ROW(2), FIT_ROW(3),
    PART_ROWS(0, SENS_W_OUT_1),
    PART_ROWS(1, SENS_W_NONE),
    PART_ROWS(2, SENS_W_NONE),
    PART_ROWS(3, SENS_W_NONE),
};

/*
 * The one INA3221 of 0.21, kept one release for migration and for a panel
 * taken back to it: in no category (SET_CAT_COUNT), so on no screen and in
 * no category's reset, and loaded and saved as stored.
 */
static const setting_def_t k_kept[4] = {
    {                                   /* SET_INA3221_EN */
        "ina3221_en", "",
        SET_CAT_COUNT, SET_TYPE_BOOL, 0, 1, 1, 0, NULL, 0, 0.0f },
    {                                   /* SET_INA3221_ADDR */
        "ina3221_adr", "",
        SET_CAT_COUNT, SET_TYPE_ENUM, 0, 0, 1, 0, settings_opts_old_addr, 4, 0.0f },
    {                                   /* SET_INA3221_MOHM */
        "ina3221_mohm", "mΩ",
        SET_CAT_COUNT, SET_TYPE_FLOAT, 5.0f, 1000.0f, 0.1f, 100.0f, NULL, 0, 0.0f },
    {                                   /* SET_INA3221_CH */
        "ina3221_ch", "",
        SET_CAT_COUNT, SET_TYPE_ENUM, 0, 0, 1, 0, settings_opts_old_ch, 2, 0.0f },
};

_Static_assert(SET_INA3221_CH - SET_INA3221_EN == 3,
               "the four kept keys are in a row");
_Static_assert(LINK_SL_MA_MAX == 32767u, "LIMIT's top is the page's");
_Static_assert(SET_INA_CH_ROWS == 3,
               "a channel's rows: WATCHES, SHUNT, LIMIT");
_Static_assert(SET_INA_W(5) == SET_INA_W_13 && SET_INA_SH(6) == SET_INA_SH_21
               && SET_INA_SH(11) == SET_INA_SH_33
               && SET_INA_LIM(0) == SET_INA_LIM_01
               && SET_INA_LIM(11) == SET_INA_LIM_33,
               "the rows' numbers are the enum's names");
_Static_assert(N_ROWS == SET_INA_PARTS + SET_INA_CHANNELS * SET_INA_CH_ROWS,
               "every SENSORS row has its schema row");

bool settings_sensors_row(setting_id_t id)
{
    return id >= SET_INA_FIT_0 && id <= SET_INA_CH_LAST;
}

const setting_def_t *settings_sensors_def(setting_id_t id)
{
    if (id >= SET_INA3221_EN && id <= SET_INA3221_CH) {
        return &k_kept[id - SET_INA3221_EN];
    }
    return settings_sensors_row(id) ? &k_rows[AT(id)] : NULL;
}

int settings_sensors_part(setting_id_t id)
{
    if (!settings_sensors_row(id)) {
        return -1;
    }
    if (id <= SET_INA_FIT_LAST) {
        return (int)id - (int)SET_INA_FIT_0;
    }
    return ((int)id - (int)SET_INA_CH_0) / SET_INA_CH_ROWS / 3;
}

uint16_t settings_sensors_watch_code(int option)
{
    if (option >= SENS_W_OUT_1 && option < SENS_W_PORT_1) {
        return (uint16_t)LINK_SM_WATCH_OUT(option - SENS_W_OUT_1);
    }
    if (option >= SENS_W_PORT_1 && option < SENS_W_SUPPLY) {
        return (uint16_t)LINK_SM_WATCH_PORT(option - SENS_W_PORT_1);
    }
    return (option == SENS_W_SUPPLY) ? (uint16_t)LINK_SM_WATCH_SUPPLY
                                     : (uint16_t)LINK_SM_WATCH_NONE;
}

void settings_sensors_unset(float *values)
{
    for (int i = SET_INA_FIT_0; i <= SET_INA_CH_LAST; ++i) {
        values[i] = NAN;
    }
}

/* Value @p i seeded with @p v where the store had none.  Whether it was. */
static int seed(float *values, setting_id_t id, float v)
{
    if (!isnan(values[id])) {
        return 0;
    }
    values[id] = v;
    return 1;
}

int settings_sensors_migrate(float *values)
{
    const bool  on     = values[SET_INA3221_EN] != 0.0f;
    const int   old    = (int)lrintf(values[SET_INA3221_ADDR]);
    const float mohm   = values[SET_INA3221_MOHM];
    const bool  all    = lrintf(values[SET_INA3221_CH]) != 0;
    const int   target = (int)lrintf(values[SET_SERVO_TARGET]);
    /* The part CH1 of which watches the tester's target: the old part,
     * or 0x40 while it was off. */
    const int   part   = (on && old >= 0 && old < SET_INA_PARTS) ? old : 0;
    const int   w1     = (target >= 1 && target <= 4)
                             ? SENS_W_PORT_1 + target - 1 : SENS_W_OUT_1;
    int n = 0;
    for (int a = 0; a < SET_INA_PARTS; ++a) {
        const bool mine = on && a == part;
        n += seed(values, SET_INA_FIT(a), mine ? 1.0f : 0.0f);
        for (int ch = 0; ch < 3; ++ch) {
            const int c = 3 * a + ch;
            const float sh = !mine ? k_rows[AT(SET_INA_SH(c))].def
                             : (ch == 0 || all) ? mohm : 0.0f;
            n += seed(values, SET_INA_SH(c), sh);
            n += seed(values, SET_INA_W(c),
                      (a == part && ch == 0) ? (float)w1
                                             : (float)SENS_W_NONE);
            n += seed(values, SET_INA_LIM(c), 0.0f);
        }
    }
    return n;
}

int settings_sensors_watch_option(uint16_t code)
{
    for (int k = 0; k < SENS_W_COUNT; ++k) {
        if (settings_sensors_watch_code(k) == code) {
            return k;
        }
    }
    return SENS_W_NONE;
}

uint16_t settings_sensors_limit_ma(int c)
{
    if (c < 0 || c >= SET_INA_CHANNELS) {
        return 0u;
    }
    /* The row's range is the page's, 0 to 32767mA (LINK_SL_MA_MAX). */
    const long ma = lrintf(settings_get(SET_INA_LIM(c)));
    return (ma <= 0) ? 0u : (uint16_t)ma;
}

uint16_t settings_sensors_dmohm(int c)
{
    if (c < 0 || c >= SET_INA_CHANNELS) {
        return 0u;
    }
    return (uint16_t)lrintf(settings_get(SET_INA_SH(c)) * 10.0f);
}

uint8_t settings_sensors_fast(void)
{
    settings_sensors_map_t m;
    settings_sensors_map(&m);
    return m.fast;
}

uint8_t settings_sensors_fitted(void)
{
    uint8_t parts = 0u;
    for (int a = 0; a < SET_INA_PARTS; ++a) {
        if (settings_get_bool(SET_INA_FIT(a))) {
            parts = (uint8_t)(parts | (1u << a));
        }
    }
    return parts;
}

uint16_t settings_sensors_meter_dmohm(void)
{
    settings_sensors_map_t m;
    settings_sensors_map(&m);
    if (m.fast != LINK_SM_FAST_NONE) {
        return m.regs[LINK_SM_SHUNT(m.fast)];
    }
    return m.one_on ? m.one_dmohm : 0u;
}

/* Whether WATCH value @p code is the servo tester's target @p target:
 * any output for OUTPUTS (0), programming port n for n. */
static bool watches_target(uint16_t code, int target)
{
    if (target >= 1 && target <= (int)LINK_SM_PARTS) {
        return code == (uint16_t)LINK_SM_WATCH_PORT(target - 1);
    }
    return code >= (uint16_t)LINK_SM_WATCH_OUT(0)
           && code <= (uint16_t)LINK_SM_WATCH_OUT(7);
}

void settings_sensors_map(settings_sensors_map_t *m)
{
    if (m == NULL) {
        return;
    }
    memset(m, 0, sizeof(*m));
    m->fast   = (uint8_t)LINK_SM_FAST_NONE;
    m->mirror = 0xFFu;
    const int target = settings_get_int(SET_SERVO_TARGET);
    for (int a = 0; a < SET_INA_PARTS; ++a) {
        const bool fitted = settings_get_bool(SET_INA_FIT(a));
        bool lit = false;
        for (int ch = 0; ch < 3; ++ch) {
            const int c = 3 * a + ch;
            const uint16_t dmohm = settings_sensors_dmohm(c);
            const uint16_t code =
                settings_sensors_watch_code(settings_get_int(SET_INA_W(c)));
            m->regs[LINK_SM_SHUNT(c)] = dmohm;
            m->regs[LINK_SM_WATCH(c)] = code;
            lit = lit || dmohm != 0u;
            if (fitted && dmohm != 0u && m->fast == LINK_SM_FAST_NONE
                && watches_target(code, target)) {
                m->fast = (uint8_t)c;
            }
        }
        if (fitted) {
            m->parts |= (uint8_t)(1u << a);
            if (!lit) {
                m->dark |= (uint8_t)(1u << a);
            }
        }
    }
    m->regs[LINK_SM_ENABLE] = m->parts;
    m->regs[LINK_SM_FAST]   = m->fast;
    if (m->fast != LINK_SM_FAST_NONE) {
        m->mirror = (uint8_t)(m->fast / 3u);
    } else {
        for (int a = 0; a < SET_INA_PARTS && m->mirror == 0xFFu; ++a) {
            if ((m->parts & (1u << a)) != 0u) {
                m->mirror = (uint8_t)a;
            }
        }
    }
    /* The one-part frame: the mirror part with a channel on, else values
     * the page takes.  The frame carries one shunt for every channel it
     * reads, so it reads the channels on the first nonzero shunt only. */
    m->one_addr  = (uint8_t)LINK_SN_I3221_ADDR_MIN;
    m->one_dmohm = 1000u;
    m->one_ch    = 0x01u;
    if (m->mirror != 0xFFu) {
        uint16_t dmohm = 0u;
        uint8_t  ch = 0u;
        for (int k = 0; k < 3; ++k) {
            const uint16_t d = m->regs[LINK_SM_SHUNT(3 * m->mirror + k)];
            dmohm = (dmohm == 0u) ? d : dmohm;
            if (d != 0u && d == dmohm) {
                ch |= (uint8_t)(1u << k);
            }
        }
        if (ch != 0u) {
            m->one_on    = true;
            m->one_addr  = (uint8_t)(LINK_SN_I3221_ADDR_MIN + m->mirror);
            m->one_dmohm = dmohm;
            m->one_ch    = ch;
        }
    }
}
