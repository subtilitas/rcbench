"""flash_manifest.py: the web flasher's fw/ folder from downloaded releases.

The cases build a fake release folder: the ZIP archives and merged images
a release publishes, its SHA256SUMS and the IOmcu partition table, with
synthetic image bytes.

SPDX-License-Identifier: MIT
"""

import hashlib
import json
import struct
import subprocess
import sys
import zipfile

import pytest

import flash_manifest as fm

TOOL = fm.__file__
ABS = 0xE48BFF57
ARM_S = 0xE48BFF59

PARTITIONS = {
    "partitions": [
        {"name": "A", "id": 0, "start": "8K", "size": "2036K",
         "families": ["rp2350-arm-s"]},
        {"name": "B", "id": 1, "start": "2044K", "size": "2036K",
         "families": ["rp2350-arm-s"], "link": ["a", 0]},
        {"name": "store", "id": 2, "start": "4080K", "size": "8K",
         "families": ["data"]},
    ],
}


def esp_image(chip_id, offset=0, app=False, size=8192):
    """An ESP-IDF image with its header at `offset`."""
    data = bytearray(b"\xff" * offset + bytes(i & 0xFF for i in range(size)))
    data[offset] = 0xE9
    struct.pack_into("<H", data, offset + 12, chip_id)
    struct.pack_into("<I", data, offset + 32,
                     0xABCD5432 if app else 0x50000000)
    return bytes(data)


def uf2(addr, count, family=ABS, e10=True):
    """A UF2 file of `count` pages from `addr`, the E10 block first."""
    blocks = []
    if e10:
        blocks.append(block(0x10FFFF00, 0, 2, ABS, 0xA000))
    for i in range(count):
        blocks.append(block(addr + i * 256, i, count, family, 0x2000))
    return b"".join(blocks)


def block(addr, no, count, family, flags):
    """One 512-byte UF2 block."""
    head = struct.pack("<8I", 0x0A324655, 0x9E5D5157, flags, addr, 256, no,
                       count, family)
    return head + bytes(476) + struct.pack("<I", 0x0AB16F30)


def write_release(root, tag, *, panel=True, dial=True, sums=True,
                  dial_chip=9, panel_member="rcbench-panel-merged.bin"):
    """Write the assets of one release into root/<tag>/."""
    folder = root / tag
    folder.mkdir(parents=True)
    ver = tag[1:]
    assets = {}
    if panel:
        with zipfile.ZipFile(folder / f"rcbench-panel-p4-{tag}.zip",
                             "w") as z:
            z.writestr(panel_member, esp_image(18, offset=0x2000))
            z.writestr("rcbench-panel.bin", esp_image(18, app=True))
        assets[f"rcbench-panel-p4-{tag}.zip"] = None
    for board in ("iomcu", "iomcu-rbmod"):
        with zipfile.ZipFile(folder / f"rcbench-{board}-{tag}.zip", "w") as z:
            z.writestr(f"rcbench-{board}-install.uf2",
                       uf2(0x10000000, 1) + uf2(0x10002000, 8, e10=False))
            z.writestr(f"rcbench-{board}.uf2", uf2(0x10000000, 8, ARM_S))
        assets[f"rcbench-{board}-{tag}.zip"] = None
    if dial:
        (folder / f"rcbench-dial-merged-{ver}.bin").write_bytes(
            esp_image(dial_chip))
        (folder / f"rcbench-dial-{ver}.bin").write_bytes(
            esp_image(9, app=True))
    (folder / f"rcbench-node-pwr-merged-{ver}.bin").write_bytes(
        esp_image(13))
    (folder / "rcbench-c6-merged-2.13.0.bin").write_bytes(esp_image(13))
    (folder / "iomcu-partitions.json").write_text(json.dumps(PARTITIONS))
    if sums:
        lines = []
        for p in sorted(folder.iterdir()):
            if p.suffix in (".zip", ".bin"):
                digest = hashlib.sha256(p.read_bytes()).hexdigest()
                lines.append(f"{digest}  ./{p.name}")
        (folder / "SHA256SUMS").write_text("\n".join(lines) + "\n")
    return folder


def listing(*tags, drafts=()):
    """A `gh release list` answer."""
    out = [{"tagName": t, "isDraft": False, "isPrerelease": False,
            "publishedAt": "2026-11-18T21:49:31Z"} for t in tags]
    out += [{"tagName": t, "isDraft": True, "isPrerelease": False,
             "publishedAt": ""} for t in drafts]
    return out


def test_select_keeps_the_newest_by_version_and_the_newest_stable():
    """The newest N by version, drafts out, plus the newest stable one."""
    tags = [f"v0.23.{i}" for i in range(12)] + ["v0.22.1", "v0.22.0",
                                                 "nightly"]
    picked = [r["tagName"] for r in fm.select(
        listing(*tags, drafts=["v0.23.99"]), keep=3)]
    assert picked == ["v0.23.11", "v0.23.10", "v0.23.9", "v0.22.1"]
    picked = [r["tagName"] for r in fm.select(
        listing("v0.24.0", "v0.23.5"), keep=6)]
    assert picked == ["v0.24.0", "v0.23.5"]


