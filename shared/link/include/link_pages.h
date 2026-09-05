/*
 * The page map: what the panel can ask the coprocessor for, and set.
 *
 * A page is a window of up to LINK_MAX_REGS 16-bit registers with one
 * function.  Adding a capability adds a page, not a message type, which
 * keeps the wire format stable while the bench grows.
 *
 * Pages are numbered with room between the groups.  A page number is part of
 * the contract between two firmwares that are flashed separately and can
 * disagree about their versions, so renumbering an existing page is a
 * breaking change; the gaps let a new page land beside related ones.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_LINK_PAGES_H
#define RCBENCH_LINK_PAGES_H

#include "link_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LINK_PAGE_IDENTITY = 0x00, /**< who and what, read-only      */
    LINK_PAGE_STATUS   = 0x01, /**< how it is, read-only         */
    LINK_PAGE_CONTROL  = 0x10, /**< what to do                   */
    LINK_PAGE_LIMITS   = 0x11, /**< when to stop without asking  */
    LINK_PAGE_FAILSAFE = 0x12, /**< where to go when nobody asks */
    LINK_PAGE_CHANNELS = 0x13, /**< what every output is asked for */
    LINK_PAGE_BENCH    = 0x20, /**< the numbers, read-only       */
    /*
     * 0x21 is reserved and not assigned.  A page number is part of the
     * contract between two firmwares flashed separately; a number that
     * changes meaning lets an old panel write into a page the coprocessor
     * interprets differently, and be acknowledged.
     */
    LINK_PAGE_OUTPUTS  = 0x22, /**< which driver drives what, on which pin */
    LINK_PAGE_CHAN_CFG = 0x23, /**< what each channel is and how it moves  */
    LINK_PAGE_CATALOGUE = 0x24, /**< the board's own pins, read-only       */
    LINK_PAGE_SHAPE     = 0x25, /**< where those pins are, read-only       */
    LINK_PAGE_ARTWORK   = 0x26, /**< what a picture of the board is        */
    LINK_PAGE_ART_DATA  = 0x27, /**< and the picture itself, a block at a time */
    LINK_PAGE_PADS      = 0x28, /**< the pads that are not pins, read-only  */
} link_page_id_t;

/*
 * The protocol version is register 0 of page 0: the one thing a host must be
 * able to read from a coprocessor whose other pages it might not understand.
 * Bump the major when a register changes meaning or a page is renumbered;
 * bump the minor when a page or a register is added at the end, which an
 * older host can ignore.
 */
#define LINK_PROTOCOL_MAJOR 2u
#define LINK_PROTOCOL_MINOR 5u

/* ----------------------------------------------------------------- outputs */

/*
 * The three output pages.  Every output protocol has a proportion of travel
 * and a driver that renders it, so that is what the wire carries:
 *
 *   CHANNELS  what each output is asked for, and nothing else.  Hot: written
 *             many times a second, and the only page in the group that is.
 *   CHAN_CFG  what a channel is (throttle or surface), how fast it may move,
 *             and the pulse widths its ends correspond to.
 *   OUTPUTS   which driver renders which channels, on which pin, how often.
 *
 * Eight of each.  At four registers per slot, eight slots fit a page of
 * LINK_MAX_REGS registers; eight is also the number of servo outputs the
 * bench has and the number of channels PPM (pulse-position modulation)
 * carries.  A ninth slot needs a minor version and a second page.
 */

#define LINK_OUT_SLOTS     8u
#define LINK_OUT_CHANNELS  8u

/** A command runs 0..LINK_CH_SPAN across the channel's own travel. */
#define LINK_CH_SPAN    1000u

/* --- what each output is asked for */
#define LINK_CH_COUNT   LINK_OUT_CHANNELS

/* --- what a channel is.  Four registers each, and a channel must be written
 *     whole: min and max are a pair, and a range half-applied is a range
 *     nothing is clamped against. */
