#!/usr/bin/env python3
"""Write a rotary knob with a round display on the dial base's bottom part.

A knob 64 across that turns round a fixed display, low on the
hand-held base: the base's bottom part with its cell
(_gen_dial_base.py) carries a new top part, the case, in place of the
dial's. The Waveshare ESP32-S3-Touch-LCD-1.85 stands still on a carrier
at the top of a shaft that rises from the case's floor; the rotor turns
round it on two 6704-2RS bearings, its hub sunk into the case, and
carries the clear acrylic disc 49.5 x 2 as its window. 22 magnets
Ø4 x 1.5, magnetised top to bottom with N and S up in turn, sit in
pockets in the hub. An AS5600 module stands upright in the case beside
the hub: its chip's plane holds the direction the magnets travel and
their axis, so the field at the chip turns 360 degrees for each N-S
pair, 11 times a turn ("magnetic gearing", after 0x23's
MicroManipulatorStepper, MIT licence). 11 x 4096 counts a turn leave
about 45 counts a step at 1000 steps a turn.

Printed parts, written to models/casing/ as STEP and STL:

    knob_case     the base's top part made new: the pebble's wall from
                  the joint up to z 0, open at the top; a floor at the
                  joint with 4 bosses for the base's M3 screws (threaded
                  inserts from below), a cable hole and a hole for the
                  sensor's wires; the shaft, a tube 20 / 14 with a
                  collar 21.6 under the lower bearing, rising from the
                  floor to the carrier; the wall the AS5600 module is
                  screwed to with 4 M3. Prints floor down, no support.
    knob_spacer   sleeve 21.6 / 20 between the bearings' inner rings
    knob_clamp    sleeve 21.6 / 20 from the upper bearing to the
                  carrier, which clamps the inner rings
    knob_rotor    hub 38 / 27 with a shoulder 24 between the bearings'
                  outer rings and the 22 magnet pockets, open to the
                  side; web over the sensor; skirt 64 / 60 from 0.5 over
                  the case's rim; lip under the window with an opening
                  of 47; the window's seat 49.7 across
    knob_carrier  plate 54 across, 2 thick, on the shaft; the display's
                  3 inserts take M2 screws through it; 3 M2 into the
                  shaft's wall

Placed bought parts, models/parts/: knob_magnets.step, the 22 magnets,
and knob_as5600.step, the AS5600 module, where they sit. No assembly
file is written: these, the printed parts and the files below, moved
along z by the value given, make the knob.

    dial_base_bottom.step      as it is
    bearing_6704_2rs.step      -13.0 and -3.0
    acrylic_disc_49_5.step     +21.7
    esp32_s3_lcd_185.step      +20.7

Frame: the dial base's. z = 0 on its top's upper face, the case's rim;
the joint with the bottom part at z -19; the knob's axis is z; the
display's USB socket is at -y; the AS5600 module stands at +x. All
sizes mm.

Stack, z: floor -19 to -17, its outer ring -17 to -15 over the bottom
part's rim; collar -17 to -13; bearings -13 to -9 and -3 to 1; rotor
hub -16 to 8, web 6 to 8, skirt 0.5 to 23.7, lip 21.0 to 21.7; carrier
9 to 11; display from 11 (its inserts' ends) to 20.7 (its glass's
front); window 21.7 to 23.7, 0.3 over the display's glass. Magnets
-6.25 to -4.75 on a circle of 17; the AS5600's chip faces them from
r = 20.5, 1.5 off the hub, at z -5.5. The module's board stands 0.15 over
the floor and ends 0.35 under the web. The knob is 56.2 high with the
base's bottom part.

``check`` fails when a part is not one valid solid, when the case has
material in a flute, or when two solids of the assembly share volume.

Usage:
    python3 hardware/mechanical/_gen_knob.py           write
    python3 hardware/mechanical/_gen_knob.py --check   exit 1 when a
                                                       file differs or
                                                       parts collide
    xvfb-run -a python3 hardware/mechanical/_gen_knob.py --render DIR

--render draws the knob to DIR/parts/knob.png, taken apart to
knob_exploded.png and cut open to knob_section.png. Needs OCP (pip
install cadquery-ocp==8.0.1.1.0).

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import math
import sys
from pathlib import Path

from OCP.BRepAlgoAPI import (BRepAlgoAPI_Common, BRepAlgoAPI_Cut,
                             BRepAlgoAPI_Fuse)
from OCP.BRepCheck import BRepCheck_Analyzer
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.BRepPrimAPI import BRepPrimAPI_MakeCylinder
from OCP.gp import gp_Ax2, gp_Dir, gp_Pnt
from OCP.StlAPI import StlAPI_Writer

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import _gen_dial_base as base  # noqa: E402
import _models  # noqa: E402

sys.path.insert(0, str(HERE.parent / "kicad" / "tools"))

GRAPHITE = base.GRAPHITE
ROTOR_RGB = (0.80, 0.52, 0.18)
WHITE = (0.93, 0.93, 0.90)
STEEL = (0.72, 0.72, 0.72)
NORTH, SOUTH = (0.75, 0.15, 0.12), (0.15, 0.30, 0.70)

TOP = 0.0               # the case's rim
JOINT = base.SPLIT
# Floor: thickness; the ring over the bottom part's rim, its top.
FLOOR_T, LEDGE_TOP = 2.0, -15.0
FLOOR_TOP = JOINT + FLOOR_T
# Shaft: outer and inner diameter, top; collar diameter and top; the
# window for the sensor's wires at its foot: half width, top.
SHAFT_D, BORE_D, SHAFT_TOP = 20.0, 14.0, 9.0
COLLAR_D, COLLAR_TOP = 21.6, -13.0
SHAFT_SLOT = (2.5, -14.5)
CABLE_D, WIRE_D = 12.0, 5.0
M3_CLEAR = 3.4
# Bearings 6704-2RS: z of each one's lower face, width.
BEARING_Z, BEARING_W = (-13.0, -3.0), 4.0
# Rotor: hub bore and outside, shoulder bore between the bearings, hub
# z; web z; skirt radii and z; lip; the window's seat.
HUB_BORE, HUB_D, SHOULDER_D, HUB_Z = 27.0, 38.0, 24.0, (-16.0, 8.0)
WEB_Z = (6.0, 8.0)
SKIRT = (30.0, 32.0, 0.5, 23.7)
LIP = (23.5, 21.0, 21.7)
SEAT_D = 49.7
# Magnets: count, circle radius, diameter, thickness, z of the lower
# face; pocket play.
MAGNETS = (22, 17.0, 4.0, 1.5, -6.25)
POCKET_PLAY = 0.1
# Carrier: diameter, z; M2 holes into the shaft's wall.
CARRIER_D, CARRIER_Z = 54.0, (9.0, 11.0)
CARRIER_SCREW_R, M2_CLEAR = 8.5, 2.4
# The display: z of its glass's front; its inserts in the model frame.
DISPLAY_TOP = 20.7
INSERTS = ((15.73, -14.9), (15.73, 14.1), (-21.27, 0.0))
# AS5600 module (hardware/kicad/modules/as5600_encoder.py): board side,
# thickness, corner chamfer, holes' inset and diameter on a 16.0 grid,
# the chip's centre on the board; SOIC-8 body; the chip's face radius.
MODULE = (22.5, 1.6, 1.2, 3.25, 3.9, (11.35, 11.15))
CHIP_BODY = (4.9, 3.9, 1.5)
CHIP_R = 20.5
# The board stands BOARD_GAP over the floor; its chip's height follows.
BOARD_GAP = 0.15
CHIP_Z = FLOOR_TOP + BOARD_GAP + MODULE[0] - MODULE[5][1]
# The wall the module is screwed to: thickness, half width, top.
BACKING = (2.0, 10.0, 3.0)
WIRE_WINDOW = (8.0, 6.0)
# Threaded inserts for the base's 4 screws, as in _gen_dial_base.py:
# hole, depth, wall; the boss's top; the bore over the hole for the
# screw's end, and its top.
INSERT, BOSS_TOP, RELIEF = (4.0, 5.0, 1.6), -11.0, (3.2, -11.5)


def _cyl(r, z0, z1, x=0.0, y=0.0):
    return base._cylinder(x, y, r, z0, z1)


def _ring(r0, r1, z0, z1):
    return BRepAlgoAPI_Cut(_cyl(r1, z0, z1), _cyl(r0, z0 - 1, z1 + 1)).Shape()


def _fuse(*shapes):
    out = shapes[0]
    for s in shapes[1:]:
        out = BRepAlgoAPI_Fuse(out, s).Shape()
    return out


def _cut(shape, *tools):
    for t in tools:
        shape = BRepAlgoAPI_Cut(shape, t).Shape()
    return shape


def _polar(r, angle):
    return r * math.cos(math.radians(angle)), r * math.sin(math.radians(angle))


def _x_drill(x0, y, z, radius, length):
    """A cylinder along +x from x0."""
    return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x0, y, z), gp_Dir(1, 0, 0)),
                                    radius, length).Shape()


def _module_frame():
    """(x of the board's front face, z of its lower edge)."""
    return CHIP_R + CHIP_BODY[2], FLOOR_TOP + BOARD_GAP


def case():
    """The case: wall, floor, bosses, shaft and the module's backing."""
    big = 60.0
    body = BRepAlgoAPI_Common(
        base._fluted(), base._box(-big, -big, JOINT, big, big, TOP)).Shape()
    body = _cut(body, base._revolve(base._profile(base.WALL, TOP + 1.0)))
    # Shadow gap at the joint, as on the dial base's top part.
    body = _cut(body, BRepAlgoAPI_Cut(
        _cyl(big, JOINT, JOINT + base.GROOVE),
        _cyl(base.BELLY[0] - base.GROOVE, JOINT - 1, JOINT + 1)).Shape())
    # Floor inside the bottom part's rim; above the rim a ring ties it
    # to the wall.
    rise, thick, gap = base.RIM
    rim_in = base.inner_radius(JOINT + rise) - gap - thick
    floor = _cyl(rim_in - gap, JOINT, FLOOR_TOP)
    ledge = _ring(rim_in - gap - 1.0, base.BELLY[0], FLOOR_TOP, LEDGE_TOP)
    inside = BRepAlgoAPI_Common(
        _fuse(floor, ledge), base._fluted()).Shape()
    body = _fuse(body, inside)
    # Bosses for the base's screws, threaded inserts from below.
    hole, deep, wall = INSERT
    for x, y, _a in base._screw_points():
        body = _fuse(body, _cyl(hole / 2 + wall, FLOOR_TOP, BOSS_TOP, x, y))
        body = _cut(body, _cyl(hole / 2, JOINT - 1, JOINT + deep, x, y),
                    _cyl(RELIEF[0] / 2, JOINT + deep - 0.5, RELIEF[1], x, y))
    # Shaft with its collar, cable bore through the floor, and a window
    # at its foot for the sensor's wires.
    body = _fuse(body, _cyl(COLLAR_D / 2, FLOOR_TOP, COLLAR_TOP),
                 _cyl(SHAFT_D / 2, COLLAR_TOP, SHAFT_TOP))
    half, slot_top = SHAFT_SLOT
    body = _cut(body, _cyl(BORE_D / 2, JOINT - 1, SHAFT_TOP + 1),
                base._box(0.0, -half, FLOOR_TOP, COLLAR_D, half, slot_top))
    for a in (30.0, 150.0, 270.0):
        x, y = _polar(CARRIER_SCREW_R, a)
        body = _cut(body, _cyl(0.8, SHAFT_TOP - 6.0, SHAFT_TOP + 1, x, y))
    # The wall behind the module, with the module's 4 holes and a window
    # for its wires.
    side, b_thick, _c, inset, _hole, (cx, _cy) = MODULE
    front, bottom = _module_frame()
    back = front + b_thick
    t, w, top = BACKING
    body = _fuse(body, base._box(back, -w, FLOOR_TOP - 0.01, back + t, w,
                                 top))
    for u in (inset, side - inset):
        for v in (inset, side - inset):
            body = _cut(body, _x_drill(back - 1, -cx + u, bottom + v,
                                       M3_CLEAR / 2, t + 2))
    ww, wh = WIRE_WINDOW
    body = _cut(body, base._box(back - 1, -ww / 2, bottom + side / 2 - wh / 2,
                                back + t + 1, ww / 2,
                                bottom + side / 2 + wh / 2))
    # The wires drop through the floor beside the backing and rise in
    # the shaft's bore through its window.
    body = _cut(body, _cyl(WIRE_D / 2, JOINT - 1, FLOOR_TOP + 1,
                           back + t + 3.0, 0.0))
    return base._merged(body)


def sleeve(z0, z1):
    """A sleeve on the shaft against the bearings' inner rings."""
    return _ring(SHAFT_D / 2, COLLAR_D / 2, z0, z1)


def rotor():
    """Hub, web, skirt, lip and the window's seat, with the pockets."""
    z0, z1 = HUB_Z
    hub = _ring(HUB_BORE / 2, HUB_D / 2, z0, z1)
    lower = BEARING_Z[0] + BEARING_W
    shoulder = _ring(SHOULDER_D / 2, HUB_BORE / 2 + 0.01, lower,
                     BEARING_Z[1])
    web = _ring(HUB_D / 2 - 0.01, SKIRT[0] + 0.01, *WEB_Z)
    r0, r1, s0, s1 = SKIRT
    skirt = _ring(r0, r1, s0, s1)
    rim, l0, l1 = LIP
    lip = _ring(rim, r0 + 0.01, l0, l1)
    seat = _ring(SEAT_D / 2, r0 + 0.01, l1, s1)
    body = _fuse(hub, shoulder, web, skirt, lip, seat)
    count, radius, diameter, thick, mz = MAGNETS
    half = diameter / 2 + POCKET_PLAY
    for k in range(count):
        pocket = _fuse(
            _cyl(half, mz - POCKET_PLAY / 2, mz + thick + POCKET_PLAY / 2,
                 radius, 0.0),
            base._box(radius, -half, mz - POCKET_PLAY / 2, HUB_D / 2 + 0.5,
                      half, mz + thick + POCKET_PLAY / 2))
        body = _cut(body, base._turned(pocket, 360.0 * k / count))
    return base._merged(body)


def carrier():
    """The display's carrier on the shaft."""
    plate = _cyl(CARRIER_D / 2, *CARRIER_Z)
    holes = [_cyl(CABLE_D / 2, CARRIER_Z[0] - 1, CARRIER_Z[1] + 1)]
    for a in (30.0, 150.0, 270.0):
        x, y = _polar(CARRIER_SCREW_R, a)
        holes.append(_cyl(1.1, CARRIER_Z[0] - 1, CARRIER_Z[1] + 1, x, y))
    for x, y in INSERTS:
        holes.append(_cyl(M2_CLEAR / 2, CARRIER_Z[0] - 1,
                          CARRIER_Z[1] + 1, x, y))
    return base._merged(_cut(plate, *holes))


def module():
    """The AS5600 module standing at +x, as OpenCASCADE solids."""
    side, thick, chamfer, inset, hole, (cx, _cy) = MODULE
    front, bottom = _module_frame()
    y0 = -cx
    board = base._box(front, y0, bottom, front + thick, y0 + side,
                      bottom + side)
    for (yy, zz) in ((y0, bottom), (y0 + side, bottom), (y0, bottom + side),
                     (y0 + side, bottom + side)):
        board = _cut(board, base._box(front - 1, yy - chamfer, zz - chamfer,
                                      front + 3, yy + chamfer, zz + chamfer))
    for u in (inset, side - inset):
        for v in (inset, side - inset):
            board = _cut(board, _x_drill(front - 1, y0 + u, bottom + v,
                                         hole / 2, thick + 2))
    length, width, height = CHIP_BODY
    chip = base._box(front - height, -length / 2, CHIP_Z - width / 2,
                     front, length / 2, CHIP_Z + width / 2)
    return [(board, WHITE), (chip, (0.12, 0.12, 0.13))]


def parts():
    """The printed parts.

    Returns:
        A dict of file stem to solid.
    """
    b0, b1 = BEARING_Z
    return {
        "knob_case": case(),
        "knob_spacer": sleeve(b0 + BEARING_W, b1),
        "knob_clamp": sleeve(b1 + BEARING_W, SHAFT_TOP),
        "knob_rotor": rotor(),
        "knob_carrier": carrier(),
    }


def placed_models():
    """The magnets and the AS5600 module where they sit, for STEP.

    Written with _solid.py and step_gen.py, whose output is the same on
    every run.

    Returns:
        A dict of file stem to step_gen.write's solids.
    """
    import _solid as so
    count, radius, diameter, thick, mz = MAGNETS
    magnets = []
    for k in range(count):
        x, y = _polar(radius, 360.0 * k / count)
        magnets.append(so.ext(so.circle(x, y, diameter / 2), mz, mz + thick,
                              NORTH if k % 2 == 0 else SOUTH))
    side, thick_b, chamfer, inset, hole, (cx, _cy) = MODULE
    front, bottom = _module_frame()
    y0, c = -cx, chamfer
    # The board's outline in the (y, z) plane, its corners cut at 45
    # degrees; frame X pushes it along +x.
    outline = [(y0 + c, bottom), (y0 + side - c, bottom),
               (y0 + side, bottom + c), (y0 + side, bottom + side - c),
               (y0 + side - c, bottom + side), (y0 + c, bottom + side),
               (y0, bottom + side - c), (y0, bottom + c)]
    holes = [so.circle(y0 + u, bottom + v, hole / 2)
             for u in (inset, side - inset) for v in (inset, side - inset)]
    length, width, height = CHIP_BODY
    module_shapes = [
        so.ext(outline, front, front + thick_b, WHITE, holes=holes,
               frame=so.X),
        # The chip: from the board's face towards the magnets.
        so.ext(so.rect(-length / 2, CHIP_Z - width / 2, length / 2,
                       CHIP_Z + width / 2), front - height, front,
               (0.12, 0.12, 0.13), frame=so.X)]
    return {"knob_magnets": so.items(magnets),
            "knob_as5600": so.items(module_shapes)}


def bought():
    """The bought parts where they sit, as (name, solid, colour)."""
    import step_check
    read = step_check.read
    out = []
    for s, c in read(_models.path("dial_base_bottom"))[1]:
        out.append(("dial_base_bottom", s, c or GRAPHITE))
    for s in base.neighbours()["cell"]:
        out.append(("cell", s, (0.55, 0.55, 0.58)))
    for z in BEARING_Z:
        for s, c in read(_models.path("bearing_6704_2rs"))[1]:
            out.append(("bearing", base._moved(s, z=z), c or STEEL))
    for s, c in read(_models.path("acrylic_disc_49_5"))[1]:
        out.append(("window", base._moved(s, z=LIP[2]), c))
    count, radius, diameter, thick, mz = MAGNETS
    for k in range(count):
        x, y = _polar(radius, 360.0 * k / count)
        out.append(("magnet", _cyl(diameter / 2, mz, mz + thick, x, y),
                    NORTH if k % 2 == 0 else SOUTH))
    for s, c in read(_models.path("esp32_s3_lcd_185"))[1]:
        out.append(("display", base._moved(s, z=DISPLAY_TOP), c or GRAPHITE))
    for s, c in module():
        out.append(("as5600", s, c))
    return out


def assembly(built):
    """Every solid of the knob, as (name, solid, colour)."""
    printed = [(stem, solid, ROTOR_RGB if stem == "knob_rotor" else GRAPHITE)
               for stem, solid in built.items()]
    return printed + bought()


def check(built, items):
    """Check the printed parts and the assembly's fit.

    Args:
        built: The printed parts.
        items: The assembly, as ``assembly`` gives it.

    Returns:
        A list of error texts, empty when all is well.
    """
    errors = []
    for stem, solid in built.items():
        if not BRepCheck_Analyzer(solid).IsValid():
            errors.append(f"{stem}: not a valid solid")
        if base.pieces(solid) != 1:
            errors.append(f"{stem}: {base.pieces(solid)} separate pieces")
    filled = sum(base.volume(BRepAlgoAPI_Common(built["knob_case"],
                                                flute).Shape())
                 for flute in base.flutes())
    if filled > 1e-3:
        errors.append(f"knob_case: {filled:.2f}mm3 lie in the flutes")
    boxes = [base._extent(s) for _n, s, _c in items]
    for i, (name, solid, _c) in enumerate(items):
        for j in range(i):
            other = items[j][0]
            if name == other and name in ("display", "as5600", "cell",
                                          "dial_base_bottom"):
                continue        # a part's own solids are its maker's
            a, b = boxes[i], boxes[j]
            if not all(a[k] < b[k + 3] - 1e-4 and b[k] < a[k + 3] - 1e-4
                       for k in range(3)):
                continue
            shared = base.volume(BRepAlgoAPI_Common(solid,
                                                    items[j][1]).Shape())
            if shared > 1e-3:
                errors.append(f"{name} and {other} share {shared:.3f}mm3")
    return errors


def render(items, directory):
    """Draw the knob whole, taken apart and cut open."""
    import step_render
    from OCP.AIS import AIS_Shape
    from OCP.Aspect import Aspect_TypeOfLine
    from OCP.Graphic3d import (Graphic3d_BufferType,
                               Graphic3d_MaterialAspect,
                               Graphic3d_NameOfMaterial)
    from OCP.Image import Image_AlienPixMap
    from OCP.Prs3d import Prs3d_LineAspect
    from OCP.TCollection import TCollection_AsciiString
    from OCP.V3d import V3d_TypeOfOrientation

    view = step_render.Renderer(1200, 1000)
    line = Prs3d_LineAspect(step_render._rgb(0.1, 0.1, 0.1),
                            Aspect_TypeOfLine.Aspect_TOL_SOLID, 1.0)

    def draw(shown_items, towards, name, edges):
        view.ctx.RemoveAll(False)
        for _n, solid, rgb in shown_items:
            shown = AIS_Shape(solid)
            shown.SetColor(step_render._rgb(*rgb))
            shown.SetMaterial(Graphic3d_MaterialAspect(
                Graphic3d_NameOfMaterial.Graphic3d_NOM_PLASTIC))
            shown.Attributes().SetFaceBoundaryDraw(edges)
            shown.Attributes().SetFaceBoundaryAspect(line)
            view.ctx.Display(shown, 1, -1, False)
        view.view.SetProj(towards)
        view.view.FitAll(0.06, False)
        view.view.Redraw()
        image = Image_AlienPixMap()
        view.view.ToPixMap(image, view.w, view.h,
                           Graphic3d_BufferType.Graphic3d_BT_RGB)
        image.Save(TCollection_AsciiString(str(_models.render(
            directory, name))))

    lift = {"window": 75.0, "display": 62.0, "knob_carrier": 50.0,
            "knob_clamp": 42.0, "knob_rotor": 32.0, "magnet": 32.0,
            "bearing": 18.0, "knob_spacer": 18.0, "knob_case": 0.0,
            "as5600": 0.0, "dial_base_bottom": -30.0, "cell": -22.0}
    apart = [(n, base._moved(s, z=lift.get(n, 0.0)), c)
             for n, s, c in items]
    half = base._box(-90, -90, -90, 90, 0, 90)
    section = [(n, BRepAlgoAPI_Cut(s, half).Shape(), c)
               for n, s, c in items if n != "cell"]
    side = V3d_TypeOfOrientation.V3d_XposYnegZpos
    draw(items, side, "knob", False)
    draw(apart, side, "knob_exploded", False)
    draw(section, V3d_TypeOfOrientation.V3d_Yneg, "knob_section", True)


def main(argv):
    """Write or check the files.

    Args:
        argv: Command-line arguments without the program name.

    Returns:
        0, or 1 for a stale file or a collision.
    """
    import step_gen
    built = parts()
    items = assembly(built)
    if "--render" in argv:
        render(items, argv[argv.index("--render") + 1])
        return 0
    errors = check(built, items)
    texts = {stem: base.step_text(stem, solid).replace(
        "_gen_dial_base.py", "_gen_knob.py") for stem, solid in built.items()}
    for stem, solids in placed_models().items():
        texts[stem] = step_gen.write(stem, solids)
    for stem, text in texts.items():
        path = _models.path(stem)
        if "--check" in argv:
            if not path.exists() or path.read_text() != text:
                errors.append(f"STALE {path.name}")
            continue
        path.write_text(text)
        if stem in built:
            solid = built[stem]
            BRepMesh_IncrementalMesh(solid, 0.03, False, 0.2)
            stl = StlAPI_Writer()
            stl.ASCIIMode = False
            stl.Write(solid, str(_models.path(stem, ".stl")))
            print(f"wrote {stem}.step and .stl, "
                  f"{base.volume(solid) / 1000:.1f}cm3")
        else:
            print(f"wrote {stem}.step")
    for error in errors:
        print(error)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
