/*
 * SPDX-License-Identifier: MIT
 */

#include "kst_limits.h"

/* The fields one image check reads. */
typedef struct {
    int32_t duty;
    int32_t boost;
    int32_t stretch;
    int32_t soft_start;
    int32_t spd_sel;
    int32_t spd;
    int32_t dead_band;
    int32_t allow_uncont;
    int32_t lower;        /* 0.1 us */
    int32_t upper;        /* 0.1 us */
    int32_t neutral;      /* 0.1 us */
    int32_t neutral_raw;
    int32_t lower_raw;
    int32_t upper_raw;
    int32_t uncont_raw;
    int32_t uncont;       /* 0.1 us */
    int32_t uncont_time;
    int32_t reversion;
    int32_t left;
    int32_t right;
    int32_t gyro;
} values_t;

static int32_t raw_of(const kst_image_t *img, kst_field_id_t id)
{
    return (int32_t)kst_field_get(img, id);
}

static void load(const kst_image_t *img, values_t *v)
{
    v->duty = raw_of(img, KST_F_DUTY);
    v->boost = raw_of(img, KST_F_BOOST);
    v->stretch = raw_of(img, KST_F_STRETCH);
    v->soft_start = raw_of(img, KST_F_SOFT_START);
    v->spd_sel = raw_of(img, KST_F_SPD_SEL);
    v->spd = raw_of(img, KST_F_SPD);
    v->dead_band = raw_of(img, KST_F_DEAD_BAND);
    v->allow_uncont = raw_of(img, KST_F_ALLOW_UNCONT);
    v->lower_raw = raw_of(img, KST_F_PULSE_LOWER);
    v->upper_raw = raw_of(img, KST_F_PULSE_UPPER);
    v->uncont_raw = raw_of(img, KST_F_UNCONT_POS);
    v->neutral_raw = raw_of(img, KST_F_NEUTRAL);
    v->lower = 8 * v->lower_raw;
    v->upper = 8 * v->upper_raw;
    v->uncont = 8 * v->uncont_raw;
    v->neutral = 0;
    (void)kst_field_display(KST_F_NEUTRAL, (uint16_t)v->neutral_raw,
                            &v->neutral);
    v->uncont_time = raw_of(img, KST_F_UNCONT_TIME);
    v->reversion = raw_of(img, KST_F_REVERSION);
    v->left = raw_of(img, KST_F_LEFT_ANGLE);
    v->right = raw_of(img, KST_F_RIGHT_ANGLE);
    v->gyro = raw_of(img, KST_F_GYRO);
}

static kst_rules_t when(bool broken, kst_rule_t rule)
{
    return broken ? KST_RULE_BIT(rule) : 0u;
}

static int32_t distance(int32_t a, int32_t b)
{
    return a > b ? a - b : b - a;
}

static kst_rules_t hard(const kst_image_t *img, const values_t *v)
{
    kst_rules_t r = 0;

    r |= when(v->duty > (int32_t)KST_DUTY_RAW_MAX, KST_H_DUTY_HIGH);
    r |= when(v->duty < (int32_t)KST_DUTY_RAW_MIN, KST_H_DUTY_LOW);
    r |= when(img->r[KST_REG_DUTY_COPY] != img->r[KST_REG_DUTY],
              KST_H_DUTY_COPY);
    r |= when(v->boost > (int32_t)KST_BOOST_RAW_MAX, KST_H_BOOST_HIGH);
    r |= when(v->stretch > (int32_t)KST_STRETCH_INDEX_MAX,
              KST_H_STRETCH_INDEX);
    r |= when(v->spd > (int32_t)KST_SPD_RAW_MAX, KST_H_SPD_HIGH);
    r |= when(v->spd_sel != 0 && v->spd < (int32_t)KST_SPD_RAW_MIN_ON,
              KST_H_SPD_SEL_LOW_SPD);
    r |= when(v->uncont_raw < (int32_t)KST_PULSE_RAW_MIN,
              KST_H_UNCONT_POS_LOW);
    r |= when(v->uncont_raw > (int32_t)KST_PULSE_RAW_MAX,
              KST_H_UNCONT_POS_HIGH);
    r |= when(v->uncont_raw < v->lower_raw || v->uncont_raw > v->upper_raw,
              KST_H_UNCONT_POS_SPAN);
    r |= when(v->uncont_time < (int32_t)KST_UNCONT_TIME_RAW_MIN,
              KST_H_UNCONT_TIME_LOW);
    r |= when(v->uncont_time > (int32_t)KST_UNCONT_TIME_RAW_MAX,
              KST_H_UNCONT_TIME_HIGH);
    r |= when(v->lower_raw < (int32_t)KST_PULSE_RAW_MIN, KST_H_LOWER_LOW);
    r |= when(v->neutral - v->lower < KST_MARGIN_HARD_TENTH_US,
              KST_H_LOWER_MARGIN);
    r |= when(v->upper_raw > (int32_t)KST_PULSE_RAW_MAX, KST_H_UPPER_HIGH);
    r |= when(v->upper - v->neutral < KST_MARGIN_HARD_TENTH_US,
              KST_H_UPPER_MARGIN);
    r |= when(v->upper - v->lower < KST_SPAN_MIN_TENTH_US, KST_H_SPAN);
    r |= when(v->neutral_raw < (int32_t)KST_NEUTRAL_RAW_MIN,
              KST_H_NEUTRAL_LOW);
    r |= when(v->neutral_raw > (int32_t)KST_NEUTRAL_RAW_MAX,
              KST_H_NEUTRAL_HIGH);
    r |= when(v->left == 0, KST_H_LEFT_ZERO);
    r |= when(v->right == 0, KST_H_RIGHT_ZERO);
    r |= when(v->gyro != 0, KST_H_GYRO_MODE);
    return r;
}

