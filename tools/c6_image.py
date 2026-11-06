#!/usr/bin/env python3
"""Build input and check of the C6 radio's image the panel carries.

The board's ESP32-C6 runs esp_hosted's co-processor firmware.  The panel
pins esp_hosted for its host side (firmware/panel/components/wifi_access/
idf_component.yml) and the release builds the C6's firmware from the
``slave/`` folder of that same component, so host and C6 run one version.

    tools/c6_image.py prepare COMPONENT OUT
        copy COMPONENT/slave to OUT (emptied first) with the committed lock
        file firmware/c6/dependencies.lock; COMPONENT is the managed
        component firmware/panel/managed_components/espressif__esp_hosted
        and must be the version the panel pins.  Run where the build runs:
        the lock's one local component (cmd_system, an ESP-IDF example
        component) is written as a path from OUT to $IDF_PATH
    tools/c6_image.py check PROJECT BUILD DEST
        check the build of PROJECT in BUILD and copy its image to
        DEST/rcbench-c6-<version>.bin and DEST/rcbench-c6-merged-<version>.bin

``check`` fails when the image is not an ESP32-C6 application of project
network_adapter whose version is the pinned one, when its SHA-256 is not
appended, when it is larger than the C6's ota_0 slot of
PROJECT/partitions.esp32c6.csv, when the sdkconfig lacks the settings below
(BT controller over the SDIO link, which the panel's NimBLE over VHCI
needs), and when the build moved a registry component of the lock file to
another version or hash, or ESP-IDF to another version.

SPDX-License-Identifier: MIT
"""

import hashlib
import os
import re
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PIN = ROOT / "firmware" / "panel" / "components" / "wifi_access" / \
    "idf_component.yml"
LOCK = ROOT / "firmware" / "c6" / "dependencies.lock"
IMAGE = "network_adapter.bin"
MERGED = "rcbench-c6-merged.bin"
PROJECT = "network_adapter"
CHIP_ESP32C6 = 0x000D
IMAGE_MAGIC = 0xE9
DESC_MAGIC = 0xABCD5432

# What the panel's host side needs of the C6 build: the SDIO slave, the BT
# controller alone (no host stack on the C6), its HCI (host controller
# interface) over the transport in RAM (VHCI), and Wi-Fi.
NEEDED = {
    "CONFIG_IDF_TARGET": '"esp32c6"',
    "CONFIG_ESP_SDIO_HOST_INTERFACE": "y",
    "CONFIG_BT_ENABLED": "y",
    "CONFIG_BT_CONTROLLER_ONLY": "y",
    "CONFIG_BT_LE_HCI_INTERFACE_USE_RAM": "y",
    "CONFIG_ESP_WIFI_ENABLED": "y",
}


IDF_MARK = "${IDF_PATH}/"


class C6Error(Exception):
    """A check failed; the message names it."""


def lock_for(text: str, out: Path, idf_path: str) -> str:
    """The committed lock file with its local paths for this machine.

    Args:
        text: firmware/c6/dependencies.lock, whose local paths start with
            ``${IDF_PATH}/``.
        out: the project folder the lock goes into.
        idf_path: ESP-IDF's folder here.

    Returns:
        The lock file, each such path made relative to @p out.

    Raises:
        C6Error: a ``${IDF_PATH}`` path and no ESP-IDF folder given.
    """
    lines = []
    for line in text.splitlines(keepends=True):
        key, _, value = line.partition("path: ")
        if value.startswith(IDF_MARK):
            if not idf_path:
                raise C6Error("IDF_PATH is not set: run where the build runs")
            target = Path(idf_path) / value[len(IDF_MARK):].strip()
            line = f"{key}path: {os.path.relpath(target, out.resolve())}\n"
        lines.append(line)
    return "".join(lines)


