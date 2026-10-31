/**
 * @file render_hitec.c
 * @brief The Hitec programmer's views for render_screen.c (render_hitec.h).
 *
 * The bench is hitec_prog_rig.h's: the panel's link module, the IOmcu's
 * HITEC page and the servo model, the control task's pass every 5ms and
 * the render loop's frame every 16ms feeding the overlay's model.  The
 * taps go through the router, as a finger's do.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos these views show.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_hitec.h"

/* The rig's checks come with the suites' harness (greatest.h), whose
 * runner this file does not call. */
#pragma GCC diagnostic ignored "-Wunused-function"
#include "hitec_prog_rig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hitec_card.h"
#include "servo_screen.h"
#include "supply_screen.h"
#include "servo_spots.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_textkey.h"

/**
 * @brief A tap at (@p x, @p y), panel pixels.
 * @param x the x
 * @param y the y
 */
static void htap_at(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };

    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    ui_router_tick(0.02f);
}

/**
 * @brief A tap on an overlay control, body pixels.
 * @param r the control; one of no size names none and ends the render
 */
static void htap(gfx_rect_t r)
{
    if (r.w <= 0 || r.h <= 0) {
        fprintf(stderr, "render_hitec: a tap names no control\n");
        exit(1);
    }
    htap_at(r.x + r.w / 2, UI_BAND_H + r.y + r.h / 2);
}

/**
 * @brief The hold page's button held 2.1s, the bench running meanwhile.
 */
static void hhold(void)
{
    const gfx_rect_t r = hitec_panel_apply_rect();
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1,
                                   .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(UI_BAND_H + r.y
                                                  + r.h / 2),
                                   .strength = 40 } };

    ui_router_event(&e);
    for (int i = 0; i < 105; ++i) {
        ui_router_tick(0.02f);
        prun(20u);
    }
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/**
 * @brief Run the bench until the model is in @p s, or end the render.
 * @param s the state
 */
static void hwait(hitec_prog_state_t s)
{
    for (unsigned i = 0; i < 60000u && pstate() != s; ++i) {
        pms();
    }
    if (pstate() != s) {
        fprintf(stderr, "render_hitec: state %d not reached, at %d\n",
                (int)s, (int)pstate());
        exit(1);
    }
    ui_router_tick(0.02f);
}

/**
 * @brief The SERIES row's list, and its entry @p i taken; -1 leaves it
 *        open.
 * @param i the entry
 */
static void hseries(int i)
{
    htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 1, 0));
    if (i >= 0) {
        htap(hitec_panel_choice_rect(i));
    }
}

/**
 * @brief CONNECT, and the bench run until the port is held and idle.
 */
static void hconnect(void)
{
    htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 0, 0));
    for (unsigned i = 0; i < 60000u
                         && (pstate() == HITEC_PROG_OPENING
                             || pstate() == HITEC_PROG_ENTERING
                             || pstate() == HITEC_PROG_READING);
         ++i) {
        pms();
    }
    hwait(HITEC_PROG_HELD);
    /* The set points the IOmcu's SUPPLY page took from the session, as
     * supply_link_adopted() hands them to the screen. */
    if (g_hw.v.online) {
        supply_screen_adopt((float)g_hw.v.set_mv / 1000.0f,
                            (float)g_hw.v.set_ma / 1000.0f);
    }
}

/**
 * @brief The row of @p page whose label is @p label, tapped.
 * @param page  the tab
 * @param label the row's label
 */
static void hrow(int page, const char *label)
{
    htap(hitec_panel_tab_rect(page));
    for (int row = 0; row < 6; ++row) {
        const char *l = hitec_panel_label(page, row, 0);

        if (l != NULL && strcmp(l, label) == 0) {
            htap(hitec_panel_value_rect(page, row, 0));
            return;
        }
    }
    fprintf(stderr, "render_hitec: no row %s\n", label);
    exit(1);
}

/**
 * @brief The list's entry @p label tapped.
 * @param label the entry
 */
