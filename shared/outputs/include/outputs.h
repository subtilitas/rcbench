/**
 * @file
 * @brief The intermediary every output goes through: one set of rules for
 *        arming, clamping, slew and the silence timeout, with a protocol as a
 *        driver behind them.
 *
 *   A channel is a normalised command and a role.  Screens and procedures
 *   write channels and do not know what is on the wire.
 *
 *   A driver declares its shape: how many channels one of it consumes, how
 *   many pins, and whether the pin is also an input.  PPM (pulse-position
 *   modulation) is eight channels on one pin, and bidirectional DShot listens
 *   on the pin it drives.
 *
 *   Arming, clamping, slew and where to go when the link goes quiet are
 *   answered here for every output.  Only turning a command into edges is
 *   per-protocol, on the IOmcu.
 *
 * The unit is a thousandth of the channel's own span.  Microseconds mean
 * nothing to DShot and a DShot code means nothing to a PWM (pulse-width
 * modulation) servo; both have a proportion of travel, and 1000 steps is
 * finer than any of these protocols resolve.
 *
 * Refused and clamped are different answers.  An endpoint outside 400 to
 * 2500us is refused: it is a configuration mistake, and accepting it leaves
 * the range looking set when it is not.  A command outside its span is
 * clamped: commands arrive many times a second from a host that may be
 * mid-drag, and an output that stops at its stop is what a stop is for.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A command runs 0..OUT_SPAN across the channel's own travel. */
#define OUT_SPAN            1000u

/** @brief Channels in a bank. */
#define OUT_MAX_CHANNELS    16u

/** @brief Slots in a bank: driver configurations, each rendering channels. */
#define OUT_MAX_SLOTS        8u

/**
 * @brief What a pulse driver may render, in microseconds.
 *
 * Endpoints outside this are refused: no servo made takes them, so they are a
 * typo, not a range.  The floor is below a 760us tail servo's end of travel,
 * 410us.
 */
#define OUT_FLOOR_US       400u
#define OUT_CEILING_US    2500u

/**
 * @brief What rest means for a channel, which is the one thing a command cannot
 *        say.
 *
 * A throttle at rest is stopped, and its slew is asymmetric: going up is
 * ramped and coming down is immediate, because reducing throttle is the safe
 * direction.  A surface at rest is centred, and its slew is symmetric,
 * because for a surface neither direction is the safe one.
 */
typedef enum {
    OUT_ROLE_THROTTLE = 0,   /**< rests at 0 */
    OUT_ROLE_SURFACE  = 1,   /**< rests at OUT_SPAN / 2 */
} out_role_t;

/** @brief A set of roles, one bit a role: what an arm covers. */
#define OUT_ROLE_BIT(r)  ((uint8_t)(1u << (unsigned)(r)))

/** @brief Every role: the bits of all of them. */
#define OUT_ROLES_ALL    ((uint8_t)(OUT_ROLE_BIT(OUT_ROLE_THROTTLE) \
                                    | OUT_ROLE_BIT(OUT_ROLE_SURFACE)))

/** @brief The drivers a slot can be configured as. */
typedef enum {
    OUT_DRIVER_NONE = 0,             /**< not configured */
    OUT_DRIVER_PWM,      /**< one channel, one pin, a pulse per frame      */
    OUT_DRIVER_PPM,      /**< several channels multiplexed onto one pin    */
    OUT_DRIVER_DSHOT,    /**< one channel, one pin, an 11-bit code         */
    OUT_DRIVER_DSHOT_BIDIR, /**< the same, inverted, with a reply           */
    OUT_DRIVER_COUNT                 /**< number of drivers */
} out_driver_t;

/**
 * @brief What a driver is shaped like.
 *
 * The channel count is a range rather than a number because PPM's is chosen
 * when it is configured.
 */
typedef struct {
    const char *name;            /**< name shown to the operator */
    uint8_t     channels_min;    /**< fewest channels a slot takes */
    uint8_t     channels_max;    /**< most channels a slot takes */
    uint8_t     pins;            /**< pins a slot takes */
    bool        reads_back;   /**< the pin is not purely an output         */
    bool        pulsed;       /**< renders through the channel's endpoints */
    uint16_t    rate_min_hz;     /**< lowest rate the driver takes: Hz, kbit/s for DShot */
    uint16_t    rate_max_hz;     /**< highest rate the driver takes: Hz, kbit/s for DShot */
} out_driver_def_t;

