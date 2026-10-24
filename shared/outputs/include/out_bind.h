/**
 * @file
 * @brief Binding a protocol to pins: what the operator chooses, and the OUTPUTS
 *        page that comes out of it.
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
 * The pin catalogue is the IOmcu's header, not its silicon.  A GPIO
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

/**
 * @brief Which IOmcu is on the other end.
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
    OUTBIND_BOARD_UNKNOWN = 0,  /**< nothing answered, or unknown identity */
    OUTBIND_BOARD_PICO_HEADER = 1,  /**< Pico form factor, wired by hand    */
} outbind_board_id_t;

/** @brief The most pins any board in this build brings out. */
#define OUTBIND_PINS  26u

/**
 * @brief One pin of a board's catalogue.
 */
typedef struct {
    uint8_t     gpio;       /**< GPIO number                                */
    uint8_t     pad;        /**< the number printed on the board            */
    bool        reserved;   /**< the IOmcu refuses it                 */
    const char *held_by;    /**< what has it, or NULL when it is free       */
} outbind_pin_t;

/**
 * @brief Where a board's pads are: two rows on one pitch, which is every
 *        0.1-inch header board.
 *
 * Hundredths of a millimetre throughout.
 *
 * Only a picture of the board needs this.  Which pins exist and what holds
 * them is the catalogue's job, and the outputs grid asks nothing else.
 */
typedef struct {
    uint16_t width_cmm;   /**< outline, along the pad rows                 */
    uint16_t height_cmm;  /**< outline, across them                        */
    uint16_t pitch_cmm;   /**< centre to centre, 254 for 0.1 inch          */
    uint16_t inset_cmm;   /**< edge to the centre of a pad row             */
    uint8_t  per_side;    /**< pads in one row                             */
    uint8_t  corner;      /**< a link_shape_corner_t: where pad 1 sits     */
} outbind_shape_t;

/**
 * @brief A pad that is not a pin: a ground, a rail, or neither.
 *
 * A servo lead has three wires and the catalogue describes one. Somebody who
 * has found where the signal goes still needs a ground for the return and a
 * rail for the positive, and counting pads to find them is how a lead ends
 * up in the wrong place.
 */
typedef struct {
    uint8_t pad;        /**< the number printed on the board                */
    uint8_t kind;       /**< a link_pad_kind_t                              */
    /**
     * Tenths of a volt. Zero on a rail means it is not a fixed voltage --
     * an input follows whatever feeds it -- and is not the same as 0V,
     * which is what a ground is.
     */
    uint8_t decivolts;
} outbind_pad_t;

/**
 * @brief One board: its identity, catalogue, outline and pads.
 */
typedef struct {
    uint16_t             id;      /**< as the identity page reports it      */
    const char          *name;    /**< name shown to the operator           */
    const outbind_pin_t *pins;    /**< catalogue, in GPIO order             */
    uint8_t              count;   /**< entries in `pins`                    */
    /**
     * The outputs are soldered, not chosen.
     *
     * The IOmcu configures itself and the panel shows what it reads
     * back without offering to change it.  A screen that let an operator
     * re-bind pins that are not on connectors would be offering a choice the
     * board cannot honour.
     */
    bool                 fixed;
    /** @brief Where the pads are, or NULL when nothing here says.
     *
     * A board without one is used from its catalogue and simply is not drawn.
     */
    const outbind_shape_t *shape;
    /** @brief The pads that are not pins, or NULL when nothing here says.
     *
     * A board without them is drawn with its grounds and rails unmarked.
     */
    const outbind_pad_t *pads;
    uint8_t              pad_count;   /**< entries in `pads`                  */
} outbind_board_t;

