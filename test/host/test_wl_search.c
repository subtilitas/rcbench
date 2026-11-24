/*
 * SEARCH (wl_search.h), the scan and bind screen of SETUP > WIRELESS, for
 * each of its three devices: the Dial, the power node and the balance
 * node.  Everything goes through the glass (ui_router_event()) and the
 * render (ui_router_render()).
 *
 * The failure this is about: on the tester's panel SCAN FOR NODE read
 * `1 FOUND` and a tap on it, or on BOUND NODE, showed no list.  SETUP's
 * render drew its rows, its strip and SAVE but never the open choice list
 * (settings_draw.c's stg_render(); ui_setlist_render() alone drew it), so
 * the list opened in the model, took the next tap where nobody could see
 * it, and closed on any other.  The cases here hold the screen that
 * replaces the list to what the glass shows: a row that the model lists
 * and the question it opens are drawn, and a tap where they are drawn
 * acts on them.
 *
 * Cases:
 * - each_row_opens_search_and_the_home_tag_returns_to_the_entry
 * - the_state_line_says_what_the_scan_did
 * - the_found_devices_and_the_question_are_drawn
 * - bind_asks_the_bind_of_the_tapped_device
 * - cancel_and_the_home_tag_close_the_question_and_bind_nothing
 * - a_scan_while_armed_or_running_is_refused_on_the_band
 * - a_bind_behind_a_running_scan_is_refused_on_the_band
 * - a_list_that_changed_between_tap_and_bind_is_refused
 * - a_list_that_changed_under_the_finger_opens_no_question
 * - view_updates_keep_the_question_and_the_press
 * - unbind_fires_at_2000ms_and_not_at_1999ms
 * - unbind_with_nothing_bound_is_grey_and_refused
 * - an_unbind_hold_ends_when_the_device_goes_or_the_finger_slides_off
 *
 * The tester's finding behind UNBIND: a bound Dial, power node or balance
 * node could only be replaced by another bind or switched OFF, never
 * forgotten.
 *
 * SPDX-License-Identifier: MIT
 */
#include "wl_bind_rig.h"

#include "wireless_bal.h"

/** @brief The device a case runs on, on the suite's output. */
#define T_LOG(...) (printf("  " __VA_ARGS__), printf("\n"))

/** @brief A view of any of the three devices, as the cases build it. */
typedef struct {
    bool    scanning;   /**< a scan runs */
    uint8_t left_s;     /**< its seconds left */
    bool    done;       /**< a scan ended */
    bool    cut;        /**< it was stopped by ARMED */
    uint8_t n;          /**< devices found */
    int     bound;      /**< the found device bound, -1 none */
    int     shift;      /**< added to every RSSI and to the first address'
                             last digit (0 to 9): a changed list */
    bool    linked;     /**< the link reads UP */
} spec_t;

/** @brief A command of any of the three devices. */
typedef struct {
    uint8_t kind;   /**< 1 SCAN, 2 BIND, 3 UNBIND: each device's enum */
    uint8_t arg;    /**< the bind's row */
} cmd_t;

/** @brief Device @p i's address with @p shift on its last digit. */
static void addr_of(char *out, size_t n, int i, int shift)
{
    snprintf(out, n, "A0:B1:C2:00:00:%02d R", (i + shift) % 100);
}

/* ------------------------------------------------------------- the Dial */

/** @brief The Dial's view from @p sp. */
static void set_dial(const spec_t *sp)
{
    dial_view_t v;
    memset(&v, 0, sizeof v);
    v.link = sp->scanning ? DIAL_LINK_SCANNING
             : sp->linked ? DIAL_LINK_UP : DIAL_LINK_NONE;
    v.scan_left_s = sp->left_s;
    v.scan_done = sp->done;
    v.scan_cut = sp->cut;
    v.found_n = sp->n;
    for (int i = 0; i < sp->n; ++i) {
        addr_of(v.found[i].addr, sizeof v.found[i].addr, i,
                i == 0 ? sp->shift : 0);
        snprintf(v.found[i].name, sizeof v.found[i].name, "DIAL-%04d", i);
        v.found[i].rssi = (int8_t)(-50 - 5 * i + sp->shift);
    }
    if (sp->bound >= 0) {
        memcpy(v.bound_addr, v.found[sp->bound].addr, sizeof v.bound_addr);
        memcpy(v.bound_name, v.found[sp->bound].name, sizeof v.bound_name);
    }
    wireless_pane_set_dial(&v);
}

