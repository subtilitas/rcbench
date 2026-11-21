#!/usr/bin/env python3
"""Build the firmware folder of the web flasher from downloaded releases.

The web flasher (flasher/) is served at /rcbench/flash/ on GitHub Pages.
A release download sends no CORS (cross-origin resource sharing) header,
so the page cannot fetch release assets from github.com.  The Pages
workflow downloads the assets of the newest releases with `gh release
download` and this tool turns them into fw/ beside the page:

    fw/index.json                 the releases, newest first
    fw/<tag>/manifest.json        board, chip, file, size, SHA-256, offset
    fw/<tag>/<image>              the images the page writes

Images per release, each written at 0x0 or by its UF2 (USB Flashing
Format) addresses:

    panel         rcbench-panel-merged.bin from rcbench-panel-p4-<tag>.zip,
                  ESP32-P4, as rcbench-panel-p4-merged-<tag>.bin
    iomcu         rcbench-iomcu-install.uf2 from rcbench-iomcu-<tag>.zip,
                  RP2350, as rcbench-iomcu-install-<tag>.uf2
    iomcu-rbmod   rcbench-iomcu-rbmod-install.uf2 from
                  rcbench-iomcu-rbmod-<tag>.zip, RP2350
    node-display  rcbench-node-display-merged-<version>.bin, ESP32-S3
    dial          rcbench-dial-merged-<version>.bin, ESP32-S3
    node-pwr      rcbench-node-pwr-merged-<version>.bin, ESP32-C6
    node-bal      rcbench-node-bal-merged-<version>.bin, ESP32-C6
    c6            rcbench-c6-merged-<esp_hosted version>.bin, ESP32-C6

A release without one of them leaves it out, and a release that gives
none fails.  Releases up to 0.22.1 carry the image of the ESP32-S3
panel as rcbench-panel-<tag>.zip: no image of the page.  Every ESP-IDF
(Espressif IoT Development Framework) image is held to its chip by the
chip ID of its header and must be a merged image, not an application
alone.  Every asset is held
to the release's SHA256SUMS.  An IOmcu entry names the image slots its
install image does not write, from the release's
firmware/iomcu/partitions.json: the page erases them, so the written
image is the one the boot ROM starts.

    tools/flash_manifest.py select LIST [--keep N]
    tools/flash_manifest.py build --releases DIR --list LIST --out DIR
                                  [--keep N]

LIST is the answer of `gh release list --json
tagName,isDraft,isPrerelease,publishedAt`.  `select` prints the tags to
download, one a line: the newest N published releases by version, plus
the newest stable release when it is older than those.  DIR/<tag>/ holds
the downloaded assets of a selected tag and its partitions.json as
iomcu-partitions.json.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import pathlib
import re
import struct
import sys
import zipfile

import release_kind

FORMAT = 1
KEEP = 6

UF2_MAGIC = (0x0A324655, 0x9E5D5157, 0x0AB16F30)
UF2_FAMILY_ABSOLUTE = 0xE48BFF57
UF2_E10_ADDR = 0x10FFFF00
FLASH_BASE = 0x10000000
SECTOR = 4096

ESP_CHIP_ID = {"esp32s3": 9, "esp32c6": 13, "esp32p4": 18}
ESP_BOOT_OFFSET = {"esp32s3": 0x0, "esp32c6": 0x0, "esp32p4": 0x2000}
APP_DESC_MAGIC = 0xABCD5432

# board, chip, the asset (a glob; {tag} is the tag), the member of a ZIP
# asset or None, the name on the page, required.  No image is required:
# releases up to 0.22.1 carry the panel of the ESP32-S3 as
# rcbench-panel-<tag>.zip and no rbmod image, and give the IOmcu alone.
IMAGES = (
    ("panel", "esp32p4", "rcbench-panel-p4-{tag}.zip",
     "rcbench-panel-merged.bin", "rcbench-panel-p4-merged-{tag}.bin", False),
    ("iomcu", "rp2350", "rcbench-iomcu-{tag}.zip",
     "rcbench-iomcu-install.uf2", "rcbench-iomcu-install-{tag}.uf2", False),
    ("iomcu-rbmod", "rp2350", "rcbench-iomcu-rbmod-{tag}.zip",
     "rcbench-iomcu-rbmod-install.uf2",
     "rcbench-iomcu-rbmod-install-{tag}.uf2", False),
    ("node-display", "esp32s3", "rcbench-node-display-merged-[0-9]*.bin",
     None, None, False),
    ("dial", "esp32s3", "rcbench-dial-merged-[0-9]*.bin", None, None, False),
    ("node-pwr", "esp32c6", "rcbench-node-pwr-merged-[0-9]*.bin",
     None, None, False),
    ("node-bal", "esp32c6", "rcbench-node-bal-merged-[0-9]*.bin",
     None, None, False),
    ("c6", "esp32c6", "rcbench-c6-merged-[0-9]*.bin", None, None, False),
)

SAFE_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}")


class BuildError(Exception):
    """A release that does not give the images the page needs."""


def kind(tag: str) -> str:
    """Name the kind of a release.

    Args:
        tag: A tag in the form vMAJOR.MINOR.PATCH.

    Returns:
        "testing" for an odd minor number, else "stable".
    """
    return "testing" if release_kind.is_testing(tag) else "stable"


def select(releases: list[dict], keep: int = KEEP) -> list[dict]:
    """Pick the releases the page offers.

    Args:
        releases: Entries of `gh release list --json tagName,isDraft,
            isPrerelease,publishedAt`.
        keep: The number of newest releases to keep.

    Returns:
        The entries, newest version first: the newest `keep` published
        releases with a tag in form, and the newest stable release when
        it is not among them.  Drafts and tags out of form are left out.
    """
    good = []
    for r in releases:
        try:
            version = release_kind.parse(r["tagName"])
        except (KeyError, ValueError):
            continue
        if r.get("isDraft"):
            continue
        good.append((version, r))
    good.sort(key=lambda x: x[0], reverse=True)
    out = [r for _, r in good[:keep]]
    if not any(kind(r["tagName"]) == "stable" for r in out):
        stable = [r for _, r in good if kind(r["tagName"]) == "stable"]
        if stable:
            out.append(stable[0])
    return out


def esp_check(data: bytes, chip: str, name: str) -> None:
    """Hold an ESP-IDF image to its chip and to the merged kind.

    Args:
        data: The image.
        chip: The chip the image is for.
        name: The file name, for messages.

    Raises:
        BuildError: No header at the chip's bootloader offset, another
            chip ID, or an application image alone.
    """
    off = ESP_BOOT_OFFSET[chip]
    if len(data) < off + 64 or data[off] != 0xE9:
        raise BuildError(f"{name}: no ESP-IDF image header at {off:#x}")
    chip_id = struct.unpack_from("<H", data, off + 12)[0]
    if chip_id != ESP_CHIP_ID[chip]:
        raise BuildError(f"{name}: chip ID {chip_id}, {ESP_CHIP_ID[chip]} "
                         f"({chip}) expected")
    if struct.unpack_from("<I", data, off + 32)[0] == APP_DESC_MAGIC:
        raise BuildError(f"{name}: an application image, not a merged one")


def uf2_sectors(data: bytes, name: str) -> set[int]:
    """Read the flash sectors an absolute UF2 image writes.

    Args:
        data: The UF2 file.
        name: The file name, for messages.

    Returns:
        The flash offsets of the 4096-byte sectors its blocks write.  The
        RP2350-E10 workaround block at 0x10ffff00 is left out.

    Raises:
        BuildError: A length that is no multiple of 512, a bad magic
            number, or a block of a family other than absolute.
    """
    if not data or len(data) % 512:
        raise BuildError(f"{name}: {len(data)} bytes is no UF2 file")
    sectors = set()
    for i in range(0, len(data), 512):
        m0, m1, _flags, addr, _size, no, count, family = struct.unpack_from(
            "<8I", data, i)
        m2 = struct.unpack_from("<I", data, i + 508)[0]
        if (m0, m1, m2) != UF2_MAGIC:
            raise BuildError(f"{name}: block {i // 512} has a bad magic")
        if family != UF2_FAMILY_ABSOLUTE:
            raise BuildError(f"{name}: block {i // 512} is of family "
                             f"{family:#010x}, not absolute")
        if addr == UF2_E10_ADDR and count == 2 and no == 0:
            continue
        sectors.add((addr - FLASH_BASE) // SECTOR * SECTOR)
    return sectors


def size_bytes(value: int | str) -> int:
    """Read a size of partitions.json: a number of bytes, or with K or M.

    Args:
        value: 8192, "8K" or "2M".

    Returns:
        The size in bytes.
    """
    if isinstance(value, int):
        return value
    m = re.fullmatch(r"\s*(\d+)\s*([KkMm]?)\s*", value)
    if not m:
        raise BuildError(f"partitions.json: size {value!r}")
    return int(m.group(1)) * {"": 1, "k": 1024, "m": 1048576}[
        m.group(2).lower()]


def erase_ranges(table: dict, written: set[int]) -> list[dict]:
    """Name the image partitions an install image leaves as they are.

    Args:
        table: The parsed firmware/iomcu/partitions.json of the release.
        written: The flash offsets of the sectors the install image writes.

    Returns:
        One {"offset", "size"} per partition that accepts an image family
        (any family but "data") and holds none of the written sectors:
        slot B of the IOmcu's table.
    """
    out = []
    for p in table.get("partitions", []):
        families = p.get("families", [])
        if not families or all(f == "data" for f in families):
            continue
        start = size_bytes(p["start"])
        size = size_bytes(p["size"])
        if start % SECTOR or size % SECTOR:
            raise BuildError(f"partition {p.get('name')}: not 4096-aligned")
        if any(start <= s < start + size for s in written):
            continue
        out.append({"offset": start, "size": size})
    return out


def read_sums(folder: pathlib.Path) -> dict[str, str]:
    """Read SHA256SUMS of a release folder.

    Args:
        folder: The folder of one release's downloaded assets.

    Returns:
        File name to SHA-256 hex digest; empty when the file is absent.
    """
    path = folder / "SHA256SUMS"
    sums = {}
    if path.exists():
        for line in path.read_text().splitlines():
            parts = line.split()
            if len(parts) == 2:
                sums[parts[1].removeprefix("./").lstrip("*")] = parts[0]
    return sums


def find_asset(folder: pathlib.Path, pattern: str) -> pathlib.Path | None:
    """Find the one asset of a release that matches a pattern.

    Args:
        folder: The folder of one release's downloaded assets.
        pattern: A glob on the file name.

    Returns:
        The path, or None when no asset matches.

    Raises:
        BuildError: More than one asset matches.
    """
    hits = sorted(p for p in folder.iterdir()
                  if fnmatch.fnmatchcase(p.name, pattern))
    if len(hits) > 1:
        raise BuildError(f"{folder.name}: {len(hits)} assets match "
                         f"{pattern}")
    return hits[0] if hits else None


def build_release(folder: pathlib.Path, entry: dict,
                  out: pathlib.Path) -> dict:
    """Write the images and the manifest of one release.

    Args:
        folder: The release's downloaded assets.
        entry: Its entry of the release list.
        out: The fw/ folder; the release goes into out/<tag>/.

    Returns:
        The manifest written.

    Raises:
        BuildError: A required image is missing, an asset differs from
            SHA256SUMS, or an image fails its check.
    """
    tag = entry["tagName"]
    sums = read_sums(folder)
    dest = out / tag
    dest.mkdir(parents=True, exist_ok=True)
    images = []
    table = None
    pt = folder / "iomcu-partitions.json"
    if pt.exists():
        table = json.loads(pt.read_text())
    for board, chip, pattern, member, rename, required in IMAGES:
        asset = find_asset(folder, pattern.format(tag=tag))
        if asset is None:
            if required:
                raise BuildError(f"{tag}: no asset {pattern.format(tag=tag)}")
            continue
        raw = asset.read_bytes()
        want = sums.get(asset.name)
        if sums and want is None:
            raise BuildError(f"{tag}: {asset.name} is not in SHA256SUMS")
        if want and hashlib.sha256(raw).hexdigest() != want:
            raise BuildError(f"{tag}: {asset.name} differs from SHA256SUMS")
        if member:
            with zipfile.ZipFile(asset) as z:
                try:
                    data = z.read(member)
                except KeyError:
                    raise BuildError(f"{tag}: {asset.name} holds no {member}")
        else:
            data = raw
        name = rename.format(tag=tag) if rename else asset.name
        if not SAFE_NAME.fullmatch(name):
            raise BuildError(f"{tag}: file name {name!r}")
        image = {"board": board, "chip": chip, "file": name,
                 "size": len(data),
                 "sha256": hashlib.sha256(data).hexdigest(), "offset": 0,
                 "format": "uf2" if chip == "rp2350" else "bin"}
        if chip == "rp2350":
            written = uf2_sectors(data, name)
            if table is not None:
                erase = erase_ranges(table, written)
                if erase:
                    image["erase"] = erase
        else:
            esp_check(data, chip, name)
        (dest / name).write_bytes(data)
        images.append(image)
    if not images:
        raise BuildError(f"{tag}: no image of the page")
    manifest = {"format": FORMAT, "tag": tag, "kind": kind(tag),
                "published": entry["publishedAt"], "images": images}
    (dest / "manifest.json").write_text(json.dumps(manifest, indent=1)
                                        + "\n")
    return manifest


def build(releases_dir: pathlib.Path, listing: list[dict],
          out: pathlib.Path, keep: int = KEEP) -> list[dict]:
    """Write fw/: one folder per selected release and the index.

    Args:
        releases_dir: Holds one folder of downloaded assets per tag.
        listing: The release list.
        out: The fw/ folder to write.
        keep: The number of newest releases to keep.

    Returns:
        The manifests written, newest first.
    """
    out.mkdir(parents=True, exist_ok=True)
    manifests = []
    for entry in select(listing, keep):
        folder = releases_dir / entry["tagName"]
        if not folder.is_dir():
            raise BuildError(f"{entry['tagName']}: no folder {folder}")
        manifests.append(build_release(folder, entry, out))
    index = {"format": FORMAT, "keep": keep,
             "releases": [{"tag": m["tag"], "kind": m["kind"],
                           "published": m["published"]} for m in manifests]}
    (out / "index.json").write_text(json.dumps(index, indent=1) + "\n")
    return manifests


def main(argv: list[str] | None = None) -> int:
    """Run the command line.

    Args:
        argv: The arguments after the program name.

    Returns:
        The exit code.
    """
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("select")
    s.add_argument("list", type=pathlib.Path)
    s.add_argument("--keep", type=int, default=KEEP)
    b = sub.add_parser("build")
    b.add_argument("--releases", type=pathlib.Path, required=True)
    b.add_argument("--list", type=pathlib.Path, required=True)
    b.add_argument("--out", type=pathlib.Path, required=True)
    b.add_argument("--keep", type=int, default=KEEP)
    args = ap.parse_args(argv)
    listing = json.loads(args.list.read_text())
    if args.cmd == "select":
        for r in select(listing, args.keep):
            print(r["tagName"])
        return 0
    try:
        manifests = build(args.releases, listing, args.out, args.keep)
    except BuildError as e:
        print(f"flash_manifest: {e}", file=sys.stderr)
        return 1
    total = 0
    for m in manifests:
        size = sum(i["size"] for i in m["images"])
        total += size
        print(f"{m['tag']}: {len(m['images'])} images, {size} bytes")
    print(f"{len(manifests)} releases, {total} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
