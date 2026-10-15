/*
 * Futaba S.BUS2 telemetry slots, read by the sender of the frames.
 *
 * Implements the S.BUS2 protocol of Futaba as a master that reads.  Futaba
 * and S.BUS2 are names or trademarks of their respective owners; they are
 * used here only to say which sensors this module works with (NOTICE,
 * "Trademarks").
 *
 * Futaba publishes no specification of the slots.  The layout below is from
 * Futaba's transmitter and sensor manuals (slot counts, default slots) and
 * from logic-analyser captures of a Futaba receiver and its sensors (slot
 * IDs, slot timing, two value encodings).  README.md lists the sources and
 * marks what is inferred and not measured.
 *
 * An S.BUS2 frame is an S.BUS frame whose footer, byte 24, is 0x04, 0x14,
 * 0x24 or 0x34.  The footer selects a group of 8 of the 32 slots: 0x04 slots
 * 0 to 7, 0x14 slots 8 to 15, 0x24 slots 16 to 23, 0x34 slots 24 to 31.
 * After the frame the sender releases the wire, and every sensor that holds
 * a slot of that group answers in its slot with 3 bytes in the same 8E2
 * format at 100 kbaud: an ID byte and 2 data bytes.
 *
 * Slot k of a group starts nominally SBUS2_SLOT0_US + k x SBUS2_SLOT_US
 * after the end of the footer.  One capture shows the slots about 700 us
 * apart, another source gives 660 us; the windows below take both.
 *
 * The ID byte of slot s (0 to 31) is the 5-bit number s written backwards,
 * times 8, plus 3: slot 1 is 0x83, slot 8 is 0x13.  The IDs of slots 0 to
 * 15 are captured; those of slots 16 to 31 follow the same rule and are not
 * captured.
 *
 * Every function here is pure: no heap, no floating point, no static
 * mutable state.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_SBUS2_H
#define RCBENCH_SBUS2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SBUS2_SLOTS        32u
#define SBUS2_GROUPS       4u
#define SBUS2_GROUP_SLOTS  8u
#define SBUS2_RECORD_BYTES 3u

/** The footers, one per slot group, in the order a sender rotates them. */
#define SBUS2_FOOTER_0 0x04u
#define SBUS2_FOOTER_1 0x14u
#define SBUS2_FOOTER_2 0x24u
#define SBUS2_FOOTER_3 0x34u

/*
 * Slot timing, in us after the end of the footer's last stop bit.
 *
 * SBUS2_SLOT0_US and SBUS2_SLOT_US are the nominal start of slot 0 and the
 * nominal spacing.  A record belongs to the slot its ID names when its first
 * byte starts no more than SBUS2_EARLY_US before that slot's nominal start
 * and less than SBUS2_LATE_US after it.  The two together are 600 us, below
 * the spacing, so no two windows overlap.  At 700 us spacing slot 7 starts
 * 280 us after its nominal start, inside the window.
 */
#define SBUS2_SLOT0_US    2000u
#define SBUS2_SLOT_US     660u
#define SBUS2_EARLY_US    200u
#define SBUS2_LATE_US     400u

/** A byte that starts more than this after the start of the byte before it
 *  begins a new record.  Bytes of one record are 120 us apart. */
#define SBUS2_BYTE_GAP_US 200u

/** How long the sender listens after the footer: slot 7 of a group ends by
 *  7.38 ms at the latest start its window takes. */
#define SBUS2_WINDOW_US   8000u

/** Bytes one window keeps: 8 records of 3, and 8 more for noise. */
#define SBUS2_CAPTURE_MAX 32u

/** A slot's value counts as current for this many windows of its group
 *  after its last record. */
#define SBUS2_LIVE_WINDOWS 3u

/** One byte received in a window. */
typedef struct {
    /** Leading edge of the start bit, ns after the end of the footer. */
    uint32_t t_ns;
    uint8_t  data;
    bool     parity_ok;   /**< even parity holds */
    bool     stop_ok;     /**< the first stop bit is at the idle level */
} sbus2_rx_byte_t;

/** The bytes of one window, in the order they arrived. */
typedef struct {
    sbus2_rx_byte_t byte[SBUS2_CAPTURE_MAX];
    uint8_t n;
    /** More bytes came than byte[] holds, or a byte's time is not one the
     *  receiver can give. */
    bool    overflow;
} sbus2_capture_t;

