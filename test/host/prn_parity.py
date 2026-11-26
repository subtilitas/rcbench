#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The two printer file readers, held to one verdict.

A printer file is read in two places: tools/printer_files.py checks the
files of record in CI, and prn_parse() reads them, and files from the SD
card, on the panel.  A file one takes and the other refuses, a refusal
with another code or line, or a file both take and read differently, is a
file CI passes and the panel reads another way.

Each case is a file of protocols/printers/ or test/host/fixtures/printers/
changed 1 to 3 times, each change one of:

- a byte flipped, inserted or deleted, from a pool that holds 0x00, 0x7F,
  0x80, 0xFF, CR, TAB and the format's own punctuation; the file cut at a
  random byte;
- a line removed, doubled, swapped with another, moved, indented or
  unindented;
- a token of a line replaced from a pool of boundary numbers and the
  format's words, removed or doubled;
- a line padded to 119, 120 or 121 bytes, the file padded to 255, 256 or
  257 lines or to 8,191, 8,192 or 8,193 bytes;
- the file saved under another name.

Both readers read it; both refuse it with the same code and line, or both
take it and print the same dump (test/host/prn_parse_dump.c and
printer_files.dump()).

The cases come from random.Random(seed) and are the same on every run of
one Python version.  A failure states the seed and the case number and
prints the file, so it runs again alone with

    prn_parity.py <prn_parse_dump> --seed <seed> --count <count>

The default run is 2 seeds of 1500 cases each.  ctest stops it after 120s.

