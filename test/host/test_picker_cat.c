/**
 * @file test_picker_cat.c
 * @brief The cat on OUTPUTS' board view: it covers nothing the screen draws or takes a
 *        press on, in every state the screen shows; a tap on it does
 *        nothing; the pick is random among the poses that fit and made once
 *        a visit; a row that grows into it hides it; a frame that repaints
 *        nothing draws nothing for it.
 *
 * Every state of the screen's screenshots (tools/render_ui.py: picker,
 * picker-drawn, picker-prog, picker-port, picker-full, picker-protocol,
 * picker-off, picker-iface) and a few more (no pin bound, 8 pins, the list
 * open beside 4 ports, a binding no read confirmed, a board that cannot be
 * drawn, a learned board with no end margin), each in English and German,
 * dark and light, with every pose forced and as many rolls as spots it
 * may take.  Each cat drawn is
 * held three ways: its box grown by UI_CAT_GAP is background in the same
 * frame drawn without it, the two frames differ nowhere outside its box,
 * and its box meets no key, mark, list face, list row, IN USE line or the
 * head, and ends above the alert band.
 *
 * Cases:
 * - the_cat_covers_nothing_in_any_state
 * - a_tap_on_the_cat_does_nothing
 * - the_pick_is_random_among_the_poses_that_fit
 * - the_pick_is_made_once_a_visit
 * - a_row_grown_into_the_cat_hides_it
 * - a_frame_that_repaints_nothing_draws_no_cat
 * - the_pick_refuses_what_it_cannot_use
 *
 * SPDX-License-Identifier: MIT
 */

#include "picker_screen_rig.h"

#include "gfx_work.h"
#include "outputs_screen.h"
#include "../../shared/ui/outputs_screen_int.h"
#include "port_view.h"
#include "ui_cats.h"

/** @brief The index of the pose named @p name in ui_cat_poses[]. */
static int pose_named(const char *name)
{
    for (int p = 0; p < UI_CAT_POSES; ++p) {
        if (strcmp(ui_cat_poses[p].name, name) == 0) { return p; }
    }
    T_FAIL("no pose %s", name);
    return 0;
}

/** @brief The same frame drawn with the cats off. */
static gfx_color_t s_bare[SCREEN_W * SCREEN_H];

/** @brief Renders into @p px, the chrome redrawn whatever it holds. */
static void render_into(gfx_color_t *px)
{
    gfx_canvas_t c = { px, SCREEN_W, SCREEN_H, SCREEN_W,
                       { 0, 0, SCREEN_W, SCREEN_H } };
    picker_screen_invalidate();
    scr()->render(&c, 0);
}

/** @brief Binds @p n SERVO PWM pins from GP0 on, as the PINS table fills. */
static void servo_pins(unsigned n)
{
    static const uint8_t gp[8] = { 0, 1, 2, 4, 5, 6, 7, 13 };
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, BOARD);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    for (unsigned k = 0; k < n && k < 8u; ++k) {
        CHECK(outbind_toggle(&b, idx(gp[k])));
    }
    picker_screen_set_binding(&b);
    picker_screen_follow(b.proto, true);
}

/* ---------------------------------------------------------- the states */

/** @brief The screenshot's bench: SERVO PWM on GP0 to GP2, DSHOT300 on
 *  GP6, with the IOmcu's drawing. */
static void st_bench(void)
{
    static const uint8_t servo[3] = { 0, 1, 2 };
    static const uint8_t esc[1] = { 6 };
    CHECK(serve_drawing());
    bind_more("SERVO PWM", servo, 3u, "SERVO PWM");
    bind_more("DSHOT300", esc, 1u, "SERVO PWM");
    picker_screen_follow(proto_named("SERVO PWM"), true);
}

/** @brief No pin bound, the board from its shape. */
static void st_none(void) { }

/** @brief The bench with the board from its shape. */
static void st_shape(void)
{
    st_bench();
    (void)picker_screen_set_drawing(NULL, 0u, NULL);
}

