/**
 * @file test_band_hwstop.c
 * @brief SETUP > PANEL > Hardware stop: the band's red HWSTOP chip in
 * FAULT's place while the heartbeat bit stands, the band as without the
 * setting while it is OFF, the STOP latch's grace over both, the setting
 * kept by its key, and the chip and its box at the band's width in both
 * languages.  The IOmcu's side is held as well: the bit clears with the
 * line and no CLEAR, while the arm latch waits for one.
 *
 * Cases:
 * - off_the_band_is_as_without_the_setting
 * - on_the_heartbeat_bit_shows_hwstop_in_faults_place
 * - on_the_setting_changes_nothing_without_the_heartbeat_bit
 * - hwstop_is_red_and_reads_at_4_5_to_1_in_both_themes
 * - the_chip_comes_and_goes_with_the_bit
 * - the_hwstop_box_names_the_button_the_cable_and_the_other_bits
 * - the_stop_latch_grace_hides_hwstop_as_it_hides_fault_10
 * - the_setting_is_kept_by_its_key_and_starts_off
 * - the_setting_is_a_panel_row
 * - the_row_and_the_box_fit_in_both_languages
 * - the_iomcu_clears_the_bit_with_the_line_and_keeps_the_arm_latch
 * - status_is_due_every_500ms_and_at_every_poll_while_fast
 * - only_hwstop_shown_reads_fast
 * - the_reads_go_fast_on_the_edge_and_slow_at_the_clear
 * - off_the_reads_stay_at_500ms_through_the_fault
 *
 * SPDX-License-Identifier: MIT
 */

#include "nav_rig.h"

#include "heartbeat.h"
#include "link_dev.h"
#include "safety_gate.h"
#include "status_poll.h"
#include "stop_latch.h"

/** @brief LINK_FAULT_HEARTBEAT, FAULT 10. */
#define BEAT ((uint16_t)LINK_FAULT_HEARTBEAT)

/** @brief A step the rest of a case cannot do without. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

/** @brief Every chip: the link up, BENCH, FAULT 10, SAFE and a run clock. */
static const ui_bench_status_t k_band = {
    .link_up = true, .armed = false, .faults = BEAT, .run_seconds = 5,
    .mode = "BENCH",
};

/**
 * @brief The settings at their defaults with no store, and the setting.
 * @param on SET_HW_STOP.
 */
static void hw_stop(bool on)
{
    settings_set_store(NULL);
    settings_init();
    settings_set(SET_HW_STOP, on ? 1.0f : 0.0f);
}

/**
 * @brief The band's pixels on the menu for @p st, from a cleared canvas.
 * @param st The status.
 * @return Their FNV-1a hash.
 */
static uint32_t band_hash(const ui_bench_status_t *st)
{
    fresh();
    to_overview();
    ui_router_set_status(st);
    ui_router_invalidate();
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    uint32_t h = 2166136261u;
    for (int y = 0; y < UI_BAND_H; ++y) {
        for (int x = 0; x < W; ++x) {
            h = (h ^ (uint32_t)fb[(size_t)y * W + x]) * 16777619u;
        }
    }
    return h;
}

/* The bitmaps the cases walk: none, each bit alone, the three this release
 * sets, all seven, one this panel has no name for, and every bit. */
static const uint16_t k_faults[] = {
    0x00u, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x20u, 0x40u,
    0x11u, 0x50u, 0x51u, 0x7Fu, 0x90u, 0x0100u, 0xFFFFu,
};
#define N_FAULTS (sizeof(k_faults) / sizeof(k_faults[0]))

/*
 * OFF, the default: no HWSTOP for any bitmap, FAULT for every non-zero one
 * with the whole bitmap as its code, and FAULT 10's box as before.
 */
TEST_CASE(off_the_band_is_as_without_the_setting)
{
    hw_stop(false);
    CHECK(!settings_get_bool(SET_HW_STOP));
    for (size_t f = 0; f < N_FAULTS; ++f) {
        ui_bench_status_t st = k_band;
        st.faults = k_faults[f];
        CHECK(!ui_band_hwstop(&st));
        CHECK(!ui_band_chip_rect(&st, UI_BAND_CHIP_HWSTOP, NULL));
        CHECK(!ui_band_help_rect(&st, UI_BAND_CHIP_HWSTOP, NULL));
        CHECK_EQ(ui_band_chip_rect(&st, UI_BAND_CHIP_FAULT, NULL),
                 st.faults != 0u);
    }
    /* FAULT 10 on its warning fill, at the place it has always had. */
    gfx_rect_t r;
    REQUIRE(ui_band_chip_rect(&k_band, UI_BAND_CHIP_FAULT, &r));
    gfx_color_t fill = 0, ink = 0;
    CHECK(ui_band_chip_colors(&k_band, UI_BAND_CHIP_FAULT, &fill, &ink));
    CHECK_EQ(fill, ui_theme_color(UI_C_WARN));
    /* A tap there opens FAULT's box. */
    fresh();
    to_overview();
    ui_router_set_status(&k_band);
    tap(r.x + r.w / 2, r.y + r.h / 2);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_FAULT);
    CHECK(!ui_band_hwstop(NULL));
}

