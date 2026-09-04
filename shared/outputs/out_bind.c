/*
 * Binding a protocol to pins.  See out_bind.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_bind.h"

#include <stddef.h>
#include <string.h>

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
/*
 * The Pico form factor: 51.00 x 21.00 mm, forty pads on 2.54 mm, twenty a
 * side, pad 1 at the bottom left with the numbers running away from it.
 *
 * The outline's own numbers.  The picker's photograph needs a different
 * inset because a photograph is not cropped to the outline, and it carries
 * that calibration itself.
 */
static const outbind_shape_t k_pico_shape = {
    5100u, 2100u, 254u, 161u, 20u, (uint8_t)LINK_SH_BOTTOM_LEFT,
};

static const outbind_board_t k_boards[] = {
    { OUTBIND_BOARD_PICO_HEADER, "RP2350-CAN", k_pico_pins, OUTBIND_PINS,
      false, &k_pico_shape },
};

/*
 * One board learned over the link, for a coprocessor this build ships no
 * catalogue for.  The name is formatted rather than carried: a name is a
 * string and the page is registers, and the identity is what the operator
 * needs to match against the board in front of them.
 */
static outbind_pin_t   s_learned_pins[LINK_CAT_PINS];
static outbind_shape_t s_learned_shape;
static bool            s_have_learned_shape;
static char            s_learned_name[16];
static outbind_board_t s_learned;
static bool            s_have_learned;

const outbind_board_t *outbind_board(uint16_t id)
{
    for (unsigned i = 0; i < sizeof(k_boards) / sizeof(k_boards[0]); ++i) {
        if (k_boards[i].id == id) {
            return &k_boards[i];
        }
    }
    /* Only after the table: a board this build describes uses its own
     * description, whatever the wire says about it. */
    if (s_have_learned && id != (uint16_t)OUTBIND_BOARD_UNKNOWN
        && s_learned.id == id) {
        return &s_learned;
    }
    return NULL;                 /* including OUTBIND_BOARD_UNKNOWN */
}

/* The panel's words for what the wire can only number. */
static const char *hold_name(uint8_t hold)
{
    switch (hold) {
    case LINK_PIN_HEARTBEAT: return "heartbeat";
    case LINK_PIN_CAN:       return "CAN";
    case LINK_PIN_FLASH:     return "flash";
    case LINK_PIN_DEBUG:     return "debug";
    case LINK_PIN_SENSOR:    return "sensor";
    default:                 return "in use";
    }
}

void outbind_board_to_regs(const outbind_board_t *board, uint16_t *regs)
{
    if (regs == NULL) {
        return;
    }
    /* Cleared first, so a slot past the board's pins carries pad 0 and says
     * there is no pin there. */
    for (unsigned i = 0; i < LINK_CAT_COUNT; ++i) {
        regs[i] = 0u;
    }
    if (board == NULL) {
        return;
    }
    for (uint8_t i = 0; i < board->count && i < LINK_CAT_PINS; ++i) {
        /*
         * The group, not the signal.  "CAN CS" and "CAN MOSI" are both the
         * link to the panel as far as an operator choosing an output pin is
         * concerned, and the wire has four bits.
         */
        uint8_t hold = LINK_PIN_FREE;
        if (board->pins[i].reserved) {
            hold = LINK_PIN_OTHER;
            const char *why = board->pins[i].held_by;
            if (why != NULL) {
                if (strstr(why, "heart") != NULL) { hold = LINK_PIN_HEARTBEAT; }
                else if (strstr(why, "CAN") != NULL) { hold = LINK_PIN_CAN; }
            }
        }
        regs[i] = LINK_CAT_OF(board->pins[i].gpio, board->pins[i].pad, hold);
    }
}

