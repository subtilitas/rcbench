/**
 * @file
 * @brief The current monitors' pages of the link: SENSE, SERVO_SENSE,
 *        SERVO_WIN, SENSE_MAP, SENSE_WIN_A, SENSE_WIN_B and SENSE_LIMIT.
 *
 * Part of the page map; included by link_pages.h only, at the place these
 * pages hold among the others.  Include link_pages.h.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_LINK_PAGES_SENSE_H
#define RCBENCH_LINK_PAGES_SENSE_H

#ifndef RCBENCH_LINK_PAGES_H
#error "include link_pages.h, which includes this file"
#endif

/* --- the SENSE page (protocol 4.7): two I2C (Inter-Integrated Circuit)
 *     current monitors on one bus the IOmcu runs on two of its pins,
 *     a TI INA228 in the ESC's power path and a TI INA3221 on the servo
 *     rail.
 *
 *     ENABLE to KHZ are the bus, one frame: ENABLE bit 0 the INA228, bit 1
 *     the INA3221, bit 2 the AS5600 output encoder (protocol 4.9); the GPIO for SDA and the one for SCL; the clock, 400kHz
 *     and nothing else (LINK_SN_KHZ_BUS): the schedule's 1ms tick does not
 *     fit a slower bus.  The RP2350 has its I2C function at pin mod 4 -- 0 I2C0 SDA,
 *     1 I2C0 SCL, 2 I2C1 SDA, 3 I2C1 SCL -- so SDA is a GPIO whose number
 *     mod 4 is 0 or 2 and SCL is the one after it: one I2C block's pair.
 *
 *     I228_ADDR to register 7 are the INA228, one frame: its address, 0x40
 *     to 0x4F; its shunt in micro-ohms, 50 to 20000; the current its range
 *     is set for, in 0.1A, 1.0 to 300.0A, the bench's design maximum.
 *     That current chooses ADCRANGE only: 1
 *     while the shunt's voltage at it is at most 40.96mV, 0 up to
 *     163.84mV, and past that the write is refused.  CURRENT_LSB is the
 *     shunt ADC's step divided by the shunt at that range, so CURRENT and
 *     VSHUNT clip together, and SHUNT_CAL is 4096 at either range.  A
 *     shunt whose full scale at the chosen range passes 2000A is refused
 *     too: below 81.92 micro-ohms only ADCRANGE 1 is taken.  The rule is
 *     the driver's, ina228_calibrate() in shared/sense.
 *
 *     I3221_ADDR to register 11 are the INA3221, one frame: its address,
 *     0x40 to 0x43; its shunt in 0.1 milliohm, 50 to 10000 (5mOhm to
 *     1Ohm); the channels read, bits 0..2 for CH1 to CH3, at least one
 *     while the part is enabled.  The part's full scale is 163.8mV across
 *     the shunt: 1.638A at the DAOKAI module's 0.1Ohm, 32.76A at 5mOhm,
 *     which is the most SERVO_SENSE's signed mA carry.
 *
 *     Registers 7 and 11 are reserved: they read 0, and a write of anything
 *     else is refused with BAD_VALUE.  They keep each part's frame four
 *     registers long for a later minor to give them a meaning.
 *
 *     Refused with BAD_VALUE: a value out of its range, SDA and SCL not one
 *     I2C block's pair (link_sn_pins_pair()) whatever ENABLE holds, and
 *     while a part is enabled a pin past the bank, reserved, bound to an
 *     output or held by the SUPPLY page, the two parts on one address while
 *     both are enabled, and any change while the bank is armed -- the bus
 *     opened again stops the readings for some milliseconds mid-run.  One
 *     frame that is not a pair is taken: ENABLE 0 with SDA or SCL 0, which
 *     is how a panel writes pins that are not set.  The pins are no
 *     output's while a part is enabled.
 *
 *     FLAGS onwards are read only: FLAGS (link_sense_flag_t); PRESENT, bit
 *     n for address 0x40 + n answering the last scan; the ID the INA228
 *     gave (DEVICE_ID) and the one the INA3221 gave (die ID); transactions
 *     failed, modulo 65536; the INA228's die temperature in 0.1C, signed,
 *     and its DIAG_ALRT as read; its CHARGE in 0.01mAh, signed 32 bit, and
 *     its ENERGY in 0.01Wh, 32 bit, each low register first and counted
 *     since the run's arm; and the ESC's own telemetry voltage (10mV) and
 *     current (10mA), ESC_FLAGS bit 0 and bit 1 saying each is valid --
 *     here because BENCH carries the INA228's while it answers.
 *
 *     The output encoder (protocol 4.9) is an ams OSRAM AS5600 magnetic
 *     angle sensor on the same bus, at its fixed address 0x36, on the horn
 *     shaft of the servo under test.  ENABLE bit 2 turns it on and has no
 *     parameter of its own: the bus pins are the INA parts', and the angle
 *     comes back as the 12-bit count, 0 to 4095 for one turn (0.0879 degrees
 *     a count), with no centre applied -- the panel keeps that.  The
 *     IOmcu reads it at 500Hz.  A bit 2 written to a 4.8 or older
 *     IOmcu is not sent: the panel gates it on minor >= 9.
 *
 *     AS5600_FLAGS (link_sense_enc_flag_t) say whether it answers and what
 *     its magnet is doing.  AS5600_ANGLE is RAW ANGLE as last read, 0 until
 *     the first read of a set-up.  It is a position only while ENC_ONLINE,
 *     ENC_VALID and ENC_MD are all set: with MD clear the part sees no
 *     magnet and the count means nothing.  With ML or MH set beside MD the
 *     count is a position whose noise the datasheet does not specify.  AS5600_MAGNITUDE is the part's CORDIC
 *     (coordinate rotation digital computer) magnitude, read at 20Hz, 0
 *     until read; it is a measure of the field.  AS5600_SAMPLES counts the
 *     angle reads, modulo 65536: two reads of the page with the same count
 *     are one sample.  AS5600_STILL_MS is how long the angle has stayed
 *     within 12 counts (1.05 degrees) of an anchor -- the angle at the last
 *     time it left that band -- in milliseconds, saturating at 65535, 0 with
 *     no sample and 0 while MD is clear: without a magnet the count is not
 *     a position, and the anchor starts afresh when MD returns.  A move that ends at time t_end, read at time t_read, shows
 *     STILL_MS = t_read - t_end to within the 2ms sample interval, so the
 *     panel times the end of a move to the IOmcu's resolution
 *     whatever its own polling interval.
 *
 *     RESETS (register 31, protocol 4.11) counts the times a current
 *     monitor was found running on its power-on set-up and not the one
 *     written to it: the INA228 in the low byte, the INA3221 in the high
 *     byte, each modulo 256, since the set-up in force was taken.  The
 *     IOmcu reads the INA228's ADC_CONFIG and the INA3221's
 *     Configuration back every 40ms; a part found reset loses ONLINE and
 *     is set up again 1000ms on.  The count shows a reset that was found
 *     and repaired between two reads of FLAGS.  Before 4.11 the register
 *     is reserved and reads 0.
 *
 *     ENABLE to register 11 are kept in the IOmcu's flash beside the
 *     bindings and the supply's wiring, and the bus is opened at boot. */