/**
 * @brief The board with this identity, or NULL when nothing here describes it.
 *
 * The table compiled into this build first, then a board learned over the
 * link.  A build that ships a catalogue for a board uses its own: the
 * compiled one has been read by somebody, names the exact signal holding
 * each reserved pin, and cannot change under a running bench.
 *
 * @param id  Identity the IOmcu reports, an outbind_board_id_t value or a
 *            learned identity.
 * @return    The board, or NULL when neither the table nor the learned board
 *            has @p id.  OUTBIND_BOARD_UNKNOWN always returns NULL.
 */
const outbind_board_t *outbind_board(uint16_t id);

/* ------------------------------------------------ a board that describes itself */

/**
 * @brief Render a board's catalogue into LINK_CAT_COUNT registers for the wire.
 *
 * Slots past the board's own pins are left with a pad number of zero, which
 * is how the page says there is no pin there.
 *
 * @param board  The board to render, or NULL for a page of zeroes.
 * @param regs   LINK_CAT_COUNT registers to write.  NULL does nothing.
 */
void outbind_board_to_regs(const outbind_board_t *board, uint16_t *regs);

/**
 * @brief Learn the board a catalogue page describes, replacing any previous
 *        one.
 *
 * There is room for exactly one: the panel talks to one IOmcu, and a
 * second learned board would be a pin map for hardware that is not on the
 * bench.  Refuses a page that cannot be a board -- no pins, a GPIO past
 * OUT_MAX_PIN, pins out of GPIO order, or the same GPIO twice -- and learns
 * nothing rather than half of it, because a catalogue half read is a pin map
 * that disagrees with the board sending it.
 *
 * Refuses an identity the compiled table already has: that board is
 * described here already, and letting the wire replace it would let an
 * IOmcu rename the pin holding the safety line.
 *
 * @param id    Identity the catalogue page came from.  Must not be
 *              OUTBIND_BOARD_UNKNOWN or an identity in the compiled table.
 * @param regs  LINK_CAT_COUNT registers of the catalogue page.  NULL is
 *              refused.
 * @return      true when the board was learned, false when it was refused and
 *              the previous learned board, if any, is unchanged.
 */
bool outbind_learn_board(uint16_t id, const uint16_t *regs);

/**
 * @brief Render a board's shape into LINK_SH_COUNT registers, or leave them
 *        zero when it has none.
 *
 * @param board  The board whose shape to render, or NULL for a page of zeroes.
 * @param regs   LINK_SH_COUNT registers to write.  NULL does nothing.
 */
void outbind_shape_to_regs(const outbind_board_t *board, uint16_t *regs);

/**
 * @brief Give the learned board a shape from a shape page.
 *
 * Refuses one that cannot be a board: no pads in a row, a corner that is not
 * one, no pitch, or an outline too small to hold the row it claims.  Refuses
 * one whose rows cannot hold the catalogue's pins, since the two pages
 * describe the same board and a pad with nowhere to sit would be drawn off
 * the outline.  A board with no shape is used and not drawn, so refusing
 * costs the picture and nothing else.
 *
 * @param id    Identity of the learned board.  Must equal the learned board's
 *              identity.
 * @param regs  LINK_SH_COUNT registers of the shape page.  NULL is refused.
 * @return      true when the shape was learned, false when it was refused.
 */
bool outbind_learn_shape(uint16_t id, const uint16_t *regs);

/**
 * @brief Where a pad's centre is, in hundredths of a millimetre from the
 *        top-left of the outline.
 *
 * False for a pad the shape does not place: pads are numbered from one, up to
 * twice the row length.
 *
 * @param shape  The outline to place the pad on.
 * @param pad    Pad number, 1 to twice @p shape's per_side.
 * @param x_cmm  Receives the x coordinate of the pad centre.
 * @param y_cmm  Receives the y coordinate of the pad centre.
 * @return       true when the pad is placed and both outputs are written;
 *               false for a NULL argument, a shape with no pads per side, or a
 *               pad outside the range above.
 */
bool outbind_pad_xy(const outbind_shape_t *shape, uint8_t pad,
                    uint16_t *x_cmm, uint16_t *y_cmm);

