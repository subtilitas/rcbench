/*
 * Binding a protocol to pins: what the operator chooses, and the OUTPUTS page
 * that comes out of it.
 *
 * A set of pins for each protocol, rather than eight independent slots.  A
 * bench is wired a protocol at a time -- four servo leads, then one ESC
 * (electronic speed controller) -- and asking for the protocol and then for
 * its pins is the shape of that job.  The sets stand together: a pin belongs
 * to at most one of them, and choosing a protocol says which set is being
 * edited rather than retargeting the pins already chosen for another.
 *
 * Each chosen pin becomes one slot on the OUTPUTS page, in pin order across
 * every protocol, taking channels from zero upward.  So the lowest chosen pin
 * is channel 0 whichever protocol holds it, and a bench wired to one protocol
 * writes the page it always wrote.
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

/**
 * The board with this identity, or NULL when nothing here describes it.
 *
 * The table compiled into this build first, then a board learned over the
 * link.  A build that ships a catalogue for a board uses its own: the
 * compiled one has been read by somebody, names the exact signal holding
 * each reserved pin, and cannot change under a running bench.
 */
const outbind_board_t *outbind_board(uint16_t id);

/* ------------------------------------------------ a board that describes itself */

/**
 * Render a board's catalogue into LINK_CAT_COUNT registers for the wire.
 *
 * Slots past the board's own pins are left with a pad number of zero, which
 * is how the page says there is no pin there.
 */
void outbind_board_to_regs(const outbind_board_t *board, uint16_t *regs);

/**
 * Learn the board a catalogue page describes, replacing any previous one.
 *
 * There is room for exactly one: the panel talks to one coprocessor, and a
 * second learned board would be a pin map for hardware that is not on the
 * bench.  Refuses a page that cannot be a board -- no pins, a GPIO past
 * OUT_MAX_PIN, pins out of GPIO order, or the same GPIO twice -- and learns
 * nothing rather than half of it, because a catalogue half read is a pin map
 * that disagrees with the board sending it.
 *
 * Refuses an identity the compiled table already has: that board is
 * described here already, and letting the wire replace it would let a
 * coprocessor rename the pin holding the safety line.
 */
bool outbind_learn_board(uint16_t id, const uint16_t *regs);

/**
 * Forget it.
 *
 * Not needed when the link drops.  The slot is keyed by identity and is
 * returned for that identity alone, so a board that has been unplugged
 * cannot be reached by the one that replaces it -- and keeping it means the
 * screen still knows the pins of a board whose link merely blinked, which is
 * what a compiled-in catalogue already gives a board this build knows.
 */
void outbind_forget_learned(void);

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
    uint16_t board;         /**< which catalogue every `pins` set indexes   */
    uint8_t  proto;         /**< the set being edited: outbind_protos()     */
    /**
     * One pin set per protocol, a bit per catalogue index of that board.
     *
     * `pins[0]` is OFF and is always empty -- OFF is how a protocol says it
     * holds nothing, so a bit there would be a pin bound to no driver.
     */
    uint32_t pins[OUTBIND_PROTOS];
} outbind_t;

/** Empty, and belonging to no board: nothing can be chosen until one is set. */
void outbind_init(outbind_t *b);

/**
 * Which protocol holds @p index, or 0 (OFF) when nothing does.
 *
 * A pin refused because another protocol holds it is a different fact from
 * one the coprocessor reserves: the first is a choice the operator can undo
 * by going to that protocol, the second is the wiring.  A screen that drew
 * them alike would offer to free the heartbeat line.
 */
uint8_t outbind_group_of(const outbind_t *b, uint8_t index);

/** Pins chosen across every protocol, and the channels they render.  Both
 *  are budgets shared by all of them: LINK_OUT_SLOTS and LINK_OUT_CHANNELS. */
uint8_t outbind_chosen_total(const outbind_t *b);
uint8_t outbind_channels_used(const outbind_t *b);

/**
 * Drop anything a selection cannot mean on its board.
 *
 * Bits above the catalogue's width, and pins past what a protocol takes.
 * `outbind_t` is a plain struct and one arrives from outside -- read off the
 * wire, or restored -- so it is trimmed before it is shown rather than
 * trusted.
 */
void outbind_trim(outbind_t *b);

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

/** How many pins the protocol being edited has chosen. */
uint8_t outbind_chosen(const outbind_t *b);

/**
 * Add or remove a pin, in the protocol being edited.
 *
 * Refuses a reserved pin, an index off the end, one more pin than the
 * protocol can take -- PPM's second pin is not a second output, it is a
 * mistake -- and a pin another protocol holds, which is that protocol's to
 * give up.  Removing a pin this protocol holds always succeeds, because an
 * operator must always be able to undo what they just did.
 */
bool outbind_toggle(outbind_t *b, uint8_t index);

/**
 * Choose which protocol is being edited.
 *
 * Nothing is dropped.  The pins chosen for the protocol being left stay
 * bound and appear as held on the grid, and the pins of the one arrived at
 * come back as they were: a selector that retargeted the current pins would
 * make binding a second protocol mean unbinding the first.
 *
 * The whole selection is trimmed here, since this is every screen's way in
 * to a binding that came from outside.
 */
void outbind_set_proto(outbind_t *b, uint8_t proto);

/** True when this pin may still be added to the protocol being edited, for a
 *  screen drawing it. */
bool outbind_can_add(const outbind_t *b, uint8_t index);

/* ------------------------------------------------------------- the pages */

/**
 * Render the selection into the OUTPUTS page.
 *
 * @p regs is LINK_OS_COUNT registers, cleared and rebuilt: every slot the
 * selection does not use is set to no driver, so a page written from this is
 * the whole truth rather than a change to what was there.
 *
 * Slots are filled in pin order across every protocol and take channels from
 * zero upward, so the lowest chosen pin is channel 0 whichever protocol holds
 * it.  Filling stops at LINK_OUT_SLOTS slots or LINK_OUT_CHANNELS channels,
 * whichever comes first.  Returns how many slots were used.
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
 * Slots on different protocols are the ordinary case; slots on the same
 * driver and rate are one protocol's set, because that pair is what names a
 * set.  The protocol left being edited is the lowest-numbered one the page
 * uses, so a page with anything on it opens on something.
 *
 * Returns false for a page this shape cannot describe -- a rate no entry
 * uses, a pin not on the header, a channel run that does not follow the slot
 * order -- and leaves @p b cleared.  The screen then shows nothing chosen
 * rather than a selection that disagrees with the page it came from.
 */
bool outbind_from_slots(outbind_t *b, uint16_t board,
                        const uint16_t *regs);

/**
 * Render the channel configuration to match.
 *
 * A throttle protocol makes its channels throttles and a pulse protocol
 * leaves them surfaces, because that is what decides where a channel rests
 * when it stops being commanded -- stopped, or centred.  With more than one
 * protocol bound the roles are mixed, and each channel takes the role of the
 * protocol whose pin renders it.  Channels the selection does not use keep
 * the schema's defaults.
 */
void outbind_to_chan_cfg(const outbind_t *b, uint16_t *regs,
                         uint16_t min_us, uint16_t max_us);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_OUT_BIND_H */
