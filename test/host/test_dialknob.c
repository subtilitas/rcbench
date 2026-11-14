/*
 * The Dial's packet codec (protocols/dialknob).
 *
 * The failures this is about: a field read from the wrong byte or with the
 * wrong sign, a field past its range taken for a value, a packet of a wrong
 * length or kind taken, a text that is not UTF-8 shown, and a decoder that
 * reads past the bytes it was given.  The packets of the decode cases are
 * built byte by byte from the layout in dialknob.h, not with the module's
 * encoders.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "dialknob.h"

/** An EVENT as bytes, from the layout. */
static void ev_bytes(uint8_t b[DK_LEN_EVENT], uint8_t seq, uint16_t counter,
                     uint8_t buttons, uint8_t gesture, uint8_t x, uint8_t y,
                     uint16_t ms, uint16_t echo)
{
    b[0] = DK_K_EVENT;
    b[1] = seq;
    b[2] = (uint8_t)(counter & 0xFFu);
    b[3] = (uint8_t)(counter >> 8);
    b[4] = buttons;
    b[5] = gesture;
    b[6] = x;
    b[7] = y;
    b[8] = (uint8_t)(ms & 0xFFu);
    b[9] = (uint8_t)(ms >> 8);
    b[10] = (uint8_t)(echo & 0xFFu);
    b[11] = (uint8_t)(echo >> 8);
}

/** A STATUS as bytes, from the layout. */
static void st_bytes(uint8_t b[DK_LEN_STATUS], uint16_t mv, uint8_t flags,
                     uint8_t rssi)
{
    memset(b, 0, DK_LEN_STATUS);
    b[0] = DK_K_STATUS;
    b[1] = 9u;
    b[2] = (uint8_t)(mv & 0xFFu);
    b[3] = (uint8_t)(mv >> 8);
    b[4] = flags;
    b[5] = 0u;
    b[6] = 23u;
    b[7] = 13u;
    b[8] = 0x10u;
    b[9] = 0x0Eu;
    b[10] = rssi;
    b[11] = 250u;
}

/** A SCREEN as bytes with @p n title bytes from @p t. */
static void scr_bytes(uint8_t b[DK_LEN_SCREEN], const char *t, size_t n)
{
    memset(b, 0, DK_LEN_SCREEN);
    b[0] = DK_K_SCREEN;
    b[1] = 3u;
    b[2] = DK_FACE_MOTOR;
    b[3] = DK_LANG_DE;
    b[4] = DK_SCR_SOUND;
    memcpy(b + 5, t, n);
}

TEST_CASE(an_event_decodes_every_field_from_its_byte)
{
    uint8_t b[DK_LEN_EVENT];
    dk_evt_t e;

    ev_bytes(b, 0xFE, 0x8001u, DK_BTN_SCREEN | DK_BTN_TOUCH | DK_BTN_STAMP,
             DK_G_TAP, 239, 238, 0xBEEF, 0x1234);
    CHECK(dk_evt_decode(b, sizeof b, &e));
    CHECK_EQ(e.seq, 0xFE);
    CHECK_EQ(e.counter, -32767);
    CHECK_EQ(e.buttons, 0x07);
    CHECK_EQ(e.gesture, DK_G_TAP);
    CHECK_EQ(e.touch_x, 239);
    CHECK_EQ(e.touch_y, 238);
    CHECK_EQ(e.ms, 0xBEEF);
    CHECK_EQ(e.echo, 0x1234);

    ev_bytes(b, 0, 0x7FFFu, 0, DK_G_NONE, 0, 0, 0, 0);
    CHECK(dk_evt_decode(b, sizeof b, &e));
    CHECK_EQ(e.counter, 32767);
    ev_bytes(b, 0, 0x8000u, 0, DK_G_NONE, 0, 0, 0, 0);
    CHECK(dk_evt_decode(b, sizeof b, &e));
    CHECK_EQ(e.counter, -32768);
    ev_bytes(b, 0, 0xFFFFu, 0, DK_G_HOLD, 0, 0, 0xFFFF, 0xFFFF);
    CHECK(dk_evt_decode(b, sizeof b, &e));
    CHECK_EQ(e.counter, -1);
    CHECK_EQ(e.ms, 0xFFFF);
}

TEST_CASE(an_event_with_a_field_past_its_range_is_refused)
{
    uint8_t b[DK_LEN_EVENT];
    dk_evt_t e;

    e.seq = 77;
    ev_bytes(b, 1, 0, 0x08, DK_G_NONE, 0, 0, 0, 0);   /* reserved bit 3 */
    CHECK(!dk_evt_decode(b, sizeof b, &e));
    ev_bytes(b, 1, 0, 0x80, DK_G_NONE, 0, 0, 0, 0);
    CHECK(!dk_evt_decode(b, sizeof b, &e));
    ev_bytes(b, 1, 0, 0, 4, 0, 0, 0, 0);                /* gesture 4 */
    CHECK(!dk_evt_decode(b, sizeof b, &e));
    ev_bytes(b, 1, 0, DK_BTN_TOUCH, DK_G_NONE, 240, 0, 0, 0);
    CHECK(!dk_evt_decode(b, sizeof b, &e));
    ev_bytes(b, 1, 0, DK_BTN_TOUCH, DK_G_NONE, 0, 240, 0, 0);
    CHECK(!dk_evt_decode(b, sizeof b, &e));
    ev_bytes(b, 1, 0, 0, DK_G_PRESS, 1, 0, 0, 0);       /* x without touch */
    CHECK(!dk_evt_decode(b, sizeof b, &e));
    ev_bytes(b, 1, 0, 0, DK_G_NONE, 0, 1, 0, 0);
    CHECK(!dk_evt_decode(b, sizeof b, &e));
    CHECK_EQ(e.seq, 77);                                /* left alone */

    ev_bytes(b, 1, 0, 0, DK_G_TAP, 120, 7, 0, 0);       /* a tap has x, y */
    CHECK(dk_evt_decode(b, sizeof b, &e));
    CHECK_EQ(e.touch_x, 120);
}