/*
 * ON with the bit: HWSTOP where FAULT 10 stood, its right edge at FAULT's,
 * and no FAULT chip; the bits beside it do not bring FAULT back.
 */
TEST_CASE(on_the_heartbeat_bit_shows_hwstop_in_faults_place)
{
    hw_stop(false);
    gfx_rect_t fault;
    REQUIRE(ui_band_chip_rect(&k_band, UI_BAND_CHIP_FAULT, &fault));
    hw_stop(true);
    for (size_t f = 0; f < N_FAULTS; ++f) {
        ui_bench_status_t st = k_band;
        st.faults = k_faults[f];
        const bool beat = (st.faults & BEAT) != 0u;
        CHECK_EQ(ui_band_hwstop(&st), beat);
        CHECK_EQ(ui_band_chip_rect(&st, UI_BAND_CHIP_HWSTOP, NULL), beat);
        CHECK_EQ(ui_band_chip_rect(&st, UI_BAND_CHIP_FAULT, NULL),
                 st.faults != 0u && !beat);
    }
    gfx_rect_t r;
    REQUIRE(ui_band_chip_rect(&k_band, UI_BAND_CHIP_HWSTOP, &r));
    CHECK_EQ(r.x + r.w, fault.x + fault.w);
    CHECK_EQ(r.y, fault.y);
    CHECK_EQ(r.h, fault.h);
    CHECK(r.w < fault.w);
    /* A tap there opens HWSTOP's box, and the band draws something else
     * than with the setting OFF. */
    fresh();
    to_overview();
    ui_router_set_status(&k_band);
    tap(r.x + r.w / 2, r.y + r.h / 2);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_HWSTOP);
    const uint32_t on = band_hash(&k_band);
    hw_stop(false);
    CHECK(band_hash(&k_band) != on);
    /* The band's key moves with the setting, so the band repaints. */
    const uint32_t key_off = ui_band_key("MENU", false, &k_band, NULL, false,
                                         UI_BAND_GUIDE_NONE);
    hw_stop(true);
    CHECK(ui_band_key("MENU", false, &k_band, NULL, false,
                      UI_BAND_GUIDE_NONE)
          != key_off);
    hw_stop(false);
}

/* Without the heartbeat bit the setting changes no pixel of the band. */
TEST_CASE(on_the_setting_changes_nothing_without_the_heartbeat_bit)
{
    for (size_t f = 0; f < N_FAULTS; ++f) {
        if ((k_faults[f] & BEAT) != 0u) {
            continue;
        }
        ui_bench_status_t st = k_band;
        st.faults = k_faults[f];
        hw_stop(false);
        const uint32_t off = band_hash(&st);
        hw_stop(true);
        CHECK_EQ(band_hash(&st), off);
    }
    hw_stop(false);
}

/* WCAG relative luminance of an RGB565 colour, 0 to 1. */
static double rel_lum(gfx_color_t c)
{
    const double ch[3] = {
        (double)((c >> 11) & 0x1Fu) / 31.0,
        (double)((c >> 5) & 0x3Fu) / 63.0,
        (double)(c & 0x1Fu) / 31.0,
    };
    double l[3];
    for (int i = 0; i < 3; ++i) {
        l[i] = (ch[i] <= 0.03928) ? ch[i] / 12.92
                                  : pow((ch[i] + 0.055) / 1.055, 2.4);
    }
    return 0.2126 * l[0] + 0.7152 * l[1] + 0.0722 * l[2];
}

/* The chip on the danger fill, its text in the dark ink of the bright fills,
 * drawn so, at 4.5:1 or more, in the dark and the light theme. */