/**
 * @brief The pads that are not pins, and how many.
 *
 * NULL and 0 for a board nothing here describes that way.
 *
 * @param board  Identity of the board, as for outbind_board().
 * @return       The pad table, or NULL.  The count is outbind_pad_count().
 */
const outbind_pad_t *outbind_pads(uint16_t board);

/**
 * @brief How many pads that are not pins the board has, or 0 for a board
 *        nothing here describes that way.
 *
 * @param board  Identity of the board, as for outbind_board().
 * @return       The number of entries in outbind_pads(), or 0.
 */
uint8_t outbind_pad_count(uint16_t board);

/**
 * @brief Render them into LINK_PAD_COUNT registers, or leave them zero.
 *
 * @param board  The board whose pads to render, or NULL for a page of zeroes.
 * @param regs   LINK_PAD_COUNT registers to write.  NULL does nothing.
 */
void outbind_pads_to_regs(const outbind_board_t *board, uint16_t *regs);

/**
 * @brief Give the learned board its grounds and rails.
 *
 * Refuses a list that cannot belong to the board: a pad of zero, a kind that
 * is not one, pads out of order or repeated, or a pad the catalogue already
 * calls a pin -- one pad cannot be both a GPIO and a ground, and a page
 * saying so is describing something other than this board. Refusing costs
 * the marks and nothing else.
 *
 * @param id    Identity of the learned board.  Must equal the learned board's
 *              identity.
 * @param regs  LINK_PAD_COUNT registers of the pad page.  NULL is refused.
 * @return      true when the pads were learned, false when the page was
 *              refused.
 */
bool outbind_learn_pads(uint16_t id, const uint16_t *regs);

/**
 * @brief Forget it.
 *
 * Not needed when the link drops.  The slot is keyed by identity and is
 * returned for that identity alone, so a board that has been unplugged
 * cannot be reached by the one that replaces it -- and keeping it means the
 * screen still knows the pins of a board whose link merely blinked, which is
 * what a compiled-in catalogue already gives a board this build knows.
 */
void outbind_forget_learned(void);

/**
 * @brief The table itself, so a caller can walk every board rather than guess
 *        at a range of identities.  outbind_board_at() returns NULL past the
 *        end.
 *
 * @return  The number of boards in the compiled table.
 */
uint8_t outbind_board_count(void);

/**
 * @brief The board at @p index in the table, or NULL past the end.
 *
 * @param index  Position in the table, 0 to outbind_board_count() - 1.
 * @return       The board, or NULL when @p index is past the end.
 */
const outbind_board_t *outbind_board_at(uint8_t index);

/**
 * @brief Whether @p index may be added to a selection on @p board.
 *
 * Takes the board rather than reading it from the table, so the rule can be
 * held against a board this build does not yet contain -- a soldered one, for
 * instance, which refuses every pin because there is nothing there to choose.
 *
 * @param board  The board whose catalogue @p index indexes.
 * @param index  Catalogue index, 0 to the board's count - 1.
 * @return       true when the pin is in range, the board is not soldered and
 *               the pin is not reserved; false otherwise.
 */
bool outbind_pin_selectable(const outbind_board_t *board, uint8_t index);

/**
 * @brief Its catalogue and how long it is.
 *
 * NULL and 0 for a board this build does not know, so a caller that forgets to
 * check offers nothing rather than offering the wrong thing.
 *
 * @param board  Identity of the board, as for outbind_board().
 * @return       The catalogue, in GPIO order, or NULL.  The length is
 *               outbind_pin_count().
 */
const outbind_pin_t *outbind_pins(uint16_t board);

/**
 * @brief How many pins the board's catalogue lists, or 0 for a board this
 *        build does not know.
 *
 * @param board  Identity of the board, as for outbind_board().
 * @return       The number of entries in outbind_pins(), or 0.
 */
uint8_t outbind_pin_count(uint16_t board);

