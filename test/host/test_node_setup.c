/*
 * The display node's boot splash and its own SETUP
 * (firmware/node/main/node_setup.c, node_setup_view.c).
 *
 * The failures this is about: a splash that ends early or late, SETUP
 * opened with a panel linked, a splash that stays when a panel links, a
 * row that edits the wrong value or keeps its edit after BACK, a name the
 * node would refuse taken from its keyboard, a shunt out of range taken
 * from its keypad, an address offered that the scan did not find as an
 * INA228, a SETUP left open without a tap, a text wider than its box in
 * either language, and a frame that redraws without a change.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "node_setup.h"
#include "node_setup_view.h"

/** @brief The frame the render cases draw into. */
static gfx_color_t s_fb[320 * 240];

/** @brief A splash at 1000ms with the defaults. */
static void start(ns_t *s, uint8_t lang)
{
    nst_vals_t v;
    memset(&v, 0, sizeof v);
    v.shunt = RCNODE_SHUNT_DEF_UOHM;
    const uint8_t fw[3] = { 0u, 23u, 17u };
    ns_init(s, &v, lang, fw, 1000u);
}

/** @brief Tap the middle of a target. */
static ns_act_t tap(ns_t *s, int t, uint32_t now)
{
    gfx_rect_t r;
    if (!ns_box(s, t, &r)) {
        T_FAIL("target %d has no box in mode %d", t, s->mode);
        return NS_ACT_NONE;
    }
    return ns_tap(s, r.x + r.w / 2, r.y + r.h / 2, now, false);
}

/** @brief SETUP opened from a fresh splash. */
static void open_setup(ns_t *s, uint8_t lang)
{
    start(s, lang);
    (void)tap(s, NS_T_SETUP, 2000u);
}

/** @brief Type a text on the name keyboard. */
static void type(ns_t *s, const char *text, uint32_t now)
{
    for (const char *p = text; *p != '\0'; ++p) {
        int k = -1;
        if (*p == ' ') {
            k = NS_KB_SPACE;
        } else {
            for (int i = 0; i < NS_KB_CHARS; ++i) {
                if (i != NS_KB_DEL && ns_kb_label(s, i)[0] == *p) {
                    k = i;
                }
            }
        }
        if (k < 0) {
            T_FAIL("no key for '%c'", *p);
            return;
        }
        (void)tap(s, NS_T_KEY0 + k, now);
    }
}

/** @brief Type digits on the keypad. */
static void digits(ns_t *s, const char *d, uint32_t now)
{
    for (const char *p = d; *p != '\0'; ++p) {
        (void)tap(s, NS_T_KEY0 + (*p - '0'), now);
    }
}

TEST_CASE(the_splash_ends_at_10s_and_not_before)
{
    ns_t s;
    start(&s, RCNODE_LANG_EN);
    CHECK_EQ(s.mode, NS_SPLASH);
    CHECK_EQ(ns_splash_left(&s, 1000u), 10000u);
    CHECK_EQ(ns_tick(&s, 10999u, false), NS_ACT_NONE);
    CHECK_EQ(s.mode, NS_SPLASH);
    CHECK_EQ(ns_splash_left(&s, 10999u), 1u);
    CHECK_EQ(ns_tick(&s, 11000u, false), NS_ACT_NONE);
    CHECK_EQ(s.mode, NS_METER);
    CHECK_EQ(ns_splash_left(&s, 11000u), 0u);
    /* The ms clock's wrap is no early end. */
    nst_vals_t v;
    memset(&v, 0, sizeof v);
    ns_init(&s, &v, RCNODE_LANG_EN, NULL, 0xFFFFF000u);
    CHECK_EQ(ns_tick(&s, 0x00001000u, false), NS_ACT_NONE);
    CHECK_EQ(s.mode, NS_SPLASH);
    CHECK_EQ(ns_tick(&s, 0xFFFFF000u + 10000u, false), NS_ACT_NONE);
    CHECK_EQ(s.mode, NS_METER);
}