TEST_CASE(hwstop_is_red_and_reads_at_4_5_to_1_in_both_themes)
{
    hw_stop(true);
    for (int th = 0; th < (int)UI_THEME_COUNT; ++th) {
        fresh();
        ui_theme_set((ui_theme_id_t)th);
        to_overview();
        ui_router_set_status(&k_band);
        ui_router_invalidate();
        ui_router_render(&cv, 0);
        gfx_color_t fill = 0, ink = 0;
        CHECK(ui_band_chip_colors(&k_band, UI_BAND_CHIP_HWSTOP, &fill, &ink));
        CHECK_EQ(fill, ui_theme_color(UI_C_DANGER));
        CHECK_EQ(ink, ui_theme_color(UI_C_TEXT_ON_LIGHT));
        gfx_rect_t r;
        REQUIRE(ui_band_chip_rect(&k_band, UI_BAND_CHIP_HWSTOP, &r));
        int fill_px = 0, ink_px = 0;
        for (int y = r.y; y < r.y + r.h; ++y) {
            for (int x = r.x + r.h / 2; x < r.x + r.w - r.h / 2; ++x) {
                fill_px += fb[(size_t)y * W + x] == fill;
                ink_px  += fb[(size_t)y * W + x] == ink;
            }
        }
        CHECK(fill_px > 0);
        CHECK(ink_px > 0);
        const double a = rel_lum(fill), b = rel_lum(ink);
        const double ratio = (a > b) ? (a + 0.05) / (b + 0.05)
                                     : (b + 0.05) / (a + 0.05);
        CHECK(ratio >= 4.5);
    }
    ui_theme_set(UI_THEME_DARK);
    hw_stop(false);
}

/*
 * The bit as STATUS reads it: HWSTOP on the first status with it, gone on
 * the first without it, FAULT then back for a bit that stays, and an open
 * HWSTOP box closed with its chip.  No ARM in between.
 */
TEST_CASE(the_chip_comes_and_goes_with_the_bit)
{
    hw_stop(true);
    ui_bench_status_t st = k_band;
    st.faults = 0u;
    fresh();
    to_overview();
    ui_router_set_status(&st);
    CHECK(!ui_band_chip_rect(ui_router_status(), UI_BAND_CHIP_HWSTOP, NULL));
    st.faults = BEAT;
    ui_router_set_status(&st);
    gfx_rect_t r;
    REQUIRE(ui_band_chip_rect(ui_router_status(), UI_BAND_CHIP_HWSTOP, &r));
    tap(r.x + r.w / 2, r.y + r.h / 2);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_HWSTOP);
    ui_router_render(&cv, 0);
    st.faults = 0u;
    ui_router_set_status(&st);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    CHECK(!ui_band_chip_rect(ui_router_status(), UI_BAND_CHIP_HWSTOP, NULL));
    CHECK(!ui_band_chip_rect(ui_router_status(), UI_BAND_CHIP_FAULT, NULL));
    /* The bench stayed SAFE throughout. */
    CHECK(!ui_router_status()->armed);

    /* STORE_OFF beside it: FAULT 40 when the line closes. */
    st.faults = (uint16_t)(BEAT | LINK_FAULT_STORE_OFF);
    ui_router_set_status(&st);
    CHECK(ui_band_chip_rect(ui_router_status(), UI_BAND_CHIP_HWSTOP, NULL));
    CHECK(!ui_band_chip_rect(ui_router_status(), UI_BAND_CHIP_FAULT, NULL));
    st.faults = (uint16_t)LINK_FAULT_STORE_OFF;
    ui_router_set_status(&st);
    CHECK(!ui_band_chip_rect(ui_router_status(), UI_BAND_CHIP_HWSTOP, NULL));
    CHECK(ui_band_chip_rect(ui_router_status(), UI_BAND_CHIP_FAULT, NULL));
    hw_stop(false);
}

/*
 * HWSTOP's box: the button or the cable, and how it clears, in both
 * languages, then the other bits as FAULT's box has them, and not
 * HEARTBEAT's own paragraph.  Taller with each bit beside it.
 */