/** @brief The bench, GP13 the programming port's and GP28 tapped. */
static void st_prog(void)
{
    st_bench();
    picker_screen_set_prog_pin(13u);
    gfx_rect_t k;
    if (picker_screen_button_rect(idx(28u), &k)) { tap_rect(k); }
}

/** @brief Eight of eight slots. */
static void st_full(void)
{
    static const uint8_t servo[5] = { 0, 1, 2, 4, 5 };
    static const uint8_t esc[3] = { 6, 7, 14 };
    CHECK(serve_drawing());
    bind_more("SERVO PWM", servo, 5u, "SERVO PWM");
    bind_more("DSHOT300", esc, 3u, "SERVO PWM");
    picker_screen_follow(proto_named("SERVO PWM"), true);
}

/** @brief The bench with the protocol list open. */
static void st_open(void)
{
    st_bench();
    tap_rect(picker_screen_list_face());
    CHECK(picker_screen_list_open());
}

/** @brief The list open beside 4 PROG PORT entries. */
static void st_open_ports(void)
{
    static const uint8_t four[LINK_PG_PORTS_MAX] = { 13u, 26u, 27u, 28u };
    picker_screen_set_prog_ports(4u, four);
    outputs_screen_set_prog_ports(4u, four);
    tap_rect(picker_screen_list_face());
    CHECK(picker_screen_list_open());
}

/** @brief OFF picked, GP13 the port's, 8 pins over 6 protocols: the
 *  tallest IN USE table. */
static void st_off(void)
{
    static const struct { const char *proto; uint8_t gp; } k[8] = {
        { "SERVO PWM", 0 },      { "SERVO PWM", 4 },
        { "MOTOR PWM", 5 },      { "MOTOR PWM", 7 },
        { "DSHOT300", 6 },       { "DSHOT600", 14 },
        { "DSHOT300 BIDIR", 1 }, { "DSHOT600 BIDIR", 2 },
    };
    CHECK(serve_drawing());
    for (unsigned i = 0; i < 8u; ++i) {
        bind_more(k[i].proto, &k[i].gp, 1u, "OFF");
    }
    picker_screen_set_prog_pin(13u);
    picker_screen_follow(0u, true);
}

/** @brief The bench with every interface of SETUP > INTERFACES on. */
static void st_iface(void)
{
    st_bench();
    picker_screen_set_prog_pin(13u);
    settings_set(SET_INA228_EN, 1.0f);
    settings_set(SET_SENSE_SDA, 16.0f);
    settings_set(SET_PDMINI_EN, 1.0f);
    settings_set(SET_PDMINI_TX, 19.0f);
    settings_set(SET_PDMINI_RX, 18.0f);
    settings_set(SET_TONE_EN, 1.0f);
}

/** @brief PROG PORT 3 picked on a 4.26 IOmcu's four ports. */
static void st_port(void)
{
    st_bench();
    port_view_t v;
    port_view_unknown(&v);
    v.count    = 4u;
    v.has_func = true;
    v.pin[0]   = 13u;
    v.pin[1]   = 14u;
    v.func[0]  = v.func[2] = (uint8_t)LINK_PG_FN_SERVO_PWM;
    v.func[1]  = v.func[3] = (uint8_t)LINK_PG_FN_NONE;
    port_view_set(&v);
    static const uint8_t held[LINK_PG_PORTS_MAX] = { 13u, 14u, 0xFFu, 0xFFu };
    outputs_screen_set_prog_ports(4u, held);
    picker_screen_set_prog_ports(4u, held);
    outputs_screen_set_board_view(true);
    outputs_screen_select_port(2u);
    picker_screen_follow(0u, true);
}

/** @brief The bench as last read, not confirmed: no key acts. */
static void st_unread(void)
{
    st_bench();
    picker_screen_follow(proto_named("SERVO PWM"), false);
}

/** @brief SERVO PWM on 8 pins: the PINS table down to the foot. */
static void st_eight(void) { servo_pins(8u); }