static void hpick(const char *label)
{
    for (int i = 0; hitec_panel_choice_label(i) != NULL; ++i) {
        if (strcmp(hitec_panel_choice_label(i), label) == 0) {
            htap(hitec_panel_choice_rect(i));
            return;
        }
    }
    fprintf(stderr, "render_hitec: no entry %s\n", label);
    exit(1);
}

/**
 * @brief The edit of each write view: D series SOFT START 60% and SPEED
 *        50%, classic SPEED 50%.
 */
static void hedits(void)
{
    hitec_prog_t *p = hitec_panel_model();

    if (hitec_prog_series(p) == (uint8_t)HITEC_CLASSIC) {
        (void)hitec_prog_edit(p, HITEC_P_C_SPEED, 50);
    } else {
        (void)hitec_prog_edit(p, HITEC_P_D_SOFT_START, 60);
        (void)hitec_prog_edit(p, HITEC_P_D_SPEED, 50);
    }
}

/**
 * @brief The views of the write side, from a servo read.
 * @param v the view's name past "servo-hitec"
 * @return false for a view of another kind
 */
static bool write_view(const char *v)
{
    hitec_prog_t *p = hitec_panel_model();
    const gfx_rect_t write = hitec_panel_value_rect(HITEC_PANEL_SERVO, 4, 1);

    if (strcmp(v, "-list") == 0 || strcmp(v, "-edit") == 0) {
        hrow(HITEC_PANEL_MOTION, TR(HP_SOFTSTART));
        if (strcmp(v, "-edit") == 0) {
            hpick("60%");
        }
        return true;
    }
    if (strcmp(v, "-keypad") == 0 || strcmp(v, "-soft") == 0) {
        hrow(HITEC_PANEL_MOTION, TR(HP_SPEED));
        if (strcmp(v, "-soft") == 0) {
            /* 20% typed: CHECK THIS CHANGE. */
            htap(ui_keypad_key_rect(hitec_panel_keypad(), UI_KEY_2));
            htap(ui_keypad_key_rect(hitec_panel_keypad(), UI_KEY_0));
            htap(ui_keypad_key_rect(hitec_panel_keypad(), UI_KEY_OK));
        }
        return true;
    }
    if (strcmp(v, "-enable") == 0) {
        hedits();
        htap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
        htap(write);
        return true;
    }
    if (strcmp(v, "-write") == 0 || strcmp(v, "-classic-write") == 0
        || strcmp(v, "-writing") == 0 || strcmp(v, "-written") == 0
        || strcmp(v, "-verify") == 0) {
        hedits();
        (void)hitec_prog_write_enable(p, true);
        htap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
        htap(write);
        if (strcmp(v, "-write") == 0 || strcmp(v, "-classic-write") == 0) {
            return true;
        }
        if (strcmp(v, "-writing") == 0) {
            const gfx_rect_t r = hitec_panel_apply_rect();
            touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                                .point = { .id = 1,
                                           .x = (int16_t)(r.x + r.w / 2),
                                           .y = (int16_t)(UI_BAND_H + r.y
                                                          + r.h / 2),
                                           .strength = 40 } };

            ui_router_event(&e);
            for (int i = 0; i < 105; ++i) {
                ui_router_tick(0.02f);
                prun(1u);
            }
            e.type = TOUCH_EVENT_UP;
            ui_router_event(&e);
            hwait(HITEC_PROG_WRITING);
            prun(40u);
            ui_router_tick(0.02f);
            return true;
        }
        hhold();
        hwait(HITEC_PROG_HELD);
        return true;
    }
    if (strcmp(v, "-restore") == 0) {
        hedits();
        (void)hitec_prog_write_enable(p, true);
        htap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
        htap(write);
        hhold();
        hwait(HITEC_PROG_HELD);
        htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 1, 1));
        hwait(HITEC_PROG_HELD);
        htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 4, 0));
        return true;
    }
    if (strcmp(v, "-classic-reread") == 0) {
        /* The station's case: the second read of cell 0x17 answers 0x04
         * where the first read 0x05. */
        hedits();
        (void)hitec_prog_write_enable(p, true);
        htap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
        htap(write);
        g_drv.eeprom[0x17] = (uint8_t)(g_drv.eeprom[0x17] - 1u);
        hhold();
        hwait(HITEC_PROG_HELD);
        return true;
    }
    if (strcmp(v, "-classic-repair") == 0) {
        (void)hitec_prog_write_enable(p, true);
        htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 2, 0));
        hpick(TR(HP_MORE_REPAIR));
        return true;
    }
    return false;
}