bool outbind_learn_board(uint16_t id, const uint16_t *regs)
{
    if (regs == NULL || id == (uint16_t)OUTBIND_BOARD_UNKNOWN) {
        return false;
    }
    /* A board the table describes is not the wire's to redescribe. */
    for (unsigned i = 0; i < sizeof(k_boards) / sizeof(k_boards[0]); ++i) {
        if (k_boards[i].id == id) {
            return false;
        }
    }

    /*
     * Read twice: once to decide, once to keep.
     *
     * The pins live in a static slot, so writing as it parsed would leave a
     * board that failed half way over the one already learned -- and the
     * failure returns false while outbind_board() goes on answering with a
     * count from the old board and pins from two.  Nothing is written until
     * the whole page is known to be a board.
     */
    uint8_t n = 0u;
    int16_t last = -1;
    for (unsigned i = 0; i < LINK_CAT_COUNT; ++i) {
        const uint8_t pad = LINK_CAT_PAD(regs[i]);
        if (pad == 0u) {
            break;              /* no pin in this slot, and none after it */
        }
        /*
         * No range check on the GPIO: the field is six bits and OUT_MAX_PIN
         * is 63, so the page cannot name a pin that is not one.  A guard
         * here would be a branch no page can take.
         */
        const uint8_t gpio = LINK_CAT_GPIO(regs[i]);
        if ((int16_t)gpio <= last) {
            return false;       /* out of order, or the same pin twice */
        }
        last = (int16_t)gpio;
        ++n;
    }
    if (n == 0u) {
        return false;           /* a board that brings out nothing is not one */
    }

    /* Decided.  From here nothing can refuse it, so the slot is safe to
     * overwrite. */
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t hold = LINK_CAT_HOLD(regs[i]);
        s_learned_pins[i].gpio     = LINK_CAT_GPIO(regs[i]);
        s_learned_pins[i].pad      = LINK_CAT_PAD(regs[i]);
        s_learned_pins[i].reserved = (hold != LINK_PIN_FREE);
        s_learned_pins[i].held_by  = (hold != LINK_PIN_FREE)
                                         ? hold_name(hold) : NULL;
    }

    /*
     * Formatted by hand rather than with snprintf().  out_bind.c is linked
     * into the coprocessor too, and it has no other reason to pull in the
     * printf machinery for a name only the panel ever draws.
     */
    {
        static const char k_pre[] = "BOARD ";
        unsigned at = sizeof(k_pre) - 1u;
        for (unsigned i = 0; i < at; ++i) { s_learned_name[i] = k_pre[i]; }
        char digits[6];
        unsigned d = 0u, v = id;
        do { digits[d++] = (char)('0' + (v % 10u)); v /= 10u; } while (v != 0u);
        while (d > 0u) { s_learned_name[at++] = digits[--d]; }
        s_learned_name[at] = '\0';
    }
    s_learned.id    = id;
    s_learned.name  = s_learned_name;
    s_learned.pins  = s_learned_pins;
    s_learned.count = n;
    /*
     * Never soldered.  `fixed` says the outputs are wired and the screen may
     * only show them, and nothing on the wire says that yet; assuming it
     * would make a board that describes itself unusable rather than usable.
     */
    s_learned.fixed = false;
    /*
     * A shape belongs to the board it came with.  Learning a board drops it,
     * so a new coprocessor is not drawn with the last one's outline while
     * its own shape page has yet to be read.
     */
    s_have_learned_shape = false;
    s_learned.shape      = NULL;
    s_have_learned       = true;
    return true;
}

void outbind_shape_to_regs(const outbind_board_t *board, uint16_t *regs)
{
    if (regs == NULL) {
        return;
    }
    for (unsigned i = 0; i < LINK_SH_COUNT; ++i) {
        regs[i] = 0u;
    }
    if (board == NULL || board->shape == NULL) {
        return;             /* a board with no shape says so with zeroes */
    }
    const outbind_shape_t *sh = board->shape;
    regs[LINK_SH_WIDTH_CMM]  = sh->width_cmm;
    regs[LINK_SH_HEIGHT_CMM] = sh->height_cmm;
    regs[LINK_SH_LAYOUT]     = LINK_SH_LAYOUT_OF(sh->corner, sh->per_side);
    regs[LINK_SH_PITCH_CMM]  = sh->pitch_cmm;
    regs[LINK_SH_INSET_CMM]  = sh->inset_cmm;
}

