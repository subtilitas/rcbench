/*
 * CRC-16/CCITT-FALSE for the panel-to-coprocessor link.
 *
 * Why sixteen bits and not IOMCU's eight: this link carries coprocessor
 * firmware images as well as register traffic, and an eight-bit residue over a
 * multi-kilobyte image is not a check, it is a formality.
 *
 * Why CCITT-FALSE specifically: poly 0x1021, init 0xFFFF, no reflection, no
 * final xor.  It is the variant with an unambiguous published check value
 * (0x29B1 over the ASCII string "123456789"), which means an implementation on
 * the other end of the wire -- written by somebody else, in another language,
 * years from now -- can be verified against one number before it is trusted.
 */
#ifndef RCBENCH_LINK_CRC_H
#define RCBENCH_LINK_CRC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The value a fresh accumulator starts at. */
#define LINK_CRC_INIT 0xFFFFu

/* The published check value: link_crc(LINK_CRC_INIT, "123456789", 9). */
#define LINK_CRC_CHECK 0x29B1u

/*
 * Fold `len` bytes into `crc` and return the new accumulator.  Incremental by
 * construction, so a receiver can check a frame as it arrives rather than
 * buffering it first -- which is what lets the coprocessor drop a corrupt
 * frame without ever having had room for it.
 */
uint16_t link_crc(uint16_t crc, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_CRC_H */