TEST_CASE(a_packet_of_a_wrong_length_or_kind_is_refused)
{
    uint8_t b[DK_PKT_MAX + 4u];
    dk_evt_t e;
    dk_panel_pkt_t p;
    dk_state_t s = { 1, 0x01020304u, 10, 10, 0 };

    memset(b, 0, sizeof b);
    ev_bytes(b, 1, 0, 0, 0, 0, 0, 0, 0);
    CHECK(dk_evt_decode(b, DK_LEN_EVENT, &e));
    CHECK(!dk_evt_decode(b, DK_LEN_EVENT - 1u, &e));
    CHECK(!dk_evt_decode(b, DK_LEN_EVENT + 1u, &e));
    CHECK(!dk_evt_decode(b, 0, &e));
    CHECK(!dk_evt_decode(NULL, DK_LEN_EVENT, &e));
    b[0] = DK_K_STATUS;
    CHECK(!dk_evt_decode(b, DK_LEN_EVENT, &e));

    CHECK_EQ(dk_state_encode(&s, b), DK_LEN_STATE);
    CHECK(dk_panel_decode(b, DK_LEN_STATE, &p));
    CHECK_EQ(p.kind, DK_K_STATE);
    CHECK(!dk_panel_decode(b, DK_LEN_STATE - 1u, &p));
    CHECK(!dk_panel_decode(b, DK_LEN_STATE + 1u, &p));
    for (unsigned k = 0; k < 256u; ++k) {
        if (k >= DK_K_SCREEN && k <= DK_K_SCALE) {
            continue;
        }
        b[0] = (uint8_t)k;      /* a Dial kind or no kind at all */
        for (size_t n = 0; n <= sizeof b; ++n) {
            CHECK(!dk_panel_decode(b, n, &p));
        }
    }
    CHECK(!dk_panel_decode(NULL, 9, &p));
}

TEST_CASE(a_status_holds_its_battery_and_rssi_bounds)
{
    uint8_t b[DK_LEN_STATUS];
    dk_status_t s;

    st_bytes(b, DK_BATT_MIN_MV, DK_ST_USB | DK_ST_CHARGING, (uint8_t)-127);
    CHECK(dk_status_decode(b, sizeof b, &s));
    CHECK_EQ(s.batt_mv, 2000);
    CHECK_EQ(s.rssi, -127);
    CHECK_EQ(s.fw[1], 23);
    CHECK_EQ(s.fw[2], 13);
    CHECK_EQ(s.uptime_s, 0x0E10);
    CHECK_EQ(s.bad, 250);
    st_bytes(b, DK_BATT_MAX_MV, DK_ST_PANEL_LOST, 20);
    CHECK(dk_status_decode(b, sizeof b, &s));
    CHECK_EQ(s.rssi, 20);
    st_bytes(b, 0, DK_ST_BATT_UNKNOWN, (uint8_t)DK_RSSI_NONE);
    CHECK(dk_status_decode(b, sizeof b, &s));
    CHECK_EQ(s.rssi, 127);

    st_bytes(b, DK_BATT_MIN_MV - 1u, 0, 0);
    CHECK(!dk_status_decode(b, sizeof b, &s));
    st_bytes(b, DK_BATT_MAX_MV + 1u, 0, 0);
    CHECK(!dk_status_decode(b, sizeof b, &s));
    st_bytes(b, 0, 0, 0);                       /* 0 needs the flag */
    CHECK(!dk_status_decode(b, sizeof b, &s));
    st_bytes(b, 3700, DK_ST_BATT_UNKNOWN, 0);   /* the flag needs 0 */
    CHECK(!dk_status_decode(b, sizeof b, &s));
    st_bytes(b, 3700, 0x10, 0);
    CHECK(!dk_status_decode(b, sizeof b, &s));
    st_bytes(b, 3700, 0, 21);
    CHECK(!dk_status_decode(b, sizeof b, &s));
    st_bytes(b, 3700, 0, (uint8_t)-128);
    CHECK(!dk_status_decode(b, sizeof b, &s));
    st_bytes(b, 3700, 0, 126);
    CHECK(!dk_status_decode(b, sizeof b, &s));
}

TEST_CASE(info_carries_the_protocol_and_an_ascii_name)
{
    uint8_t b[DK_LEN_INFO];
    dk_info_t i = { DK_PROTOCOL, { 0, 23, 13 }, DK_INFO_BOUND, DK_HW_DIAL11,
                    "DIAL-3A10" };
    dk_info_t o;

    CHECK_EQ(dk_info_encode(&i, b), DK_LEN_INFO);
    CHECK_EQ(b[1], DK_PROTOCOL);
    CHECK_EQ(b[3], 23);
    CHECK_EQ(b[8], 'D');
    CHECK_EQ(b[16], '0');
    CHECK_EQ(b[17], 0);
    CHECK(dk_info_decode(b, sizeof b, &o));
    CHECK_STR_EQ(o.name, "DIAL-3A10");
    CHECK_EQ(o.flags, DK_INFO_BOUND);

    memcpy(i.name, "ABCDEFGHIJKLM", 13);        /* no NUL: cut to 12 */
    CHECK_EQ(dk_info_encode(&i, b), DK_LEN_INFO);
    CHECK(dk_info_decode(b, sizeof b, &o));
    CHECK_STR_EQ(o.name, "ABCDEFGHIJKL");

    strcpy(i.name, "D\xC3\xA4");                /* not ASCII */
    CHECK_EQ(dk_info_encode(&i, b), 0u);
    strcpy(i.name, "DIAL");
    i.proto = 0;
    CHECK_EQ(dk_info_encode(&i, b), 0u);
    i.proto = DK_PROTOCOL;
    i.hw = 2;
    CHECK_EQ(dk_info_encode(&i, b), 0u);
    i.hw = DK_HW_DIAL11;
    i.flags = 0x04;
    CHECK_EQ(dk_info_encode(&i, b), 0u);
    i.flags = 0;
    CHECK_EQ(dk_info_encode(&i, b), DK_LEN_INFO);
    b[7] = 1;                                   /* reserved byte */
    CHECK(!dk_info_decode(b, sizeof b, &o));
}