bool outbind_learn_shape(uint16_t id, const uint16_t *regs)
{
    if (regs == NULL || !s_have_learned || s_learned.id != id) {
        return false;       /* a shape for a board that is not the one here */
    }
    outbind_shape_t sh;
    sh.width_cmm  = regs[LINK_SH_WIDTH_CMM];
    sh.height_cmm = regs[LINK_SH_HEIGHT_CMM];
    sh.pitch_cmm  = regs[LINK_SH_PITCH_CMM];
    sh.inset_cmm  = regs[LINK_SH_INSET_CMM];
    sh.per_side   = LINK_SH_PER_SIDE(regs[LINK_SH_LAYOUT]);
    sh.corner     = LINK_SH_CORNER(regs[LINK_SH_LAYOUT]);

    if (sh.per_side == 0u || sh.pitch_cmm == 0u
        || sh.corner > (uint8_t)LINK_SH_TOP_RIGHT) {
        return false;
    }
    /* The row has to fit the outline it claims, and the two rows have to fit
     * across it. */
    if ((uint32_t)(sh.per_side - 1u) * sh.pitch_cmm > sh.width_cmm
        || (uint32_t)sh.inset_cmm * 2u >= sh.height_cmm) {
        return false;
    }
    /*
     * And it has to place every pad the catalogue named.  The two pages
     * describe one board; a pad with nowhere to sit would be drawn off the
     * outline, which is the failure a drawn board exists to avoid.
     */
    const unsigned pads = (unsigned)sh.per_side * 2u;
    for (uint8_t i = 0; i < s_learned.count; ++i) {
        if (s_learned_pins[i].pad == 0u || s_learned_pins[i].pad > pads) {
            return false;
        }
    }

    s_learned_shape      = sh;
    s_learned.shape      = &s_learned_shape;
    s_have_learned_shape = true;
    return true;
}

bool outbind_pad_xy(const outbind_shape_t *shape, uint8_t pad,
                    uint16_t *x_cmm, uint16_t *y_cmm)
{
    if (shape == NULL || x_cmm == NULL || y_cmm == NULL
        || shape->per_side == 0u || pad == 0u
        || pad > (unsigned)shape->per_side * 2u) {
        return false;
    }
    /*
     * The row pad 1 is in, then the other one coming back: pads run away
     * from their corner along one edge and return along the opposite edge,
     * so the second row's numbers ascend in the direction the first row's
     * descend.
     */
    const uint8_t idx = (uint8_t)(pad - 1u);
    const bool    far = (idx >= shape->per_side);
    const uint8_t in_row = far ? (uint8_t)(idx - shape->per_side) : idx;
    const uint8_t along  = far ? (uint8_t)(shape->per_side - 1u - in_row)
                               : in_row;

    /* Centred, so the margin either end of a row is the same. */
    const uint32_t span = (uint32_t)(shape->per_side - 1u) * shape->pitch_cmm;
    const uint32_t lead = ((uint32_t)shape->width_cmm - span) / 2u;
    uint32_t x = lead + (uint32_t)along * shape->pitch_cmm;

    /* Pad 1's own row sits at its corner's edge; the other row opposite. */
    const bool starts_top = (shape->corner == (uint8_t)LINK_SH_TOP_LEFT
                             || shape->corner == (uint8_t)LINK_SH_TOP_RIGHT);
    const bool on_top = far ? !starts_top : starts_top;
    uint32_t y = on_top ? shape->inset_cmm
                        : ((uint32_t)shape->height_cmm - shape->inset_cmm);

    /* A corner on the right numbers the other way along the row. */
    if (shape->corner == (uint8_t)LINK_SH_BOTTOM_RIGHT
        || shape->corner == (uint8_t)LINK_SH_TOP_RIGHT) {
        x = (uint32_t)shape->width_cmm - x;
    }

    *x_cmm = (uint16_t)x;
    *y_cmm = (uint16_t)y;
    return true;
}

