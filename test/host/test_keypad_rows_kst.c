/**
 * @file test_keypad_rows_kst.c
 * @brief The servo programmer's keypads (KST and Chaservo) held to
 *        keypad_rows_rig.h's four rules: every number field of TUNE, PULSE
 *        and FAILSAFE on a servo read, and MORE's programming voltage,
 *        current and PWM wait before CONNECT.  A tap opens the keypad with
 *        the field's name, unit, the range the core takes and the field's
 *        digits; a value past the range is refused and the field keeps its
 *        value; CANCEL keeps it; OK sets the value typed.  A switch toggles
 *        and STRETCH opens its list: neither opens the keypad.
 *
 * SPDX-License-Identifier: MIT
 */

#include "kst_panel_screen_rig.h"

#include "keypad_rows_rig.h"

/** @brief A field's row on its page, and its name there. */
typedef struct {
    int            page, row, col;
    kst_field_id_t id;
    const char    *name;      /**< the vendor's name; NULL: a text id */
    ui_text_id_t   label;     /**< the text, where name is NULL       */
} field_row_t;

/** @brief The rows of TUNE, PULSE and FAILSAFE, as kst_panel.c lays them
 *         out. */
static const field_row_t k_fields[] = {
    { PG_TUNE, 0, 0, KST_F_DEAD_BAND, "DEAD BAND", TX_COUNT },
    { PG_TUNE, 1, 0, KST_F_DUTY, "MAX. DUTY", TX_COUNT },
    { PG_TUNE, 1, 1, KST_F_BOOST, "BOOST", TX_COUNT },
    { PG_TUNE, 2, 0, KST_F_STRETCH, "STRETCH", TX_COUNT },
    { PG_TUNE, 2, 1, KST_F_SOFT_START, "SOFT START", TX_COUNT },
    { PG_TUNE, 3, 0, KST_F_SPD_SEL, NULL, TX_KP_SPEED_LIMIT },
    { PG_TUNE, 3, 1, KST_F_SPD, NULL, TX_KP_SPEED },
    { PG_PULSE, 0, 0, KST_F_NEUTRAL, "NEUTRAL", TX_COUNT },
    { PG_PULSE, 1, 0, KST_F_PULSE_LOWER, "PULSE LOWER", TX_COUNT },
    { PG_PULSE, 1, 1, KST_F_PULSE_UPPER, "PULSE UPPER", TX_COUNT },
    { PG_PULSE, 2, 0, KST_F_LEFT_ANGLE, "LEFT ANGLE", TX_COUNT },
    { PG_PULSE, 2, 1, KST_F_RIGHT_ANGLE, "RIGHT ANGLE", TX_COUNT },
    { PG_PULSE, 3, 0, KST_F_REVERSION, "REVERSION", TX_COUNT },
    { PG_FAILSAFE, 0, 0, KST_F_ALLOW_UNCONT, NULL, TX_KP_FS_MOVE },
    { PG_FAILSAFE, 1, 0, KST_F_UNCONT_POS, "POSITION", TX_COUNT },
    { PG_FAILSAFE, 1, 1, KST_F_UNCONT_TIME, NULL, TX_KP_FS_AFTER },
};
#define FIELD_COUNT ((int)(sizeof(k_fields) / sizeof(k_fields[0])))

/** @brief Whether the case opens MORE's entries (else the fields). */
static bool s_more;

/** @brief The keypad the programmer holds. */
static const ui_keypad_t *kp(void) { return kst_panel_keypad(); }

/**
 * @brief A key of the programmer's keypad tapped.
 * @param k The key.
 */
static void kkey(ui_key_t k) { key(k); }

/**
 * @brief The keypad of row @p row: a field's value on its page, or MORE's
 *        entry @p row in its list.
 * @param row The row, an index of k_fields or of MORE's list.
 */
static void open_row(int row)
{
    if (s_more) {
        tap(kst_panel_tab_rect(PG_SERVO));
        tap(val(PG_SERVO, 2, 1));              /* MORE: the list */
        tap(kst_panel_choice_rect(row));
        return;
    }
    const field_row_t *f = &k_fields[row];
    tap(kst_panel_tab_rect(f->page));
    tap(val(f->page, f->row, f->col));
}

/** @brief The programmer's keypad as keypad_rows_rig.h reaches it. */
static const kpr_screen_t k_kst = { kp, kkey, open_row };

/**
 * @brief The unit and the digits a field of @p unit shows.
 * @param unit The field's unit.
 * @param decimals Takes the digits after the point.
 * @return The unit's text: "us", "%" or "s"; "" for one without.
 */
static const char *unit_of(kst_unit_t unit, int *decimals)
{
    *decimals = (unit == KST_UNIT_TENTH_US || unit == KST_UNIT_TENTH_PERCENT
                 || unit == KST_UNIT_TENTH_S) ? 1 : 0;
    switch (unit) {
    case KST_UNIT_TENTH_US:      return "us";
    case KST_UNIT_PERCENT:
    case KST_UNIT_TENTH_PERCENT: return "%";
    case KST_UNIT_TENTH_S:       return "s";
    default:                     return "";
    }
}