/** @brief The Dial's next command. */
static bool take_dial(cmd_t *out)
{
    dial_cmd_t c;
    if (!wireless_pane_take_dial(&c)) {
        return false;
    }
    out->kind = c.kind;
    out->arg = c.arg;
    return true;
}

/* ------------------------------------------------------- the power node */

/** @brief The power node's view from @p sp. */
static void set_pnode(const spec_t *sp)
{
    pnode_view_t v;
    memset(&v, 0, sizeof v);
    v.link = sp->scanning ? PNODE_LINK_SCANNING
             : sp->linked ? PNODE_LINK_UP : PNODE_LINK_NONE;
    v.scan_left_s = sp->left_s;
    v.scan_done = sp->done;
    v.scan_cut = sp->cut;
    v.found_n = sp->n;
    for (int i = 0; i < sp->n; ++i) {
        addr_of(v.found[i].addr, sizeof v.found[i].addr, i,
                i == 0 ? sp->shift : 0);
        snprintf(v.found[i].name, sizeof v.found[i].name,
                 "rcbench-pwr-%04d", i);
        v.found[i].rssi = (int8_t)(-50 - 5 * i + sp->shift);
    }
    if (sp->bound >= 0) {
        memcpy(v.bound_addr, v.found[sp->bound].addr, sizeof v.bound_addr);
        memcpy(v.bound_name, v.found[sp->bound].name, sizeof v.bound_name);
    }
    wireless_pane_set_pnode(&v);
}

/** @brief The power node's next command. */
static bool take_pnode(cmd_t *out)
{
    pnode_cmd_t c;
    if (!wireless_pane_take_pnode(&c)) {
        return false;
    }
    out->kind = c.kind;
    out->arg = c.arg;
    return true;
}

/* ----------------------------------------------------- the balance node */

/** @brief The balance node's view from @p sp. */
static void set_bal(const spec_t *sp)
{
    bal_view_t v;
    memset(&v, 0, sizeof v);
    v.link = sp->scanning ? BAL_LINK_SCANNING
             : sp->linked ? BAL_LINK_UP : BAL_LINK_NONE;
    v.scan_left_s = sp->left_s;
    v.scan_done = sp->done;
    v.scan_cut = sp->cut;
    v.found_n = sp->n;
    for (int i = 0; i < sp->n; ++i) {
        addr_of(v.found[i].addr, sizeof v.found[i].addr, i,
                i == 0 ? sp->shift : 0);
        snprintf(v.found[i].name, sizeof v.found[i].name,
                 "rcbench-bal-%04d", i);
        v.found[i].rssi = (int8_t)(-50 - 5 * i + sp->shift);
    }
    if (sp->bound >= 0) {
        memcpy(v.bound_addr, v.found[sp->bound].addr, sizeof v.bound_addr);
        memcpy(v.bound_name, v.found[sp->bound].name, sizeof v.bound_name);
    }
    wireless_pane_set_bal(&v);
}

/** @brief The balance node's next command. */
static bool take_bal(cmd_t *out)
{
    bal_cmd_t c;
    if (!wireless_pane_take_bal(&c)) {
        return false;
    }
    out->kind = c.kind;
    out->arg = c.arg;
    return true;
}

/* --------------------------------------------------------- the devices */

/** @brief One of the three devices as the cases drive it. */
typedef struct {
    const char                *name;     /**< for the log */
    setting_id_t               en;       /**< its ON row */
    int                        entry;    /**< its WL_ENTRY_* */
    const ui_setlist_custom_t *scan_row; /**< SCAN FOR ... */
    const ui_setlist_custom_t *bind_row; /**< BOUND ... */
    ui_text_id_t               kind;     /**< SEARCH's title */
    ui_text_id_t               armed;    /**< the scan refused while ARMED */
    ui_text_id_t               scanning; /**< a bind behind a scan */
    ui_text_id_t               gone;     /**< a bind of a changed list */
    void (*set)(const spec_t *sp);       /**< hands its view in */
    bool (*take)(cmd_t *out);            /**< its next command */
} wdev_t;