enum {
    LINK_CC_ROLE    = 0, /**< 0 throttle, 1 surface                     */
    LINK_CC_SLEW    = 1, /**< span a second; 0 means at once            */
    LINK_CC_MIN_US  = 2, /**< the pulse a command of 0 renders as       */
    LINK_CC_MAX_US  = 3, /**< and of LINK_CH_SPAN                       */
    LINK_CC_STRIDE  = 4,
};
#define LINK_CC_COUNT  (LINK_OUT_CHANNELS * LINK_CC_STRIDE)

#define LINK_CC_ROLE_THROTTLE 0u
#define LINK_CC_ROLE_SURFACE  1u

/*
 * A channel's range before anything is written, and the widest range the
 * coprocessor accepts for the limits themselves: a servo asked for 400 us
 * buzzes rather than moves.
 *
 * The limits are enforced at the coprocessor, the end holding the wire, so a
 * restarted, reflashed or wrong host cannot drive a servo into its stops.
 */
#define LINK_CC_DEFAULT_MIN 1000u
#define LINK_CC_DEFAULT_MAX 2000u
#define LINK_CC_FLOOR_US     500u
#define LINK_CC_CEILING_US  2500u

/* --- which driver drives what.  Four registers a slot, written whole for the
 *     same reason: a driver claimed on a pin the rest of the write has not
 *     arrived to name yet is a driver on the wrong pin. */
enum {
    LINK_OS_DRIVER  = 0, /**< link_out_driver_t                          */
    LINK_OS_PIN     = 1, /**< which pin it drives                        */
    /*
     * First channel and count in one register: the run of channels this slot
     * renders.  PPM is eight channels on one pin, so a slot cannot be assumed
     * to be one channel.
     */
    LINK_OS_RANGE   = 2, /**< (first << 8) | count                       */
    LINK_OS_RATE_HZ = 3, /**< frames a second, or kbit/s for DShot       */
    LINK_OS_STRIDE  = 4,
};
#define LINK_OS_COUNT  (LINK_OUT_SLOTS * LINK_OS_STRIDE)

#define LINK_OS_RANGE_OF(first, count) \
    ((uint16_t)((((unsigned)(first) & 0xFFu) << 8) | ((unsigned)(count) & 0xFFu)))
#define LINK_OS_FIRST(range) ((uint8_t)((range) >> 8))
#define LINK_OS_CHANNELS(range) ((uint8_t)((range) & 0xFFu))

/*
 * The drivers, numbered on the wire.  A contract like a page number: nothing
 * is renumbered, and a new protocol appends.
 */
typedef enum {
    LINK_DRIVER_NONE        = 0,
    LINK_DRIVER_PWM         = 1,
    LINK_DRIVER_PPM         = 2,
    LINK_DRIVER_DSHOT       = 3,
    /*
     * Bidirectional DShot is its own driver rather than a flag on the one
     * above.  It inverts the line and the checksum, so an ESC (electronic
     * speed controller) set up for one protocol ignores the other entirely;
     * a slot that could be switched between them by a flag would look like
     * one output with a setting instead of two incompatible wires.
     */
    LINK_DRIVER_DSHOT_BIDIR = 4,
} link_out_driver_t;

/* ---------------------------------------------------------------- identity */
enum {
    LINK_ID_PROTOCOL_MAJOR = 0,
    LINK_ID_PROTOCOL_MINOR = 1,
    LINK_ID_FIRMWARE_MAJOR = 2,
    LINK_ID_FIRMWARE_MINOR = 3,
    LINK_ID_FIRMWARE_PATCH = 4,
    LINK_ID_HARDWARE       = 5, /**< board revision                        */
    LINK_ID_CAPABILITIES   = 6, /**< bitmap: which optional pages are real */
    LINK_ID_COUNT          = 7,
};

/*
 * What the coprocessor has fitted, as opposed to what the panel has screens
 * for.  The menu marks derive from this bitmap, so a mark disappears when the
 * part is fitted.
 *
 * Absent is not forbidden.  A screen whose capability is missing opens and
 * runs from the model, marked MODELLED.
 */
