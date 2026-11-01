/**
 * @file
 * @brief The ESC signal-wire session's rows, its draft and its editors.  See
 *        esc_wire_int.h.
 *
 * The tabs group the map's fields by what they act on.  A byte a layout does not
 * name is left out of its tab.  Fields shown and not edited that the map names at
 * the start of a page sit on IDENTITY.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "esc_wire_int.h"
#include "link_pages.h"
#include "supply_screen.h"
#include "ui_text.h"

/** @brief A tab of a firmware that addresses its fields by byte. */
typedef struct {
    ui_text_id_t  name;   /**< its label                                      */
    const uint8_t *offs;  /**< the bytes                                      */
    uint8_t       count;  /**< their count, at most EWP_ROWS x EWP_COLS       */
} tab_bytes_t;

/** @brief A tab of ESCape32's keys. */
typedef struct {
    ui_text_id_t       name; /**< its label                                   */
    const char *const *keys; /**< the keys                                    */
    uint8_t            count; /**< their count                                */
} tab_keys_t;

/** @brief BLHeli_S: what drives the motor. */
static const uint8_t k_s_motor[] = { 0x09u, 0x0Bu, 0x15u, 0x1Fu, 0x23u, 0x24u, 0x27u,
                                     0x0Fu };
/** @brief BLHeli_S: throttle shown, sound and light. */
static const uint8_t k_s_other[] = { 0x19u, 0x1Au, 0x21u, 0x1Bu, 0x1Cu, 0x1Du, 0x28u };
/** @brief Bluejay: what drives the motor. */
static const uint8_t k_bj_motor[] = { 0x04u, 0x07u, 0x09u, 0x10u, 0x0Bu, 0x15u, 0x1Fu,
                                      0x23u, 0x24u, 0x27u, 0x29u, 0x06u };
/** @brief Bluejay: sound, light and what is kept. */
static const uint8_t k_bj_other[] = { 0x05u, 0x1Bu, 0x1Cu, 0x1Du, 0x28u, 0x0Au, 0x2Au };
/** @brief AM32: what drives the motor. */
static const uint8_t k_am_motor[] = { 0x05u, 0x06u, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u,
                                      0x17u, 0x18u, 0x19u, 0x1Au, 0x1Bu };
/** @brief AM32: brakes and sine mode. */
static const uint8_t k_am_brake[] = { 0x0Cu, 0x0Du, 0x1Cu, 0x26u, 0x29u, 0x2Au, 0x28u,
                                      0x2Du, 0x27u, 0x2Fu };
/** @brief AM32: input and sound. */
static const uint8_t k_am_input[] = { 0x07u, 0x2Eu, 0x20u, 0x21u, 0x22u, 0x23u, 0x1Fu,
                                      0x1Eu };
/** @brief AM32: protection. */
static const uint8_t k_am_prot[] = { 0x08u, 0x24u, 0x25u, 0x2Bu, 0x2Cu, 0x09u, 0x0Au,
                                     0x0Bu, 0x16u, 0x1Du };

/** @brief A tab's table row. */
#define TAB_B(id, a) { TX_##id, (a), (uint8_t)sizeof(a) }

/** @brief BLHeli_S's tabs. */
static const tab_bytes_t k_tabs_s[] = {
    TAB_B(EW_TAB_MOTOR, k_s_motor), TAB_B(EW_TAB_OTHER, k_s_other),
};
/** @brief Bluejay's tabs. */
static const tab_bytes_t k_tabs_bj[] = {
    TAB_B(EW_TAB_MOTOR, k_bj_motor), TAB_B(EW_TAB_OTHER, k_bj_other),
};
/** @brief AM32's tabs. */
static const tab_bytes_t k_tabs_am[] = {
    TAB_B(EW_TAB_MOTOR, k_am_motor), TAB_B(EW_TAB_BRAKE, k_am_brake),
    TAB_B(EW_TAB_INPUT, k_am_input), TAB_B(EW_TAB_PROTECT, k_am_prot),
};