enum {
    LINK_SN_ENABLE          = 0,
    LINK_SN_SDA_PIN         = 1,
    LINK_SN_SCL_PIN         = 2,
    LINK_SN_KHZ             = 3,
    LINK_SN_I228_ADDR       = 4,
    LINK_SN_I228_SHUNT_UOHM = 5,
    LINK_SN_I228_MAX_DA     = 6,
    LINK_SN_RESERVED_7      = 7,   /**< reads 0; written 0                  */
    LINK_SN_I3221_ADDR      = 8,
    LINK_SN_I3221_SHUNT_DMOHM = 9,
    LINK_SN_I3221_CHANNELS  = 10,
    LINK_SN_RESERVED_11     = 11,  /**< reads 0; written 0                  */
    LINK_SN_FLAGS           = 12,  /**< read only from here                 */
    LINK_SN_PRESENT         = 13,
    LINK_SN_I228_ID         = 14,
    LINK_SN_I3221_ID        = 15,
    LINK_SN_ERRORS          = 16,
    LINK_SN_I228_TEMP_DC    = 17,  /**< 0.1C, signed -- read as int16_t     */
    LINK_SN_I228_DIAG       = 18,
    LINK_SN_I228_CHARGE_LO  = 19,  /**< 0.01mAh, int32_t, low half first    */
    LINK_SN_I228_CHARGE_HI  = 20,
    LINK_SN_I228_ENERGY_LO  = 21,  /**< 0.01Wh, uint32_t, low half first    */
    LINK_SN_I228_ENERGY_HI  = 22,
    LINK_SN_ESC_VOLTAGE_CV  = 23,  /**< 10mV steps                          */
    LINK_SN_ESC_CURRENT_CA  = 24,  /**< 10mA steps                          */
    LINK_SN_ESC_FLAGS       = 25,
    LINK_SN_AS5600_FLAGS    = 26,  /**< protocol 4.9, from here             */
    LINK_SN_AS5600_ANGLE    = 27,  /**< RAW ANGLE, 0 to 4095                */
    LINK_SN_AS5600_MAGNITUDE = 28, /**< 12 bits; 0 until read               */
    LINK_SN_AS5600_SAMPLES  = 29,  /**< angle reads, modulo 65536           */
    LINK_SN_AS5600_STILL_MS = 30,  /**< within 12 counts of its anchor, ms  */
    LINK_SN_RESETS          = 31,  /**< protocol 4.11; reads 0 before it    */
    LINK_SN_COUNT           = 32,
};
/** @brief The registers a 4.8 IOmcu's page has. */
#define LINK_SN_COUNT_V48 26u
/**
 * @brief The set-up, ENABLE to register 11: what a write may change, and what
 * flash keeps.
 */
