"""Two lithium polymer cells with protection board, wires and plug.

    lipo_702030_380mah     print "702030 3.7V 380mAh 1.406Wh"
    lipo_102540_1000mah    print "102540 3.7V 1000mAh"

Each cell is a foil pouch, a head at one end where the folded seal and
the protection board lie under tape, and a red and a black wire that
leave the head's end sideways and end in a white 2 pin plug.

Sources: three owner photos on 5mm squared paper (the cells lying
flat, and twice standing on a long edge; not in the repository). The
camera stands 171 over the paper, so a face h over the paper reads
larger by 171 / (171 - h); the sizes below are divided by that.

    702030: 31.0 long, of which the head 9.0; 20.0 wide over the two
    side seams, which stand 1.7 out and are folded up; on edge it
    reads 6.7 thick. Tape amber.
    102540: 40.5 long, of which the head 8.3; 24.8 wide, the side
    seams flat on the pouch; on edge it reads 9.7 thick. Tape black.

The type numbers give thickness, width and length: 7.0 x 20 x 30 and
10 x 25 x 40. The models take 7.0 and 10.0 thick and 20.0 and 25.0
wide, and the lengths of the photos, which include the head. Lengths
are within about 1.

The print is the font of _font.py set to the height and length each
line has in the photo, not traced.

Not measured, estimated: the head's thickness (5.0 and 8.0), the
pouch's edge radius (0.3 of the thickness), the wires' diameter (1.2
in the photo) and length (about 60; drawn straight), the plug's
height (2.1). The plug is 3.6 x 4.4 in the photo; its type and pitch
are not identified, the model draws 1.25 between the wires. Which wire
is plus is not measured; red is drawn as on the cells.

Frame of both models: origin at the middle of the cell's width, at
the end away from the head, on the face the cell lies on. x across
the cell, y towards the head, z up; the print is on the upper face and
the wires leave towards -x. All sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _parts as p
import _solid as s

INK = (0.15, 0.15, 0.16)
WIRE = 60.0
# Stem: width, length, thickness, head's length and thickness, tape,
# side seam, and the print as (text, x, y, letter height, length).
CELLS = {
    "lipo_702030_380mah": (20.0, 31.0, 7.0, 9.0, 5.0, p.KAPTON, 1.7, (
        ("702030", 2.45, 8.05, 1.4, 5.75), ("PL", 2.45, 15.9, 1.4, 1.5),
        ("-", 2.45, 18.8, 1.4, 0.6), ("380mAh", 0.15, 8.05, 1.4, 5.75),
        ("3.7V", 0.15, 15.25, 1.4, 3.1), ("+", 0.15, 18.85, 1.4, 0.7),
        ("2610", -2.1, 6.8, 1.4, 3.25),
        ("1.406Wh", -2.1, 13.3, 1.4, 7.2))),
    "lipo_102540_1000mah": (25.0, 40.5, 10.0, 8.3, 8.0, p.BLACK, 0.0, (
        ("102540", 3.45, 20.65, 1.8, 8.5),
        ("3.7V 1000mAh", 0.45, 15.8, 1.8, 17.8),
        ("3.7Wh", -2.35, 21.5, 1.8, 7.1), ("+", -2.35, 27.6, 1.8, 0.8))),
}


def cell(stem):
    """One cell as shapes.

    Args:
        stem: A key of CELLS.

    Returns:
        The shapes: pouch, head, print, wires and plug.
    """
    width, length, thick, head, head_thick, tape, seam, lines = CELLS[stem]
    out = p.pouch_cell(width, length, thick, head, head_thick, tape, seam)
    for string, x, y, height, size in lines:
        out += s.place(p.text(string, height, INK, size), x, y, thick,
                       -90.0)
    out += s.place(p.cell_lead(WIRE), -width / 2, length - 1.2, thick / 2,
                   180.0)
    return out


def parts(_colors):
    """The models of the two cells.

    Returns:
        A dict of file stem to solids.
    """
    return {stem: s.items(cell(stem)) for stem in CELLS}
