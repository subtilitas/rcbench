/*
 * Binding a protocol to pins.  See out_bind.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_bind.h"

#include <stddef.h>

#include "link_pages.h"
#include "outputs_pages.h"

/*
 * The header, in GPIO order.
 *
 * GP23 to GP25 are missing because the Pico form factor does not bring them
 * out, and the pad numbers are the ones printed on the board so an operator
 * counting pads and an operator reading GPIOs arrive at the same pin.
 *
 * The reserved six are the coprocessor's: GP3 carries the safety heartbeat
 * in from the panel, and GP8 to GP12 are the CAN (Controller Area Network)
 * controller's interrupt and its SPI (Serial Peripheral Interface) bus.
 * firmware/iomcu/include/iomcu_pins.h assigns the same pins for the drivers
 * that use them; the coprocessor reserves the union of both, so the two
 * disagreeing costs a pin rather than the safety line.
 */
static const outbind_pin_t k_pico_pins[OUTBIND_PINS] = {
    {  0,  1, false, NULL },
    {  1,  2, false, NULL },
    {  2,  4, false, NULL },
    {  3,  5, true,  "heartbeat" },
    {  4,  6, false, NULL },
    {  5,  7, false, NULL },
    {  6,  9, false, NULL },
    {  7, 10, false, NULL },
    {  8, 11, true,  "CAN INT" },
    {  9, 12, true,  "CAN CS" },
    { 10, 14, true,  "CAN SCK" },
    { 11, 15, true,  "CAN MOSI" },
    { 12, 16, true,  "CAN MISO" },
    { 13, 17, false, NULL },
    { 14, 19, false, NULL },
    { 15, 20, false, NULL },
    { 16, 21, false, NULL },
    { 17, 22, false, NULL },
    { 18, 24, false, NULL },
    { 19, 25, false, NULL },
    { 20, 26, false, NULL },
    { 21, 27, false, NULL },
    { 22, 29, false, NULL },
    { 26, 31, false, NULL },
    { 27, 32, false, NULL },
    { 28, 34, false, NULL },
};

/*
 * Every board this build knows.  A second one appends a row here and a
 * catalogue above it; nothing else in this file is per-board.
 */
static const outbind_board_t k_boards[] = {
    { OUTBIND_BOARD_PICO_HEADER, "RP2350-CAN", k_pico_pins, OUTBIND_PINS,
      false },
};

const outbind_board_t *outbind_board(uint16_t id)
{
    for (unsigned i = 0; i < sizeof(k_boards) / sizeof(k_boards[0]); ++i) {
        if (k_boards[i].id == id) {
            return &k_boards[i];
        }
    }
    return NULL;                 /* including OUTBIND_BOARD_UNKNOWN */
}

uint8_t outbind_board_count(void)
{
    return (uint8_t)(sizeof(k_boards) / sizeof(k_boards[0]));
}

const outbind_board_t *outbind_board_at(uint8_t index)
{
    return (index < outbind_board_count()) ? &k_boards[index] : NULL;
}

/* Which bits of a selection can mean anything on this board. */
static uint32_t board_mask(const outbind_board_t *bd)
{
    if (bd == NULL) {
        return 0u;
    }
    return (bd->count >= 32u) ? ~(uint32_t)0u
                              : (((uint32_t)1u << bd->count) - 1u);
}

bool outbind_pin_selectable(const outbind_board_t *board, uint8_t index)
{
    if (board == NULL || index >= board->count) {
        return false;
    }
    if (board->fixed) {
        return false;      /* soldered: there is nothing here to choose */
    }
    return !board->pins[index].reserved;
}

const outbind_pin_t *outbind_pins(uint16_t board)
{
    const outbind_board_t *b = outbind_board(board);
    return (b != NULL) ? b->pins : NULL;
}

uint8_t outbind_pin_count(uint16_t board)
{
    const outbind_board_t *b = outbind_board(board);
    return (b != NULL) ? b->count : 0u;
}