#define LINK_SN_CONFIG_COUNT 12u

/** @brief ENABLE's bits. */
#define LINK_SN_EN_I228   0x01u
#define LINK_SN_EN_I3221  0x02u
#define LINK_SN_EN_AS5600 0x04u     /**< protocol 4.9 */
/** @brief Every bit ENABLE takes. */
#define LINK_SN_EN_ALL    (LINK_SN_EN_I228 | LINK_SN_EN_I3221 | LINK_SN_EN_AS5600)

/* The ranges a write is held to. */
/** @brief The bus clock, the one KHZ takes: 400kHz, the parts' fast mode. */
#define LINK_SN_KHZ_BUS          400u
#define LINK_SN_I228_ADDR_MIN    0x40u
#define LINK_SN_I228_ADDR_MAX    0x4Fu
#define LINK_SN_I228_UOHM_MIN      50u
#define LINK_SN_I228_UOHM_MAX   20000u
#define LINK_SN_I228_DA_MIN        10u
#define LINK_SN_I228_DA_MAX      3000u
#define LINK_SN_I3221_ADDR_MIN   0x40u
#define LINK_SN_I3221_ADDR_MAX   0x43u
#define LINK_SN_I3221_DMOHM_MIN    50u
#define LINK_SN_I3221_DMOHM_MAX 10000u
#define LINK_SN_I3221_CH_ALL     0x07u

/**
 * @brief Whether @p sda and @p scl are one I2C block's pair: SDA a GPIO whose
 * number mod 4 is 0 or 2, SCL the GPIO after it.
 *
 * The one rule for the page, the bus that opens the pins, the panel's writes
 * and the values SETUP offers.  It does not say the pins are free, or that the
 * board has them.
 * @param sda the GPIO of the data line
 * @param scl the GPIO of the clock line
 * @return true when the pair is one I2C block's pair
 */
static inline bool link_sn_pins_pair(unsigned sda, unsigned scl)
{
    return (sda % 2u) == 0u && scl == sda + 1u;
}

/** @brief What the IOmcu says about the bus and the two parts. */
typedef enum {
    LINK_SN_I228_ONLINE    = 1u << 0, /**< answering, identity good, in use */
    /**
     * @brief Its last identity read was an INA228's.
     *
     * With ONLINE clear, it has stopped answering since.
     */
    LINK_SN_I228_ID_OK     = 1u << 1,
    /**
     * @brief Something answers at its address with another identity, and is not
     * used.
     */
    LINK_SN_I228_ID_WRONG  = 1u << 2,
    /**
     * @brief A current read at the end of its range, in the last 50ms window
     * or since the run's arm: BENCH's current and power, or their peaks, are
     * then bounds, not values.
     */
    LINK_SN_I228_CLIPPED   = 1u << 3,
    LINK_SN_I3221_ONLINE   = 1u << 4,
    LINK_SN_I3221_ID_OK    = 1u << 5,
    LINK_SN_I3221_ID_WRONG = 1u << 6,
    LINK_SN_BUS_OPEN       = 1u << 8, /**< the I2C block runs on its pins   */
    LINK_SN_BUS_STUCK      = 1u << 9, /**< SDA held low, being clocked free */
} link_sense_flag_t;

/** @brief RESETS' two counts, each modulo 256. */
#define LINK_SN_RESETS_I228(r)  ((uint8_t)((r) & 0xFFu))
#define LINK_SN_RESETS_I3221(r) ((uint8_t)(((r) >> 8) & 0xFFu))

