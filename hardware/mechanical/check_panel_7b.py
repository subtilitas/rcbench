#!/usr/bin/env python3
"""Hold the panel board's model against Waveshare's STEP file of it.

The file is the one _measure_step.py reads; it is not in this
repository. Its solids are grouped into components as _measure_step.py
groups them. For each component the shapes of panel_7b.py whose centre
lies in the component's box are taken together, and the box round them
is compared with the component's box, side by side.

Usage:
    python3 hardware/mechanical/check_panel_7b.py FILE.stp

Prints the largest difference of a side for each kind of part and the
components no shape answers. Exit status 1 when a difference is above
the kind's limit in LIMITS or a component has no shape. Needs OCP (pip
install cadquery-ocp==8.0.1.1.0).

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import _measure_step as measure  # noqa: E402
import panel_7b  # noqa: E402

# mm, the most a side of a part's box may differ from the file's, by
# kind. A named part is drawn as a shape of _parts.py, to its own
# nominal size where the file's box holds legs through the board.
LIMITS = {"chip": 0.06, "leaded": 0.06, "box": 0.02, "named": 1.3}
REACH = 1.5             # mm


def shape_box(shape):
    """The box round one shape of _solid.py.

    Args:
        shape: The shape.

    Returns:
        (x0, y0, z0, x1, y1, z1).
    """
    outer, _holes, d0, d1, _rgb, (o, u, v, w) = shape
    points = [[o[k] + q[0] * u[k] + q[1] * v[k] + d * w[k] for k in range(3)]
              for q in outer for d in (d0, d1)]
    return tuple(min(q[k] for q in points) for k in range(3)) + tuple(
        max(q[k] for q in points) for k in range(3))


def compare(path):
    """Compare every component of the file with the model.

    Args:
        path: Waveshare's STEP file.

    Returns:
        (worst, missing): a dict of kind to the largest difference of a
        side in mm, and the boxes of the components no shape answers.
    """
    solids = [s for s in measure.measure(path)
              if max(s["box"][3] - s["box"][0],
                     s["box"][4] - s["box"][1]) < measure.SHEET]
    boxes = [shape_box(s) for s in panel_7b.measured() + panel_7b.connectors()
             + panel_7b.standoffs()]
    worst, missing = dict.fromkeys(LIMITS, 0.0), []
    for group in measure.components(solids):
        want = measure._union(group)
        dx, dy = want[3] - want[0], want[4] - want[1]
        size = (max(dx, dy), min(dx, dy))
        height = want[5] - max(want[2], 0.0)
        if measure._match(measure.CHIP_SIZES, (*size, height)) and len(
                group) in (1, 3, 4):
            kind = "chip"
        elif measure._match(measure.NAMED, (*size, len(group))):
            if measure._match(measure.NAMED,
                              (*size, len(group))) in measure.WRONG:
                continue
            kind = "named"
        elif measure._leaded(group) and size[0] < 8.0:
            kind = "leaded"
        else:
            kind = "box"
        # A shape answers the component when its centre lies in the
        # component's box and it is no more than REACH larger: the
        # shield can over the ESP32-P4 is not the ESP32-P4.
        inside = [b for b in boxes if all(
            want[k] - 0.01 <= (b[k] + b[k + 3]) / 2 <= want[k + 3] + 0.01
            and b[k] >= want[k] - REACH and b[k + 3] <= want[k + 3] + REACH
            for k in range(2))]
        if not inside:
            missing.append(want)
            continue
        got = tuple(min(b[k] for b in inside) for k in range(3)) + tuple(
            max(b[k] for b in inside) for k in range(3, 6))
        # Legs under the board are not drawn: compare from the board up.
        sides = [abs(got[k] - want[k]) for k in (0, 1, 3, 4, 5)]
        worst[kind] = max(worst[kind], *sides)
    return worst, missing


def main(argv):
    """Run from the command line.

    Args:
        argv: Waveshare's STEP file.

    Returns:
        0 when the model holds, else 1.
    """
    worst, missing = compare(Path(argv[0]))
    bad = len(missing)
    for kind, limit in LIMITS.items():
        ok = worst[kind] <= limit
        print(f"{'ok  ' if ok else 'FAIL'} {kind}: largest difference of a "
              f"side {worst[kind]:.2f}mm, limit {limit}mm")
        bad += not ok
    for box in missing:
        print("FAIL no shape for the component at "
              + ", ".join(f"{v:.2f}" for v in box))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