def test_build_writes_manifest_images_and_index(tmp_path):
    """Each image with board, chip, size, SHA-256 and offset; the index."""
    rel = tmp_path / "rel"
    write_release(rel, "v0.23.15")
    write_release(rel, "v0.22.1", panel=False, dial=False)
    out = tmp_path / "fw"
    fm.build(rel, listing("v0.22.1", "v0.23.15"), out)
    index = json.loads((out / "index.json").read_text())
    assert [r["tag"] for r in index["releases"]] == ["v0.23.15", "v0.22.1"]
    assert [r["kind"] for r in index["releases"]] == ["testing", "stable"]
    assert index["keep"] == 6
    m = json.loads((out / "v0.23.15" / "manifest.json").read_text())
    assert m["format"] == 1 and m["tag"] == "v0.23.15"
    boards = {i["board"]: i for i in m["images"]}
    assert list(boards) == ["panel", "iomcu", "iomcu-rbmod", "dial",
                            "node-pwr", "c6"]
    panel = boards["panel"]
    assert panel["file"] == "rcbench-panel-p4-merged-v0.23.15.bin"
    assert panel["chip"] == "esp32p4" and panel["offset"] == 0
    data = (out / "v0.23.15" / panel["file"]).read_bytes()
    assert panel["size"] == len(data)
    assert panel["sha256"] == hashlib.sha256(data).hexdigest()
    assert boards["dial"]["chip"] == "esp32s3"
    assert boards["node-pwr"]["chip"] == "esp32c6"
    iomcu = boards["iomcu"]
    assert iomcu["format"] == "uf2"
    assert iomcu["file"] == "rcbench-iomcu-install-v0.23.15.uf2"
    assert iomcu["erase"] == [{"offset": 2044 * 1024, "size": 2036 * 1024}]
    old = json.loads((out / "v0.22.1" / "manifest.json").read_text())
    assert [i["board"] for i in old["images"]] == [
        "iomcu", "iomcu-rbmod", "node-pwr", "c6"]
    assert sorted(p.name for p in (out / "v0.23.15").iterdir()) == sorted(
        [i["file"] for i in m["images"]] + ["manifest.json"])


def test_an_image_of_another_chip_fails(tmp_path):
    """A Dial image with the chip ID of an ESP32-C6 fails the build."""
    rel = tmp_path / "rel"
    write_release(rel, "v0.23.15", dial_chip=13)
    with pytest.raises(fm.BuildError, match="chip ID 13, 9"):
        fm.build(rel, listing("v0.23.15"), tmp_path / "fw")


def test_an_application_image_fails(tmp_path):
    """An application alone in place of the merged panel image fails."""
    rel = tmp_path / "rel"
    folder = write_release(rel, "v0.23.15", sums=False)
    with zipfile.ZipFile(folder / "rcbench-panel-p4-v0.23.15.zip", "w") as z:
        z.writestr("rcbench-panel-merged.bin",
                   esp_image(18, offset=0x2000, app=True))
    with pytest.raises(fm.BuildError, match="application image"):
        fm.build(rel, listing("v0.23.15"), tmp_path / "fw")


def test_an_asset_that_differs_from_sha256sums_fails(tmp_path):
    """A changed byte after SHA256SUMS fails the build."""
    rel = tmp_path / "rel"
    folder = write_release(rel, "v0.23.15")
    path = folder / "rcbench-c6-merged-2.13.0.bin"
    path.write_bytes(b"\x00" + path.read_bytes()[1:])
    with pytest.raises(fm.BuildError, match="differs from SHA256SUMS"):
        fm.build(rel, listing("v0.23.15"), tmp_path / "fw")


def test_a_zip_without_its_member_fails(tmp_path):
    """The panel archive without the merged image fails."""
    rel = tmp_path / "rel"
    write_release(rel, "v0.23.15", panel_member="other.bin")
    with pytest.raises(fm.BuildError, match="holds no rcbench-panel-merged"):
        fm.build(rel, listing("v0.23.15"), tmp_path / "fw")


def test_an_install_image_of_another_family_fails():
    """The install image must be absolute: its blocks go where they say."""
    with pytest.raises(fm.BuildError, match="not absolute"):
        fm.uf2_sectors(uf2(0x10000000, 2, ARM_S, e10=False), "x.uf2")
    assert fm.uf2_sectors(uf2(0x10002000, 20), "x.uf2") == {0x2000, 0x3000}


def test_partition_sizes_and_the_ranges_to_erase():
    """K and M suffixes; data partitions and written slots are kept."""
    assert fm.size_bytes("8K") == 8192
    assert fm.size_bytes("2M") == 2 * 1048576
    assert fm.size_bytes(4096) == 4096
    assert fm.erase_ranges(PARTITIONS, {0, 0x2000}) == [
        {"offset": 0x1FF000, "size": 0x1FD000}]
    assert fm.erase_ranges(PARTITIONS, {0x1FF000}) == [
        {"offset": 0x2000, "size": 0x1FD000}]


def test_the_command_line_prints_the_tags_and_the_sizes(tmp_path):
    """`select` prints tags; `build` prints a line per release and a sum."""
    rel = tmp_path / "rel"
    write_release(rel, "v0.23.15")
    lst = tmp_path / "list.json"
    lst.write_text(json.dumps(listing("v0.23.15")))
    run = subprocess.run([sys.executable, TOOL, "select", str(lst)],
                         capture_output=True, text=True, check=True)
    assert run.stdout == "v0.23.15\n"
    run = subprocess.run([sys.executable, TOOL, "build", "--releases",
                          str(rel), "--list", str(lst), "--out",
                          str(tmp_path / "fw")],
                         capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stderr
    assert "v0.23.15: 6 images" in run.stdout
    assert run.stdout.strip().endswith("bytes")
    (rel / "v0.23.15" / "rcbench-c6-merged-2.13.0.bin").write_bytes(b"x")
    run = subprocess.run([sys.executable, TOOL, "build", "--releases",
                          str(rel), "--list", str(lst), "--out",
                          str(tmp_path / "fw2")],
                         capture_output=True, text=True, check=False)
    assert run.returncode == 1
    assert "differs from SHA256SUMS" in run.stderr