/**
 * @brief The table itself, so callers can ask rather than assume.
 *
 * @param d The driver to look up.
 * @return The table entry for @p d, or NULL when @p d is not a driver.
 */
const out_driver_def_t *out_driver(out_driver_t d);

/** @brief The state of one channel of the bank. */
typedef struct {
    out_role_t role;         /**< throttle or surface */
    uint16_t   command;      /**< asked for, 0..OUT_SPAN                   */
    uint16_t   actual;       /**< what the slew has reached                */
    uint16_t   rest;         /**< where it goes when it is not driving     */
    uint16_t   slew_per_s;   /**< span units per second; 0 is immediate    */
    uint16_t   slew_rem;     /**< thousandths carried between steps        */
    uint16_t   min_us;       /**< what a pulse driver renders 0 as         */
    uint16_t   max_us;       /**< and OUT_SPAN                             */
    uint32_t   last_command_ms;  /**< time of the last command, in ms */
} out_channel_t;

/** @brief One driver configuration: the channels and pin it renders. */
typedef struct {
    out_driver_t driver;         /**< OUT_DRIVER_* */
    uint8_t      first_channel;  /**< first channel the slot renders */
    uint8_t      channels;       /**< channels the slot renders */
    uint8_t      pin;            /**< GPIO number */
    uint16_t     rate_hz;        /**< frame rate in Hz, or bit rate in kbit/s for DShot */
} out_slot_t;

/** @brief The highest pin number a reservation mask can hold. */
#define OUT_MAX_PIN     63u

/** @brief The state of a bank of channels and slots. */
typedef struct {
    out_channel_t channel[OUT_MAX_CHANNELS];  /**< per-channel state */
    out_slot_t    slot[OUT_MAX_SLOTS];        /**< per-slot configuration */
    bool          armed;
    uint8_t       roles;        /**< what an arm covers, OUT_ROLE_BIT()s   */
    uint32_t      timeout_ms;   /**< silence after which a channel rests   */
    uint32_t      last_step_ms;   /**< time of the last outputs_step() */
    uint64_t      reserved;     /**< pins this build will not drive        */
} outputs_t;

/** @brief Silence after which a channel is rendered at its role's rest.  It
 *  keeps driving; see outputs_driving(). */
#define OUT_DEFAULT_TIMEOUT_MS  500u

/**
 * @brief The command a channel of @p role rests at, 0..OUT_SPAN of its own
 *        endpoints: 0 for a throttle, OUT_SPAN / 2 for a surface.
 *
 * What an armed bank renders on a channel nobody commands, and what a screen
 * that shows such a channel's value shows.
 *
 * @param role The channel's role.
 * @return 0 for a throttle, OUT_SPAN / 2 for a surface.
 */
uint16_t outputs_role_rest(out_role_t role);

/**
 * @brief Set the bank to its start: every channel a surface at rest, the
 *        endpoints 1000 to 2000us, and the timeout OUT_DEFAULT_TIMEOUT_MS.
 *
 * Nothing is done when @p o is NULL. The bank starts disarmed, with every role
 * in its arm set.
 *
 * @param o The bank, overwritten.
 * @param now_ms Current time in milliseconds, stamped on every channel.
 */
void outputs_init(outputs_t *o, uint32_t now_ms);

/**
 * @brief Configure one slot, or clear it with OUT_DRIVER_NONE.
 *
 * Refuses a driver it does not know, a channel count the driver cannot take,
 * a channel range that runs off the end, a rate the driver cannot produce, a
 * pin another slot is already driving, and a channel another slot is already
 * rendering.
 *
 * OUT_DRIVER_NONE clears the slot and is accepted for any valid slot index.
 *
 * @param o The bank.
 * @param slot Slot index, 0 to OUT_MAX_SLOTS - 1.
 * @param cfg The configuration to store.
 * @return true when the slot is stored or cleared, false when @p o or @p cfg is
 *         NULL, the slot index is out of range, or a check above refuses it.
 */