TEST_CASE(setup_is_offered_only_before_the_link)
{
    ns_t s;
    start(&s, RCNODE_LANG_EN);
    gfx_rect_t r;
    CHECK(ns_box(&s, NS_T_SETUP, &r));
    /* A tap on SETUP while a panel is linked changes nothing. */
    CHECK_EQ(ns_tap(&s, r.x + 5, r.y + 5, 2000u, true), NS_ACT_NONE);
    CHECK_EQ(s.mode, NS_SPLASH);
    /* A link during the 10s goes to the meter at once. */
    CHECK_EQ(ns_tick(&s, 2500u, true), NS_ACT_NONE);
    CHECK_EQ(s.mode, NS_METER);
    /* On the meter, SETUP is gone: no box, no tap opens it. */
    CHECK(!ns_box(&s, NS_T_SETUP, &r));
    CHECK_EQ(ns_tap(&s, 160, 190, 3000u, false), NS_ACT_NONE);
    CHECK_EQ(s.mode, NS_METER);
    CHECK(!ns_in_setup(&s));
    /* Without a link, SETUP opens; a tap beside it does nothing. */
    start(&s, RCNODE_LANG_EN);
    CHECK_EQ(ns_tap(&s, 5, 5, 2000u, false), NS_ACT_NONE);
    CHECK_EQ(s.mode, NS_SPLASH);
    CHECK_EQ(tap(&s, NS_T_SETUP, 2000u), NS_ACT_NONE);
    CHECK_EQ(s.mode, NS_LIST);
    CHECK(ns_in_setup(&s));
    /* The splash's 10s no longer count once SETUP is open. */
    CHECK_EQ(ns_tick(&s, 30000u, false), NS_ACT_NONE);
    CHECK_EQ(s.mode, NS_LIST);
}

TEST_CASE(setup_ends_after_60s_without_a_tap_or_with_a_link)
{
    ns_t s;
    open_setup(&s, RCNODE_LANG_EN);
    (void)tap(&s, NS_T_ROW3, 5000u);           /* SHUNT */
    CHECK_EQ(ns_tick(&s, 5000u + NS_IDLE_MS - 1u, false), NS_ACT_NONE);
    CHECK_EQ(s.mode, NS_SHUNT);
    CHECK_EQ(ns_tick(&s, 5000u + NS_IDLE_MS, false), NS_ACT_LEAVE);
    CHECK_EQ(s.mode, NS_METER);
    open_setup(&s, RCNODE_LANG_EN);
    CHECK_EQ(ns_tick(&s, 2100u, true), NS_ACT_LEAVE);
    CHECK_EQ(s.mode, NS_METER);
}

TEST_CASE(name_row_types_up_to_16_trims_and_cancel_keeps_the_old)
{
    ns_t s;
    open_setup(&s, RCNODE_LANG_EN);
    CHECK_EQ(tap(&s, NS_T_ROW0, 3000u), NS_ACT_NONE);
    CHECK_EQ(s.mode, NS_NAME);
    type(&s, " XT90 MAIN ", 3000u);
    CHECK_STR_EQ(s.text, " XT90 MAIN ");
    (void)tap(&s, NS_T_KEY0 + NS_KB_OK, 3000u);
    CHECK_EQ(s.mode, NS_LIST);
    CHECK_STR_EQ(s.edit.name, "XT90 MAIN");
    char v[32];
    ns_row_value(&s, 0, v, sizeof v);
    CHECK_STR_EQ(v, "XT90 MAIN");
    /* 17 keys type 16 characters. */
    (void)tap(&s, NS_T_ROW0, 3000u);
    (void)tap(&s, NS_T_KEY0 + NS_KB_CLR, 3000u);
    type(&s, "0123456789ABCDEFG", 3000u);
    CHECK_STR_EQ(s.text, "0123456789ABCDEF");
    (void)tap(&s, NS_T_KEY0 + NS_KB_DEL, 3000u);
    CHECK_STR_EQ(s.text, "0123456789ABCDE");
    type(&s, "-_.", 3000u);
    CHECK_STR_EQ(s.text, "0123456789ABCDE-");
    /* CANCEL keeps the name before. */
    (void)tap(&s, NS_T_KEY0 + NS_KB_CANCEL, 3000u);
    CHECK_STR_EQ(s.edit.name, "XT90 MAIN");
    /* An empty name, or spaces alone, is no name. */
    (void)tap(&s, NS_T_ROW0, 3000u);
    (void)tap(&s, NS_T_KEY0 + NS_KB_CLR, 3000u);
    type(&s, "   ", 3000u);
    (void)tap(&s, NS_T_KEY0 + NS_KB_OK, 3000u);
    CHECK_STR_EQ(s.edit.name, "");
    ns_row_value(&s, 0, v, sizeof v);
    CHECK_STR_EQ(v, "NONE");
    /* Every character key types one rcnode_uname_ok() takes. */
    for (int k = 0; k < NS_KB_CHARS; ++k) {
        if (k != NS_KB_DEL) {
            char one[2] = { ns_kb_label(&s, k)[0], '\0' };
            CHECK(rcnode_uname_ok(one));
        }
    }
}

