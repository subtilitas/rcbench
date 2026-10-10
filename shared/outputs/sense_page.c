/*
 * The SENSE, SERVO_SENSE and SERVO_WIN link pages.  See sense_page.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_page.h"

#include <stddef.h>
#include <string.h>

#include "as5600.h"
#include "ina228.h"
#include "ina3221.h"
#include "link_msg.h"

/* The INA3221's shunt full scale, 163.8 mV, in uV: 4095 steps of 40 uV. */
#define I3221_FULL_SCALE_UV 163800uL

/* The largest current a register of signed mA carries. */
#define MA_MAX 32767u

_Static_assert(LINK_SW_RING == SENSE_WIN_RING,
               "SERVO_WIN carries the schedule's ring");
_Static_assert(LINK_SW_ENTRIES + LINK_SW_RING * LINK_SW_E_STRIDE
               == LINK_SW_COUNT, "SERVO_WIN is its header and its entries");

void sense_page_defaults(uint16_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, LINK_SN_CONFIG_COUNT * sizeof(uint16_t));
    cfg[LINK_SN_SDA_PIN]           = SENSE_DEFAULT_SDA;
    cfg[LINK_SN_SCL_PIN]           = SENSE_DEFAULT_SCL;
    cfg[LINK_SN_KHZ]               = SENSE_DEFAULT_KHZ;
    cfg[LINK_SN_I228_ADDR]         = SENSE_DEFAULT_I228_ADDR;
    cfg[LINK_SN_I228_SHUNT_UOHM]   = SENSE_DEFAULT_I228_UOHM;
    cfg[LINK_SN_I228_MAX_DA]       = SENSE_DEFAULT_I228_DA;
    cfg[LINK_SN_I3221_ADDR]        = SENSE_DEFAULT_I3221_ADDR;
    cfg[LINK_SN_I3221_SHUNT_DMOHM] = SENSE_DEFAULT_I3221_DMOHM;
    cfg[LINK_SN_I3221_CHANNELS]    = SENSE_DEFAULT_I3221_CH;
}

void sense_page_init(sense_page_t *p)
{
    if (p != NULL) {
        memset(p, 0, sizeof(*p));
        sense_page_defaults(p->sense);
        p->servo[LINK_SS_CAP_MOVE_MA] = SENSE_DEFAULT_MOVE_MA;
        p->servo[LINK_SS_CAP_BAND_MA] = SENSE_DEFAULT_BAND_MA;
    }
}

/* Whether the driver can calibrate the INA228 for this shunt and maximum:
 * one rule, the driver's, so the page takes no set-up the part refuses. */
static bool i228_calibrates(uint16_t shunt_uohm, uint16_t max_da)
{
    ina228_cal_t cal;
    return ina228_calibrate(shunt_uohm, (uint32_t)max_da * 100u, &cal)
           == INA228_SETUP_OK;
}

uint32_t sense_i3221_full_scale_ma(uint16_t shunt_dmohm)
{
    if (shunt_dmohm == 0u) {
        return 0u;
    }
    /* uV / (0.1 mOhm) = 10 mA a step, so uV * 10 / dmOhm is mA. */
    return (I3221_FULL_SCALE_UV * 10u) / shunt_dmohm;
}

bool sense_page_enabled(const sense_page_t *p)
{
    return p != NULL
           && (p->sense[LINK_SN_ENABLE] & LINK_SN_EN_ALL) != 0u;
}

uint8_t sense_page_sda(const sense_page_t *p)
{
    return (p != NULL) ? (uint8_t)p->sense[LINK_SN_SDA_PIN] : 0u;
}

uint8_t sense_page_scl(const sense_page_t *p)
{
    return (p != NULL) ? (uint8_t)p->sense[LINK_SN_SCL_PIN] : 0u;
}

uint32_t sense_page_hz(const sense_page_t *p)
{
    return (p != NULL) ? (uint32_t)p->sense[LINK_SN_KHZ] * 1000u : 0u;
}

uint64_t sense_page_pins(const sense_page_t *p)
{
    if (!sense_page_enabled(p)) {
        return 0u;
    }
    return ((uint64_t)1u << p->sense[LINK_SN_SDA_PIN])
           | ((uint64_t)1u << p->sense[LINK_SN_SCL_PIN]);
}

uint64_t sense_page_held(const sense_page_t *p)
{
    return (p != NULL) ? (sense_page_pins(p) | p->held) : 0u;
}

/* Whether @p pin may carry the bus: in the bank, not another page's, not
 * reserved unless it is one this page already holds, and no output's. */