/** @brief ESCape32: the motor. */
static const char *const k_e_motor[] = { "arm", "damp", "revdir", "brushed", "timing",
                                         "sine_range", "sine_power", "bec" };
/** @brief ESCape32: frequency and duty. */
static const char *const k_e_duty[] = { "freq_min", "freq_max", "duty_min", "duty_max",
                                        "duty_spup", "duty_ramp", "duty_rate",
                                        "duty_drag", "duty_lock" };
/** @brief ESCape32: the throttle. */
static const char *const k_e_throt[] = { "throt_mode", "throt_rev", "throt_brk",
                                         "throt_set", "throt_ztc", "throt_cal",
                                         "throt_min", "throt_mid", "throt_max",
                                         "analog_min", "analog_max" };
/** @brief ESCape32: input, telemetry, sound and light. */
static const char *const k_e_input[] = { "input_mode", "input_ch1", "input_ch2",
                                         "telem_mode", "telem_phid", "telem_poles",
                                         "telem_volt", "telem_curr", "volume", "beacon",
                                         "led" };
/** @brief ESCape32: protection. */
static const char *const k_e_prot[] = { "prot_stall", "prot_temp", "prot_sens",
                                        "prot_volt", "prot_cells", "prot_curr",
                                        "prot_park" };
/** @brief A tab's table row of keys. */
#define TAB_K(id, a) { TX_##id, (a), (uint8_t)(sizeof(a) / sizeof((a)[0])) }

/** @brief ESCape32's tabs. */
static const tab_keys_t k_tabs_e[] = {
    TAB_K(EW_TAB_MOTOR, k_e_motor), TAB_K(EW_TAB_DUTY, k_e_duty),
    TAB_K(EW_TAB_THROTTLE, k_e_throt), TAB_K(EW_TAB_INPUT_SOUND, k_e_input),
    TAB_K(EW_TAB_PROTECT, k_e_prot),
};

_Static_assert(sizeof(k_tabs_e) / sizeof(k_tabs_e[0]) + 1u <= EWP_TABS,
               "IDENTITY and ESCape32's tabs fit");

/**
 * @brief The byte tabs of a firmware.
 * @param fw    The firmware.
 * @param count Takes their count.
 * @return The tabs, NULL for none.
 */
static const tab_bytes_t *byte_tabs(uint8_t fw, size_t *count)
{
    switch (fw) {
    case EW_FW_S:
        *count = sizeof(k_tabs_s) / sizeof(k_tabs_s[0]);
        return k_tabs_s;
    case EW_FW_BJ:
        *count = sizeof(k_tabs_bj) / sizeof(k_tabs_bj[0]);
        return k_tabs_bj;
    case EW_FW_AM:
        *count = sizeof(k_tabs_am) / sizeof(k_tabs_am[0]);
        return k_tabs_am;
    default:
        *count = 0u;
        return NULL;
    }
}

bool ewp_has_rows(void)
{
    const esc_link_readout_t *rd = &ewp_s.prog.f.rd;

    return rd->fw == (uint8_t)EW_FW_E32
           || ew_map_nth(rd->fw, rd->layout, 0u) != NULL;
}

ui_text_id_t ewp_tab_text(int tab)
{
    const uint8_t fw = ewp_s.prog.f.rd.fw;
    size_t n = 0u;
    const tab_bytes_t *b = byte_tabs(fw, &n);

    if (tab <= 0) {
        return TX_EW_TAB_ID;
    }
    if (fw == (uint8_t)EW_FW_E32) {
        return (size_t)tab <= sizeof(k_tabs_e) / sizeof(k_tabs_e[0])
                   ? k_tabs_e[tab - 1].name : TX_EW_TAB_ID;
    }
    if (!ewp_has_rows()) {
        return TX_EW_TAB_HEX;
    }
    return b != NULL && (size_t)tab <= n ? b[tab - 1].name : TX_EW_TAB_ID;
}