TEST_CASE(shunt_row_takes_50_to_10000_and_refuses_the_rest)
{
    ns_t s;
    open_setup(&s, RCNODE_LANG_EN);
    (void)tap(&s, NS_T_ROW3, 3000u);
    CHECK_EQ(s.mode, NS_SHUNT);
    CHECK_STR_EQ(s.num, "200");
    (void)tap(&s, NS_T_KEY0 + NS_KP_CLR, 3000u);
    digits(&s, "49", 3000u);
    (void)tap(&s, NS_T_KEY0 + NS_KP_OK, 3000u);
    CHECK_EQ(s.mode, NS_SHUNT);
    CHECK(s.refused);
    CHECK_EQ(s.edit.shunt, 200u);
    (void)tap(&s, NS_T_KEY0 + NS_KP_CLR, 3000u);
    CHECK(!s.refused);
    digits(&s, "100001", 3000u);               /* 5 digits at most */
    CHECK_STR_EQ(s.num, "10000");
    (void)tap(&s, NS_T_KEY0 + NS_KP_OK, 3000u);
    CHECK_EQ(s.mode, NS_LIST);
    CHECK_EQ(s.edit.shunt, 10000u);
    (void)tap(&s, NS_T_ROW3, 3000u);
    (void)tap(&s, NS_T_KEY0 + NS_KP_CLR, 3000u);
    digits(&s, "050", 3000u);                  /* no leading zero */
    CHECK_STR_EQ(s.num, "50");
    (void)tap(&s, NS_T_KEY0 + NS_KP_OK, 3000u);
    CHECK_EQ(s.edit.shunt, 50u);
    (void)tap(&s, NS_T_ROW3, 3000u);
    (void)tap(&s, NS_T_KEY0 + NS_KP_CLR, 3000u);
    (void)tap(&s, NS_T_KEY0 + NS_KP_OK, 3000u);  /* empty: refused */
    CHECK(s.refused);
    (void)tap(&s, NS_T_KEY0 + NS_KP_CANCEL, 3000u);
    CHECK_EQ(s.edit.shunt, 50u);
    char v[32];
    ns_row_value(&s, 3, v, sizeof v);
    CHECK_STR_EQ(v, "50uΩ");
}

/** @brief A scan result with INA228s at 0x40 and 0x45, other parts at
 *         0x18 and 0x38. */
static nscan_t scan_result(void)
{
    nscan_t r;
    memset(&r, 0, sizeof r);
    r.done = true;
    const uint8_t a[] = { 0x18u, 0x38u, 0x40u, 0x45u };
    for (size_t i = 0; i < sizeof a; ++i) {
        r.answered[a[i] >> 3] = (uint8_t)(r.answered[a[i] >> 3] | (1u << (a[i] & 7u)));
    }
    r.ina228[0x40 >> 3] = (uint8_t)((1u << (0x40 & 7u)) | (1u << (0x45 & 7u)));
    r.n = 4u;
    r.n_ina228 = 2u;
    return r;
}

