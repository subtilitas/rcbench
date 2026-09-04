/*
 * Binding a protocol to pins: what the operator chooses, and the OUTPUTS page
 * that comes out of it.
 *
 * One protocol and a set of pins, rather than eight independent slots.  A
 * bench is wired one protocol at a time -- four servo leads, or one ESC
 * (electronic speed controller) -- and asking for the protocol once and then
 * for the pins is the shape of that job.  Each chosen pin becomes one slot on
 * the OUTPUTS page, in pin order, taking channels from zero upward.
 *
 * The pin catalogue is the coprocessor's header, not its silicon.  A GPIO
 * (general-purpose input/output) the module does not bring out to a pad
 * cannot be wired to anything, so it is not offered; GP23 to GP25 are absent
 * for that reason rather than because anything reserves them.
 *
 * Reserved pins are offered but cannot be chosen.  Hiding them would leave an
 * operator hunting for GP10 and finding a gap; showing them greyed says the
 * pin exists and what already has it.
 *
 * Nothing here talks to the wire.  It produces register arrays, and the panel
 * writes them.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_OUT_BIND_H
#define RCBENCH_OUT_BIND_H

#include <stdbool.h>
#include <stdint.h>

#include "outputs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------ the boards */

/*
 * Which coprocessor is on the other end.
 *
 * There is one today and there will be more: a custom board carries its
 * outputs on pins that are soldered, not chosen, and knows a different set of
 * them.  The panel is told which by the identity page rather than assuming,
 * because the two things that must never happen are showing a pin map for a
 * board that is not there and applying a binding made for one board to
 * another.
 *
 * The numbers are a contract, like a page number: nothing is renumbered, and
 * a new board appends.  Zero means nothing answered, or answered with an
 * identity this build has never heard of -- and both are the same thing to a
 * panel: it does not know this board, so it offers nothing.
 */
typedef enum {
    OUTBIND_BOARD_UNKNOWN = 0,
    OUTBIND_BOARD_PICO_HEADER = 1,  /**< Pico form factor, wired by hand    */
} outbind_board_id_t;

/** The most pins any board in this build brings out. */
#define OUTBIND_PINS  26u

typedef struct {
    uint8_t     gpio;
    uint8_t     pad;        /**< the number printed on the board            */
    bool        reserved;   /**< the coprocessor refuses it                 */
    const char *held_by;    /**< what has it, or NULL when it is free       */
} outbind_pin_t;

typedef struct {
    uint16_t             id;      /**< as the identity page reports it      */
    const char          *name;
    const outbind_pin_t *pins;
    uint8_t              count;
    /**
     * The outputs are soldered, not chosen.
     *
     * The coprocessor configures itself and the panel shows what it reads
     * back without offering to change it.  A screen that let an operator
     * re-bind pins that are not on connectors would be offering a choice the
     * board cannot honour.
     */
    bool                 fixed;
} outbind_board_t;

/** The board with this identity, or NULL when this build does not know it. */
const outbind_board_t *outbind_board(uint16_t id);

/** The table itself, so a caller can walk every board rather than guess at a
 *  range of identities.  outbind_board_at() returns NULL past the end. */
uint8_t outbind_board_count(void);
const outbind_board_t *outbind_board_at(uint8_t index);

/**
 * Whether @p index may be added to a selection on @p board.
 *
 * Takes the board rather than reading it from the table, so the rule can be
 * held against a board this build does not yet contain -- a soldered one, for
 * instance, which refuses every pin because there is nothing there to choose.
 */
bool outbind_pin_selectable(const outbind_board_t *board, uint8_t index);

/** Its catalogue and how long it is.  NULL and 0 for a board this build does
 *  not know, so a caller that forgets to check offers nothing rather than
 *  offering the wrong thing. */
const outbind_pin_t *outbind_pins(uint16_t board);
uint8_t outbind_pin_count(uint16_t board);

/** Index of @p gpio in that board's catalogue, or its count when the board
 *  does not bring that pin out. */
uint8_t outbind_index_of(uint16_t board, uint8_t gpio);

/**
 * Every reserved pin of a board, one bit per GPIO.
 *
 * The coprocessor hands its own board's mask to outputs_reserve_pins() and
 * the panel greys the same pins, so the two ends cannot disagree about which
 * pins exist to be given away.  The coprocessor adds the pins its part does
 * not have.  An unknown board reserves everything: a build that cannot say
 * which pins are safe must not hand any of them out.
 */
