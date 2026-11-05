/*
 * The BWT901CL's notifications on 1E4D as the owner's phone recorded them
 * (nRF Connect, 2026-11-05, sensor 00:0C:BF:08:19:29 advertising as HC-06).
 *
 * k_cap_20 is one 20-byte notification: the tail of a frame (02 47), a
 * whole 0x53 angle frame (55 53 33 FF 19 FD F0 ED 38 48 4D: roll -1.126°,
 * pitch -4.081°, yaw -25.400°, version word 0x4838, sum 0x54D, low byte
 * 0x4D) and the first 7 bytes of an 0x54 frame.  k_cap_tail_a and
 * k_cap_tail_b are two short notifications of 4 bytes, frame tails.  The
 * report the phone showed: the flags "LE General Discoverable, LE and BR/EDR
 * capable" (0x1A is the byte those words decode from; the byte itself was
 * not recorded), the 16-bit UUID list 0x18F0 and the complete local name
 * HC-06.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BLE_CAPTURE_H
#define BLE_CAPTURE_H

#include <stdint.h>

#include "bleuart.h"

#pragma GCC diagnostic ignored "-Wunused-variable"

/* The 20-byte notification. */
static const uint8_t k_cap_20[20] = {
    0x02, 0x47, 0x55, 0x53, 0x33, 0xFF, 0x19, 0xFD, 0xF0, 0xED,
    0x38, 0x48, 0x4D, 0x55, 0x54, 0x1A, 0xEE, 0x49, 0x24, 0x68,
};
/* Its angle frame starts at this byte. */
#define CAP_ANGLE_AT 2u
/* The two 4-byte tails. */
static const uint8_t k_cap_tail_a[4] = { 0xF1, 0x00, 0x00, 0x37 };
static const uint8_t k_cap_tail_b[4] = { 0x2F, 0x00, 0x00, 0x96 };

/* The angles of the frame, °, and the tolerance the cases take. */
#define CAP_ROLL   (-1.126)
#define CAP_PITCH  (-4.081)
#define CAP_YAW    (-25.400)
#define CAP_VERSION 0x4838u
#define CAP_TOL    0.001

/* The sensor's address, least significant byte first, and its text. */
static const bleu_addr_t k_cap_addr = {
    { 0x29, 0x19, 0x08, 0xBF, 0x0C, 0x00 }, BLEU_ADDR_PUBLIC
};
#define CAP_ADDR_TEXT "00:0C:BF:08:19:29"

/* The report the station heard on the panel (ADV_IND, public address,
 * -82dBm, no scan response), all 31 bytes: flags 0x0A, the complete 16-bit
 * UUID list 0x18F0, and the complete local name HC-06 in a field of 22
 * bytes padded with NULs. */
static const uint8_t k_station_adv[31] = {
    0x02, 0x01, 0x0A,
    0x03, 0x03, 0xF0, 0x18,
    0x17, 0x09, 0x48, 0x43, 0x2D, 0x30, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
#define STATION_RSSI (-82)

/* Its advertising data as the phone showed it: flags, the UUID list, the
 * name. */
static const uint8_t k_cap_adv[] = {
    0x02, 0x01, 0x1A,
    0x03, 0x03, 0xF0, 0x18,
    0x06, 0x09, 'H', 'C', '-', '0', '6',
};

#endif
