/*
 * The registry of programmers (prog_registry.h): the table as it ships,
 * its checks, a row's state and reason, the list rules and the view, the
 * rules exercised on fake tables.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "esc_stick_panel.h"
#include "kst_panel.h"
#include "link_pages.h"
#include "prog_registry.h"
#include "ui_text.h"
#include "ui_theme.h"

#define W UI_PANEL_W
#define H UI_PANEL_H

static gfx_color_t *fb;
static gfx_canvas_t cv;

static void canvas(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
}

static touch_event_t evt(touch_event_type_t t, int x, int y, uint8_t id)
{
    const touch_event_t e = { .type = t,
                              .point = { .id = id, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 1 } };
    return e;
}

static int mid_x(gfx_rect_t r) { return r.x + r.w / 2; }
static int mid_y(gfx_rect_t r) { return r.y + r.h / 2; }

/* ------------------------------------------------- two fake sessions */

typedef struct {
    bool    open, holds;
    uint8_t pin;
    int     backs;
} fake_t;

static fake_t fa, fb_;

static void a_init(gfx_rect_t r) { (void)r; memset(&fa, 0, sizeof(fa)); }
static void a_none(void) { }
static void a_open(void) { fa.open = true; }
static bool a_is_open(void) { return fa.open; }
static void a_back(void) { ++fa.backs; }
static bool a_holds(void) { return fa.holds; }
static void a_armed(bool on) { (void)on; }
static void a_tick(float dt) { (void)dt; }
static bool a_press(uint8_t id) { (void)id; return false; }
static void a_event(const touch_event_t *e) { (void)e; }
static void a_render(gfx_canvas_t *c) { (void)c; }
static uint32_t a_rev(void) { return 0u; }
static gfx_rect_t a_rect(void) { return (gfx_rect_t){ 0, 0, 0, 0 }; }
static uint8_t a_pin(void) { return fa.pin; }
static bool a_safe(int x, int y) { (void)x; (void)y; return false; }

static bool b_holds(void) { return fb_.holds; }
static uint8_t b_pin(void) { return fb_.pin; }

static const prog_page_set_t k_set_a = {
    a_init, a_none, a_open, a_is_open, a_back, a_holds, a_armed, a_tick,
    a_press, a_event, a_render, a_rev, a_rev, a_rect, a_none, a_none, a_pin,
    a_safe,
};
static const prog_page_set_t k_set_b = {
    a_init, a_none, a_open, a_is_open, a_back, b_holds, a_armed, a_tick,
    a_press, a_event, a_render, a_rev, a_rev, a_rect, a_none, a_none, b_pin,
    a_safe,
};
/* A set with a hook missing. */
static const prog_page_set_t k_set_holed = {
    a_init, a_none, a_open, a_is_open, a_back, a_holds, a_armed, a_tick,
    a_press, a_event, a_render, a_rev, a_rev, a_rect, a_none, a_none, NULL,
    a_safe,
};

static void fakes_fresh(void)
{
    memset(&fa, 0, sizeof(fa));
    memset(&fb_, 0, sizeof(fb_));
    fa.pin = PROG_REG_NO_PIN;
    fb_.pin = PROG_REG_NO_PIN;
}

/* A row of SERVO's PROGRAM list on @p set. */
static prog_reg_row_t row_of(ui_text_id_t name, ui_text_id_t group,
                             uint8_t port, const prog_page_set_t *set)
{
    return (prog_reg_row_t){ PROG_DEV_SERVO, UI_WB_PROGRAM, name, group,
                             TX_SV_TARGET_PORT, port, LINK_CAP_SERVO_PWM,
                             set, false };
}

/* ------------------------------------------------------ the table */

/* The table as it ships: sound in every language, every row's name and
 * page set present.  SERVO's PROGRAM list is one live row, the KST and
 * Chaservo programmer on the tester's programming port, over the left
 * column.  MOTOR
 * & ESC's is one row too, ESC STICK under STICK MENU, full width, holding
 * no port and needing no capability.  The modelled protocols without a
 * transport (prog_models.h) are no row. */