/**
 * @brief The raw value OK is tried on for field @p id: the nearest under
 *        @p raw (over it at the bottom of the range) that shows a value of
 *        its own and that the shown value types back to, so no soft limit
 *        holds it back and no rounding moves it.
 * @param id The field.
 * @param raw The draft's raw value.
 * @param lo The range's raw bottom.
 * @param hi Its raw top.
 * @param shown Takes the value it shows, in the field's display units.
 * @return The raw value; @p raw where none is found.
 */
static uint16_t step_under(kst_field_id_t id, uint16_t raw, uint16_t lo,
                           uint16_t hi, int32_t *shown)
{
    int32_t now = 0;
    (void)kst_field_display(id, raw, &now);
    for (int dir = -1; dir <= 1; dir += 2) {
        for (int d = 1; d <= 64; ++d) {
            const int cand = (int)raw + dir * d;
            uint16_t back = 0;
            if (cand < (int)(lo < hi ? lo : hi)
                || cand > (int)(lo < hi ? hi : lo)) {
                break;
            }
            if (kst_field_display(id, (uint16_t)cand, shown)
                && *shown != now
                && kst_field_raw_from_display(id, *shown, &back)
                && back == (uint16_t)cand) {
                return (uint16_t)cand;
            }
        }
    }
    *shown = now;
    return raw;
}

/*
 * Every number field on a servo read: its keypad opens with the field's
 * name, unit and the range the core takes for it; a value past the range
 * is refused; CANCEL keeps the draft; OK puts the value typed into the
 * draft: the nearest value under the servo's that the field's raw steps
 * hold, which no soft limit holds back.  Nothing reaches the servo.
 */
TEST_CASE(every_number_field_takes_ok_and_refuses_and_cancels)
{
    int numbers = 0;
    s_more = false;
    for (int r = 0; r < FIELD_COUNT; ++r) {
        const field_row_t *f = &k_fields[r];
        const kst_unit_t unit = (kst_unit_t)kst_field(f->id)->unit;
        fresh();
        connected();
        if (unit == KST_UNIT_BOOL || unit == KST_UNIT_FACTOR) {
            /* A switch flips, STRETCH lists its factors: no keypad. */
            open_row(r);
            CHECK(!kp()->open);
            continue;
        }
        ++numbers;
        uint16_t lo = 0, hi = 0;
        int32_t dlo = 0, dhi = 0, dok = 0;
        CHECK(kst_prog_range(M, f->id, &lo, &hi));
        CHECK(kst_field_display(f->id, lo, &dlo));
        CHECK(kst_field_display(f->id, hi, &dhi));
        const uint16_t next = step_under(f->id, draft(f->id), lo, hi, &dok);
        CHECK(next != draft(f->id));
        int decimals = 0;
        kpr_row_t e = { 0 };
        e.name = (f->name != NULL) ? f->name : ui_tr(f->label);
        e.title = e.name;
        e.unit = unit_of(unit, &decimals);
        e.decimals = decimals;
        const float scale = (decimals != 0) ? 10.0f : 1.0f;
        e.min = (float)(dlo < dhi ? dlo : dhi) / scale;
        e.max = (float)(dlo < dhi ? dhi : dlo) / scale;
        e.ok = (float)dok / scale;
        kpr_row(&k_kst, r, &e);
        CHECK_EQ(draft(f->id), next);
        CHECK_EQ(g_rig.sim.n_writes, 0);
    }
    /* DEAD BAND, MAX. DUTY, BOOST, SPEED, NEUTRAL, PULSE LOWER and
     * UPPER, LEFT and RIGHT ANGLE, POSITION and AFTER. */
    CHECK_EQ(numbers, 11);
}

/*
 * MORE before CONNECT: the programming voltage (4.8 to 6.0V, a standard
 * servo's, until HV SERVO is on), the current (mA) and the PWM wait (ms)
 * each open the keypad and take OK, refuse a value past the range and
 * keep the value on CANCEL; what OK sets is kept in the settings.
 */
TEST_CASE(mores_keypads_take_ok_and_refuse_and_cancel)
{
    const kpr_row_t rows[3] = {
        { "voltage", TR(KP_PROG_VOLTAGE), "V",
          (float)KST_PORT_SUPPLY_MIN_MV / 1000.0f,
          (float)KST_PANEL_STD_MV / 1000.0f, 1, 5.5f },
        { "current", TR(KP_PROG_CURRENT), "mA", (float)PDMINI_I_MIN_MA,
          (float)PDMINI_I_MAX_MA, 0, 1500.0f },
        { "wait", TR(KP_PWM_WAIT_TITLE), "ms", 0.0f,
          (float)KST_PROG_WAIT_MAX_MS, 0, 250.0f },
    };
    s_more = true;
    for (int r = 0; r < 3; ++r) {
        fresh();
        link_up();
        kpr_row(&k_kst, r, &rows[r]);
    }
    CHECK_NEAR(settings_get(SET_SERVO_PROG_WAIT), 250.0f, 1e-3f);
    s_more = false;
}

int main(void)
{
    RUN(every_number_field_takes_ok_and_refuses_and_cancels);
    RUN(mores_keypads_take_ok_and_refuse_and_cancel);
    return test_summary("keypad_rows_kst");
}