void ewp_tabs_follow(void)
{
    static ui_text_id_t s_ids[EWP_TABS];
    const esc_link_readout_t *rd = &ewp_s.prog.f.rd;
    const int set = (int)rd->fw * 256 + (int)rd->layout;
    size_t n = 0u;

    if (set == ewp_s.tab_set) {
        return;
    }
    ewp_s.tab_set = set;
    if (rd->fw == (uint8_t)EW_FW_E32) {
        n = sizeof(k_tabs_e) / sizeof(k_tabs_e[0]);
    } else if (ewp_has_rows()) {
        (void)byte_tabs(rd->fw, &n);
    } else {
        n = 1u;     /* the hex page */
    }
    ewp_s.tab_count = (int)n + 1;
    for (int i = 0; i < ewp_s.tab_count; ++i) {
        s_ids[i] = ewp_tab_text(i);
    }
    ui_tabs_init_text(&ewp_s.tabs, s_ids, ewp_s.tab_count, ewp_tabs_rect());
    ++ewp_s.rev;
}

bool ewp_row(int tab, int i, ewp_row_t *out)
{
    const esc_link_readout_t *rd = &ewp_s.prog.f.rd;

    if (tab <= 0 || i < 0) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (rd->fw == (uint8_t)EW_FW_E32) {
        if ((size_t)tab > sizeof(k_tabs_e) / sizeof(k_tabs_e[0])) {
            return false;
        }
        const tab_keys_t *t = &k_tabs_e[tab - 1];
        size_t slot = 0u;

        if (i >= t->count) {
            return false;
        }
        out->k = ew_key_find(t->keys[i], strlen(t->keys[i]), &slot);
        out->slot = (uint8_t)slot;
        return out->k != NULL;
    }
    size_t n = 0u;
    const tab_bytes_t *b = byte_tabs(rd->fw, &n);

    if (b == NULL || (size_t)tab > n) {
        return false;
    }
    int seen = 0;

    for (unsigned k = 0u; k < b[tab - 1].count; ++k) {
        const ew_field_t *f = ew_map_find(rd->fw, rd->layout, b[tab - 1].offs[k]);

        if (f != NULL && seen++ == i) {
            out->f = f;
            return true;
        }
    }
    return false;
}

const char *ewp_row_name(const ewp_row_t *r)
{
    return r->f != NULL ? r->f->name : r->k->key;
}

void ewp_row_text(const ewp_row_t *r, char *out, size_t size)
{
    if (r->f != NULL) {
        (void)ew_field_text(r->f, ewp_s.draft[r->f->off], out, size);
    } else {
        (void)ew_key_text(r->k, ew_e32_get(ewp_s.draft, r->slot), out, size);
    }
}

bool ewp_row_changed(const ewp_row_t *r)
{
    const uint8_t *page = ewp_s.prog.f.image.page;

    return r->f != NULL
               ? ewp_s.draft[r->f->off] != page[r->f->off]
               : ew_e32_get(ewp_s.draft, r->slot) != ew_e32_get(page, r->slot);
}

bool ewp_row_live(const ewp_row_t *r)
{
    if ((ewp_s.prog.f.rd.flags & LINK_EW_F_WRITABLE) == 0u) {
        return false;
    }
    return r->f == NULL || r->f->access == (uint8_t)EW_ACC_EDIT;
}

int ewp_changed_count(void)
{
    const esc_link_readout_t *rd = &ewp_s.prog.f.rd;
    const uint8_t *page = ewp_s.prog.f.image.page;
    int n = 0;

    if (rd->fw == (uint8_t)EW_FW_E32) {
        for (size_t i = 0u; i < EW_E32_KEYS; ++i) {
            n += ew_e32_get(ewp_s.draft, i) != ew_e32_get(page, i) ? 1 : 0;
        }
        return n;
    }
    const ew_field_t *f;

    for (size_t i = 0u; (f = ew_map_nth(rd->fw, rd->layout, i)) != NULL; ++i) {
        n += ewp_s.draft[f->off] != page[f->off] ? 1 : 0;
    }
    return n;
}