TEST_CASE(the_shipped_table_is_sound_and_lists_the_kst_programmer)
{
    size_t n = 0;
    const prog_reg_row_t *rows = prog_registry(&n);
    if (rows == NULL || n < 1u) {
        T_FAIL("no table");
        return;
    }
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        size_t at = 99u;
        CHECK_EQ(prog_reg_check(rows, n, &at), PROG_REG_OK);
        CHECK_EQ(at, 99u);
        for (size_t i = 0; i < n; ++i) {
            CHECK(ui_tr(rows[i].name)[0] != '\0');
            CHECK(ui_tr(rows[i].group)[0] != '\0');
            CHECK(ui_tr(rows[i].transport)[0] != '\0');
            CHECK(rows[i].page_set != NULL);
        }
    }
    ui_text_set_language(UI_LANG_EN);

    prog_list_t l;
    prog_list_build(&l, rows, n, PROG_DEV_SERVO, UI_WB_PROGRAM, NULL);
    CHECK_EQ(l.total, 1u);
    CHECK_EQ(l.count, 1u);
    CHECK(!l.search);
    CHECK(!l.headings);
    const prog_reg_row_t *kst = &rows[l.e[0].row];
    CHECK_EQ(kst->name, TX_PL_KST);
    CHECK_STR_EQ(ui_tr(kst->name), "KST and Chaservo servos");
    CHECK_EQ(kst->transport, TX_SV_TARGET_PORT);
    CHECK_EQ(kst->port, PROG_REG_TESTER_PORT);
    CHECK_EQ(kst->needs_capability, (uint16_t)LINK_CAP_SERVO_PWM);
    CHECK(kst->page_set == &kst_panel_pages);
    CHECK(!kst->full_width);
    for (size_t i = 0; i < n; ++i) {
        CHECK(strstr(ui_tr(rows[i].name), "Hitec") == NULL);
    }
    /* MOTOR & ESC's list: ESC STICK alone. */
    prog_list_build(&l, rows, n, PROG_DEV_MOTOR, UI_WB_PROGRAM, NULL);
    CHECK_EQ(l.total, 1u);
    CHECK_EQ(l.count, 1u);
    CHECK(!l.search);
    CHECK(!l.headings);
    const prog_reg_row_t *stick = &rows[l.e[0].row];
    CHECK_EQ(stick->name, TX_PL_STICK);
    CHECK_STR_EQ(ui_tr(stick->name), "ESCs by throttle stick");
    CHECK_EQ(stick->group, TX_PL_GRP_STICK);
    CHECK_STR_EQ(ui_tr(stick->group), "STICK MENU");
    CHECK_EQ(stick->transport, TX_MO_SET_THROTTLE);
    CHECK_EQ(stick->port, PROG_REG_NO_PORT);
    CHECK_EQ(stick->needs_capability, 0u);
    CHECK(stick->page_set == &esc_stick_pages);
    CHECK(stick->full_width);
    CHECK_EQ(prog_reg_state(rows, n, l.e[0].row, 0u), PROG_ROW_LIVE);
    /* No TEST list yet. */
    prog_list_build(&l, rows, n, PROG_DEV_MOTOR, UI_WB_TEST, NULL);
    CHECK_EQ(l.total, 0u);
    prog_list_build(&l, rows, n, PROG_DEV_SERVO, UI_WB_TEST, NULL);
    CHECK_EQ(l.total, 0u);
    /* A NULL count is allowed. */
    CHECK(prog_registry(NULL) == rows);
}

/* Each fault the check finds, on the row it names. */
TEST_CASE(the_check_names_each_fault_and_its_row)
{
    ui_text_set_language(UI_LANG_EN);
    prog_reg_row_t r[2];
    size_t at = 99u;

    r[0] = row_of(TX_PL_KST, TX_PL_GRP_PROG_PORT, 0u, &k_set_a);
    r[1] = row_of(TX_WB_LIVE, TX_PL_GRP_PROG_PORT, 0u, &k_set_b);
    CHECK_EQ(prog_reg_check(r, 2, &at), PROG_REG_OK);
    CHECK_EQ(at, 99u);
    CHECK_EQ(prog_reg_check(NULL, 2, &at), PROG_REG_OK);
    CHECK_EQ(prog_reg_check(r, 0, &at), PROG_REG_OK);

    static const struct { int field; prog_reg_fault_t f; } k[] = {
        { 0, PROG_REG_BAD_DEVICE }, { 1, PROG_REG_BAD_TASK },
        { 2, PROG_REG_NO_NAME },    { 3, PROG_REG_NO_GROUP },
        { 4, PROG_REG_NO_TRANSPORT }, { 5, PROG_REG_BAD_PORT },
        { 6, PROG_REG_NO_PAGE_SET }, { 7, PROG_REG_NO_PAGE_SET },
        { 8, PROG_REG_SAME_NAME },  { 9, PROG_REG_BAD_TASK },
        { 10, PROG_REG_BAD_WIDTH }, { 11, PROG_REG_BAD_WIDTH },
    };
    for (size_t c = 0; c < sizeof(k) / sizeof(k[0]); ++c) {
        r[0] = row_of(TX_PL_KST, TX_PL_GRP_PROG_PORT, 0u, &k_set_a);
        r[1] = row_of(TX_WB_LIVE, TX_PL_GRP_PROG_PORT, 0u, &k_set_b);
        switch (k[c].field) {
        case 0: r[1].device = PROG_DEVICES; break;
        case 1: r[1].task = UI_WB_LIVE; break;
        case 2: r[1].name = TX_COUNT; break;
        case 3: r[1].group = TX_COUNT; break;
        case 4: r[1].transport = TX_COUNT; break;
        case 5: r[1].port = PROG_REG_PORTS; break;
        case 6: r[1].page_set = NULL; break;
        case 7: r[1].page_set = &k_set_holed; break;
        case 8: r[1].name = TX_PL_KST; break;
        case 9: r[1].task = UI_WB_SETTINGS; break;
        case 10: r[1].full_width = true; break;      /* SERVO full width */
        default: r[1].device = PROG_DEV_MOTOR; break; /* MOTOR not so */
        }
        at = 99u;
        CHECK_EQ(prog_reg_check(r, 2, &at), k[c].f);
        CHECK_EQ(at, 1u);
    }
    /* The last port, and none, are in range; one name on two lists is
     * no fault. */
    r[0] = row_of(TX_PL_KST, TX_PL_GRP_PROG_PORT, PROG_REG_PORTS - 1u,
                  &k_set_a);
    r[1] = row_of(TX_PL_KST, TX_PL_GRP_PROG_PORT, PROG_REG_NO_PORT,
                  &k_set_b);
    r[1].task = UI_WB_TEST;
    CHECK_EQ(prog_reg_check(r, 2, NULL), PROG_REG_OK);
}

/* A list holds PROG_LIST_MAX rows; one more fails the check, and the list
 * shows the first PROG_LIST_MAX. */