static bool pin_free(const sense_page_t *p, const outputs_t *o,
                     uint16_t pin, uint64_t taken)
{
    if (pin > OUT_MAX_PIN) {
        return false;
    }
    const uint64_t bit = (uint64_t)1u << pin;
    if ((taken & bit) != 0u) {
        return false;
    }
    const bool ours = (sense_page_pins(p) & bit) != 0u;
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

/* SDA one I2C block's data pin and SCL its clock: the I2C function sits at
 * pin mod 4, 0 and 2 the data lines, each with its clock on the pin after. */
static bool one_block(uint16_t sda, uint16_t scl)
{
    return (sda % 2u) == 0u && scl == (uint16_t)(sda + 1u);
}

/* The values themselves, whatever is enabled: a set-up kept in flash is
 * one a later enable can take without another write. */
static bool values_ok(const uint16_t *c)
{
    if (c[LINK_SN_ENABLE] > LINK_SN_EN_ALL
        || c[LINK_SN_KHZ] != LINK_SN_KHZ_BUS
        || c[LINK_SN_RESERVED_7] != 0u || c[LINK_SN_RESERVED_11] != 0u) {
        return false;
    }
    if (c[LINK_SN_I228_ADDR] < LINK_SN_I228_ADDR_MIN
        || c[LINK_SN_I228_ADDR] > LINK_SN_I228_ADDR_MAX
        || c[LINK_SN_I228_SHUNT_UOHM] < LINK_SN_I228_UOHM_MIN
        || c[LINK_SN_I228_SHUNT_UOHM] > LINK_SN_I228_UOHM_MAX
        || c[LINK_SN_I228_MAX_DA] < LINK_SN_I228_DA_MIN
        || c[LINK_SN_I228_MAX_DA] > LINK_SN_I228_DA_MAX
        || !i228_calibrates(c[LINK_SN_I228_SHUNT_UOHM],
                            c[LINK_SN_I228_MAX_DA])) {
        return false;
    }
    if (c[LINK_SN_I3221_ADDR] < LINK_SN_I3221_ADDR_MIN
        || c[LINK_SN_I3221_ADDR] > LINK_SN_I3221_ADDR_MAX
        || c[LINK_SN_I3221_SHUNT_DMOHM] < LINK_SN_I3221_DMOHM_MIN
        || c[LINK_SN_I3221_SHUNT_DMOHM] > LINK_SN_I3221_DMOHM_MAX
        || c[LINK_SN_I3221_CHANNELS] > LINK_SN_I3221_CH_ALL) {
        return false;
    }
    return true;
}

/*
 * A new set-up: everything read under the old one is gone -- FLAGS,
 * PRESENT, the IDs, ERRORS, the readings, the windows and a finished
 * capture's result -- so no read shows a part online, or a value scaled by
 * the old shunt, before core 1 has read under the new one.  The ESC's own
 * telemetry (registers 23 to 25) is not the bus's and stays; the output
 * encoder's (26 to 30) and RESETS (31) go with the rest, and so does the
 * window ring.  CAP_SEQ
 * counts on across set-ups.  A capture under way cannot meet this: a
 * set-up is refused while the bank drives, and a stopped bank ends it.
 */
static void forget_readings(sense_page_t *p)
{
    memset(&p->sense[LINK_SN_FLAGS], 0,
           (size_t)(LINK_SN_ESC_VOLTAGE_CV - LINK_SN_FLAGS) * sizeof(uint16_t));
    memset(&p->sense[LINK_SN_AS5600_FLAGS], 0,
           (size_t)(LINK_SN_COUNT - LINK_SN_AS5600_FLAGS) * sizeof(uint16_t));
    memset(&p->servo[LINK_SS_CH_MEAN_MA], 0,
           (size_t)(LINK_SS_CH_FLAGS + 1) * sizeof(uint16_t));
    memset(p->win, 0, sizeof(p->win));
    p->servo[LINK_SS_CAP_ARM]      = 0u;
    p->servo[LINK_SS_CAP_STATE]    = (uint16_t)LINK_CAP_IDLE;
    p->servo[LINK_SS_CAP_MOVE_T]   = 0u;
    p->servo[LINK_SS_CAP_ARRIVE_T] = 0u;
    p->servo[LINK_SS_CAP_PEAK_MA]  = 0u;
    p->servo[LINK_SS_CAP_MEAN_MA]  = 0u;
    p->servo[LINK_SS_CAP_SAMPLES]  = 0u;
}

uint8_t sense_page_write(sense_page_t *p, uint8_t off, uint8_t n,
                         const uint16_t *in, const outputs_t *o,
                         uint64_t taken)
{
    if (p == NULL || in == NULL || o == NULL) {
        return LINK_NACK_BAD_RANGE;
    }
    if ((unsigned)off + (unsigned)n > (unsigned)LINK_SN_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    if (n == 0u) {
        return 0u;
    }
    if ((unsigned)off + (unsigned)n > LINK_SN_CONFIG_COUNT) {
        return LINK_NACK_READ_ONLY;
    }
    uint16_t next[LINK_SN_CONFIG_COUNT];
    memcpy(next, p->sense, sizeof(next));
    for (uint8_t i = 0; i < n; ++i) {
        next[off + i] = in[i];
    }
    if (memcmp(next, p->sense, sizeof(next)) == 0) {
        return 0u;                  /* the set-up in force: nothing moves */
    }
    /* Not mid-run: the bus opened again is readings missing from it. */
    if (outputs_driving(o)) {
        return LINK_NACK_BAD_VALUE;
    }
    if (!values_ok(next)) {
        return LINK_NACK_BAD_VALUE;
    }
    const uint16_t en = next[LINK_SN_ENABLE];
    if ((en & LINK_SN_EN_I3221) != 0u && next[LINK_SN_I3221_CHANNELS] == 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    if ((en & LINK_SN_EN_I228) != 0u && (en & LINK_SN_EN_I3221) != 0u
        && next[LINK_SN_I228_ADDR] == next[LINK_SN_I3221_ADDR]) {
        return LINK_NACK_BAD_VALUE;
    }
    if (en != 0u
        && (!one_block(next[LINK_SN_SDA_PIN], next[LINK_SN_SCL_PIN])
            || !pin_free(p, o, next[LINK_SN_SDA_PIN], taken)
            || !pin_free(p, o, next[LINK_SN_SCL_PIN], taken))) {
        return LINK_NACK_BAD_VALUE;
    }
    memcpy(p->sense, next, sizeof(next));
    ++p->cfg_gen;
    forget_readings(p);
    return 0u;
}

void sense_page_read(const sense_page_t *p, uint8_t off, uint8_t n,
                     uint16_t *out)
{
    if (p == NULL || out == NULL
        || (unsigned)off + (unsigned)n > (unsigned)LINK_SN_COUNT) {
        return;
    }
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = p->sense[off + i];
    }
}

/* Whether output channel @p ch is a surface a PWM slot renders: the one
 * kind of output whose pulse edge a capture can be timed from. */
static bool pwm_surface(const sense_page_t *p, const outputs_t *o, uint8_t ch)
{
    if (o->channel[ch].role != OUT_ROLE_SURFACE) {
        return false;
    }
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        /* A slot the bank holds and the silicon could not bind -- a
         * compare register another pin has, GP0 beside GP16 -- renders no
         * frame to time from. */
        if (o->slot[i].driver == OUT_DRIVER_PWM
            && o->slot[i].first_channel == ch
            && (p->bound & (1u << i)) != 0u) {
            return true;
        }
    }
    return false;
}

void sense_page_bound(sense_page_t *p, uint8_t slots)
{
    if (p != NULL) {
        p->bound = slots;
    }
}

/* A capture's frame, CAP_ARM to CAP_BAND_MA, judged as an arm. */
static uint8_t arm_check(const sense_page_t *p, const uint16_t *f,
                         const outputs_t *o)
{
    const uint16_t arm = f[0];
    const uint8_t ch = LINK_SS_ARM_CH(arm);
    if ((arm & LINK_SS_ARM) == 0u
        || (arm & (uint16_t)~LINK_SS_ARM_BITS) != 0u || ch == 0u
        || f[2] == 0u || f[2] > MA_MAX
        || f[3] == 0u || f[3] > MA_MAX) {
        return LINK_NACK_BAD_VALUE;
    }
    /* CH1 only: the schedule samples CH1 fast enough to time a move, and
     * CH2 and CH3 at 50 Hz. */
    if (ch != LINK_SS_CAP_CH) {
        return LINK_NACK_BAD_VALUE;
    }
    const uint16_t *c = p->sense;
    if ((c[LINK_SN_ENABLE] & LINK_SN_EN_I3221) == 0u
        || (c[LINK_SN_I3221_CHANNELS] & (1u << (ch - 1u))) == 0u
        || !pwm_surface(p, o, LINK_SS_ARM_OUT(arm))) {
        return LINK_NACK_BAD_VALUE;
    }
    if (!outputs_driving(o)) {
        return LINK_NACK_NOT_ARMED;
    }
    return 0u;
}

uint8_t sense_servo_write(sense_page_t *p, uint8_t off, uint8_t n,
                          const uint16_t *in, const outputs_t *o)
{
    if (p == NULL || in == NULL || o == NULL) {
        return LINK_NACK_BAD_RANGE;
    }
    if ((unsigned)off + (unsigned)n > (unsigned)LINK_SS_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    if (n == 0u) {
        return 0u;
    }
    if (off < (uint8_t)LINK_SS_CAP_ARM
        || (unsigned)off + (unsigned)n > (unsigned)LINK_SS_CAP_STATE) {
        return LINK_NACK_READ_ONLY;
    }
    /* A disarm, alone or at the head of the frame: never refused, and the
     * registers beside it are not taken -- they belong to the next arm. */
    if (off == (uint8_t)LINK_SS_CAP_ARM && in[0] == 0u) {
        p->servo[LINK_SS_CAP_ARM] = 0u;
        p->servo[LINK_SS_CAP_STATE] = (uint16_t)LINK_CAP_IDLE;
        ++p->cap_gen;
        return 0u;
    }
    /* Anything else is an arm, and an arm is its whole frame. */
    if (off != (uint8_t)LINK_SS_CAP_ARM || n != LINK_SS_CAP_FRAME) {
        return LINK_NACK_BAD_VALUE;
    }
    const uint8_t nack = arm_check(p, in, o);
    if (nack != 0u) {
        return nack;
    }
    memcpy(&p->servo[LINK_SS_CAP_ARM], in,
           LINK_SS_CAP_FRAME * sizeof(uint16_t));
    p->servo[LINK_SS_CAP_STATE]    = (uint16_t)LINK_CAP_ARMED;
    p->servo[LINK_SS_CAP_MOVE_T]   = 0u;
    p->servo[LINK_SS_CAP_ARRIVE_T] = 0u;
    p->servo[LINK_SS_CAP_PEAK_MA]  = 0u;
    p->servo[LINK_SS_CAP_MEAN_MA]  = 0u;
    p->servo[LINK_SS_CAP_SAMPLES]  = 0u;
    p->servo[LINK_SS_CH_FLAGS] &= (uint16_t)~LINK_SS_CAP_CLIPPED;
    ++p->cap_gen;
    return 0u;
}

void sense_servo_read(const sense_page_t *p, uint8_t off, uint8_t n,
                      uint16_t *out)
{
    if (p == NULL || out == NULL
        || (unsigned)off + (unsigned)n > (unsigned)LINK_SS_COUNT) {
        return;
    }
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = p->servo[off + i];
    }
}

void sense_win_read(const sense_page_t *p, uint8_t off, uint8_t n,
                    uint16_t *out)
{
    if (p == NULL || out == NULL
        || (unsigned)off + (unsigned)n > (unsigned)LINK_SW_COUNT) {
        return;
    }
    for (uint8_t i = 0; i < n; ++i) {
        const unsigned r = (unsigned)off + i;
        uint16_t v = p->win[r];
        /* The capture's three are SERVO_SENSE's, as that page reads now:
         * an arm or a disarm shows here in the same pass. */
        if (r == (unsigned)LINK_SW_FLAGS) {
            if ((p->servo[LINK_SS_CH_FLAGS] & LINK_SS_CAP_CLIPPED) != 0u) {
                v |= (uint16_t)LINK_SW_CAP_CLIPPED;
            }
        } else if (r == (unsigned)LINK_SW_CAP_STATE) {
            v = p->servo[LINK_SS_CAP_STATE];
        } else if (r == (unsigned)LINK_SW_CAP_SEQ) {
            v = p->servo[LINK_SS_CAP_SEQ];
        }
        out[i] = v;
    }
}

bool sense_page_step(sense_page_t *p, bool driving)
{
    if (p == NULL || driving) {
        return false;
    }
    const uint16_t st = p->servo[LINK_SS_CAP_STATE];
    if (st == (uint16_t)LINK_CAP_ARMED || st == (uint16_t)LINK_CAP_WAIT_MOVE
        || st == (uint16_t)LINK_CAP_MOVING) {
        p->servo[LINK_SS_CAP_ARM]   = 0u;
        p->servo[LINK_SS_CAP_STATE] = (uint16_t)LINK_CAP_IDLE;
        ++p->cap_gen;
        return true;
    }
    return false;
}

uint8_t sense_page_slots_check(const sense_page_t *p, const uint16_t *slots)
{
    if (slots == NULL) {
        return LINK_NACK_BAD_VALUE;
    }
    const uint64_t held = sense_page_held(p);
    for (unsigned s = 0; s < LINK_OUT_SLOTS; ++s) {
        const uint16_t *r = &slots[(size_t)s * LINK_OS_STRIDE];
        if (r[LINK_OS_DRIVER] != 0u && r[LINK_OS_PIN] <= OUT_MAX_PIN
            && (held & ((uint64_t)1u << r[LINK_OS_PIN])) != 0u) {
            return LINK_NACK_BAD_VALUE;
        }
    }
    return 0u;
}

/* ------------------------------------------------- core 1's order and view */

void sense_page_cmd(const sense_page_t *p, sense_cmd_t *cmd)
{
    if (p == NULL || cmd == NULL) {
        return;
    }
    const uint16_t *c  = p->sense;
    const uint16_t  en = c[LINK_SN_ENABLE];
    cmd->cfg_gen = p->cfg_gen;
    cmd->sda     = (uint8_t)c[LINK_SN_SDA_PIN];
    cmd->scl     = (uint8_t)c[LINK_SN_SCL_PIN];
    memset(&cmd->parts, 0, sizeof(cmd->parts));
    cmd->parts.ina228_en          = (en & LINK_SN_EN_I228) != 0u;
    cmd->parts.ina228_addr        = (uint8_t)c[LINK_SN_I228_ADDR];
    cmd->parts.ina228_shunt_uohm  = c[LINK_SN_I228_SHUNT_UOHM];
    cmd->parts.ina228_max_ma      = (uint32_t)c[LINK_SN_I228_MAX_DA] * 100u;
    cmd->parts.ina3221_en         = (en & LINK_SN_EN_I3221) != 0u;
    cmd->parts.ina3221_addr       = (uint8_t)c[LINK_SN_I3221_ADDR];
    cmd->parts.ina3221_shunt_uohm =
        (uint32_t)c[LINK_SN_I3221_SHUNT_DMOHM] * 100u;
    cmd->parts.ina3221_channels   = (uint8_t)c[LINK_SN_I3221_CHANNELS];
    cmd->parts.as5600_en          = (en & LINK_SN_EN_AS5600) != 0u;
    const uint16_t *v = p->servo;
    cmd->cap_gen      = p->cap_gen;
    cmd->cap_on       = (v[LINK_SS_CAP_ARM] & LINK_SS_ARM) != 0u;
    cmd->cap.rise_ua  = SENSE_CAP_RISE_AUTO;
    cmd->cap.hold_ua  = (int32_t)(int16_t)v[LINK_SS_CAP_HOLD_MA] * 1000;
    cmd->cap.move_ua  = (int32_t)v[LINK_SS_CAP_MOVE_MA] * 1000;
    cmd->cap.band_ua  = (int32_t)v[LINK_SS_CAP_BAND_MA] * 1000;
}

/* @p a / @p d to the nearest, halves away from zero; @p d positive. */
static int64_t div_round(int64_t a, int64_t d)
{
    return (a >= 0) ? (a + d / 2) / d : -((-a + d / 2) / d);
}

/* A signed register: two's complement, held to -32767 .. 32767. */
static uint16_t reg_i16(int64_t v)
{
    if (v > 32767) {
        v = 32767;
    } else if (v < -32767) {
        v = -32767;
    }
    return (uint16_t)(int16_t)v;
}

/* An unsigned register, held to 0 .. 65535. */
static uint16_t reg_u16(int64_t v)
{
    if (v < 0) {
        return 0u;
    }
    return (v > 65535) ? 65535u : (uint16_t)v;
}

/* A 32-bit value into two registers, low half first. */
static void reg_32(uint16_t *r, uint32_t v)
{
    r[0] = (uint16_t)(v & 0xFFFFu);
    r[1] = (uint16_t)(v >> 16);
}

static bool clipped(const sense_window_t *w)
{
    return w->clip_hi || w->clip_lo;
}

static uint16_t part_flags(sense_state_t st, bool id_ok, uint16_t online,
                           uint16_t good, uint16_t wrong)
{
    uint16_t f = 0u;
    if (st == SENSE_PART_ONLINE) {
        f |= online;
    }
    if (id_ok) {
        f |= good;
    }
    if (st == SENSE_PART_WRONG_ID) {
        f |= wrong;
    }
    return f;
}

static uint16_t sense_flags(const sense_snap_t *s)
{
    uint16_t f = 0u;
    if (s->open) {
        f |= (uint16_t)LINK_SN_BUS_OPEN;
    }
    if (s->stuck) {
        f |= (uint16_t)LINK_SN_BUS_STUCK;
    }
    f |= part_flags(s->i228, ina228_identity_ok(s->i228_maker, s->i228_device),
                    LINK_SN_I228_ONLINE, LINK_SN_I228_ID_OK,
                    LINK_SN_I228_ID_WRONG);
    f |= part_flags(s->i3221, ina3221_identity_ok(s->i3221_maker, s->i3221_die),
                    LINK_SN_I3221_ONLINE, LINK_SN_I3221_ID_OK,
                    LINK_SN_I3221_ID_WRONG);
    if ((s->have_win && clipped(&s->win[SENSE_SRC_INA228]))
        || s->run.i_clipped) {
        f |= (uint16_t)LINK_SN_I228_CLIPPED;
    }
    return f;
}

/* Whether the snapshot's charge and energy are run @p run_gen's. */
static bool totals_ok(const sense_snap_t *s, uint16_t run_gen)
{
    return s->run_gen == run_gen && s->run.totals_ok
           && s->i228 == SENSE_PART_ONLINE;
}

static void publish_sense(uint16_t *r, const sense_snap_t *s,
                          uint16_t run_gen)
{
    r[LINK_SN_FLAGS]    = sense_flags(s);
    r[LINK_SN_PRESENT]  = s->present;
    r[LINK_SN_I228_ID]  = s->i228_device;
    r[LINK_SN_I3221_ID] = s->i3221_die;
    r[LINK_SN_ERRORS]   = s->errors;
    r[LINK_SN_I228_TEMP_DC] =
        s->have_temp ? reg_i16(div_round(s->temp_mdegc, 100)) : 0u;
    r[LINK_SN_I228_DIAG] = s->have_diag ? s->diag : 0u;
    uint32_t charge = 0u;
    uint32_t energy = 0u;
    if (totals_ok(s, run_gen)) {
        /* 0.01 mAh is 36 mC, and 0.01 Wh is 36 J. */
        int64_t c = div_round(s->run.charge_uc, 36000);
        if (c > INT32_MAX) {
            c = INT32_MAX;
        } else if (c < -INT32_MAX) {
            c = -INT32_MAX;
        }
        charge = (uint32_t)(int32_t)c;
        const uint64_t e = (s->run.energy_mj + 18000u) / 36000u;
        energy = (e > UINT32_MAX) ? UINT32_MAX : (uint32_t)e;
    }
    reg_32(&r[LINK_SN_I228_CHARGE_LO], charge);
    reg_32(&r[LINK_SN_I228_ENERGY_LO], energy);
    r[LINK_SN_RESETS] = (uint16_t)(((unsigned)s->i3221_resets << 8)
                                   | s->i228_resets);
}

static void publish_channels(uint16_t *r, const sense_snap_t *s)
{
    uint16_t flags = (uint16_t)(r[LINK_SS_CH_FLAGS] & LINK_SS_CAP_CLIPPED);
    for (unsigned ch = 1u; ch <= LINK_SS_CHANNELS; ++ch) {
        uint16_t *c = &r[(size_t)(ch - 1u) * LINK_SS_CH_STRIDE];
        sense_window_t w;
        memset(&w, 0, sizeof(w));
        if (s->have_win) {
            w = s->win[SENSE_SRC_CH1 + ch - 1u];
        }
        c[LINK_SS_CH_MEAN_MA] = reg_i16(div_round(w.i_mean_ua, 1000));
        c[LINK_SS_CH_MAX_MA]  = reg_i16(div_round(w.i_max_ua, 1000));
        c[LINK_SS_CH_MEAN_MV] = reg_u16(div_round(w.v_mean_uv, 1000));
        c[LINK_SS_CH_MIN_MV]  = reg_u16(div_round(w.v_min_uv, 1000));
        if (w.n_i > 0u || w.n_v > 0u) {
            flags |= LINK_SS_CH_VALID(ch);
        }
        if (clipped(&w)) {
            flags |= LINK_SS_CH_CLIPPED(ch);
        }
    }
    r[LINK_SS_WINDOW]   = s->have_win ? s->win[SENSE_SRC_CH1].number : 0u;
    r[LINK_SS_CH_FLAGS] = flags;
}

/* CH1's ring into SERVO_WIN: the newest window's number, then each entry.
 * The capture's registers are filled at the read (sense_win_read()). */
static void publish_ring(uint16_t *r, const sense_snap_t *s)
{
    memset(r, 0, (size_t)LINK_SW_COUNT * sizeof(uint16_t));
    if (!s->have_win) {
        return;
    }
    r[LINK_SW_WINDOW] = s->win[SENSE_SRC_CH1].number;
    r[LINK_SW_FLAGS]  = (uint16_t)LINK_SW_HAVE;
    for (unsigned k = 0; k < LINK_SW_RING; ++k) {
        const sense_ring_win_t *w = &s->ring[k];
        if (!w->closed) {
            continue;
        }
        uint16_t *e = &r[LINK_SW_ENTRY(k, 0)];
        uint16_t f = (uint16_t)LINK_SW_E_CLOSED;
        f |= (uint16_t)((w->n_clip > LINK_SW_E_CLIPPED_MAX)
                        ? LINK_SW_E_CLIPPED_MAX : w->n_clip);
        if (w->n_i > 0u) {
            f |= (uint16_t)LINK_SW_E_CURRENT;
        }
        if (w->n_v > 0u) {
            f |= (uint16_t)LINK_SW_E_VOLTAGE;
        }
        if (w->clip_hi) {
            f |= (uint16_t)LINK_SW_E_CLIP_HI;
        }
        if (w->clip_lo) {
            f |= (uint16_t)LINK_SW_E_CLIP_LO;
        }
        e[LINK_SW_E_MEAN_MA] = reg_i16(div_round(w->i_mean_ua, 1000));
        e[LINK_SW_E_MAX_MA]  = reg_i16(div_round(w->i_max_ua, 1000));
        e[LINK_SW_E_MIN_MA]  = reg_i16(div_round(w->i_min_ua, 1000));
        e[LINK_SW_E_MEAN_MV] = reg_u16(div_round(w->v_mean_uv, 1000));
        e[LINK_SW_E_MIN_MV]  = reg_u16(div_round(w->v_min_uv, 1000));
        e[LINK_SW_E_FLAGS]   = f;
    }
}

static void publish_capture(uint16_t *r, const sense_snap_t *s)
{
    r[LINK_SS_CAP_STATE]    = (uint16_t)s->cap_state;
    r[LINK_SS_CAP_SEQ]      = s->cap_seq;
    r[LINK_SS_CAP_MOVE_T]   = reg_u16(s->cap_move_t);
    r[LINK_SS_CAP_ARRIVE_T] = reg_u16(s->cap_arrive_t);
    r[LINK_SS_CAP_PEAK_MA]  = reg_i16(div_round(s->cap_peak_ua, 1000));
    r[LINK_SS_CAP_MEAN_MA]  = reg_i16(div_round(s->cap_mean_ua, 1000));
    r[LINK_SS_CAP_SAMPLES]  = reg_u16(s->cap_samples);
    if (s->cap_clipped) {
        r[LINK_SS_CH_FLAGS] |= LINK_SS_CAP_CLIPPED;
    } else {
        r[LINK_SS_CH_FLAGS] &= (uint16_t)~LINK_SS_CAP_CLIPPED;
    }
}

/* The output encoder's registers 26 to 30.  The angle and its figures are
 * the snapshot's only for a part that is online or was: a part gone
 * offline keeps its last reading and loses ONLINE, and the still time
 * goes on counting from it. */
static void publish_enc(uint16_t *r, const sense_snap_t *s)
{
    uint16_t f = 0u;
    if (s->enc == SENSE_PART_ONLINE) {
        f |= LINK_SN_ENC_ONLINE;
        if (as5600_md(s->enc_status)) {
            f |= LINK_SN_ENC_MD;
        }
        if (as5600_ml(s->enc_status)) {
            f |= LINK_SN_ENC_ML;
        }
        if (as5600_mh(s->enc_status)) {
            f |= LINK_SN_ENC_MH;
        }
    }
    if (s->enc == SENSE_PART_WRONG_ID) {
        f |= LINK_SN_ENC_WRONG;
    }
    if (s->enc_have_angle) {
        f |= LINK_SN_ENC_VALID;
    }
    r[LINK_SN_AS5600_FLAGS]     = f;
    r[LINK_SN_AS5600_ANGLE]     = s->enc_have_angle ? s->enc_raw : 0u;
    r[LINK_SN_AS5600_MAGNITUDE] = s->enc_have_mag ? s->enc_magnitude : 0u;
    r[LINK_SN_AS5600_SAMPLES]   = s->enc_samples;
    r[LINK_SN_AS5600_STILL_MS]  = s->enc_have_angle ? s->enc_still_ms : 0u;
}

void sense_page_publish(sense_page_t *p, const sense_snap_t *s,
                        uint16_t run_gen)
{
    if (p == NULL || s == NULL) {
        return;
    }
    /* The pins first and whatever set-up the snapshot was taken under:
     * one from before a change is exactly when old pins are still held. */
    p->held = s->held;
    if (s->cfg_gen != p->cfg_gen) {
        return;
    }
    publish_sense(p->sense, s, run_gen);
    publish_enc(p->sense, s);
    publish_channels(p->servo, s);
    publish_ring(p->win, s);
    if (s->cap_gen == p->cap_gen) {
        publish_capture(p->servo, s);
    }
}

/* Volts or amps in hundredths, rounded and held to a register. */
static uint16_t hundredths(float x)
{
    const float h = x * 100.0f + 0.5f;
    if (!(h > 0.0f)) {
        return 0u;                      /* negative, zero or NaN */
    }
    return (h >= 65535.0f) ? 65535u : (uint16_t)h;
}

void sense_page_esc(sense_page_t *p, bool v_ok, float volts, bool i_ok,
                    float amps)
{
    if (p == NULL) {
        return;
    }
    uint16_t *r = p->sense;
    r[LINK_SN_ESC_VOLTAGE_CV] = v_ok ? hundredths(volts) : 0u;
    r[LINK_SN_ESC_CURRENT_CA] = i_ok ? hundredths(amps) : 0u;
    r[LINK_SN_ESC_FLAGS] = (uint16_t)((v_ok ? LINK_SN_ESC_VOLTAGE_OK : 0u)
                                      | (i_ok ? LINK_SN_ESC_CURRENT_OK : 0u));
}

void sense_page_bench(sense_page_t *p, const sense_snap_t *s,
                      uint16_t run_gen, bool driving, bench_state_t *b)
{
    if (p == NULL || s == NULL || b == NULL) {
        return;
    }
    const bool fresh   = s->cfg_gen == p->cfg_gen;
    const bool enabled = (p->sense[LINK_SN_ENABLE] & LINK_SN_EN_I228) != 0u;
    const bool online  = enabled && fresh && s->i228 == SENSE_PART_ONLINE;
    /* The source holds to the end of a run it was online in. */
    p->sensed = enabled && (online || (p->sensed && driving));
    if (!p->sensed) {
        return;
    }
    const uint16_t ok = (uint16_t)(LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK
                                   | LINK_BN_TOTALS_OK);
    b->flags   = (uint16_t)((b->flags & (uint16_t)~ok) | LINK_BN_SENSED);
    b->voltage = 0.0f;
    b->current = 0.0f;
    b->power   = 0.0f;
    const sense_window_t *w = &s->win[SENSE_SRC_INA228];
    if (online && s->have_win) {
        if (w->n_v > 0u) {
            b->voltage = (float)w->v_mean_uv * 1e-6f;
            b->flags  |= (uint16_t)LINK_BN_VOLTAGE_OK;
        }
        if (w->n_i > 0u) {
            b->current = (float)w->i_mean_ua * 1e-6f;
            b->flags  |= (uint16_t)LINK_BN_CURRENT_OK;
        }
        if (w->n_v > 0u && w->n_i > 0u) {
            b->power = b->voltage * b->current;
        }
    }
    /* The peaks of the run in force, from the 500 Hz samples, kept when
     * the part drops out within it. */
    const sense_run_t *r = &s->run;
    const bool run = fresh && s->run_gen == run_gen;
    b->voltage_min = (run && r->have_v) ? (float)r->v_min_uv * 1e-6f
                                        : b->voltage;
    b->current_max = (run && r->have_i) ? (float)r->i_max_ua * 1e-6f
                                        : b->current;
    b->power_max   = (run && r->have_p) ? (float)r->p_max_uw * 1e-6f
                                        : b->power;
    b->charge_mah = 0.0f;
    b->energy_wh  = 0.0f;
    if (fresh && totals_ok(s, run_gen)) {
        /* 1 mAh is 3.6 C; 1 Wh is 3600 J. */
        b->charge_mah = (float)r->charge_uc / 3.6e6f;
        b->energy_wh  = (float)r->energy_mj / 3.6e6f;
        b->flags     |= (uint16_t)LINK_BN_TOTALS_OK;
    }
}

uint16_t sense_page_caps(const sense_page_t *p)
{
    if (p == NULL) {
        return 0u;
    }
    const uint16_t en = p->sense[LINK_SN_ENABLE];
    uint16_t caps = 0u;
    if ((en & LINK_SN_EN_I228) != 0u) {
        caps |= (uint16_t)LINK_CAP_PACK_SENSE;
    }
    if ((en & LINK_SN_EN_I3221) != 0u) {
        caps |= (uint16_t)LINK_CAP_SERVO_SENSE;
    }
    return caps;
}