TEST_CASE(a_title_is_utf8_padded_with_nul_and_cut_on_a_character)
{
    uint8_t b[DK_LEN_SCREEN];
    dk_screen_t s;
    dk_screen_t in = { 5, DK_FACE_VALUE, DK_LANG_DE, 0, "" };

    scr_bytes(b, "MOTOR & ESC", 11);
    CHECK(dk_screen_decode(b, sizeof b, &s));
    CHECK_STR_EQ(s.title, "MOTOR & ESC");
    CHECK_EQ(s.face, DK_FACE_MOTOR);
    CHECK_EQ(s.lang, DK_LANG_DE);
    CHECK_EQ(s.flags, DK_SCR_SOUND);
    scr_bytes(b, "123456789012345", 15);        /* full, no NUL */
    CHECK(dk_screen_decode(b, sizeof b, &s));
    CHECK_STR_EQ(s.title, "123456789012345");
    scr_bytes(b, "", 0);
    CHECK(dk_screen_decode(b, sizeof b, &s));
    CHECK_STR_EQ(s.title, "");

    scr_bytes(b, "AB\0C", 4);                   /* a byte after the NUL */
    CHECK(!dk_screen_decode(b, sizeof b, &s));
    scr_bytes(b, "A\x01", 2);                   /* control */
    CHECK(!dk_screen_decode(b, sizeof b, &s));
    scr_bytes(b, "A\x7F", 2);
    CHECK(!dk_screen_decode(b, sizeof b, &s));
    scr_bytes(b, "\xC2\x85", 2);                /* C1 control U+0085 */
    CHECK(!dk_screen_decode(b, sizeof b, &s));
    scr_bytes(b, "\xC3", 1);                    /* cut sequence */
    CHECK(!dk_screen_decode(b, sizeof b, &s));
    scr_bytes(b, "\xC0\xAF", 2);                /* overlong */
    CHECK(!dk_screen_decode(b, sizeof b, &s));
    scr_bytes(b, "\xE0\x80\xAF", 3);            /* overlong, 3 bytes */
    CHECK(!dk_screen_decode(b, sizeof b, &s));
    scr_bytes(b, "\xE0\x82\xA0", 3);            /* U+00A0 in 3 bytes */
    CHECK(!dk_screen_decode(b, sizeof b, &s));
    scr_bytes(b, "\xED\xA0\x80", 3);            /* surrogate */
    CHECK(!dk_screen_decode(b, sizeof b, &s));
    scr_bytes(b, "\xF4\x90\x80\x80", 4);        /* above U+10FFFF */
    CHECK(!dk_screen_decode(b, sizeof b, &s));
    scr_bytes(b, "\xF0\x8F\xBF\xBF", 4);        /* overlong, 4 bytes */
    CHECK(!dk_screen_decode(b, sizeof b, &s));
    scr_bytes(b, "\x80", 1);                    /* lone continuation */
    CHECK(!dk_screen_decode(b, sizeof b, &s));
    scr_bytes(b, "\xC3\x41", 2);                /* not a continuation */
    CHECK(!dk_screen_decode(b, sizeof b, &s));
    scr_bytes(b, "\xC2\xA0\xC3\xBC\xE2\x82\xAC\xF4\x8F\xBF\xBF", 11);
    CHECK(dk_screen_decode(b, sizeof b, &s));   /* U+00A0 to U+10FFFF */

    /* 13 ASCII bytes and a 2-byte umlaut fill 15; one more ASCII byte
     * before it leaves no room for the umlaut's second byte. */
    strcpy(in.title, "ABCDEFGHIJKLM\xC3\xBC");
    CHECK_EQ(dk_screen_encode(&in, b), DK_LEN_SCREEN);
    CHECK(dk_screen_decode(b, sizeof b, &s));
    CHECK_STR_EQ(s.title, "ABCDEFGHIJKLM\xC3\xBC");
    strcpy(in.title, "ABCDEFGHIJKLMN\xC3");   /* ends in a cut sequence */
    CHECK_EQ(dk_screen_encode(&in, b), 0u);
}

TEST_CASE(the_encoder_cuts_a_long_text_at_a_whole_character)
{
    uint8_t b[DK_LEN_NAV];
    dk_nav_t n = { 1, 0, 1, DK_SLOT_FOCUS, "" };
    dk_nav_t o;
    char t[4];

    /* A field filled to its 16th byte with no NUL: cut to 15 bytes, and
     * a 3-byte euro sign across the cut goes whole. */
    memcpy(n.item, "ABCDEFGHIJKLMN\xE2\x82", 16);
    CHECK_EQ(dk_nav_encode(&n, b), DK_LEN_NAV);
    CHECK(dk_nav_decode(b, sizeof b, &o));
    CHECK_STR_EQ(o.item, "ABCDEFGHIJKLMN");
    CHECK_EQ(b[19], 0);
    memcpy(n.item, "ABCDEFGHIJKLM\xC3\xBCX", 16);
    CHECK_EQ(dk_nav_encode(&n, b), DK_LEN_NAV);
    CHECK(dk_nav_decode(b, sizeof b, &o));
    CHECK_STR_EQ(o.item, "ABCDEFGHIJKLM\xC3\xBC");

    /* dk_text_copy() fills a field from a longer text the same way. */
    CHECK_EQ(dk_text_copy(n.item, sizeof n.item,
                          "EINSTELLUNGEN F\xC3\x9CR"), 15u);
    CHECK_STR_EQ(n.item, "EINSTELLUNGEN F");
    CHECK_EQ(dk_text_copy(n.item, sizeof n.item,
                          "EINSTELLUNG F\xC3\x9CR"), 15u);
    CHECK_STR_EQ(n.item, "EINSTELLUNG F\xC3\x9C");
    CHECK_EQ(dk_text_copy(t, sizeof t, "A\xE2\x82\xAC"), 1u);
    CHECK_STR_EQ(t, "A");
    CHECK_EQ(dk_text_copy(t, sizeof t, "ABC"), 3u);
    CHECK_STR_EQ(t, "ABC");
    CHECK_EQ(dk_text_copy(t, sizeof t, NULL), 0u);
    CHECK_STR_EQ(t, "");
    CHECK_EQ(dk_text_copy(t, 0, "A"), 0u);
    CHECK_EQ(dk_text_copy(NULL, 4, "A"), 0u);
}