bool outputs_configure(outputs_t *o, uint8_t slot, const out_slot_t *cfg);

/**
 * @brief Declare the pins this build must never drive, one bit per pin.
 *
 * Which pins exist and which are already spoken for is the one thing about an
 * output the bank cannot work out for itself: it is a property of the board
 * and the firmware around it, not of the protocol.  The pin arrives from the
 * host over the OUTPUTS page and is therefore whatever an operator typed, so
 * something has to hold the list of pins that already carry the CAN
 * (Controller Area Network) controller, the safety line and the console.
 *
 * Setting the mask does not tear down a slot that is already using a pin in
 * it.  Reserve before configuring; the IOmcu does this once at boot,
 * and the panel leaves the mask empty because it drives no pin of its own.
 *
 * @param o The bank.
 * @param mask Reserved pins, bit n for GPn. Pins above OUT_MAX_PIN are not
 *        reachable and need no bit.
 */
void outputs_reserve_pins(outputs_t *o, uint64_t mask);

/**
 * @brief Whether @p pin may be driven: it exists and is not in the reserved
 *        mask.
 *
 * The reserved mask is the only test here. Slots that already drive a pin are
 * tested by outputs_configure().
 *
 * @param o The bank.
 * @param pin GPIO number.
 * @return true when @p pin is at most OUT_MAX_PIN and not in the reserved
 *         mask, false otherwise or for NULL.
 */
bool outputs_pin_available(const outputs_t *o, uint8_t pin);

/**
 * @brief Endpoints, refused rather than clamped.
 *
 * Inverted pairs are straightened: an inverted pair is a mistake rather than a
 * range, and it would make the clamp unsatisfiable.
 *
 * @param o The bank.
 * @param ch Channel index, 0 to OUT_MAX_CHANNELS - 1.
 * @param min_us Pulse width in microseconds for command 0.
 * @param max_us Pulse width in microseconds for OUT_SPAN.
 * @return true when stored. false for NULL, a channel out of range, or an
 *         endpoint outside OUT_FLOOR_US to OUT_CEILING_US (400 to 2500us).
 */
bool outputs_set_endpoints(outputs_t *o, uint8_t ch, uint16_t min_us,
                           uint16_t max_us);

/**
 * @brief Set the role of a channel: throttle or surface.
 *
 * A role change moves the channel's rest. When the bank is disarmed the
 * channel's command and actual value move to the new rest as well.
 *
 * @param o The bank.
 * @param ch Channel index, 0 to OUT_MAX_CHANNELS - 1.
 * @param role The new role.
 * @return true when the role is set or already was, false for NULL, a channel
 *         out of range, or a role other than the two defined.
 */
bool outputs_set_role(outputs_t *o, uint8_t ch, out_role_t role);

/**
 * @brief Set how fast a channel moves towards its command.
 *
 * @param o The bank.
 * @param ch Channel index, 0 to OUT_MAX_CHANNELS - 1.
 * @param per_s Speed in span units per second; 0 is immediate.
 * @return true when stored, false for NULL or a channel out of range.
 */
bool outputs_set_slew(outputs_t *o, uint8_t ch, uint16_t per_s);

/**
 * @brief Apply @p count channels of a CHANNELS page from @p first, as commands.
 *
 * A page write arrives in frames and each frame says which registers it
 * carries, so only those are commanded: a channel outside the frame has not
 * been commanded by it, and stamping its clock would keep it alive on
 * somebody else's traffic.  That is the whole of the per-channel timeout --
 * a servo held at 10Hz must not keep a surface nobody is driving from
 * reaching its rest.
 *
 * @param o The bank.
 * @param regs The CHANNELS page, indexed by channel number.
 * @param first First channel to command, below LINK_CH_COUNT.
 * @param count Number of channels from @p first. Clipped at LINK_CH_COUNT.
 * @param now_ms Current time in milliseconds, stamped on each channel.
 */
void outputs_channels_apply_n(outputs_t *o, const uint16_t *regs,
                              uint8_t first, uint8_t count, uint32_t now_ms);