/** @brief A board nothing knows: OUTPUTS draws its grid view, with no cat. */
static void st_cannot(void)
{
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, 4321u);
    picker_screen_set_binding(&b);
    CHECK(!picker_screen_can_draw());
}

/** @brief A board that describes itself, as an IOmcu of another make
 *  would. */
#define LEARNED ((uint16_t)4242)

/** @brief A learned board with no margin past its end pads, keys from
 *  x 187 to 837, its four corner pins bound. */
static void st_learned(void)
{
    uint16_t cat[LINK_CAT_COUNT];
    for (unsigned i = 0; i < LINK_CAT_COUNT; ++i) { cat[i] = 0u; }
    cat[0] = LINK_CAT_OF(0, 1, LINK_PIN_FREE);
    cat[1] = LINK_CAT_OF(1, 20, LINK_PIN_FREE);
    cat[2] = LINK_CAT_OF(2, 21, LINK_PIN_FREE);
    cat[3] = LINK_CAT_OF(3, 40, LINK_PIN_FREE);
    CHECK(outbind_learn_board(LEARNED, cat));
    uint16_t sh[LINK_SH_COUNT];
    sh[LINK_SH_WIDTH_CMM]  = 19u * 254u;
    sh[LINK_SH_HEIGHT_CMM] = 1900u;
    sh[LINK_SH_LAYOUT]     = LINK_SH_LAYOUT_OF(LINK_SH_BOTTOM_LEFT, 20);
    sh[LINK_SH_PITCH_CMM]  = 254u;
    sh[LINK_SH_INSET_CMM]  = 100u;
    CHECK(outbind_learn_shape(LEARNED, sh));
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, LEARNED);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    for (uint8_t i = 0; i < 4u; ++i) { CHECK(outbind_toggle(&b, i)); }
    picker_screen_set_binding(&b);
    picker_screen_follow(b.proto, true);
}

static const struct {
    const char *name;
    void (*pose)(void);
} k_states[] = {
    { "none", st_none },         { "bench", st_bench },
    { "shape", st_shape },       { "prog", st_prog },
    { "full", st_full },         { "open", st_open },
    { "open-ports", st_open_ports }, { "off", st_off },
    { "iface", st_iface },       { "port", st_port },
    { "unread", st_unread },     { "eight", st_eight },
    { "cannot", st_cannot },     { "learned", st_learned },
};
#define STATES ((int)(sizeof(k_states) / sizeof(k_states[0])))

/** @brief Puts back what a state set beyond the picker's own state. */
static void undo_state(void)
{
    outputs_screen()->reset();
    picker_screen_set_prog_ports(0u, NULL);
    outputs_screen_set_prog_ports(OUTPUTS_PORTS_UNKNOWN, NULL);
    port_view_t v;
    port_view_unknown(&v);
    port_view_set(&v);
    settings_set_store(NULL);
    settings_init();
    outbind_forget_learned();
}

/** @brief Poses state @p st with @p pose forced and the roll from
 *  @p seed. */
static void pose_state(int st, int pose, uint32_t seed)
{
    undo_state();
    ui_cats_force(pose);
    ui_cats_seed(seed);
    fresh();
    k_states[st].pose();
}

/** @brief Whether @p r grown by @p g meets @p o. */
static bool meets(gfx_rect_t r, int g, gfx_rect_t o)
{
    return overlap(gfx_rect_make(r.x - g, r.y - g, r.w + 2 * g, r.h + 2 * g),
                   o);
}

/** @brief Every box the screen hit-tests or prints in that @p r grown by
 *  UI_CAT_GAP meets; 0 when none. */