TEST_CASE(a_list_over_its_maximum_fails_the_check)
{
    static prog_reg_row_t r[PROG_LIST_MAX + 1];
    for (int i = 0; i < PROG_LIST_MAX + 1; ++i) {
        r[i] = row_of((ui_text_id_t)i, TX_PL_GRP_PROG_PORT,
                      PROG_REG_NO_PORT, &k_set_a);
    }
    size_t at = 99u;
    CHECK_EQ(prog_reg_check(r, PROG_LIST_MAX, NULL), PROG_REG_OK);
    CHECK_EQ(prog_reg_check(r, PROG_LIST_MAX + 1, &at), PROG_REG_TOO_MANY);
    CHECK_EQ(at, 0u);
    prog_list_t l;
    prog_list_build(&l, r, PROG_LIST_MAX + 1, PROG_DEV_SERVO, UI_WB_PROGRAM,
                    NULL);
    CHECK_EQ(l.total, (uint8_t)PROG_LIST_MAX);
    CHECK_EQ(l.rows, (uint8_t)PROG_LIST_MAX);
}

/*
 * A device's sessions: each page set its rows open, once, in the table's
 * order, and none of the other device's; a row that opens a set full
 * width makes the set full width.  The shipped table: SERVO opens the KST
 * session, MOTOR & ESC ESC STICK, full width.
 */
TEST_CASE(a_devices_sessions_are_each_listed_once)
{
    ui_text_set_language(UI_LANG_EN);
    const prog_page_set_t *set[PROG_LIST_MAX];
    CHECK_EQ(prog_reg_sets(PROG_DEV_SERVO, set), 1);
    CHECK(set[0] == &kst_panel_pages);
    CHECK_EQ(prog_reg_sets(PROG_DEV_MOTOR, set), 1);
    CHECK(set[0] == &esc_stick_pages);
    CHECK(prog_reg_full_width(&esc_stick_pages));
    CHECK(!prog_reg_full_width(&kst_panel_pages));
    CHECK(!prog_reg_full_width(NULL));
    CHECK_EQ(prog_reg_sets(PROG_DEVICES, set), 0);

    /* Three rows: two SERVO rows on set A, one MOTOR row on set B, full
     * width; the check is content. */
    prog_reg_row_t r[3];
    r[0] = row_of(TX_PL_KST, TX_PL_GRP_PROG_PORT, 0u, &k_set_a);
    r[1] = row_of(TX_WB_LIVE, TX_PL_GRP_PROG_PORT, 0u, &k_set_a);
    r[2] = row_of(TX_PL_STICK, TX_PL_GRP_STICK, PROG_REG_NO_PORT, &k_set_b);
    r[2].device = PROG_DEV_MOTOR;
    r[2].full_width = true;
    CHECK_EQ(prog_reg_check(r, 3, NULL), PROG_REG_OK);
    prog_registry_use(r, 3);
    CHECK_EQ(prog_reg_sets(PROG_DEV_SERVO, set), 1);
    CHECK(set[0] == &k_set_a);
    CHECK_EQ(prog_reg_sets(PROG_DEV_MOTOR, set), 1);
    CHECK(set[0] == &k_set_b);
    CHECK(prog_reg_full_width(&k_set_b));
    CHECK(!prog_reg_full_width(&k_set_a));
    /* An empty table: no session. */
    prog_registry_use(r, 0);
    CHECK_EQ(prog_reg_sets(PROG_DEV_SERVO, set), 0);
    CHECK(!prog_reg_full_width(&k_set_b));
    prog_registry_use(NULL, 0);
}

/* ------------------------------------------------- a row's state */

/* A row whose capability the IOmcu does not report is dimmed with its
 * reason, the lowest missing bit named, in both languages. */
TEST_CASE(a_row_the_iomcu_lacks_is_dimmed_with_its_reason)
{
    fakes_fresh();
    prog_reg_row_t r[1] = { row_of(TX_PL_KST, TX_PL_GRP_PROG_PORT, 0u,
                                   &k_set_a) };
    char out[48];

    ui_text_set_language(UI_LANG_EN);
    CHECK_EQ(prog_reg_state(r, 1, 0, 0u), PROG_ROW_NO_CAP);
    CHECK(prog_reg_detail(r, 1, 0, 0u, out, sizeof(out)));
    CHECK_STR_EQ(out, "IOMCU HAS NO SERVO PWM");
    /* Other bits do not stand in for it. */
    CHECK(prog_reg_detail(r, 1, 0, (uint16_t)~LINK_CAP_SERVO_PWM, out,
                          sizeof(out)));
    ui_text_set_language(UI_LANG_DE);
    CHECK(prog_reg_detail(r, 1, 0, 0u, out, sizeof(out)));
    CHECK_STR_EQ(out, "IOMCU OHNE SERVO PWM");
    ui_text_set_language(UI_LANG_EN);

    /* Reported: live, the port's number and the session's pin. */
    CHECK_EQ(prog_reg_state(r, 1, 0, LINK_CAP_SERVO_PWM), PROG_ROW_LIVE);
    CHECK(!prog_reg_detail(r, 1, 0, LINK_CAP_SERVO_PWM, out, sizeof(out)));
    CHECK_STR_EQ(out, "PROG PORT 1");
    fa.pin = 13u;
    CHECK(!prog_reg_detail(r, 1, 0, LINK_CAP_SERVO_PWM, out, sizeof(out)));
    CHECK_STR_EQ(out, "PROG PORT 1 GP13");
    ui_text_set_language(UI_LANG_DE);
    CHECK(!prog_reg_detail(r, 1, 0, LINK_CAP_SERVO_PWM, out, sizeof(out)));
    CHECK_STR_EQ(out, "PROG-PORT 1 GP13");
    ui_text_set_language(UI_LANG_EN);

    /* Two bits missing: the lower is named. */
    r[0].needs_capability = LINK_CAP_PROGRAM | LINK_CAP_ESC_TELEM;
    CHECK_EQ(prog_reg_missing(&r[0], 0u), (uint16_t)LINK_CAP_ESC_TELEM);
    CHECK_EQ(prog_reg_missing(&r[0], LINK_CAP_ESC_TELEM),
             (uint16_t)LINK_CAP_PROGRAM);
    CHECK_EQ(prog_reg_missing(&r[0], 0xFFFFu), 0u);
    CHECK_EQ(prog_reg_missing(NULL, 0u), 0u);
    CHECK(prog_reg_detail(r, 1, 0, 0u, out, sizeof(out)));
    CHECK_STR_EQ(out, "IOMCU HAS NO ESC TELEMETRY");
    /* A row that needs nothing is live with nothing reported. */
    r[0].needs_capability = 0u;
    CHECK_EQ(prog_reg_state(r, 1, 0, 0u), PROG_ROW_LIVE);

    /* Every named bit, and none for others. */
    CHECK_STR_EQ(prog_reg_cap_name(LINK_CAP_ESC_DRIVE), "ESC DRIVE");
    CHECK_STR_EQ(prog_reg_cap_name(LINK_CAP_PROGRAM), "PROGRAM");
    CHECK_STR_EQ(prog_reg_cap_name(0u), "");
    CHECK_STR_EQ(prog_reg_cap_name(1u << 9), "");
    CHECK_STR_EQ(prog_reg_cap_name(3u), "");

    /* Out of range and no room. */
    CHECK_EQ(prog_reg_state(r, 1, 1, 0xFFFFu), PROG_ROW_NO_CAP);
    CHECK_EQ(prog_reg_state(NULL, 1, 0, 0xFFFFu), PROG_ROW_NO_CAP);
    CHECK(prog_reg_detail(r, 1, 1, 0xFFFFu, out, sizeof(out)));
    CHECK_STR_EQ(out, "");
    CHECK(prog_reg_detail(r, 1, 0, 0xFFFFu, NULL, 4));
    out[0] = 'x';
    CHECK(prog_reg_detail(r, 1, 0, 0xFFFFu, out, 0));
    CHECK_EQ(out[0], 'x');
    fa.pin = 13u;
    r[0].needs_capability = LINK_CAP_SERVO_PWM;
    CHECK(!prog_reg_detail(r, 1, 0, LINK_CAP_SERVO_PWM, out, 6));
    CHECK_STR_EQ(out, "PROG ");
}

