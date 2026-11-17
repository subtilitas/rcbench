/**
 * @file
 * @brief Moving one pin from the protocol that holds it to another: the
 *        verdict and the binding after the move.
 *
 * A pin belongs to at most one protocol (out_bind.h), so a pin one protocol
 * holds is refused to every other by outbind_can_add().  A move gives the
 * pin to the other protocol in one step: the pin leaves its holder's set and
 * joins the taker's, and the result is one binding to write.
 *
 * Channels follow pins (outbind_to_slots()), not protocols.  The moved pin
 * keeps its slot and its first channel, every other pin keeps its slot and
 * its channels, and only the moved slot's driver, rate and channel role
 * change.  PPM is the one protocol whose pin renders 8 channels: a PPM pin
 * that moves frees 7 of them, and PPM takes a pin only while no other pin
 * stays bound.
 *
 * Because nothing else moves, moving the pin back to its holder restores
 * the binding bit for bit.  That reverse move is the undo.
 *
 * Refused, each with its own verdict so the screen can say why:
 *  - a binding no read has confirmed, a reserved pin, a soldered board
 *    without roles;
 *  - a programming port's pin and a pin an interface holds (SENSOR BUS, PD
 *    MINI, PHASE TAP): no protocol holds them, and each is set on the screen
 *    that owns it;
 *  - a pin no protocol holds, or the taker's own: a plain tap binds or
 *    unbinds it;
 *  - OFF as the taker;
 *  - a taker at its pin cap, a page without a slot or the channels the
 *    taker needs;
 *  - the PWM (pulse-width modulation) rules of outbind_pwm_clash_ports(),
 *    checked on the binding after the move.
 *
 * Pure: no UI, no link, no state.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_OUT_BIND_TAKE_H
#define RCBENCH_OUT_BIND_TAKE_H

#include <stdbool.h>
#include <stdint.h>

#include "out_bind.h"
#include "outputs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The verdict on a move, in the order the checks run. */
typedef enum {
    /** @brief The move is possible; the binding after it is filled. */
    OUTBIND_TAKE_OK = 0,
    /** @brief A NULL binding, a board this build does not know, a pin past
     *         the catalogue or a taker past the protocol table. */
    OUTBIND_TAKE_INVALID,
    /** @brief No read has confirmed the binding: nothing is moved on it. */
    OUTBIND_TAKE_UNREAD,
    /** @brief The IOmcu reserves the pin (heartbeat, CAN). */
    OUTBIND_TAKE_RESERVED,
    /** @brief A programming port has the pin; info.port names it. */
    OUTBIND_TAKE_PORT,
    /** @brief An interface holds the pin; info.iface names it. */
    OUTBIND_TAKE_IFACE,
    /** @brief No protocol holds the pin: a plain tap binds it. */
    OUTBIND_TAKE_NOT_HELD,
    /** @brief The taker holds the pin already: a plain tap unbinds it. */
    OUTBIND_TAKE_SAME,
    /** @brief The board's pins are soldered and carry no roles. */
    OUTBIND_TAKE_FIXED,
    /** @brief The taker is OFF, which holds no pins. */
    OUTBIND_TAKE_OFF,
    /** @brief The taker holds info.cap pins already. */
    OUTBIND_TAKE_CAP,
    /** @brief Every slot of the page is in use without the moved pin. */
    OUTBIND_TAKE_SLOTS,
    /** @brief The taker needs info.need channels and info.free are free. */
    OUTBIND_TAKE_CHANNELS,
    /** @brief The pin shares a PWM slice with programming port pin
     *         info.with. */
    OUTBIND_TAKE_CLASH_PROG_SLICE,
    /** @brief The pin shares a compare register with PWM pin info.with. */
    OUTBIND_TAKE_CLASH_COMPARE,
    /** @brief On a board with roles: the pin's slice holds info.with, a PWM
     *         output of the other role. */
    OUTBIND_TAKE_CLASH_OUT_SLICE,
} outbind_take_t;

/**
 * @brief What else holds pins beside the binding.
 *
 * Zero-initialised it describes an unread binding with no port and no
 * interface, so a context left unfilled refuses every move.
 */