static unsigned meets_any(gfx_rect_t r)
{
    unsigned n = 0u;
    const uint16_t board = picker_screen_binding()->board;
    for (uint8_t i = 0; i < outbind_pin_count(board); ++i) {
        gfx_rect_t k;
        if (picker_screen_button_rect(i, &k) && meets(r, UI_CAT_GAP, k)) {
            ++n;
        }
    }
    for (uint8_t e = 0; e < outbind_pad_count(board); ++e) {
        gfx_rect_t m;
        if (picker_screen_marker_rect(e, &m) && meets(r, UI_CAT_GAP, m)) {
            ++n;
        }
    }
    if (picker_screen_port_shown() >= 0) {
        /* The entry's rows, down the column. */
        n += meets(r, UI_CAT_GAP, gfx_rect_make(OB_COL_X, OB_FN_Y, OB_LIST_W,
                                                OU_FOOT - OB_FN_Y)) ? 1u : 0u;
    } else {
        n += meets(r, UI_CAT_GAP, picker_screen_last_rect()) ? 1u : 0u;
    }
    if (picker_screen_can_draw()) {
        n += meets(r, UI_CAT_GAP, picker_screen_list_face()) ? 1u : 0u;
        n += meets(r, UI_CAT_GAP, picker_screen_area(PICKER_AREA_HEAD))
                 ? 1u : 0u;
    }
    if (picker_screen_list_open()) {
        for (uint8_t i = 0; i < (uint8_t)(OUTBIND_PROTOS + 4u); ++i) {
            const gfx_rect_t row = picker_screen_list_row(i);
            n += (!gfx_rect_empty(row) && meets(r, UI_CAT_GAP, row)) ? 1u
                                                                       : 0u;
        }
    }
    picker_use_t u;
    for (uint8_t k = 0; picker_screen_in_use(k, &u); ++k) {
        n += meets(r, UI_CAT_GAP, u.rect) ? 1u : 0u;
    }
    return n;
}

/** @brief The pixels of the bare frame inside @p r grown by UI_CAT_GAP
 *  that are not the background. */
static unsigned drawn_under(gfx_rect_t r)
{
    unsigned n = 0u;
    for (int y = r.y - UI_CAT_GAP; y < r.y + r.h + UI_CAT_GAP; ++y) {
        for (int x = r.x - UI_CAT_GAP; x < r.x + r.w + UI_CAT_GAP; ++x) {
            if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) {
                continue;
            }
            n += (s_bare[(size_t)y * SCREEN_W + (size_t)x] != UI_BG) ? 1u
                                                                     : 0u;
        }
    }
    return n;
}

/** @brief The pixels outside @p r where the two frames differ. */
static unsigned differs_outside(gfx_rect_t r)
{
    unsigned n = 0u;
    for (int y = 0; y < SCREEN_H; ++y) {
        for (int x = 0; x < SCREEN_W; ++x) {
            const size_t i = (size_t)y * SCREEN_W + (size_t)x;
            n += (!gfx_rect_contains(r, x, y) && s_px[i] != s_bare[i]) ? 1u
                                                                       : 0u;
        }
    }
    return n;
}

/* ----------------------------------------------------------- the cases */

/*
 * Every state, language, theme, pose and roll: a cat drawn covers only
 * background, changes nothing outside its box and meets no box the screen
 * hit-tests or prints in.  Every pose and both spots are drawn somewhere,
 * so the case is not passed by drawing none.
 */