/* Two rows on one port are exclusive: while one session holds the port the
 * other is PORT IN USE, and free again once it lets go.  Another port,
 * no port, and a row on the same session do not block. */
TEST_CASE(two_rows_on_one_port_are_exclusive)
{
    fakes_fresh();
    ui_text_set_language(UI_LANG_EN);
    prog_reg_row_t r[4] = {
        row_of(TX_PL_KST, TX_PL_GRP_PROG_PORT, 0u, &k_set_a),
        row_of(TX_WB_LIVE, TX_PL_GRP_PROG_PORT, 0u, &k_set_b),
        row_of(TX_ON, TX_PL_GRP_PROG_PORT, 1u, &k_set_b),
        row_of(TX_OFF, TX_PL_GRP_PROG_PORT, PROG_REG_NO_PORT, &k_set_b),
    };
    const uint16_t caps = LINK_CAP_SERVO_PWM;
    char out[48];

    CHECK_EQ(prog_reg_check(r, 4, NULL), PROG_REG_OK);
    for (size_t i = 0; i < 4; ++i) {
        CHECK_EQ(prog_reg_state(r, 4, i, caps), PROG_ROW_LIVE);
    }
    fa.holds = true;
    CHECK_EQ(prog_reg_state(r, 4, 0, caps), PROG_ROW_LIVE);
    CHECK_EQ(prog_reg_state(r, 4, 1, caps), PROG_ROW_PORT_BUSY);
    CHECK(prog_reg_detail(r, 4, 1, caps, out, sizeof(out)));
    CHECK_STR_EQ(out, "PORT IN USE");
    ui_text_set_language(UI_LANG_DE);
    CHECK(prog_reg_detail(r, 4, 1, caps, out, sizeof(out)));
    CHECK_STR_EQ(out, "PORT BELEGT");
    ui_text_set_language(UI_LANG_EN);
    CHECK_EQ(prog_reg_state(r, 4, 2, caps), PROG_ROW_LIVE);
    CHECK_EQ(prog_reg_state(r, 4, 3, caps), PROG_ROW_LIVE);
    /* The other way round. */
    fa.holds = false;
    fb_.holds = true;
    CHECK_EQ(prog_reg_state(r, 4, 0, caps), PROG_ROW_PORT_BUSY);
    CHECK_EQ(prog_reg_state(r, 4, 1, caps), PROG_ROW_LIVE);
    /* A missing capability outranks a busy port. */
    CHECK_EQ(prog_reg_state(r, 4, 0, 0u), PROG_ROW_NO_CAP);
    /* Free again. */
    fb_.holds = false;
    CHECK_EQ(prog_reg_state(r, 4, 0, caps), PROG_ROW_LIVE);
    /* Two rows of one session on one port: the same session. */
    r[1].page_set = &k_set_a;
    fa.holds = true;
    CHECK_EQ(prog_reg_state(r, 4, 1, caps), PROG_ROW_LIVE);
}

/* A row on the tester's port follows SERVO TESTER: the port it names, its
 * number and pin in the detail, PORT IN USE against a row on that port
 * and while the HITEC page holds it, unless its own session does. */