/**
 * @brief Command a channel.
 *
 * Clamped, and remembered even while disarmed.
 *
 * @param o The bank.
 * @param ch Channel index, 0 to OUT_MAX_CHANNELS - 1.
 * @param command Asked position, 0 to OUT_SPAN; a larger value is clamped to
 *        OUT_SPAN.
 * @param now_ms Current time in milliseconds, stamped as the last command.
 * @return true when stored, false for NULL or a channel out of range.
 */
bool outputs_set(outputs_t *o, uint8_t ch, uint16_t command, uint32_t now_ms);

/**
 * @brief Command every channel below @p limit that carries @p role, and say how
 *        many took it.
 *
 * One command reaching several channels is what a bench throttle is: the
 * screen has one slider, and the pins it should drive are whichever ones the
 * binding said are motors.  Naming them by role rather than by number keeps
 * that decision in the binding, where the operator made it, instead of
 * fixing a channel number in two places that must agree.
 *
 * Channels of another role are not touched, so a servo bound beside a motor
 * keeps its own command.
 *
 * @param o The bank.
 * @param role Role whose channels are commanded.
 * @param limit Channels below this index are considered, clipped at
 *        OUT_MAX_CHANNELS.
 * @param command Asked position, 0 to OUT_SPAN, as outputs_set() takes it.
 * @param now_ms Current time in milliseconds.
 * @return The number of channels of @p role below @p limit that were
 *         commanded. 0 for NULL.
 */
unsigned outputs_set_role_channels(outputs_t *o, out_role_t role,
                                   uint8_t limit, uint16_t command,
                                   uint32_t now_ms);

/**
 * @brief Arming is decided by the end holding the wire, not by the asker.
 *
 * Disarming goes to rest with no ramp.
 *
 * Re-asserting the current state is not an event and does nothing, so the
 * call is safe every pass from a loop that recomputes whether driving is
 * allowed.  Stamping the clock on every call would hold every channel alive
 * and defeat the timeout.
 *
 * @param o The bank.
 * @param armed true to arm for every role, false to disarm.
 * @param now_ms Current time in milliseconds.
 */
void outputs_arm(outputs_t *o, bool armed, uint32_t now_ms);

/**
 * @brief Arm the channels of @p roles, OUT_ROLE_BIT()s, and only those.  0
 *        disarms.  outputs_arm() is this with OUT_ROLES_ALL.
 *
 * A channel of a role outside the set is not driven: outputs_step() holds it
 * at its rest and outputs_channel_driving() says no.  A bank armed for the
 * throttle leaves a surface bound beside it without a pulse, and the other
 * way round.
 *
 * Changing the set while armed is applied at once: a channel whose role
 * leaves goes to rest with no ramp, as at a disarm, and a channel whose role
 * joins is stamped fresh, as at an arm.  Re-asserting the set in force is not
 * an event.  Bits outside OUT_ROLES_ALL are ignored.
 *
 * @param o The bank.
 * @param roles Set of roles to arm, OUT_ROLE_BIT() bits. 0 disarms.
 * @param now_ms Current time in milliseconds.
 */
void outputs_arm_roles(outputs_t *o, uint8_t roles, uint32_t now_ms);

/**
 * @brief The roles the bank is armed for: 0 while disarmed.
 *
 * @param o The bank.
 * @return The OUT_ROLE_BIT() set the bank is armed for, 0 while disarmed or
 *         for NULL.
 */
uint8_t outputs_armed_roles(const outputs_t *o);

/**
 * @brief Whether the bank is armed for @p role.
 *
 * @param o The bank.
 * @param role The role to test.
 * @return true when the role's bit is in outputs_armed_roles(), false otherwise.
 */
bool outputs_role_armed(const outputs_t *o, out_role_t role);

/**
 * @brief Whether @p ch is driven: the bank is armed for its role.
 *
 * @param o The bank.
 * @param ch Channel index, 0 to OUT_MAX_CHANNELS - 1.
 * @return true when the channel's role is armed, false for NULL or a channel
 *         out of range.
 */
bool outputs_channel_driving(const outputs_t *o, uint8_t ch);

