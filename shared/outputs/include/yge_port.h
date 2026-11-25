/**
 * @file yge_port.h
 * @brief Programming for YGE ESCs (electronic speed controllers) on a
 *        programming port: takes the port's pin, switches the supply and runs
 *        one protocols/openyge session behind the YGE_PROG and YGE_DATA pages.
 *
 * Pure logic: the pin, the line and the supply are behind yge_port_hw_t, the
 * wire behind the session's oy_driver_t.  docs/LinkYge.md states the rules; a
 * test pins each of them.
 *
 * | Event | Line | Supply | After |
 * |---|---|---|---|
 * | ARM asked, free | the port's | as it is | OPEN refused while asked |
 * | ARM asked, held | as it was | as it was | refused (safety_gate_hold_refuses_arm()); the quiet mask holds the bank down as well |
 * | STOP, also in a write | PARK at once | OFF, PROG_SUPPLY off | held, STOPPED |
 * | ABORT | PARK at once, also in a write | OFF, PROG_SUPPLY off | held |
 * | link or heartbeat lost | PARK at once, also in a write | OFF at once | held, STOPPED |
 * | error | PARK at once | OFF, PROG_SUPPLY off | held, FAULT |
 * | LEAVE | PARK | OFF | held, OPEN |
 * | CLOSE, the port refuses the pin | closed | as it is | held, LINK_YGE_NOT_BACK: no ARM; CLOSE tries again |
 * | restart of the IOmcu | the chip's reset state | off at boot | FREE |
 *
 * An error is any operation of the session that ends other than OY_OK, a
 * guard refusal of a draft (OY_GUARD), a supply that fails, a BY HAND wait
 * that runs out.  A write cut after its first frame leaves the ESC's table
 * not known (LINK_YP_F_CUT_WRITE); whether one parameter's write is stored
 * whole is not known (docs/OpenYGE.md section 8, U5).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_YGE_PORT_H
#define RCBENCH_YGE_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"
#include "openyge_session.h"
#include "supply_page.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The port renders nothing this long before it gives up the pin: 2
 *         frames at 40Hz, the lowest rate. */
#define YGE_PORT_STOP_MS 50u
/** @brief The supply is off at least this long before ON, ms. */
#define YGE_PORT_READ_OFF_MIN_MS 250u
/** @brief The supply has this long to read off, ms. */
#define YGE_PORT_READ_OFF_MAX_MS 3000u
/** @brief The supply has this long to read on, ms. */
#define YGE_PORT_READ_ON_MAX_MS 3000u
/** @brief BY HAND: HAND_DONE is taken up to this long after ENTER, ms; the
 *         wait ends 1ms later. */
#define YGE_PORT_HAND_MS 60000u
/** @brief What a pin hook returns for no pin. */
#define YGE_PORT_NO_PIN 0xFFu
/** @brief yge_port_other(): bits from here up refuse every port. */
#define YGE_PORT_OTHER_ALL 0xF0u
/** @name The bits yge_port_others() sets above the ports' own. */
/** @{ */
#define YGE_PORT_OTHER_SERVO  0x10u /**< a KST or HITEC session holds a port  */
#define YGE_PORT_OTHER_ESC    0x20u /**< the ESC_WIRE page holds a pin       */
#define YGE_PORT_OTHER_ATTACH 0x40u /**< a PD mini wiring saved, not attached */
#define YGE_PORT_OTHER_UPDATE 0x80u /**< a firmware update holds the bench   */
/** @} */