TEST_CASE(a_row_on_the_testers_port_follows_the_tester)
{
    fakes_fresh();
    ui_text_set_language(UI_LANG_EN);
    prog_reg_row_t r[2] = {
        row_of(TX_PL_KST, TX_PL_GRP_PROG_PORT, PROG_REG_TESTER_PORT,
               &k_set_a),
        row_of(TX_WB_LIVE, TX_PL_GRP_PROG_PORT, 1u, &k_set_b),
    };
    const uint16_t caps = LINK_CAP_SERVO_PWM;
    char out[48];

    CHECK_EQ(prog_reg_check(r, 2, NULL), PROG_REG_OK);
    /* The tester on the outputs: no port, the transport alone. */
    prog_registry_set_ports(PROG_REG_NO_PORT, 0u);
    CHECK_EQ(prog_reg_port(&r[0]), PROG_REG_NO_PORT);
    CHECK(!prog_reg_detail(r, 2, 0, caps, out, sizeof(out)));
    CHECK_STR_EQ(out, "PROG PORT");
    fa.pin = 13u;
    CHECK(!prog_reg_detail(r, 2, 0, caps, out, sizeof(out)));
    CHECK_STR_EQ(out, "PROG PORT GP13");
    /* PORT 2: its number and pin. */
    prog_registry_set_ports(1u, 0u);
    CHECK_EQ(prog_reg_port(&r[0]), 1u);
    fa.pin = 14u;
    CHECK(!prog_reg_detail(r, 2, 0, caps, out, sizeof(out)));
    CHECK_STR_EQ(out, "PROG PORT 2 GP14");
    fa.pin = PROG_REG_NO_PIN;
    CHECK(!prog_reg_detail(r, 2, 0, caps, out, sizeof(out)));
    CHECK_STR_EQ(out, "PROG PORT 2");
    ui_text_set_language(UI_LANG_DE);
    CHECK(!prog_reg_detail(r, 2, 0, caps, out, sizeof(out)));
    CHECK_STR_EQ(out, "PROG-PORT 2");
    ui_text_set_language(UI_LANG_EN);
    /* The other row on port 2 holds it. */
    fb_.holds = true;
    CHECK_EQ(prog_reg_state(r, 2, 0, caps), PROG_ROW_PORT_BUSY);
    prog_registry_set_ports(2u, 0u);
    CHECK_EQ(prog_reg_state(r, 2, 0, caps), PROG_ROW_LIVE);
    fb_.holds = false;
    /* The HITEC page holds port 3: every row on it is PORT IN USE, a row
     * on another port is not. */
    prog_registry_set_ports(2u, 0x4u);
    CHECK_EQ(prog_reg_state(r, 2, 0, caps), PROG_ROW_PORT_BUSY);
    CHECK(prog_reg_detail(r, 2, 0, caps, out, sizeof(out)));
    CHECK_STR_EQ(out, "PORT IN USE");
    CHECK_EQ(prog_reg_state(r, 2, 1, caps), PROG_ROW_LIVE);
    /* Unless the row's own session holds the port. */
    fa.holds = true;
    CHECK_EQ(prog_reg_state(r, 2, 0, caps), PROG_ROW_LIVE);
    fa.holds = false;
    /* A port past the four and bits past them are none. */
    prog_registry_set_ports(4u, 0xF0u);
    CHECK_EQ(prog_reg_port(&r[0]), PROG_REG_NO_PORT);
    CHECK_EQ(prog_reg_state(r, 2, 1, caps), PROG_ROW_LIVE);
    CHECK_EQ(prog_reg_port(NULL), PROG_REG_NO_PORT);
    /* A port value that is neither a port, none nor the tester's fails
     * the check. */
    r[1].port = 0xFDu;
    CHECK_EQ(prog_reg_check(r, 2, NULL), PROG_REG_BAD_PORT);
    prog_registry_set_ports(PROG_REG_NO_PORT, 0u);
}

/* ---------------------------------------------------- the list rules */

/* @p n rows of SERVO's PROGRAM list, @p g of them under a second group,
 * every fifth named for the search. */
static prog_reg_row_t g_rows[PROG_LIST_MAX + 4];

static size_t fake_list(int n, int g)
{
    int k = 0;
    /* One row of another device and one of TEST first: never listed. */
    g_rows[k] = row_of(TX_PL_KST, TX_PL_GRP_PROG_PORT, PROG_REG_NO_PORT,
                       &k_set_a);
    g_rows[k++].device = PROG_DEV_MOTOR;
    g_rows[k] = row_of(TX_PL_KST, TX_PL_GRP_PROG_PORT, PROG_REG_NO_PORT,
                       &k_set_a);
    g_rows[k++].task = UI_WB_TEST;
    for (int i = 0; i < n; ++i) {
        g_rows[k++] = row_of((i % 5 == 0) ? TX_PL_KST : TX_WB_LIVE,
                             (i % 2 == 1 && g > 0) ? TX_SP_FIND
                                                   : TX_PL_GRP_PROG_PORT,
                             PROG_REG_NO_PORT, &k_set_a);
        if (i % 2 == 1 && g > 0) {
            --g;
        }
    }
    return (size_t)k;
}

/* SEARCH from the tenth row, not at nine; headings from the ninth row
 * shown, not at eight. */
TEST_CASE(search_comes_over_nine_rows_and_headings_over_eight)
{
    ui_text_set_language(UI_LANG_EN);
    prog_list_t l;
    static const struct { int n; bool search, headings; } k[] = {
        { 0, false, false }, { 1, false, false }, { 8, false, false },
        { 9, false, true },  { 10, true, true },  { 48, true, true },
    };
    for (size_t c = 0; c < sizeof(k) / sizeof(k[0]); ++c) {
        const size_t n = fake_list(k[c].n, 0);
        prog_list_build(&l, g_rows, n, PROG_DEV_SERVO, UI_WB_PROGRAM, NULL);
        CHECK_EQ(l.total, (uint8_t)k[c].n);
        CHECK_EQ(l.rows, (uint8_t)k[c].n);
        CHECK_EQ(l.search, k[c].search);
        CHECK_EQ(l.headings, k[c].headings);
        /* One group: one heading over the rows when there are headings. */
        CHECK_EQ(l.count, (uint8_t)(k[c].n + (k[c].headings ? 1 : 0)));
        if (k[c].headings) {
            CHECK(l.e[0].heading);
            CHECK(!l.e[1].heading);
        }
    }
}