/**
 * @brief The card's answer: 4 frames, then drawn.
 */
static void hsettle(void)
{
    prun(64u);
    ui_router_tick(0.02f);
}

/**
 * @brief A D485HW file under @p name, the extension included, with SOFT
 *        START 60% and SPEED 50%.
 * @param name the file's name
 */
static void hfile(const char *name)
{
    hitec_image_t img;

    d485(&img);
    (void)hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SOFT_START, 60);
    (void)hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 50);
    files_put_image(name, &img);
}

/**
 * @brief FILES tapped, and the card's list.
 */
static void hfiles(void)
{
    htap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 1, 0));
    hsettle();
}

/**
 * @brief The file @p name of the open list tapped, and the card's answer.
 * @param name the file's base name
 */
static void hload(const char *name)
{
    hfiles();
    for (int i = 0; hitec_panel_choice_label(i) != NULL; ++i) {
        const char *l = hitec_panel_choice_label(i);

        if (strncmp(l, name, strlen(name)) == 0 && l[strlen(name)] == ' ') {
            htap(hitec_panel_choice_rect(i));
            hsettle();
            return;
        }
    }
    fprintf(stderr, "render_hitec: no file %s\n", name);
    exit(1);
}

/**
 * @brief The keyboard's keys for @p s, after CLR, then OK.
 * @param s the name: A to Z, 0 to 9, "-", "_" and "."
 */
static void htype(const char *s)
{
    static const char chars[] = "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM_.";
    const ui_textkey_t *k = hitec_panel_textkey();

    htap(ui_textkey_key_rect(k, UI_TK_CLR));
    for (; *s != '\0'; ++s) {
        htap(ui_textkey_key_rect(k, (int)(strchr(chars, *s) - chars)));
    }
    htap(ui_textkey_key_rect(k, UI_TK_OK));
}

/**
 * @brief The edits of the write views, the switch on, and the write page.
 */
static void hwrite_page(void)
{
    hedits();
    (void)hitec_prog_write_enable(hitec_panel_model(), true);
    htap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 4, 1));
}

/**
 * @brief The views of the servo files, from a servo read, a card in.
 * @param v the view's name past "servo-hitec"
 * @return false for a view of another kind
 */
