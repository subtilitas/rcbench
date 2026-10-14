#!/usr/bin/env python3
"""Open the archives test_wa_zip wrote and hold them to their manifests.

Each <name>.zip has a <name>.txt beside it.  A "# full" manifest lists every
entry in order: Python's zipfile must read the archive (testzip() finds no
bad CRC), `unzip -t` must pass it, and every name, size, CRC, time and
flag must be the manifest's.  A "# sparse" manifest is a ZIP64 limit case
whose data is a hole: the directory is read, each entry's header offset and
size held to the manifest, the last entry's 5 bytes read back with their
CRC, `unzip -l` must list it, and the file is removed afterwards.

Usage: check_wa_zip.py <folder>
"""

import pathlib
import shutil
import subprocess
import sys
import time
import zipfile


def dos_time(t):
    t = max(t, 315532800)
    t = min(t, 4354819198)
    g = time.gmtime(t)
    return (g.tm_year, g.tm_mon, g.tm_mday, g.tm_hour, g.tm_min,
            g.tm_sec // 2 * 2)


def check_full(path, rows):
    with zipfile.ZipFile(path) as z:
        bad = z.testzip()
        if bad is not None:
            return f"{path.name}: bad CRC in {bad}"
        infos = z.infolist()
        if len(infos) != len(rows):
            return f"{path.name}: {len(infos)} entries, manifest {len(rows)}"
        for info, row in zip(infos, rows):
            kind, name, size, crc, mtime = row[:5]
            if info.filename != name:
                return f"{path.name}: entry {info.filename!r}, want {name!r}"
            if info.is_dir() != (kind == "D"):
                return f"{path.name}: {name} folder flag wrong"
            if info.file_size != int(size) or info.compress_size != int(size):
                return f"{path.name}: {name} size {info.file_size}"
            if info.CRC != int(crc):
                return f"{path.name}: {name} CRC {info.CRC:08x}"
            if info.compress_type != zipfile.ZIP_STORED:
                return f"{path.name}: {name} not stored"
            if info.flag_bits & 0x0808 != 0x0808:
                return f"{path.name}: {name} flags {info.flag_bits:04x}"
            if info.date_time != dos_time(int(mtime)):
                return f"{path.name}: {name} time {info.date_time}"
    zip64 = b"PK\x06\x06" in path.read_bytes()[-200:]
    if zip64 != (len(rows) >= 0xFFFF):
        return f"{path.name}: ZIP64 end record {zip64} for {len(rows)} entries"
    r = subprocess.run(["unzip", "-t", "-qq", str(path)],
                       capture_output=True, text=True, check=False)
    # unzip says "zipfile is empty" with exit 1 for an archive with no
    # entries; zipfile has read that one already.
    if r.returncode != 0 and not (len(rows) == 0 and r.returncode == 1):
        return f"{path.name}: unzip -t exit {r.returncode}: {r.stdout}{r.stderr}"
    return None


def check_sparse(path, rows):
    try:
        with zipfile.ZipFile(path) as z:
            infos = z.infolist()
            if len(infos) != len(rows):
                return f"{path.name}: {len(infos)} entries"
            for info, row in zip(infos, rows):
                _, name, size, _, _, offset = row
                if info.filename != name or info.file_size != int(size):
                    return f"{path.name}: {info.filename} {info.file_size}"
                if info.header_offset != int(offset):
                    return f"{path.name}: {name} at {info.header_offset}"
            if z.read(infos[-1]) != b"hello":
                return f"{path.name}: last entry does not read back"
        r = subprocess.run(["unzip", "-l", str(path)], capture_output=True,
                           text=True, check=False)
        if r.returncode != 0:
            return f"{path.name}: unzip -l exit {r.returncode}: {r.stderr}"
        for row in rows:
            if row[1] not in r.stdout or row[2] not in r.stdout:
                return f"{path.name}: unzip -l does not list {row[1]} {row[2]}"
    finally:
        path.unlink()
    return None


def main():
    folder = pathlib.Path(sys.argv[1])
    if shutil.which("unzip") is None:
        print("unzip is not installed", file=sys.stderr)
        return 1
    names = sorted(folder.glob("*.zip"))
    if len(names) < 10:
        print(f"{len(names)} archives in {folder}; test_wa_zip writes 11",
              file=sys.stderr)
        return 1
    failed = 0
    for path in names:
        lines = path.with_suffix(".txt").read_text(encoding="utf-8").splitlines()
        rows = [ln.split("\t") for ln in lines[1:]]
        check = check_full if lines[0] == "# full" else check_sparse
        err = check(path, rows)
        print(f"{path.name}: {'ok' if err is None else err}")
        failed += err is not None
    nested = [p.read_bytes() for p in names if p.name.startswith("nested")]
    if len(set(nested)) != 1:
        print("the nested archives differ with the piece size")
        failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