/** @brief The three, in the order of WIRELESS' entries. */
static const wdev_t k_devs[] = {
    { "dial", SET_DIAL_EN, WL_ENTRY_DIAL, &wireless_pane_scan_row,
      &wireless_pane_bind_row, TX_DIAL_ENTRY, TX_DIAL_SCAN_ARMED,
      TX_DIAL_BIND_SCANNING, TX_DIAL_BIND_GONE, set_dial, take_dial },
    { "balance node", SET_BNODE_EN, WL_ENTRY_BAL, &wireless_bal_scan_row,
      &wireless_bal_bind_row, TX_BNODE_ENTRY, TX_BNODE_SCAN_ARMED,
      TX_NODE_BIND_SCANNING, TX_NODE_BIND_GONE, set_bal, take_bal },
    { "power node", SET_PNODE_EN, WL_ENTRY_PNODE,
      &wireless_pane_pnode_scan_row, &wireless_pane_pnode_bind_row,
      TX_PNODE_ENTRY, TX_PNODE_SCAN_ARMED, TX_NODE_BIND_SCANNING,
      TX_NODE_BIND_GONE, set_pnode, take_pnode },
};

/** @brief The devices' count. */
#define DEVS ((int)(sizeof k_devs / sizeof k_devs[0]))

/** @brief A scan that ended with @p n found, @p bound bound. */
static spec_t found(uint8_t n, int bound)
{
    spec_t sp;
    memset(&sp, 0, sizeof sp);
    sp.done = true;
    sp.n = n;
    sp.bound = bound;
    sp.linked = bound >= 0;
    return sp;
}

/** @brief The band's status with the bench ARMED or SAFE. */
static void set_armed(bool armed)
{
    ui_bench_status_t st;
    memset(&st, 0, sizeof st);
    st.armed = armed;
    ui_router_set_status(&st);
}

/** @brief Every command @p d queued dropped. */
static void drain(const wdev_t *d)
{
    cmd_t c;
    while (d->take(&c)) { }
}

/** @brief SETUP > WIRELESS on @p d's entry, the device ON, @p sp its view,
 *         nothing queued. */
static void start(const wdev_t *d, const spec_t *sp)
{
    fresh_hub();
    ui_text_set_language(UI_LANG_EN);
    set_armed(false);
    for (int i = 0; i < DEVS; ++i) {
        spec_t none;
        memset(&none, 0, sizeof none);
        none.bound = -1;
        k_devs[i].set(&none);
        drain(&k_devs[i]);
    }
    open_wireless(d->en);
    d->set(sp);
    drain(d);
    ui_router_set_alert("");
    CHECK_EQ(wireless_pane_selected(), d->entry);
}