TEST_CASE(the_cat_covers_nothing_in_any_state)
{
    unsigned shown[UI_CAT_POSES] = { 0 };
    unsigned left = 0u, right = 0u;
    for (int l = 0; l < (int)UI_LANG_COUNT; ++l) {
        ui_text_set_language((ui_lang_t)l);
        for (int th = 0; th < 2; ++th) {
            for (int st = 0; st < STATES; ++st) {
                for (int p = 0; p < UI_CAT_POSES; ++p) {
                    /* As many rolls as spots the pose may take; every
                     * pose today takes one. */
                    const uint32_t m = ui_cat_poses[p].spots;
                    const uint32_t rolls =
                        (uint32_t)__builtin_popcount(m);
                    for (uint32_t seed = 1u; seed <= rolls; ++seed) {
                        pose_state(st, p, seed);
                        ui_theme_set(th ? UI_THEME_LIGHT : UI_THEME_DARK);
                        render_into(s_px);
                        gfx_rect_t r;
                        int pose;
                        if (!picker_screen_cat(&r, &pose)) {
                            continue;
                        }
                        CHECK_EQ(pose, p);
                        ++shown[p];
                        left  += (r.x < SCREEN_W / 2) ? 1u : 0u;
                        right += (r.x >= SCREEN_W / 2) ? 1u : 0u;
                        ui_cats_force(UI_CAT_NONE);
                        render_into(s_bare);
                        CHECK(!picker_screen_cat(NULL, NULL));
                        const bool ok =
                            r.x >= 0 && r.y >= 0
                            && r.x + r.w <= SCREEN_W && r.y + r.h <= PK_FOOT
                            && drawn_under(r) == 0u
                            && differs_outside(r) == 0u
                            && meets_any(r) == 0u;
                        if (!ok) {
                            T_FAIL("%s, lang %d, theme %d, pose %d: the "
                                   "cat at %d,%d %dx%d covers something",
                                   k_states[st].name, l, th, p, r.x, r.y,
                                   r.w, r.h);
                        }
                    }
                }
            }
        }
    }
    undo_state();
    ui_cats_force(UI_CAT_RANDOM);
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    for (int p = 0; p < UI_CAT_POSES; ++p) {
        if (shown[p] == 0u) { T_FAIL("pose %d is never drawn", p); }
    }
    CHECK(left > 0u);
    CHECK(right > 0u);
}

/*
 * A tap anywhere on the cat, every pose: nothing applied, the binding,
 * the list and the port as they were, and the frame drawn again the same
 * pixel for pixel.
 */
TEST_CASE(a_tap_on_the_cat_does_nothing)
{
    for (int p = 0; p < UI_CAT_POSES; ++p) {
        pose_state(1, p, 1u);           /* the bench */
        render_into(s_px);
        gfx_rect_t r;
        CHECK(picker_screen_cat(&r, NULL));
        memcpy(s_bare, s_px, sizeof(s_bare));
        const outbind_t was = *picker_screen_binding();
        const int port = picker_screen_port_shown();
        s_applied = 0;
        for (int y = r.y; y < r.y + r.h; y += 9) {
            for (int x = r.x; x < r.x + r.w; x += 9) {
                tap(x, y);
            }
        }
        tap(r.x + r.w - 1, r.y + r.h - 1);
        CHECK_EQ(s_applied, 0);
        CHECK(memcmp(&was, picker_screen_binding(), sizeof(was)) == 0);
        CHECK(!picker_screen_list_open());
        CHECK_EQ(picker_screen_port_shown(), port);
        render_into(s_px);
        CHECK(memcmp(s_px, s_bare, sizeof(s_px)) == 0);
    }
    undo_state();
    ui_cats_force(UI_CAT_RANDOM);
}

/** @brief The roll a test source hands out next. */
static uint32_t s_next_roll;
/** @brief How many rolls the test source has handed out. */
static unsigned s_rolls;

/** @brief A random source that returns s_next_roll and counts. */
static uint32_t test_roll(void)
{
    ++s_rolls;
    return s_next_roll;
}

/** @brief The pose drawn in state @p st with roll @p roll, or
 *  UI_CAT_NONE; its box's left edge in @p x. */
static int pose_for(int st, uint32_t roll, int *x)
{
    undo_state();
    ui_cats_force(UI_CAT_RANDOM);
    ui_cats_set_random(test_roll);
    s_next_roll = roll;
    fresh();
    k_states[st].pose();
    render_into(s_px);
    gfx_rect_t r;
    int pose = UI_CAT_NONE;
    (void)picker_screen_cat(&r, &pose);
    *x = r.x;
    ui_cats_set_random(NULL);
    return pose;
}

/*
 * With every pose fitting (the bench), roll k picks pose k mod the
 * number of poses, on the spot it takes.  With the PINS table down to the
 * foot (8 pins) only the poses for the right are picked; with IN USE at
 * its tallest (OFF) only the ones for the left.
 */
