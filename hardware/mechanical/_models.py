"""Where each model of hardware/mechanical/models/ lies.

    models/pcb/      boards with their parts, and layouts of boards
    models/casing/   printed parts: cases, bases, frames
    models/parts/    bought parts, and assemblies with their neighbours

Every stem is in exactly one folder. A generator asks ``path`` for its
file; a stem that is not listed here is an error, so a new model is
sorted when it is added.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parent / "models"
FOLDERS = {
    "pcb": (
        "boost_5v_en", "core2350b", "core2350b_usb_adapter",
        "dcdc_buck_mini", "es3c28p", "esp32_c6_zero", "ina219_cjmcu219",
        "ina228_node_layout", "ina228_xt90", "ina3221_module",
        "lipo_charger_boost_5v", "matek_mbec12s", "panel_7b", "rbmod_v2",
        "rbmod_v2_smd", "rp2350_can", "usbc_pd_trigger"),
    "casing": (
        "dial_base_bottom", "dial_base_top", "flap_drive",
        "ina228_node_bezel", "ina228_node_frame", "ina228_node_plate",
        "ina228_xt90_case"),
    "parts": (
        "estop_m16_assembly", "estop_m16_button", "estop_m16_nut",
        "estop_m16_shim", "flap_drive_assembly", "flap_drive_wing",
        "kst_x10", "lipo_102540_1000mah", "lipo_702030_380mah", "m5dial",
        "m5dial_nut", "servo_standard", "wit_imu"),
}
FOLDER = {stem: folder for folder, stems in FOLDERS.items()
          for stem in stems}
SUFFIXES = (".step", ".stl")


def path(stem, suffix=".step"):
    """The file of one model.

    Args:
        stem: The model's name without suffix.
        suffix: ".step", ".stl" or another suffix with its dot.

    Returns:
        The path models/<folder>/<stem><suffix>.

    Raises:
        KeyError: The stem is in no folder of FOLDERS.
    """
    if stem not in FOLDER:
        raise KeyError(f"{stem} is in no folder of _models.FOLDERS")
    return ROOT / FOLDER[stem] / f"{stem}{suffix}"


def strays():
    """The files under models/ that no stem of FOLDERS accounts for.

    Returns:
        The paths, sorted: a file directly in models/, a file in a
        folder its stem is not listed for, or a file of another suffix.
    """
    out = []
    for found in sorted(ROOT.rglob("*")):
        if found.is_dir() or found.name.startswith("."):
            continue
        if found.suffix not in SUFFIXES or FOLDER.get(found.stem) != (
                found.parent.name) or found.parent.parent != ROOT:
            out.append(found)
    return out
