/*
 * SPDX-License-Identifier: MIT
 */

#include "kst_reg.h"

#include <string.h>

/* Conversions between a raw value and its displayed value. */
enum {
    CONV_RAW = 0,     /* displayed as stored */
    CONV_DUTY,        /* round(raw x 100 / 255) % */
    CONV_BOOST,       /* 50 us + 12.5 us x raw */
    CONV_STRETCH,     /* 1 << index */
    CONV_DEAD_BAND,   /* 0.2 us x (65 - raw) */
    CONV_PULSE,       /* 0.8 us x raw */
    CONV_NEUTRAL,     /* 810 us + raw x 409 / 2048 us */
    CONV_PROT1_TIME,  /* 0.1 s x raw, capped at 100.0 s; decode only */
    CONV_PROT1_PWM,   /* round(raw x 97 / 255) %; decode only */
    CONV_THERMO,      /* count of 1 bits upward from bit 0 */
};

#define ONE(name, reg, shift, bits, access, unit, conv) \
    { name, reg, shift, bits, 0, 0, 0, access, unit, conv }
#define TWO(name, lo, hi, hi_shift, hi_bits, access, unit, conv) \
    { name, lo, 0, 8, hi, hi_shift, hi_bits, access, unit, conv }
#define UNNAMED(reg, shift, bits) \
    ONE("", reg, shift, bits, KST_ACCESS_NEVER, KST_UNIT_RAW, CONV_RAW)

#define ED KST_ACCESS_EDITABLE
#define LK KST_ACCESS_LOCKED
#define NV KST_ACCESS_NEVER

/* In the order of kst_field_id_t. */
static const kst_field_t k_fields[KST_F_COUNT] = {
    UNNAMED(0x00, 0, 8),
    ONE("", 0x01, 0, 8, NV, KST_UNIT_RAW, CONV_RAW),
    ONE("Max. Duty", 0x02, 0, 8, ED, KST_UNIT_PERCENT, CONV_DUTY),
    ONE("Boost", 0x03, 0, 8, ED, KST_UNIT_TENTH_US, CONV_BOOST),
    ONE("Stretch", 0x04, 0, 3, ED, KST_UNIT_FACTOR, CONV_STRETCH),
    ONE("Soft_Start", 0x04, 3, 1, ED, KST_UNIT_BOOL, CONV_RAW),
    UNNAMED(0x04, 4, 2),
    ONE("20k_sel", 0x04, 6, 1, LK, KST_UNIT_BOOL, CONV_RAW),
    ONE("spd_sel", 0x04, 7, 1, ED, KST_UNIT_BOOL, CONV_RAW),
    ONE("Dead Band", 0x05, 0, 6, ED, KST_UNIT_TENTH_US, CONV_DEAD_BAND),
    ONE("Lock", 0x05, 6, 1, LK, KST_UNIT_BOOL, CONV_RAW),
    ONE("Allow Uncont", 0x05, 7, 1, ED, KST_UNIT_BOOL, CONV_RAW),
    UNNAMED(0x06, 0, 8),
    TWO("Pulse Lower", 0x07, 0x09, 0, 4, ED, KST_UNIT_TENTH_US, CONV_PULSE),
    TWO("Pulse Upper", 0x08, 0x09, 4, 4, ED, KST_UNIT_TENTH_US, CONV_PULSE),
    TWO("Neutral", 0x0B, 0x0A, 0, 5, ED, KST_UNIT_TENTH_US, CONV_NEUTRAL),
    UNNAMED(0x0A, 5, 1),
    ONE("Reversion", 0x0A, 6, 1, ED, KST_UNIT_BOOL, CONV_RAW),
    UNNAMED(0x0A, 7, 1),
    TWO("Uncontrolled Pos", 0x0C, 0x0D, 0, 4, ED, KST_UNIT_TENTH_US,
        CONV_PULSE),
    UNNAMED(0x0D, 6, 2),
    ONE("Uncontrolled Time", 0x0E, 0, 8, ED, KST_UNIT_TENTH_S, CONV_RAW),
    UNNAMED(0x0F, 0, 8),
    ONE("Left Angle", 0x10, 0, 8, ED, KST_UNIT_RAW, CONV_RAW),
    ONE("Right Angle", 0x11, 0, 8, ED, KST_UNIT_RAW, CONV_RAW),
    UNNAMED(0x12, 0, 3),
    ONE("prot1_cancle", 0x12, 3, 1, LK, KST_UNIT_BOOL, CONV_RAW),
    UNNAMED(0x12, 4, 2),
    ONE("prot1en", 0x12, 6, 1, LK, KST_UNIT_BOOL, CONV_RAW),
    UNNAMED(0x12, 7, 1),
    TWO("prot1_time", 0x13, 0x0D, 4, 2, LK, KST_UNIT_TENTH_S,
        CONV_PROT1_TIME),
    UNNAMED(0x14, 0, 8),
    ONE("prot1_pwm", 0x15, 0, 8, LK, KST_UNIT_PERCENT, CONV_PROT1_PWM),
    UNNAMED(0x16, 0, 8),
    TWO("prot_pot_err", 0x17, 0x18, 0, 4, LK, KST_UNIT_COUNT_OF, CONV_THERMO),
    UNNAMED(0x18, 4, 4),
    ONE("pot_sam_times", 0x19, 0, 8, LK, KST_UNIT_RAW, CONV_RAW),
    ONE("prot_pwm_err", 0x1A, 0, 8, LK, KST_UNIT_COUNT_OF, CONV_THERMO),
    TWO("SPD", 0x1B, 0x1C, 0, 4, ED, KST_UNIT_TENTH_PERCENT, CONV_RAW),
    UNNAMED(0x1C, 4, 4),
    ONE("node_addr", 0x1D, 0, 8, NV, KST_UNIT_RAW, CONV_RAW),
    UNNAMED(0x1E, 0, 8),
    ONE("Servo / Gyro Servo", 0x1F, 0, 1, LK, KST_UNIT_BOOL, CONV_RAW),
    UNNAMED(0x1F, 1, 7),
};