/* Headings put each group where the table first names it, its rows in the
 * table's order under it. */
TEST_CASE(headings_group_the_rows_in_the_tables_order)
{
    const size_t n = fake_list(9, 4);   /* rows 1, 3, 5, 7 in the second */
    prog_list_t l;
    prog_list_build(&l, g_rows, n, PROG_DEV_SERVO, UI_WB_PROGRAM, NULL);
    CHECK(l.headings);
    CHECK_EQ(l.count, 11u);
    CHECK(l.e[0].heading);
    CHECK_EQ(g_rows[l.e[0].row].group, TX_PL_GRP_PROG_PORT);
    static const uint8_t first[] = { 2, 4, 6, 8, 10 };
    for (int i = 0; i < 5; ++i) {
        CHECK(!l.e[1 + i].heading);
        CHECK_EQ(l.e[1 + i].row, first[i]);
    }
    CHECK(l.e[6].heading);
    CHECK_EQ(g_rows[l.e[6].row].group, TX_SP_FIND);
    static const uint8_t second[] = { 3, 5, 7, 9 };
    for (int i = 0; i < 4; ++i) {
        CHECK(!l.e[7 + i].heading);
        CHECK_EQ(l.e[7 + i].row, second[i]);
    }
}

/* The search narrows the rows by name, "*" for any text, without regard
 * to case; SEARCH stays while it narrows, and headings go once eight rows
 * or fewer are shown. */
TEST_CASE(the_search_narrows_the_list_and_keeps_its_box)
{
    ui_text_set_language(UI_LANG_EN);
    const size_t n = fake_list(12, 0);   /* rows 0, 5, 10: KST */
    prog_list_t l;
    prog_list_build(&l, g_rows, n, PROG_DEV_SERVO, UI_WB_PROGRAM, "kst");
    CHECK_EQ(l.total, 12u);
    CHECK_EQ(l.rows, 3u);
    CHECK(l.search);
    CHECK(!l.headings);
    CHECK_EQ(l.count, 3u);
    CHECK_EQ(l.e[0].row, 2u);
    CHECK_EQ(l.e[1].row, 7u);
    CHECK_EQ(l.e[2].row, 12u);
    prog_list_build(&l, g_rows, n, PROG_DEV_SERVO, UI_WB_PROGRAM,
                    "CHA*SERVOS");
    CHECK_EQ(l.rows, 3u);
    prog_list_build(&l, g_rows, n, PROG_DEV_SERVO, UI_WB_PROGRAM, "nothing");
    CHECK_EQ(l.rows, 0u);
    CHECK_EQ(l.count, 0u);
    CHECK(l.search);
    prog_list_build(&l, g_rows, n, PROG_DEV_SERVO, UI_WB_PROGRAM, "*");
    CHECK_EQ(l.rows, 12u);
    CHECK(l.headings);
    prog_list_build(&l, g_rows, n, PROG_DEV_SERVO, UI_WB_PROGRAM, "");
    CHECK_EQ(l.rows, 12u);
    /* NULL in, nothing out. */
    prog_list_build(NULL, g_rows, n, PROG_DEV_SERVO, UI_WB_PROGRAM, NULL);
    prog_list_build(&l, NULL, n, PROG_DEV_SERVO, UI_WB_PROGRAM, NULL);
    CHECK_EQ(l.count, 0u);
    CHECK_EQ(l.total, 0u);
}

/* --------------------------------------------------------- the view */

/* The SERVO screen's page area. */
static const gfx_rect_t AREA = { 6, 38, 488, 388 };