/**
 * @brief Whether @p slot is driven: it is configured and the bank is armed for
 *        the role of at least one of its channels.
 *
 * A PPM (pulse-position modulation) frame carries all of its channels or none,
 * so a slot that mixes roles drives every channel when one of them is armed;
 * the others are held at their rest by outputs_step().
 *
 * @param o The bank.
 * @param slot Slot index, 0 to OUT_MAX_SLOTS - 1.
 * @return true when the slot has a driver and one of its channels is driven,
 *         false otherwise or for NULL.
 */
bool outputs_slot_driving(const outputs_t *o, uint8_t slot);

/**
 * @brief Has this channel stopped being commanded?
 *
 * Per channel rather than per bank, because the pages that write them are
 * written independently: a servo being dragged does not keep a throttle
 * alive, and a throttle nobody has touched must not stop a servo.
 *
 * @param o The bank.
 * @param ch Channel index, 0 to OUT_MAX_CHANNELS - 1.
 * @param now_ms Current time in milliseconds.
 * @return true when at least timeout_ms has passed since the last command, and
 *         also for NULL or a channel out of range.
 */
bool outputs_overdue(const outputs_t *o, uint8_t ch, uint32_t now_ms);

/**
 * @brief Say a channel is still being watched without changing what it was
 *        asked.
 *
 * A loop that is running with nothing new to say differs from one that has
 * stopped, and the timeout exists to tell those apart.  Re-sending the last
 * command would also overwrite a command that arrived from elsewhere in the
 * same pass.
 *
 * @param o The bank.
 * @param ch Channel index, 0 to OUT_MAX_CHANNELS - 1.
 * @param now_ms Current time in milliseconds, stamped as the last command time.
 * @return true when stamped, false for NULL or a channel out of range.
 */
bool outputs_keepalive(outputs_t *o, uint8_t ch, uint32_t now_ms);

/**
 * @brief Whether the bank is armed, for a screen that shows it.
 *
 * @param o The bank.
 * @return true when armed, false when disarmed or for NULL.
 */
bool outputs_armed(const outputs_t *o);

/**
 * @brief Advance slew to @p now_ms, and put a channel nobody has commanded for
 *        timeout_ms at its role's rest.
 *
 * It decides what a channel renders, not
 * whether it renders: see outputs_driving().
 *
 * @param o The bank. Nothing is done when NULL.
 * @param now_ms Current time in milliseconds.
 */
void outputs_step(outputs_t *o, uint32_t now_ms);

/**
 * @brief Everything to rest, immediately.
 *
 * The failsafe edge calls this, and it reaches every output because every
 * output is in this bank.
 *
 * @param o The bank. Nothing is done when NULL.
 */
void outputs_all_off(outputs_t *o);

/**
 * @brief The position a channel has reached.
 *
 * @param o The bank.
 * @param ch Channel index, 0 to OUT_MAX_CHANNELS - 1.
 * @return The slew's actual value, 0..OUT_SPAN; 0 for NULL or a channel out of
 *         range.
 */
uint16_t outputs_actual(const outputs_t *o, uint8_t ch);

/**
 * @brief What @p ch is being asked for, as against outputs_actual()'s where the
 *        slew and the staleness timeout have got it to.
 *
 * A channel nobody has commanded is asking for its role's rest.
 *
 * @param o The bank.
 * @param ch Channel index, 0 to OUT_MAX_CHANNELS - 1.
 * @return The command, 0..OUT_SPAN; 0 for NULL or a channel out of range.
 */
uint16_t outputs_command(const outputs_t *o, uint8_t ch);

/**
 * @brief What a pulse driver should emit for @p ch, in microseconds.
 *
 * The actual value mapped onto the channel's min_us..max_us endpoints, rounded
 * to the nearest microsecond.
 *
 * @param o The bank.
 * @param ch Channel index, 0 to OUT_MAX_CHANNELS - 1.
 * @return The pulse width in microseconds; 0 for NULL or a channel out of range.
 */
uint16_t outputs_pulse_us(const outputs_t *o, uint8_t ch);

