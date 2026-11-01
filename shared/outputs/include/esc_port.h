/**
 * @file esc_port.h
 * @brief The ESC (electronic speed controller) on its signal wire: takes the pin
 *        of a throttle slot or a DShot programming port, switches the supply and
 *        runs one protocols/escwire session behind the ESC_WIRE and ESC_DATA pages.
 *
 * Pure logic: the pin, the wire driver and the supply are behind esc_port_hw_t.
 * docs/LinkEsc.md states the rules; a test pins each of them.
 *
 * | Event | Line | Supply | After |
 * |---|---|---|---|
 * | ARM asked, free | the output's | as it is | OPEN refused while asked |
 * | ARM asked, held | as it was | as it was | refused (safety_gate_hold_refuses_arm()); the quiet mask holds the bank down as well |
 * | STOP, no write | PARK at once | OFF, PROG_SUPPLY off | held, STOPPED |
 * | STOP in a write | as the write needs, to its read-back, then PARK | panel's OFF waits, then OFF | held, STOPPED |
 * | ABORT | PARK at once, also in a write | OFF, PROG_SUPPLY off | held |
 * | link or heartbeat lost | PARK at once, also in a write | OFF at once | held, STOPPED |
 * | error | PARK at once | OFF, PROG_SUPPLY off | held, FAULT |
 * | CLOSE, its output refuses the pin | closed | as it is | held, LINK_ESC_NOT_BACK: no ARM; CLOSE tries again |
 * | restart of the IOmcu | the chip's reset state | off at boot | FREE |
 *
 * An error is any operation of the session that ends other than EW_SES_OK, a
 * guard refusal, a supply that fails, a BY HAND wait that runs out.  The session
 * itself keeps an ESC in its mode after EW_SES_ERR_TORN and EW_SES_ERR_LOST, so
 * that a repair can follow; the port does not: all_off() aborts the session,
 * which parks the line, and switches the PD mini and PROG_SUPPLY off.  A repair is a new ENTER and
 * a new read.  What the power loss costs a half-written page, by the
 * specification: family S and Bluejay lose 48 tag bytes and read read-only, AM32
 * stays erased until RESTORE (docs/LinkEsc.md, Current limitations).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_ESC_PORT_H
#define RCBENCH_ESC_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "ew_session.h"
#include "link_pages.h"
#include "outputs.h"
#include "supply_page.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The output renders nothing this long before it gives up the pin: 2
 *         frames at 40Hz, the lowest rate. */
#define ESC_PORT_STOP_MS 50u
/** @brief The supply is off at least this long before its off time starts, ms. */
#define ESC_PORT_READ_OFF_MIN_MS 250u
/** @brief The supply has this long to read off, ms. */
#define ESC_PORT_READ_OFF_MAX_MS 3000u
/** @brief The supply has this long to read on, ms. */
#define ESC_PORT_READ_ON_MAX_MS 3000u
/** @brief The line is driven high this long before ON, ms. */
#define ESC_PORT_LEAD_MS 50u
/** @brief BY HAND: the line is driven high at most this long, ms. */
#define ESC_PORT_HAND_MS 60000u
/** @brief A write counts as running this long after its last frame, ms. */
#define ESC_PORT_WRITE_TAIL_MS 150u
/** @brief The quiet mask's bit of a held output slot; ports hold bits 0 to 3. */
#define ESC_PORT_QUIET_SLOT 0x10u
/** @brief What a pin hook returns for no pin. */
#define ESC_PORT_NO_PIN 0xFFu

/** @brief What the port needs from the hardware. */
typedef struct {
    void *ctx;        /**< the first argument of each hook                  */
    uint8_t ports;    /**< programming ports the build has                  */
    /** The pin of a throttle slot or a DShot port the IOmcu holds now, else
     *  ESC_PORT_NO_PIN.  @p is_port false: @p index is a slot. */
    uint8_t (*pin)(void *ctx, bool is_port, uint8_t index);
    /** Take the pin from its output and open the wire on it, parked.  False
     *  when the line gets no resources; give() returns the pin. */
    bool (*take)(void *ctx, uint8_t pin);
    /** Close the line and give the pin back as its output had it, moving nothing
     *  else.  False when its output did not take it back. */
    bool (*give)(void *ctx);
    /** What the supply says now; the build's PD mini. */
    void (*supply)(void *ctx, supply_port_view_t *out);
    /** Ask the supply on at @p mv and @p ma, or off.  False when not taken. */
    bool (*supply_set)(void *ctx, bool on, uint16_t mv, uint16_t ma);
    /** An error or a stop: every other supply of the bench off (PROG_SUPPLY). */
    void (*others_off)(void *ctx);
    ew_driver_t driver; /**< the wire driver; now_us() works while closed   */
} esc_port_hw_t;