/* ------------------------------------------------------------- the editors */

void ewp_editors_close(void)
{
    ui_keypad_close(&ewp_s.kp);
    ewp_s.kp_what = EWP_KP_NONE;
    ewp_s.ch_what = EWP_CH_NONE;
    ewp_s.ch_count = 0;
    ++ewp_s.rev;
}

void ewp_choice_add(const char *label, int value)
{
    if (ewp_s.ch_count < EWP_CHOICE_MAX) {
        ewp_s.ch_values[ewp_s.ch_count] = value;
        snprintf(ewp_s.ch_labels[ewp_s.ch_count],
                 sizeof(ewp_s.ch_labels[0]), "%s", label);
        ++ewp_s.ch_count;
    }
}

/**
 * @brief The power of ten of a field's decimals.
 * @param dec The decimals.
 * @return 10 to the @p dec.
 */
static float tens(unsigned dec)
{
    float v = 1.0f;

    while (dec-- > 0u) {
        v *= 10.0f;
    }
    return v;
}

/**
 * @brief A field's byte as the operator's number.
 * @param f   The field.
 * @param raw The byte.
 * @return The number.
 */
static float field_value(const ew_field_t *f, int raw)
{
    const int32_t mul = f->mul != 0 ? f->mul : 1;
    const float div = f->div != 0u ? (float)f->div : 1.0f;

    return ((float)((int32_t)raw * mul + f->add)) / div / tens(f->dec);
}

/**
 * @brief The keypad for a number of the field or key in the editor.
 */
static void keypad_for_row(void)
{
    static char s_unit[8];
    const ewp_row_t *r = &ewp_s.edit;
    const gfx_rect_t area = ewp_editor_area();

    ewp_s.kp_what = EWP_KP_ROW;
    ewp_s.ch_what = EWP_CH_NONE;
    if (r->f != NULL) {
        const ew_field_t *f = r->f;
        const int dec = f->dec < 3u ? (int)f->dec : 3;

        snprintf(s_unit, sizeof(s_unit), "%s", f->unit);
        ui_keypad_open(&ewp_s.kp, area, f->name, s_unit,
                       field_value(f, ewp_s.draft[f->off]), field_value(f, f->lo),
                       field_value(f, f->hi), f->div != 0u ? 1 : dec);
        return;
    }
    const ew_key_t *k = r->k;
    const int32_t mul = k->mul != 0 ? k->mul : 1;
    const float scale = (float)mul / tens(k->dec);
    const float now = (float)ew_e32_get(ewp_s.draft, r->slot) * scale;

    snprintf(s_unit, sizeof(s_unit), "%s", k->unit);
    if (k->lo < 0) {
        ui_keypad_open_signed(&ewp_s.kp, area, k->key, s_unit, now,
                              (float)k->lo * scale, (float)k->hi * scale,
                              (int)k->dec);
    } else {
        ui_keypad_open(&ewp_s.kp, area, k->key, s_unit, now, (float)k->lo * scale,
                       (float)k->hi * scale, (int)k->dec);
    }
}