static bool files_view(const char *v)
{
    hitec_image_t img;

    if (strcmp(v, "-files") == 0 || strcmp(v, "-files-next") == 0) {
        static const char *const k_names[] = {
            "ELEV.TXT", "HITEC001.TXT", "HITEC002.TXT", "RUDDER.TXT",
            "WING-L.TXT", "WING-R.TXT",
        };

        for (size_t i = 0; i < sizeof(k_names) / sizeof(k_names[0]); ++i) {
            hfile(k_names[i]);
        }
        hs5645(&img);
        files_put_image("THROTTLE.TXT", &img);
        hfiles();
        if (strcmp(v, "-files-next") == 0) {
            hpick(TR(KP_FILES_NEXT));
            hsettle();
        }
        return true;
    }
    if (strcmp(v, "-files-none") == 0) {
        hfiles();
        return true;
    }
    if (strcmp(v, "-name") == 0 || strcmp(v, "-name-exists") == 0) {
        hfile("WING.TXT");
        hfiles();
        hpick(TR(KP_FILE_SAVE));
        if (strcmp(v, "-name-exists") == 0) {
            htype("WING");
            hsettle();
        }
        return true;
    }
    if (strcmp(v, "-file-loaded") == 0) {
        d485(&img);
        (void)hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SOFT_START, 60);
        (void)hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 50);
        (void)hitec_image_put_reg(&img, 0x64, 7u);
        files_put_image("WING-L.TXT", &img);
        hload("WING-L");
        return true;
    }
    if (strcmp(v, "-file-check") == 0 || strcmp(v, "-file-model") == 0) {
        d485(&img);
        if (strcmp(v, "-file-model") == 0) {
            (void)hitec_image_put_reg(&img, HITEC_D_REG_MODEL, 34999u);
            (void)hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 50);
        } else {
            (void)hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_LEFT_END,
                                  3300);
        }
        files_put_image("WING-L.TXT", &img);
        hload("WING-L");
        return true;
    }
    if (strcmp(v, "-file-warnings") == 0
        || strcmp(v, "-file-warnings-next") == 0) {
        /* Another model past all 7 rules, from a servo with a dead band
         * of 3 and overload protection at 30% (render_hitec_view()). */
        hitec_image_t sv;

        d485(&sv);
        (void)hitec_param_set(&sv, HITEC_LAYOUT_D, HITEC_P_D_DEAD_BAND, 3);
        (void)hitec_param_set(&sv, HITEC_LAYOUT_D, HITEC_P_D_OVERLOAD, 30);
        img = sv;
        (void)hitec_image_put_reg(&img, HITEC_D_REG_MODEL, 34999u);
        (void)hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 10);
        (void)hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_DEAD_BAND, 1);
        (void)hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_OVERLOAD, 0);
        (void)hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_LEFT_END, 3000);
        (void)hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_RIGHT_END,
                              13300);
        (void)hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_NEUTRAL, 7000);
        (void)hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_DIRECTION,
                              HITEC_CCW);
        (void)hitec_param_set(&img, HITEC_LAYOUT_D, HITEC_P_D_FAILSAFE,
                              2000);
        files_put_image("WING-L.TXT", &img);
        hload("WING-L");
        if (strcmp(v, "-file-warnings-next") == 0) {
            hhold();
        }
        return true;
    }
    if (strcmp(v, "-file-series") == 0) {
        hs5645(&img);
        files_put_image("THROTTLE.TXT", &img);
        hload("THROTTLE");
        return true;
    }
    if (strcmp(v, "-file-damaged") == 0) {
        char text[HITEC_CARD_FILE_MAX];
        size_t len;

        d485(&img);
        len = hitec_card_file_text(&img, false, text, sizeof(text));
        text[50] = text[50] == '0' ? '1' : '0';
        files_put("WING-L.TXT", text, len);
        hload("WING-L");
        return true;
    }
    if (strcmp(v, "-backup-write") == 0 || strcmp(v, "-backup-saving") == 0) {
        hwrite_page();
        if (strcmp(v, "-backup-saving") == 0) {
            g_files_slow = true;
            hhold();
            hwait(HITEC_PROG_BACKUP);
        }
        return true;
    }
    if (strcmp(v, "-backup-file") == 0 || strcmp(v, "-restore-backup") == 0
        || strcmp(v, "-restore-page") == 0) {
        hwrite_page();
        hhold();
        hwait(HITEC_PROG_HELD);
        if (strcmp(v, "-backup-file") != 0) {
            htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 4, 0));
        }
        if (strcmp(v, "-restore-page") == 0) {
            hpick(TR(KP_FROM_SESSION));
        }
        return true;
    }
    if (strcmp(v, "-other-servo") == 0) {
        /* The backup file HITEC001 saved by a write, then another D485HW
         * on the port, dead band 4, and READ ALL. */
        hwrite_page();
        hhold();
        hwait(HITEC_PROG_HELD);
        servo_d485();
        g_drv.dreg[0x4Eu] = 4u;
        htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 1, 1));
        for (unsigned i = 0; i < 60000u && pstate() == HITEC_PROG_READING;
             ++i) {
            pms();
        }
        hwait(HITEC_PROG_HELD);
        return true;
    }
    if (strcmp(v, "-nofile") == 0) {
        g_card = false;
        prun(20u);
        hwrite_page();
        hhold();
        hwait(HITEC_PROG_HELD);
        return true;
    }
    if (strcmp(v, "-classic-file-backup") == 0) {
        /* FACTORY DEFAULT changes cell 0x1D; the backup file HITEC001 of
         * the servo as found brings it back. */
        (void)hitec_prog_write_enable(hitec_panel_model(), true);
        htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 2, 0));
        hpick(TR(HP_MORE_FACTORY));
        hhold();
        hwait(HITEC_PROG_HELD);
        hload("HITEC001");
        return true;
    }
    if (strcmp(v, "-classic-more-factory") == 0
        || strcmp(v, "-classic-factory") == 0
        || strcmp(v, "-classic-factory-done") == 0
        || strcmp(v, "-classic-factory-cell") == 0) {
        if (strcmp(v, "-classic-more-factory") != 0) {
            (void)hitec_prog_write_enable(hitec_panel_model(), true);
        }
        htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 2, 0));
        if (strcmp(v, "-classic-more-factory") != 0) {
            hpick(TR(HP_MORE_FACTORY));
        }
        if (strcmp(v, "-classic-factory-done") == 0) {
            hhold();
            hwait(HITEC_PROG_HELD);
        }
        return true;
    }
    return false;
}