def lock_pins(text: str) -> dict[str, tuple[str, str]]:
    """The pins of a lock file: each component's version and hash.

    Local components (a ``path:`` source) are left out: their entry is a
    path on the machine that wrote it.

    Args:
        text: a dependencies.lock.

    Returns:
        {name: (version, component_hash)}; ESP-IDF as "idf" with no hash.
    """
    pins = {}
    name, fields = None, {}

    def close():
        if name is not None and "path" not in fields:
            pins[name] = (fields.get("version", ""), fields.get("hash", ""))

    in_deps = False
    for line in text.splitlines():
        if not line.startswith(" "):
            close()
            name, fields = None, {}
            in_deps = line.startswith("dependencies:")
            continue
        if not in_deps:
            continue
        if line.startswith("  ") and not line.startswith("   "):
            close()
            name, fields = line.strip().rstrip(":"), {}
        elif line.startswith("    component_hash: "):
            fields["hash"] = line.split(": ", 1)[1].strip()
        elif line.startswith("    version: "):
            fields["version"] = line.split(": ", 1)[1].strip().strip("'")
        elif line.strip().startswith("path: "):
            fields["path"] = True
    close()
    return pins


def pinned_version(text: str) -> str:
    """The esp_hosted version the panel pins.

    Args:
        text: idf_component.yml of components/wifi_access.

    Returns:
        MAJOR.MINOR.PATCH.

    Raises:
        C6Error: no exact pin of espressif/esp_hosted.
    """
    m = re.search(r'espressif/esp_hosted:\s*\n\s*version:\s*'
                  r'"==(\d+\.\d+\.\d+)"', text)
    if not m:
        raise C6Error("no exact pin of espressif/esp_hosted in " + PIN.name)
    return m.group(1)


def component_version(text: str) -> str:
    """The version a component's own idf_component.yml states.

    Args:
        text: the component's idf_component.yml.

    Returns:
        MAJOR.MINOR.PATCH.

    Raises:
        C6Error: no top-level version line.
    """
    m = re.search(r"^version:\s*['\"]?(\d+\.\d+\.\d+)", text, re.M)
    if not m:
        raise C6Error("the component states no version")
    return m.group(1)


def slot_bytes(csv: str) -> int:
    """The size of ota_0 in a partition table's CSV.

    Args:
        csv: partitions.esp32c6.csv.

    Returns:
        Bytes.

    Raises:
        C6Error: no ota_0, or a size it cannot read.
    """
    for line in csv.splitlines():
        cols = [c.strip() for c in line.split("#", 1)[0].split(",")]
        if len(cols) >= 5 and cols[0] == "ota_0":
            size = cols[4]
            m = re.fullmatch(r"(\d+)([KM]?)", size, re.I)
            if m:
                mult = {"": 1, "K": 1024, "M": 1024 * 1024}[m.group(2).upper()]
                return int(m.group(1)) * mult
            if size.lower().startswith("0x"):
                return int(size, 16)
            raise C6Error("ota_0's size is not readable: " + size)
    raise C6Error("no ota_0 in the partition table")


def sdkconfig_missing(text: str) -> list[str]:
    """The settings of NEEDED the sdkconfig does not have.

    Args:
        text: the build's sdkconfig.

    Returns:
        Each missing NAME=value, in NEEDED's order.
    """
    have = {}
    for line in text.splitlines():
        if "=" in line and not line.startswith("#"):
            k, v = line.split("=", 1)
            have[k.strip()] = v.strip()
    return [f"{k}={v}" for k, v in NEEDED.items() if have.get(k) != v]


def image_info(data: bytes) -> tuple[int, str, str, bool]:
    """Chip, version, project and the hash flag of an ESP-IDF app image.

    Args:
        data: the image.

    Returns:
        (chip ID, version text, project name, SHA-256 appended).

    Raises:
        C6Error: not an application image with its descriptor.
    """
    if len(data) < 24 + 8 + 256 or data[0] != IMAGE_MAGIC:
        raise C6Error("not an ESP-IDF application image")
    chip = struct.unpack_from("<H", data, 12)[0]
    hashed = data[23] == 1
    if struct.unpack_from("<I", data, 32)[0] != DESC_MAGIC:
        raise C6Error("no application descriptor")
    version = data[48:80].split(b"\0", 1)[0].decode("ascii", "replace")
    project = data[80:112].split(b"\0", 1)[0].decode("ascii", "replace")
    return chip, version, project, hashed


