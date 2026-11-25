/**
 * @file
 * @brief Programming for YGE ESCs: the tabs, the rows, the draft and the
 *        editors.  See yge_panel_int.h.
 *
 * The tabs group the fields of docs/OpenYGE.md 5.3 by what they act on.  The
 * flag bits of index 26 are rows of their own.  A row whose index is at or
 * above the table's count is left out of its tab.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "link_pages.h"
#include "openyge_session.h"
#include "port_view.h"
#include "supply_screen.h"
#include "yge_panel_int.h"

/** @brief A tab's rows. */
typedef struct {
    const ygp_row_t *rows;  /**< the rows                                     */
    uint8_t          count; /**< their count, at most YGP_TAB_ROWS            */
} tab_t;

/** @brief MODE & BEC. */
static const ygp_row_t k_mode[] = {
    { OY_I_MODE, 0u }, { OY_I_BEC, 0u }, { OY_I_FLAGS, YGP_BIT_BEC12V },
    { OY_I_FLAGS, YGP_BIT_DIRECTION }, { OY_I_FLAGS, YGP_BIT_F3C },
    { OY_I_FLAGS, YGP_BIT_KEEP_MAH },
};
/** @brief PROTECTION. */
static const ygp_row_t k_prot[] = { { 8u, 0u }, { 9u, 0u }, { 27u, 0u } };
/** @brief START. */
static const ygp_row_t k_start[] = {
    { 23u, 0u }, { 24u, 0u }, { 3u, 0u }, { 7u, 0u }, { 10u, 0u }, { 4u, 0u },
};
/** @brief GOVERNOR & RPM. */
static const ygp_row_t k_gov[] = {
    { 5u, 0u }, { 6u, 0u }, { 21u, 0u }, { 22u, 0u }, { 20u, 0u },
};
/** @brief STICK. */
static const ygp_row_t k_stick[] = { { 17u, 0u }, { 18u, 0u } };
/** @brief RAW: the fields whose meaning is not known, and the telemetry
 *         type and the flags as numbers. */
static const ygp_row_t k_raw[] = {
    { 16u, 0u }, { 19u, 0u }, { OY_I_TELEMETRY, 0u }, { OY_I_FLAGS, 0u },
    { 28u, 0u }, { 29u, 0u }, { 30u, 0u }, { 31u, 0u },
};

/** @brief A tab's table row. */
#define TAB(a) { (a), (uint8_t)(sizeof(a) / sizeof((a)[0])) }

/** @brief The tabs after IDENTITY. */
static const tab_t k_tabs[] = {
    TAB(k_mode), TAB(k_prot), TAB(k_start), TAB(k_gov), TAB(k_stick), TAB(k_raw),
};

_Static_assert(sizeof(k_tabs) / sizeof(k_tabs[0]) + 1u == YGP_TABS,
               "IDENTITY and the field tabs");
_Static_assert(YGP_TABS <= UI_TABS_MAX, "the tab strip takes every tab");

void ygp_tabs_follow(void)
{
    static const ui_text_id_t k_ids[YGP_TABS] = {
        TX_YG_TAB_ID, TX_YG_TAB_MODE, TX_YG_TAB_PROTECT, TX_YG_TAB_START,
        TX_YG_TAB_GOV, TX_YG_TAB_STICK, TX_YG_TAB_RAW,
    };

    if (ygp_s.tabs_set) {
        return;
    }
    ygp_s.tabs_set = true;
    ui_tabs_init_text(&ygp_s.tabs, k_ids, YGP_TABS, ygp_tabs_rect());
    ++ygp_s.rev;
}

bool ygp_row(int tab, int i, ygp_row_t *out)
{
    const uint16_t entries = ygp_s.prog.f.image.entries;

    if (tab <= 0 || tab >= YGP_TABS || i < 0) {
        return false;
    }
    const tab_t *t = &k_tabs[tab - 1];
    int seen = 0;

    for (unsigned k = 0u; k < t->count; ++k) {
        if (t->rows[k].index < entries && seen++ == i) {
            *out = t->rows[k];
            return true;
        }
    }
    return false;
}

