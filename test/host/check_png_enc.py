#!/usr/bin/env python3
"""Decode the pictures test_png_enc wrote and hold each pixel to its pattern.

Python's zlib inflates the stream, so nothing of the encoder is shared with
the check.  Every CRC-32, the chunk order, the zlib Adler-32 (zlib checks it)
and the filter bytes are read, and each pixel must be the RGB565 pattern with
its bits replicated to 8.

Usage: check_png_enc.py <folder>
"""

import pathlib
import re
import struct
import sys
import zlib

SIG = b"\x89PNG\r\n\x1a\n"


def chunks(data):
    """Yield (type, payload), checking each CRC-32."""
    assert data[:8] == SIG, "signature"
    at = 8
    while at < len(data):
        n = struct.unpack(">I", data[at:at + 4])[0]
        kind = data[at + 4:at + 8]
        body = data[at + 8:at + 8 + n]
        crc = struct.unpack(">I", data[at + 8 + n:at + 12 + n])[0]
        assert crc == zlib.crc32(kind + body), f"CRC of {kind}"
        yield kind, body
        at += 12 + n


def decode(data):
    """Return width, height and the rows of RGB bytes, unfiltered."""
    kinds = []
    idat = b""
    width = height = 0
    for kind, body in chunks(data):
        kinds.append(kind)
        if kind == b"IHDR":
            width, height, depth, colour, comp, flt, lace = struct.unpack(
                ">IIBBBBB", body)
            assert (depth, colour, comp, flt, lace) == (8, 2, 0, 0, 0), body
        elif kind == b"IDAT":
            idat += body
    assert kinds[0] == b"IHDR" and kinds[-1] == b"IEND", kinds[:2]
    assert set(kinds[1:-1]) == {b"IDAT"}, kinds
    raw = zlib.decompress(idat)
    stride = width * 3
    assert len(raw) == height * (1 + stride), "stream length"
    rows = []
    prev = bytes(stride)
    for y in range(height):
        at = y * (1 + stride)
        kind = raw[at]
        row = bytearray(raw[at + 1:at + 1 + stride])
        assert kind in (0, 1, 2), f"filter {kind}"
        for i in range(stride):
            if kind == 1 and i >= 3:
                row[i] = (row[i] + row[i - 3]) & 255
            elif kind == 2:
                row[i] = (row[i] + prev[i]) & 255
        prev = bytes(row)
        rows.append(prev)
    return width, height, rows


def pattern(kind, x, y):
    """The pixel test_png_enc.c draws."""
    if kind == 0:
        return 0x1234
    if kind == 1:
        return (x * 7 + y * 11) & 0xFFFF
    if kind == 2:
        return ((((x * 2654435761) & 0xFFFFFFFF)
                 ^ ((y * 40503) & 0xFFFFFFFF)) >> 13) & 0xFFFF
    return 0xF800 if ((x // 37) ^ (y // 19)) & 1 else 0x07E0


def rgb(v):
    """RGB565 to the bytes the file must hold."""
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))


def main():
    """Check every picture in the folder."""
    files = sorted(pathlib.Path(sys.argv[1]).glob("p*_*_*.png"))
    assert len(files) == 16, f"{len(files)} pictures"
    for path in files:
        kind, want_w, want_h = map(int, re.match(r"p(\d)_(\d+)_(\d+)",
                                                 path.name).groups())
        width, height, rows = decode(path.read_bytes())
        assert (width, height) == (want_w, want_h), path.name
        for y, row in enumerate(rows):
            want = b"".join(rgb(pattern(kind, x, y)) for x in range(width))
            assert row == want, f"{path.name} row {y}"
    print(f"{len(files)} pictures byte-exact")
    return 0


if __name__ == "__main__":
    sys.exit(main())