/** The timing a window is read with; sbus2_timing_default() gives the
 *  values of the SBUS2_* macros. */
typedef struct {
    uint16_t slot0_us;
    uint16_t slot_us;
    uint16_t early_us;
    uint16_t late_us;
    uint16_t byte_gap_us;
} sbus2_timing_t;

/** One slot of a window. */
typedef struct {
    uint8_t  data[2];
    /** Start of the record's first byte, us after the end of the footer. */
    uint16_t t_us;
    bool     taken;
} sbus2_slot_rec_t;

/** What one window held, by slot of the footer's group. */
typedef struct {
    uint8_t group;                          /**< 0 to 3 */
    sbus2_slot_rec_t slot[SBUS2_GROUP_SLOTS];
    uint8_t taken;      /**< records assigned to a slot */
    uint8_t duplicate;  /**< a second record for a slot already taken */
    uint8_t wrong_id;   /**< ID byte of no slot, or of a slot of another group */
    uint8_t early;      /**< started before its slot's window */
    uint8_t late;       /**< started after its slot's window */
    uint8_t short_rec;  /**< fewer than 3 bytes */
    uint8_t bad_byte;   /**< a byte with a parity or stop bit error */
    bool    overflow;   /**< the capture's overflow */
} sbus2_window_t;

/** One slot over many windows. */
typedef struct {
    uint8_t  data[2];   /**< the last record */
    uint16_t t_us;      /**< its start after the footer */
    /** Windows of the slot's group since the last record, up to 255. */
    uint8_t  age;
    bool     valid;     /**< a record has come since the reset */
    uint32_t records;
    /** Windows of the slot's group without a record after a first one. */
    uint32_t missed;
} sbus2_slot_t;

/** Every slot, and the window counters summed. */
typedef struct {
    sbus2_slot_t slot[SBUS2_SLOTS];
    uint32_t windows;
    uint32_t taken;
    uint32_t duplicate;
    uint32_t wrong_id;
    uint32_t early;
    uint32_t late;
    uint32_t short_rec;
    uint32_t bad_byte;
    uint32_t overflow;
} sbus2_table_t;

/** The sensors whose default slots the map knows (Futaba's manuals). */
typedef enum {
    SBUS2_SENSOR_NONE = 0,
    SBUS2_SENSOR_RECEIVER,   /**< slot 0, the receiver's own voltages */
    SBUS2_SENSOR_TEMP,       /**< SBS-01T, SBS-01TE: 1 slot, default 1 */
    SBUS2_SENSOR_RPM,        /**< SBS-01RM, -01RO, -01RB: 1 slot, default 2 */
    SBUS2_SENSOR_ALTITUDE,   /**< SBS-01A: 3 slots, default 3 */
    SBUS2_SENSOR_VOLTAGE,    /**< SBS-01V: 2 slots, default 6 */
    SBUS2_SENSOR_GPS,        /**< SBS-01G: 8 slots, default 8 */
    SBUS2_SENSOR_SERVO,      /**< SBS-01S: 6 slots, default 16 */
    SBUS2_SENSOR_CURRENT,    /**< SBS-01C: 3 slots, default 24 */
    SBUS2_SENSOR_COUNT
} sbus2_sensor_t;

/** How the 2 data bytes of a slot become a value. */
typedef enum {
    /** The two bytes as they came; every slot without a known encoding. */
    SBUS2_DEC_RAW = 0,
    /** The second byte in 0.1 V: slot 0's receiver and external voltage
     *  records.  From 2 captures, 1 source. */
    SBUS2_DEC_VOLT,
    /** The first byte less 100, in degrees Celsius: the temperature sensor.
     *  Inferred from 2 captures, 1 source; the second byte is 0x80 in both
     *  and not read. */
    SBUS2_DEC_TEMP,
    SBUS2_DEC_COUNT
} sbus2_decode_t;

/** One slot of the map. */
typedef struct {
    uint8_t sensor;   /**< sbus2_sensor_t */
    uint8_t part;     /**< which of the sensor's slots, from 0 */
    uint8_t decode;   /**< sbus2_decode_t */
} sbus2_map_entry_t;

