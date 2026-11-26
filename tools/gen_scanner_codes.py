#!/usr/bin/env python3
"""Write the QR scanner's setup codes as a C table, or check the table.

The panel draws the GM861 module's "Serial Output" setup code on
SETUP > SENSORS > SCANNER's SHOW SETUP CODE.  The code is a stored module
matrix, taken from page 15 of Hangzhou Grow Technology's "GM861/GM861-LED
Bar Code Reader Module User Manual" V1.2.4 (2025-07), so the panel needs no
QR encoder.  The matrix is held here as text and checked as a QR code of
version 1 (21 x 21 modules):

- the 3 finder patterns, their separators and the 2 timing patterns;
- the dark module;
- both copies of the 15 format bits equal and a BCH (Bose-Chaudhuri-
  Hocquenghem) code word, which gives the error correction level and the
  mask;
- the 26 code words read under that mask pass the Reed-Solomon check;
- the data decodes, and its bytes are written into the C file's comment.

    tools/gen_scanner_codes.py            write shared/scanner/scanner_codes.c
    tools/gen_scanner_codes.py --check    fail when the file differs or a
                                          check above fails
    tools/gen_scanner_codes.py --from-png IMAGE
                                          print the matrix of a code image
                                          (needs Pillow): how the text below
                                          was taken from the manual's page

SPDX-License-Identifier: MIT
"""

import argparse
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "shared" / "scanner" / "scanner_codes.c"

N = 21

# "Serial Output", manual V1.2.4 page 15 (PDF page 17, the page's first
# image, 350 x 350px), sampled at each module's center.  '#' dark.
SERIAL_OUTPUT = (
    "#######.#.....#######",
    "#.....#.#...#.#.....#",
    "#.###.#..###..#.###.#",
    "#.###.#.#..#..#.###.#",
    "#.###.#...##..#.###.#",
    "#.....#...###.#.....#",
    "#######.#.#.#.#######",
    "........#..##........",
    "#.##.###.#.##.#..#.##",
    "#...#....#.###..#.##.",
    "#...###.#.##.##..#..#",
    ".##.#..#...#.#..#..#.",
    "...#..##.##.#..#.##..",
    "........#.##..#...#.#",
    "#######.#..###.....##",
    "#.....#.##.....#..##.",
    "#.###.#...#.#.#..#..#",
    "#.###.#.#..#..#.#..#.",
    "#.###.#.###.#.###.#..",
    "#.....#......#...#..#",
    "#######.#.#..#..#....",
)

# Error correction level of the 2 format bits: data and EC code words of a
# version 1 code.
LEVELS = {1: ("L", 19, 7), 0: ("M", 16, 10), 3: ("Q", 13, 13),
          2: ("H", 9, 17)}