/* Nine rows fit with no bar; the tenth brings SEARCH and pages. */
TEST_CASE(the_view_fits_nine_rows_and_pages_past_them)
{
    canvas();
    prog_view_t v;
    prog_view_init(&v, AREA);
    prog_list_t l;

    size_t n = fake_list(9, 0);
    prog_list_build(&l, g_rows, n, PROG_DEV_SERVO, UI_WB_PROGRAM, NULL);
    /* Nine rows under a heading: 9 x 40 + 28 overruns the 374 px, so the
     * list pages and the bar holds "^" and "v" but no SEARCH. */
    CHECK(prog_view_has_bar(&v, &l));
    CHECK_EQ(prog_view_search_rect(&v, &l).w, 0);
    CHECK(prog_view_up_rect(&v, &l).w > 0);

    n = fake_list(8, 0);
    prog_list_build(&l, g_rows, n, PROG_DEV_SERVO, UI_WB_PROGRAM, NULL);
    CHECK(!prog_view_has_bar(&v, &l));
    CHECK_EQ(prog_view_shown(&v, &l), 8);
    CHECK_EQ(prog_view_up_rect(&v, &l).w, 0);
    const gfx_rect_t r0 = prog_view_entry_rect(&v, &l, 0);
    CHECK_EQ(r0.x, 16);
    CHECK_EQ(r0.y, 48);
    CHECK_EQ(r0.w, 468);
    CHECK_EQ(r0.h, PROG_VIEW_ROW_H);
    CHECK_EQ(prog_view_entry_rect(&v, &l, 1).y, 48 + PROG_VIEW_ROW_P);
    CHECK_EQ(prog_view_entry_rect(&v, &l, 8).w, 0);

    n = fake_list(10, 0);
    prog_list_build(&l, g_rows, n, PROG_DEV_SERVO, UI_WB_PROGRAM, NULL);
    CHECK(prog_view_has_bar(&v, &l));
    const gfx_rect_t sr = prog_view_search_rect(&v, &l);
    CHECK(sr.w > 0);
    CHECK_EQ(sr.y, 48);
    /* Under the bar: a heading and as many rows as fit. */
    CHECK_EQ(prog_view_entry_rect(&v, &l, 0).y,
             48 + PROG_VIEW_BAR_H + 8);
    const int shown = prog_view_shown(&v, &l);
    CHECK(shown >= 8);
    CHECK(shown < l.count);
    const gfx_rect_t last = prog_view_entry_rect(&v, &l, shown - 1);
    CHECK(last.y + last.h <= AREA.y + AREA.h - PROG_VIEW_PAD);

    /* "v" pages on, to the last entry; "^" back to the top. */
    const gfx_rect_t dn = prog_view_down_rect(&v, &l);
    const gfx_rect_t up = prog_view_up_rect(&v, &l);
    touch_event_t e = evt(TOUCH_EVENT_DOWN, mid_x(dn), mid_y(dn), 1);
    CHECK_EQ(prog_view_event(&v, &l, &e, NULL), PROG_VIEW_TAKEN);
    e.type = TOUCH_EVENT_UP;
    CHECK_EQ(prog_view_event(&v, &l, &e, NULL), PROG_VIEW_TAKEN);
    CHECK_EQ(v.top, shown);
    CHECK_EQ(v.top + prog_view_shown(&v, &l), (int)l.count);
    /* At the end "v" stays. */
    e = evt(TOUCH_EVENT_DOWN, mid_x(dn), mid_y(dn), 1);
    (void)prog_view_event(&v, &l, &e, NULL);
    e.type = TOUCH_EVENT_UP;
    (void)prog_view_event(&v, &l, &e, NULL);
    CHECK_EQ(v.top, shown);
    e = evt(TOUCH_EVENT_DOWN, mid_x(up), mid_y(up), 1);
    (void)prog_view_event(&v, &l, &e, NULL);
    e.type = TOUCH_EVENT_UP;
    (void)prog_view_event(&v, &l, &e, NULL);
    CHECK_EQ(v.top, 0);
    /* At the top "^" stays. */
    e = evt(TOUCH_EVENT_DOWN, mid_x(up), mid_y(up), 1);
    (void)prog_view_event(&v, &l, &e, NULL);
    e.type = TOUCH_EVENT_UP;
    (void)prog_view_event(&v, &l, &e, NULL);
    CHECK_EQ(v.top, 0);
    /* The bar draws. */
    prog_view_render(&v, &l, g_rows, n, LINK_CAP_SERVO_PWM, "", &cv);
    prog_view_render(&v, &l, g_rows, n, LINK_CAP_SERVO_PWM, "KST", &cv);
    prog_view_top(&v);
    CHECK_EQ(v.top, 0);
}

/* A row's tap names the table's row; a heading, a slide off, a release
 * marked no-tap and a second contact name none; SEARCH says so. */
TEST_CASE(the_view_reports_a_row_tap_and_nothing_else)
{
    canvas();
    prog_view_t v;
    prog_view_init(&v, AREA);
    prog_list_t l;
    size_t n = fake_list(3, 0);
    prog_list_build(&l, g_rows, n, PROG_DEV_SERVO, UI_WB_PROGRAM, NULL);
    const gfx_rect_t r1 = prog_view_entry_rect(&v, &l, 1);
    uint8_t row = 0xFFu;

    touch_event_t e = evt(TOUCH_EVENT_DOWN, mid_x(r1), mid_y(r1), 1);
    CHECK_EQ(prog_view_event(&v, &l, &e, &row), PROG_VIEW_TAKEN);
    CHECK_EQ(v.pressed, 1);
    /* A second contact is not the view's press. */
    touch_event_t e2 = evt(TOUCH_EVENT_DOWN, 30, 300, 2);
    CHECK_EQ(prog_view_event(&v, &l, &e2, &row), PROG_VIEW_TAKEN);
    CHECK_EQ(v.pressed, 1);
    e2.type = TOUCH_EVENT_UP;
    CHECK_EQ(prog_view_event(&v, &l, &e2, &row), PROG_VIEW_NONE);
    e.type = TOUCH_EVENT_MOVE;
    CHECK_EQ(prog_view_event(&v, &l, &e, &row), PROG_VIEW_TAKEN);
    e.type = TOUCH_EVENT_UP;
    CHECK_EQ(prog_view_event(&v, &l, &e, &row), PROG_VIEW_ROW);
    CHECK_EQ(row, 3u);
    CHECK_EQ(v.pressed, PROG_VIEW_P_NONE);

    /* Slid off before the release. */
    e = evt(TOUCH_EVENT_DOWN, mid_x(r1), mid_y(r1), 1);
    (void)prog_view_event(&v, &l, &e, &row);
    e = evt(TOUCH_EVENT_UP, mid_x(r1), r1.y + r1.h + 3, 1);
    CHECK_EQ(prog_view_event(&v, &l, &e, &row), PROG_VIEW_TAKEN);
    /* A release marked no-tap. */
    e = evt(TOUCH_EVENT_DOWN, mid_x(r1), mid_y(r1), 1);
    (void)prog_view_event(&v, &l, &e, &row);
    e.type = TOUCH_EVENT_UP;
    e.flags = TOUCH_FLAG_NO_TAP;
    CHECK_EQ(prog_view_event(&v, &l, &e, &row), PROG_VIEW_TAKEN);
    /* Cancelled. */
    e = evt(TOUCH_EVENT_DOWN, mid_x(r1), mid_y(r1), 1);
    (void)prog_view_event(&v, &l, &e, &row);
    prog_view_cancel(&v);
    e.type = TOUCH_EVENT_UP;
    CHECK_EQ(prog_view_event(&v, &l, &e, &row), PROG_VIEW_NONE);
    /* Outside the area; and inside it under the rows. */
    e = evt(TOUCH_EVENT_DOWN, 700, 100, 1);
    CHECK_EQ(prog_view_event(&v, &l, &e, &row), PROG_VIEW_NONE);
    e = evt(TOUCH_EVENT_DOWN, 100, 400, 1);
    CHECK_EQ(prog_view_event(&v, &l, &e, &row), PROG_VIEW_TAKEN);
    CHECK_EQ(v.pressed, PROG_VIEW_P_NONE);

    /* A heading takes a press and opens nothing. */
    n = fake_list(9, 0);
    prog_list_build(&l, g_rows, n, PROG_DEV_SERVO, UI_WB_PROGRAM, NULL);
    const gfx_rect_t h0 = prog_view_entry_rect(&v, &l, 0);
    CHECK(l.e[0].heading);
    e = evt(TOUCH_EVENT_DOWN, mid_x(h0), mid_y(h0), 1);
    (void)prog_view_event(&v, &l, &e, &row);
    e.type = TOUCH_EVENT_UP;
    CHECK_EQ(prog_view_event(&v, &l, &e, &row), PROG_VIEW_TAKEN);

    /* SEARCH. */
    n = fake_list(10, 0);
    prog_list_build(&l, g_rows, n, PROG_DEV_SERVO, UI_WB_PROGRAM, NULL);
    const gfx_rect_t sr = prog_view_search_rect(&v, &l);
    e = evt(TOUCH_EVENT_DOWN, mid_x(sr), mid_y(sr), 1);
    (void)prog_view_event(&v, &l, &e, &row);
    prog_view_render(&v, &l, g_rows, n, LINK_CAP_SERVO_PWM, NULL, &cv);
    e.type = TOUCH_EVENT_UP;
    CHECK_EQ(prog_view_event(&v, &l, &e, &row), PROG_VIEW_SEARCH);

    /* NULL in. */
    CHECK_EQ(prog_view_event(NULL, &l, &e, &row), PROG_VIEW_NONE);
    CHECK_EQ(prog_view_event(&v, NULL, &e, &row), PROG_VIEW_NONE);
    CHECK_EQ(prog_view_event(&v, &l, NULL, &row), PROG_VIEW_NONE);
    CHECK(!prog_view_has_bar(NULL, &l));
    CHECK_EQ(prog_view_shown(NULL, &l), 0);
    CHECK_EQ(prog_view_entry_rect(&v, NULL, 0).w, 0);
    CHECK_EQ(prog_view_search_rect(NULL, &l).w, 0);
    CHECK_EQ(prog_view_up_rect(&v, NULL).w, 0);
    CHECK_EQ(prog_view_down_rect(NULL, &l).w, 0);
    prog_view_init(NULL, AREA);
    prog_view_top(NULL);
    prog_view_cancel(NULL);
    prog_view_render(NULL, &l, g_rows, n, 0u, NULL, &cv);
    prog_view_render(&v, &l, g_rows, n, 0u, NULL, NULL);
}