TEST_CASE(nav_holds_index_below_count_and_three_slots)
{
    dk_nav_t n = { 1, 0, 0, DK_SLOT_PREV, "" };
    uint8_t b[DK_LEN_NAV];
    dk_nav_t o;

    CHECK_EQ(dk_nav_encode(&n, b), DK_LEN_NAV);     /* an empty list */
    n.index = 1;
    CHECK_EQ(dk_nav_encode(&n, b), 0u);
    n.count = 255;
    n.index = 254;
    n.slot = DK_SLOT_NEXT;
    strcpy(n.item, "PANEL");
    CHECK_EQ(dk_nav_encode(&n, b), DK_LEN_NAV);
    CHECK(dk_nav_decode(b, sizeof b, &o));
    CHECK_EQ(o.index, 254);
    CHECK_EQ(o.count, 255);
    CHECK_EQ(o.slot, DK_SLOT_NEXT);
    CHECK_STR_EQ(o.item, "PANEL");
    n.index = 255;
    CHECK_EQ(dk_nav_encode(&n, b), 0u);
    n.index = 0;
    n.slot = 3;
    CHECK_EQ(dk_nav_encode(&n, b), 0u);
}

TEST_CASE(screen_refuses_an_unknown_face_language_or_flag)
{
    dk_screen_t s = { 0, DK_FACE_NAV, DK_LANG_EN,
                      DK_SCR_NAVIGATES | DK_SCR_SOUND,
                      "SETUP" };
    uint8_t b[DK_LEN_SCREEN];

    CHECK_EQ(dk_screen_encode(&s, b), DK_LEN_SCREEN);
    s.face = DK_FACE_HOME;
    CHECK_EQ(dk_screen_encode(&s, b), DK_LEN_SCREEN);
    s.face = 0;
    CHECK_EQ(dk_screen_encode(&s, b), 0u);
    for (unsigned i = 0; i < DK_LEN_SCREEN; ++i) {
        CHECK_EQ(b[i], 0);                      /* a refused packet is 0 */
    }
    s.face = DK_FACE_HOME + 1;
    CHECK_EQ(dk_screen_encode(&s, b), 0u);
    s.face = DK_FACE_NAV;
    s.lang = 2;
    CHECK_EQ(dk_screen_encode(&s, b), 0u);
    s.lang = DK_LANG_DE;
    s.flags = 0x04;
    CHECK_EQ(dk_screen_encode(&s, b), 0u);
}

TEST_CASE(value_meta_holds_range_step_decimals_and_int32_ends)
{
    dk_value_meta_t m = { 2, "\xC2\xB0" "C", 4, INT32_MIN, INT32_MAX, 1 };
    dk_value_meta_t o;
    uint8_t b[DK_LEN_VALUE_META];

    CHECK_EQ(dk_value_meta_encode(&m, b), DK_LEN_VALUE_META);
    CHECK_EQ(b[8], 0x00);
    CHECK_EQ(b[11], 0x80);                      /* INT32_MIN, LSB first */
    CHECK_EQ(b[12], 0xFF);
    CHECK_EQ(b[15], 0x7F);
    CHECK(dk_value_meta_decode(b, sizeof b, &o));
    CHECK_EQ(o.min, INT32_MIN);
    CHECK_EQ(o.max, INT32_MAX);
    CHECK_EQ(o.step, 1);
    CHECK_EQ(o.decimals, 4);
    CHECK_STR_EQ(o.unit, "\xC2\xB0" "C");

    m.min = 5;
    m.max = 5;                                  /* one value */
    m.step = INT32_MAX;
    CHECK_EQ(dk_value_meta_encode(&m, b), DK_LEN_VALUE_META);
    m.max = 4;
    CHECK_EQ(dk_value_meta_encode(&m, b), 0u);
    m.max = 60;
    m.step = 0;
    CHECK_EQ(dk_value_meta_encode(&m, b), 0u);
    m.step = -1;
    CHECK_EQ(dk_value_meta_encode(&m, b), 0u);
    m.step = 1;
    m.decimals = 5;
    CHECK_EQ(dk_value_meta_encode(&m, b), 0u);
    m.decimals = 0;
    CHECK_EQ(dk_value_meta_encode(&m, b), DK_LEN_VALUE_META);
    b[7] = 1;                                   /* reserved byte */
    CHECK(!dk_value_meta_decode(b, sizeof b, &o));
}

TEST_CASE(value_round_trips_int32_ends_and_refuses_unknown_flags)
{
    dk_value_t v = { 200, INT32_MIN, INT32_MAX, DK_VAL_SET | DK_VAL_LIMIT };
    dk_value_t o;
    uint8_t b[DK_LEN_VALUE];

    CHECK_EQ(dk_value_encode(&v, b), DK_LEN_VALUE);
    CHECK(dk_value_decode(b, sizeof b, &o));
    CHECK_EQ(o.value, INT32_MIN);
    CHECK_EQ(o.was, INT32_MAX);
    CHECK_EQ(o.flags, 3);
    CHECK_EQ(o.seq, 200);
    v.value = -1;
    CHECK_EQ(dk_value_encode(&v, b), DK_LEN_VALUE);
    CHECK_EQ(b[2], 0xFF);
    CHECK_EQ(b[5], 0xFF);
    CHECK(dk_value_decode(b, sizeof b, &o));
    CHECK_EQ(o.value, -1);
    v.flags = 0x04;
    CHECK_EQ(dk_value_encode(&v, b), 0u);
}

