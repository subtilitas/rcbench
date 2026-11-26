#!/usr/bin/env python3
"""Encode bytes as a literal-only LZMA "alone" stream: the reference.

protocols/supvan/lzlit.c is a port of this file and writes the same bytes;
test/tools/test_lzlit.py holds the two to each other and decodes both with
Python's lzma (liblzma).  A Supvan T50M Pro label printer printed a stream
of this encoder on the station.

The stream codes every byte as a literal: no matches, no end marker.  It
is a 13-byte header (properties 0x5D = lc 3, lp 0, pb 2; dictionary 8,192
bytes, little-endian 32 bits; the exact uncompressed length, little-endian
64 bits) and the range-coded body.  Any LZMA decoder that knows the length
from the header decodes it; with no match the dictionary is never read.

Per byte (LZMA specification, Igor Pavlov):

- is_match[state][pos_state], bit 0.  With literals only the state stays 0.
- the literal coder: 0x300 probabilities per context, the context the
  previous byte >> (8 - lc) (lp 0).  The 8 bits go most significant first
  through a binary tree: the symbol starts at 1, symbol = symbol * 2 + bit.
- probabilities are 11 bits, start at 1,024 and adapt by a shift of 5.

The range coder is the one of LzmaEnc.c: a 33-bit low with carry, a 32-bit
range, a cache byte and its count, normalised below 2^24 and flushed by 5
shifts.

    tools/lzlit.py FILE      encode FILE, decode with lzma, compare

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import lzma
import struct
import sys

LC, LP, PB = 3, 0, 2
DICT_SIZE = 8192
BIT_MODEL_TOTAL_BITS = 11
BIT_MODEL_TOTAL = 1 << BIT_MODEL_TOTAL_BITS
MOVE_BITS = 5
TOP = 1 << 24


class RangeEncoder:
    """The range coder of LzmaEnc.c, writing into a bytearray."""

    def __init__(self):
        """Start with an empty output, the full range and no carry."""
        self.low = 0
        self.range = 0xFFFFFFFF
        self.cache = 0
        self.cache_size = 1
        self.out = bytearray()

    def shift_low(self):
        """Move the top byte of low out, through the carry's cache."""
        if self.low < 0xFF000000 or self.low >= (1 << 32):
            carry = self.low >> 32
            temp = self.cache
            while True:
                self.out.append((temp + carry) & 0xFF)
                temp = 0xFF
                self.cache_size -= 1
                if self.cache_size == 0:
                    break
            self.cache = (self.low >> 24) & 0xFF
        self.cache_size += 1
        self.low = (self.low << 8) & 0xFFFFFFFF

    def encode_bit(self, probs, i, bit):
        """Code one bit with the adaptive probability probs[i].

        Args:
            probs: the probability table, updated.
            i: the index of the probability.
            bit: the bit, 0 or 1.
        """
        p = probs[i]
        bound = (self.range >> BIT_MODEL_TOTAL_BITS) * p
        if bit == 0:
            self.range = bound
            probs[i] = p + ((BIT_MODEL_TOTAL - p) >> MOVE_BITS)
        else:
            self.low += bound
            self.range -= bound
            probs[i] = p - (p >> MOVE_BITS)
        while self.range < TOP:
            self.range = (self.range << 8) & 0xFFFFFFFF
            self.shift_low()

    def flush(self):
        """Write out what low still holds."""
        for _ in range(5):
            self.shift_low()


def encode(data: bytes) -> bytes:
    """Encode data as an LZMA-alone stream of literals only.

    Args:
        data: the bytes.

    Returns:
        The header and the range-coded body.
    """
    rc = RangeEncoder()
    is_match = [BIT_MODEL_TOTAL // 2] * (12 << 4)
    literal = [BIT_MODEL_TOTAL // 2] * (0x300 << (LC + LP))
    prev = 0
    pb_mask = (1 << PB) - 1
    for pos, b in enumerate(data):
        rc.encode_bit(is_match, pos & pb_mask, 0)
        base = 0x300 * (prev >> (8 - LC))
        sym = 1
        for k in range(7, -1, -1):
            bit = (b >> k) & 1
            rc.encode_bit(literal, base + sym, bit)
            sym = (sym << 1) | bit
        prev = b
    rc.flush()
    props = (PB * 5 + LP) * 9 + LC
    header = struct.pack('<BIQ', props, DICT_SIZE, len(data))
    return header + bytes(rc.out)


def main() -> int:
    """Encode the file named on the command line and decode it back.

    Returns:
        0 when lzma decodes the stream to the file's bytes, else 1.
    """
    if len(sys.argv) != 2:
        sys.exit('usage: lzlit.py FILE')
    with open(sys.argv[1], 'rb') as f:
        raw = f.read()
    z = encode(raw)
    back = lzma.decompress(z, format=lzma.FORMAT_ALONE)
    same = back == raw
    print('raw %d B -> %d B, decode %s'
          % (len(raw), len(z), 'OK' if same else 'MISMATCH'))
    return 0 if same else 1


if __name__ == '__main__':
    sys.exit(main())