kst_rules_t kst_limits_image(const kst_image_t *img)
{
    values_t v;

    if (img == NULL) {
        return KST_RULES_HARD;
    }
    load(img, &v);
    return hard(img, &v);
}

static kst_rules_t access(const kst_image_t *backup, const kst_image_t *target,
                          kst_field_set_t unlocked, kst_field_set_t *refused)
{
    kst_rules_t r = 0;
    kst_field_set_t set = 0;

    for (unsigned i = 0; i < KST_F_COUNT; ++i) {
        const kst_field_id_t id = (kst_field_id_t)i;
        const kst_field_t *f = kst_field(id);

        if (kst_field_get(backup, id) == kst_field_get(target, id)) {
            continue;
        }
        if (f->access == KST_ACCESS_NEVER) {
            /* Register 01 follows 02; KST_H_DUTY_COPY holds it there. */
            if (id != KST_F_DUTY_COPY) {
                r |= KST_RULE_BIT(KST_H_NEVER);
                set |= KST_FIELD_BIT(id);
            }
        } else if (f->access == KST_ACCESS_LOCKED
                   && (unlocked & KST_FIELD_BIT(id)) == 0u) {
            r |= KST_RULE_BIT(KST_H_LOCKED);
            set |= KST_FIELD_BIT(id);
        }
    }
    if (refused != NULL) {
        *refused = set;
    }
    return r;
}

static kst_rules_t soft(const values_t *b, const values_t *t)
{
    kst_rules_t r = 0;
    const bool narrow = t->dead_band > (int32_t)KST_DEAD_BAND_RAW_1US;

    r |= when(t->duty > b->duty, KST_S_DUTY_RAISED);
    r |= when(t->duty != b->duty && t->duty < (int32_t)KST_DUTY_RAW_HALF,
              KST_S_DUTY_BELOW_HALF);
    r |= when(t->boost > b->boost, KST_S_BOOST_RAISED);
    r |= when(t->stretch > b->stretch, KST_S_STRETCH_RAISED);
    r |= when(t->soft_start < b->soft_start, KST_S_SOFT_START_OFF);
    r |= when(t->spd_sel > b->spd_sel, KST_S_SPD_SEL_ON);
    r |= when(t->spd_sel != 0 && t->spd < (int32_t)KST_SPD_RAW_SLOW
                  && (t->spd != b->spd || t->spd_sel != b->spd_sel),
              KST_S_SPD_SLOW);
    r |= when(t->dead_band > b->dead_band, KST_S_DEAD_BAND_NARROWED);
    r |= when(t->dead_band != b->dead_band && narrow,
              KST_S_DEAD_BAND_BELOW_1US);
    r |= when(t->allow_uncont > b->allow_uncont, KST_S_ALLOW_UNCONT_ON);
    r |= when(t->allow_uncont != 0
                  && distance(t->uncont, t->neutral) > KST_UNCONT_FAR_TENTH_US
                  && (t->uncont != b->uncont || t->neutral != b->neutral
                      || t->allow_uncont != b->allow_uncont),
              KST_S_UNCONT_POS_FAR);
    r |= when(t->uncont_time != b->uncont_time
                  && t->uncont_time < (int32_t)KST_UNCONT_TIME_RAW_SHORT,
              KST_S_UNCONT_TIME_SHORT);
    r |= when(t->neutral - t->lower < KST_MARGIN_SOFT_TENTH_US
                  && (t->lower != b->lower || t->neutral != b->neutral),
              KST_S_LOWER_NEAR);
    r |= when(distance(t->lower, b->lower) > KST_END_MOVE_TENTH_US,
              KST_S_LOWER_MOVED);
    r |= when(t->upper - t->neutral < KST_MARGIN_SOFT_TENTH_US
                  && (t->upper != b->upper || t->neutral != b->neutral),
              KST_S_UPPER_NEAR);
    r |= when(distance(t->upper, b->upper) > KST_END_MOVE_TENTH_US,
              KST_S_UPPER_MOVED);
    r |= when(distance(t->neutral, b->neutral) > KST_NEUTRAL_MOVE_TENTH_US,
              KST_S_NEUTRAL_MOVED);
    r |= when(t->reversion != b->reversion, KST_S_REVERSION_CHANGED);
    r |= when(t->left > b->left, KST_S_LEFT_RAISED);
    r |= when(t->left != b->left && t->left < (int32_t)KST_ANGLE_RAW_SMALL,
              KST_S_LEFT_SMALL);
    r |= when(t->right > b->right, KST_S_RIGHT_RAISED);
    r |= when(t->right != b->right && t->right < (int32_t)KST_ANGLE_RAW_SMALL,
              KST_S_RIGHT_SMALL);
    r |= when(t->gyro != b->gyro, KST_S_GYRO_CHANGED);
    r |= when(narrow && (t->stretch > 0 || t->boost > b->boost)
                  && (t->dead_band != b->dead_band || t->stretch != b->stretch
                      || t->boost != b->boost),
              KST_S_NARROW_BAND_GAIN);
    r |= when(t->duty > (int32_t)KST_DUTY_RAW_90
                  && (t->left > b->left || t->right > b->right),
              KST_S_HIGH_DUTY_TRAVEL);
    return r;
}

kst_rules_t kst_limits_edit(const kst_image_t *backup,
                            const kst_image_t *target,
                            kst_field_set_t unlocked,
                            kst_field_set_t *refused)
{
    values_t b;
    values_t t;

    if (backup == NULL || target == NULL) {
        if (refused != NULL) {
            *refused = 0;
        }
        return KST_RULES_HARD;
    }
    load(backup, &b);
    load(target, &t);
    return hard(target, &t) | access(backup, target, unlocked, refused)
           | soft(&b, &t);
}