/**
 * @brief Index of @p gpio in that board's catalogue, or its count when the
 *        board does not bring that pin out.
 *
 * @param board  Identity of the board, as for outbind_board().
 * @param gpio   GPIO number to look up.
 * @return       Catalogue index of @p gpio.  The board's count when it is not
 *               listed, and 0 for a board this build does not know.
 */
uint8_t outbind_index_of(uint16_t board, uint8_t gpio);

/**
 * @brief Every pin of a board that no output and no PD mini pin may take, one
 *        bit per GPIO (0 to 63).
 *
 * That is the catalogue's reserved pins, plus every pin the catalogue does
 * not list: the pins the board uses without bringing them out and the
 * numbers the part does not have.  The IOmcu hands this mask to
 * outputs_reserve_pins(), which the OUTPUTS and SUPPLY pages both consult,
 * and the panel greys the catalogue's reserved pins, so the two ends cannot
 * disagree about which pins exist to be given away.  An unknown board
 * reserves everything: a build that cannot say which pins are safe must not
 * hand any of them out.
 *
 * @param board  Identity of the board, as for outbind_board().
 * @return       Bit n is set when GPIO n is reserved. All 64 bits are set for
 *               an unknown board.
 */
uint64_t outbind_reserved_mask(uint16_t board);

/* ------------------------------------------------------------- protocols */

/**
 * @brief What can be bound, as an operator names it.
 *
 * Rate is not offered separately.  DShot600 and DShot300 are different
 * entries because they are different things to choose between, not one thing
 * with a number attached; the same goes for the bidirectional pair.
 */
typedef struct {
    const char *name;          /**< label the operator sees                */
    out_driver_t driver;       /**< the driver that renders the pins       */
    uint16_t     rate;      /**< Hz for a pulse driver, kbit/s for DShot    */
    uint8_t      max_pins;  /**< PPM is one pin by definition               */
    uint8_t      channels;  /**< channels one pin of it renders             */
    /**
     * What the channels this entry renders are for.
     *
     * Carried by the entry rather than derived from the driver: a pulse at
     * 50Hz is a servo on one pin and an ESC (electronic speed controller)
     * on the next, and the two rest in different places -- centred at
     * 1500us, or stopped at the low end.  The driver cannot tell them
     * apart, so the operator does, by choosing the entry.
     */
    out_role_t   role;
} outbind_proto_t;

/** @brief Entries in the protocol table, OFF included. */
#define OUTBIND_PROTOS 8u

/**
 * @brief The protocol table, indexed by protocol number; entry 0 is OFF.
 *
 * @return  The table of OUTBIND_PROTOS entries.
 */
const outbind_proto_t *outbind_protos(void);

/* --------------------------------------------------------- the selection */

/**
 * @brief The selection: which board, the protocol being edited, and each
 *        protocol's pins.
 */
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

/**
 * @brief Empty, and belonging to no board: nothing can be chosen until one is
 *        set.
 *
 * @param b  The selection to clear.  NULL does nothing.
 */
void outbind_init(outbind_t *b);

/**
 * @brief Which protocol holds @p index, or 0 (OFF) when nothing does.
 *
 * A pin refused because another protocol holds it is a different fact from
 * one the IOmcu reserves: the first is a choice the operator can undo
 * by going to that protocol, the second is the wiring.  A screen that drew
 * them alike would offer to free the heartbeat line.
 *
 * @param b      The selection.
 * @param index  Catalogue index of the pin.
 * @return       The protocol number holding the pin, or 0 when none does or
 *               @p b is NULL.
 */
uint8_t outbind_group_of(const outbind_t *b, uint8_t index);

/**
 * @brief Pins chosen across every protocol, and the channels they render.
 *
 * Both are budgets shared by all of them: LINK_OUT_SLOTS and LINK_OUT_CHANNELS.
 *
 * @param b  The selection.
 * @return   The number of pins chosen in protocols 1 and up. 0 for NULL.
 */