const char *ygp_row_name(const ygp_row_t *r)
{
    switch (r->bit) {
    case YGP_BIT_DIRECTION:
        return "DIRECTION";
    case YGP_BIT_F3C:
        return "F3C AUTOROTATION";
    case YGP_BIT_KEEP_MAH:
        return "KEEP MAH";
    case YGP_BIT_BEC12V:
        return "BEC UP TO 12V";
    default:
        break;
    }
    const oy_field_t *f = oy_map_field(r->index);

    return f != NULL ? f->key : "";
}

/**
 * @brief A unit as the face writes it: µs as us.
 * @param unit The map's unit.
 * @return The unit to draw.
 */
static const char *unit_shown(const char *unit)
{
    return strcmp(unit, "µs") == 0 ? "us" : unit;
}

/**
 * @brief The power of ten of a field's decimals.
 * @param dec The decimals.
 * @return 10 to the @p dec.
 */
static unsigned tens(unsigned dec)
{
    unsigned v = 1u;

    while (dec-- > 0u) {
        v *= 10u;
    }
    return v;
}

void ygp_value_text(const ygp_row_t *r, uint16_t v, char *out, size_t size)
{
    if (r->bit != 0u) {
        const bool on = (v & r->bit) != 0u;

        if (r->bit == YGP_BIT_DIRECTION) {
            snprintf(out, size, "%s", on ? "REVERSED" : "NORMAL");
        } else {
            snprintf(out, size, "%s", on ? "ON" : "OFF");
        }
        return;
    }
    const oy_field_t *f = oy_map_field(r->index);

    if (f == NULL || f->access == (uint8_t)OY_ACC_RAW) {
        snprintf(out, size, "%u", (unsigned)v);
        return;
    }
    if (f->kind == (uint8_t)OY_KIND_FLAGS) {
        snprintf(out, size, "0x%04X", (unsigned)v);
        return;
    }
    if (f->kind == (uint8_t)OY_KIND_CHOICE) {
        const char *name = oy_map_choice_name(r->index, v);

        if (name != NULL) {
            snprintf(out, size, "%s", name);
        } else {
            snprintf(out, size, "%u", (unsigned)v);
        }
        return;
    }
    if (f->decimals == 0u) {
        snprintf(out, size, "%u%s", (unsigned)v, unit_shown(f->unit));
        return;
    }
    const unsigned t = tens(f->decimals);

    snprintf(out, size, "%u.%0*u%s", (unsigned)v / t, (int)f->decimals,
             (unsigned)v % t, unit_shown(f->unit));
}

bool ygp_row_check(const ygp_row_t *r)
{
    const oy_field_t *f = oy_map_field(r->index);

    if (f == NULL) {
        return false;
    }
    if (r->index == OY_I_FLAGS) {
        return r->bit != 0u && (f->check_bits & r->bit) != 0u;
    }
    return f->access == (uint8_t)OY_ACC_CHECK;
}

bool ygp_row_changed(const ygp_row_t *r)
{
    const uint16_t was = ygp_s.prog.f.image.value[r->index];
    const uint16_t now = ygp_s.draft[r->index];

    return r->bit != 0u ? ((was ^ now) & r->bit) != 0u : was != now;
}

bool ygp_row_live(const ygp_row_t *r)
{
    const yge_prog_t *m = &ygp_s.prog;

    if ((m->f.rd.flags & LINK_YP_F_WRITABLE) == 0u
        || r->index >= m->f.image.entries || !oy_map_writable(r->index)) {
        return false;
    }
    if (r->index == OY_I_FLAGS) {
        const oy_field_t *f = oy_map_field(r->index);

        return r->bit != 0u && (f->edit_bits & r->bit) != 0u;
    }
    return true;
}

int ygp_changed_count(void)
{
    const yge_link_image_t *im = &ygp_s.prog.f.image;
    int n = 0;

    for (unsigned i = 0u; i < im->entries && i < YGE_LINK_VALUES_MAX; ++i) {
        n += ygp_s.draft[i] != im->value[i] ? 1 : 0;
    }
    return n;
}

void ygp_draft_follow(void)
{
    const yge_link_image_t *im = &ygp_s.prog.f.image;

    if (im->gen == ygp_s.draft_gen) {
        return;
    }
    ygp_s.draft_gen = im->gen;
    memcpy(ygp_s.draft, im->value, sizeof(ygp_s.draft));
    ygp_s.draft_reads = im->reads;
    ygp_s.draft_crc = im->read_crc;
    ++ygp_s.rev;
}