TEST_CASE(the_hwstop_box_names_the_button_the_cable_and_the_other_bits)
{
    hw_stop(true);
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK(strstr(ui_tr(TX_BH_HWSTOP), "HWSTOP") == NULL);
        CHECK(strstr(ui_tr(TX_BH_HWSTOP), "SAFE") != NULL);
        CHECK(strstr(ui_tr(TX_BH_HWSTOP), "ARM") != NULL);
        CHECK(strstr(ui_tr(TX_BH_HWSTOP_CLEAR), "HWSTOP") != NULL);
        CHECK(strstr(ui_tr(TX_BH_HWSTOP_CLEAR), "ARM") != NULL);
        CHECK_STR_EQ(ui_tr(TX_BAND_HWSTOP), "HWSTOP");
    }
    ui_text_set_language(UI_LANG_DE);
    CHECK(strstr(ui_tr(TX_BH_HWSTOP), "STOP-Taster") != NULL);
    CHECK(strstr(ui_tr(TX_BH_HWSTOP), "Kabel") != NULL);
    ui_text_set_language(UI_LANG_EN);
    CHECK(strstr(ui_tr(TX_BH_HWSTOP), "stop button") != NULL);
    CHECK(strstr(ui_tr(TX_BH_HWSTOP), "cable is broken") != NULL);

    ui_bench_status_t st = k_band;
    gfx_rect_t alone, two, fault;
    REQUIRE(ui_band_help_rect(&st, UI_BAND_CHIP_HWSTOP, &alone));
    st.faults = (uint16_t)(BEAT | LINK_FAULT_LINK_SILENT);
    REQUIRE(ui_band_help_rect(&st, UI_BAND_CHIP_HWSTOP, &two));
    CHECK(two.h > alone.h);
    /* As wide as FAULT's: the panel less 8px each side. */
    CHECK_EQ(alone.w, W - 16);
    CHECK_EQ(alone.x, 8);
    hw_stop(false);
    st.faults = (uint16_t)LINK_FAULT_LINK_SILENT;
    REQUIRE(ui_band_help_rect(&st, UI_BAND_CHIP_FAULT, &fault));
    hw_stop(true);
    st.faults = (uint16_t)(BEAT | LINK_FAULT_LINK_SILENT);
    /* Two paragraphs and LINK_SILENT's, against LINK_SILENT's alone. */
    CHECK(two.h > fault.h);

    /* Drawn: the danger frame's title, and no pixel of the box differs
     * between a box opened with the bits and one that followed them. */
    fresh();
    to_overview();
    ui_router_set_status(&st);
    gfx_rect_t r;
    REQUIRE(ui_band_chip_rect(&st, UI_BAND_CHIP_HWSTOP, &r));
    tap(r.x + r.w / 2, r.y + r.h / 2);
    REQUIRE(ui_router_help() == UI_BAND_CHIP_HWSTOP);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    gfx_rect_t box;
    REQUIRE(ui_router_help_rect(&box));
    int danger = 0;
    for (int x = box.x; x < box.x + box.w; ++x) {
        for (int y = box.y; y < box.y + 24; ++y) {
            danger += fb[(size_t)y * W + x] == ui_theme_color(UI_C_DANGER);
        }
    }
    CHECK(danger > 0);
    hw_stop(false);
}

/*
 * While an operator's STOP stands, while its reset runs and for
 * STOP_LATCH_GRACE_MS after it, the panel withholds the line itself: the
 * bitmap the band gets has no heartbeat bit, so neither FAULT 10 nor HWSTOP
 * shows, and either shows 1ms later.  Across the 32-bit wrap as well.
 */
TEST_CASE(the_stop_latch_grace_hides_hwstop_as_it_hides_fault_10)
{
    const uint32_t done_at[] = { 1000u, 0xFFFFFF00u };
    for (int on = 0; on < 2; ++on) {
        hw_stop(on != 0);
        ui_bench_status_t st = k_band;
        /* Latched: the band shows STOP, and no bit 10 in any form. */
        st.faults = stop_latch_faults(NULL, 0u, true, BEAT);
        st.stop_latched = true;
        CHECK(!ui_band_hwstop(&st));
        CHECK(!ui_band_chip_rect(&st, UI_BAND_CHIP_FAULT, NULL));
        st.stop_latched = false;
        for (size_t k = 0; k < sizeof(done_at) / sizeof(done_at[0]); ++k) {
            stop_latch_t l;
            memset(&l, 0, sizeof(l));
            l.state    = STOP_LATCH_RESUMED;
            st.faults  = stop_latch_faults(&l, done_at[k], false, BEAT);
            CHECK(!ui_band_hwstop(&st));
            /* The reset done at done_at[k]: the grace runs. */
            l.state    = STOP_LATCH_UNLATCHED;
            l.grace    = true;
            l.grace_ms = done_at[k];
            st.faults = stop_latch_faults(&l, done_at[k] + STOP_LATCH_GRACE_MS,
                                          false, BEAT);
            CHECK(!ui_band_hwstop(&st));
            CHECK(!ui_band_chip_rect(&st, UI_BAND_CHIP_FAULT, NULL));
            st.faults = stop_latch_faults(&l,
                                          done_at[k] + STOP_LATCH_GRACE_MS
                                              + 1u,
                                          false, BEAT);
            CHECK_EQ(ui_band_hwstop(&st), on != 0);
            CHECK_EQ(ui_band_chip_rect(&st, UI_BAND_CHIP_FAULT, NULL),
                     on == 0);
        }
    }
    hw_stop(false);
}

/* ----------------------------------------------------- the stored copy */

