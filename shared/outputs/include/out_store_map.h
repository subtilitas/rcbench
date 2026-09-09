/*
 * Where the next copy of the output binding goes in the coprocessor's flash.
 *
 * The store is a run of record slots laid across two or more flash sectors.
 * A save programs one slot.  A sector is erased only when nothing in it is
 * still wanted, and the erase can be taken before the save that needs it.
 * This file is that arithmetic -- which record is the live one, which slot
 * the next save takes, which sector can be erased now -- with no flash API,
 * no addresses and no clock in it, so the host suite holds it.
 *
 * Why not one record rewritten in place.  Erasing a NOR (not-or) flash
 * sector is one command to the die, and the die cannot be read while it runs;
 * the coprocessor executes from that die, so an erase per save is an erase
 * window per save with the core answering nothing.  On the bring-up module a
 * save that erased and programmed inside one window measured 19,178 us,
 * against a CAN (Controller Area Network) frame time of about 130 us and two
 * receive buffers in the controller.  Neither half of that window was timed
 * on its own: a page program is one page rather than one sector, and
 * out_store_last_program_us() and out_store_last_erase_us() are what will
 * separate them.
 *
 * What a power cut leaves behind:
 *
 *   During a program.  That slot ends up neither erased nor valid, and the
 *   record before it is still the newest valid one, so the load returns the
 *   binding from before the save.  A store of one record rewritten in place
 *   has nothing to return there.
 *
 *   During an erase.  The sector being erased never holds the newest record:
 *   out_store_map_stale() offers a sector only while the live record is in
 *   another one, and out_store_map_write() erases only a sector it is
 *   leaving.  With one sector configured there is nowhere else for the record
 *   to be and that guarantee is gone; the coprocessor configures two.
 *
 * The second guarantee needs a half-erased record to be rejected, and a
 * checksum is not enough for it.  An erase lifts bits towards 0xFF, so a
 * superseded record's sequence number can only grow -- and for the corrupted
 * record to be rejected it has to fail its check, which a 16-bit checksum
 * does with probability 65535/65536 per candidate rather than always.  The
 * number of candidates reachable by setting bits alone is enormous, so that
 * is a likelihood and not a guarantee.
 *
 * out_store_seq_ok() is what makes it one.  The caller stores the sequence
 * number twice, the second time complemented, and an erase cannot keep the
 * pair: a bit gained in one would have to be lost in the other, and losing a
 * bit is what an erase cannot do.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** One record slot, as the caller found it in flash. */
typedef struct {
    bool     erased;  /**< every byte 0xFF: nothing has been written here */
    bool     valid;   /**< magic, version and checksum all agree */
    uint32_t seq;     /**< the record's sequence number; valid records only */
} out_store_rec_t;

/** Where the next save goes. */
typedef struct {
    uint8_t  at;            /**< the record slot to program */
    uint32_t seq;           /**< the sequence number that record carries */
    bool     erase_first;   /**< a sector has to be erased before the write */
    uint8_t  erase_sector;  /**< which one, when erase_first */
} out_store_write_t;

/**
 * The live record: the valid one with the highest sequence number.
 *
 * -1 when none of them is valid, which is a store that has never been
 * written, one written by a build with a different record in it, and one
 * whose only records are torn.  The caller keeps its defaults for all three.
 *
 * The comparison is a plain unsigned one rather than a wrap-safe difference.
 * The sequence starts at 1 and counts saves; a sector pair of 32 slots at
 * 100,000 erase cycles a sector accepts about 3.2 million saves before the
 * part wears out, and a uint32_t counts to 4,294,967,295.
 */
int out_store_map_newest(const out_store_rec_t *recs, uint8_t sectors,
                         uint8_t per_sector);

/**
 * Plan the next save.
 *
 * The slot after the live record, while its sector has an erased slot left;
 * then the first sector that is already erased; then the next sector, which
 * has to be erased first.  A slot that is neither erased nor valid is a torn
 * write and is skipped rather than programmed over: bits only clear, so a
 * second record written into a used slot is the two of them anded together.
 *
 * False when @p out is NULL or the geometry is empty; the store is otherwise
 * always placeable, because a full store erases a sector to make room.
 */
bool out_store_map_write(const out_store_rec_t *recs, uint8_t sectors,
                         uint8_t per_sector, out_store_write_t *out);

/**
 * A sector worth erasing now, ahead of the save that would have to wait for
 * it, or -1 when there is none.
 *
 * A sector qualifies when it does not hold the live record and is not
 * already erased.  Erasing it costs the same window as erasing it later; the
 * point of taking it early is that the caller chooses the moment, and that
 * the save which finds the sector ready costs a page program alone.
 */
int out_store_map_stale(const out_store_rec_t *recs, uint8_t sectors,
                        uint8_t per_sector);

/**
 * Whether a record's sequence pair survived whatever happened to the flash.
 *
 * @p seq_inv is written as ~@p seq.  A flash erase only ever sets bits, so
 * any bit an interrupted erase lifted in either word breaks the pair and
 * cannot be compensated for by a bit lifted in the other: the complement
 * would need a bit cleared, which no erase does.
 *
 * This is a stronger check than the record's checksum and it answers a
 * narrower question.  It says nothing about the configuration -- that is the
 * checksum's -- and everything about whether this record may be ranked
 * against the others.
 */
bool out_store_seq_ok(uint32_t seq, uint32_t seq_inv);