/** @brief AS5600_FLAGS' bits (protocol 4.9). */
typedef enum {
    /** @brief Answering at 0x36 with a STATUS an AS5600 can give. */
    LINK_SN_ENC_ONLINE = 1u << 0,
    /**
     * @brief STATUS MD: a magnet is detected.
     *
     * Clear, AS5600_ANGLE is no position.
     */
    LINK_SN_ENC_MD     = 1u << 1,
    /** @brief STATUS ML: the field is too weak. */
    LINK_SN_ENC_ML     = 1u << 2,
    /** @brief STATUS MH: the field is too strong. */
    LINK_SN_ENC_MH     = 1u << 3,
    /**
     * @brief Something answers at 0x36 with a STATUS no AS5600 gives, ML and MH
     * both set; not used.
     */
    LINK_SN_ENC_WRONG  = 1u << 4,
    /** @brief AS5600_ANGLE and bits 1 to 3 hold a reading of this set-up. */
    LINK_SN_ENC_VALID  = 1u << 5,
} link_sense_enc_flag_t;

/** @brief ESC_FLAGS' bits. */
#define LINK_SN_ESC_VOLTAGE_OK 0x01u
#define LINK_SN_ESC_CURRENT_OK 0x02u

/* --- the SERVO_SENSE page (protocol 4.7): the INA3221's three channels,
 *     and a move timed on the IOmcu's clock.
 *
 *     Registers 0 to 11, four a channel from CH1: the mean and the highest
 *     current in mA, signed, and the mean and the lowest bus voltage in mV,
 *     at the load side of the shunt, over the last 50ms window.  WINDOW
 *     numbers the windows, modulo 65536; a read does not end one, so a
 *     reply lost on the link loses nothing.  CH_FLAGS bits 0..2 say a
 *     channel's window holds readings; bits 4..6 that one of them read the
 *     top of the range, 163.8mV across the shunt, which makes that
 *     channel's mean and highest current lower bounds and not values; bit 7
 *     the same of the capture's move.
 *
 *     CAP_ARM to CAP_BAND_MA are a capture, one frame: CAP_ARM with bit 7
 *     set, the INA3221 channel in bits 0..1 (1: CH2 and CH3 are refused,
 *     LINK_SS_CAP_CH) and in bits 8..10 the
 *     output channel (0 to 7) whose next changed command starts the timing;
 *     the holding level the move ends at in mA, signed, -32768 to 32767
 *     (protocol 4.11; before it 0 to 32767, and 32768 to 65535 refused):
 *     a holding level near 0A reads below zero by the part's offset; the
 *     distance from
 *     the level before the command that counts as movement, and the band
 *     around the holding level that counts as arrival, each 1 to 32767mA.
 *     An arm is the whole frame from CAP_ARM; an arm restarts a capture
 *     already running.  CAP_ARM written 0 disarms and is never refused,
 *     alone or at the head of the frame, whose other registers are then not
 *     stored.  Refused with BAD_VALUE: any other write that is not the
 *     whole frame, CAP_ARM bits outside the three fields, a value out of
 *     range, an INA3221 channel other than CH1 or one SENSE does not read,
 *     and an output channel
 *     that is not a surface rendered by a PWM slot; with NOT_ARMED on a
 *     disarmed bank.  A bank that stops driving ends a capture that has not
 *     finished: CAP_ARM reads 0 and CAP_STATE idle.
 *
 *     CAP_STATE onwards are read only: the state (link_cap_state_t);
 *     captures finished, modulo 65536; the time from the PWM frame that
 *     carries the new pulse to the movement and to the arrival, in 0.1ms,
 *     6553.5ms at most, resolved to CH1's 1ms sample interval; the
 *     highest and the mean filtered current of the move in mA, signed,
 *     over CAP_SAMPLES samples.  A capture ends arrived, at a stop, late
 *     (movement and no arrival within 3000ms plus the meter's lag),
 *     unseen (no movement in that time) or lost (the INA3221 stopped
 *     answering, or no PWM edge came within 3000ms of the arm).
 *
 *     Not kept: an IOmcu restart reads 0 throughout. */
enum {
    LINK_SS_CH_MEAN_MA  = 0,   /**< channel n's at LINK_SS_CH_STRIDE * (n-1) */
    LINK_SS_CH_MAX_MA   = 1,
    LINK_SS_CH_MEAN_MV  = 2,
    LINK_SS_CH_MIN_MV   = 3,
    LINK_SS_CH_STRIDE   = 4,
};
#define LINK_SS_CHANNELS 3u
/**
 * @brief Register offsets of the SERVO_SENSE page's window fields, from
 * register 12.
 */