uint64_t outbind_reserved_mask(uint16_t board);

/* ------------------------------------------------------------- protocols */

/**
 * What can be bound, as an operator names it.
 *
 * Rate is not offered separately.  DShot600 and DShot300 are different
 * entries because they are different things to choose between, not one thing
 * with a number attached; the same goes for the bidirectional pair.
 */
typedef struct {
    const char *name;
    out_driver_t driver;
    uint16_t     rate;      /**< Hz for a pulse driver, kbit/s for DShot    */
    uint8_t      max_pins;  /**< PPM is one pin by definition               */
    uint8_t      channels;  /**< channels one pin of it renders             */
} outbind_proto_t;

#define OUTBIND_PROTOS 7u

const outbind_proto_t *outbind_protos(void);

/* --------------------------------------------------------- the selection */

typedef struct {
    uint16_t board;         /**< which catalogue `pins` indexes             */
    uint8_t  proto;         /**< index into outbind_protos()                */
    uint32_t pins;          /**< one bit per catalogue index of that board  */
} outbind_t;

/** Empty, and belonging to no board: nothing can be chosen until one is set. */
void outbind_init(outbind_t *b);

/**
 * Say which board this binding is for.
 *
 * Clears the selection whenever the board changes, and that is the point: a
 * bit in `pins` is an index into one board's catalogue and means a different
 * pin -- or no pin -- in another's.  Carrying a selection across would bind
 * whatever happened to sit at that index on the board now in front of the
 * operator.
 */
void outbind_set_board(outbind_t *b, uint16_t board);

/** How many pins are chosen. */
uint8_t outbind_chosen(const outbind_t *b);

/**
 * Add or remove a pin.
 *
 * Refuses a reserved pin, an index off the end, and one more pin than the
 * protocol can take -- PPM's second pin is not a second output, it is a
 * mistake.  Removing always succeeds, because an operator must always be
 * able to undo what they just did.
 */
bool outbind_toggle(outbind_t *b, uint8_t index);

/**
 * Change protocol, dropping whatever no longer fits.
 *
 * Switching to PPM with four pins chosen keeps the first and drops the rest,
 * rather than refusing the switch: the protocol is what was asked for, and
 * the pins are the part that has to give.
 */
void outbind_set_proto(outbind_t *b, uint8_t proto);

/** True when this pin may still be added, for a screen drawing it. */
bool outbind_can_add(const outbind_t *b, uint8_t index);

/* ------------------------------------------------------------- the pages */

/**
 * Render the selection into the OUTPUTS page.
 *
 * @p regs is LINK_OS_COUNT registers, cleared and rebuilt: every slot the
 * selection does not use is set to no driver, so a page written from this is
 * the whole truth rather than a change to what was there.
 *
 * Slots are filled in pin order and take channels from zero upward, so the
 * lowest chosen pin is channel 0.  Returns how many slots were used.
 */
uint8_t outbind_to_slots(const outbind_t *b, uint16_t *regs);

/**
 * Read a selection back out of an OUTPUTS page.
 *
 * The coprocessor holds the configuration, so the panel asks it what is
 * driving rather than remembering: a binding is a description of wiring, and
 * a panel that reapplied a remembered one would be applying it to whatever is
 * on the bench now.
 *
 * Returns false for a page this shape cannot describe -- slots on different
 * protocols, a rate no entry uses, a pin not on the header -- and leaves @p b
 * cleared.  The screen then shows nothing chosen rather than a selection that
 * disagrees with the page it came from.
 */
bool outbind_from_slots(outbind_t *b, uint16_t board,
                        const uint16_t *regs);

/**
 * Render the channel configuration to match.
 *
 * A throttle protocol makes its channels throttles and a pulse protocol
 * leaves them surfaces, because that is what decides where a channel rests
 * when it stops being commanded -- stopped, or centred.  Channels the
 * selection does not use keep the schema's defaults.
 */
void outbind_to_chan_cfg(const outbind_t *b, uint16_t *regs,
                         uint16_t min_us, uint16_t max_us);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_OUT_BIND_H */