/** @brief NVS as the panel's store addresses it: one value per key. */
static struct { char key[16]; float v; } s_nvs[SETTING_COUNT];
static int s_nvs_n;

/** @brief Load every key the store holds into its setting's slot. */
static bool nvs_load(float *values, int count)
{
    bool any = false;
    for (int i = 0; i < count; ++i) {
        for (int k = 0; k < s_nvs_n; ++k) {
            if (strcmp(settings_def((setting_id_t)i)->key, s_nvs[k].key)
                == 0) {
                values[i] = s_nvs[k].v;
                any = true;
            }
        }
    }
    return any;
}

/** @brief Write every setting under its key. */
static bool nvs_save(const float *values, int count)
{
    s_nvs_n = 0;
    for (int i = 0; i < count && i < SETTING_COUNT; ++i) {
        snprintf(s_nvs[s_nvs_n].key, sizeof(s_nvs[0].key), "%s",
                 settings_def((setting_id_t)i)->key);
        s_nvs[s_nvs_n].v = values[i];
        ++s_nvs_n;
    }
    return true;
}

static const settings_store_t k_nvs = { nvs_load, nvs_save, NULL, NULL };

/*
 * The key is hw_stop, an NVS key of 15 bytes or fewer that no other setting
 * has.  A store from a release without it starts OFF; ON saved is ON after
 * a restart, and OFF saved is OFF.
 */
TEST_CASE(the_setting_is_kept_by_its_key_and_starts_off)
{
    const setting_def_t *d = settings_def(SET_HW_STOP);
    REQUIRE(d != NULL);
    CHECK_STR_EQ(d->key, "hw_stop");
    CHECK(strlen(d->key) <= 15u);
    CHECK_EQ(d->type, SET_TYPE_BOOL);
    CHECK_EQ(d->cat, SET_CAT_APP);
    CHECK_EQ(d->def, 0.0f);
    for (int i = 0; i < SETTING_COUNT; ++i) {
        if (i != SET_HW_STOP) {
            CHECK(strcmp(settings_def((setting_id_t)i)->key, d->key) != 0);
        }
    }

    /* A store of an older release: theme only. */
    s_nvs_n = 1;
    snprintf(s_nvs[0].key, sizeof(s_nvs[0].key), "theme");
    s_nvs[0].v = 1.0f;
    settings_set_store(&k_nvs);
    settings_init();
    CHECK(!settings_get_bool(SET_HW_STOP));

    settings_set(SET_HW_STOP, 1.0f);
    CHECK(settings_save());
    settings_set(SET_HW_STOP, 0.0f);
    settings_init();
    CHECK(settings_get_bool(SET_HW_STOP));
    CHECK(!settings_dirty());

    settings_set(SET_HW_STOP, 0.0f);
    CHECK(settings_save());
    settings_set(SET_HW_STOP, 1.0f);
    settings_init();
    CHECK(!settings_get_bool(SET_HW_STOP));
    settings_set_store(NULL);
    settings_init();
}

/* The row is on SETUP > PANEL, after Demo mode, labelled in both
 * languages. */
TEST_CASE(the_setting_is_a_panel_row)
{
    CHECK_EQ(settings_def(SET_HW_STOP)->cat, SET_CAT_APP);
    CHECK_EQ(SET_HW_STOP, SET_DEMO_MODE + 1);
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(ui_setting_label(SET_HW_STOP), "Hardware stop");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(ui_setting_label(SET_HW_STOP), "Hardware-STOP");
    ui_text_set_language(UI_LANG_EN);
}

/*
 * The widest row with HWSTOP, a clock of 99 hours, ARMED MOTOR and the
 * other six bits, starts right of MOTOR & ESC's tag (x 268) in both
 * languages, and HWSTOP's box stays clear of STOP and above the alert strip.
 */