/** @brief One port.  Not to be moved after esc_port_init(). */
typedef struct {
    esc_port_hw_t hw;                /**< the hardware                      */
    ew_session_t  ses;               /**< the session                       */
    uint16_t staged[LINK_EW_W_STAGED]; /**< registers 4 to 13               */
    uint16_t last_cmd[LINK_EW_W_CMD_FRAME]; /**< the frame last taken       */
    uint8_t  draft[EW_PAGE_MAX];     /**< the page to write                 */
    uint8_t  backup[LINK_ED_BLOCK_BYTES]; /**< a backup's identity          */
    uint32_t t0_ms;                  /**< when this state began             */
    uint32_t now_ms;                 /**< the clock at the last call        */
    uint32_t tail_t0;                /**< when the last write's frame ended */
    uint32_t hold_ms;                /**< ENTER's hold                      */
    uint16_t seq;                    /**< LINK_EW_SEQ                       */
    uint16_t reads;                  /**< LINK_EW_READS                     */
    uint16_t read_crc;               /**< LINK_EW_READ_CRC                  */
    uint16_t target;                 /**< LINK_EW_TARGET                    */
    uint16_t mv;                     /**< ENTER's voltage                   */
    uint16_t ma;                     /**< ENTER's limit                     */
    uint16_t off_ms;                 /**< ENTER's time off                  */
    uint8_t  state;                  /**< a link_esc_state_t                */
    uint8_t  op;                     /**< a link_esc_op_t                   */
    uint8_t  result;                 /**< a link_esc_result_t               */
    uint8_t  refusal;                /**< a link_esc_ref_t                  */
    uint8_t  line;                   /**< a link_esc_line_t                 */
    uint8_t  pin;                    /**< the GPIO                          */
    uint8_t  block;                  /**< LINK_EW_BLOCK                     */
    uint8_t  other;                  /**< KST, HITEC or an update holds     */
    bool     safe;                   /**< heartbeat trusted, link not silent */
    bool     armed;                  /**< the bank is armed or asked to arm */
    bool     has_pin;                /**< the line holds the pin            */
    bool     stopped;                /**< LINK_EW_F_STOPPED                 */
    bool     fault;                  /**< LINK_EW_F_FAULT                   */
    bool     by_hand;                /**< ENTER's power                     */
    bool     switched;               /**< this session asked the supply on  */
    bool     off_seen;               /**< POWER_OFF: the supply read off    */
    bool     was_writing;            /**< the session wrote at the last step */
    bool     tail;                   /**< ESC_PORT_WRITE_TAIL_MS runs       */
} esc_port_t;

/**
 * @brief Prepare a free port.
 * @param p  the port, zeroed first
 * @param hw the hooks, copied
 * @return false for a NULL, no ports, or a hook missing
 */
bool esc_port_init(esc_port_t *p, const esc_port_hw_t *hw);

/**
 * @brief A write of ESC_WIRE.  A window that starts in registers 0 to 3 is a
 *        command and has to be exactly those 4.  A command that is taken is
 *        acknowledged; what became of it is on the page.
 * @param p      the port
 * @param off    the first register
 * @param n      registers
 * @param in     their values
 * @param now_ms the clock, ms
 * @return 0, or the LINK_NACK_* reason with nothing changed
 */
uint8_t esc_port_write(esc_port_t *p, uint8_t off, uint8_t n,
                       const uint16_t *in, uint32_t now_ms);