void outbind_forget_learned(void)
{
    s_have_learned       = false;
    s_have_learned_shape = false;
    s_learned.shape      = NULL;
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

/* --------------------------------------------------------- the selection */

void outbind_init(outbind_t *b)
{
    if (b == NULL) {
        return;
    }
    b->board = (uint16_t)OUTBIND_BOARD_UNKNOWN;
    b->proto = 0u;      /* OFF: nothing drives until somebody says so */
    for (uint8_t g = 0; g < OUTBIND_PROTOS; ++g) {
        b->pins[g] = 0u;
    }
}

void outbind_set_board(outbind_t *b, uint16_t board)
{
    if (b == NULL || b->board == board) {
        return;
    }
    b->board = board;
    /* A pin index belongs to one catalogue.  Keeping the selection across a
     * change of board would bind whatever sits at that index on the board
     * that is actually connected -- and that is true of every protocol's
     * set, not only the one being edited. */
    for (uint8_t g = 0; g < OUTBIND_PROTOS; ++g) {
        b->pins[g] = 0u;
    }
    b->proto = 0u;
}

/* How many bits are set in one protocol's pin set. */
static uint8_t count_pins(uint32_t pins, uint8_t width)
{
    uint8_t n = 0u;
    for (uint8_t i = 0; i < width; ++i) {
        if ((pins & ((uint32_t)1u << i)) != 0u) {
            ++n;
        }
    }
    return n;
}

/* What a protocol may take: its own cap, and never more slots than the page
 * has. */
static uint8_t cap_of(const outbind_proto_t *p)
{
    return (p->max_pins < OUT_MAX_SLOTS) ? p->max_pins : (uint8_t)OUT_MAX_SLOTS;
}

uint8_t outbind_chosen(const outbind_t *b)
{
    if (b == NULL) {
        return 0u;
    }
    const uint8_t proto = (b->proto < OUTBIND_PROTOS) ? b->proto : 0u;
    return count_pins(b->pins[proto], outbind_pin_count(b->board));
}

uint8_t outbind_chosen_total(const outbind_t *b)
{
    if (b == NULL) {
        return 0u;
    }
    const uint8_t cnt = outbind_pin_count(b->board);
    uint8_t n = 0u;
    for (uint8_t g = 1; g < OUTBIND_PROTOS; ++g) {
        n = (uint8_t)(n + count_pins(b->pins[g], cnt));
    }
    return n;
}

uint8_t outbind_channels_used(const outbind_t *b)
{
    if (b == NULL) {
        return 0u;
    }
    const uint8_t cnt = outbind_pin_count(b->board);
    unsigned n = 0u;
    for (uint8_t g = 1; g < OUTBIND_PROTOS; ++g) {
        n += (unsigned)count_pins(b->pins[g], cnt) * k_protos[g].channels;
    }
    return (uint8_t)((n > 0xFFu) ? 0xFFu : n);
}

uint8_t outbind_group_of(const outbind_t *b, uint8_t index)
{
    if (b == NULL || index >= outbind_pin_count(b->board)) {
        return 0u;
    }
    const uint32_t bit = (uint32_t)1u << index;
    for (uint8_t g = 1; g < OUTBIND_PROTOS; ++g) {
        if ((b->pins[g] & bit) != 0u) {
            return g;
        }
    }
    return 0u;
}

void outbind_trim(outbind_t *b)
{
    if (b == NULL) {
        return;
    }
    const outbind_board_t *bd = outbind_board(b->board);
    if (b->proto >= OUTBIND_PROTOS) {
        b->proto = 0u;
    }
    b->pins[0] = 0u;         /* OFF holds nothing, whatever arrived saying so */
    if (bd == NULL) {
        /* No catalogue to read the bits against, so none of them mean a pin. */
        for (uint8_t g = 0; g < OUTBIND_PROTOS; ++g) {
            b->pins[g] = 0u;
        }
        return;
    }
    /*
     * Anything above the board's own width first.  The walk below only covers
     * that width, so a stray high bit would survive every trim and leave
     * outbind_chosen() and the mask disagreeing about what is selected.
     */
    const uint32_t mask = board_mask(bd);
    uint32_t taken = 0u;
    for (uint8_t g = 1; g < OUTBIND_PROTOS; ++g) {
        b->pins[g] &= mask;
        const uint8_t cap = cap_of(&k_protos[g]);
        uint8_t kept = 0u;
        for (uint8_t i = 0; i < bd->count; ++i) {
            const uint32_t bit = (uint32_t)1u << i;
            if ((b->pins[g] & bit) == 0u) {
                continue;
            }
            /*
             * One pin, one protocol.  A pin claimed twice is a page or a
             * restored struct that cannot be rendered, and the lower-numbered
             * protocol keeps it so the result does not depend on walk order.
             */
            if (kept < cap && !bd->pins[i].reserved && (taken & bit) == 0u) {
                ++kept;
                taken |= bit;
            } else {
                b->pins[g] &= ~bit;
            }
        }
    }

    /*
     * Then the page's own budgets, walked exactly as outbind_to_slots()
     * fills it -- pin order across every protocol, stopping at the first pin
     * there is no room for.  A pin left ticked past that point is one the
     * screen offers and the page never carries, which is the failure the far
     * end cannot report: nothing refuses it, it simply does not drive.
     */
    uint8_t slot = 0u, channel = 0u;
    bool room = true;
    for (uint8_t i = 0; i < bd->count; ++i) {
        const uint32_t bit = (uint32_t)1u << i;
        uint8_t g = 0u;
        for (uint8_t k = 1; k < OUTBIND_PROTOS; ++k) {
            if ((b->pins[k] & bit) != 0u) {
                g = k;
                break;
            }
        }
        if (g == 0u) {
            continue;
        }
        const uint8_t ch = k_protos[g].channels;
        if (room && slot < LINK_OUT_SLOTS
            && (unsigned)channel + ch <= LINK_OUT_CHANNELS) {
            channel = (uint8_t)(channel + ch);
            ++slot;
        } else {
            /* to_slots() stops at this pin rather than skipping it, so
             * nothing beyond it is written either. */
            room = false;
            b->pins[g] &= ~bit;
        }
    }
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
    const uint8_t proto = (b->proto < OUTBIND_PROTOS) ? b->proto : 0u;
    const outbind_proto_t *p = &k_protos[proto];
    if (p->max_pins == 0u) {
        return false;                 /* OFF takes no pins */
    }
    /* A pin another protocol holds is that protocol's to give up. */
    const uint8_t held = outbind_group_of(b, index);
    if (held != 0u && held != proto) {
        return false;
    }
    if (outbind_chosen(b) >= cap_of(p)) {
        return false;
    }
    /*
     * The page's own budgets, shared by every protocol: eight slots and eight
     * channels.  PPM renders eight channels on its one pin, so it fills the
     * channel budget by itself and nothing else fits beside it.
     */
    if (outbind_chosen_total(b) >= LINK_OUT_SLOTS) {
        return false;
    }
    return (unsigned)outbind_channels_used(b) + p->channels
           <= LINK_OUT_CHANNELS;
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
    const uint8_t proto = (b->proto < OUTBIND_PROTOS) ? b->proto : 0u;
    const uint32_t bit = (uint32_t)1u << index;
    if ((b->pins[proto] & bit) != 0u) {
        b->pins[proto] &= ~bit;       /* undoing is allowed; adding may not be */
        return true;
    }
    if (!outbind_can_add(b, index)) {
        return false;
    }
    b->pins[proto] |= bit;
    return true;
}

void outbind_set_proto(outbind_t *b, uint8_t proto)
{
    if (b == NULL || proto >= OUTBIND_PROTOS) {
        return;
    }
    /*
     * Switching says which set is being edited.  The pins of the protocol
     * being left stay bound: binding a second protocol is not a way of
     * unbinding the first, and an operator who has wired four servos and now
     * wants an ESC has not changed their mind about the servos.
     */
    b->proto = proto;
    outbind_trim(b);
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
    const outbind_board_t *bd = outbind_board(b->board);
    if (bd == NULL) {
        return 0u;
    }
    /*
     * Pin order across every protocol, not one protocol's pins and then the
     * next.  A slot's channels follow the pin it drives, so the operator
     * reads the grid top to bottom and the channels count up with it however
     * the protocols are mixed.
     */
    uint8_t slot = 0u, channel = 0u;
    for (uint8_t i = 0; i < bd->count && slot < LINK_OUT_SLOTS; ++i) {
        const uint8_t g = outbind_group_of(b, i);
        if (g == 0u) {
            continue;
        }
        const outbind_proto_t *p = &k_protos[g];
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

    for (uint8_t slot = 0; slot < LINK_OUT_SLOTS; ++slot) {
        const uint16_t *r = &regs[(size_t)slot * LINK_OS_STRIDE];
        if (r[LINK_OS_DRIVER] == (uint16_t)LINK_DRIVER_NONE) {
            continue;
        }
        /* Which entry describes this slot: driver and rate together, because
         * DShot300 and DShot600 are the same driver.  That pair is also what
         * names a set, so two slots matching the same entry are two pins of
         * one protocol rather than two protocols that happen to agree. */
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
        b->pins[found] |= (uint32_t)1u << idx;
    }

    /*
     * Opened on the lowest-numbered protocol the page uses.  A page with
     * anything on it opens on something the operator can see the pins of,
     * and the choice does not depend on which slot happened to be first.
     */
    b->proto = 0u;
    for (uint8_t g = 1; g < OUTBIND_PROTOS; ++g) {
        if (b->pins[g] != 0u) {
            b->proto = g;
            break;
        }
    }

    /*
     * The whole of "is this a page this screen can show": render the binding
     * back and require it to be the page it came from.
     *
     * Matching on driver and rate alone accepts pages this shape cannot
     * describe -- a PPM slot claiming one channel instead of eight, a
     * first-channel field that does not follow the slot order, a pin claimed
     * by two slots, more channels than the page has.  Each would draw a
     * binding the operator could not have made and could not reproduce.
     * Comparing against what this selection would write catches all of them,
     * including the ones not thought of.
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
    const outbind_board_t *bd = outbind_board(b->board);
    if (bd == NULL) {
        return;
    }
    /*
     * The same walk outbind_to_slots() makes, so a channel takes the role of
     * the protocol whose pin renders it.  A DShot channel is a throttle and a
     * pulse channel is a surface.  The role is not decoration: it decides
     * where the channel goes when it stops being commanded, and a throttle
     * that centres is a motor at half power.
     */
    uint8_t slot = 0u, channel = 0u;
    for (uint8_t i = 0; i < bd->count && slot < LINK_OUT_SLOTS; ++i) {
        const uint8_t g = outbind_group_of(b, i);
        if (g == 0u) {
            continue;
        }
        const outbind_proto_t *p = &k_protos[g];
        if ((unsigned)channel + p->channels > LINK_OUT_CHANNELS) {
            break;
        }
        const uint16_t role = (p->driver == OUT_DRIVER_DSHOT
                               || p->driver == OUT_DRIVER_DSHOT_BIDIR)
                                  ? (uint16_t)LINK_CC_ROLE_THROTTLE
                                  : (uint16_t)LINK_CC_ROLE_SURFACE;
        for (uint8_t c = channel; c < channel + p->channels; ++c) {
            uint16_t *r = &regs[(size_t)c * LINK_CC_STRIDE];
            r[LINK_CC_ROLE] = role;
            if (min_us >= LINK_CC_FLOOR_US && max_us <= LINK_CC_CEILING_US
                && min_us < max_us) {
                r[LINK_CC_MIN_US] = min_us;
                r[LINK_CC_MAX_US] = max_us;
            }
        }
        channel = (uint8_t)(channel + p->channels);
        ++slot;
    }
}