/** @brief A tap in the middle of @p r, panel coordinates. */
static void tap_rect(gfx_rect_t r)
{
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/** @brief A tap on the WIRELESS row @p c of @p d's entry. */
static void tap_row(const wdev_t *d, const ui_setlist_custom_t *c)
{
    int x, y;
    wbr_row_mid(d->entry, c, &x, &y);
    tap(x, y);
}

/** @brief The home tag of the screen on top tapped. */
static void home(void)
{
    int x, y;
    wbr_home_at(&x, &y);
    tap(x, y);
}

/** @brief SEARCH's found device @p k tapped. */
static void tap_found(int k)
{
    gfx_rect_t r;
    CHECK(wbr_found(k, &r));
    tap_rect(r);
}

/** @brief One frame drawn into the rig's canvas, framebuffer @p b. */
static void draw(int b)
{
    ui_router_render(&s_c, b);
}

/** @brief The strings the last traced frame drew, panel coordinates. @{ */
#define INKS_MAX 256
static gfx_rect_t s_ink[INKS_MAX];
static int        s_ink_n;
/** @} */

/** @brief gfx_text_watch()'s function: a drawn string's ink, moved from
 *         its canvas to the panel by the canvas' first pixel. */
static void watch(const gfx_canvas_t *c, gfx_rect_t ink)
{
    if (s_ink_n >= INKS_MAX || s_fb == NULL) {
        return;
    }
    const ptrdiff_t at = c->pixels - s_fb;
    ink.x = (int16_t)(ink.x + (int)(at % W));
    ink.y = (int16_t)(ink.y + (int)(at / W));
    s_ink[s_ink_n++] = ink;
}

/** @brief SEARCH drawn whole, every string it draws traced. */
static void draw_traced(void)
{
    s_ink_n = 0;
    wl_search_invalidate();
    gfx_text_watch(watch);
    draw(0);
    gfx_text_watch(NULL);
}

/** @brief The traced strings wholly inside @p r, body coordinates. */
static int texts_in(gfx_rect_t r)
{
    r.y = (int16_t)(r.y + UI_BAND_H);
    int n = 0;
    for (int i = 0; i < s_ink_n; ++i) {
        const gfx_rect_t k = s_ink[i];
        n += (k.x >= r.x && k.y >= r.y && k.x + k.w <= r.x + r.w
              && k.y + k.h <= r.y + r.h) ? 1 : 0;
    }
    return n;
}

/* ---------------------------------------------------------------- cases */

/* For each device: SCAN FOR ... opens SEARCH with the device's kind and
 * queues one SCAN; BOUND ... opens it and queues nothing.  The home tag
 * returns to SETUP > WIRELESS, the list open on the device's entry. */
TEST_CASE(each_row_opens_search_and_the_home_tag_returns_to_the_entry)
{
    for (int i = 0; i < DEVS; ++i) {
        const wdev_t *d = &k_devs[i];
        T_LOG("%s", d->name);
        const spec_t sp = found(2u, 1);
        start(d, &sp);
        tap_row(d, d->scan_row);
        CHECK_EQ(ui_router_current(), SCREEN_WL_SEARCH);
        CHECK(wl_search_device() != NULL);
        CHECK_EQ(wl_search_device()->kind, d->kind);
        CHECK_EQ(wl_search_device()->entry, d->entry);
        cmd_t c;
        CHECK(d->take(&c));
        CHECK_EQ(c.kind, 1u);
        CHECK(!d->take(&c));
        home();
        CHECK_EQ(ui_router_current(), SCREEN_SETUP);
        CHECK_EQ(settings_screen_category(), (int)SET_CAT_WIRELESS);
        CHECK_EQ(wireless_pane_selected(), d->entry);
        CHECK(wbr_row(d->entry, d->bind_row) >= 0);
        tap_row(d, d->bind_row);
        CHECK_EQ(ui_router_current(), SCREEN_WL_SEARCH);
        CHECK_EQ(wl_search_device()->kind, d->kind);
        CHECK(!d->take(&c));
        gfx_rect_t r;
        CHECK(wbr_found(1, &r));
        CHECK(!wbr_found(2, &r));
        home();
        /* The home tag on WIRELESS climbs to the tiles, as before. */
        CHECK_EQ(settings_screen_category(), (int)SET_CAT_WIRELESS);
        home();
        CHECK_EQ(ui_router_current(), SCREEN_SETUP);
        CHECK_EQ(settings_screen_category(), -1);
    }
}

/* The state line in each state, in both languages' formats: SCANNING with
 * the seconds left, FOUND n, NONE FOUND, STOPPED BY ARM, NO SCAN YET. */
TEST_CASE(the_state_line_says_what_the_scan_did)
{
    const wdev_t *d = &k_devs[2];
    spec_t sp = found(2u, -1);
    start(d, &sp);
    tap_row(d, d->bind_row);
    char want[64];
    snprintf(want, sizeof want, TR(WLS_FOUND), 2u);
    CHECK_STR_EQ(wbr_state(), want);
    sp.scanning = true;
    sp.n = 0u;
    sp.left_s = 7u;
    d->set(&sp);
    snprintf(want, sizeof want, TR(WLS_SCANNING), 7u);
    CHECK_STR_EQ(wbr_state(), want);
    CHECK_STR_EQ(wbr_state(), "SCANNING: 7s LEFT");
    sp = found(0u, -1);
    d->set(&sp);
    CHECK_STR_EQ(wbr_state(), TR(DIAL_SCAN_NONE));
    sp = found(1u, -1);
    sp.cut = true;
    d->set(&sp);
    CHECK_STR_EQ(wbr_state(), TR(DIAL_SCAN_CUT));
    gfx_rect_t r;
    CHECK(wbr_found(0, &r));     /* what the cut scan found stays listed */
    sp = found(0u, -1);
    sp.done = false;
    d->set(&sp);
    CHECK_STR_EQ(wbr_state(), TR(WLS_NO_SCAN));
    ui_text_set_language(UI_LANG_DE);
    sp.scanning = true;
    sp.left_s = 3u;
    d->set(&sp);
    CHECK_STR_EQ(wbr_state(), "SUCHE LÄUFT: NOCH 3s");
    ui_text_set_language(UI_LANG_EN);
}

/* What the model lists is on the glass: no row is drawn before a scan
 * found one; after it each found device's row carries its text, all 8 of
 * a full scan; the question's box, its CANCEL and its BIND carry theirs
 * while it is open, and BIND none once it closed.  Without the rows or
 * the question drawn it is the tester's failure: a list that is open in
 * the model and takes taps where nothing shows. */
TEST_CASE(the_found_devices_and_the_question_are_drawn)
{
    for (int i = 0; i < DEVS; ++i) {
        const wdev_t *d = &k_devs[i];
        T_LOG("%s", d->name);
        spec_t sp = found(0u, -1);
        sp.done = false;
        start(d, &sp);
        tap_row(d, d->bind_row);
        draw_traced();
        for (int k = 0; k < (int)WLS_FOUND_MAX; ++k) {
            CHECK_EQ(texts_in(wl_search_row_rect(k)), 0);
        }
        CHECK_EQ(texts_in(wl_search_part(WLS_PART_AGAIN)), 1);
        CHECK(texts_in(wl_search_part(WLS_PART_CARD)) >= 3);
        sp = found((uint8_t)WLS_FOUND_MAX, 3);
        d->set(&sp);
        draw_traced();
        for (int k = 0; k < (int)WLS_FOUND_MAX; ++k) {
            CHECK_EQ(texts_in(wl_search_row_rect(k)), 1);
        }
        CHECK_EQ(texts_in(wl_search_part(WLS_PART_BIND)), 0);
        tap_found(5);
        CHECK(wl_search_asking());
        draw_traced();
        CHECK_EQ(texts_in(wl_search_part(WLS_PART_BIND)), 1);
        CHECK_EQ(texts_in(wl_search_part(WLS_PART_CANCEL)), 1);
        /* The title, the name, the address and RSSI, the bound one it
         * replaces, the note, CANCEL and BIND. */
        CHECK(texts_in(wl_search_part(WLS_PART_BOX)) >= 7);
        tap_rect(wbr_part(WLS_PART_CANCEL));
        CHECK(!wl_search_asking());
        draw_traced();
        CHECK_EQ(texts_in(wl_search_part(WLS_PART_BIND)), 0);
        CHECK_EQ(texts_in(wl_search_row_rect(0)), 1);
    }
}

/* BIND queues the bind of the tapped device by its row, closes the
 * question and stays on SEARCH; the device the bind kept is marked as the
 * bound one once the view says so. */
TEST_CASE(bind_asks_the_bind_of_the_tapped_device)
{
    for (int i = 0; i < DEVS; ++i) {
        const wdev_t *d = &k_devs[i];
        T_LOG("%s", d->name);
        spec_t sp = found(3u, 0);
        start(d, &sp);
        tap_row(d, d->bind_row);
        tap_found(2);
        CHECK(wl_search_asking());
        const wls_dev_t *a = wl_search_asked();
        CHECK(a != NULL);
        char want[WLS_ADDR_MAX];
        addr_of(want, sizeof want, 2, 0);
        CHECK_STR_EQ(a != NULL ? a->addr : "", want);
        cmd_t c;
        CHECK(!d->take(&c));
        tap_rect(wbr_part(WLS_PART_BIND));
        CHECK(!wl_search_asking());
        CHECK_EQ(ui_router_current(), SCREEN_WL_SEARCH);
        CHECK(d->take(&c));
        CHECK_EQ(c.kind, 2u);
        CHECK_EQ(c.arg, 2u);
        CHECK(!d->take(&c));
        sp.bound = 2;
        d->set(&sp);
        wls_view_t v;
        wl_search_view(&v);
        CHECK_STR_EQ(v.bound_addr, want);
    }
}

/* CANCEL closes the question and queues nothing; so does the home tag,
 * which stays on SEARCH while it closes the question. */
TEST_CASE(cancel_and_the_home_tag_close_the_question_and_bind_nothing)
{
    for (int i = 0; i < DEVS; ++i) {
        const wdev_t *d = &k_devs[i];
        T_LOG("%s", d->name);
        const spec_t sp = found(2u, -1);
        start(d, &sp);
        tap_row(d, d->bind_row);
        tap_found(1);
        CHECK(wl_search_asking());
        /* SCAN AGAIN is under the question: a tap there does nothing. */
        tap_rect(wbr_part(WLS_PART_AGAIN));
        CHECK(wl_search_asking());
        tap_rect(wbr_part(WLS_PART_CANCEL));
        CHECK(!wl_search_asking());
        cmd_t c;
        CHECK(!d->take(&c));
        tap_found(0);
        CHECK(wl_search_asking());
        home();
        CHECK(!wl_search_asking());
        CHECK_EQ(ui_router_current(), SCREEN_WL_SEARCH);
        CHECK(!d->take(&c));
    }
}

/* A scan while ARMED: SCAN FOR ... still opens SEARCH, and it and SCAN
 * AGAIN queue no scan and say why on the band.  A scan while one runs:
 * SCAN AGAIN queues none and says so. */
TEST_CASE(a_scan_while_armed_or_running_is_refused_on_the_band)
{
    for (int i = 0; i < DEVS; ++i) {
        const wdev_t *d = &k_devs[i];
        T_LOG("%s", d->name);
        spec_t sp = found(1u, -1);
        start(d, &sp);
        set_armed(true);
        tap_row(d, d->scan_row);
        CHECK_EQ(ui_router_current(), SCREEN_WL_SEARCH);
        cmd_t c;
        CHECK(!d->take(&c));
        CHECK_STR_EQ(wbr_band(), ui_tr(d->armed));
        ui_router_set_alert("");
        tap_rect(wbr_part(WLS_PART_AGAIN));
        CHECK(!d->take(&c));
        CHECK_STR_EQ(wbr_band(), ui_tr(d->armed));
        set_armed(false);
        sp.scanning = true;
        sp.left_s = 9u;
        d->set(&sp);
        ui_router_set_alert("");
        tap_rect(wbr_part(WLS_PART_AGAIN));
        CHECK(!d->take(&c));
        CHECK_STR_EQ(wbr_band(), TR(NODE_SCAN_RUNNING));
        sp = found(1u, -1);
        d->set(&sp);
        tap_rect(wbr_part(WLS_PART_AGAIN));
        CHECK(d->take(&c));
        CHECK_EQ(c.kind, 1u);
    }
}

/* A device tapped while a scan runs opens no question and says so; a
 * question open when a scan starts refuses its BIND the same way and
 * closes. */
TEST_CASE(a_bind_behind_a_running_scan_is_refused_on_the_band)
{
    for (int i = 0; i < DEVS; ++i) {
        const wdev_t *d = &k_devs[i];
        T_LOG("%s", d->name);
        spec_t sp = found(2u, -1);
        sp.scanning = true;
        sp.left_s = 4u;
        start(d, &sp);
        tap_row(d, d->bind_row);
        tap_found(0);
        CHECK(!wl_search_asking());
        CHECK_STR_EQ(wbr_band(), ui_tr(d->scanning));
        sp.scanning = false;
        d->set(&sp);
        tap_found(0);
        CHECK(wl_search_asking());
        sp.scanning = true;
        d->set(&sp);
        ui_router_set_alert("");
        tap_rect(wbr_part(WLS_PART_BIND));
        CHECK(!wl_search_asking());
        CHECK_STR_EQ(wbr_band(), ui_tr(d->scanning));
        cmd_t c;
        CHECK(!d->take(&c));
    }
}

/* The question open on a device, then a new list: another device at its
 * row, or fewer devices.  BIND binds nothing, says the list changed, and
 * closes.  The device still at its row binds as before. */
TEST_CASE(a_list_that_changed_between_tap_and_bind_is_refused)
{
    for (int i = 0; i < DEVS; ++i) {
        const wdev_t *d = &k_devs[i];
        T_LOG("%s", d->name);
        spec_t sp = found(2u, -1);
        start(d, &sp);
        tap_row(d, d->bind_row);
        tap_found(0);
        sp.shift = 3;               /* another device at row 0 */
        d->set(&sp);
        tap_rect(wbr_part(WLS_PART_BIND));
        CHECK(!wl_search_asking());
        CHECK_STR_EQ(wbr_band(), ui_tr(d->gone));
        cmd_t c;
        CHECK(!d->take(&c));
        tap_found(1);
        sp = found(1u, -1);         /* row 1 gone */
        d->set(&sp);
        ui_router_set_alert("");
        tap_rect(wbr_part(WLS_PART_BIND));
        CHECK_STR_EQ(wbr_band(), ui_tr(d->gone));
        CHECK(!d->take(&c));
        tap_found(0);
        tap_rect(wbr_part(WLS_PART_BIND));
        CHECK(d->take(&c));
        CHECK_EQ(c.kind, 2u);
        CHECK_EQ(c.arg, 0u);
    }
}

/* A finger down on a device, the list changed under it, the finger up:
 * no question on the device that took the row's place; the band says the
 * list changed. */
TEST_CASE(a_list_that_changed_under_the_finger_opens_no_question)
{
    const wdev_t *d = &k_devs[0];
    spec_t sp = found(2u, -1);
    start(d, &sp);
    tap_row(d, d->bind_row);
    gfx_rect_t r;
    CHECK(wbr_found(0, &r));
    wbr_down(r.x + r.w / 2, r.y + r.h / 2);
    sp.shift = 5;
    d->set(&sp);
    wbr_up(r.x + r.w / 2, r.y + r.h / 2);
    CHECK(!wl_search_asking());
    CHECK_STR_EQ(wbr_band(), ui_tr(d->gone));
}

/* The views a running bench hands in each frame: RSSI changes, the link
 * going UP and away, the bound device changing, rendered after each.  The
 * question stays open on the device its tap took, a finger held on BIND
 * stays held and binds on its release, and a finger held on a row opens
 * the question on its release. */
TEST_CASE(view_updates_keep_the_question_and_the_press)
{
    for (int i = 0; i < DEVS; ++i) {
        const wdev_t *d = &k_devs[i];
        T_LOG("%s", d->name);
        spec_t sp = found(3u, -1);
        start(d, &sp);
        tap_row(d, d->bind_row);
        /* A finger held on a row through the updates. */
        gfx_rect_t r;
        CHECK(wbr_found(1, &r));
        wbr_down(r.x + r.w / 2, r.y + r.h / 2);
        for (int f = 0; f < 20; ++f) {
            spec_t u = sp;
            u.linked = (f & 1) != 0;
            u.bound = (f % 3) - 1;
            d->set(&u);
            ui_router_tick(0.016f);
            draw(f & 1);
        }
        wbr_up(r.x + r.w / 2, r.y + r.h / 2);
        CHECK(wl_search_asking());
        char want[WLS_ADDR_MAX];
        addr_of(want, sizeof want, 1, 0);
        CHECK_STR_EQ(wl_search_asked() != NULL ? wl_search_asked()->addr
                                               : "", want);
        /* The question through the updates, then a finger held on BIND
         * through more of them. */
        const gfx_rect_t b = wbr_part(WLS_PART_BIND);
        for (int f = 0; f < 40; ++f) {
            spec_t u = sp;
            u.linked = (f & 1) != 0;
            u.bound = (f % 4) - 1;
            d->set(&u);
            if (f == 20) {
                wbr_down(b.x + b.w / 2, b.y + b.h / 2);
            }
            ui_router_tick(0.016f);
            draw(f & 1);
            CHECK(wl_search_asking());
        }
        CHECK_STR_EQ(wl_search_asked() != NULL ? wl_search_asked()->addr
                                               : "", want);
        cmd_t c;
        CHECK(!d->take(&c));
        wbr_up(b.x + b.w / 2, b.y + b.h / 2);
        CHECK(!wl_search_asking());
        CHECK(d->take(&c));
        CHECK_EQ(c.kind, 2u);
        CHECK_EQ(c.arg, 1u);
    }
}

/** @brief A finger on UNBIND: DOWN, or UP, or a MOVE to (@p dx, 0) from
 *         its middle, panel coordinates. */
static void unbind_touch(touch_event_type_t type, int dx)
{
    const gfx_rect_t r = wbr_part(WLS_PART_UNBIND);
    touch_event_t e = { .type = type,
                        .point = { .id = 1,
                                   .x = (int16_t)(r.x + r.w / 2 + dx),
                                   .y = (int16_t)(r.y + r.h / 2) } };
    ui_router_event(&e);
}

/** @brief Frames of 250ms, @p n of them, then one of @p rest_s. */
static void hold_frames(int n, float rest_s)
{
    for (int k = 0; k < n; ++k) {
        ui_router_tick(0.25f);
    }
    if (rest_s > 0.0f) {
        ui_router_tick(rest_s);
    }
}

/* For each device, bound: UNBIND let go at 1999ms queues nothing and the
 * band shows the hint; held to 2000ms it queues one UNBIND on that frame,
 * and none more while the finger stays or on its release.  ARMED does not
 * refuse it.  The label is drawn on the button. */
TEST_CASE(unbind_fires_at_2000ms_and_not_at_1999ms)
{
    for (int i = 0; i < DEVS; ++i) {
        const wdev_t *d = &k_devs[i];
        T_LOG("%s", d->name);
        const spec_t sp = found(2u, 0);
        start(d, &sp);
        tap_row(d, d->bind_row);
        CHECK(wl_search_unbind_ready());
        draw_traced();
        CHECK_EQ(texts_in(wl_search_part(WLS_PART_UNBIND)), 1);
        cmd_t c;
        ui_router_set_alert(NULL);
        unbind_touch(TOUCH_EVENT_DOWN, 0);
        hold_frames(7, 0.249f);
        CHECK(!d->take(&c));
        unbind_touch(TOUCH_EVENT_UP, 0);
        CHECK_STR_EQ(wbr_band(), ui_tr(TX_HOLD_HINT_WLS_UNBIND));
        CHECK(!d->take(&c));
        ui_router_set_alert("");
        set_armed(true);
        unbind_touch(TOUCH_EVENT_DOWN, 0);
        hold_frames(7, 0.249f);
        CHECK(!d->take(&c));
        ui_router_tick(0.001f);
        CHECK(d->take(&c));
        CHECK_EQ(c.kind, 3u);
        CHECK(!d->take(&c));
        hold_frames(8, 0.0f);
        unbind_touch(TOUCH_EVENT_UP, 0);
        CHECK(!d->take(&c));
        CHECK_STR_EQ(wbr_band(), "");
        CHECK_EQ(ui_router_current(), SCREEN_WL_SEARCH);
        set_armed(false);
    }
}

/* For each device, nothing bound: UNBIND is grey, takes no hold, and a
 * press on it queues nothing however long, the band saying NOTHING IS
 * BOUND.  With the bind question open UNBIND takes no press at all. */
TEST_CASE(unbind_with_nothing_bound_is_grey_and_refused)
{
    for (int i = 0; i < DEVS; ++i) {
        const wdev_t *d = &k_devs[i];
        T_LOG("%s", d->name);
        const spec_t sp = found(2u, -1);
        start(d, &sp);
        tap_row(d, d->bind_row);
        CHECK(!wl_search_unbind_ready());
        draw_traced();
        CHECK_EQ(texts_in(wl_search_part(WLS_PART_UNBIND)), 1);
        cmd_t c;
        wbr_hold_unbind(NULL, 3000u);
        CHECK(!d->take(&c));
        CHECK_STR_EQ(wbr_band(), TR(WLS_UNBIND_NONE));
        /* Bound, with the question open: the question takes the press. */
        const spec_t b = found(2u, 0);
        d->set(&b);
        tap_found(1);
        CHECK(wl_search_asking());
        CHECK(!wl_search_unbind_ready());
        ui_router_set_alert("");
        wbr_hold_unbind(NULL, 3000u);
        CHECK(!d->take(&c));
        CHECK(wl_search_asking());
        CHECK_STR_EQ(wbr_band(), "");
    }
}

/* A hold on UNBIND ends without an unbind when the bound device goes
 * under the finger (another bind, the device OFF), and when the finger
 * slides off the button; sliding back does not resume it. */
TEST_CASE(an_unbind_hold_ends_when_the_device_goes_or_the_finger_slides_off)
{
    for (int i = 0; i < DEVS; ++i) {
        const wdev_t *d = &k_devs[i];
        T_LOG("%s", d->name);
        const spec_t sp = found(2u, 0);
        start(d, &sp);
        tap_row(d, d->bind_row);
        cmd_t c;
        unbind_touch(TOUCH_EVENT_DOWN, 0);
        hold_frames(4, 0.0f);
        const spec_t none = found(2u, -1);
        d->set(&none);
        hold_frames(8, 0.0f);
        d->set(&sp);
        hold_frames(8, 0.0f);
        unbind_touch(TOUCH_EVENT_UP, 0);
        CHECK(!d->take(&c));
        unbind_touch(TOUCH_EVENT_DOWN, 0);
        hold_frames(4, 0.0f);
        unbind_touch(TOUCH_EVENT_MOVE, -200);
        hold_frames(4, 0.0f);
        unbind_touch(TOUCH_EVENT_MOVE, 0);
        hold_frames(8, 0.0f);
        unbind_touch(TOUCH_EVENT_UP, 0);
        CHECK(!d->take(&c));
        /* A fresh press still unbinds. */
        wbr_hold_unbind(NULL, 2000u);
        CHECK(d->take(&c));
        CHECK_EQ(c.kind, 3u);
    }
}

int main(void)
{
    RUN(each_row_opens_search_and_the_home_tag_returns_to_the_entry);
    RUN(the_state_line_says_what_the_scan_did);
    RUN(the_found_devices_and_the_question_are_drawn);
    RUN(bind_asks_the_bind_of_the_tapped_device);
    RUN(cancel_and_the_home_tag_close_the_question_and_bind_nothing);
    RUN(a_scan_while_armed_or_running_is_refused_on_the_band);
    RUN(a_bind_behind_a_running_scan_is_refused_on_the_band);
    RUN(a_list_that_changed_between_tap_and_bind_is_refused);
    RUN(a_list_that_changed_under_the_finger_opens_no_question);
    RUN(view_updates_keep_the_question_and_the_press);
    RUN(unbind_fires_at_2000ms_and_not_at_1999ms);
    RUN(unbind_with_nothing_bound_is_grey_and_refused);
    RUN(an_unbind_hold_ends_when_the_device_goes_or_the_finger_slides_off);
    return test_summary("wl_search");
}