const kst_field_t *kst_field(kst_field_id_t id)
{
    if ((unsigned)id >= KST_F_COUNT) {
        return NULL;
    }
    return &k_fields[id];
}

static unsigned part_mask(unsigned shift, unsigned bits)
{
    return ((1u << bits) - 1u) << shift;
}

static unsigned width(const kst_field_t *f)
{
    return (unsigned)f->lo_bits + f->hi_bits;
}

uint16_t kst_field_raw_max(kst_field_id_t id)
{
    const kst_field_t *f = kst_field(id);

    return f == NULL ? 0u : (uint16_t)((1u << width(f)) - 1u);
}

uint8_t kst_field_reg_mask(kst_field_id_t id, uint8_t reg)
{
    const kst_field_t *f = kst_field(id);
    unsigned mask = 0;

    if (f == NULL) {
        return 0;
    }
    if (f->lo_reg == reg) {
        mask |= part_mask(f->lo_shift, f->lo_bits);
    }
    if (f->hi_bits != 0u && f->hi_reg == reg) {
        mask |= part_mask(f->hi_shift, f->hi_bits);
    }
    return (uint8_t)mask;
}

uint16_t kst_field_get(const kst_image_t *img, kst_field_id_t id)
{
    const kst_field_t *f = kst_field(id);
    unsigned raw;

    if (img == NULL || f == NULL) {
        return 0;
    }
    raw = ((unsigned)img->r[f->lo_reg] >> f->lo_shift)
          & ((1u << f->lo_bits) - 1u);
    if (f->hi_bits != 0u) {
        const unsigned hi = ((unsigned)img->r[f->hi_reg] >> f->hi_shift)
                            & ((1u << f->hi_bits) - 1u);

        raw |= hi << f->lo_bits;
    }
    return (uint16_t)raw;
}

bool kst_field_set(kst_image_t *img, kst_field_id_t id, uint16_t raw)
{
    const kst_field_t *f = kst_field(id);
    unsigned mask;

    if (img == NULL || f == NULL || raw > kst_field_raw_max(id)) {
        return false;
    }
    mask = part_mask(f->lo_shift, f->lo_bits);
    img->r[f->lo_reg] = (uint8_t)((img->r[f->lo_reg] & ~mask)
                                  | (((unsigned)raw << f->lo_shift) & mask));
    if (f->hi_bits != 0u) {
        const unsigned hi = (unsigned)raw >> f->lo_bits;

        mask = part_mask(f->hi_shift, f->hi_bits);
        img->r[f->hi_reg] = (uint8_t)((img->r[f->hi_reg] & ~mask)
                                      | ((hi << f->hi_shift) & mask));
    }
    return true;
}

bool kst_field_edit(kst_image_t *img, kst_field_id_t id, uint16_t raw)
{
    if (!kst_field_set(img, id, raw)) {
        return false;
    }
    if (id == KST_F_DUTY) {
        img->r[KST_REG_DUTY_COPY] = img->r[KST_REG_DUTY];
    }
    return true;
}

/* 1 bits upward from bit 0, up to the first 0. */
static int32_t thermo(unsigned raw)
{
    int32_t n = 0;

    while ((raw & 1u) != 0u) {
        n++;
        raw >>= 1;
    }
    return n;
}