uint8_t outbind_index_of(uint16_t board, uint8_t gpio)
{
    const outbind_board_t *b = outbind_board(board);
    if (b == NULL) {
        return 0u;               /* no catalogue, so no index into one */
    }
    for (uint8_t i = 0; i < b->count; ++i) {
        if (b->pins[i].gpio == gpio) {
            return i;
        }
    }
    return b->count;
}

uint64_t outbind_reserved_mask(uint16_t board)
{
    const outbind_board_t *b = outbind_board(board);
    if (b == NULL) {
        /* A build that cannot say which pins are safe hands none of them
         * out.  Refusing everything is the failure that shows; reserving
         * nothing is the one that drives the heartbeat line. */
        return ~(uint64_t)0u;
    }
    uint64_t m = 0u;
    for (uint8_t i = 0; i < b->count; ++i) {
        if (b->pins[i].reserved) {
            m |= (uint64_t)1u << b->pins[i].gpio;
        }
    }
    return m;
}

/* ------------------------------------------------------------- protocols */

/*
 * PWM and PPM run at 50 Hz, which every analogue and digital servo takes.
 * The DShot rates are bit rates and the two bidirectional entries are the
 * same wire asking for telemetry back.
 */
static const outbind_proto_t k_protos[OUTBIND_PROTOS] = {
    { "OFF",            OUT_DRIVER_NONE,        0,   0, 0 },
    { "SERVO PWM",      OUT_DRIVER_PWM,        50,   8, 1 },
    { "PPM",            OUT_DRIVER_PPM,        50,   1, 8 },
    { "DSHOT300",       OUT_DRIVER_DSHOT,     300,   8, 1 },
    { "DSHOT600",       OUT_DRIVER_DSHOT,     600,   8, 1 },
    { "DSHOT300 BIDIR", OUT_DRIVER_DSHOT_BIDIR, 300, 8, 1 },
    { "DSHOT600 BIDIR", OUT_DRIVER_DSHOT_BIDIR, 600, 8, 1 },
};

const outbind_proto_t *outbind_protos(void) { return k_protos; }

static const outbind_proto_t *proto_of(const outbind_t *b)
{
    return &k_protos[(b->proto < OUTBIND_PROTOS) ? b->proto : 0u];
}

/* --------------------------------------------------------- the selection */

void outbind_init(outbind_t *b)
{
    if (b == NULL) {
        return;
    }
    b->board = (uint16_t)OUTBIND_BOARD_UNKNOWN;
    b->proto = 0u;      /* OFF: nothing drives until somebody says so */
    b->pins  = 0u;
}

void outbind_set_board(outbind_t *b, uint16_t board)
{
    if (b == NULL || b->board == board) {
        return;
    }
    b->board = board;
    /* A pin index belongs to one catalogue.  Keeping the selection across a
     * change of board would bind whatever sits at that index on the board
     * that is actually connected. */
    b->pins  = 0u;
    b->proto = 0u;
}

uint8_t outbind_chosen(const outbind_t *b)
{
    if (b == NULL) {
        return 0u;
    }
    uint8_t n = 0u;
    const uint8_t cnt = outbind_pin_count(b->board);
    for (uint8_t i = 0; i < cnt; ++i) {
        if ((b->pins & ((uint32_t)1u << i)) != 0u) {
            ++n;
        }
    }
    return n;
}

bool outbind_can_add(const outbind_t *b, uint8_t index)
{
    if (b == NULL) {
        return false;
    }
    const outbind_board_t *bd = outbind_board(b->board);
    if (!outbind_pin_selectable(bd, index)) {
        return false;
    }
    const outbind_proto_t *p = proto_of(b);
    if (p->max_pins == 0u) {
        return false;                 /* OFF takes no pins */
    }
    const uint8_t cap = (p->max_pins < OUT_MAX_SLOTS) ? p->max_pins
                                                      : (uint8_t)OUT_MAX_SLOTS;
    return outbind_chosen(b) < cap;
}

