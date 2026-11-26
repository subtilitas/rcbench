"""Where each model of hardware/mechanical/models/ lies.

    models/pcb/      boards with their parts, and layouts of boards
    models/casing/   printed parts: cases, bases, frames
    models/parts/    bought parts, and assemblies with their neighbours

renders/ has the same 3 folders: a model's picture lies in the model's
folder, a view of several models in the folder VIEWS names.

A model that no generator writes, such as casing/rcbench_case.step from
Onshape, is listed here like the others and kept as delivered:
``gen_models.py --check`` finds no spec for it and does not compare it.

Every stem is in exactly one folder. A generator asks ``path`` for its
file; a stem that is not listed here is an error, so a new model is
sorted when it is added.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parent / "models"
FOLDERS = {
    "pcb": (
        "boost_5v_en", "charger_boost_2s4s", "core2350b",
        "core2350b_usb_adapter", "dcdc_buck_mini", "es3c28p",
        "esp32_c6_zero", "esp32_s3_lcd_185", "ina219_cjmcu219",
        "ina228_node_layout", "ina228_xt90", "ina3221_module",
        "lipo_charger_boost_5v", "matek_mbec12s", "panel_7b",
        "pd_trigger_pd31", "rbmod_v2",
        "rbmod_v2_smd", "rp2350_can", "usbc_pd_trigger",
        "usbc_plug_breakout"),
    "casing": (
        "dial_base_bottom", "dial_base_top", "display_node_box",
        "display_node_lid", "display_node_xt60_box",
        "display_node_xt60_lid", "flap_drive",
        "ina228_node_bezel", "ina228_node_frame", "ina228_node_plate",
        "ina228_xt90_case", "knob_bezel", "knob_carrier", "knob_case",
        "knob_clamp", "knob_rotor", "knob_spacer", "rcbench_case"),
    "parts": (
        "acrylic_disc_49_5", "bearing_6704_2rs", "esc_motor_set",
        "estop_m16_assembly", "estop_m16_button", "estop_m16_nut",
        "estop_m16_shim", "flap_drive_assembly", "flap_drive_wing",
        "flycolor_flydragon_lite_40a", "hitec_d485hw", "knob_as5600",
        "knob_magnets", "kst_x10", "lipo_102540_1000mah",
        "lipo_702030_380mah", "m5dial", "m5dial_nut", "motor_simulator",
        "servo_standard", "supvan_t50m_pro", "tmotor_at2814", "wit_imu"),
}
FOLDER = {stem: folder for folder, stems in FOLDERS.items()
          for stem in stems}
# Renders that show no single model: a second view of one, or several
# models together. renders/ has the folders of models/; a model's
# render lies in the model's folder, the contact sheet all.png beside
# the folders.
VIEWS = {
    "pcb": ("es3c28p_back", "esp32_s3_lcd_185_back"),
    "casing": (
        "dial_base", "dial_base_exploded", "dial_base_section",
        "display_node_case", "display_node_case_exploded",
        "display_node_case_section", "display_node_lid_inside",
        "display_node_xt60_case", "display_node_xt60_case_exploded",
        "display_node_xt60_case_section", "display_node_xt60_seat",
        "ina228_node_case", "ina228_node_case_back",
        "ina228_node_case_exploded", "ina228_node_case_joints",
        "ina228_node_case_section"),
    "parts": (
        "bench_front", "bench_iso", "bench_top", "flap_drive_above",
        "flap_drive_below", "flap_drive_section", "flap_drive_unit",
        "flap_drive_unit_2", "knob", "knob_exploded", "knob_section"),
}
VIEW = {name: folder for folder, names in VIEWS.items() for name in names}
RENDERS = ROOT.parent / "renders"
SHEET = "all.png"
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


def render(directory, name):
    """Where the render of a model or of a view goes.

    Args:
        directory: The renders' root.
        name: The model's stem or the view's name, without suffix.

    Returns:
        The path <directory>/<folder>/<name>.png; its folder exists.

    Raises:
        KeyError: The name is no stem of FOLDERS and no view of VIEWS.
    """
    folder = FOLDER.get(name) or VIEW.get(name)
    if folder is None:
        raise KeyError(f"{name} is no model and no view of _models.py")
    out = Path(directory) / folder
    out.mkdir(parents=True, exist_ok=True)
    return out / f"{name}.png"


def stray_renders():
    """The files under renders/ that lie where ``render`` does not put them.

    Returns:
        The paths, sorted.
    """
    out = []
    for found in sorted(RENDERS.rglob("*")):
        if found.is_dir() or found == RENDERS / SHEET:
            continue
        folder = FOLDER.get(found.stem) or VIEW.get(found.stem)
        if found.suffix != ".png" or found.parent != RENDERS / str(folder):
            out.append(found)
    return out


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