/** @brief What the port needs from the hardware. */
typedef struct {
    void *ctx;        /**< the first argument of each hook                  */
    uint8_t ports;    /**< programming ports the build has                  */
    /** @brief The pin of a programming port the IOmcu holds now, else
     *  YGE_PORT_NO_PIN. */
    uint8_t (*pin)(void *ctx, uint8_t port);
    /** @brief Take the pin from its port and open the line on it, parked.
     *  False when the line gets no resources; give() returns the pin. */
    bool (*take)(void *ctx, uint8_t port, uint8_t pin);
    /** @brief Close the line and give the pin back to its port.  False when
     *  the port did not take it back. */
    bool (*give)(void *ctx);
    /** @brief The line parked (false) or listening (true). */
    void (*line)(void *ctx, bool listen);
    /** @brief What the supply says now; the build's PD mini. */
    void (*supply)(void *ctx, supply_port_view_t *out);
    /** @brief Ask the supply on at @p mv and @p ma, or off.  False when not
     *  taken. */
    bool (*supply_set)(void *ctx, bool on, uint16_t mv, uint16_t ma);
    /** @brief An error or a stop: every other supply of the bench off
     *  (PROG_SUPPLY). */
    void (*others_off)(void *ctx);
    oy_driver_t driver; /**< the wire driver                                */
} yge_port_hw_t;

/** @brief One page.  Not to be moved after yge_port_init(). */
typedef struct {
    yge_port_hw_t hw;                 /**< the hardware                     */
    oy_session_t  ses;                /**< the session                      */
    uint16_t staged[LINK_YP_W_STAGED]; /**< registers 4 to 11               */
    uint16_t last_cmd[LINK_YP_W_CMD_FRAME]; /**< the frame last taken       */
    uint16_t draft[OPENYGE_MAX_PARAMS]; /**< the table to write             */
    uint32_t t0_ms;                   /**< when this state began            */
    uint32_t now_ms;                  /**< the clock at the last call       */
    uint16_t seq;                     /**< LINK_YP_SEQ                      */
    uint16_t mv;                      /**< ENTER's voltage                  */
    uint16_t ma;                      /**< ENTER's limit                    */
    uint16_t boot_ms;                 /**< ENTER's boot wait                */
    uint8_t  state;                   /**< a link_yge_state_t               */
    uint8_t  op;                      /**< a link_yge_op_t                  */
    uint8_t  result;                  /**< a link_yge_result_t              */
    uint8_t  refusal;                 /**< a link_yge_ref_t                 */
    uint8_t  ses_res;                 /**< the session's last result        */
    uint8_t  line;                    /**< a link_yge_line_t                */
    uint8_t  pin;                     /**< the GPIO                         */
    uint8_t  port;                    /**< the port; YGE_PORT_NO_PIN free   */
    uint8_t  block;                   /**< LINK_YP_BLOCK                    */
    uint8_t  other;                   /**< yge_port_other()                 */
    bool     safe;                    /**< heartbeat trusted, link not silent */
    bool     armed;                   /**< the bank is armed or asked to arm */
    bool     has_pin;                 /**< the line holds the pin           */
    bool     stopped;                 /**< LINK_YP_F_STOPPED                */
    bool     fault;                   /**< LINK_YP_F_FAULT                  */
    bool     by_hand;                 /**< ENTER's power                    */
    bool     switched;                /**< this session asked the supply on */
    bool     off_seen;                /**< POWER_OFF: the supply read off   */
} yge_port_t;

/**
 * @brief Prepare a free page.
 * @param p  the page, zeroed first
 * @param hw the hooks, copied
 * @return false for a NULL, no ports, or a hook missing
 */
bool yge_port_init(yge_port_t *p, const yge_port_hw_t *hw);

/**
 * @brief A write of YGE_PROG.  A window that starts in registers 0 to 3 is a
 *        command and has to be exactly those 4.  A command that is taken is
 *        acknowledged; what became of it is on the page.
 * @param p      the page
 * @param off    the first register
 * @param n      registers
 * @param in     their values
 * @param now_ms the clock, ms
 * @return 0, or the LINK_NACK_* reason with nothing changed
 */
uint8_t yge_port_write(yge_port_t *p, uint8_t off, uint8_t n,
                       const uint16_t *in, uint32_t now_ms);