TEST_CASE(the_row_and_the_box_fit_in_both_languages)
{
    hw_stop(true);
    const gfx_rect_t tag = ui_home_tag_rect(ui_router_title(SCREEN_MOTOR));
    const gfx_rect_t stop = ui_band_stop_rect();
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t f = 0; f < N_FAULTS; ++f) {
            for (int armed = 0; armed < 2; ++armed) {
                ui_bench_status_t st = k_band;
                st.faults      = (uint16_t)(k_faults[f] | BEAT);
                st.armed       = armed != 0;
                st.armed_roles = UI_ARMED_MOTOR;
                st.run_seconds = 99u * 3600u + 59u * 60u + 59u;
                int left = W;
                for (int c = UI_BAND_CHIP_NONE + 1; c < UI_BAND_CHIP_COUNT;
                     ++c) {
                    gfx_rect_t r;
                    if (ui_band_chip_rect(&st, (ui_band_chip_t)c, &r)
                        && r.x < left) {
                        left = r.x;
                    }
                }
                if (left - 2 < tag.x + tag.w) {
                    T_FAIL("language %d, faults %04X: chips from x %d", lang,
                           (unsigned)st.faults, left);
                }
                gfx_rect_t b;
                REQUIRE(ui_band_help_rect(&st, UI_BAND_CHIP_HWSTOP, &b));
                CHECK(b.x + b.w <= W && b.x >= 0);
                CHECK(b.y >= stop.y + stop.h);
                if (b.y + b.h > UI_ALERT_Y) {
                    T_FAIL("language %d, faults %04X: box to y %d", lang,
                           (unsigned)st.faults, b.y + b.h);
                }
            }
        }
        /* The label fits its chip: the chip's width is the label's. */
        gfx_rect_t r;
        REQUIRE(ui_band_chip_rect(&k_band, UI_BAND_CHIP_HWSTOP, &r));
        CHECK_EQ(r.w, 22 + gfx_text_width(UI_FONT_BAND, "HWSTOP", 1) + 4);
    }
    ui_text_set_language(UI_LANG_EN);
    hw_stop(false);
}

/* ------------------------------------------------------ the IOmcu's side */

/*
 * STATUS's heartbeat bit follows the line: set when it goes quiet past
 * HEARTBEAT_MAX_GAP_MS, clear again HEARTBEAT_GOOD_RUN good intervals after
 * it edges again, with no CLEAR written.  The arm latch the drop set stays
 * until a CLEAR.  The interval of 20ms is the panel's.
 */
TEST_CASE(the_iomcu_clears_the_bit_with_the_line_and_keeps_the_arm_latch)
{
    const uint32_t starts[] = { 0u, 0xFFFFFF00u };
    for (size_t k = 0; k < sizeof(starts) / sizeof(starts[0]); ++k) {
        heartbeat_mon_t beat;
        link_dev_t dev;
        uint16_t control[LINK_CT_COUNT];
        memset(control, 0, sizeof(control));
        heartbeat_mon_init(&beat);
        link_dev_init(&dev, NULL, 0, NULL, starts[k]);
        uint32_t t = starts[k];
        link_dev_clear_failsafe(&dev, t);
        uint16_t state = 0u, faults = 0u;
        /* The line edges: trusted, and STATUS has no bit. */
        for (int i = 0; i <= (int)HEARTBEAT_GOOD_RUN; ++i, t += 20u) {
            dev.last_request_ms = t;
            (void)safety_gate_step(&beat, true, &dev, control, t);
        }
        safety_gate_status(&beat, &dev, control, &state, &faults);
        CHECK_EQ(faults & BEAT, 0u);
        CHECK(!link_dev_arm_latched(&dev));
        /* The button opens the line: quiet past the gap. */
        const uint32_t opened = t;
        for (; t - opened <= HEARTBEAT_MAX_GAP_MS; t += 5u) {
            dev.last_request_ms = t;
            (void)safety_gate_step(&beat, false, &dev, control, t);
        }
        safety_gate_status(&beat, &dev, control, &state, &faults);
        CHECK_EQ(faults & BEAT, BEAT);
        CHECK_EQ(state, (uint16_t)LINK_STATE_FAILSAFE);
        CHECK(link_dev_arm_latched(&dev));
        /* Closed: the first edge is a slow interval, then the good run. */
        dev.last_request_ms = t;
        (void)safety_gate_step(&beat, true, &dev, control, t);
        for (uint32_t i = 1u; i < HEARTBEAT_GOOD_RUN; ++i) {
            t += 20u;
            dev.last_request_ms = t;
            (void)safety_gate_step(&beat, true, &dev, control, t);
            safety_gate_status(&beat, &dev, control, &state, &faults);
            CHECK_EQ(faults & BEAT, BEAT);
        }
        t += 20u;
        dev.last_request_ms = t;
        (void)safety_gate_step(&beat, true, &dev, control, t);
        safety_gate_status(&beat, &dev, control, &state, &faults);
        CHECK_EQ(faults & BEAT, 0u);
        CHECK_EQ(state, (uint16_t)LINK_STATE_IDLE);
        /* No CLEAR yet: ARM is still refused. */
        CHECK(link_dev_arm_latched(&dev));
        CHECK(!safety_gate_may_arm(&beat, &dev));
    }
}

/* ------------------------------------------------- the STATUS read rate */

/* Slow: due at STATUS_POLL_MS after the last read and not 1ms before,
 * across the 32-bit wrap.  Fast: due at every call.  The read's time is
 * kept only when due. */