typedef struct {
    /** @brief The binding was read from the IOmcu and confirmed. */
    bool            read;
    /** @brief Each programming port's pin, OUTBIND_NO_GPIO (any GPIO past
     *         OUT_MAX_PIN) for a port without one; NULL for no port. */
    const uint8_t  *ports;
    /** @brief The same ports as the PWM rules see them: a port that is
     *         under no PWM rule as OUTBIND_NO_GPIO (outputs_pwm_pins());
     *         NULL for none. */
    const uint8_t  *pwm_ports;
    /** @brief Entries in @c ports and in @c pwm_ports. */
    uint8_t         port_count;
    /** @brief The pins each interface holds, bit g for GPIO g; NULL for
     *         no interface. */
    const uint64_t *ifaces;
    /** @brief Entries in @c ifaces. */
    uint8_t         iface_count;
} outbind_take_ctx_t;

/** @brief No port, interface or channel in an outbind_take_info_t field. */
#define OUTBIND_TAKE_UNSET 0xFFu

/**
 * @brief What a move does, or why it is refused.
 *
 * Every field the checks reach before the verdict is filled; the rest keep
 * their "none" value (OUTBIND_NO_GPIO, OUTBIND_TAKE_UNSET, 0).
 */
typedef struct {
    uint8_t    gpio;     /**< the pin's GPIO, OUTBIND_NO_GPIO when invalid  */
    uint8_t    pad;      /**< the number printed on the board, or 0         */
    uint8_t    loser;    /**< the protocol holding the pin, 0 for none      */
    uint8_t    taker;    /**< the protocol asked to take it                 */
    uint8_t    keeps;    /**< pins the loser keeps after the move           */
    /** @brief The pin's first channel after the move; OUTBIND_TAKE_UNSET
     *         unless the verdict is OUTBIND_TAKE_OK. */
    uint8_t    channel;
    out_role_t role_was; /**< what the loser's channels are for             */
    out_role_t role_now; /**< what the taker's channels are for             */
    uint8_t    cap;      /**< the taker's pin cap on this board             */
    uint8_t    need;     /**< channels one pin of the taker renders         */
    /** @brief Channels free once the pin has left its holder. */
    uint8_t    free;
    /** @brief The GPIO a PWM rule clashes with, or OUTBIND_NO_GPIO. */
    uint8_t    with;
    /** @brief For OUTBIND_TAKE_PORT: the port, 0 to port_count - 1. */
    uint8_t    port;
    /** @brief For OUTBIND_TAKE_IFACE: the interface, 0 to
     *         iface_count - 1. */
    uint8_t    iface;
} outbind_take_info_t;

/**
 * @brief Whether catalogue entry @p index may move to protocol @p taker,
 *        and the binding after the move.
 *
 * The checks run in the order of outbind_take_t and the first refusal
 * stands.  The PWM rules run on the binding with the pin taken from its
 * holder and @p taker as the protocol being edited: outbind_pwm_clash_ports()
 * says nothing about a pin a protocol holds.
 *
 * @param b      The binding as it stands.
 * @param index  Catalogue index of the pin.
 * @param taker  The protocol that is to hold the pin, 1 to
 *               OUTBIND_PROTOS - 1.
 * @param ctx    The ports and interfaces beside the binding, and whether it
 *               was read; NULL reads as an unread binding.
 * @param after  On OUTBIND_TAKE_OK the binding after the move, with
 *               @p taker as the protocol being edited; on any other verdict
 *               a copy of @p b.  NULL to skip; untouched when @p b is NULL.
 * @param info   What the move does or why it is refused; NULL to skip.
 * @return       The verdict.
 */
outbind_take_t outbind_take_check(const outbind_t *b, uint8_t index,
                                  uint8_t taker,
                                  const outbind_take_ctx_t *ctx,
                                  outbind_t *after,
                                  outbind_take_info_t *info);

/**
 * @brief outbind_take_check() against board @p bd rather than the one the
 *        table names for @p b.
 *
 * Holds the rules against a board this build does not contain, as
 * outbind_pin_selectable() does: a soldered board without roles, for one.
 * @p bd's catalogue is read for the pin; everything else is read from the
 * table entry of @p b's board, so @p bd is that entry or a copy of it.
 *
 * @param bd     The board, or NULL for OUTBIND_TAKE_INVALID.
 * @param b      The binding as it stands.
 * @param index  Catalogue index of the pin.
 * @param taker  The protocol that is to hold the pin.
 * @param ctx    The ports, interfaces and read state, or NULL.
 * @param after  The binding after the move, or NULL.
 * @param info   What the move does or why it is refused, or NULL.
 * @return       The verdict.
 */
outbind_take_t outbind_take_check_on(const outbind_board_t *bd,
                                     const outbind_t *b, uint8_t index,
                                     uint8_t taker,
                                     const outbind_take_ctx_t *ctx,
                                     outbind_t *after,
                                     outbind_take_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_OUT_BIND_TAKE_H */