typedef struct {
    sbus2_map_entry_t slot[SBUS2_SLOTS];
} sbus2_map_t;

typedef enum {
    SBUS2_UNIT_RAW = 0,      /**< value = first byte x 256 + second byte */
    SBUS2_UNIT_DECIVOLT,     /**< value in 0.1 V */
    SBUS2_UNIT_DEGC,         /**< value in degrees Celsius */
} sbus2_unit_t;

typedef struct {
    uint8_t unit;     /**< sbus2_unit_t */
    int32_t value;
    uint8_t raw[2];   /**< the data bytes, always */
} sbus2_value_t;

/** Longest text sbus2_value_text() writes, with its NUL. */
#define SBUS2_VALUE_TEXT 16u

/** The footer of slot group @p group (taken modulo 4). */
uint8_t sbus2_footer(unsigned group);

/** The slot group a footer selects, 0 to 3, or -1 for a byte that is no
 *  S.BUS2 footer. */
int sbus2_footer_group(uint8_t footer);

/** The ID byte of slot @p slot (taken modulo 32). */
uint8_t sbus2_slot_id(unsigned slot);

/** The slot an ID byte names, 0 to 31, or -1 for a byte that names none
 *  (its low 3 bits are not 011). */
int sbus2_id_slot(uint8_t id);

void sbus2_timing_default(sbus2_timing_t *t);

/** True when the windows of @p t do not overlap and slot 7's window ends
 *  inside SBUS2_WINDOW_US. */
bool sbus2_timing_ok(const sbus2_timing_t *t);

/**
 * Cut the bytes of @p cap into records and assign each to its slot.
 *
 * A record is up to 3 bytes; a byte that starts more than byte_gap_us after
 * the one before it starts a new record.  A record of 3 bytes, each with
 * its parity and stop bit, whose ID names a slot of @p footer's group and
 * whose first byte starts in that slot's window, is taken; the first such
 * record of a slot is kept.  Every other record is counted by its reason, in
 * this order: short, bad byte, wrong ID, early, late, duplicate.
 *
 * @p t NULL reads with the defaults.  False, with @p out cleared, when
 * @p footer is no S.BUS2 footer, @p t is not sbus2_timing_ok() or @p cap or
 * @p out is NULL.
 */
bool sbus2_window_decode(const sbus2_timing_t *t, uint8_t footer,
                         const sbus2_capture_t *cap, sbus2_window_t *out);

void sbus2_table_reset(sbus2_table_t *tab);

/** Take a window into the table: its records, the age of every slot of its
 *  group and the counters. */
void sbus2_table_update(sbus2_table_t *tab, const sbus2_window_t *win);

/** A record has come for @p slot within the last SBUS2_LIVE_WINDOWS windows
 *  of its group. */
bool sbus2_slot_live(const sbus2_table_t *tab, unsigned slot);

/** How many slots @p sensor takes; 0 for SBUS2_SENSOR_NONE and an unknown
 *  value. */
unsigned sbus2_sensor_slots(unsigned sensor);

/** Every sensor at its Futaba default slot, with the encodings above where
 *  one is known and SBUS2_DEC_RAW elsewhere.  Slots 22, 23 and 27 to 31 are
 *  SBUS2_SENSOR_NONE. */
void sbus2_map_default(sbus2_map_t *map);

/** Set one slot of the map.  False, with the map unchanged, for a slot above
 *  31, an unknown sensor or encoding, or a part the sensor does not have
 *  (SBUS2_SENSOR_NONE takes part 0). */
bool sbus2_map_set(sbus2_map_t *map, unsigned slot, unsigned sensor,
                   unsigned part, unsigned decode);

/** The value of @p data under encoding @p decode; an unknown encoding reads
 *  as SBUS2_DEC_RAW. */
void sbus2_value(unsigned decode, const uint8_t *data, sbus2_value_t *out);

/**
 * @p v as text in @p buf: "5.1 V", "-12 C", or for a raw value the two bytes
 * as upper-case hexadecimal, "C0 33".  ASCII only.  Returns the length
 * written without the NUL; 0, with an empty string where @p len allows one,
 * when @p len is below SBUS2_VALUE_TEXT.
 */
size_t sbus2_value_text(const sbus2_value_t *v, char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_SBUS2_H */