TEST_CASE(the_pick_is_random_among_the_poses_that_fit)
{
    int x;
    for (uint32_t k = 0; k < 2u * (uint32_t)UI_CAT_POSES; ++k) {
        const int p = pose_for(1, k, &x);
        CHECK_EQ(p, (int)(k % (uint32_t)UI_CAT_POSES));
        const bool right = (ui_cat_poses[p].spots
                            & UI_CAT_IN(UI_CAT_SPOT_PICKER_RIGHT)) != 0u;
        CHECK(right == (x >= SCREEN_W / 2));
    }

    unsigned seen_right = 0u, seen_left = 0u;
    for (uint32_t k = 0; k < 4u * (uint32_t)UI_CAT_POSES; ++k) {
        const int p8 = pose_for(11, k, &x);            /* eight */
        CHECK(p8 != UI_CAT_NONE);
        CHECK((ui_cat_poses[p8].spots
               & UI_CAT_IN(UI_CAT_SPOT_PICKER_RIGHT)) != 0u);
        CHECK(x >= SCREEN_W / 2);
        seen_right |= 1u << (unsigned)p8;
        const int po = pose_for(7, k, &x);             /* off */
        CHECK(po != UI_CAT_NONE);
        CHECK((ui_cat_poses[po].spots
               & UI_CAT_IN(UI_CAT_SPOT_PICKER_LEFT)) != 0u);
        CHECK(x < SCREEN_W / 2);
        seen_left |= 1u << (unsigned)po;
    }
    /* Every pose that may take the spot is picked for some roll. */
    for (int p = 0; p < UI_CAT_POSES; ++p) {
        const uint32_t m = ui_cat_poses[p].spots;
        if ((m & UI_CAT_IN(UI_CAT_SPOT_PICKER_RIGHT)) != 0u) {
            CHECK((seen_right & (1u << (unsigned)p)) != 0u);
        }
        if ((m & UI_CAT_IN(UI_CAT_SPOT_PICKER_LEFT)) != 0u) {
            CHECK((seen_left & (1u << (unsigned)p)) != 0u);
        }
    }
    undo_state();
}

/*
 * One roll a visit, taken on entering: frames and changes after it keep the
 * pose; leaving and entering again rolls again.
 */
TEST_CASE(the_pick_is_made_once_a_visit)
{
    undo_state();
    ui_cats_force(UI_CAT_RANDOM);
    ui_cats_set_random(test_roll);
    s_rolls = 0u;
    const int sleeping = pose_named("sleep_grey");
    s_next_roll = (uint32_t)sleeping;    /* every pose fits the bench */
    fresh();
    st_bench();
    CHECK_EQ(s_rolls, 1u);
    render_into(s_px);
    int pose;
    CHECK(picker_screen_cat(NULL, &pose));
    CHECK_EQ(pose, sleeping);
    s_next_roll = 0u;
    for (int i = 0; i < 3; ++i) {
        tap_rect(picker_screen_list_face());   /* open, then closed */
        render_into(s_px);
    }
    CHECK(scr()->back());
    render_into(s_px);
    CHECK(picker_screen_cat(NULL, &pose));
    CHECK_EQ(pose, sleeping);
    CHECK_EQ(s_rolls, 1u);
    scr()->leave();
    scr()->enter();
    picker_screen_follow(proto_named("SERVO PWM"), true);
    CHECK_EQ(s_rolls, 2u);
    render_into(s_px);
    CHECK(picker_screen_cat(NULL, &pose));
    CHECK_EQ(pose, 0);
    /* Reset forgets the pick: no cat until the next visit. */
    scr()->reset();
    picker_screen_set_binding(picker_screen_binding());
    render_into(s_px);
    CHECK(!picker_screen_cat(NULL, &pose));
    CHECK_EQ(pose, UI_CAT_NONE);
    ui_cats_set_random(NULL);
    undo_state();
}

/*
 * The sleeping cat bottom left beside 3 pins: 5 more grow the PINS table
 * into it and it is not drawn; back to 3 and it is, the same pose.
 */