enum {
    LINK_SS_WINDOW       = 12,
    LINK_SS_CH_FLAGS     = 13,
    LINK_SS_CAP_ARM      = 14,
    LINK_SS_CAP_HOLD_MA  = 15,
    LINK_SS_CAP_MOVE_MA  = 16,
    LINK_SS_CAP_BAND_MA  = 17,
    LINK_SS_CAP_STATE    = 18,  /**< read only from here */
    LINK_SS_CAP_SEQ      = 19,
    LINK_SS_CAP_MOVE_T   = 20,  /**< 0.1ms               */
    LINK_SS_CAP_ARRIVE_T = 21,  /**< 0.1ms               */
    LINK_SS_CAP_PEAK_MA  = 22,
    LINK_SS_CAP_MEAN_MA  = 23,
    LINK_SS_CAP_SAMPLES  = 24,
    LINK_SS_COUNT        = 25,
};
/** @brief The registers a capture is armed with, from LINK_SS_CAP_ARM. */
#define LINK_SS_CAP_FRAME 4u

/** @brief CH_FLAGS' bits, for INA3221 channel 1 to 3. */
#define LINK_SS_CH_VALID(ch)   ((uint16_t)(1u << ((unsigned)(ch) - 1u)))
#define LINK_SS_CH_CLIPPED(ch) ((uint16_t)(1u << ((unsigned)(ch) + 3u)))
#define LINK_SS_CAP_CLIPPED    0x80u

/**
 * @brief CAP_ARM: bit 7, the INA3221 channel (1 to 3) and the output channel (0
 * to 7).
 *
 * With bit 11 (4.13) the number in bits 8 to 10 is a programming port of the
 * PROG page, 0 to 3.
 */
#define LINK_SS_ARM            0x80u
#define LINK_SS_ARM_OF(ch, out) \
    ((uint16_t)(LINK_SS_ARM | ((unsigned)(ch) & 0x03u) \
                | (((unsigned)(out) & 0x07u) << 8)))
#define LINK_SS_ARM_CH(r)      ((uint8_t)((r) & 0x03u))
#define LINK_SS_ARM_OUT(r)     ((uint8_t)(((r) >> 8) & 0x07u))
/** @brief Every bit CAP_ARM may carry. */
#define LINK_SS_ARM_BITS       0x0F83u
/** @brief Bits 8 to 10 name a programming port, not an output channel. */
#define LINK_SS_ARM_PORT       0x0800u
/**
 * @brief The one INA3221 channel a capture is taken on; CH2 and CH3 are refused
 * with BAD_VALUE, the field kept for a channel read fast enough later.
 */
#define LINK_SS_CAP_CH         1u

/**
 * @brief The states of a capture, as the capture reports them.
 */
typedef enum {
    LINK_CAP_IDLE      = 0,
    LINK_CAP_ARMED     = 1, /**< waiting for the command to change         */
    LINK_CAP_WAIT_MOVE = 2, /**< the new pulse is out; no movement yet     */
    LINK_CAP_MOVING    = 3,
    LINK_CAP_ARRIVED   = 4, /**< back within the band of the holding level */
    LINK_CAP_AT_STOP   = 5, /**< settled, pushing on an end stop           */
    LINK_CAP_LATE      = 6, /**< movement, and no arrival within 3000ms
                                 plus the meter's lag                      */
    LINK_CAP_UNSEEN    = 7, /**< no movement within 3000ms plus the
                                 meter's lag: neither timed nor late       */
    LINK_CAP_LOST      = 8, /**< the INA3221 stopped answering, or no PWM
                                 edge came within 3000ms of the arm        */
} link_cap_state_t;