TEST_CASE(live_motor_holds_throttle_temperatures_and_valid_bits)
{
    dk_live_motor_t m = { 7, DK_LIVE_ARMED | DK_LIVE_DRIVING | DK_LIVE_FAULT,
                          0x3F, DK_THROTTLE_MAX, 65535, 65535, -32768,
                          65535, DK_TEMP_MIN, DK_TEMP_MAX, 65535 };
    dk_live_motor_t o;
    uint8_t b[DK_LEN_LIVE_MOTOR];

    CHECK_EQ(dk_live_motor_encode(&m, b), DK_LEN_LIVE_MOTOR);
    CHECK_EQ(b[4], 0x10);                       /* 10,000 = 0x2710 */
    CHECK_EQ(b[5], 0x27);
    CHECK_EQ(b[10], 0x00);
    CHECK_EQ(b[11], 0x80);
    CHECK_EQ(b[14], 0xD8);                      /* -40 */
    CHECK(dk_live_motor_decode(b, sizeof b, &o));
    CHECK_EQ(o.ca, -32768);
    CHECK_EQ(o.esc_c, -40);
    CHECK_EQ(o.mot_c, 125);
    CHECK_EQ(o.thr_cpct, 10000);
    CHECK_EQ(o.run_s, 65535);

    m.thr_cpct = DK_THROTTLE_MAX + 1u;
    CHECK_EQ(dk_live_motor_encode(&m, b), 0u);
    m.thr_cpct = 0;
    m.esc_c = DK_TEMP_MIN - 1;
    CHECK_EQ(dk_live_motor_encode(&m, b), 0u);
    m.esc_c = 20;
    m.mot_c = 126;
    CHECK_EQ(dk_live_motor_encode(&m, b), 0u);
    m.mot_c = 20;
    m.flags = 0x08;
    CHECK_EQ(dk_live_motor_encode(&m, b), 0u);
    m.flags = 0;
    m.valid = 0x40;
    CHECK_EQ(dk_live_motor_encode(&m, b), 0u);

    /* Each reading without its valid bit must be 0. */
    {
        static const uint8_t bit[6] = { DK_MV_RPM, DK_MV_VOLT, DK_MV_CURR,
                                        DK_MV_POWER, DK_MV_ESC_T,
                                        DK_MV_MOT_T };
        for (unsigned k = 0; k < 6u; ++k) {
            dk_live_motor_t z = { 1, 0, 0x3F, 0, 1, 1, 1, 1, 1, 1, 0 };
            CHECK_EQ(dk_live_motor_encode(&z, b), DK_LEN_LIVE_MOTOR);
            z.valid = (uint8_t)(0x3Fu & ~bit[k]);
            CHECK_EQ(dk_live_motor_encode(&z, b), 0u);
        }
    }
}

TEST_CASE(live_servo_holds_pulses_angles_and_valid_flags)
{
    dk_live_servo_t s = { 9, 0x1F, DK_US_MAX, DK_DDEG_MAX, -DK_CDEG_MAX,
                          65535, -32768, 65535, DK_US_MIN, 32767 };
    dk_live_servo_t o;
    uint8_t b[DK_LEN_LIVE_SERVO];

    CHECK_EQ(dk_live_servo_encode(&s, b), DK_LEN_LIVE_SERVO);
    CHECK_EQ(b[3], 0xC4);                       /* 2500 = 0x09C4 */
    CHECK_EQ(b[4], 0x09);
    CHECK(dk_live_servo_decode(b, sizeof b, &o));
    CHECK_EQ(o.cmd_ddeg, 1800);
    CHECK_EQ(o.meas_cdeg, -18000);
    CHECK_EQ(o.rail_ca, -32768);
    CHECK_EQ(o.fb_us, 400);
    CHECK_EQ(o.fb_ca, 32767);

    s.cmd_us = 0;                               /* none */
    CHECK_EQ(dk_live_servo_encode(&s, b), DK_LEN_LIVE_SERVO);
    s.cmd_us = DK_US_MIN - 1u;
    CHECK_EQ(dk_live_servo_encode(&s, b), 0u);
    s.cmd_us = DK_US_MAX + 1u;
    CHECK_EQ(dk_live_servo_encode(&s, b), 0u);
    s.cmd_us = 1500;
    s.cmd_ddeg = DK_DDEG_MAX + 1;
    CHECK_EQ(dk_live_servo_encode(&s, b), 0u);
    s.cmd_ddeg = -DK_DDEG_MAX - 1;
    CHECK_EQ(dk_live_servo_encode(&s, b), 0u);
    s.cmd_ddeg = 0;
    s.meas_cdeg = DK_CDEG_MAX + 1;
    CHECK_EQ(dk_live_servo_encode(&s, b), 0u);
    s.meas_cdeg = 0;
    s.fb_us = DK_US_MAX + 1u;
    CHECK_EQ(dk_live_servo_encode(&s, b), 0u);
    s.fb_us = 0;                                /* FB set needs a pulse */
    CHECK_EQ(dk_live_servo_encode(&s, b), 0u);
    s.fb_us = 1500;
    s.flags = 0x20;
    CHECK_EQ(dk_live_servo_encode(&s, b), 0u);

    {
        dk_live_servo_t z = { 1, 0, 1500, 0, 0, 0, 0, 0, 0, 0 };
        CHECK_EQ(dk_live_servo_encode(&z, b), DK_LEN_LIVE_SERVO);
        z.meas_cdeg = 1;                        /* without DK_LIVE_MEAS */
        CHECK_EQ(dk_live_servo_encode(&z, b), 0u);
        z.meas_cdeg = 0;
        z.fb_ca = 1;                            /* without DK_LIVE_FB */
        CHECK_EQ(dk_live_servo_encode(&z, b), 0u);
        z.fb_ca = 0;
        z.rail_mv = 1;                          /* without DK_LIVE_RAIL */
        CHECK_EQ(dk_live_servo_encode(&z, b), 0u);
        z.rail_mv = 0;
        z.rail_dw = 1;
        CHECK_EQ(dk_live_servo_encode(&z, b), 0u);
        z.rail_dw = 0;
        z.rail_ca = -1;
        CHECK_EQ(dk_live_servo_encode(&z, b), 0u);
    }
}