bool outbind_toggle(outbind_t *b, uint8_t index)
{
    if (b == NULL) {
        return false;
    }
    const outbind_board_t *bd = outbind_board(b->board);
    if (bd == NULL || index >= bd->count) {
        return false;
    }
    /*
     * A soldered board refuses both directions.  Only refusing additions
     * would let an operator untick an output that is physically wired, and
     * the page written from that would take away a driver the board still
     * has a connector for.
     */
    if (bd->fixed) {
        return false;
    }
    const uint32_t bit = (uint32_t)1u << index;
    if ((b->pins & bit) != 0u) {
        b->pins &= ~bit;              /* undoing is allowed; adding may not be */
        return true;
    }
    if (!outbind_can_add(b, index)) {
        return false;
    }
    b->pins |= bit;
    return true;
}

void outbind_set_proto(outbind_t *b, uint8_t proto)
{
    if (b == NULL || proto >= OUTBIND_PROTOS) {
        return;
    }
    b->proto = proto;
    /*
     * Trim from the top, keeping the pins chosen first.  Switching to PPM
     * with four pins ticked is an operator who has decided on PPM; refusing
     * the switch would make them undo the pins to say so.
     */
    const outbind_proto_t *p = proto_of(b);
    const uint8_t cap = (p->max_pins < OUT_MAX_SLOTS) ? p->max_pins
                                                      : (uint8_t)OUT_MAX_SLOTS;
    /*
     * Anything above the board's own width first.  The trim below only walks
     * that width, so a stray high bit would survive every protocol change and
     * leave outbind_chosen() and the mask disagreeing about what is selected.
     */
    const outbind_board_t *bd = outbind_board(b->board);
    b->pins &= board_mask(bd);

    uint8_t kept = 0u;
    const uint8_t cnt = (bd != NULL) ? bd->count : 0u;
    for (uint8_t i = 0; i < cnt; ++i) {
        const uint32_t bit = (uint32_t)1u << i;
        if ((b->pins & bit) == 0u) {
            continue;
        }
        if (kept < cap && !bd->pins[i].reserved) {
            ++kept;
        } else {
            b->pins &= ~bit;
        }
    }
}

/* ------------------------------------------------------------- the pages */

uint8_t outbind_to_slots(const outbind_t *b, uint16_t *regs)
{
    if (regs == NULL) {
        return 0u;
    }
    /* Cleared first: a page written from a selection says what every slot is,
     * not only the ones this selection happens to use. */
    outputs_slots_defaults(regs);
    if (b == NULL) {
        return 0u;
    }
    const outbind_proto_t *p = proto_of(b);
    if (p->driver == OUT_DRIVER_NONE) {
        return 0u;
    }

    const outbind_board_t *bd = outbind_board(b->board);
    if (bd == NULL) {
        return 0u;
    }
    uint8_t slot = 0u, channel = 0u;
    for (uint8_t i = 0; i < bd->count && slot < LINK_OUT_SLOTS; ++i) {
        if ((b->pins & ((uint32_t)1u << i)) == 0u) {
            continue;
        }
        if ((unsigned)channel + p->channels > LINK_OUT_CHANNELS) {
            break;                    /* no channels left to render into */
        }
        uint16_t *r = &regs[(size_t)slot * LINK_OS_STRIDE];
        r[LINK_OS_DRIVER]  = (uint16_t)link_driver_of(p->driver);
        r[LINK_OS_PIN]     = bd->pins[i].gpio;
        r[LINK_OS_RANGE]   = LINK_OS_RANGE_OF(channel, p->channels);
        r[LINK_OS_RATE_HZ] = p->rate;
        channel = (uint8_t)(channel + p->channels);
        ++slot;
    }
    return slot;
}