typedef enum {
    LINK_CAP_ESC_DRIVE   = 1u << 0, /**< a signal line out to an ESC       */
    LINK_CAP_ESC_TELEM   = 1u << 1, /**< telemetry back from one           */
    LINK_CAP_SERVO_PWM   = 1u << 2, /**< pulses out to a servo             */
    LINK_CAP_SERVO_SENSE = 1u << 3, /**< current, per output               */
    LINK_CAP_PACK_SENSE  = 1u << 4, /**< pack volts and amps               */
    LINK_CAP_RECEIVER    = 1u << 5, /**< a receiver bus decoded in PIO     */
    LINK_CAP_VIBRATION   = 1u << 6, /**< accelerometer and an index pulse  */
    LINK_CAP_CELLS       = 1u << 7, /**< a cell monitor on the balance lead*/
    LINK_CAP_PROGRAM     = 1u << 8, /**< one-wire and text-CLI programming */
} link_cap_t;

/* ------------------------------------------------------------------ status */
enum {
    LINK_ST_STATE      = 0, /**< a link_dev_state_t                      */
    LINK_ST_FAULTS     = 1, /**< a bitmap of link_fault_t                */
    LINK_ST_UPTIME_MS_LO = 2,
    LINK_ST_UPTIME_MS_HI = 3,
    LINK_ST_FRAMES_LO  = 4, /**< frames accepted, for a link that is only */
    LINK_ST_FRAMES_HI  = 5, /**<   sometimes wrong                        */
    LINK_ST_CRC_ERRORS = 6,
    LINK_ST_RESYNCS    = 7,
    LINK_ST_COUNT      = 8,
};

/**
 * What the coprocessor did on its own authority.  Sticky until read and
 * explicitly cleared: a fault that lasted 4 ms is still the reason the motor
 * stopped.
 */
typedef enum {
    LINK_FAULT_NONE          = 0,
    LINK_FAULT_LINK_SILENT   = 1u << 0,
    LINK_FAULT_OVERCURRENT   = 1u << 1,
    LINK_FAULT_OVERTEMP      = 1u << 2,
    LINK_FAULT_STALL         = 1u << 3,
    LINK_FAULT_HEARTBEAT     = 1u << 4, /**< the safety line stopped edging */
    LINK_FAULT_VERSION       = 1u << 5, /**< the two ends disagree          */
} link_fault_t;

typedef enum {
    LINK_STATE_IDLE     = 0, /**< alive, outputs off            */
    LINK_STATE_ARMED    = 1, /**< outputs live                  */
    LINK_STATE_FAILSAFE = 2, /**< acted on its own authority    */
} link_dev_state_t;

/* ------------------------------------------------------------------- bench */
/*
 * The numbers, read-only.
 *
 * Fixed-point rather than floats: a register is 16 bits, and the scale is
 * part of the contract between the two firmwares.
 *
 * The peaks are tracked here rather than on the panel: the coprocessor has
 * the fast samples and the panel sees one poll in fifty of them.
 */
enum {
    LINK_BN_VOLTAGE_CV   = 0,  /**< 10 mV steps, 0..655.35 V              */
    LINK_BN_CURRENT_CA   = 1,  /**< 10 mA steps, 0..655.35 A              */
    LINK_BN_POWER_W      = 2,  /**< watts                                  */
    LINK_BN_RPM          = 3,
    LINK_BN_TEMP_ESC_DC  = 4,  /**< 0.1 C, signed -- read as int16_t       */
    LINK_BN_TEMP_MOT_DC  = 5,  /**< 0.1 C, signed                          */
    LINK_BN_CHARGE_MAH   = 6,  /**< accumulated by the current monitor     */
    LINK_BN_ENERGY_DWH   = 7,  /**< 0.1 Wh                                 */
    LINK_BN_VOLT_MIN_CV  = 8,  /**< sag: the lowest the bus went           */
    LINK_BN_CURR_MAX_CA  = 9,
    LINK_BN_POWER_MAX_W  = 10,
    LINK_BN_RPM_MAX      = 11,
    LINK_BN_FLAGS        = 12, /**< a bitmap of link_bench_flag_t          */
    LINK_BN_COUNT        = 13,
};