/**
 * @brief A read of YGE_PROG.
 * @param p   the page
 * @param off the first register
 * @param n   registers
 * @param out takes them; left alone for a window past LINK_YP_COUNT
 */
void yge_port_read(const yge_port_t *p, uint8_t off, uint8_t n, uint16_t *out);

/**
 * @brief A write of YGE_DATA, in the block LINK_YP_W_BLOCK names.
 * @param p   the page
 * @param off the first register
 * @param n   registers
 * @param in  their values
 * @return 0; LINK_NACK_READ_ONLY for a block that is; LINK_NACK_BUSY while an
 *         operation runs; LINK_NACK_BAD_RANGE past the block
 */
uint8_t yge_port_data_write(yge_port_t *p, uint8_t off, uint8_t n,
                            const uint16_t *in);

/**
 * @brief A read of YGE_DATA, in the block LINK_YP_W_BLOCK names.
 * @param p   the page
 * @param off the first register
 * @param n   registers
 * @param out takes them; left alone for a window past LINK_YD_COUNT
 */
void yge_port_data_read(const yge_port_t *p, uint8_t off, uint8_t n,
                        uint16_t *out);

/**
 * @brief One pass.
 * @param p      the page
 * @param safe   the heartbeat is trusted and the link is not silent; false
 *               cuts at once, also in a write
 * @param stop   the bank was switched off or disarmed, or an update started;
 *               cuts at once, also in a write
 * @param now_ms the clock, ms
 */
void yge_port_step(yge_port_t *p, bool safe, bool stop, uint32_t now_ms);

/**
 * @brief Where the page is.
 * @param p the page, may be NULL
 * @return its state; LINK_YGE_FREE for NULL
 */
link_yge_state_t yge_port_state(const yge_port_t *p);

/**
 * @brief The bank may not arm while this is not 0 (safety_gate_arm_now()).
 * @param p the page, may be NULL
 * @return in every state but free, bit n for port n
 */
uint8_t yge_port_quiet_mask(const yge_port_t *p);

/**
 * @brief The programming port whose pin the line holds.
 * @param p the page, may be NULL
 * @return bit n for port n; 0 else
 */
uint8_t yge_port_pin_mask(const yge_port_t *p);

/**
 * @brief Whether the page holds the SUPPLY page: every state but free.
 * @param p the page, may be NULL
 * @return true while held
 */
bool yge_port_supply_hold(const yge_port_t *p);

/**
 * @brief Whether a write runs: from its first frame to the end of its read
 *        after.  CLOSE and LEAVE are refused then; STOP is not.
 * @param p the page, may be NULL
 * @return true then
 */
bool yge_port_writing(const yge_port_t *p);

/**
 * @brief Whether the bank is armed or asked to arm: OPEN is refused then.
 * @param p     the page, may be NULL
 * @param armed true then
 */
void yge_port_bench_armed(yge_port_t *p, bool armed);

/**
 * @brief What else has the bench: OPEN is refused for port n while bit n is
 *        set, and for every port while a bit of YGE_PORT_OTHER_ALL is.
 * @param p    the page, may be NULL
 * @param mask the ports refused, and YGE_PORT_OTHER_ALL bits
 */
void yge_port_other(yge_port_t *p, uint8_t mask);

/**
 * @brief The mask yge_port_other() takes, as iomcu.h's ports_exclusive()
 *        builds it.  KST, HITEC and ESC_WIRE share the one PD mini with this
 *        page, so a session of any of them refuses every port.
 * @param servo_ports KST's and HITEC's quiet masks
 * @param esc         the ESC_WIRE page is held (esc_port_supply_hold())
 * @param attach      a PD mini wiring is saved and not attached
 * @param update      a firmware update holds the bench
 * @param rail        the ports whose CONNECT the node's rail refuses
 *                    (rsn_prog_refused()), bit n for port n
 * @return the mask
 */
uint8_t yge_port_others(uint8_t servo_ports, bool esc, bool attach, bool update,
                        uint8_t rail);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_YGE_PORT_H */