/** @brief The walk over every row of every field tab. */
typedef void (*row_fn)(const ygp_row_t *r, void *ctx);

/**
 * @brief Calls @p fn for each changed row, tab by tab.  The flags as one
 *        number on RAW are left out: their bits have rows of their own.
 * @param fn  The call.
 * @param ctx Its context.
 */
static void changed_rows(row_fn fn, void *ctx)
{
    for (int tab = 1; tab < YGP_TABS; ++tab) {
        ygp_row_t r;

        for (int i = 0; ygp_row(tab, i, &r); ++i) {
            if (ygp_row_changed(&r)
                && (r.index != OY_I_FLAGS || r.bit != 0u)) {
                fn(&r, ctx);
            }
        }
    }
}

/** @brief What the CHECK line has found. */
typedef struct {
    unsigned  n;     /**< CHECK fields changed                                */
    ygp_row_t first; /**< the first                                           */
} check_walk_t;

/**
 * @brief A changed row, for the CHECK line.
 * @param r   The row.
 * @param ctx The check_walk_t.
 */
static void check_visit(const ygp_row_t *r, void *ctx)
{
    if (ygp_row_check(r)) {
        check_walk_t *w = ctx;

        if (w->n == 0u) {
            w->first = *r;
        }
        ++w->n;
    }
}

void ygp_check_line(char *out, size_t size)
{
    check_walk_t w = { 0u, { 0u, 0u } };
    char value[32];

    out[0] = '\0';
    changed_rows(check_visit, &w);
    if (w.n == 0u) {
        return;
    }
    ygp_value_text(&w.first, ygp_s.draft[w.first.index], value, sizeof(value));
    if (w.n == 1u) {
        snprintf(out, size, TR(YG_CHECK_ONE), ygp_row_name(&w.first), value);
    } else {
        snprintf(out, size, TR(YG_CHECK_MORE), ygp_row_name(&w.first), value,
                 w.n - 1u);
    }
}

/**
 * @brief A changed row as a line of the page open: `NAME: from -> to`.
 * @param r   The row.
 * @param ctx Unused.
 */
static void page_visit(const ygp_row_t *r, void *ctx)
{
    char from[32];
    char to[32];
    char line[YGP_LINE];

    (void)ctx;
    ygp_value_text(r, ygp_s.prog.f.image.value[r->index], from, sizeof(from));
    ygp_value_text(r, ygp_s.draft[r->index], to, sizeof(to));
    snprintf(line, sizeof(line), "%s: %s -> %s", ygp_row_name(r), from, to);
    ygp_page_add(line);
}

void ygp_page_changes(void)
{
    changed_rows(page_visit, NULL);
}

/* ------------------------------------------------------------- the editors */

void ygp_editors_close(void)
{
    ui_keypad_close(&ygp_s.kp);
    ygp_s.kp_what = YGP_KP_NONE;
    ygp_s.ch_what = YGP_CH_NONE;
    ygp_s.ch_count = 0;
    ++ygp_s.rev;
}

void ygp_choice_add(const char *label, int value)
{
    if (ygp_s.ch_count < YGP_CHOICE_MAX) {
        ygp_s.ch_values[ygp_s.ch_count] = value;
        snprintf(ygp_s.ch_labels[ygp_s.ch_count], sizeof(ygp_s.ch_labels[0]),
                 "%s", label);
        ++ygp_s.ch_count;
    }
}

/**
 * @brief Whether the draft may take @p v at the row's index: in the field's
 *        writable set, or the value as read.
 * @param index The index.
 * @param v     The value.
 * @return true then.
 */
static bool value_takes(uint8_t index, uint16_t v)
{
    const uint16_t was = ygp_s.prog.f.image.value[index];

    return v == was || oy_map_value_ok(index, v, was, ygp_s.draft[OY_I_FLAGS]);
}

/**
 * @brief The keypad for the number of the row in the editor.
 * @param f The field.
 */