uint8_t outbind_chosen_total(const outbind_t *b);

/**
 * @brief Channels the chosen pins render, across every protocol.
 *
 * @param b  The selection.
 * @return   The sum of each chosen pin's channels, capped at 255. 0 for NULL.
 */
uint8_t outbind_channels_used(const outbind_t *b);

/**
 * @brief Drop anything a selection cannot mean on its board.
 *
 * Bits above the catalogue's width, and pins past what a protocol takes.
 * `outbind_t` is a plain struct and one arrives from outside -- read off the
 * wire, or restored -- so it is trimmed before it is shown rather than
 * trusted.
 *
 * @param b  The selection, trimmed in place.  NULL does nothing.
 */
void outbind_trim(outbind_t *b);

/**
 * @brief Say which board this binding is for.
 *
 * Clears the selection whenever the board changes, and that is the point: a
 * bit in `pins` is an index into one board's catalogue and means a different
 * pin -- or no pin -- in another's.  Carrying a selection across would bind
 * whatever happened to sit at that index on the board now in front of the
 * operator.
 *
 * @param b      The selection.  NULL does nothing.
 * @param board  Identity of the board the selection is now for.
 */
void outbind_set_board(outbind_t *b, uint16_t board);

/**
 * @brief How many pins the protocol being edited has chosen.
 *
 * @param b  The selection.
 * @return   The count of pins in the edited protocol. 0 for NULL.
 */
uint8_t outbind_chosen(const outbind_t *b);

/**
 * @brief The protocol a page rendered from this selection reads back as: the
 *        lowest-numbered one holding a pin, or OFF when nothing does.
 *
 * Which protocol is being edited is not on the wire.  A page carries pins,
 * and outbind_from_slots() has to name a protocol from them, so it names this
 * one.  A screen that has just been handed a binding uses this to tell the
 * two apart: a protocol equal to it says only what the pins already said,
 * and any other is somebody's choice.
 *
 * @param b  The selection.
 * @return   The protocol number, or 0 (OFF) when no pin is chosen or @p b is
 *           NULL.
 */
uint8_t outbind_wire_proto(const outbind_t *b);

/**
 * @brief Add or remove a pin, in the protocol being edited.
 *
 * Refuses a reserved pin, an index off the end, one more pin than the
 * protocol can take -- PPM's second pin is not a second output, it is a
 * mistake -- and a pin another protocol holds, which is that protocol's to
 * give up.  Removing a pin this protocol holds always succeeds, because an
 * operator must always be able to undo what they just did.
 *
 * @param b      The selection, changed on success.
 * @param index  Catalogue index of the pin.
 * @return       true when the pin was toggled; false when it was refused, and
 *               the selection is unchanged.
 */
bool outbind_toggle(outbind_t *b, uint8_t index);

/**
 * @brief Choose which protocol is being edited.
 *
 * Nothing is dropped.  The pins chosen for the protocol being left stay
 * bound and appear as held on the grid, and the pins of the one arrived at
 * come back as they were: a selector that retargeted the current pins would
 * make binding a second protocol mean unbinding the first.
 *
 * The whole selection is trimmed here, since this is every screen's way in
 * to a binding that came from outside.
 *
 * @param b      The selection.  NULL does nothing.
 * @param proto  Protocol number, an index into outbind_protos().  A value of
 *               OUTBIND_PROTOS or more does nothing.
 */
void outbind_set_proto(outbind_t *b, uint8_t proto);

/**
 * @brief True when this pin may still be added to the protocol being edited,
 *        for a screen drawing it.
 *
 * Includes the compare-register rule of outbind_pwm_clash(), which needs no
 * programming port: a PWM protocol is not offered the partner of a pin bound
 * as PWM.  The slice rule needs the port's pin, which this binding does not
 * carry, so a screen asks outbind_pwm_clash() for it.
 *
 * @param b      The selection.
 * @param index  Catalogue index of the pin.
 * @return       true when the pin may be added; false otherwise.
 */
