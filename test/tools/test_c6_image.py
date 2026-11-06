"""c6_image.py: what it holds the C6 radio's image to.

The pinned version, the slot, the sdkconfig's needs, the image's own
checks, the lock file's paths and pins, and prepare and check end to end
on a fake component and build.

SPDX-License-Identifier: MIT
"""

import struct

import pytest

import c6_image as c6

PIN_YML = """dependencies:
  espressif/esp_wifi_remote:
    version: "==1.6.5"
  espressif/esp_hosted:
    version: "==2.13.0"
"""

GOOD_SDK = "\n".join(f"{k}={v}" for k, v in c6.NEEDED.items()) + "\n"

LOCK = """dependencies:
  cmd_system:
    dependencies: []
    source:
      path: ${IDF_PATH}/examples/system/console/advanced/components/cmd_system
      type: local
    version: '*'
  espressif/iperf:
    component_hash: aa11
    dependencies:
    - name: idf
      require: private
      version: '>=4.3'
    source:
      registry_url: https://components.espressif.com/
      type: service
    version: 0.1.4
  idf:
    source:
      type: idf
    version: 6.1.0
direct_dependencies:
- cmd_system
- espressif/iperf
manifest_hash: 00
target: esp32c6
version: 3.0.0
"""


def image(chip=c6.CHIP_ESP32C6, version="2.13.0", project="network_adapter",
          hashed=1, size=4096):
    """An app image's head: header, one segment header, the descriptor.

    Args:
        chip: Chip ID at offset 12.
        version: Descriptor version text.
        project: Descriptor project name.
        hashed: Hash-appended flag.
        size: Total bytes.

    Returns:
        The image's bytes.
    """
    b = bytearray(size)
    b[0] = c6.IMAGE_MAGIC
    b[1] = 1
    struct.pack_into("<H", b, 12, chip)
    b[23] = hashed
    struct.pack_into("<I", b, 32, c6.DESC_MAGIC)
    b[48:48 + len(version)] = version.encode()
    b[80:80 + len(project)] = project.encode()
    return bytes(b)


def test_the_pin_is_the_exact_esp_hosted_version():
    """The pin is the exact esp hosted version."""
    assert c6.pinned_version(PIN_YML) == "2.13.0"
    with pytest.raises(c6.C6Error):
        c6.pinned_version(PIN_YML.replace("==2.13.0", "^2.13.0"))
    yml = "description: x\nversion: 2.13.0\n"
    assert c6.component_version(yml) == "2.13.0"
    with pytest.raises(c6.C6Error):
        c6.component_version("description: x\n")


def test_the_slot_is_ota_0_in_k_m_or_hex():
    """The slot is ota 0 in k m or hex."""
    csv = "nvs, data, nvs, 0x9000, 16K,\nota_0, app, ota_0, 0x10000, 1920K,\n"
    assert c6.slot_bytes(csv) == 1920 * 1024
    assert c6.slot_bytes(csv.replace("1920K", "2M")) == 2 * 1024 * 1024
    assert c6.slot_bytes(csv.replace("1920K", "0x1E0000")) == 0x1E0000
    with pytest.raises(c6.C6Error):
        c6.slot_bytes("nvs, data, nvs, 0x9000, 16K,\n")
    with pytest.raises(c6.C6Error):
        c6.slot_bytes(csv.replace("1920K", "lots"))


def test_the_sdkconfig_names_what_it_lacks():
    """The sdkconfig names what it lacks."""
    assert c6.sdkconfig_missing(GOOD_SDK) == []
    sdk = GOOD_SDK.replace("CONFIG_BT_LE_HCI_INTERFACE_USE_RAM=y",
                           "# CONFIG_BT_LE_HCI_INTERFACE_USE_RAM is not set")
    assert c6.sdkconfig_missing(sdk) == [
        "CONFIG_BT_LE_HCI_INTERFACE_USE_RAM=y"]
    assert len(c6.sdkconfig_missing("")) == len(c6.NEEDED)


def test_the_image_must_be_the_pinned_c6_build_inside_the_slot():
    """The image must be the pinned c6 build inside the slot."""
    c6.check_image(image(), "2.13.0", 4096)
    for bad in (image(chip=0x0012), image(version="2.12.3"),
                image(project="rcbench-panel"), image(hashed=0)):
        with pytest.raises(c6.C6Error):
            c6.check_image(bad, "2.13.0", 4096)
    with pytest.raises(c6.C6Error):
        c6.check_image(image(size=4097), "2.13.0", 4096)
    with pytest.raises(c6.C6Error):
        c6.check_image(b"\0" * 400, "2.13.0", 4096)
    head = bytearray(image())
    struct.pack_into("<I", head, 32, 0)
    with pytest.raises(c6.C6Error):
        c6.check_image(bytes(head), "2.13.0", 4096)


