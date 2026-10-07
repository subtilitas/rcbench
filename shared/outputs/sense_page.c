/*
 * The SENSE and SERVO_SENSE link pages.  See sense_page.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_page.h"

#include <stddef.h>
#include <string.h>

#include "link_msg.h"

/* The INA3221's shunt full scale, 163.8 mV, in uV: 4095 steps of 40 uV. */
#define I3221_FULL_SCALE_UV 163800uL

/* The INA228's two shunt ranges, in uV. */
#define I228_RANGE1_UV 40960uL
#define I228_RANGE0_UV 163840uL

/* The largest current a register of signed mA carries. */
#define MA_MAX 32767u

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

uint16_t sense_i228_cal(uint16_t shunt_uohm, uint16_t max_da,
                        uint8_t *adcrange)
{
    /* The shunt's voltage at the maximum: A/10 * uOhm = uV * 10. */
    const uint32_t product = (uint32_t)max_da * (uint32_t)shunt_uohm;
    if (product == 0u || product > I228_RANGE0_UV * 10u) {
        return 0u;
    }
    /* The range is all the maximum chooses: CURRENT_LSB is the ADC's step
     * over the shunt at either one, so SHUNT_CAL does not move. */
    if (adcrange != NULL) {
        *adcrange = (product <= I228_RANGE1_UV * 10u) ? 1u : 0u;
    }
    return (uint16_t)SENSE_I228_SHUNT_CAL;
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
           && (p->sense[LINK_SN_ENABLE]
               & (LINK_SN_EN_I228 | LINK_SN_EN_I3221)) != 0u;
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
    if (c[LINK_SN_ENABLE] > (LINK_SN_EN_I228 | LINK_SN_EN_I3221)
        || (c[LINK_SN_KHZ] != LINK_SN_KHZ_STANDARD
            && c[LINK_SN_KHZ] != LINK_SN_KHZ_FAST)
        || c[LINK_SN_RESERVED_7] != 0u || c[LINK_SN_RESERVED_11] != 0u) {
        return false;
    }
    if (c[LINK_SN_I228_ADDR] < LINK_SN_I228_ADDR_MIN
        || c[LINK_SN_I228_ADDR] > LINK_SN_I228_ADDR_MAX
        || c[LINK_SN_I228_SHUNT_UOHM] < LINK_SN_I228_UOHM_MIN
        || c[LINK_SN_I228_SHUNT_UOHM] > LINK_SN_I228_UOHM_MAX
        || c[LINK_SN_I228_MAX_DA] < LINK_SN_I228_DA_MIN
        || c[LINK_SN_I228_MAX_DA] > LINK_SN_I228_DA_MAX
        || sense_i228_cal(c[LINK_SN_I228_SHUNT_UOHM], c[LINK_SN_I228_MAX_DA],
                          NULL) == 0u) {
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
static bool pwm_surface(const outputs_t *o, uint8_t ch)
{
    if (o->channel[ch].role != OUT_ROLE_SURFACE) {
        return false;
    }
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (o->slot[i].driver == OUT_DRIVER_PWM
            && o->slot[i].first_channel == ch) {
            return true;
        }
    }
    return false;
}

/* A capture's frame, CAP_ARM to CAP_BAND_MA, judged as an arm. */
static uint8_t arm_check(const sense_page_t *p, const uint16_t *f,
                         const outputs_t *o)
{
    const uint16_t arm = f[0];
    const uint8_t ch = LINK_SS_ARM_CH(arm);
    if ((arm & LINK_SS_ARM) == 0u
        || (arm & (uint16_t)~LINK_SS_ARM_BITS) != 0u || ch == 0u
        || f[1] > MA_MAX || f[2] == 0u || f[2] > MA_MAX
        || f[3] == 0u || f[3] > MA_MAX) {
        return LINK_NACK_BAD_VALUE;
    }
    const uint16_t *c = p->sense;
    if ((c[LINK_SN_ENABLE] & LINK_SN_EN_I3221) == 0u
        || (c[LINK_SN_I3221_CHANNELS] & (1u << (ch - 1u))) == 0u
        || !pwm_surface(o, LINK_SS_ARM_OUT(arm))) {
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

void sense_page_step(sense_page_t *p, bool driving)
{
    if (p == NULL || driving) {
        return;
    }
    const uint16_t st = p->servo[LINK_SS_CAP_STATE];
    if (st == (uint16_t)LINK_CAP_ARMED || st == (uint16_t)LINK_CAP_WAIT_MOVE
        || st == (uint16_t)LINK_CAP_MOVING) {
        p->servo[LINK_SS_CAP_ARM]   = 0u;
        p->servo[LINK_SS_CAP_STATE] = (uint16_t)LINK_CAP_IDLE;
    }
}

uint8_t sense_page_slots_check(const sense_page_t *p, const uint16_t *slots)
{
    if (slots == NULL) {
        return LINK_NACK_BAD_VALUE;
    }
    const uint64_t held = sense_page_pins(p);
    for (unsigned s = 0; s < LINK_OUT_SLOTS; ++s) {
        const uint16_t *r = &slots[(size_t)s * LINK_OS_STRIDE];
        if (r[LINK_OS_DRIVER] != 0u && r[LINK_OS_PIN] <= OUT_MAX_PIN
            && (held & ((uint64_t)1u << r[LINK_OS_PIN])) != 0u) {
            return LINK_NACK_BAD_VALUE;
        }
    }
    return 0u;
}