/** What the coprocessor says about the numbers it is sending. */
typedef enum {
    LINK_BN_VOLTAGE_OK = 1u << 0, /**< a sensor answered; else the field is 0 */
    LINK_BN_CURRENT_OK = 1u << 1,
    LINK_BN_RPM_OK     = 1u << 2,
    LINK_BN_TEMP_OK    = 1u << 3,
    /**
     * The numbers are modelled, not measured.  Set by a coprocessor running
     * without a front end and by the panel's own simulator; the panel draws
     * SIMULATION across the screen either way.
     */
    LINK_BN_SIMULATED  = 1u << 7,
} link_bench_flag_t;

/* --------------------------------------------------------------- catalogue */

/*
 * The board's own pins: which GPIOs it brings out, the pad number printed
 * beside each, and what already holds the ones an output may not have.
 *
 * A board describes itself so a panel that has never heard of it can still
 * offer the right pins.  What it cannot do is make a pin safe: the
 * coprocessor reserves its own set at its own end whatever it says here, so
 * a page that lies costs a pin rather than the safety line.
 *
 * One register per pin and no count register.  A count would have to live in
 * the identity page, and lengthening that page breaks every coprocessor
 * built before it: the panel asks for LINK_ID_COUNT registers, and a device
 * whose identity page is shorter refuses the read rather than returning what
 * it has -- which would leave the link down instead of degraded.  Instead a
 * pad number of zero means there is no pin in that slot, since pads are
 * numbered from one.
 *
 * A coprocessor built before this page answers NACK with LINK_NACK_BAD_PAGE,
 * which is the panel's cue to use the catalogue compiled into it.
 */
#define LINK_CAT_PINS  32u
#define LINK_CAT_COUNT LINK_CAT_PINS

/** gpio in 6 bits, pad in 6, and what holds it in 4. */
#define LINK_CAT_OF(gpio, pad, hold) \
    ((uint16_t)((((unsigned)(gpio) & 0x3Fu) << 10) \
                | (((unsigned)(pad) & 0x3Fu) << 4) \
                | ((unsigned)(hold) & 0x0Fu)))
#define LINK_CAT_GPIO(r) ((uint8_t)(((r) >> 10) & 0x3Fu))
#define LINK_CAT_PAD(r)  ((uint8_t)(((r) >> 4) & 0x3Fu))
#define LINK_CAT_HOLD(r) ((uint8_t)((r) & 0x0Fu))

/**
 * What holds a pin the bench may not drive.
 *
 * A code rather than a name: a name is a string and a register is sixteen
 * bits.  The panel prints its own words for these, so a board it knows shows
 * the exact signal -- "CAN CS" rather than "CAN" -- and a board it has only
 * been told about shows the group.
 */
typedef enum {
    LINK_PIN_FREE      = 0,  /**< an output may have it                    */
    LINK_PIN_HEARTBEAT = 1,  /**< the safety line                          */
    LINK_PIN_CAN       = 2,  /**< the link to the panel                    */
    LINK_PIN_FLASH     = 3,
    LINK_PIN_DEBUG     = 4,
    LINK_PIN_SENSOR    = 5,
    LINK_PIN_OTHER     = 15, /**< spoken for, and this build has no word   */
} link_pin_hold_t;

/* ------------------------------------------------------------------- shape */

/*
 * Where the pins are, as opposed to which they are.
 *
 * The catalogue page names a pad and the pin on it; it does not say where
 * that pad sits.  A picture of the board is the one place a pin number has
 * to become a position, and a picture drawn from a guessed shape points at
 * the wrong pad with the same confidence as the right one -- so the board
 * says its shape rather than the panel assuming one.
 *
 * A coprocessor that does not serve this page gets no drawn board.  That is
 * a screen the operator does not get, not a bench that does not work: the
 * outputs grid offers the same pins from the catalogue alone.
 *
 * Two rows on one pitch, which is every 0.1-inch header board.  Pad 1 sits
 * at LINK_SH_CORNER and the numbers run away from it along that edge, then
 * back along the opposite one -- the DIP (dual in-line package) convention,
 * and the one the Pico form factor follows.
 *
 * Hundredths of a millimetre throughout: a register is sixteen bits, which
 * reaches 655.35 mm, and 0.01 mm is finer than any board is placed to.
 */