/**
 * @brief Whether the bank is armed for any role.
 *
 * The same answer as outputs_armed(), asked by a driver rather than by a
 * screen.  Which pins carry edges is outputs_slot_driving() and
 * outputs_channel_driving(): an arm covers the roles it names and no others.
 *
 * Two gates a driver also stands behind are not in this answer:
 *
 *   The heartbeat.  Nothing here can see the safety line.  What armed means
 *   is settled by the end holding the wire before it calls outputs_arm(): the
 *   IOmcu recomputes it every pass from the ARM register, its link
 *   failsafe and its heartbeat monitor, so a bank armed there already carries
 *   a beat that was judged this pass.  "This pass" is the whole of the
 *   guarantee: silence is noticed by a poll, so the line can have been dead
 *   for up to one pass of that loop before the poll says so, and the
 *   monostable is what covers that gap in hardware.
 *
 *   A command arriving.  That is per channel, outputs_overdue(), and it
 *   decides what a channel renders rather than whether it renders.
 *   outputs_step() puts an overdue channel at its rest and leaves the others
 *   where they are.
 *
 * Limitation: an armed bank drives every bound pin of the roles it is armed
 * for, whether or not anything is commanding it.  A channel that has had no command for timeout_ms is
 * rendered at its role's rest.  A surface's rest is OUT_SPAN/2, which
 * outputs_pulse_us() renders as the midpoint of that channel's own endpoints:
 * 1500us across the default 1000 to 2000us, and 760us across the 660 to
 * 860us of a narrow servo.  Endpoints are per channel and are written over
 * the CHAN_CFG page, so a channel carrying the wrong role presents the
 * midpoint of whatever range it was given, for as long as the bank is armed.
 *
 * The timeout does reach that channel.  outputs_overdue() is true for it and
 * outputs_step() acts on it every pass; what the timeout resolves to is the
 * rest the channel is already at, so the edges stay on the pin.  A disarm is
 * what takes them away.
 *
 * Nor is a wrong role out of reach of every command.  Channels are addressed
 * two ways.  outputs_set_role_channels() addresses them by role and passes a
 * surface by, which is why the throttle does not reach one.  outputs_set()
 * and outputs_channels_apply_n() address them by index, and the CHANNELS page
 * is written that way, so a write there commands a channel whatever its role.
 *
 * Rest is not where a channel sits for the first timeout_ms after an arm
 * either.  outputs_arm() stamps last_command_ms on all OUT_MAX_CHANNELS
 * channels, so a command given while disarmed is fresh again and is rendered
 * until it goes overdue.  What gets rendered in that time is the slew's
 * answer, not the command:
 *
 *   slew_per_s 0   the first step is the whole distance, so the bank
 *                  carries the last command it was left with for the whole
 *                  timeout -- 500ms at OUT_DEFAULT_TIMEOUT_MS
 *   slew_per_s > 0 the disarm has already put actual at rest, so the bank
 *                  carries a ramp from rest towards that command.  It
 *                  arrives only if the distance is under
 *                  slew_per_s * timeout_ms / 1000, half of slew_per_s at
 *                  the default timeout; past that the timeout returns it to
 *                  rest with the command never reached.  The timeout runs
 *                  from the arm, not from the arrival.  That bound holds
 *                  however often the caller steps, because outputs_step()
 *                  carries the part-unit between steps rather than rounding
 *                  each one up.
 *
 * A throttle rests at 0, so its re-arm ramp is always upward and is slewed.
 * A surface rests at the middle of its travel and ramps either way.
 *
 * The bank is what carries it; the pin can lag.  A driver with a setup
 * sequence sends that first: firmware/iomcu/src/outputs_hw.c sends
 * DSHOT_CMD_EDT_ENABLE for DSHOT_CMD_REPEATS (10) frames on the edge into
 * driving for a bidirectional DShot slot, which at the IOmcu's
 * 1,000Hz update rate is the first 10ms of the arm.  The command reaches
 * that ESC after them.
 *
 * What an ESC (electronic speed controller) does with the surface rest is a
 * question about the ESC.  A receiver output of 1500us is about half
 * throttle.  Whether one that has seen no pulses and is then handed 1500us
 * runs there or refuses to arm is not measured on this bench.
 *
 * @param o The bank.
 * @return true when the bank is armed for any role, false when it is not or
 *         @p o is NULL.
 */
bool outputs_driving(const outputs_t *o);

#ifdef __cplusplus
}
#endif
