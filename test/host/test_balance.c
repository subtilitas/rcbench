/*
 * What the balance screen has to get right so far: both panes draw, the tabs
 * switch them, and no line of the guidance runs off the card it is in.
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "balance_screen.h"
#include "ui_screen.h"
#include "ui_theme.h"

#define W 800
#define H 480

/* Mirrored from balance_screen.c. */
#define PAD      6
#define LCARD_W  494
#define RCARD_X  (PAD + LCARD_W + 8)
#define RCARD_W  (W - RCARD_X - PAD)

static gfx_color_t *fb;
static gfx_canvas_t cv;
static const ui_screen_t *scr;

static void fresh(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    scr = balance_screen();
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

static int lit(void)
{
    int n = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != ui_theme_color(UI_C_BG)) { ++n; }
    }
    return n;
}

/*
 * The guidance is prose in a fixed-width font in a card of known width, which
 * is a combination that silently truncates.  It did: three of these lines ran
 * off the right-hand edge on the first draw, and nothing said so -- the text
 * renderer clips and returns.
 *
 * So look at the two columns just inside the card's edge.  Anything of the
 * text colours there means a line reached the wall, and the next word after
 * it was never seen.
 */
TEST_CASE(no_guidance_runs_off_its_card)
{
    for (int pane = 0; pane < 2; ++pane) {
        fresh();
        if (pane == 1) {
            tap(200, 22);                   /* the AIRCRAFT tab */
        }
        scr->render(&cv, 0);

        const gfx_color_t text = ui_theme_color(UI_C_TEXT);
        const gfx_color_t dim  = ui_theme_color(UI_C_TEXT_DIM);
        int touching = 0;
        for (int x = RCARD_X + RCARD_W - 3; x < RCARD_X + RCARD_W - 1; ++x) {
            for (int y = 0; y < H; ++y) {
                const gfx_color_t p = fb[(size_t)y * W + x];
                if (p == text || p == dim) { ++touching; }
            }
        }
        if (touching > 0) {
            T_FAIL("pane %d has %d text pixels against the card edge",
                   pane, touching);
        }
    }
}

/* Both panes draw something, and they draw different things. */
TEST_CASE(both_panes_draw_and_differ)
{
    fresh();
    scr->render(&cv, 0);
    const int rig = lit();
    gfx_color_t *first = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(first, fb, (size_t)W * H * sizeof(gfx_color_t));

    tap(200, 22);
    scr->render(&cv, 0);
    const int air = lit();

    CHECK(rig > 20000);
    CHECK(air > 5000);
    int differ = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != first[i]) { ++differ; }
    }
    if (differ < 5000) {
        T_FAIL("the two panes differ by only %d pixels", differ);
    }
    free(first);
}

/*
 * The rig diagram has to stay inside its own card.  A callout leader placed
 * by hand is exactly the sort of thing that ends up under the neighbouring
 * panel after a layout change.
 */
TEST_CASE(the_diagram_stays_in_its_card)
{
    fresh();
    scr->render(&cv, 0);
    int stray = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = PAD + LCARD_W; x < RCARD_X; ++x) {
            if (fb[(size_t)y * W + x] != ui_theme_color(UI_C_BG)) { ++stray; }
        }
    }
    if (stray > 0) {
        T_FAIL("%d pixels of the diagram are in the gutter", stray);
    }
}

int main(void)
{
    RUN(no_guidance_runs_off_its_card);
    RUN(both_panes_draw_and_differ);
    RUN(the_diagram_stays_in_its_card);
    return test_summary("balance");
}