enum {
    LINK_SH_WIDTH_CMM  = 0, /**< outline, along the pad rows              */
    LINK_SH_HEIGHT_CMM = 1, /**< outline, across them                     */
    LINK_SH_LAYOUT     = 2, /**< (corner << 8) | pads in one row          */
    LINK_SH_PITCH_CMM  = 3, /**< centre to centre, 254 for 0.1 inch       */
    /**
     * Edge to the centre of the pad row.  The outline's own edge, not a
     * photograph's: artwork carries its own calibration because a photo is
     * not cropped to the outline.
     */
    LINK_SH_INSET_CMM  = 4,
    LINK_SH_COUNT      = 5,
};

#define LINK_SH_LAYOUT_OF(corner, per_side) \
    ((uint16_t)((((unsigned)(corner) & 0xFFu) << 8) \
                | ((unsigned)(per_side) & 0xFFu)))
#define LINK_SH_CORNER(r)   ((uint8_t)(((r) >> 8) & 0xFFu))
#define LINK_SH_PER_SIDE(r) ((uint8_t)((r) & 0xFFu))

/** Which corner of the outline pad 1 sits at, seen with the board as drawn. */
typedef enum {
    LINK_SH_BOTTOM_LEFT  = 0,
    LINK_SH_BOTTOM_RIGHT = 1,
    LINK_SH_TOP_LEFT     = 2,
    LINK_SH_TOP_RIGHT    = 3,
} link_shape_corner_t;

/* ----------------------------------------------------------------- artwork */

/*
 * A photograph of the board, so the picker shows the thing in the
 * operator's hands rather than an outline of it.
 *
 * Two pages because a page is thirty-two registers and no more.  The
 * picture is two hundred kilobytes, which is three thousand-odd reads
 * however it is cut, so every register the metadata takes out of the data
 * page costs another sixty round trips.  Metadata here, payload next door,
 * and a block stays sixty-two bytes.
 *
 * It is transferred once and kept.  At 1 Mbit/s a block is a write to say
 * which one and a read to fetch it, about 1.4 ms the pair, so the whole
 * picture is roughly ten seconds of link -- fine once for a board that has
 * never been seen, and absurd every time the link comes up.
 *
 * LINK_AW_BLOCKS of zero means this coprocessor carries no picture of
 * itself.  That is the ordinary answer, not a fault: a board is drawn from
 * its shape page when there is no photograph, and used from its catalogue
 * when there is no shape.
 */
enum {
    LINK_AW_BLOCKS   = 0, /**< blocks of payload; 0 when there is none    */
    LINK_AW_WIDTH    = 1, /**< pixels                                     */
    LINK_AW_HEIGHT   = 2, /**< pixels                                     */
    LINK_AW_FORMAT   = 3, /**< a link_art_format_t                        */
    LINK_AW_BYTES_LO = 4, /**< payload length, low half                   */
    LINK_AW_BYTES_HI = 5,
    /**
     * link_crc() over the whole payload, seeded zero.  The transfer is long
     * enough that a block lost and silently skipped would otherwise be
     * found by an operator looking at a corrupted board photograph months
     * later, and cached in flash until then.
     */
    LINK_AW_CRC      = 6,
    LINK_AW_COUNT    = 7,
};

typedef enum {
    LINK_ART_NONE   = 0,
    LINK_ART_RGB565 = 1, /**< two bytes a pixel, low byte first           */
} link_art_format_t;

/*
 * The payload, a block at a time: write LINK_AD_BLOCK to say which, then
 * read the page.  Two transactions rather than one because a block that
 * advanced on being read would resend nothing after a reply went missing,
 * and the host would assemble a picture with a hole in it.
 *
 * Bytes little-endian within a register, the low byte first, which is the
 * order an RGB565 framebuffer is already in on both parts.  A final block
 * carrying an odd count leaves the high byte of its last register unused.
 */