TEST_CASE(state_holds_its_steps_and_the_full_panel_clock)
{
    dk_state_t s = { 255, 0xFFFFFFFFu, DK_STEP_MIN, DK_STEP_MAX,
                     DK_STA_ARMED | DK_STA_DEGRADED };
    dk_state_t o;
    uint8_t b[DK_LEN_STATE];

    CHECK_EQ(dk_state_encode(&s, b), DK_LEN_STATE);
    CHECK(dk_state_decode(b, sizeof b, &o));
    CHECK_EQ(o.panel_ms, 0xFFFFFFFFu);
    CHECK_EQ(o.thr_step, 1);
    CHECK_EQ(o.srv_step, 100);
    s.panel_ms = 0x00010203u;
    CHECK_EQ(dk_state_encode(&s, b), DK_LEN_STATE);
    CHECK_EQ(b[2], 0x03);
    CHECK_EQ(b[5], 0x00);
    s.thr_step = 0;
    CHECK_EQ(dk_state_encode(&s, b), 0u);
    s.thr_step = DK_STEP_MAX + 1u;
    CHECK_EQ(dk_state_encode(&s, b), 0u);
    s.thr_step = 10;
    s.srv_step = 0;
    CHECK_EQ(dk_state_encode(&s, b), 0u);
    s.srv_step = DK_STEP_MAX + 1u;
    CHECK_EQ(dk_state_encode(&s, b), 0u);
    s.srv_step = 10;
    s.flags = 0x04;
    CHECK_EQ(dk_state_encode(&s, b), 0u);
}

TEST_CASE(scale_holds_the_travel_and_both_pulse_ends_or_neither)
{
    uint8_t b[DK_LEN_SCALE];
    dk_scale_t s = { 7, 14591, 30000, 8000, 450, 500, 2500, 8400, 500 };
    dk_scale_t o;
    dk_panel_pkt_t p;

    CHECK_EQ(dk_scale_encode(&s, b), DK_LEN_SCALE);
    CHECK_EQ(b[0], DK_K_SCALE);
    CHECK_EQ(b[2], 0xFF);       /* 14,591 = 0x38FF, least significant first */
    CHECK_EQ(b[3], 0x38);
    CHECK(dk_scale_decode(b, DK_LEN_SCALE, &o));
    CHECK(o.rpm == 14591 && o.mv == 30000 && o.ca == 8000
          && o.travel == 450 && o.us_min == 500 && o.us_max == 2500
          && o.rail_mv == 8400 && o.rail_ca == 500);
    CHECK(dk_panel_decode(b, DK_LEN_SCALE, &p));
    CHECK_EQ(p.kind, DK_K_SCALE);
    CHECK(!dk_scale_decode(b, DK_LEN_SCALE - 1u, &o));
    CHECK(!dk_scale_decode(NULL, DK_LEN_SCALE, &o));
    CHECK(!dk_scale_decode(b, DK_LEN_SCALE, NULL));

    /* Every full scale may be 0, not known. */
    memset(&s, 0, sizeof s);
    CHECK_EQ(dk_scale_encode(&s, b), DK_LEN_SCALE);
    s.travel = DK_DDEG_MAX;
    CHECK_EQ(dk_scale_encode(&s, b), DK_LEN_SCALE);
    s.travel = DK_DDEG_MAX + 1u;
    CHECK_EQ(dk_scale_encode(&s, b), 0u);
    s.travel = 0;
    /* One pulse end alone, an end out of range or the ends out of order. */
    s.us_min = 500;
    CHECK_EQ(dk_scale_encode(&s, b), 0u);
    s.us_min = 0;
    s.us_max = 2500;
    CHECK_EQ(dk_scale_encode(&s, b), 0u);
    s.us_min = DK_US_MIN - 1u;
    CHECK_EQ(dk_scale_encode(&s, b), 0u);
    s.us_min = DK_US_MIN;
    s.us_max = DK_US_MAX;
    CHECK_EQ(dk_scale_encode(&s, b), DK_LEN_SCALE);
    s.us_max = DK_US_MAX + 1u;
    CHECK_EQ(dk_scale_encode(&s, b), 0u);
    s.us_min = 1500;
    s.us_max = 1500;
    CHECK_EQ(dk_scale_encode(&s, b), 0u);
    s.us_max = 1501;
    CHECK_EQ(dk_scale_encode(&s, b), DK_LEN_SCALE);
    CHECK_EQ(dk_scale_encode(NULL, b), 0u);
    CHECK_EQ(dk_scale_encode(&s, NULL), 0u);
}