bool kst_field_display(kst_field_id_t id, uint16_t raw, int32_t *display)
{
    const kst_field_t *f = kst_field(id);
    const int32_t r = (int32_t)raw;
    int32_t d;

    if (f == NULL || display == NULL || raw > kst_field_raw_max(id)) {
        return false;
    }
    switch (f->conv) {
    case CONV_DUTY:
        d = (r * 100 + 127) / 255;
        break;
    case CONV_BOOST:
        d = 500 + 125 * r;
        break;
    case CONV_STRETCH:
        if (raw > 5u) {
            return false;
        }
        d = (int32_t)(1u << raw);
        break;
    case CONV_DEAD_BAND:
        d = 2 * (65 - r);
        break;
    case CONV_PULSE:
        d = 8 * r;
        break;
    case CONV_NEUTRAL:
        d = 8100 + (r * 4090 + 1024) / 2048;
        break;
    case CONV_PROT1_TIME:
        d = r > 1000 ? 1000 : r;
        break;
    case CONV_PROT1_PWM:
        d = (r * 97 + 127) / 255;
        break;
    case CONV_THERMO:
        d = thermo(raw);
        break;
    default:
        d = r;
        break;
    }
    *display = d;
    return true;
}

bool kst_field_raw_from_display(kst_field_id_t id, int32_t display,
                                uint16_t *raw)
{
    const kst_field_t *f = kst_field(id);
    const int32_t max = (int32_t)kst_field_raw_max(id);
    int32_t r;

    /* The widest displayed value is Boost's 500 + 125 x 255.  The bound
     * keeps every product below inside 32 bits. */
    if (f == NULL || raw == NULL || display < 0 || display > 65535) {
        return false;
    }
    switch (f->conv) {
    case CONV_DUTY:
        /* The vendor tool's rule: 50 % is 127, so the tie goes down. */
        r = display > 100 ? -1 : (display * 255 + 49) / 100;
        break;
    case CONV_BOOST:
        r = display < 500 ? -1 : (display - 500 + 62) / 125;
        break;
    case CONV_STRETCH:
        r = -1;
        for (int32_t i = 0; i <= 5; ++i) {
            if (display == (int32_t)(1u << (unsigned)i)) {
                r = i;
            }
        }
        break;
    case CONV_DEAD_BAND:
        r = display < 4 ? -1 : 65 - (display + 1) / 2;
        break;
    case CONV_PULSE:
        r = (display + 4) / 8;
        break;
    case CONV_NEUTRAL:
        r = display < 8100 ? -1 : ((display - 8100) * 2048 + 2045) / 4090;
        break;
    case CONV_PROT1_TIME:
    case CONV_PROT1_PWM:
        r = -1;
        break;
    case CONV_THERMO:
        r = display > (int32_t)width(f) ? -1
                                        : (int32_t)((1u << (unsigned)display) - 1u);
        break;
    default:
        r = display;
        break;
    }
    if (r < 0 || r > max) {
        return false;
    }
    *raw = (uint16_t)r;
    return true;
}

/* --- layout fingerprint --------------------------------------------------- */

static void fp_fail(kst_fingerprint_t *fp, kst_fp_rule_t rule, uint32_t regs)
{
    fp->rules |= (uint16_t)(1u << (unsigned)rule);
    fp->regs |= regs;
}

#define REG_BIT(reg) ((uint32_t)1 << (reg))

bool kst_fingerprint(const kst_image_t *img, kst_fingerprint_t *out)
{
    kst_fingerprint_t fp;

    memset(&fp, 0, sizeof(fp));
    if (img == NULL) {
        fp.rules = (uint16_t)((1u << KST_FP_COUNT) - 1u);
        fp.regs = 0xFFFFFFFFu;
    } else {
        if (img->r[0x00] != 0x00u) {
            fp_fail(&fp, KST_FP_R00, REG_BIT(0x00));
        }
        if (img->r[KST_REG_DUTY_COPY] != img->r[KST_REG_DUTY]) {
            fp_fail(&fp, KST_FP_DUTY_COPY,
                    REG_BIT(KST_REG_DUTY_COPY) | REG_BIT(KST_REG_DUTY));
        }
        if (img->r[0x06] != 0x04u) {
            fp_fail(&fp, KST_FP_R06, REG_BIT(0x06));
        }
        if (img->r[0x1E] != 0x00u) {
            fp_fail(&fp, KST_FP_R1E, REG_BIT(0x1E));
        }
        if ((img->r[0x04] & 0x30u) != 0u) {
            fp_fail(&fp, KST_FP_R04_B54, REG_BIT(0x04));
        }
        if ((img->r[0x0A] & 0xA0u) != 0u) {
            fp_fail(&fp, KST_FP_R0A_B5_B7, REG_BIT(0x0A));
        }
        if ((img->r[0x12] & 0xB7u) != 0u) {
            fp_fail(&fp, KST_FP_R12_UNNAMED, REG_BIT(0x12));
        }
        if ((img->r[0x1F] & 0xFEu) != 0u) {
            fp_fail(&fp, KST_FP_R1F_B71, REG_BIT(0x1F));
        }
        if ((img->r[0x04] & 0x07u) > 5u) {
            fp_fail(&fp, KST_FP_STRETCH_INDEX, REG_BIT(0x04));
        }
    }
    if (out != NULL) {
        *out = fp;
    }
    return fp.rules == 0u;
}