TEST_CASE(scan_row_asks_a_scan_and_shows_every_answer)
{
    ns_t s;
    open_setup(&s, RCNODE_LANG_EN);
    char v[32];
    ns_row_value(&s, 1, v, sizeof v);
    CHECK_STR_EQ(v, "TAP TO SCAN");
    CHECK_EQ(tap(&s, NS_T_ROW0 + 1, 3000u), NS_ACT_SCAN);
    CHECK_EQ(s.mode, NS_SCAN);
    const nscan_t r = scan_result();
    ns_scan_done(&s, &r);
    CHECK_EQ(ns_chips(&s), 4);
    CHECK_EQ(ns_chip_addr(&s, 0), 0x18u);
    CHECK_EQ(ns_chip_addr(&s, 3), 0x45u);
    CHECK_EQ(ns_chip_addr(&s, 4), 0u);
    CHECK_EQ(tap(&s, NS_T_LEFT, 3000u), NS_ACT_SCAN);   /* AGAIN */
    CHECK_EQ(tap(&s, NS_T_RIGHT, 3000u), NS_ACT_NONE);  /* BACK */
    CHECK_EQ(s.mode, NS_LIST);
    ns_row_value(&s, 1, v, sizeof v);
    CHECK_STR_EQ(v, "4 FOUND");
    /* A bus with more answers than room shows 29 and "+N". */
    nscan_t many;
    memset(&many, 0, sizeof many);
    many.done = true;
    for (unsigned a = 0x08u; a < 0x08u + 40u; ++a) {
        many.answered[a >> 3] = (uint8_t)(many.answered[a >> 3] | (1u << (a & 7u)));
    }
    many.n = 40u;
    (void)tap(&s, NS_T_ROW0 + 1, 3000u);
    ns_scan_done(&s, &many);
    CHECK_EQ(ns_chips(&s), NS_SCAN_CHIPS - 1);
}

TEST_CASE(address_row_offers_auto_and_the_ina228s_found)
{
    ns_t s;
    open_setup(&s, RCNODE_LANG_EN);
    /* No scan yet: the choice asks one. */
    CHECK_EQ(tap(&s, NS_T_ROW0 + 2, 3000u), NS_ACT_SCAN);
    CHECK_EQ(s.mode, NS_ADDR);
    const nscan_t r = scan_result();
    ns_scan_done(&s, &r);
    /* The two INA228s, not the codec or the touch controller. */
    CHECK_EQ(ns_chips(&s), 2);
    CHECK_EQ(ns_chip_addr(&s, 0), 0x40u);
    CHECK_EQ(ns_chip_addr(&s, 1), 0x45u);
    (void)tap(&s, NS_T_CHIP0 + 1, 3000u);
    CHECK_EQ(s.mode, NS_LIST);
    CHECK_EQ(s.edit.i228, 0x45u);
    char v[32];
    ns_row_value(&s, 2, v, sizeof v);
    CHECK_STR_EQ(v, "0x45");
    /* With a scan done, the choice opens without another. */
    CHECK_EQ(tap(&s, NS_T_ROW0 + 2, 3000u), NS_ACT_NONE);
    (void)tap(&s, NS_T_AUTO, 3000u);
    CHECK_EQ(s.edit.i228, RCNODE_I228_AUTO);
    ns_row_value(&s, 2, v, sizeof v);
    CHECK_STR_EQ(v, "AUTO");
    /* A fixed address in force that the scan did not find stays on
     * offer, so it can be kept. */
    s.edit.i228 = 0x4Cu;
    (void)tap(&s, NS_T_ROW0 + 2, 3000u);
    CHECK_EQ(ns_chips(&s), 3);
    CHECK_EQ(ns_chip_addr(&s, 2), 0x4Cu);
    (void)tap(&s, NS_T_RIGHT, 3000u);
    CHECK_EQ(s.edit.i228, 0x4Cu);
}