/* --- the SERVO_WIN page (protocol 4.11): the INA3221's CH1, the servo
 *     under test, as its last four 50ms windows.  Read only: every write
 *     is refused with READ_ONLY.
 *
 *     SERVO_SENSE shows the last window alone, so a host whose reads lie
 *     further apart than 50ms misses windows.  Here a host takes each
 *     window number once, and misses none while two of its reads lie at
 *     most 200ms apart.
 *
 *     WINDOW is the number of the newest complete window, modulo 65536,
 *     the number SERVO_SENSE's WINDOW shows.  FLAGS bit 0 (LINK_SW_HAVE)
 *     says a window has closed under the set-up in force, so WINDOW is a
 *     number; bit 7 (LINK_SW_CAP_CLIPPED) is SERVO_SENSE's capture bit.
 *     CAP_STATE and CAP_SEQ read as SERVO_SENSE's registers 18 and 19, so
 *     one read shows the windows and whether a capture has ended.
 *
 *     Then four entries of six registers, newest first: entry k, from
 *     register 4 + 6 k, is window number WINDOW - k.  Mean, highest and
 *     lowest current in mA, signed; mean and lowest bus voltage in mV, at
 *     the load side of the shunt; and the entry's flags.  The schedule
 *     reads CH1's current every 1ms and its bus voltage every 24ms: a
 *     window holds up to 50 current samples and 2 or 3 voltage samples.
 *
 *     An entry's flags: bit 15 (LINK_SW_E_CLOSED) a window closed with
 *     this number -- clear for a number the IOmcu skipped because
 *     its tick ran late by more than a window, and for a number before
 *     the first window, and the entry then reads 0 throughout; bit 8
 *     (LINK_SW_E_CURRENT) the window holds current samples; bit 9
 *     (LINK_SW_E_VOLTAGE) it holds voltage samples; bit 10
 *     (LINK_SW_E_CLIP_HI) a current sample read the top of the range,
 *     163.8mV across the shunt; bit 11 (LINK_SW_E_CLIP_LO) one read the
 *     bottom; bits 0..7 the number of samples that did, held at 255.
 *
 *     A clipped sample counts in an entry as the reading it is: the end
 *     of the range, 1.638A or -1.6384A on the 0.1Ohm shunt, in the
 *     mean, the highest and the lowest.  SERVO_SENSE's registers 0 to 11
 *     leave a clipped sample out of their figures, so the two pages
 *     differ for a window with a clipped sample, and only for such a
 *     window.
 *
 *     A window being filled when the INA3221 is found reset (SENSE's
 *     RESETS) is emptied: its entry is closed and holds no samples.
 *
 *     Not kept, and cleared by a SENSE set-up taken. */
enum {
    LINK_SW_WINDOW    = 0,
    LINK_SW_FLAGS     = 1,
    LINK_SW_CAP_STATE = 2,   /**< link_cap_state_t                       */
    LINK_SW_CAP_SEQ   = 3,
    LINK_SW_ENTRIES   = 4,   /**< entry k's registers start at 4 + 6 k   */
    LINK_SW_COUNT     = 28,
};
/** @brief The windows the page carries. */
#define LINK_SW_RING 4u
/** @brief One entry's registers. */
enum {
    LINK_SW_E_MEAN_MA = 0,   /**< signed -- read as int16_t              */
    LINK_SW_E_MAX_MA  = 1,   /**< signed                                 */
    LINK_SW_E_MIN_MA  = 2,   /**< signed                                 */
    LINK_SW_E_MEAN_MV = 3,
    LINK_SW_E_MIN_MV  = 4,
    LINK_SW_E_FLAGS   = 5,
    LINK_SW_E_STRIDE  = 6,
};
/** @brief Register @p r of the entry @p k windows before the newest. */
#define LINK_SW_ENTRY(k, r) \
    ((unsigned)LINK_SW_ENTRIES + (unsigned)(k) * LINK_SW_E_STRIDE + (unsigned)(r))

/** @brief FLAGS' bits. */
#define LINK_SW_HAVE        0x01u
#define LINK_SW_CAP_CLIPPED 0x80u

/** @brief An entry's flags. */
#define LINK_SW_E_CLIPPED(f) ((uint8_t)((f) & 0xFFu))
#define LINK_SW_E_CLIPPED_MAX 255u
#define LINK_SW_E_CURRENT   0x0100u
#define LINK_SW_E_VOLTAGE   0x0200u
#define LINK_SW_E_CLIP_HI   0x0400u
#define LINK_SW_E_CLIP_LO   0x0800u
#define LINK_SW_E_CLOSED    0x8000u