void ewp_edit_open(const ewp_row_t *r)
{
    char text[32];

    ewp_editors_close();
    ewp_s.edit = *r;
    if (r->f != NULL) {
        const ew_field_t *f = r->f;

        if (f->labels != NULL) {
            ewp_s.ch_what = EWP_CH_FIELD;
            for (unsigned v = f->lo; v <= f->hi; ++v) {
                (void)ew_field_text(f, (uint8_t)v, text, sizeof(text));
                ewp_choice_add(text, (int)v);
            }
            return;
        }
        if (f->has_alt) {
            ewp_s.ch_what = EWP_CH_FIELD;
            ewp_choice_add(f->alt_label, (int)f->alt);
            ewp_choice_add(TR(EW_TYPE_NUMBER), EWP_CHOICE_NUMBER);
            return;
        }
        keypad_for_row();
        return;
    }
    const ew_key_t *k = r->k;

    if (k->labels != NULL) {
        ewp_s.ch_what = EWP_CH_FIELD;
        for (int v = k->lo; v <= k->hi; ++v) {
            (void)ew_key_text(k, (int16_t)v, text, sizeof(text));
            ewp_choice_add(text, v);
        }
        return;
    }
    if (k->has_alt) {
        ewp_s.ch_what = EWP_CH_FIELD;
        ewp_choice_add(k->alt_label, (int)k->alt);
        ewp_choice_add(TR(EW_TYPE_NUMBER), EWP_CHOICE_NUMBER);
        return;
    }
    keypad_for_row();
}

/**
 * @brief A value of the row in the editor goes into the draft.
 * @param v The byte or the key's value.
 */
static void row_set(int v)
{
    if (ewp_s.edit.f != NULL) {
        ewp_s.draft[ewp_s.edit.f->off] = (uint8_t)v;
    } else {
        ew_e32_put(ewp_s.draft, ewp_s.edit.slot, (int16_t)v);
    }
    ewp_s.note[0] = '\0';
}

void ewp_choice_done(int i)
{
    if (i < 0 || i >= ewp_s.ch_count) {
        return;
    }
    const int v = ewp_s.ch_values[i];
    const int what = ewp_s.ch_what;

    if (what == EWP_CH_FIELD && v == EWP_CHOICE_NUMBER) {
        ewp_s.ch_count = 0;
        keypad_for_row();
        return;
    }
    ewp_editors_close();
    switch (what) {
    case EWP_CH_TARGET:
        ewp_s.target_at = (uint8_t)v;
        break;
    case EWP_CH_POWER:
        ewp_s.power = (uint8_t)v;
        break;
    case EWP_CH_FIELD:
        row_set(v);
        break;
    case EWP_CH_FILES:
        ewp_file_chosen(v);
        break;
    default:
        break;
    }
}

/**
 * @brief The keypad's number on the row in the editor.
 * @param v The number.
 */
static void keypad_row(float v)
{
    const ewp_row_t *r = &ewp_s.edit;

    if (r->f != NULL) {
        const ew_field_t *f = r->f;
        const float mul = (float)(f->mul != 0 ? f->mul : 1);
        const float div = f->div != 0u ? (float)f->div : 1.0f;
        const long raw = lroundf((v * tens(f->dec) * div - (float)f->add) / mul);

        if (raw < 0 || raw > 255 || !ew_field_ok(f, (uint8_t)raw)) {
            ui_keypad_refuse(&ewp_s.kp);
            return;
        }
        row_set((int)raw);
    } else {
        const ew_key_t *k = r->k;
        const float scale = (float)(k->mul != 0 ? k->mul : 1) / tens(k->dec);
        const long raw = lroundf(v / scale);

        if (raw < INT16_MIN || raw > INT16_MAX || !ew_key_ok(k, (int16_t)raw)) {
            ui_keypad_refuse(&ewp_s.kp);
            return;
        }
        row_set((int)raw);
    }
    ewp_editors_close();
}

void ewp_keypad_done(float v)
{
    switch (ewp_s.kp_what) {
    case EWP_KP_VOLT:
        ewp_s.mv = (uint16_t)lroundf(v * 1000.0f);
        ewp_s.supply_typed = true;
        break;
    case EWP_KP_AMP:
        ewp_s.ma = (uint16_t)lroundf(v * 1000.0f);
        ewp_s.supply_typed = true;
        break;
    case EWP_KP_HOLD:
        ewp_s.hold_ms = (uint32_t)lroundf(v * 1000.0f);
        break;
    case EWP_KP_OFF:
        ewp_s.off_ms = (uint16_t)lroundf(v * 1000.0f);
        break;
    case EWP_KP_ROW:
        keypad_row(v);
        return;
    default:
        break;
    }
    ewp_editors_close();
}