TEST_CASE(save_hands_the_edits_over_and_back_drops_them)
{
    ns_t s;
    open_setup(&s, RCNODE_LANG_EN);
    (void)tap(&s, NS_T_ROW3, 3000u);
    (void)tap(&s, NS_T_KEY0 + NS_KP_CLR, 3000u);
    digits(&s, "500", 3000u);
    (void)tap(&s, NS_T_KEY0 + NS_KP_OK, 3000u);
    CHECK_EQ(tap(&s, NS_T_RIGHT, 3000u), NS_ACT_SAVE);
    CHECK_EQ(s.mode, NS_METER);
    CHECK_EQ(s.edit.shunt, 500u);
    open_setup(&s, RCNODE_LANG_EN);
    CHECK_EQ(tap(&s, NS_T_LEFT, 3000u), NS_ACT_LEAVE);
    CHECK_EQ(s.mode, NS_METER);
}

/** @brief Whether a text fits a width at a face. */
static bool fits(const char *t, const gfx_font_t *f, int w)
{
    const int tw = gfx_text_width(f, t, 1);
    if (tw > w) {
        printf("      \"%s\" is %dpx in %dpx\n", t, tw, w);
        return false;
    }
    return true;
}

TEST_CASE(every_text_fits_its_box_in_both_languages)
{
    for (int lang = 0; lang < 2; ++lang) {
        ns_t s;
        open_setup(&s, (uint8_t)lang);
        /* Each row: its label and its widest value side by side. */
        static const int k_label[4] = { NS_TX_NAME, NS_TX_SCAN, NS_TX_ADDR,
                                        NS_TX_SHUNT };
        strcpy(s.edit.name, "0123456789ABCDEF");
        s.edit.shunt = 10000u;
        s.edit.i228 = 0x4Fu;
        for (int row = 0; row < 4; ++row) {
            char v[48];
            char both[96];
            ns_row_value(&s, row, v, sizeof v);
            (void)snprintf(both, sizeof both, "%s  %s",
                           ns_text(s.lang, k_label[row]), v);
            CHECK(fits(both, &gfx_font_10x20, 320 - 16));
        }
        char v[48];
        s.scan.done = false;
        ns_row_value(&s, 1, v, sizeof v);
        char both[96];
        (void)snprintf(both, sizeof both, "%s  %s", ns_text(s.lang, NS_TX_SCAN), v);
        CHECK(fits(both, &gfx_font_10x20, 320 - 16));
        /* The splash's lines, the band's titles, the messages. */
        CHECK(fits(ns_text(s.lang, NS_TX_ROLE), &gfx_font_10x20, 320));
        CHECK(fits("FIRMWARE 255.255.255", &gfx_font_10x20, 320));
        (void)snprintf(both, sizeof both, "%s  %s", ns_text(s.lang, NS_TX_ADDR),
                       ns_text(s.lang, NS_TX_LEGEND));
        CHECK(fits(both, &gfx_font_10x20, 320 - 16));
        CHECK(fits(ns_text(s.lang, NS_TX_NO_ANSWER), &gfx_font_10x20, 320));
        CHECK(fits(ns_text(s.lang, NS_TX_NO_INA), &gfx_font_10x20, 320));
        CHECK(fits(ns_text(s.lang, NS_TX_SETUP), &gfx_font_20x36, 200));
        /* The bottom buttons, 148px at 12 x 24. */
        CHECK(fits(ns_text(s.lang, NS_TX_BACK), &gfx_font_12x24, 148 - 4));
        CHECK(fits(ns_text(s.lang, NS_TX_SAVE), &gfx_font_12x24, 148 - 4));
        CHECK(fits(ns_text(s.lang, NS_TX_AGAIN), &gfx_font_12x24, 148 - 4));
        /* The keyboard's and the keypad's keys. */
        (void)tap(&s, NS_T_ROW0, 3000u);
        for (int k = 0; k < NS_KB_KEYS; ++k) {
            gfx_rect_t r;
            CHECK(ns_box(&s, NS_T_KEY0 + k, &r));
            const bool word = k >= NS_KB_CHARS || k == NS_KB_DEL;
            CHECK(fits(ns_kb_label(&s, k), word ? &gfx_font_10x20
                                                : &gfx_font_12x24, r.w - 2));
        }
        CHECK(fits("0123456789ABCDEF", &gfx_font_12x24, 320 - 8 - 60));
        (void)tap(&s, NS_T_KEY0 + NS_KB_CANCEL, 3000u);
        (void)tap(&s, NS_T_ROW3, 3000u);
        for (int k = 0; k < NS_KP_KEYS; ++k) {
            gfx_rect_t r;
            CHECK(ns_box(&s, NS_T_KEY0 + k, &r));
            CHECK(fits(ns_kp_label(&s, k), k <= 9 ? &gfx_font_20x36
                                                  : &gfx_font_12x24, r.w - 2));
        }
        (void)snprintf(both, sizeof both, "10000uΩ  %s",
                       ns_text(s.lang, NS_TX_RANGE));
        CHECK(fits(both, &gfx_font_12x24, 320 - 16));
    }
}