bool outbind_from_slots(outbind_t *b, uint16_t board, const uint16_t *regs)
{
    if (b == NULL || regs == NULL) {
        return false;
    }
    const outbind_board_t *bd = outbind_board(board);
    outbind_init(b);
    b->board = board;
    if (bd == NULL) {
        return false;      /* no catalogue to read the pins against */
    }

    uint8_t proto = 0u;
    uint32_t pins = 0u;
    for (uint8_t slot = 0; slot < LINK_OUT_SLOTS; ++slot) {
        const uint16_t *r = &regs[(size_t)slot * LINK_OS_STRIDE];
        if (r[LINK_OS_DRIVER] == (uint16_t)LINK_DRIVER_NONE) {
            continue;
        }
        /* Which entry describes this slot: driver and rate together, because
         * DShot300 and DShot600 are the same driver. */
        uint8_t found = 0u;
        for (uint8_t i = 1; i < OUTBIND_PROTOS; ++i) {
            if (link_driver_of(k_protos[i].driver) == r[LINK_OS_DRIVER]
                && k_protos[i].rate == r[LINK_OS_RATE_HZ]) {
                found = i;
                break;
            }
        }
        if (found == 0u) {
            outbind_init(b);
            b->board = board;
            return false;          /* a rate or driver no entry offers */
        }
        /* One protocol, not eight slots: a page with two of them is not
         * something this screen can show, and showing one of the two would
         * be showing a bench that is not there. */
        if (proto != 0u && found != proto) {
            outbind_init(b);
            b->board = board;
            return false;
        }
        proto = found;

        /*
         * Checked at its full width before it is narrowed.  The register is
         * sixteen bits and a pin is eight, so 0x0100 cast first becomes GP0
         * and a page naming a pin that does not exist would read back as a
         * binding on the first pin of the header.
         */
        const uint16_t pin = r[LINK_OS_PIN];
        if (pin > OUT_MAX_PIN) {
            outbind_init(b);
            b->board = board;
            return false;
        }
        const uint8_t idx = outbind_index_of(board, (uint8_t)pin);
        if (idx >= bd->count || bd->pins[idx].reserved) {
            /*
             * A pin not on the header, or one that is spoken for.  The page
             * stores what was written and the bank refuses a reserved pin
             * only at apply, so a page can name one while nothing drives it.
             * Reading it back as ticked would show a binding the bank never
             * accepted, and one the screen would not let you tick again.
             */
            outbind_init(b);
            b->board = board;
            return false;
        }
        pins |= (uint32_t)1u << idx;
    }

    b->proto = proto;
    b->pins  = pins;

    /*
     * The whole of "is this a page this screen can show": render the binding
     * back and require it to be the page it came from.
     *
     * Matching on driver and rate alone accepted pages this shape cannot
     * describe -- a PPM slot claiming one channel instead of eight, two PPM
     * slots at once, a first-channel field that does not follow the slot
     * order.  Each would have drawn a binding the operator could not have
     * made and could not reproduce.  Comparing against what this selection
     * would write catches all of them, including the ones not thought of.
     */
    uint16_t check[LINK_OS_COUNT];
    (void)outbind_to_slots(b, check);
    for (unsigned i = 0; i < LINK_OS_COUNT; ++i) {
        if (check[i] != regs[i]) {
            outbind_init(b);
            b->board = board;
            return false;
        }
    }
    return true;
}

void outbind_to_chan_cfg(const outbind_t *b, uint16_t *regs,
                         uint16_t min_us, uint16_t max_us)
{
    if (regs == NULL) {
        return;
    }
    outputs_chan_cfg_defaults(regs);
    if (b == NULL) {
        return;
    }
    const outbind_proto_t *p = proto_of(b);
    if (p->driver == OUT_DRIVER_NONE) {
        return;
    }
    /*
     * A DShot channel is a throttle and a pulse channel is a surface.  The
     * role is not decoration: it decides where the channel goes when it stops
     * being commanded, and a throttle that centres is a motor at half power.
     */
    const uint16_t role = (p->driver == OUT_DRIVER_DSHOT
                           || p->driver == OUT_DRIVER_DSHOT_BIDIR)
                              ? (uint16_t)LINK_CC_ROLE_THROTTLE
                              : (uint16_t)LINK_CC_ROLE_SURFACE;
    const uint8_t used = (uint8_t)(outbind_chosen(b) * p->channels);
    for (uint8_t c = 0; c < used && c < LINK_OUT_CHANNELS; ++c) {
        uint16_t *r = &regs[(size_t)c * LINK_CC_STRIDE];
        r[LINK_CC_ROLE] = role;
        if (min_us >= LINK_CC_FLOOR_US && max_us <= LINK_CC_CEILING_US
            && min_us < max_us) {
            r[LINK_CC_MIN_US] = min_us;
            r[LINK_CC_MAX_US] = max_us;
        }
    }
}