static void keypad_for_row(const oy_field_t *f)
{
    static char s_unit[8];
    const float t = (float)tens(f->decimals);
    const bool flag12 = f->hi_flag != 0u
                        && (ygp_s.draft[OY_I_FLAGS] & OY_FLAG_BEC12V) != 0u;
    const uint16_t hi = flag12 ? f->hi_flag : f->hi;

    ygp_s.kp_what = YGP_KP_ROW;
    snprintf(s_unit, sizeof(s_unit), "%s", unit_shown(f->unit));
    ui_keypad_open(&ygp_s.kp, ygp_editor_area(), f->key, s_unit,
                   (float)ygp_s.draft[ygp_s.edit.index] / t, (float)f->lo / t,
                   (float)hi / t, (int)f->decimals);
}

void ygp_edit_open(const ygp_row_t *r)
{
    ygp_editors_close();
    ygp_s.edit = *r;
    if (r->bit != 0u) {
        const uint16_t now = ygp_s.draft[r->index];

        ygp_s.ch_what = YGP_CH_FIELD;
        for (unsigned on = 0u; on < 2u; ++on) {
            const uint16_t v = on != 0u ? (uint16_t)(now | r->bit)
                                        : (uint16_t)(now & (uint16_t)~r->bit);
            char text[32];

            ygp_value_text(r, v, text, sizeof(text));
            ygp_choice_add(text, (int)v);
        }
        return;
    }
    const oy_field_t *f = oy_map_field(r->index);

    if (f == NULL) {
        return;
    }
    if (f->kind == (uint8_t)OY_KIND_CHOICE) {
        ygp_s.ch_what = YGP_CH_FIELD;
        for (unsigned v = 0u; v < f->n_names; ++v) {
            if (f->names[v] != NULL && value_takes(r->index, (uint16_t)v)) {
                ygp_choice_add(f->names[v], (int)v);
            }
        }
        return;
    }
    keypad_for_row(f);
}

void ygp_choice_done(int i)
{
    if (i < 0 || i >= ygp_s.ch_count) {
        return;
    }
    const int v = ygp_s.ch_values[i];
    const int what = ygp_s.ch_what;

    ygp_editors_close();
    switch (what) {
    case YGP_CH_PORT:
        ygp_s.port = (uint8_t)v;
        break;
    case YGP_CH_POWER:
        ygp_s.power = (uint8_t)v;
        break;
    case YGP_CH_FIELD:
        ygp_s.draft[ygp_s.edit.index] = (uint16_t)v;
        ygp_s.note[0] = '\0';
        break;
    case YGP_CH_FILES:
        ygp_file_chosen(v);
        break;
    default:
        break;
    }
}

/**
 * @brief The keypad's number on the row in the editor: refused outside the
 *        field's writable set.
 * @param v The number.
 */
static void keypad_row(float v)
{
    const oy_field_t *f = oy_map_field(ygp_s.edit.index);

    if (f == NULL) {
        ygp_editors_close();
        return;
    }
    const long raw = lroundf(v * (float)tens(f->decimals));

    if (raw < 0 || raw > 0xFFFF || !value_takes(ygp_s.edit.index, (uint16_t)raw)) {
        ui_keypad_refuse(&ygp_s.kp);
        return;
    }
    ygp_s.draft[ygp_s.edit.index] = (uint16_t)raw;
    ygp_s.note[0] = '\0';
    ygp_editors_close();
}

void ygp_keypad_done(float v)
{
    switch (ygp_s.kp_what) {
    case YGP_KP_VOLT:
        ygp_s.mv = (uint16_t)lroundf(v * 1000.0f);
        ygp_s.supply_typed = true;
        break;
    case YGP_KP_AMP:
        ygp_s.ma = (uint16_t)lroundf(v * 1000.0f);
        ygp_s.supply_typed = true;
        break;
    case YGP_KP_BOOT:
        ygp_s.boot_ms = (uint16_t)lroundf(v * 1000.0f);
        break;
    case YGP_KP_ROW:
        keypad_row(v);
        return;
    default:
        break;
    }
    ygp_editors_close();
}

/**
 * @brief The higher of two values.
 * @param a One.
 * @param b The other.
 * @return The higher.
 */
static float hi_of(float a, float b)
{
    return a > b ? a : b;
}

/**
 * @brief The lower of two values.
 * @param a One.
 * @param b The other.
 * @return The lower.
 */
static float lo_of(float a, float b)
{
    return a < b ? a : b;
}