usage: prn_parity.py <prn_parse_dump> [--seed N ...] [--count N]
"""
import argparse
import pathlib
import random
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO / "tools"))
import printer_files as pf  # noqa: E402
import printer_spec as ps  # noqa: E402

SEEDS = (1, 20261126)
COUNT = 1500

BYTES = [0x00, 0x09, 0x0A, 0x0D, 0x20, 0x23, 0x2D, 0x3A, 0x3D, 0x7B, 0x7D,
         0x7E, 0x7F, 0x80, 0xFF, 0x30, 0x78, 0x41]
NUMBERS = ["0", "1", "2", "3", "4", "5", "7", "8", "9", "10", "15", "16",
           "19", "20", "23", "24", "40", "41", "48", "49", "63", "64", "65",
           "100", "127", "128", "129", "200", "201", "244", "245", "247",
           "248", "255", "256", "499", "500", "501", "1000", "1023", "1024",
           "1025", "3200", "3201", "4095", "4096", "5000", "5001", "8192",
           "8193", "30000", "30001", "60000", "60001", "65535", "65536",
           "4294967295", "4294967296", "99999999999", "0x", "0x0", "0xFF",
           "0xZZ", "0x100000000", "-1", "-128", "-129", "007", "1.5", ""]
WORDS = (ps.FIELDS + ps.VARS + ps.ENCS + ps.CKS + ps.CODES[1:] + ps.FKINDS
         + ps.EDGES
         + ["id", "name", "version", "licence", "source", "match", "oui",
            "iname", "svc", "service", "chr", "cmd", "data", "rx", "other",
            "write", "write_nr", "notify", "cccd", "next", "discover",
            "mtu_min", "conn", "head_dots", "dots_mm", "line", "lsb", "msb",
            "yes", "no", "label", "feed", "dot0", "align", "center",
            "margin", "roll", "record", "fixed", "limits", "density", "page",
            "size", "lines", "head", "flags", "compress", "none",
            "lzma_lit", "chunk", "pad", "max", "frame", "rows", "req", "nr",
            "table", "speed", "mean_page", "else", "tmpl", "join",
            "magic_len", "tail", "check", "key", "answer", "field", "error",
            "step", "send", "wait", "expect", "poll", "until", "within",
            "tries", "save", "relink", "delay", "and", "set", "clear", "=",
            "!=", "c0-1", "lay_w", "job", "query", "print", "abort", "from",
            "to", "relink_ms", "answer_ms", "verdict", "counter", "result",
            "done", "end", "on", "FF00", "FFE9", "7E", "7E5A", "zz",
            "0000e0ff-3c17-d293-8e48-14fe2e4da212", "A4:93:40", "a=1",
            "p0=frames", "p3=1", "a=nope", "{data}", "{a:u8}", "{p0:le16}",
            "{len:u8:0:-1}", "{ck:sum8:0:-1}", "{ck:svpage:le16}",
            "{ck:sum16le:10:-1}", "{idx:u8}", "{pflags:u8}", "{line_no:be16}",
            "{c0:le32}", "{bogus:u8}", "{a:u9}", "{len:u8:0:-200}", "{",
            "{}", "{data:x}", "{ck:svpage:be16}", "material", "ready",
            "start", "pause", "full", "bulk", "x", "_", "a-b", "T50M"])


def base_files() -> list[pathlib.Path]:
    """The files the cases are made from.

    Returns:
        The files of record and the suites' fixtures.
    """
    return (sorted((REPO / "protocols" / "printers").glob("*.prn"))
            + sorted((REPO / "test" / "host" / "fixtures" / "printers")
                     .glob("*.prn")))


def pad(rng: random.Random, lines: list[bytes]) -> bytes:
    """Pad a file to a line count or a size at the format's limits.

    Args:
        rng: the generator.
        lines: the file's lines.

    Returns:
        The padded file.
    """
    if rng.random() < 0.5:
        want = rng.choice([255, 256, 257])
        while len(lines) < want:
            lines.insert(len(lines) - 1, b"#")
        return b"\n".join(lines)
    want = rng.choice([8191, 8192, 8193])
    body = b"\n".join(lines)
    filler = b"#" + b"x" * 100 + b"\n"
    extra = b""
    while len(body) + len(extra) + len(filler) <= want:
        extra += filler
    extra += b"#" * max(0, want - len(body) - len(extra))
    return extra + body if rng.random() < 0.5 else body + extra


def change(rng: random.Random, data: bytes) -> bytes:
    """One change of a file.

    Args:
        rng: the generator.
        data: the file.

    Returns:
        The changed file.
    """
    lines = data.split(b"\n")
    pick = rng.randrange(14)
    if pick == 0 and data:
        i = rng.randrange(len(data))
        return data[:i] + bytes([data[i] ^ (1 << rng.randrange(8))]) \
            + data[i + 1:]
    if pick == 1:
        i = rng.randrange(len(data) + 1)
        return data[:i] + bytes([rng.choice(BYTES)]) + data[i:]
    if pick == 2 and data:
        i = rng.randrange(len(data))
        return data[:i] + data[i + 1:]
    if pick == 3:
        return data[:rng.randrange(len(data) + 1)]
    if pick == 13:
        return pad(rng, lines)
    i = rng.randrange(len(lines))
    if pick == 4 and len(lines) > 1:
        del lines[i]
    elif pick == 5:
        lines.insert(i, lines[i])
    elif pick == 6:
        j = rng.randrange(len(lines))
        lines[i], lines[j] = lines[j], lines[i]
    elif pick == 7:
        line = lines.pop(i)
        lines.insert(rng.randrange(len(lines) + 1), line)
    elif pick == 8:
        lines[i] = (rng.choice([b"  ", b"\t", b" "]) + lines[i].lstrip()
                    if rng.random() < 0.5 else lines[i].lstrip())
    elif pick in (9, 10, 11):
        toks = lines[i].split(b" ")
        k = rng.randrange(len(toks))
        how = rng.random()
        if pick == 9 or how < 0.4:
            pool = NUMBERS if rng.random() < 0.5 else WORDS
            toks[k] = rng.choice(pool).encode()
        elif how < 0.7:
            del toks[k]
        else:
            toks.insert(k, toks[k])
        lines[i] = b" ".join(toks)
    elif pick == 12:
        width = rng.choice([119, 120, 121])
        line = lines[i] + (b" #" if b"#" not in lines[i] else b"")
        lines[i] = line.ljust(width, b"x")
    return b"\n".join(lines)


def main() -> int:
    """Run the cases.

    Returns:
        0, or 1 on the first case the readers read differently.
    """
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dump", help="the prn_parse_dump program")
    ap.add_argument("--seed", type=int, nargs="*", default=list(SEEDS))
    ap.add_argument("--count", type=int, default=COUNT)
    args = ap.parse_args()
    bases = [(p.name, p.read_bytes()) for p in base_files()]
    taken = refused = 0
    with tempfile.TemporaryDirectory(prefix="prn-parity-") as tmp:
        root = pathlib.Path(tmp)
        for seed in args.seed:
            rng = random.Random(seed)
            cases = []
            for n in range(args.count):
                name, data = rng.choice(bases)
                for _ in range(rng.randint(1, 3)):
                    data = change(rng, data)
                if rng.random() < 0.03:
                    name = rng.choice(["other.prn", "x.prn", name[:-4]])
                folder = root / f"{seed}-{n}"
                folder.mkdir()
                path = folder / name
                path.write_bytes(data)
                cases.append(path)
            run = subprocess.run([args.dump],
                                 input="\n".join(map(str, cases)) + "\n",
                                 capture_output=True, text=True, check=True)
            got = run.stdout.splitlines()
            if len(got) != len(cases):
                print(f"seed {seed}: {len(got)} lines for {len(cases)} files")
                return 1
            for n, (path, line) in enumerate(zip(cases, got, strict=True)):
                want = pf.dump_line(path)
                if want != line:
                    print(f"seed {seed} case {n}: the readers differ\n"
                          f"  C:      {line[:300]}\n  Python: {want[:300]}\n"
                          "--- the file ---")
                    print(path.read_bytes().decode("latin-1"))
                    return 1
                if "\tOK\t" in line:
                    taken += 1
                else:
                    refused += 1
    print(f"{taken + refused} cases: {taken} taken, {refused} refused, "
          "the same in both readers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