TEST_CASE(every_packet_round_trips_through_its_encoder_and_decoder)
{
    uint8_t b[DK_PKT_MAX];
    dk_evt_t e = { 3, -5, DK_BTN_SCREEN, DK_G_PRESS, 0, 0, 1000, 2000 };
    dk_evt_t eo;
    dk_status_t s = { 4, 3712, DK_ST_USB, { 1, 2, 3 }, 60, -58, 2 };
    dk_status_t so;
    dk_panel_pkt_t p;
    dk_value_meta_t m = { 6, "%", 1, 0, 1000, 5 };
    dk_live_motor_t lm = { 8, DK_LIVE_ARMED, DK_MV_RPM | DK_MV_VOLT, 2500,
                           12000, 16800, 0, 0, 0, 0, 42 };
    dk_live_servo_t ls = { 9, DK_LIVE_MEAS, 1520, 45, 452, 0, 0, 0, 0, 0 };

    CHECK_EQ(dk_evt_encode(&e, b), DK_LEN_EVENT);
    CHECK(dk_evt_decode(b, DK_LEN_EVENT, &eo));
    CHECK(eo.seq == 3 && eo.counter == -5 && eo.buttons == DK_BTN_SCREEN
          && eo.gesture == DK_G_PRESS && eo.ms == 1000 && eo.echo == 2000);
    CHECK_EQ(dk_status_encode(&s, b), DK_LEN_STATUS);
    CHECK(dk_status_decode(b, DK_LEN_STATUS, &so));
    CHECK_EQ(so.batt_mv, 3712);
    CHECK_EQ(so.rssi, -58);
    CHECK_EQ(dk_value_meta_encode(&m, b), DK_LEN_VALUE_META);
    CHECK(dk_panel_decode(b, DK_LEN_VALUE_META, &p));
    CHECK_EQ(p.kind, DK_K_VALUE_META);
    CHECK_EQ(p.u.meta.max, 1000);
    CHECK_EQ(p.u.meta.step, 5);
    CHECK_EQ(dk_live_motor_encode(&lm, b), DK_LEN_LIVE_MOTOR);
    CHECK(dk_panel_decode(b, DK_LEN_LIVE_MOTOR, &p));
    CHECK_EQ(p.u.motor.rpm, 12000);
    CHECK_EQ(p.u.motor.mv, 16800);
    CHECK_EQ(p.u.motor.thr_cpct, 2500);
    CHECK_EQ(dk_live_servo_encode(&ls, b), DK_LEN_LIVE_SERVO);
    CHECK(dk_panel_decode(b, DK_LEN_LIVE_SERVO, &p));
    CHECK_EQ(p.u.servo.meas_cdeg, 452);
    CHECK_EQ(p.u.servo.cmd_us, 1520);

    CHECK_EQ(dk_evt_encode(NULL, b), 0u);
    CHECK_EQ(dk_evt_encode(&e, NULL), 0u);
    CHECK(!dk_evt_decode(b, DK_LEN_EVENT, NULL));
}