TEST_CASE(status_is_due_every_500ms_and_at_every_poll_while_fast)
{
    const uint32_t starts[] = { 0u, 1000u, 0xFFFFFF00u };
    for (size_t k = 0; k < sizeof(starts) / sizeof(starts[0]); ++k) {
        uint32_t last = starts[k];
        CHECK(!status_poll_due(&last, starts[k] + STATUS_POLL_MS - 1u,
                               false));
        CHECK_EQ(last, starts[k]);
        CHECK(status_poll_due(&last, starts[k] + STATUS_POLL_MS, false));
        CHECK_EQ(last, starts[k] + STATUS_POLL_MS);
        CHECK(status_poll_due(&last, last + 1u, true));
        CHECK(status_poll_due(&last, last, true));
        CHECK(!status_poll_due(&last, last + STATUS_POLL_MS - 1u, false));
    }
    CHECK(!status_poll_due(NULL, 1000u, true));
    CHECK_EQ(STATUS_POLL_MS, 500u);
}

/* Fast only with the setting ON and bit 10 in the band's bitmap: not with
 * the setting OFF, not for other bits, not under the STOP latch or its
 * grace, and by the rule the band's chip follows. */
TEST_CASE(only_hwstop_shown_reads_fast)
{
    for (size_t f = 0; f < N_FAULTS; ++f) {
        const uint16_t b = k_faults[f];
        CHECK(!status_poll_fast(false, b));
        CHECK_EQ(status_poll_fast(true, b), (b & BEAT) != 0u);
        for (int on = 0; on < 2; ++on) {
            hw_stop(on != 0);
            ui_bench_status_t st = k_band;
            st.faults = b;
            CHECK_EQ(ui_band_hwstop(&st), status_poll_fast(on != 0, b));
        }
    }
    CHECK(!status_poll_fast(true, stop_latch_faults(NULL, 0u, true, BEAT)));
    stop_latch_t l;
    memset(&l, 0, sizeof(l));
    l.grace    = true;
    l.grace_ms = 1000u;
    CHECK(!status_poll_fast(true, stop_latch_faults(&l, 1000u
                                                        + STOP_LATCH_GRACE_MS,
                                                    false, BEAT)));
    CHECK(status_poll_fast(true, stop_latch_faults(&l, 1001u
                                                       + STOP_LATCH_GRACE_MS,
                                                   false, BEAT)));
    hw_stop(false);
}

/** @brief One bench poll's STATUS read in the model: when and what. */
typedef struct {
    uint32_t t;      /**< the read, ms                          */
    uint16_t faults; /**< STATUS's bitmap at that moment        */
} st_read_t;

/**
 * @brief The panel's polls against the IOmcu's monitor, 1ms a step.
 *
 * The panel edges the line every 20ms while it is closed; the line is open
 * from @p open_at to @p close_at.  The control task looks every 5ms and
 * polls 50ms or more after the last poll; the bench read takes 3ms and the
 * STATUS read, when status_poll_due() says so, comes after it, as in
 * poll_far_end().  Every read is recorded.
 * @param hw_stop  SET_HW_STOP.
 * @param t0       The start, ms; the wrap is crossed from 0xFFFFF000.
 * @param open_at  The line opens, ms after @p t0.
 * @param close_at The line closes, ms after @p t0.
 * @param end      The run's length, ms.
 * @param out      The reads, at most @p max.
 * @param max      Room in @p out.
 * @return The reads recorded.
 */
static int run_polls(bool hw_stop, uint32_t t0, uint32_t open_at,
                     uint32_t close_at, uint32_t end, st_read_t *out,
                     int max)
{
    heartbeat_mon_t beat;
    link_dev_t dev;
    uint16_t control[LINK_CT_COUNT];
    memset(control, 0, sizeof(control));
    heartbeat_mon_init(&beat);
    link_dev_init(&dev, NULL, 0, NULL, t0);
    link_dev_clear_failsafe(&dev, t0);
    uint32_t last_poll = t0, last_status = t0;
    uint16_t faults = 0u;   /* the bitmap of the last read, as the band has */
    int n = 0;
    for (uint32_t i = 0; i < end; ++i) {
        const uint32_t t = t0 + i;
        const bool closed = i < open_at || i >= close_at;
        dev.last_request_ms = t;
        (void)safety_gate_step(&beat, closed && i % 20u == 0u, &dev, control,
                               t);
        if (i % 5u == 0u && (uint32_t)(t - last_poll) >= 50u) {
            last_poll = t;
            /* The bench read; the STATUS read is decided 3ms later. */
            const uint32_t at = t + 3u;
            if (status_poll_due(&last_status, at,
                                status_poll_fast(hw_stop, faults))) {
                uint16_t state = 0u;
                safety_gate_status(&beat, &dev, control, &state, &faults);
                if (n < max) {
                    out[n].t      = at;
                    out[n].faults = faults;
                }
                ++n;
            }
        }
    }
    return n < max ? n : max;
}