TEST_CASE(a_row_grown_into_the_cat_hides_it)
{
    undo_state();
    const int sleeping = pose_named("sleep_grey");
    ui_cats_force(sleeping);
    fresh();
    servo_pins(3u);
    render_into(s_px);
    gfx_rect_t r;
    CHECK(picker_screen_cat(&r, NULL));
    CHECK(r.x < SCREEN_W / 2);
    servo_pins(8u);
    render_into(s_px);
    CHECK(!picker_screen_cat(&r, NULL));
    CHECK(gfx_rect_empty(r));
    servo_pins(3u);
    render_into(s_px);
    int pose;
    CHECK(picker_screen_cat(&r, &pose));
    CHECK_EQ(pose, sleeping);
    ui_cats_force(UI_CAT_RANDOM);
}

/*
 * The cat is drawn with the chrome: the frame after one that drew it, with
 * nothing changed, visits no pixel; the frame that draws it visits its
 * covered pixels more than the same frame without it.
 */
TEST_CASE(a_frame_that_repaints_nothing_draws_no_cat)
{
    undo_state();
    ui_cats_force(0);
    fresh();
    st_bench();
    gfx_canvas_t c = { s_px, SCREEN_W, SCREEN_H, SCREEN_W,
                       { 0, 0, SCREEN_W, SCREEN_H } };
    picker_screen_invalidate();
    unsigned long w0 = gfx_work();
    scr()->render(&c, 0);
    const unsigned long with_cat = gfx_work() - w0;
    CHECK(picker_screen_cat(NULL, NULL));
    w0 = gfx_work();
    scr()->render(&c, 0);
    CHECK_EQ(gfx_work() - w0, 0ul);
    CHECK(picker_screen_cat(NULL, NULL));
    ui_cats_force(UI_CAT_NONE);
    picker_screen_invalidate();
    w0 = gfx_work();
    scr()->render(&c, 0);
    const unsigned long bare = gfx_work() - w0;
    CHECK(with_cat > bare);
    ui_cats_force(UI_CAT_RANDOM);
}

/** @brief A clear test that says every box is clear. */
static bool all_clear(gfx_rect_t r)
{
    (void)r;
    return true;
}

/** @brief A clear test that says no box is. */
static bool none_clear(gfx_rect_t r)
{
    (void)r;
    return false;
}

/*
 * ui_cats_pick() picks none for no spots, too many, no clear test,
 * nothing clear or UI_CAT_NONE forced; a forced pose out of range is
 * UI_CAT_NONE; a spot no pose takes is never picked; ui_cats_draw() and
 * ui_cat_rect() draw and place nothing for a pick out of range.
 */