/** xorshift32: the fuzz's numbers, the same on every run. */
static uint32_t rnd(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

/** Re-encode a decoded panel packet. */
static size_t panel_encode(const dk_panel_pkt_t *p, uint8_t *out)
{
    switch (p->kind) {
    case DK_K_SCREEN:     return dk_screen_encode(&p->u.screen, out);
    case DK_K_NAV:        return dk_nav_encode(&p->u.nav, out);
    case DK_K_VALUE_META: return dk_value_meta_encode(&p->u.meta, out);
    case DK_K_VALUE:      return dk_value_encode(&p->u.value, out);
    case DK_K_LIVE_MOTOR: return dk_live_motor_encode(&p->u.motor, out);
    case DK_K_LIVE_SERVO: return dk_live_servo_encode(&p->u.servo, out);
    case DK_K_STATE:      return dk_state_encode(&p->u.state, out);
    case DK_K_SCALE:      return dk_scale_encode(&p->u.scale, out);
    default:              return 0;
    }
}

TEST_CASE(a_million_random_packets_decode_only_to_their_own_bytes)
{
    /* Each packet is a heap block of exactly its length, so a read past it
     * is a fault under AddressSanitizer.  A packet that decodes encodes to
     * the same bytes: one form per packet, no field left unchecked. */
    uint32_t seed = 0x2545F491u;
    unsigned taken = 0;
    unsigned bad = 0;
    for (unsigned i = 0; i < 1000000u; ++i) {
        const size_t len = rnd(&seed) % (DK_PKT_MAX + 3u);
        uint8_t *pkt = malloc(len == 0u ? 1u : len);
        uint8_t again[DK_PKT_MAX];
        dk_panel_pkt_t p;
        dk_evt_t e;
        dk_status_t s;
        dk_info_t inf;
        REQUIRE_ALLOC(pkt);
        for (size_t k = 0; k < len; ++k) {
            pkt[k] = (uint8_t)rnd(&seed);
        }
        if (len > 0u) {
            /* Most packets get a valid kind, or nearly all would stop at
             * byte 0. */
            static const uint8_t kinds[11] = {
                DK_K_EVENT, DK_K_STATUS, DK_K_INFO, DK_K_SCREEN, DK_K_NAV,
                DK_K_VALUE_META, DK_K_VALUE, DK_K_LIVE_MOTOR,
                DK_K_LIVE_SERVO, DK_K_STATE, DK_K_SCALE };
            if ((rnd(&seed) & 7u) != 0u) {
                pkt[0] = kinds[rnd(&seed) % 11u];
            }
            /* And a share get small fields, so range checks pass often. */
            if ((rnd(&seed) & 1u) != 0u) {
                for (size_t k = 2; k < len; ++k) {
                    pkt[k] = (uint8_t)(pkt[k] & 0x03u);
                }
            }
        }
        if (dk_panel_decode(pkt, len, &p)) {
            ++taken;
            if (panel_encode(&p, again) != len
                || memcmp(again, pkt, len) != 0) {
                ++bad;
            }
        }
        if (dk_evt_decode(pkt, len, &e)) {
            ++taken;
            if (dk_evt_encode(&e, again) != len
                || memcmp(again, pkt, len) != 0) {
                ++bad;
            }
        }
        if (dk_status_decode(pkt, len, &s)) {
            ++taken;
            if (dk_status_encode(&s, again) != len
                || memcmp(again, pkt, len) != 0) {
                ++bad;
            }
        }
        if (dk_info_decode(pkt, len, &inf)) {
            ++taken;
            if (dk_info_encode(&inf, again) != len
                || memcmp(again, pkt, len) != 0) {
                ++bad;
            }
        }
        (void)dk_adv_has_service(pkt, len);
        free(pkt);
    }
    CHECK_EQ(bad, 0u);
    CHECK(taken > 1000u);       /* the fuzz reaches past the range checks */
}

TEST_CASE(the_service_uuid_is_found_in_advertising_data)
{
    uint8_t ad[31] = {
        0x02, 0x01, 0x06,                       /* flags */
        0x11, 0x07, DK_UUID_LE(DK_UUID_SVC),    /* complete 128-bit list */
        0x05, 0x09, 'D', 'I', 'A', 'L',
    };

    CHECK(dk_adv_has_service(ad, 3u + 18u + 6u));
    CHECK_EQ(ad[5], 0x00);
    CHECK_EQ(ad[20], 0x8F);
    ad[4] = 0x06;                               /* incomplete list */
    CHECK(dk_adv_has_service(ad, 3u + 18u));
    ad[4] = 0x03;                               /* a 16-bit list */
    CHECK(!dk_adv_has_service(ad, 3u + 18u));
    ad[4] = 0x07;
    CHECK(!dk_adv_has_service(ad, 3u + 17u));   /* cut: length past end */
    ad[5] = DK_UUID_EVENT;                      /* a characteristic's UUID */
    CHECK(!dk_adv_has_service(ad, 3u + 18u));
    ad[5] = DK_UUID_SVC;
    ad[20] = 0x8E;
    CHECK(!dk_adv_has_service(ad, 3u + 18u));
    ad[20] = 0x8F;
    CHECK(!dk_adv_has_service(ad, 0));
    CHECK(!dk_adv_has_service(NULL, 31));

    /* The service second in a list of two, whole and then cut. */
    {
        const uint8_t svc[16] = { DK_UUID_LE(DK_UUID_SVC) };
        uint8_t other[40];
        memset(other, 0, sizeof other);
        other[0] = 33u;                         /* type + 2 x 16 bytes */
        other[1] = 0x07;
        memset(other + 2, 0xAA, 16);
        memcpy(other + 18, svc, 16);
        CHECK(dk_adv_has_service(other, 34));
        CHECK(!dk_adv_has_service(other, 33));  /* a length past the end */
        other[0] = 29u;                         /* the second is cut */
        CHECK(!dk_adv_has_service(other, 30));
    }
}

TEST_CASE(a_value_is_written_with_its_unit_and_no_space)
{
    char t[24];

    CHECK_EQ(dk_value_text(125, 1, "%", t, sizeof t), 5u);
    CHECK_STR_EQ(t, "12.5%");
    CHECK_EQ(dk_value_text(-5, 2, "V", t, sizeof t), 6u);
    CHECK_STR_EQ(t, "-0.05V");
    dk_value_text(0, 0, NULL, t, sizeof t);
    CHECK_STR_EQ(t, "0");
    dk_value_text(0, 3, "", t, sizeof t);
    CHECK_STR_EQ(t, "0.000");
    dk_value_text(INT32_MIN, 0, "", t, sizeof t);
    CHECK_STR_EQ(t, "-2147483648");
    dk_value_text(INT32_MIN, 4, "\xC2\xB0" "C", t, sizeof t);
    CHECK_STR_EQ(t, "-214748.3648\xC2\xB0" "C");
    dk_value_text(INT32_MAX, 9, "ms", t, sizeof t);  /* decimals cut to 4 */
    CHECK_STR_EQ(t, "214748.3647ms");
    dk_value_text(1500, 0, "us", t, sizeof t);
    CHECK_STR_EQ(t, "1500us");

    CHECK_EQ(dk_value_text(1500, 0, "us", t, 7), 6u);   /* fits exactly */
    CHECK_EQ(dk_value_text(1500, 0, "us", t, 6), 0u);   /* one short */
    CHECK_STR_EQ(t, "");
    CHECK_EQ(dk_value_text(-1, 1, "", t, 5), 4u);       /* "-0.1" */
    CHECK_EQ(dk_value_text(-1, 1, "", t, 4), 0u);
    CHECK_EQ(dk_value_text(1, 0, "", NULL, 4), 0u);
    CHECK_EQ(dk_value_text(1, 0, "", t, 0), 0u);
}

int main(void)
{
    RUN(an_event_decodes_every_field_from_its_byte);
    RUN(an_event_with_a_field_past_its_range_is_refused);
    RUN(a_packet_of_a_wrong_length_or_kind_is_refused);
    RUN(a_status_holds_its_battery_and_rssi_bounds);
    RUN(info_carries_the_protocol_and_an_ascii_name);
    RUN(a_title_is_utf8_padded_with_nul_and_cut_on_a_character);
    RUN(the_encoder_cuts_a_long_text_at_a_whole_character);
    RUN(nav_holds_index_below_count_and_three_slots);
    RUN(screen_refuses_an_unknown_face_language_or_flag);
    RUN(value_meta_holds_range_step_decimals_and_int32_ends);
    RUN(value_round_trips_int32_ends_and_refuses_unknown_flags);
    RUN(live_motor_holds_throttle_temperatures_and_valid_bits);
    RUN(live_servo_holds_pulses_angles_and_valid_flags);
    RUN(state_holds_its_steps_and_the_full_panel_clock);
    RUN(scale_holds_the_travel_and_both_pulse_ends_or_neither);
    RUN(every_packet_round_trips_through_its_encoder_and_decoder);
    RUN(a_million_random_packets_decode_only_to_their_own_bytes);
    RUN(the_service_uuid_is_found_in_advertising_data);
    RUN(a_value_is_written_with_its_unit_and_no_space);
    return test_summary("dialknob");
}