bool outbind_can_add(const outbind_t *b, uint8_t index);

/** @brief No programming port pin, for outbind_pwm_clash(). */
#define OUTBIND_NO_GPIO 0xFFu

/**
 * @brief Why the PWM generator cannot take a pin, as outbind_pwm_clash() says.
 */
typedef enum {
    OUTBIND_CLASH_NONE = 0,
    /** @brief The pin's slice is the programming port's pin's slice. */
    OUTBIND_CLASH_PROG_SLICE,
    /** @brief The pin's compare register drives a pin already bound as PWM. */
    OUTBIND_CLASH_COMPARE,
} outbind_clash_t;

/**
 * @brief Whether the IOmcu refuses catalogue entry @p index for the protocol
 *        being edited, beside @p prog_gpio and the pins @p b binds as PWM.
 *
 * Applies to a protocol on OUT_DRIVER_PWM and a pin that is not reserved
 * and that no protocol holds; any other pin or protocol is
 * OUTBIND_CLASH_NONE.  Two refusals, in this order:
 *
 *  - OUTBIND_CLASH_PROG_SLICE: the pin shares a slice with @p prog_gpio, the
 *    programming port's pin (prog_port_slots_check()).  Only while no
 *    protocol holds @p prog_gpio: a slot on that pin leaves the port without
 *    one.  OUTBIND_NO_GPIO, or any GPIO past OUT_MAX_PIN, is no port.
 *  - OUTBIND_CLASH_COMPARE: the pin shares a compare register with a pin a
 *    PWM protocol holds (out_pwm_bind() in the IOmcu).
 *
 * Both are out_pwm_same_slice() and out_pwm_same_compare(), the helpers the
 * IOmcu decides with.  @p with, when not NULL, gets the GPIO the pin
 * clashes with, or OUTBIND_NO_GPIO for OUTBIND_CLASH_NONE.
 *
 * @param b         The selection.
 * @param index     Catalogue index of the pin being checked.
 * @param prog_gpio GPIO of the programming port's pin, or OUTBIND_NO_GPIO.
 * @param with      Receives the GPIO clashed with, or NULL to skip it.
 * @return          The refusal, or OUTBIND_CLASH_NONE.
 */
outbind_clash_t outbind_pwm_clash(const outbind_t *b, uint8_t index,
                                  uint8_t prog_gpio, uint8_t *with);

/**
 * @brief outbind_pwm_clash() beside every programming port's pin: @p count pins
 *        from @p ports (NULL or 0 for none), each OUTBIND_NO_GPIO for a port
 *        without one.
 *
 * The slice rule is checked against each in order, the first clash named; then
 * the compare rule.  The IOmcu checks a slot against every port it has
 * (prog_port_slots_check()).
 *
 * @param b      The selection.
 * @param index  Catalogue index of the pin being checked.
 * @param ports  GPIOs of the programming ports, or NULL.
 * @param count  Number of entries in @p ports.
 * @param with   Receives the GPIO clashed with, or NULL to skip it.
 * @return       The first refusal found, or OUTBIND_CLASH_NONE.
 */
outbind_clash_t outbind_pwm_clash_ports(const outbind_t *b, uint8_t index,
                                        const uint8_t *ports, uint8_t count,
                                        uint8_t *with);

/* ------------------------------------------------------------- the pages */

/**
 * @brief Render the selection into the OUTPUTS page.
 *
 * @p regs is LINK_OS_COUNT registers, cleared and rebuilt: every slot the
 * selection does not use is set to no driver, so a page written from this is
 * the whole truth rather than a change to what was there.
 *
 * Slots are filled in pin order across every protocol and take channels from
 * zero upward, so the lowest chosen pin is channel 0 whichever protocol holds
 * it.  Filling stops at LINK_OUT_SLOTS slots or LINK_OUT_CHANNELS channels,
 * whichever comes first.  Returns how many slots were used.
 *
 * @param b     The selection.  NULL leaves @p regs with the default slots.
 * @param regs  LINK_OS_COUNT registers to write.  NULL does nothing.
 * @return      The number of slots used, 0 for NULL arguments.
 */