TEST_CASE(no_two_targets_overlap_and_every_box_is_on_the_screen)
{
    static const int k_modes[] = { NS_SPLASH, NS_LIST, NS_NAME, NS_SHUNT,
                                   NS_SCAN, NS_ADDR };
    ns_t s;
    open_setup(&s, RCNODE_LANG_EN);
    s.scan = scan_result();
    for (size_t m = 0; m < sizeof k_modes / sizeof k_modes[0]; ++m) {
        s.mode = (uint8_t)k_modes[m];
        for (int a = NS_T_SETUP; a <= NS_T_CHIP_LAST; ++a) {
            gfx_rect_t ra;
            if (!ns_box(&s, a, &ra)) {
                continue;
            }
            CHECK(ra.x >= 0 && ra.y >= 0 && ra.x + ra.w <= 320
                  && ra.y + ra.h <= 240);
            for (int b = a + 1; b <= NS_T_CHIP_LAST; ++b) {
                gfx_rect_t rb;
                if (ns_box(&s, b, &rb) && ra.x < rb.x + rb.w && rb.x < ra.x + ra.w
                    && ra.y < rb.y + rb.h && rb.y < ra.y + ra.h) {
                    T_FAIL("mode %d: targets %d and %d overlap", s.mode, a, b);
                }
            }
        }
    }
}

TEST_CASE(a_screen_draws_whole_once_then_only_the_splash_bar)
{
    gfx_canvas_t c;
    gfx_canvas_init(&c, s_fb, 320, 240, 0);
    ns_t s;
    start(&s, RCNODE_LANG_DE);
    nsv_t v;
    nsv_init(&v);
    CHECK_EQ(nsv_render(&v, &c, &s, 1000u), 2);    /* the screen, the bar */
    CHECK_EQ(nsv_render(&v, &c, &s, 1000u), 0);
    CHECK_EQ(nsv_render(&v, &c, &s, 1100u), 1);    /* the bar shortened */
    (void)tap(&s, NS_T_SETUP, 2000u);
    CHECK_EQ(nsv_render(&v, &c, &s, 2000u), 1);
    CHECK_EQ(nsv_render(&v, &c, &s, 2100u), 0);
    (void)tap(&s, NS_T_ROW0, 2200u);
    type(&s, "A", 2200u);
    CHECK_EQ(nsv_render(&v, &c, &s, 2200u), 1);
    s.mode = NS_METER;
    CHECK_EQ(nsv_render(&v, &c, &s, 2300u), 0);
    CHECK_EQ(nsv_render(NULL, &c, &s, 0u), 0);
}

int main(void)
{
    RUN(the_splash_ends_at_10s_and_not_before);
    RUN(setup_is_offered_only_before_the_link);
    RUN(setup_ends_after_60s_without_a_tap_or_with_a_link);
    RUN(name_row_types_up_to_16_trims_and_cancel_keeps_the_old);
    RUN(shunt_row_takes_50_to_10000_and_refuses_the_rest);
    RUN(scan_row_asks_a_scan_and_shows_every_answer);
    RUN(address_row_offers_auto_and_the_ina228s_found);
    RUN(save_hands_the_edits_over_and_back_drops_them);
    RUN(every_text_fits_its_box_in_both_languages);
    RUN(no_two_targets_overlap_and_every_box_is_on_the_screen);
    RUN(a_screen_draws_whole_once_then_only_the_splash_bar);
    return test_summary("node_setup");
}