/* A live row is drawn with its ">", a dimmed one without and with its
 * reason in the warning colour. */
TEST_CASE(a_dimmed_row_draws_its_reason_in_the_warning_colour)
{
    fakes_fresh();
    canvas();
    prog_view_t v;
    prog_view_init(&v, AREA);
    prog_reg_row_t r[1] = { row_of(TX_PL_KST, TX_PL_GRP_PROG_PORT, 0u,
                                   &k_set_a) };
    prog_list_t l;
    prog_list_build(&l, r, 1, PROG_DEV_SERVO, UI_WB_PROGRAM, NULL);
    const gfx_rect_t row = prog_view_entry_rect(&v, &l, 0);
    const gfx_color_t warn = ui_theme_color(UI_C_WARN);
    const gfx_color_t accent = ui_theme_color(UI_C_ACCENT);

    int warned = 0, chevron = 0;
    prog_view_render(&v, &l, r, 1, LINK_CAP_SERVO_PWM, NULL, &cv);
    for (int y = row.y; y < row.y + row.h; ++y) {
        for (int x = row.x; x < row.x + row.w; ++x) {
            warned += fb[y * W + x] == warn;
            chevron += x >= row.x + row.w - 24 && fb[y * W + x] == accent;
        }
    }
    CHECK_EQ(warned, 0);
    CHECK(chevron > 0);

    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    warned = 0;
    chevron = 0;
    prog_view_render(&v, &l, r, 1, 0u, NULL, &cv);
    for (int y = row.y; y < row.y + row.h; ++y) {
        for (int x = row.x; x < row.x + row.w; ++x) {
            warned += fb[y * W + x] == warn;
            chevron += x >= row.x + row.w - 24 && fb[y * W + x] == accent;
        }
    }
    CHECK(warned > 0);
    CHECK_EQ(chevron, 0);
}

int main(void)
{
    RUN(the_shipped_table_is_sound_and_lists_the_kst_programmer);
    RUN(the_check_names_each_fault_and_its_row);
    RUN(a_list_over_its_maximum_fails_the_check);
    RUN(a_devices_sessions_are_each_listed_once);
    RUN(a_row_the_iomcu_lacks_is_dimmed_with_its_reason);
    RUN(two_rows_on_one_port_are_exclusive);
    RUN(a_row_on_the_testers_port_follows_the_tester);
    RUN(search_comes_over_nine_rows_and_headings_over_eight);
    RUN(headings_group_the_rows_in_the_tables_order);
    RUN(the_search_narrows_the_list_and_keeps_its_box);
    RUN(the_view_fits_nine_rows_and_pages_past_them);
    RUN(the_view_reports_a_row_tap_and_nothing_else);
    RUN(a_dimmed_row_draws_its_reason_in_the_warning_colour);
    free(fb);
    return test_summary("prog_registry");
}