uint8_t outbind_to_slots(const outbind_t *b, uint16_t *regs);

/**
 * @brief Read a selection back out of an OUTPUTS page.
 *
 * The IOmcu holds the configuration, so the panel asks it what is
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
 *
 * @p chan_cfg is the CHAN_CFG page that came back with @p regs, and it is
 * what separates two entries the slots page cannot: SERVO PWM and MOTOR PWM
 * are one driver at one rate, and only the role of the slot's first channel
 * says which was chosen.  NULL takes the first entry that matches driver and
 * rate, which on this catalogue reads a motor back as a servo and rests it
 * at half throttle, so pass the page whenever there is one.
 *
 * @param b         Receives the selection.  Cleared on refusal.
 * @param board     Identity of the board the page was read from.
 * @param regs      LINK_OS_COUNT registers of the OUTPUTS page.
 * @param chan_cfg  LINK_CC_COUNT registers of the CHAN_CFG page, or NULL.
 * @return          true when the page reads back to itself as a selection on
 *                  @p board; false when it is refused and @p b is cleared.
 */
bool outbind_from_slots(outbind_t *b, uint16_t board,
                        const uint16_t *regs, const uint16_t *chan_cfg);

/** @brief outbind_role_proto(): the role's slots are bound as several entries. */
#define OUTBIND_PROTO_MIXED 0xFFu

/**
 * @brief The entry the slots of @p role are bound as, an index into
 *        outbind_protos(), read from an OUTPUTS page and the CHAN_CFG page that
 *        came back with it: what MOTOR & ESC's TARGET row names before the
 *        throttle's pins.
 *
 * A slot is the role's when its first channel carries the role on @p
 * chan_cfg, as outbind_from_slots() reads it; its entry is the one of that
 * role with the slot's driver and rate.  Returns 0 when no slot is the
 * role's, when a page is NULL, or when the role's one slot set matches no
 * entry, and OUTBIND_PROTO_MIXED when the role's slots match two entries
 * or one matches none beside one that does.
 *
 * @param regs      LINK_OS_COUNT registers of the OUTPUTS page.
 * @param chan_cfg  LINK_CC_COUNT registers of the CHAN_CFG page.
 * @param role      The role to look for.
 * @return          The entry index, 0, or OUTBIND_PROTO_MIXED as above.
 */
uint8_t outbind_role_proto(const uint16_t *regs, const uint16_t *chan_cfg,
                           out_role_t role);

/**
 * @brief Render the channel configuration to match.
 *
 * Each entry says what its channels are for, and that is what decides where a
 * channel rests when it stops being commanded -- stopped, or centred.  With
 * more than one protocol bound the roles are mixed, and each channel takes the
 * role of the protocol whose pin renders it.  Channels the selection does not
 * use keep the schema's defaults.
 *
 * @p min_us and @p max_us go to the throttle channels only, through
 * outputs_chan_cfg_set_throttle_range(); every surface channel keeps the
 * schema's 1000 to 2000us.  Endpoints that page would refuse are not
 * written at all.
 *
 * @param b       The selection.  NULL leaves @p regs at the defaults.
 * @param regs    LINK_CC_COUNT registers to write.  NULL does nothing.
 * @param min_us  Low endpoint of the throttle channels, in microseconds.
 * @param max_us  High endpoint of the throttle channels, in microseconds.
 */
void outbind_to_chan_cfg(const outbind_t *b, uint16_t *regs,
                         uint16_t min_us, uint16_t max_us);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_OUT_BIND_H */