/**
 * @brief Whether @p v is one of the views of the classic HS-5645MG.
 * @param v the view's name past "servo-hitec"
 * @return true for one
 */
static bool classic_view(const char *v)
{
    return strncmp(v, "-classic", 8) == 0 || strcmp(v, "-nopd") == 0;
}

void render_hitec_view(const char *view)
{
    const char *v = view + strlen("servo-hitec");
    const bool c_view = classic_view(v);

    rig_at(strcmp(v, "-nopd") == 0 ? SUPPLY_NONE : SUPPLY_ON, true);
    if (c_view) {
        servo_hs5645();
    } else {
        servo_d485();
    }
    if (strncmp(v, "-file-warnings", 14) == 0) {
        hitec_image_t sv;

        d485(&sv);
        (void)hitec_param_set(&sv, HITEC_LAYOUT_D, HITEC_P_D_DEAD_BAND, 3);
        (void)hitec_param_set(&sv, HITEC_LAYOUT_D, HITEC_P_D_OVERLOAD, 30);
        for (unsigned i = 0; i < 128u; ++i) {
            g_drv.dreg[2u * i] = hitec_image_reg(&sv, (uint8_t)(2u * i));
        }
    }
    ui_router_goto(SCREEN_SERVO);
    /* The tester on PROG PORT 1 at GP13, as the control task reports it. */
    servo_screen_set_drive(1u, PIN, true);
    htap_at(SV_TAB_PROGRAM_X, UI_BAND_H + SV_TAB_Y);
    htap_at(SV_PROG_ROW_X, UI_BAND_H + SV_PROG_ROW_Y(1));
    prun(50u);
    ui_router_tick(0.02f);
    if (!hitec_panel_is_open()) {
        fprintf(stderr, "render_hitec: the overlay did not open\n");
        exit(1);
    }
    if (v[0] == '\0') {
        return;
    }
    if (strcmp(v, "-series") == 0) {
        hseries(-1);
        return;
    }
    if (strcmp(v, "-more") == 0) {
        htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 2, 0));
        return;
    }
    if (strcmp(v, "-rx") == 0) {
        /* MORE's third entry in PWM: Hitec D receive's list. */
        htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 2, 0));
        htap(hitec_panel_choice_rect(2));
        return;
    }
    if (strcmp(v, "-other") == 0 || strcmp(v, "-release") == 0
        || strcmp(v, "-released") == 0) {
        /* The bench console's session on port 1, as the panel sees it. */
        (void)open_as(CH, HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
        hwait(HITEC_PROG_OTHER);
        if (strcmp(v, "-other") == 0) {
            return;
        }
        htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 0, 0));
        if (strcmp(v, "-release") == 0) {
            return;
        }
        hhold();
        hwait(HITEC_PROG_PWM);
        return;
    }
    if (c_view) {
        hseries(1);
    }
    if (strcmp(v, "-classic-factory-cell") == 0
        || strcmp(v, "-classic-file-backup") == 0) {
        /* The factory block equal to the settings but for cell 0x1D. */
        for (unsigned c = 0; c < 0x24u; ++c) {
            g_drv.eeprom[0x30u + c] = g_drv.eeprom[c];
        }
        g_drv.eeprom[0x30u + 0x1Du] ^= 0x01u;
    }
    if (strcmp(v, "-classic-sum") == 0 || strcmp(v, "-classic-repair") == 0) {
        /* A balance one off: the sum is wrong. */
        g_drv.eeprom[HITEC_C_SUM_LAST] ^= 0x01u;
    }
    if (strcmp(v, "-verify") == 0 || strcmp(v, "-restore") == 0) {
        /* A servo that takes no write. */
        g_drv.d_drop = true;
    }
    if (strcmp(v, "-nopd") == 0) {
        htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 0, 0));
        prun(50u);
        return;
    }
    if (strcmp(v, "-reading") == 0) {
        htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 0, 0));
        hwait(HITEC_PROG_READING);
        prun(160u);
        ui_router_tick(0.02f);
        return;
    }
    if (strncmp(v, "-restore-", 9) == 0) {
        /* The operator's set points before the session. */
        settings_set(SET_SUPPLY_V_MAX, 8.4f);
        supply_screen_limits_changed();
        supply_screen_put(strcmp(v, "-restore-hv") == 0 ? 7.4f : 5.5f, 2.0f);
    }
    hconnect();
    if (strcmp(v, "-read") == 0 || strcmp(v, "-classic") == 0
        || strcmp(v, "-classic-sum") == 0) {
        return;
    }
    if (write_view(v)) {
        return;
    }
    /* The files' views: a card in. */
    g_card = true;
    prun(20u);
    if (files_view(v)) {
        return;
    }
    if (strcmp(v, "-stopped") == 0) {
        press_stop();
        prun(200u);
        hwait(HITEC_PROG_HELD);
        return;
    }
    if (strcmp(v, "-leave") == 0 || strcmp(v, "-classic-leave") == 0) {
        htap(hitec_panel_close_rect());
        return;
    }
    if (strcmp(v, "-restore-ask") == 0 || strcmp(v, "-restore-hv") == 0) {
        /* The output on at the session's 5.0V 1.0A, the operator's set
         * points 5.5V 2.0A (7.4V for the HV warning) from before CONNECT;
         * LEAVE, and SERVO asks. */
        supply_screen_set_output(true);
        htap(hitec_panel_close_rect());
        hhold();
        hwait(HITEC_PROG_PWM);
        ui_router_tick(0.02f);
        if (!servo_screen_asks_restore()) {
            fprintf(stderr, "render_hitec: no restore question\n");
            exit(1);
        }
        if (strcmp(v, "-restore-hv") == 0) {
            htap_at(UI_MARGIN + 140,
                    UI_ALERT_Y - 4 - 56 + 28);
        }
        return;
    }
    if (strcmp(v, "-motion") == 0) {
        htap(hitec_panel_tab_rect(HITEC_PANEL_MOTION));
    } else if (strcmp(v, "-travel") == 0
               || strcmp(v, "-classic-travel") == 0) {
        htap(hitec_panel_tab_rect(HITEC_PANEL_TRAVEL));
    } else if (strcmp(v, "-protect") == 0
               || strcmp(v, "-classic-protect") == 0) {
        htap(hitec_panel_tab_rect(HITEC_PANEL_PROTECT));
    } else if (strcmp(v, "-info") == 0 || strcmp(v, "-classic-info") == 0) {
        htap(hitec_panel_tab_rect(HITEC_PANEL_INFO));
    } else {
        fprintf(stderr, "render_hitec: no view %s\n", view);
        exit(1);
    }
}

void render_hitec_feed(bool held)
{
    rig_at(SUPPLY_ON, true);
    if (held) {
        (void)open_as(CH, HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
        hwait(HITEC_PROG_OTHER);
    }
}