TEST_CASE(the_pick_refuses_what_it_cannot_use)
{
    const ui_cat_spot_t spots[2] = {
        { UI_CAT_SPOT_PICKER_LEFT,  8,   492, UI_CAT_FROM_LEFT },
        { UI_CAT_SPOT_PICKER_RIGHT, 1022, 492, UI_CAT_FROM_RIGHT },
    };
    ui_cats_force(UI_CAT_RANDOM);
    CHECK_EQ(ui_cats_pick(NULL, 2, all_clear, 0u).pose, UI_CAT_NONE);
    CHECK_EQ(ui_cats_pick(spots, 0, all_clear, 0u).pose, UI_CAT_NONE);
    CHECK_EQ(ui_cats_pick(spots, 33, all_clear, 0u).pose, UI_CAT_NONE);
    CHECK_EQ(ui_cats_pick(spots, 2, NULL, 0u).pose, UI_CAT_NONE);
    CHECK_EQ(ui_cats_pick(spots, 2, none_clear, 0u).pose, UI_CAT_NONE);
    CHECK(ui_cats_pick(spots, 2, all_clear, 0u).pose >= 0);
    /* The largest roll: still a pose and a spot in range. */
    const ui_cat_pick_t big = ui_cats_pick(spots, 2, all_clear, 0xFFFFFFFFu);
    CHECK(big.pose >= 0 && big.pose < UI_CAT_POSES);
    CHECK(big.spot >= 0 && big.spot < 2);

    ui_cats_force(UI_CAT_NONE);
    CHECK_EQ(ui_cats_forced(), UI_CAT_NONE);
    CHECK_EQ(ui_cats_pick(spots, 2, all_clear, 0u).pose, UI_CAT_NONE);
    ui_cats_force(UI_CAT_POSES);
    CHECK_EQ(ui_cats_forced(), UI_CAT_NONE);
    ui_cats_force(-3);
    CHECK_EQ(ui_cats_forced(), UI_CAT_NONE);
    ui_cats_force(UI_CAT_POSES - 1);
    CHECK_EQ(ui_cats_forced(), UI_CAT_POSES - 1);
    CHECK_EQ(ui_cats_pick(spots, 2, all_clear, 12345u).pose,
             UI_CAT_POSES - 1);

    /* A spot whose id no pose takes, and one out of the mask's range. */
    ui_cats_force(UI_CAT_RANDOM);
    const ui_cat_spot_t odd[2] = {
        { (ui_cat_spot_id_t)UI_CAT_SPOTS, 8, 492, UI_CAT_FROM_LEFT },
        { (ui_cat_spot_id_t)40, 8, 492, UI_CAT_FROM_LEFT },
    };
    CHECK_EQ(ui_cats_pick(odd, 2, all_clear, 0u).pose, UI_CAT_NONE);

    /* Two clear spots for one pose: spot (roll / poses) mod 2. */
    const int sleeping = pose_named("sleep_grey");
    ui_cats_force(sleeping);
    const ui_cat_spot_t two[2] = {
        { UI_CAT_SPOT_PICKER_LEFT, 8,   492, UI_CAT_FROM_LEFT },
        { UI_CAT_SPOT_PICKER_LEFT, 300, 492, UI_CAT_FROM_LEFT },
    };
    CHECK_EQ(ui_cats_pick(two, 2, all_clear, 0u).spot, 0);
    CHECK_EQ(ui_cats_pick(two, 2, all_clear, 1u).spot, 1);
    CHECK_EQ(ui_cats_pick(two, 2, all_clear, 2u).spot, 0);
    CHECK_EQ(ui_cats_pick(two, 2, all_clear, 1u).pose, sleeping);
    ui_cats_force(UI_CAT_RANDOM);

    CHECK(gfx_rect_empty(ui_cat_rect(-1, &spots[0])));
    CHECK(gfx_rect_empty(ui_cat_rect(UI_CAT_POSES, &spots[0])));
    CHECK(gfx_rect_empty(ui_cat_rect(0, NULL)));
    const gfx_rect_t r0 = ui_cat_rect(0, &spots[1]);
    CHECK_EQ(r0.x + r0.w, 1022);
    CHECK_EQ(r0.y + r0.h, 492);
    gfx_canvas_t c = { s_px, SCREEN_W, SCREEN_H, SCREEN_W,
                       { 0, 0, SCREEN_W, SCREEN_H } };
    CHECK(gfx_rect_empty(ui_cats_draw(&c, UI_CAT_PICK_NONE, spots, 2)));
    CHECK(gfx_rect_empty(ui_cats_draw(&c, (ui_cat_pick_t){ 0, 2 }, spots,
                                      2)));
    CHECK(gfx_rect_empty(ui_cats_draw(&c, (ui_cat_pick_t){ 0, 0 }, NULL,
                                      2)));

    /* The built-in source: seeded alike, rolled alike; 0 seeds as 1. */
    ui_cats_seed(0u);
    const uint32_t a = ui_cats_roll();
    ui_cats_seed(1u);
    CHECK_EQ(ui_cats_roll(), a);
    CHECK(ui_cats_roll() != a);
}

int main(void)
{
    RUN(the_cat_covers_nothing_in_any_state);
    RUN(a_tap_on_the_cat_does_nothing);
    RUN(the_pick_is_random_among_the_poses_that_fit);
    RUN(the_pick_is_made_once_a_visit);
    RUN(a_row_grown_into_the_cat_hides_it);
    RUN(a_frame_that_repaints_nothing_draws_no_cat);
    RUN(the_pick_refuses_what_it_cannot_use);
    return test_summary("picker_cat");
}