MASKS = (
    lambda r, c: (r + c) % 2 == 0,
    lambda r, c: r % 2 == 0,
    lambda r, c: c % 3 == 0,
    lambda r, c: (r + c) % 3 == 0,
    lambda r, c: (r // 2 + c // 3) % 2 == 0,
    lambda r, c: (r * c) % 2 + (r * c) % 3 == 0,
    lambda r, c: ((r * c) % 2 + (r * c) % 3) % 2 == 0,
    lambda r, c: ((r + c) % 2 + (r * c) % 3) % 2 == 0,
)

ALNUM = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:"


def matrix(rows):
    """The rows as booleans.

    Args:
        rows: N strings of N characters, '#' dark.

    Returns:
        N lists of N booleans, True dark.
    """
    return [[ch == "#" for ch in row] for row in rows]


def finder_ok(m, r0, c0):
    """Whether a finder pattern sits with its corner at (r0, c0).

    Args:
        m: The matrix.
        r0: Its top row.
        c0: Its left column.

    Returns:
        True for the 7 x 7 pattern: dark ring, light ring, 3 x 3 dark core.
    """
    for r in range(7):
        for c in range(7):
            ring = max(abs(r - 3), abs(c - 3))
            if m[r0 + r][c0 + c] != (ring != 2):
                return False
    return True


def bch_words():
    """The 32 valid format words, masked with 0x5412.

    Returns:
        A dict of word to its 5 data bits.
    """
    out = {}
    for data in range(32):
        v = data << 10
        for i in range(14, 9, -1):
            if (v >> i) & 1:
                v ^= 0x537 << (i - 10)
        out[((data << 10) | v) ^ 0x5412] = data
    return out


def bits_value(bits):
    """A list of booleans, most significant first, as an integer."""
    return int("".join("1" if b else "0" for b in bits), 2)


def format_words(m):
    """The two copies of the format bits.

    Returns:
        (around the top-left finder, split between the other two).
    """
    a = [m[8][c] for c in (0, 1, 2, 3, 4, 5, 7, 8)]
    a += [m[r][8] for r in (7, 5, 4, 3, 2, 1, 0)]
    b = [m[r][8] for r in (20, 19, 18, 17, 16, 15, 14)]
    b += [m[8][c] for c in (13, 14, 15, 16, 17, 18, 19, 20)]
    return bits_value(a), bits_value(b)


def is_function(r, c):
    """Whether module (r, c) belongs to a pattern or to the format bits."""
    if (r < 9 and c < 9) or (r < 9 and c >= N - 8) or (r >= N - 8 and c < 9):
        return True
    return r == 6 or c == 6


def codewords(m, mask):
    """The 26 code words, read in the zig-zag order under @p mask."""
    bits = []
    col = N - 1
    up = True
    while col > 0:
        if col == 6:
            col -= 1
        rows = range(N - 1, -1, -1) if up else range(N)
        for r in rows:
            for c in (col, col - 1):
                if not is_function(r, c):
                    bits.append(m[r][c] != MASKS[mask](r, c))
        up = not up
        col -= 2
    return [bits_value(bits[i * 8:i * 8 + 8]) for i in range(26)]


def gf_tables():
    """Exponent and logarithm tables of GF(256), polynomial 0x11D."""
    exp = [0] * 512
    log = [0] * 256
    x = 1
    for i in range(255):
        exp[i] = x
        log[x] = i
        x <<= 1
        if x & 0x100:
            x ^= 0x11D
    for i in range(255, 512):
        exp[i] = exp[i - 255]
    return exp, log


def syndromes_zero(words, ec):
    """Whether the Reed-Solomon syndromes of a block are all 0.

    Args:
        words: The block's code words, data first.
        ec: Its EC code words.

    Returns:
        True for a block without an error.
    """
    exp, log = gf_tables()
    for i in range(ec):
        s = 0
        for w in words:
            s = (exp[(log[s] + i) % 255] if s else 0) ^ w
        if s:
            return False
    return True


def segments(data):
    """Decode the data code words' segments.

    Returns:
        The decoded bytes of every segment, joined.
    """
    s = "".join(f"{b:08b}" for b in data)
    pos = 0
    out = b""
    while pos + 4 <= len(s):
        mode = int(s[pos:pos + 4], 2)
        pos += 4
        if mode == 0:
            break
        if mode == 4:
            n = int(s[pos:pos + 8], 2)
            pos += 8
            out += bytes(int(s[pos + 8 * i:pos + 8 * i + 8], 2)
                         for i in range(n))
            pos += 8 * n
        elif mode == 2:
            n = int(s[pos:pos + 9], 2)
            pos += 9
            text = ""
            for _ in range(n // 2):
                v = int(s[pos:pos + 11], 2)
                text += ALNUM[v // 45] + ALNUM[v % 45]
                pos += 11
            if n % 2:
                text += ALNUM[int(s[pos:pos + 6], 2)]
                pos += 6
            out += text.encode()
        elif mode == 1:
            n = int(s[pos:pos + 10], 2)
            pos += 10
            text = ""
            while n >= 3:
                text += str(int(s[pos:pos + 10], 2)).zfill(3)
                pos += 10
                n -= 3
            if n == 2:
                text += str(int(s[pos:pos + 7], 2)).zfill(2)
                pos += 7
            elif n == 1:
                text += str(int(s[pos:pos + 4], 2))
                pos += 4
            out += text.encode()
        else:
            raise ValueError(f"mode {mode} is not decoded here")
    return out


def problems(rows):
    """Check the matrix as a version 1 QR code.

    Returns:
        (problems, level, mask, payload); level, mask and payload are None
        once a problem stops the reading.
    """
    out = []
    if len(rows) != N or any(len(r) != N for r in rows):
        return [f"the matrix is not {N} x {N}"], None, None, None
    m = matrix(rows)
    for r0, c0, name in ((0, 0, "top left"), (0, N - 7, "top right"),
                         (N - 7, 0, "bottom left")):
        if not finder_ok(m, r0, c0):
            out.append(f"no finder pattern {name}")
    for i in range(8, N - 8):
        if m[6][i] != (i % 2 == 0) or m[i][6] != (i % 2 == 0):
            out.append(f"timing pattern broken at {i}")
            break
    if not m[N - 8][8]:
        out.append("the dark module is light")
    a, b = format_words(m)
    valid = bch_words()
    if a != b:
        out.append(f"the format copies differ: {a:015b} {b:015b}")
    if a not in valid:
        out.append(f"the format bits {a:015b} are no BCH code word")
    if out:
        return out, None, None, None
    level, mask = valid[a] >> 3, valid[a] & 7
    name, data_n, ec_n = LEVELS[level]
    words = codewords(m, mask)
    if not syndromes_zero(words, ec_n):
        out.append("the code words fail the Reed-Solomon check")
        return out, name, mask, None
    return out, name, mask, segments(words[:data_n])


def c_file(rows, level, mask, payload):
    """The C table's text."""
    lines = [
        "/**",
        " * @file",
        " * @brief The QR scanner's setup codes as module matrices.",
        " *        Written by tools/gen_scanner_codes.py; do not edit.",
        " *",
        " * \"Serial Output\": GM861 manual V1.2.4, page 15.  QR version 1,",
        f" * level {level}, mask {mask}, data bytes {payload.hex(' ')}.",
        " *",
        " * SPDX-License-Identifier: MIT",
        " */",
        '#include "scanner_codes.h"',
        "",
        "const uint32_t scanner_code_serial[SCANNER_CODE_MODULES] = {",
    ]
    for row in rows:
        v = bits_value([ch == "#" for ch in row])
        lines.append(f"    0x{v:06X}u,  /* {row} */")
    lines += ["};", ""]
    return "\n".join(lines)


def from_png(path):
    """Print the matrix of a code image, edge to edge, as the rows above."""
    from PIL import Image  # pylint: disable=import-outside-toplevel

    im = Image.open(path).convert("L")
    w, h = im.size
    px = im.load()
    dark = [(x, y) for y in range(0, h, 2) for x in range(0, w, 2)
            if px[x, y] < 128]
    x0 = min(p[0] for p in dark)
    x1 = max(p[0] for p in dark) + 2
    y0 = min(p[1] for p in dark)
    y1 = max(p[1] for p in dark) + 2
    mw = (x1 - x0) / N
    mh = (y1 - y0) / N
    for r in range(N):
        row = "".join("#" if px[int(x0 + (c + 0.5) * mw),
                                 int(y0 + (r + 0.5) * mh)] < 128 else "."
                      for c in range(N))
        print(f'    "{row}",')


def main(argv=None):
    """Write or check the table.

    Returns:
        0 when written or in agreement, 1 otherwise.
    """
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--from-png", metavar="IMAGE")
    args = ap.parse_args(argv)
    if args.from_png:
        from_png(args.from_png)
        return 0
    found, level, mask, payload = problems(SERIAL_OUTPUT)
    for p in found:
        print(f"gen_scanner_codes: Serial Output: {p}", file=sys.stderr)
    if found:
        return 1
    text = c_file(SERIAL_OUTPUT, level, mask, payload)
    if args.check:
        have = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if have != text:
            print(f"gen_scanner_codes: {OUT.relative_to(REPO)} differs; "
                  "run tools/gen_scanner_codes.py", file=sys.stderr)
            return 1
        return 0
    OUT.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