def check_image(data: bytes, want: str, slot: int) -> None:
    """Hold an image to the C6's slot and the pinned version.

    Args:
        data: the image.
        want: the version it must carry.
        slot: the slot's bytes.

    Raises:
        C6Error: the first check that failed.
    """
    chip, version, project, hashed = image_info(data)
    if chip != CHIP_ESP32C6:
        raise C6Error(f"built for chip 0x{chip:04X}, not the ESP32-C6")
    if project != PROJECT:
        raise C6Error(f"project {project!r}, not {PROJECT!r}")
    if version != want:
        raise C6Error(f"version {version!r}, the panel pins {want!r}")
    if not hashed:
        raise C6Error("built without the appended SHA-256")
    if len(data) > slot:
        raise C6Error(f"{len(data)} bytes, the slot holds {slot}")


def prepare(component: Path, out: Path) -> str:
    """Copy the slave project with the committed lock file.

    Args:
        component: the esp_hosted managed component.
        out: the project folder to write; emptied first.

    Returns:
        The version.

    Raises:
        C6Error: another version than the panel pins, or no slave folder.
    """
    want = pinned_version(PIN.read_text())
    got = component_version((component / "idf_component.yml").read_text())
    if got != want:
        raise C6Error(f"component {got}, the panel pins {want}")
    if not (component / "slave" / "CMakeLists.txt").is_file():
        raise C6Error(f"no slave project in {component}")
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(component / "slave", out)
    (out / "dependencies.lock").write_text(
        lock_for(LOCK.read_text(), out, os.environ.get("IDF_PATH", "")))
    return want


def check(project: Path, build: Path, dest: Path) -> list[str]:
    """Check a build and copy its images under release names.

    Args:
        project: the prepared slave project.
        build: its build folder.
        dest: where the named images go.

    Returns:
        The report's lines.

    Raises:
        C6Error: the first check that failed.
    """
    want = pinned_version(PIN.read_text())
    built = lock_pins((project / "dependencies.lock").read_text())
    pinned = lock_pins(LOCK.read_text())
    if built != pinned:
        moved = sorted(k for k in set(built) | set(pinned)
                       if built.get(k) != pinned.get(k))
        raise C6Error("the build moved " + ", ".join(moved) + " off "
                      "firmware/c6/dependencies.lock")
    missing = sdkconfig_missing((project / "sdkconfig").read_text())
    if missing:
        raise C6Error("sdkconfig lacks " + ", ".join(missing))
    slot = slot_bytes((project / "partitions.esp32c6.csv").read_text())
    data = (build / IMAGE).read_bytes()
    check_image(data, want, slot)
    dest.mkdir(parents=True, exist_ok=True)
    app = dest / f"rcbench-c6-{want}.bin"
    shutil.copy2(build / IMAGE, app)
    lines = [f"{app.name}: {len(data)} bytes of a {slot}-byte slot, "
             f"{100 * len(data) / slot:.1f}%, "
             f"sha256 {hashlib.sha256(data).hexdigest()}"]
    merged = build / MERGED
    if merged.is_file():
        m = dest / f"rcbench-c6-merged-{want}.bin"
        shutil.copy2(merged, m)
        lines.append(f"{m.name}: {merged.stat().st_size} bytes, flashed at "
                     f"0x0 over header H4")
    return lines


def main(argv: list[str]) -> int:
    """Run ``prepare`` or ``check``.

    Args:
        argv: the arguments after the program's name.

    Returns:
        0 on success, 1 on a failed check, 2 on a usage error.
    """
    try:
        if len(argv) == 3 and argv[0] == "prepare":
            v = prepare(Path(argv[1]), Path(argv[2]))
            print(f"c6_image: esp_hosted {v} slave project in {argv[2]}")
            return 0
        if len(argv) == 4 and argv[0] == "check":
            for line in check(Path(argv[1]), Path(argv[2]), Path(argv[3])):
                print("c6_image: " + line)
            return 0
    except (C6Error, OSError) as e:
        print(f"c6_image: {e}", file=sys.stderr)
        return 1
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
