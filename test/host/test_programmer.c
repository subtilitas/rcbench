/*
 * What the programmer decides: that a protocol is chosen before anything is
 * read, that changing it cannot leave the previous device's identity beside
 * the new one's parameters, and that a stepper stops rather than wraps.
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "programmer_screen.h"
#include "ui_screen.h"
#include "ui_theme.h"

#define W 800
#define H 480

static gfx_color_t *fb;
static gfx_canvas_t cv;
static const ui_screen_t *scr;

/* Mirrored from programmer_screen.c; if the layout moves these move with it. */
#define CHIP_CX(i) (18 + (i) * 154 + 73)
#define CHIP_CY    37
#define CONNECT_X  698
#define CONNECT_Y  131
#define ROW_DN_X   653
#define ROW_UP_X   765
#define ROW_CY(i)  (206 + (i) * 28 + 10)
#define PAGE_UP_X  723
#define PAGE_DN_X  761
#define PAGE_CY    190

static void fresh(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    scr = programmer_screen();
    scr->reset();
}

static void tap(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    scr->event(&e);
    e.type = TOUCH_EVENT_UP;
    scr->event(&e);
}

/*
 * The reason the protocol is chosen first: the device that answers a one-wire
 * bootloader is not the device that will answer a text CLI, and a screen
 * showing one identity above the other's parameters is the single lie this
 * must never tell.
 */
TEST_CASE(changing_protocol_drops_the_connection)
{
    fresh();
    tap(CONNECT_X, CONNECT_Y);
    CHECK(programmer_screen_connected());

    tap(CHIP_CX(2), CHIP_CY);                /* ESCape32 */
    CHECK_EQ(programmer_screen_protocol(), 2);
    CHECK(!programmer_screen_connected());
}

/* Each protocol brings its own parameters and its own defaults, rather than
 * carrying the last one's indices across into a different list. */
TEST_CASE(each_protocol_starts_from_its_own_defaults)
{
    fresh();
    tap(CONNECT_X, CONNECT_Y);
    /* BLHeli_S: PWM frequency is the third row and starts at 24 kHz. */
    CHECK_EQ(programmer_screen_choice(2), 0);
    tap(ROW_UP_X, ROW_CY(2));
    CHECK_EQ(programmer_screen_choice(2), 1);

    tap(CHIP_CX(1), CHIP_CY);                /* AM32 */
    tap(CONNECT_X, CONNECT_Y);
    /* AM32's third row starts at 48 kHz, not at whatever BLHeli_S was left
     * on -- and certainly not at the index that was edited. */
    CHECK_EQ(programmer_screen_choice(2), 1);
}

/*
 * Clamped, not wrapped.  A parameter that rolls from its last value round to
 * its first will one day be set to the wrong end by somebody pressing once
 * more than they meant to, and on an ESC the wrong end is a direction.
 */
TEST_CASE(a_stepper_stops_at_its_ends)
{
    fresh();
    tap(CONNECT_X, CONNECT_Y);

    for (int i = 0; i < 12; ++i) {
        tap(ROW_DN_X, ROW_CY(0));
    }
    CHECK_EQ(programmer_screen_choice(0), 0);

    for (int i = 0; i < 12; ++i) {
        tap(ROW_UP_X, ROW_CY(0));
    }
    /* Motor direction has three values, so the top is two. */
    CHECK_EQ(programmer_screen_choice(0), 2);
}

/* Nothing is editable until something has answered. */
TEST_CASE(no_device_means_no_editing)
{
    fresh();
    const int before = programmer_screen_choice(0);
    tap(ROW_UP_X, ROW_CY(0));
    CHECK_EQ(programmer_screen_choice(0), before);

    tap(CONNECT_X, CONNECT_Y);
    tap(ROW_UP_X, ROW_CY(0));
    CHECK(programmer_screen_choice(0) != before);
}

/* Eight parameters in six rows: the other two have to be reachable. */
TEST_CASE(paging_reaches_the_rows_that_do_not_fit)
{
    fresh();
    tap(CONNECT_X, CONNECT_Y);
    scr->render(&cv, 0);

    gfx_color_t *first = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(first, fb, (size_t)W * H * sizeof(gfx_color_t));

    tap(PAGE_DN_X, PAGE_CY);
    tap(PAGE_DN_X, PAGE_CY);
    scr->render(&cv, 0);
    int differ = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != first[i]) { ++differ; }
    }
    if (differ < 2000) {
        T_FAIL("paging changed only %d pixels", differ);
    }

    /* And it stops at the end rather than running off it. */
    for (int i = 0; i < 10; ++i) {
        tap(PAGE_DN_X, PAGE_CY);
    }
    scr->render(&cv, 0);
    gfx_color_t *bottom = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(bottom, fb, (size_t)W * H * sizeof(gfx_color_t));
    tap(PAGE_DN_X, PAGE_CY);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(bottom, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);

    free(first);
    free(bottom);
}

/* Every protocol renders, and each says which transport it is. */
TEST_CASE(every_protocol_draws_and_names_its_transport)
{
    for (int p = 0; p < 5; ++p) {
        fresh();
        tap(CHIP_CX(p), CHIP_CY);
        tap(CONNECT_X, CONNECT_Y);
        memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        scr->render(&cv, 0);

        int lit = 0;
        for (int i = 0; i < W * H; ++i) {
            if (fb[i] != ui_theme_color(UI_C_BG)) { ++lit; }
        }
        if (lit < 20000) {
            T_FAIL("protocol %d drew only %d pixels", p, lit);
        }
        CHECK_EQ(programmer_screen_protocol(), p);
    }
}

int main(void)
{
    RUN(changing_protocol_drops_the_connection);
    RUN(each_protocol_starts_from_its_own_defaults);
    RUN(a_stepper_stops_at_its_ends);
    RUN(no_device_means_no_editing);
    RUN(paging_reaches_the_rows_that_do_not_fit);
    RUN(every_protocol_draws_and_names_its_transport);
    return test_summary("programmer");
}
