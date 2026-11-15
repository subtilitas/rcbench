"""A module's board for the specs: the plate and where its parts go.

A spec gives positions on the board in mm from the board's centre, x
to the right and y down as seen from the top side, the way they are
read off a photo. ``Board`` turns them into the model's frame, which is
the frame of the module's KiCad model: the origin at the footprint's
origin on the board's underside, x to the right, y up, z up.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import importlib.util
from pathlib import Path

import _parts as p
import _solid as s

MODULES = Path(__file__).resolve().parents[1] / "kicad" / "modules"


def kicad_spec(name):
    """Load a spec of hardware/kicad/modules/.

    Args:
        name: The file's name without ".py".

    Returns:
        The module.
    """
    spec = importlib.util.spec_from_file_location(
        name, MODULES / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Board:
    """The board of a KiCad spec.

    Args:
        spec: The KiCad spec; its FOOTPRINT gives size and origin.
        thick: The board's thickness.
    """

    def __init__(self, spec, thick=1.6):
        fp = spec.FOOTPRINT
        self.w, self.h = fp["board"]
        self.ox, self.oy = fp["origin"]
        self.t = thick

    def at(self, x, y):
        """The model's (x, y) of a place given from the board's centre."""
        return (self.w / 2 + x - self.ox, self.oy - self.h / 2 - y)

    def outline(self, radius):
        """The board's outline with rounded corners, in the model."""
        (x0, y1), (x1, y0) = (self.at(-self.w / 2, -self.h / 2),
                              self.at(self.w / 2, self.h / 2))
        return s.rounded(x0, y0, x1, y1, radius)

    def plate(self, rgb, outline, holes):
        """The board itself.

        Args:
            rgb: The solder mask's colour.
            outline: The outline in the model, as ``outline`` gives it.
            holes: (x, y, diameter) of each round hole.
        """
        return [s.ext(outline, 0.0, self.t, rgb, holes=[
            s.circle(*self.at(x, y), d / 2) for x, y, d in holes])]

    def top(self, shapes, x, y, turn=0.0, z=0.0):
        """Shapes put on the top side, z over the board's surface."""
        return s.place(shapes, *self.at(x, y), self.t + z, turn)

    def under(self, shapes, x, y, turn=0.0):
        """Shapes put on the underside, turned over."""
        return s.place(shapes, *self.at(x, y), 0.0, turn, flip=True)

    def through(self, shapes, x, y, turn=0.0):
        """Shapes whose origin is on the board's underside, upright."""
        return s.place(shapes, *self.at(x, y), 0.0, turn)

    def words(self, lines, under=False, rgb=p.WHITE, z=0.0):
        """Lines of print.

        Args:
            lines: (text, x, y, height, width) or (text, x, y, height,
                width, turn) of each line, (x, y) its centre.
            under: Whether the print is on the underside.
            rgb: The print's colour.
            z: The print's height over the board's surface.
        """
        out = []
        for string, x, y, height, width, *turn in lines:
            shapes = p.text(string, height, rgb, width)
            if under:
                out += self.under(s.place(shapes, z=z), x, y, *turn)
            else:
                out += self.top(shapes, x, y, *turn, z=z)
        return out