/**
 * @brief A read of ESC_WIRE.
 * @param p   the port
 * @param off the first register
 * @param n   registers
 * @param out takes them; left alone for a window past LINK_EW_COUNT
 */
void esc_port_read(const esc_port_t *p, uint8_t off, uint8_t n, uint16_t *out);

/**
 * @brief A write of ESC_DATA, in the block LINK_EW_W_BLOCK names.
 * @param p   the port
 * @param off the first register
 * @param n   registers
 * @param in  their values
 * @return 0; LINK_NACK_READ_ONLY for a block that is; LINK_NACK_BUSY while an
 *         operation runs; LINK_NACK_BAD_RANGE past the block or the page
 */
uint8_t esc_port_data_write(esc_port_t *p, uint8_t off, uint8_t n,
                            const uint16_t *in);

/**
 * @brief A read of ESC_DATA, in the block LINK_EW_W_BLOCK names.
 * @param p   the port
 * @param off the first register
 * @param n   registers
 * @param out takes them; 0 past what the block holds
 */
void esc_port_data_read(const esc_port_t *p, uint8_t off, uint8_t n,
                        uint16_t *out);

/**
 * @brief One pass.
 * @param p      the port
 * @param safe   the heartbeat is trusted and the link is not silent; false cuts
 *               at once, also in a write
 * @param stop   the bank was switched off or disarmed; a write runs to its
 *               read-back first
 * @param now_ms the clock, ms
 */
void esc_port_step(esc_port_t *p, bool safe, bool stop, uint32_t now_ms);

/**
 * @brief Where the port is.
 * @param p the port, may be NULL
 * @return its state; LINK_ESC_FREE for NULL
 */
link_esc_state_t esc_port_state(const esc_port_t *p);

/**
 * @brief The bank may not arm while this is not 0 (safety_gate_arm_now()).
 * @param p the port, may be NULL
 * @return in every state but free, bit n for port n or ESC_PORT_QUIET_SLOT
 */
uint8_t esc_port_quiet_mask(const esc_port_t *p);

/**
 * @brief The programming port whose pin the line holds.
 * @param p the port, may be NULL
 * @return bit n for port n; 0 else
 */
uint8_t esc_port_pin_mask(const esc_port_t *p);

/**
 * @brief The slots an apply may bind (hw_apply_only()): all but a held one, from
 *        the OPEN on, so nothing renders on the pin while the port has it.
 * @param p    the port, may be NULL
 * @param mask the slots asked
 * @return @p mask without the held slot
 */
uint8_t esc_port_may_bind(const esc_port_t *p, uint8_t mask);

/**
 * @brief The pin the port holds, as a reservation mask (outputs_reserve_pins()).
 * @param p the port, may be NULL
 * @return its bit from the take to the give; 0 else
 */
uint64_t esc_port_pins(const esc_port_t *p);

/**
 * @brief Whether the port holds the SUPPLY page: every state but free.
 * @param p the port, may be NULL
 * @return true while held
 */
bool esc_port_supply_hold(const esc_port_t *p);

/**
 * @brief Whether the panel's OFF waits (supply_page_port_writes()): from a
 *        write's first erase, program or `save` to its end, and
 *        ESC_PORT_WRITE_TAIL_MS after.
 * @param p the port, may be NULL
 * @return true then
 */
bool esc_port_writing(const esc_port_t *p);

/**
 * @brief Whether the bank is armed or asked to arm: OPEN is refused then.
 * @param p     the port, may be NULL
 * @param armed true then
 */
void esc_port_bench_armed(esc_port_t *p, bool armed);

/**
 * @brief Whether something else has the bench: OPEN is refused while not 0.
 * @param p    the port, may be NULL
 * @param mask KST's and HITEC's quiet masks, a bit for an update
 */
void esc_port_other(esc_port_t *p, uint8_t mask);

/**
 * @brief The pin of a throttle slot: its driver set, bound, its first channel a
 *        throttle.
 * @param o     the bank, may be NULL
 * @param slot  the slot
 * @param bound bit n: slot n is bound
 * @return the pin, or ESC_PORT_NO_PIN
 */
uint8_t esc_port_slot_pin(const outputs_t *o, uint8_t slot, uint8_t bound);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ESC_PORT_H */