/*
 * ON: STATUS every 500 to 555ms until a read carries bit 10, then at every
 * poll, 50 to 55ms apart, until the first read without it; that read comes
 * 160ms or less after the line closes, and the next STATUS_POLL_MS or more
 * after it.  The first edge after the close lands 0 to 19ms later, so every
 * phase of the panel's 20ms is covered, and the run crosses the wrap.
 */
TEST_CASE(the_reads_go_fast_on_the_edge_and_slow_at_the_clear)
{
    static st_read_t r[256];
    const uint32_t t0s[] = { 0u, 0xFFFFF000u };
    uint32_t worst = 0u, best = UINT32_MAX;
    for (size_t k = 0; k < sizeof(t0s) / sizeof(t0s[0]); ++k) {
        for (uint32_t ph = 0; ph < 60u; ++ph) {
            const uint32_t open_at = 2000u + ph, close_at = 5000u + ph;
            const int n = run_polls(true, t0s[k], open_at, close_at, 8000u, r,
                                    256);
            int first_set = -1, first_clear = -1;
            for (int i = 0; i < n; ++i) {
                if (first_set < 0 && (r[i].faults & BEAT) != 0u) {
                    first_set = i;
                }
                if (first_set >= 0 && first_clear < 0
                    && (r[i].faults & BEAT) == 0u) {
                    first_clear = i;
                }
            }
            REQUIRE(first_set > 0 && first_clear > first_set + 1);
            REQUIRE(first_clear + 1 < n);
            for (int i = 1; i < n; ++i) {
                const uint32_t gap = r[i].t - r[i - 1].t;
                const bool fast = i > first_set && i <= first_clear;
                if (fast ? (gap < 50u || gap > 55u)
                         : (gap < STATUS_POLL_MS || gap > 555u)) {
                    T_FAIL("phase %u read %d: %ums after the last", ph, i,
                           gap);
                    return;
                }
            }
            const uint32_t after = r[first_clear].t - (t0s[k] + close_at);
            worst = after > worst ? after : worst;
            best  = after < best ? after : best;
            /* The fast reads stop at the clear. */
            CHECK(r[first_clear + 1].t - r[first_clear].t >= STATUS_POLL_MS);
        }
    }
    CHECK(worst <= 160u);
    CHECK(best >= 80u);
}

/* OFF: the same press and release, every read 500 to 555ms apart, and the
 * clear read up to 100 + 555ms after the close. */
TEST_CASE(off_the_reads_stay_at_500ms_through_the_fault)
{
    static st_read_t r[64];
    for (uint32_t ph = 0; ph < 60u; ph += 7u) {
        const int n = run_polls(false, 0u, 2000u + ph, 5000u + ph, 8000u, r,
                                64);
        REQUIRE(n > 10);
        bool saw = false;
        for (int i = 1; i < n; ++i) {
            const uint32_t gap = r[i].t - r[i - 1].t;
            CHECK(gap >= STATUS_POLL_MS && gap <= 555u);
            saw = saw || (r[i].faults & BEAT) != 0u;
        }
        CHECK(saw);
    }
}

int main(void)
{
    RUN(off_the_band_is_as_without_the_setting);
    RUN(on_the_heartbeat_bit_shows_hwstop_in_faults_place);
    RUN(on_the_setting_changes_nothing_without_the_heartbeat_bit);
    RUN(hwstop_is_red_and_reads_at_4_5_to_1_in_both_themes);
    RUN(the_chip_comes_and_goes_with_the_bit);
    RUN(the_hwstop_box_names_the_button_the_cable_and_the_other_bits);
    RUN(the_stop_latch_grace_hides_hwstop_as_it_hides_fault_10);
    RUN(the_setting_is_kept_by_its_key_and_starts_off);
    RUN(the_setting_is_a_panel_row);
    RUN(the_row_and_the_box_fit_in_both_languages);
    RUN(the_iomcu_clears_the_bit_with_the_line_and_keeps_the_arm_latch);
    RUN(status_is_due_every_500ms_and_at_every_poll_while_fast);
    RUN(only_hwstop_shown_reads_fast);
    RUN(the_reads_go_fast_on_the_edge_and_slow_at_the_clear);
    RUN(off_the_reads_stay_at_500ms_through_the_fault);
    return test_summary("band_hwstop");
}