void ygp_connect_row(int row)
{
    const gfx_rect_t area = ygp_editor_area();
    const supply_caps_t lim = supply_screen_caps();
    const port_view_t *pv = port_view_get();

    ygp_editors_close();
    switch (row) {
    case YGP_C_PORT: {
        const unsigned n = pv->count != PORT_VIEW_UNKNOWN && pv->count != 0u
                                   && pv->count < YGE_PANEL_PORTS
                               ? pv->count : YGE_PANEL_PORTS;

        ygp_s.ch_what = YGP_CH_PORT;
        for (unsigned p = 0u; p < n; ++p) {
            char line[32];

            if (pv->count != PORT_VIEW_UNKNOWN && p < pv->count
                && pv->pin[p] < 64u) {
                snprintf(line, sizeof(line), TR(EW_TARGET_PORT), p + 1u,
                         (unsigned)pv->pin[p]);
            } else {
                snprintf(line, sizeof(line), TR(YG_TARGET_PORT), p + 1u);
            }
            ygp_choice_add(line, (int)p);
        }
        break;
    }
    case YGP_C_POWER:
        ygp_s.ch_what = YGP_CH_POWER;
        ygp_choice_add(TR(EW_POWER_SWITCHED), LINK_YGE_SWITCHED);
        ygp_choice_add(TR(EW_POWER_HAND), LINK_YGE_BY_HAND);
        break;
    case YGP_C_VOLT:
        ygp_s.kp_what = YGP_KP_VOLT;
        ui_keypad_open(&ygp_s.kp, area, TR(EW_ROW_VOLT), "V",
                       (float)ygp_s.mv / 1000.0f,
                       hi_of(lim.v_min, (float)LINK_YGE_SUPPLY_MIN_MV / 1000.0f),
                       lo_of(lim.v_max, (float)YGE_PROG_MAX_MV / 1000.0f), 2);
        break;
    case YGP_C_AMP:
        ygp_s.kp_what = YGP_KP_AMP;
        ui_keypad_open(&ygp_s.kp, area, TR(EW_ROW_AMP), "A",
                       (float)ygp_s.ma / 1000.0f,
                       hi_of(lim.i_min, (float)YGE_PROG_MIN_MA / 1000.0f),
                       lo_of(lim.i_max, (float)YGE_PROG_MAX_MA / 1000.0f), 2);
        break;
    case YGP_C_BOOT:
        ygp_s.kp_what = YGP_KP_BOOT;
        ui_keypad_open(&ygp_s.kp, area, TR(YG_ROW_BOOT), "s",
                       (float)ygp_s.boot_ms / 1000.0f,
                       (float)OY_BOOT_MS_MIN / 1000.0f,
                       (float)OY_BOOT_MS_MAX / 1000.0f, 1);
        break;
    default:
        break;
    }
}

void ygp_connect_text(int row, char *out, size_t size)
{
    yge_prog_connect_t v;

    ygp_connect_values(&v);
    const bool hand = v.power.power == (uint8_t)LINK_YGE_BY_HAND;

    switch (row) {
    case YGP_C_PORT:
        (void)yge_panel_target_text(out, size);
        break;
    case YGP_C_POWER:
        snprintf(out, size, "%s", hand ? TR(EW_POWER_HAND) : TR(EW_POWER_SWITCHED));
        break;
    case YGP_C_VOLT:
    case YGP_C_AMP:
        if (hand) {
            snprintf(out, size, "%s", TR(EW_POWER_NONE));
        } else {
            const unsigned m = row == YGP_C_VOLT ? v.power.mv : v.power.ma;

            snprintf(out, size, row == YGP_C_VOLT ? "%u.%02uV" : "%u.%02uA",
                     m / 1000u, m % 1000u / 10u);
        }
        break;
    default:
        snprintf(out, size, "%u.%us", (unsigned)v.power.boot_ms / 1000u,
                 (unsigned)v.power.boot_ms % 1000u / 100u);
        break;
    }
}

/* ------------------------------------------------------------ the hold pages */

void ygp_page_open(int kind)
{
    ygp_editors_close();
    ygp_s.page = kind;
    ygp_s.page_lines = 0;
    ygp_s.note[0] = '\0';
}

void ygp_page_add(const char *s)
{
    if (ygp_s.page_lines < YGP_PAGE_LINES) {
        snprintf(ygp_s.lines[ygp_s.page_lines], sizeof(ygp_s.lines[0]), "%s", s);
        ++ygp_s.page_lines;
    }
}