/* --- the SENSE_MAP page (protocol 4.25): up to four INA3221 parts on the
 *     sensor bus, at 0x40 to 0x43, three channels each.  Channel c is
 *     part a's channel ch for c = 3 a + ch - 1, so 0 to 11.
 *
 *     ENABLE bit a: the part at 0x40 + a is fitted.  FAST: the channel
 *     read every 1ms, whose last four windows SERVO_WIN carries and
 *     on which SERVO_SENSE times a move; 0xFF (LINK_SM_FAST_NONE) none.
 *     SHUNT[c]: channel c's shunt in 0.1mOhm, 50 to 10000, 0 = the
 *     channel is off and never read.  WATCH[c]: what channel c measures,
 *     for the panel's screens and logs; the IOmcu keeps it and acts on
 *     nothing in it.
 *
 *     Registers 0 to 25 are kept in the IOmcu's flash.  A write lies
 *     within them: one that reaches register 26 is refused with
 *     READ_ONLY.  The registers written are merged into the map and the
 *     whole map judged; a map refused leaves every register as it was.
 *     Refused with BAD_VALUE: ENABLE above 15; FAST not 0xFF and past 11,
 *     on a part not fitted or on a channel whose SHUNT is 0; a SHUNT not 0
 *     outside 50 to 10000; a WATCH value outside the list below; a fitted
 *     part with all three SHUNT 0; a fitted part on the address of the
 *     INA228 while SENSE enables it; any change while the bank drives.
 *     A part not fitted keeps the values written for it, judged by range
 *     alone.
 *
 *     ID[a] (read only): the die ID part a answered at its last probe, 0
 *     before one.  FLAGS (read only): bit a part a online; bit 4 + a part
 *     a answered with another identity; bit 8 (LINK_SM_IN_FORCE) the map
 *     has been written and is kept; bit 9 (LINK_SM_DERIVED) it has never
 *     been written and is SENSE's one part (below).
 *
 *     SENSE and the map.  Until the map is first written it is derived
 *     from SENSE: ENABLE bit 1 fits the part at SENSE's I3221_ADDR, each
 *     channel in I3221_CHANNELS has SENSE's shunt, FAST is that part's
 *     CH1 when it is read, and nothing is watched.  Once written, SENSE's
 *     ENABLE bit 1 and registers 8 to 10 mirror the map: bit 1 set while
 *     a part is fitted; I3221_ADDR the mirror part -- FAST's part, or
 *     the lowest fitted part while FAST is 0xFF; I3221_SHUNT_DMOHM that
 *     part's CH1 shunt, or its first one not 0; I3221_CHANNELS its
 *     channels with a shunt.  A SENSE write that would change bit 1 or
 *     registers 8 to 10 is then refused with BAD_VALUE.  SERVO_SENSE's
 *     registers 0 to 11 are the mirror part's three channels, and a
 *     capture's channel 1 (LINK_SS_CAP_CH) is FAST. */
enum {
    LINK_SM_ENABLE   = 0,
    LINK_SM_FAST     = 1,
    LINK_SM_SHUNT_0  = 2,    /**< SHUNT[c] at 2 + c                      */
    LINK_SM_WATCH_0  = 14,   /**< WATCH[c] at 14 + c                     */
    LINK_SM_KEPT     = 26,   /**< registers 0 to 25 are kept; read only
                                  from here                              */
    LINK_SM_ID_0     = 26,   /**< ID[a] at 26 + a                        */
    LINK_SM_FLAGS    = 30,
    LINK_SM_RESERVED_31 = 31,
    LINK_SM_COUNT    = 32,
};
#define LINK_SM_PARTS        4u
#define LINK_SM_CHANNELS    12u
#define LINK_SM_SHUNT(c)    ((unsigned)LINK_SM_SHUNT_0 + (unsigned)(c))
#define LINK_SM_WATCH(c)    ((unsigned)LINK_SM_WATCH_0 + (unsigned)(c))
#define LINK_SM_ID(a)       ((unsigned)LINK_SM_ID_0 + (unsigned)(a))
#define LINK_SM_FIT_ALL     0x000Fu
#define LINK_SM_FAST_NONE   0x00FFu
#define LINK_SM_DMOHM_MIN   50u
#define LINK_SM_DMOHM_MAX   10000u
/** @brief WATCH values. */
#define LINK_SM_WATCH_NONE    0x00u
#define LINK_SM_WATCH_OUT(n)  (0x01u + (unsigned)(n))   /**< output channel n, 0..7 */
#define LINK_SM_WATCH_PORT(n) (0x10u + (unsigned)(n))   /**< programming port n, 0..3 */
#define LINK_SM_WATCH_SUPPLY  0x20u     /**< WATCH value: the supply */
/** @brief FLAGS' bits. */
#define LINK_SM_ONLINE(a)    (1u << (unsigned)(a))
#define LINK_SM_ID_WRONG(a)  (1u << (4u + (unsigned)(a)))
#define LINK_SM_IN_FORCE     0x0100u
#define LINK_SM_DERIVED      0x0200u