def test_the_lock_takes_this_machines_idf_path(tmp_path):
    """The lock takes this machines idf path."""
    out = tmp_path / "proj"
    out.mkdir()
    idf = tmp_path / "idf"
    text = c6.lock_for(LOCK, out, str(idf))
    assert "${IDF_PATH}" not in text
    assert "path: ../idf/examples/system/console/advanced/components/" \
           "cmd_system\n" in text
    with pytest.raises(c6.C6Error):
        c6.lock_for(LOCK, out, "")


def test_the_pins_leave_out_local_paths_and_hold_versions_and_hashes():
    """The pins leave out local paths and hold versions and hashes."""
    pins = c6.lock_pins(LOCK)
    assert pins == {"espressif/iperf": ("0.1.4", "aa11"),
                    "idf": ("6.1.0", "")}
    moved = LOCK.replace("version: 0.1.4", "version: 0.1.5")
    assert c6.lock_pins(moved) != pins
    rehashed = LOCK.replace("aa11", "bb22")
    assert c6.lock_pins(rehashed) != pins
    other_path = LOCK.replace("${IDF_PATH}", "../../../opt/esp/idf")
    assert c6.lock_pins(other_path) == pins


@pytest.fixture
def tree(tmp_path, monkeypatch):
    """A component, the panel's pin and the committed lock in tmp_path.

    Args:
        tmp_path: pytest's folder.
        monkeypatch: pytest's patcher.

    Returns:
        (component folder, project folder, build folder, dest folder).
    """
    comp = tmp_path / "esp_hosted"
    (comp / "slave" / "main").mkdir(parents=True)
    (comp / "idf_component.yml").write_text("version: 2.13.0\n")
    (comp / "slave" / "CMakeLists.txt").write_text(
        "project(network_adapter)\n")
    (comp / "slave" / "partitions.esp32c6.csv").write_text(
        "ota_0, app, ota_0, 0x10000, 1920K,\n")
    pin = tmp_path / "idf_component.yml"
    pin.write_text(PIN_YML)
    lock = tmp_path / "dependencies.lock"
    lock.write_text(LOCK)
    monkeypatch.setattr(c6, "PIN", pin)
    monkeypatch.setattr(c6, "LOCK", lock)
    monkeypatch.setenv("IDF_PATH", str(tmp_path / "idf"))
    return comp, tmp_path / "proj", tmp_path / "build", tmp_path / "dist"


def test_prepare_and_check_name_the_images(tree):
    """Prepare and check name the images."""
    comp, proj, build, dist = tree
    assert c6.main(["prepare", str(comp), str(proj)]) == 0
    assert (proj / "CMakeLists.txt").is_file()
    (proj / "sdkconfig").write_text(GOOD_SDK)
    build.mkdir()
    (build / c6.IMAGE).write_bytes(image())
    (build / c6.MERGED).write_bytes(b"\xff" * 70000)
    lines = c6.check(proj, build, dist)
    assert lines[0].startswith("rcbench-c6-2.13.0.bin: 4096 bytes of a "
                               "1966080-byte slot, 0.2%, sha256 ")
    assert (dist / "rcbench-c6-2.13.0.bin").read_bytes() == image()
    assert (dist / "rcbench-c6-merged-2.13.0.bin").stat().st_size == 70000
    assert c6.main(["check", str(proj), str(build), str(dist)]) == 0


def test_prepare_refuses_another_version_or_no_slave(tree):
    """Prepare refuses another version or no slave."""
    comp, proj, _, _ = tree
    (comp / "idf_component.yml").write_text("version: 2.12.3\n")
    assert c6.main(["prepare", str(comp), str(proj)]) == 1
    (comp / "idf_component.yml").write_text("version: 2.13.0\n")
    (comp / "slave" / "CMakeLists.txt").unlink()
    with pytest.raises(c6.C6Error):
        c6.prepare(comp, proj)


def test_check_fails_on_a_moved_pin_a_missing_setting_or_no_image(tree):
    """Check fails on a moved pin a missing setting or no image."""
    comp, proj, build, dist = tree
    c6.prepare(comp, proj)
    (proj / "sdkconfig").write_text(GOOD_SDK)
    build.mkdir()
    (build / c6.IMAGE).write_bytes(image())
    lock = proj / "dependencies.lock"
    good = lock.read_text()
    lock.write_text(good.replace("version: 0.1.4", "version: 0.1.5"))
    with pytest.raises(c6.C6Error, match="espressif/iperf"):
        c6.check(proj, build, dist)
    lock.write_text(good)
    (proj / "sdkconfig").write_text(GOOD_SDK.replace("CONFIG_BT_ENABLED=y",
                                                     ""))
    with pytest.raises(c6.C6Error, match="CONFIG_BT_ENABLED"):
        c6.check(proj, build, dist)
    (proj / "sdkconfig").write_text(GOOD_SDK)
    (build / c6.IMAGE).unlink()
    assert c6.main(["check", str(proj), str(build), str(dist)]) == 1


def test_a_wrong_call_prints_the_usage():
    """A wrong call prints the usage."""
    assert c6.main([]) == 2
    assert c6.main(["build", "x"]) == 2