void ewp_connect_row(int row)
{
    const gfx_rect_t area = ewp_editor_area();

    ewp_editors_close();
    switch (row) {
    case 0:
        ewp_s.ch_what = EWP_CH_TARGET;
        for (unsigned i = 0u; i < ewp_s.target_count; ++i) {
            char line[32];
            const esc_wire_target_t *t = &ewp_s.targets[i];

            snprintf(line, sizeof(line),
                     (t->target & LINK_EW_TARGET_PORT) != 0u ? TR(EW_TARGET_PORT)
                                                             : TR(EW_TARGET_SLOT),
                     (unsigned)(t->target & 0xFFu) + 1u, (unsigned)t->gpio);
            ewp_choice_add(line, (int)i);
        }
        break;
    case 1:
        ewp_s.ch_what = EWP_CH_POWER;
        ewp_choice_add(TR(EW_POWER_SWITCHED), LINK_ESC_SWITCHED);
        ewp_choice_add(TR(EW_POWER_HAND), LINK_ESC_BY_HAND);
        break;
    case 2: {
        const supply_caps_t lim = supply_screen_caps();
        const float lo = (float)LINK_ESC_SUPPLY_MIN_MV / 1000.0f;

        ewp_s.kp_what = EWP_KP_VOLT;
        ui_keypad_open(&ewp_s.kp, area, TR(EW_ROW_VOLT), "V", (float)ewp_s.mv / 1000.0f,
                       lim.v_min > lo ? lim.v_min : lo, lim.v_max, 2);
        break;
    }
    case 3: {
        const supply_caps_t lim = supply_screen_caps();
        const float lo = (float)ESC_PROG_MIN_MA / 1000.0f;

        ewp_s.kp_what = EWP_KP_AMP;
        ui_keypad_open(&ewp_s.kp, area, TR(EW_ROW_AMP), "A", (float)ewp_s.ma / 1000.0f,
                       lim.i_min > lo ? lim.i_min : lo, lim.i_max, 2);
        break;
    }
    case 4:
        ewp_s.kp_what = EWP_KP_HOLD;
        ui_keypad_open(&ewp_s.kp, area, TR(EW_ROW_HOLD), "s",
                       (float)ewp_s.hold_ms / 1000.0f,
                       (float)ESC_WIRE_HOLD_MIN_MS / 1000.0f,
                       (float)ESC_WIRE_HOLD_MAX_MS / 1000.0f, 1);
        break;
    case 5:
        ewp_s.kp_what = EWP_KP_OFF;
        ui_keypad_open(&ewp_s.kp, area, TR(EW_ROW_OFF), "s",
                       (float)ewp_s.off_ms / 1000.0f, 0.0f,
                       (float)LINK_ESC_OFF_MS_MAX / 1000.0f, 1);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------ the hold pages */

void ewp_page_open(int kind)
{
    ewp_editors_close();
    ewp_s.page = kind;
    ewp_s.page_lines = 0;
    memset(ewp_s.line_warn, 0, sizeof(ewp_s.line_warn));
    ewp_s.note[0] = '\0';
}

void ewp_page_add(const char *s)
{
    if (ewp_s.page_lines < EWP_PAGE_LINES) {
        snprintf(ewp_s.lines[ewp_s.page_lines], sizeof(ewp_s.lines[0]), "%s", s);
        ++ewp_s.page_lines;
    }
}

/**
 * @brief Whether a change to @p to is one the warning names.
 * @param flags The field's or key's EW_FF_*.
 * @param to    The new value.
 * @return true then.
 */
static bool warns_on(uint8_t flags, int to)
{
    if ((flags & EW_FF_WARN) == 0u) {
        return false;
    }
    return (flags & EW_FF_WARN_ZERO) == 0u || to == 0;
}

/** @brief The WRITE list's walk: the page it fills and where it is. */
typedef struct {
    int at;    /**< the changes passed            */
    int first; /**< the page's first change       */
} write_walk_t;

/**
 * @brief One changed field or key: its line when it falls on the page shown.
 * @param w     The walk.
 * @param name  Its name.
 * @param from  Its value as read.
 * @param to    Its value in the snapshot.
 * @param warns It is a second-look field.
 */
static void write_line(write_walk_t *w, const char *name, const char *from,
                       const char *to, bool warns)
{
    if (w->at >= w->first && w->at < w->first + EWP_WRITE_PER_PAGE) {
        char line[EWP_LINE];

        snprintf(line, sizeof(line), warns ? TR(EW_WRITE_WARN_LINE) : "%s: %s -> %s",
                 name, from, to);
        ewp_page_add(line);
        if (warns) {
            ewp_s.line_warn[ewp_s.page_lines - 1] = true;
        }
    }
    ++w->at;
}

/**
 * @brief The snapshot's changes in the list's order, second-look fields first.
 * @param w The walk; its @c at ends as their count.
 */
static void write_walk(write_walk_t *w)
{
    const esc_link_readout_t *rd = &ewp_s.prog.f.rd;
    const uint8_t *page = ewp_s.prog.f.image.page;
    char from[32];
    char to[32];

    for (int pass = 0; pass < 2; ++pass) {
        const bool want = pass == 0;

        if (rd->fw == (uint8_t)EW_FW_E32) {
            for (size_t i = 0u; i < EW_E32_KEYS; ++i) {
                const ew_key_t *k = &ew_map_e32[i];
                const int16_t a = ew_e32_get(page, i);
                const int16_t b = ew_e32_get(ewp_s.w_draft, i);

                if (a != b && warns_on(k->flags, b) == want) {
                    (void)ew_key_text(k, a, from, sizeof(from));
                    (void)ew_key_text(k, b, to, sizeof(to));
                    write_line(w, k->key, from, to, want);
                }
            }
        } else {
            const ew_field_t *f;

            for (size_t i = 0u; (f = ew_map_nth(rd->fw, rd->layout, i)) != NULL; ++i) {
                const uint8_t a = page[f->off];
                const uint8_t b = ewp_s.w_draft[f->off];

                if (a != b && warns_on(f->flags, b) == want) {
                    (void)ew_field_text(f, a, from, sizeof(from));
                    (void)ew_field_text(f, b, to, sizeof(to));
                    write_line(w, f->name, from, to, want);
                }
            }
        }
    }
}

int ewp_write_pages(void)
{
    write_walk_t w = { 0, INT_MAX - EWP_WRITE_PER_PAGE };

    write_walk(&w);
    return w.at > 0 ? (w.at + EWP_WRITE_PER_PAGE - 1) / EWP_WRITE_PER_PAGE : 1;
}

/**
 * @brief The WRITE list's page shown, as lines.
 */
static void write_fill(void)
{
    const int pages = ewp_write_pages();
    write_walk_t w = { 0, ewp_s.w_page * EWP_WRITE_PER_PAGE };

    ewp_s.page_lines = 0;
    memset(ewp_s.line_warn, 0, sizeof(ewp_s.line_warn));
    write_walk(&w);
    if (ewp_s.w_page == pages - 1) {
        ewp_s.w_seen_last = true;
        ewp_page_add(TR(EW_WRITE_NOTE));
    }
    ++ewp_s.rev;
}

void ewp_page_write(void)
{
    ewp_page_open(EWP_PAGE_WRITE);
    memcpy(ewp_s.w_draft, ewp_s.draft, sizeof(ewp_s.w_draft));
    ewp_s.w_page = 0;
    ewp_s.w_seen_last = false;
    write_fill();
}

void ewp_write_turn(int delta)
{
    const int to = ewp_s.w_page + delta;

    if (ewp_s.page == EWP_PAGE_WRITE && to >= 0 && to < ewp_write_pages()) {
        ewp_s.w_page = to;
        write_fill();
    }
}
