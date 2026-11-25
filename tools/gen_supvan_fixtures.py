#!/usr/bin/env python3
"""Write the fixtures of the Supvan label printer's host tests.

The files under test/host/fixtures/supvan/ are built here from rules
written a second time, apart from the C in protocols/supvan/: the literal
encoder of tools/lzlit.py, and the print buffer and data frame as the
station's report and heeen/supvan-cups (MIT) describe them.  test_lzlit.c
and test_supvan_page.c compare the C output with them byte for byte.

| File | Content |
| --- | --- |
| lzlit-empty.bin | the stream of 0 bytes |
| lzlit-one.bin | the stream of one byte 0x00 |
| lzlit-zeros.bin | the stream of 12,288 bytes 0x00 |
| lzlit-pattern.bin | bytes 0 to 255 16 times, then 4,096 bytes 0x00 |
| lzlit-random.bin | 5,000 bytes of lcg_bytes(1) |
| label-bufs.bin | the 3 print buffers of label_bits(), 4,096 bytes each |
| label.lz | their stream |
| label-frames.bin | its data frames, 512 bytes each |

The label files stand in for the station's captures of one label from
bleprint.py --lit (station step H0).  When H0's files arrive they replace
label-bufs.bin, label.lz and label-frames.bin, together with
label_bits() here and in test_supvan_page.c, which then draw the
captured label's bitmap; the C tests stay as they are.

    tools/gen_supvan_fixtures.py            write the files
    tools/gen_supvan_fixtures.py --check    exit 1 when one differs

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys

import lzlit

REPO = pathlib.Path(__file__).resolve().parent.parent
OUT = REPO / 'test' / 'host' / 'fixtures' / 'supvan'

LINE_BYTES = 48
BUF_SIZE = 4096
BUF_HEAD = 14
BUF_COLS = 84
MARGIN = 8
DENSITY = 4
CHUNK = 500
LABEL_COLS = 224


def lcg_bytes(seed: int, n: int) -> bytes:
    """Return n bytes of a 32-bit linear congruential generator.

    x = x * 1103515245 + 12345 modulo 2^32, the byte its top 8 bits; the
    C tests run the same generator.

    Args:
        seed: the start value.
        n: the count.

    Returns:
        The bytes.
    """
    x = seed
    out = bytearray()
    for _ in range(n):
        x = (x * 1103515245 + 12345) & 0xFFFFFFFF
        out.append(x >> 24)
    return bytes(out)


def label_bits() -> bytes:
    """Return the test label's bitmap: 224 columns of 48 bytes.

    Dot (col, row) is black inside the 40mm label (rows 32 to 351) where
    (col * 7 + row * 3) % 23 < 8.  The C test draws the same bitmap.

    Returns:
        The bitmap, column-major, least significant bit first.
    """
    bits = bytearray(LABEL_COLS * LINE_BYTES)
    for col in range(LABEL_COLS):
        for row in range(32, 352):
            if (col * 7 + row * 3) % 23 < 8:
                bits[col * LINE_BYTES + row // 8] |= 1 << (row % 8)
    return bytes(bits)


def print_buffer(bits: bytes, cols: int, index: int) -> bytes:
    """Build print buffer index of an image.

    Args:
        bits: the image, cols * 48 bytes.
        cols: its columns.
        index: the buffer, from 0.

    Returns:
        The 4,096 bytes.
    """
    count = (cols + BUF_COLS - 1) // BUF_COLS
    first = index * BUF_COLS
    n = min(BUF_COLS, cols - first)
    buf = bytearray(BUF_SIZE)
    page = (0x02 if index == 0 else 0) | (0x0C if index == count - 1 else 0)
    buf[2] = page
    buf[3] = DENSITY << 2 | 1 << 6
    buf[4:6] = struct.pack('<H', n)
    buf[6] = LINE_BYTES
    buf[8:10] = struct.pack('<H', MARGIN)
    buf[10:12] = struct.pack('<H', MARGIN)
    buf[12] = DENSITY
    data = bits[first * LINE_BYTES:(first + n) * LINE_BYTES]
    buf[BUF_HEAD:BUF_HEAD + len(data)] = data
    end = BUF_HEAD + len(data)
    total = sum(buf[2:BUF_HEAD])
    total += sum(buf[i] for i in range(255, end, 256))
    buf[0:2] = struct.pack('<H', total & 0xFFFF)
    return bytes(buf)


def data_frames(stream: bytes) -> bytes:
    """Wrap a stream in data frames.

    Args:
        stream: the stream.

    Returns:
        The frames, 512 bytes each.
    """
    count = (len(stream) + CHUNK - 1) // CHUNK
    out = bytearray()
    for i in range(count):
        chunk = stream[i * CHUNK:(i + 1) * CHUNK].ljust(CHUNK, b'\0')
        body = bytes([i, count]) + chunk
        packet = b'\xAA\xBB' + struct.pack('<H', sum(body) & 0xFFFF) + body
        out += b'\x7E\x5A\xFC\x01\x10\x02' + packet
    return bytes(out)


def fixtures() -> dict[str, bytes]:
    """Return every fixture by its file name.

    Returns:
        The files' contents.
    """
    bits = label_bits()
    bufs = b''.join(print_buffer(bits, LABEL_COLS, i) for i in range(3))
    stream = lzlit.encode(bufs)
    return {
        'lzlit-empty.bin': lzlit.encode(b''),
        'lzlit-one.bin': lzlit.encode(b'\0'),
        'lzlit-zeros.bin': lzlit.encode(bytes(12288)),
        'lzlit-pattern.bin': lzlit.encode(bytes(range(256)) * 16
                                          + bytes(4096)),
        'lzlit-random.bin': lzlit.encode(lcg_bytes(1, 5000)),
        'label-bufs.bin': bufs,
        'label.lz': stream,
        'label-frames.bin': data_frames(stream),
    }


def main() -> int:
    """Write the fixtures, or compare them with --check.

    Returns:
        0, or 1 when --check finds a file that differs.
    """
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--check', action='store_true',
                    help='compare instead of writing')
    args = ap.parse_args()
    bad = 0
    for name, body in fixtures().items():
        path = OUT / name
        if args.check:
            if not path.exists() or path.read_bytes() != body:
                print(f'{path.relative_to(REPO)} is out of date',
                      file=sys.stderr)
                bad += 1
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(body)
        print(f'wrote {path.relative_to(REPO)} ({len(body)} bytes)')
    if bad:
        print('Run tools/gen_supvan_fixtures.py.', file=sys.stderr)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
