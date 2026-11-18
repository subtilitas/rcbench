/**
 * @file render_outputs.c
 * @brief OUTPUTS' views of a pin another protocol holds for
 *        tools/render_ui.py (render_outputs.h).
 *
 * outputs-take: the RP2350-CAN with its drawing, SERVO PWM on GPIO 0 to 2
 * and DSHOT600 on GPIO 6 and 7, SERVO PWM picked and GPIO 7 tapped: the
 * card over the board, DSHOT600 keeping 1 pin and CH4 becoming a servo
 * channel.  outputs-take-last: SERVO PWM on GPIO 28 alone, DSHOT600 BIDIR
 * picked and GPIO 28 tapped: SERVO PWM keeps no pin, CH0 becomes a motor
 * channel, the longest names on the card.  outputs-take-why: SENSORS and
 * PD MINI wired as on outputs-iface, SERVO PWM picked and GPIO 16 tapped:
 * no card, the reason names SENSORS.  outputs-grid-take: a board with
 * neither a shape nor a drawing, SERVO PWM on GPIO 0 to 2 and DSHOT300 on
 * GPIO 4 to 6, GPIO 5 tapped: the card over the grid's two right columns.
 * outputs-rbmod-take: the rbmod carrier as outputs-rbmod poses it,
 * DSHOT300 picked and PWM01 tapped.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_outputs.h"

#include <string.h>

#include "out_bind.h"
#include "outputs_screen.h"
#include "outputs_spots.h"
#include "picker_screen.h"
#include "settings.h"
#include "settings_sensors.h"
#include "ui_layout.h"
#include "ui_screen.h"

/** @brief The drawing the IOmcu serves for the RP2350-CAN. */
extern const uint8_t  board_drawing_rp2350_can[];
extern const uint32_t board_drawing_rp2350_can_bytes; /**< its size */

/**
 * @brief A tap at (@p x, @p y) of the body, through the router.
 * @param x the column
 * @param y the row under the band
 */
static void tap(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)(UI_BAND_H + y),
                                   .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/**
 * @brief Protocol @p name's index in the catalogue.
 * @param name the name
 * @return the index, OFF for none
 */
static uint8_t proto(const char *name)
{
    for (uint8_t i = 0; i < OUTBIND_PROTOS; ++i) {
        if (strcmp(outbind_protos()[i].name, name) == 0) {
            return i;
        }
    }
    return 0u;
}

/**
 * @brief Bind GPIO @p gpio of board @p board to protocol @p name.
 * @param b     the binding
 * @param board the board
 * @param name  the protocol
 * @param gpio  the pin
 */
static void bind(outbind_t *b, uint16_t board, const char *name, uint8_t gpio)
{
    outbind_set_proto(b, proto(name));
    (void)outbind_toggle(b, outbind_index_of(board, gpio));
}

/**
 * @brief Both views hold @p b, @p picked selected, the board view
 *        following.
 * @param b      the binding
 * @param picked the protocol selected
 */
static void show(outbind_t *b, const char *picked)
{
    outbind_set_proto(b, proto(picked));
    outputs_screen_set_binding(b);
    picker_screen_set_binding(b);
    outputs_screen_select(proto(picked));
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
    ui_router_tick(0.0f);
}

/**
 * @brief A tap on GPIO @p gpio's key on the board view.
 * @param board the board
 * @param gpio  the pin
 */
static void tap_key(uint16_t board, uint8_t gpio)
{
    gfx_rect_t k;
    if (picker_screen_button_rect(outbind_index_of(board, gpio), &k)) {
        tap(k.x + k.w / 2, k.y + k.h / 2);
    }
}

/**
 * @brief The board views: the RP2350-CAN with its drawing.
 * @param view the view's name
 */
static void board_views(const char *view)
{
    const uint16_t brd = OUTBIND_BOARD_PICO_HEADER;
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, brd);
    static const uint8_t none[LINK_PG_PORTS_MAX] = { 0xFFu, 0xFFu, 0xFFu,
                                                     0xFFu };
    picker_screen_set_prog_ports(0u, none);
    if (strcmp(view, "outputs-take-last") == 0) {
        bind(&b, brd, "SERVO PWM", 28u);
        show(&b, "DSHOT600 BIDIR");
    } else {
        for (uint8_t g = 0; g < 3u; ++g) {
            bind(&b, brd, "SERVO PWM", g);
        }
        bind(&b, brd, "DSHOT600", 6u);
        bind(&b, brd, "DSHOT600", 7u);
        show(&b, "SERVO PWM");
    }
    (void)picker_screen_set_drawing(board_drawing_rp2350_can,
                                    board_drawing_rp2350_can_bytes, NULL);
    if (strcmp(view, "outputs-take-why") == 0) {
        settings_set(SET_INA228_EN, 1.0f);
        settings_set(SET_INA_FIT(0), 1.0f);
        settings_set(SET_SENSE_SDA, 16.0f);
        settings_set(SET_PDMINI_EN, 1.0f);
        settings_set(SET_PDMINI_RX, 18.0f);
        settings_set(SET_PDMINI_TX, 19.0f);
        settings_set(SET_TONE_EN, 1.0f);
        ui_router_tick(0.0f);
        tap_key(brd, 16u);
        return;
    }
    tap_key(brd, strcmp(view, "outputs-take-last") == 0 ? 28u : 7u);
}

/**
 * @brief The grid of a board with neither a shape nor a drawing.
 */
static void grid_view(void)
{
    const uint16_t learned = 4242u;
    uint16_t cat[LINK_CAT_COUNT] = { 0 };
    for (unsigned i = 0; i < 12u && i < LINK_CAT_COUNT; ++i) {
        cat[i] = LINK_CAT_OF(i, i + 1u, LINK_PIN_FREE);
    }
    (void)outbind_learn_board(learned, cat);
    outbind_t g;
    outbind_init(&g);
    outbind_set_board(&g, learned);
    for (uint8_t i = 0; i < 3u; ++i) {
        bind(&g, learned, "SERVO PWM", i);
    }
    for (uint8_t i = 4; i < 7u; ++i) {
        bind(&g, learned, "DSHOT300", i);
    }
    show(&g, "SERVO PWM");
    tap(OU_CELL_X(5) + 60, OU_CELL_Y(5) + 30);
}

bool render_outputs_pose(const char *view)
{
    if (strcmp(view, "outputs-take") == 0
        || strcmp(view, "outputs-take-last") == 0
        || strcmp(view, "outputs-take-why") == 0) {
        board_views(view);
        return true;
    }
    if (strcmp(view, "outputs-grid-take") == 0) {
        grid_view();
        return true;
    }
    if (strcmp(view, "outputs-rbmod-take") == 0) {
        /* The carrier as outputs-rbmod: DSHOT300 picked, PWM01 tapped. */
        outputs_screen_select(proto("DSHOT300"));
        ui_router_tick(0.0f);
        tap(OU_CELL_X(0) + 60, OU_CELL_Y(0) + 30);
        return true;
    }
    return false;
}
