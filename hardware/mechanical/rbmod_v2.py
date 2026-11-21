"""The rbmod carrier board, version 2: with its SMD parts, and complete.

    rbmod_v2_smd   the board with its copper and its 17 surface-mounted
                   parts
    rbmod_v2       the same with 7 through-hole connectors and the
                   5 modules

The carrier is 80.0 x 89.0mm with a notch of 4.4 x 13.0mm in its left
edge and 6 holes of 3.0mm. It carries an RP2350-CAN, an INA3221 module,
2 mini buck converters and a USB-C PD trigger board.

Source: rbmod.kicad_pcb of the branch rbmod_v2 of the owner's KiCad
repository, commit 86d2301 (not in this repository). _read_kicad_pcb.py
reads its outline, holes and footprints into _rbmod_v2_data.py; every
place, turn and drill of this model comes from there. The modules are
the models of hardware/kicad/modules/, whose footprints the board uses:
the drilled pads of each agree with its spec. Each is drawn part by
part by the spec of its name in this directory.

Drawn from package sizes, not from the board file: 1206 resistors
3.2 x 1.6 x 0.55, 0402 resistors, 0603 capacitors, the 1210 capacitor
3.2 x 2.5 x 2.5, the SMB diode 4.3 x 3.6 x 2.3 and 5.4 over its leads,
the SMC diode 6.9 x 5.9 x 2.3 and 7.9 over its leads. The JST PH
headers B2B-PH-K and B4B-PH-K follow hardware/kicad/modules/_jst_ph.py.

Not populated (owner, 2026-11-21): J6, a JST PH header of 4 pins, and
J8, a Molex Mini-Fit Jr header of 2 pins. The board file does not mark
them; DNP names them here. Their drilled pads are in the board, the
parts are in neither model.

Estimated: the angled 3 x 4 pin headers as a plastic bar 2.54mm thick,
1.52mm behind the third row, with pins that end 15.12mm from the first
row, the F.Fab drawing's reach. They are not taken from a maker's
drawing.

The copper of the 2 outer layers is drawn from the board file: the
pads, gold (ENEPIG: nickel, palladium, gold; owner, 2026-11-21) and
0.035mm thick, and the tracks, the vias and the filled
zones, 0.02mm thick in a lighter green, as copper shows under the
solder mask. Every part stands on the pads' top, 0.035mm over the
board.

Not drawn: the inner layers, the print, the vias' holes, solder. The
board is drawn green and 1.579mm thick, the board file's stack. The RP2350-CAN, the INA3221 board and the 2 buck
converters stand on their headers' plastic, 2.5mm over the carrier; the
PD trigger board lies on the carrier.

Frame: origin at the outline's lower left corner seen from the top
side, x to the right, y up, z = 0 on the top side, mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "kicad" / "tools"))
import _board  # noqa: E402
import _parts as p  # noqa: E402
import _rbmod_v2_data as data  # noqa: E402
import _solid as s  # noqa: E402
import step_gen  # noqa: E402

GREEN = step_gen.COLORS["green"]
JST = _board.kicad_spec("_jst_ph")
MODULES = {"RP2350-CAN": "rp2350_can", "INA3221_Module": "ina3221_module",
           "DCDC_Buck_Mini_30x18mm_EN": "dcdc_buck_mini",
           "USBC_PD_Trigger_DIP3_30x20mm": "usbc_pd_trigger"}
DNP = ("J6", "J8")              # not populated (owner, 2026-11-21)
COPPER = ((0.17, 0.62, 0.31), 0.02)  # copper under the mask; thickness
ENEPIG = (0.96, 0.80, 0.30)     # the pads' finish (owner, 2026-11-21)
ANGLED = (2.54, 1.52, 15.12)    # bar's thickness, its gap to the third
#                                 row, the pins' reach from the first row


def ph_header(pins):
    """A JST PH top-entry header B<n>B-PH-K, pin 1 at the origin.

    The pins run along +x. The housing reaches 1.95 past the first and
    the last pin, 1.7 to +y and 2.8 to -y.
    """
    x0, x1 = -JST.END, JST.PITCH * (pins - 1) + JST.END
    y0, y1 = JST.ROW - JST.DEPTH, JST.ROW
    w = JST.WALL
    xs = [JST.PITCH * i for i in range(pins)]
    half = JST.PIN / 2
    out = [s.ext(s.rect(x0, y0, x1, y1), 0.0, JST.FLOOR, p.CREAM, holes=[
               s.rect(x - half, -half, x + half, half) for x in xs]),
           s.ext(s.rect(x0, y0, x1, y1), JST.FLOOR, JST.HEIGHT, p.CREAM,
                 holes=[s.rect(x0 + w, y0 + w, x1 - w, y1 - w)])]
    for x in xs:
        out.append(s.box(x - half, -half, x + half, half, -JST.PIN_BELOW,
                         JST.PIN_TOP, p.TIN))
    return out


def angled_header():
    """The angled 3 x 4 pin header, pin 1 at the origin.

    Rows of 3 run along +x, 4 of them towards -y. The pin of the third
    column is the lowest; each pin rises from its hole and runs to +x
    through the plastic bar.
    """
    thick, gap, reach = ANGLED
    half = 0.32
    bar = 2 * 2.54 + gap
    out = [s.ext(s.rect(bar, -3 * 2.54 - 1.27, bar + thick, 1.27), 0.0,
                 3 * 2.54, p.BLACK)]
    for row in range(4):
        y = -2.54 * row
        for column in range(3):
            x, z = 2.54 * column, 1.27 + 2.54 * (2 - column)
            out.append(s.box(x - half, y - half, x + half, y + half,
                             -3.0, z + half, p.GOLD))
            out.append(s.box(x + half, y - half, bar, y + half, z - half,
                             z + half, p.GOLD))
            out.append(s.box(bar + thick, y - half, reach, y + half,
                             z - half, z + half, p.GOLD))
    return out


def module(name):
    """A module as shapes, pad 1 at the origin on the carrier's top side.

    The shapes are those of the module's KiCad model, which its spec in
    this directory draws part by part.
    """
    spec = _board.kicad_spec(MODULES[name])
    path = HERE / f"{spec.MODEL['detail']}.py"
    found = importlib.util.spec_from_file_location(path.stem, path)
    drawn = importlib.util.module_from_spec(found)
    found.loader.exec_module(drawn)
    return drawn.mounted()


def part(footprint):
    """The shapes of one footprint in its own frame.

    Raises:
        ValueError: For a footprint the model does not know.
    """
    name = footprint["footprint"].split(":")[1]
    if name in MODULES:
        return module(name)
    made = {
        "R_1206_3216Metric": lambda: p.chip(3.2, 1.6, 0.55, p.EPOXY),
        "R_0402_1005Metric": p.r0402,
        "C_0603_1608Metric": p.c0603,
        "C_1210_3225Metric": lambda: p.chip(3.2, 2.5, 2.5, p.TAN),
        "D_SMB": lambda: p.sod(4.3, 3.6, 2.3, 5.4, 2.0),
        "D_SMC": lambda: p.sod(6.9, 5.9, 2.3, 7.9, 3.0),
        "JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical": lambda: ph_header(2),
        "JST_PH_B4B-PH-K_1x04_P2.00mm_Vertical": lambda: ph_header(4),
        "PinHeader_3x04_P2.54mm_Horizontal": angled_header,
    }
    if name not in made:
        raise ValueError(f"no model of {name}")
    return made[name]()


def unpopulated(footprint):
    """Whether the part is left off: marked in the file or named in DNP."""
    return footprint["dnp"] or footprint["ref"] in DNP


def board():
    """The carrier: plate, holes, and the copper of its 2 outer layers.

    The pads are p.PAD thick and gold, their finish ENEPIG. The tracks, the vias and the
    filled zones are COPPER[1] thick in a lighter green, as copper
    shows under the solder mask.
    """
    drills = [d for fp in data.FOOTPRINTS for d in fp["drills"]]
    t = data.THICKNESS
    out = [s.ext(list(data.OUTLINE), -t, 0.0, GREEN, holes=[
        s.circle(x, y, d / 2) for x, y, d in list(data.HOLES) + drills])]
    rgb, thick = COPPER
    for areas, z0, z1, colour in (
            (data.PADS_TOP, 0.0, p.PAD, ENEPIG),
            (data.PADS_BOTTOM, -t - p.PAD, -t, ENEPIG),
            (data.COPPER_TOP, 0.0, thick, rgb),
            (data.COPPER_BOTTOM, -t - thick, -t, rgb)):
        out += [s.ext(outer, z0, z1, colour, holes=holes)
                for outer, holes in areas]
    return out


def placed(smd):
    """The parts of one kind in the board's frame.

    Args:
        smd: True for the surface-mounted parts, False for the others.
    """
    out = []
    for fp in data.FOOTPRINTS:
        if fp["smd"] == smd and not unpopulated(fp):
            out += s.place(part(fp), x=fp["at"][0], y=fp["at"][1],
                           z=p.PAD, turn=fp["turn"])
    return out


def parts(_colors):
    """The two models.

    Returns:
        A dict of file stem to solids.
    """
    bare = board() + placed(True)
    return {"rbmod_v2_smd": s.items(bare),
            "rbmod_v2": s.items(bare + placed(False))}
