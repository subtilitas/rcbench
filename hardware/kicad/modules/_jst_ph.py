"""JST PH top-entry header B<n>B-PH-K-S: footprint, symbol and model.

Shared by the specs of the ES3C28P's sockets (es3c28p_*.py): each spec
names its pins and gets a footprint, a symbol and a 3D model of a
2.0mm JST PH header with that many pins.

Source: JST's PH catalogue ePH.pdf (jst-mfg.com; not in the
repository), "Header (Through-hole type)", top entry, and "PC board
layout": pitch 2.0mm; housing (n - 1) x 2.0 + 3.9mm wide, 4.5mm deep,
6.0mm high; the outline reaches 1.95mm past the first and the last pin
along the row and 1.7mm from the row to one long side (2.8mm to the
other); holes 0.7mm +0.1/-0mm; pins 3.4mm under the carrier's top side.
The holes are drawn 0.8mm, the top of that range: JST's note 3 asks for
a larger hole in glass fibre boards. The pads are 1.2 x 1.75mm, so 0.8mm
of copper parts two pads.

Estimated, not in the catalogue: the housing's floor (0.6mm) and walls
(0.6mm), the pins' section (0.5mm square) and their top (4.6mm over the
carrier). The model leaves out the key slots and the windows in the
walls.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

PITCH = 2.0
END, ROW, DEPTH, HEIGHT = 1.95, 1.7, 4.5, 6.0
DRILL, PAD = 0.8, (1.2, 1.75)
FLOOR, WALL, PIN, PIN_TOP, PIN_BELOW = 0.6, 0.6, 0.5, 4.6, 3.4


def width(n):
    """The housing's width for n pins."""
    return (n - 1) * PITCH + 2 * END


def pin_x(i):
    """The x of pin i (1-based) in the housing's frame."""
    return END + PITCH * (i - 1)


def footprint(name, desc, title, labels):
    """The footprint of an n-pin header, pin 1 at the origin.

    The board frame is the housing seen from the top: (0, 0) its corner
    on the long side 1.7mm from the row, pins along +x.

    Args:
        name: The footprint's name.
        desc: Its description.
        title: A short name printed on F.Fab, for example "ES3C28P UART".
        labels: The pin names, pin 1 first; F.SilkS prints them under
            the pads.

    Returns:
        The FOOTPRINT dict for kicad_gen.
    """
    n = len(labels)
    w = width(n)
    pads = [{"num": str(i), "x": pin_x(i), "y": ROW,
             "shape": "rect" if i == 1 else "oval", "size": PAD,
             "drill": DRILL} for i in range(1, n + 1)]
    x1 = pin_x(1)
    mark = ("poly", [(x1 - 0.35, 0.2), (x1 + 0.35, 0.2), (x1, 0.6)])
    # Odd pins print 0.8mm under the housing, even pins 1.6mm: a 4-letter
    # name at 0.6mm is wider than the 2.0mm pitch, so neighbours stagger.
    silk = [mark] + [("text", label, pin_x(i),
                      DEPTH + (0.8 if i % 2 else 1.6), 0.6)
                     for i, label in enumerate(labels, 1)]
    fab = [mark, ("text", title, w / 2, 3.6, 0.5 if n < 3 else 0.7)]
    return {"name": name, "desc": desc, "board": (w, DEPTH),
            "origin": (x1, ROW), "pads": pads, "silk": silk, "fab": fab,
            "value_at": (w / 2, DEPTH + 3.0)}


def symbol(name, desc, footprint_name, title, pins):
    """The symbol: one pin each, pin 1 at the top, all on the left.

    Args:
        name: The symbol's name.
        desc: Its description.
        footprint_name: The footprint it names.
        title: The socket's name, printed in the body under "ES3C28P".
        pins: (KiCad pin type, name) of each pin, pin 1 first.

    Returns:
        The SYMBOL dict for kicad_gen.
    """
    n = len(pins)
    top = 1.27 * (n - 1)
    out = [(kind, pin, str(i + 1), -7.62, top - 2.54 * i, 0)
           for i, (kind, pin) in enumerate(pins)]
    return {"name": name, "desc": desc, "footprint": footprint_name,
            "ref": "J", "body": (-5.08, top + 2.54, 10.16, top - 2.54 * n),
            "graphics": [("text", "ES3C28P", 5.08, 0.9, 1.0),
                         ("text", title, 5.08, -0.9, 1.0)],
            "pins": out}


def model(n):
    """The 3D model: floor, four walls and n pins.

    Args:
        n: Number of pins.

    Returns:
        The MODEL dict for step_gen: the floor is the "board", 0.6mm
        thick, with the footprint's holes; walls and pins are bodies.
    """
    w, t, h = width(n), WALL, HEIGHT - FLOOR
    bodies = [("box", 0.0, 0.0, w, t, h, "beige"),
              ("box", 0.0, DEPTH - t, w, DEPTH, h, "beige"),
              ("box", 0.0, t, t, DEPTH - t, h, "beige"),
              ("box", w - t, t, w, DEPTH - t, h, "beige")]
    s = PIN / 2
    for i in range(1, n + 1):
        x = pin_x(i)
        bodies.append(("box", x - s, ROW - s, x + s, ROW + s,
                       PIN_BELOW + PIN_TOP, "gold", -FLOOR - PIN_BELOW))
    return {"mount": "flat", "thickness": FLOOR, "color": "beige",
            "bodies": bodies}