/* --- the SENSE_WIN_A and SENSE_WIN_B pages (protocol 4.25): every
 *     channel's last complete 50ms window, channels 0 to 5 on A and 6 to
 *     11 on B.  Read only: every write is refused with READ_ONLY.
 *
 *     WINDOW is the window's number, SERVO_SENSE's WINDOW.  FLAGS: bit a
 *     part a online, on both pages; bit 8 (LINK_SX_HAVE) a window has
 *     closed under the set-up in force.  Then six entries of five
 *     registers, entry k from register 2 + 5 k the page's channel k: mean
 *     and highest current in mA, signed; mean and lowest bus voltage in
 *     mV; the entry's flags -- bit 0 current samples, bit 1 voltage
 *     samples, bit 2 a current sample at the top of the range, bit 3 one
 *     at the bottom.  A clipped sample is left out of the figures, as on
 *     SERVO_SENSE.  A channel off, or of a part not online, reads 0
 *     throughout.  Rates: FAST every 1ms; every other channel with a
 *     shunt in turn, one a 1ms tick (1000 / n Hz for n of them, 90.9Hz
 *     with 11); each bus voltage every 24ms.  Not kept. */
enum {
    LINK_SX_WINDOW   = 0,
    LINK_SX_FLAGS    = 1,
    LINK_SX_ENTRIES  = 2,
    LINK_SX_COUNT    = 32,
};
#define LINK_SX_PER_PAGE 6u
/**
 * @brief Register offsets within one entry of the SENSE_WIN_A and SENSE_WIN_B
 * pages.
 */
enum {
    LINK_SX_E_MEAN_MA = 0,   /**< signed -- read as int16_t              */
    LINK_SX_E_MAX_MA  = 1,   /**< signed                                 */
    LINK_SX_E_MEAN_MV = 2,
    LINK_SX_E_MIN_MV  = 3,
    LINK_SX_E_FLAGS   = 4,
    LINK_SX_E_STRIDE  = 5,
};
/** @brief Register @p r of the page's entry @p k (channel 6 * page + k). */
#define LINK_SX_E(k, r) \
    ((unsigned)LINK_SX_ENTRIES + (unsigned)(k) * LINK_SX_E_STRIDE + (unsigned)(r))
#define LINK_SX_HAVE        0x0100u
#define LINK_SX_E_CURRENT   0x0001u
#define LINK_SX_E_VOLTAGE   0x0002u
#define LINK_SX_E_CLIP_HI   0x0004u
#define LINK_SX_E_CLIP_LO   0x0008u

/* --- the SENSE_LIMIT page (protocol 4.27): a current limit for each of
 *     SENSE_MAP's 12 channels, and the windows that passed them.  An
 *     alert only: a limit passed stops nothing and drives nothing.
 *
 *     LIMIT_MA[c] (registers 0 to 11): channel c's limit in mA, 1 to
 *     32767; 0 = none.  Kept in the IOmcu's flash.  A write lies within
 *     them: one that reaches register 12 is refused with READ_ONLY, a
 *     value above 32767 with BAD_VALUE, and a refused write stores none
 *     of its registers.  Taken in any bank state.
 *
 *     Core 1 judges each channel's window once, as it closes
 *     (sense_window_over()): over when its highest current, in mA as
 *     SENSE_WIN shows it, is above the limit, or when a sample read the
 *     top of the range and that top, unrounded, is above the limit.  A
 *     negative current is never over, nor a channel off or of a part not
 *     online at the close.  Core 0 adds what core 1 counted at each
 *     snapshot, so no window is missed however late it takes one.
 *
 *     OVER (register 12, read only): bit c set when a window of channel c
 *     was over since the last read of this register.  A read that covers
 *     register 12 clears it after the reply is filled; a reply lost on
 *     the link loses those bits, which OVER_COUNT still counts.
 *     OVER_COUNT (register 13, read only): channel windows over, modulo
 *     65536, since the IOmcu started.  Registers 14 and 15 are reserved:
 *     they read 0. */
enum {
    LINK_SL_LIMIT_0    = 0,     /**< LIMIT_MA[c] at c                    */
    LINK_SL_KEPT       = 12,    /**< registers 0 to 11 are kept          */
    LINK_SL_OVER       = 12,
    LINK_SL_OVER_COUNT = 13,
    LINK_SL_COUNT      = 16,
};
#define LINK_SL_MA_MAX 32767u

#endif /* RCBENCH_LINK_PAGES_SENSE_H */