enum {
    LINK_AD_BLOCK = 0,            /**< read and write: which block follows */
    LINK_AD_DATA  = 1,            /**< the payload starts here             */
    LINK_AD_COUNT = LINK_MAX_REGS,
};
#define LINK_AD_WORDS ((unsigned)(LINK_AD_COUNT - LINK_AD_DATA))
#define LINK_AD_BYTES (LINK_AD_WORDS * 2u)

/* -------------------------------------------------------------------- pads */

/*
 * The pads that are not pins: grounds, rails, and the few that are neither.
 *
 * A servo lead has three wires and the catalogue describes one of them. An
 * operator who has found where the signal goes still has to find a ground for
 * the return and a rail for the positive, and counting pads on a board to do
 * it is how a lead goes to the wrong place.
 *
 * Separate from the catalogue because they do not fit in it: a page is
 * thirty-two registers, the catalogue is one per pin an output may have, and
 * a forty-pad board has more pads than that between the two of them.
 *
 * A coprocessor that does not serve this page has its grounds and rails
 * unmarked, which is where the picker started. It is a lead placed by
 * reading the board instead of the screen, not a bench that does not work.
 */
#define LINK_PAD_SLOTS 32u
#define LINK_PAD_COUNT LINK_PAD_SLOTS

/** pad in 6 bits, what it is in 2, and its rail in 8 tenths of a volt. */
#define LINK_PAD_OF(pad, kind, dv) \
    ((uint16_t)((((unsigned)(pad) & 0x3Fu) << 10) \
                | (((unsigned)(kind) & 0x03u) << 8) \
                | ((unsigned)(dv) & 0xFFu)))
#define LINK_PAD_NUM(r)  ((uint8_t)(((r) >> 10) & 0x3Fu))
#define LINK_PAD_KIND(r) ((uint8_t)(((r) >> 8) & 0x03u))
#define LINK_PAD_DV(r)   ((uint8_t)((r) & 0xFFu))

/**
 * What a pad that is not a pin is.
 *
 * Tenths of a volt reach 25.5 V, which covers every rail a logic board
 * brings to a header. Zero on a power pad means the rail is not a fixed
 * voltage -- an input like VSYS follows whatever feeds it -- and the screen
 * says so rather than printing a number that is only sometimes true.
 */
typedef enum {
    LINK_PAD_NONE   = 0, /**< no pad in this slot; the list ends here      */
    LINK_PAD_GROUND = 1, /**< 0 V, and what a servo lead returns through   */
    LINK_PAD_POWER  = 2, /**< a rail, at LINK_PAD_DV tenths of a volt      */
    LINK_PAD_OTHER  = 3, /**< neither: a RUN or an enable                  */
} link_pad_kind_t;

/* ----------------------------------------------------------------- control */
enum {
    LINK_CT_ARM        = 0, /**< non-zero asks for ARMED                    */
    LINK_CT_THROTTLE   = 1, /**< 0..10000, hundredths of a percent          */
    LINK_CT_CLEAR      = 2, /**< write LINK_CLEAR_MAGIC to leave FAILSAFE   */
    /*
     * The magnet count of the motor under test, so an electrical speed can
     * be turned into a mechanical one.  It is the one number bidirectional
     * DShot cannot carry: an ESC (electronic speed controller) reports
     * electrical periods and has no idea what it is bolted to.  Zero means
     * nobody has said, and rpm (revolutions per minute) is then not reported
     * rather than reported wrong.
     */
    LINK_CT_MOTOR_POLES = 3,
    LINK_CT_COUNT      = 4,
};

/** Even, and between these; zero is the fourth legal value, meaning unknown. */
#define LINK_POLES_MIN  2u
#define LINK_POLES_MAX 42u

/**
 * Leaving failsafe takes this value written to LINK_CT_CLEAR, not any write
 * and not the link coming back: a link that recovers must not re-arm a bench
 * on its own.
 */
#define LINK_CLEAR_MAGIC 0x5AFEu

/** Throttle is hundredths of a percent: 0..10000 inclusive. */
#define LINK_THROTTLE_MAX 10000u

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_PAGES_H */
