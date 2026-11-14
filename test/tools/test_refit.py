"""hardware/mechanical/_stl.py and _refit.py: a mesh back on its surfaces.

The STL reader splits a file into closed parts and refuses an open one.
The refit finds the planes of a box, the cylinder, cone and torus of
turned shapes with their radii, keeps them with noise on the vertices,
leaves a prism with a flat side flat, and writes helices within 0.02um.
The dial's own parts come out with a thread on body and nut.

SPDX-License-Identifier: MIT
"""

import math
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "hardware" / "mechanical"))
sys.path.insert(0, str(ROOT / "hardware" / "kicad" / "tools"))
import _refit  # noqa: E402
import _stl  # noqa: E402
import _surface  # noqa: E402
import m5dial  # noqa: E402
import step_gen  # noqa: E402


def write_stl(path, tris):
    """Write triangles (3 points each) as a binary STL."""
    raw = bytes(80) + struct.pack("<I", len(tris))
    for tri in tris:
        raw += struct.pack("<12fH", 0.0, 0.0, 0.0,
                           *(c for p in tri for c in p), 0)
    path.write_bytes(raw)


def quad(a, b, c, d):
    """The 2 triangles of a quadrilateral."""
    return [(a, b, c), (a, c, d)]


def box(x0, y0, z0, x1, y1, z1):
    """The 12 triangles of a box, counter-clockwise from outside."""
    p = [(x, y, z) for z in (z0, z1) for y in (y0, y1) for x in (x0, x1)]
    return (quad(p[0], p[2], p[3], p[1]) + quad(p[4], p[5], p[7], p[6])
            + quad(p[0], p[1], p[5], p[4]) + quad(p[2], p[6], p[7], p[3])
            + quad(p[0], p[4], p[6], p[2]) + quad(p[1], p[3], p[7], p[5]))


def indexed(tris):
    """Triangles of points as (points, index triples)."""
    index, points, out = {}, [], []
    for tri in tris:
        row = []
        for p in tri:
            key = tuple(round(c, 9) for c in p)
            if key not in index:
                index[key] = len(points)
                points.append(tuple(float(c) for c in p))
            row.append(index[key])
        out.append(tuple(row))
    return points, out


def turned(profile, steps, wobble=0.0):
    """A closed profile of (rho, z) turned about z, as a mesh.

    Args:
        profile: The profile, counter-clockwise with rho to the right
            and z up; it starts and ends on the axis.
        steps: Triangle columns around the axis.
        wobble: Vertices off the axis move this far in or out.

    Returns:
        (points, index triples), counter-clockwise from outside.
    """
    def at(k, j):
        rho, z = profile[k]
        if rho > 0:
            rho += wobble * math.sin(7.0 * k + 3.0 * (j % steps))
        a = 2 * math.pi * (j % steps) / steps
        return (rho * math.cos(a), rho * math.sin(a), z)

    tris = []
    for k in range(len(profile) - 1):
        for j in range(steps):
            a, b, c, d = at(k, j), at(k, j + 1), at(k + 1, j + 1), at(k + 1,
                                                                    j)
            if profile[k][0] > 0:
                tris.append((a, b, c))
            if profile[k + 1][0] > 0:
                tris.append((a, c, d))
    return indexed(tris)


def kinds(faces):
    """Surface kind to the faces' surfaces."""
    out = {}
    for surface, _same, _loops in faces:
        out.setdefault(surface[0], []).append(surface)
    return out


def test_box_file_is_one_closed_part(tmp_path):
    """A box reads as 8 points, 12 triangles and one part with its box."""
    path = tmp_path / "box.stl"
    write_stl(path, box(0, 0, 0, 2, 3, 4))
    points, tris = _stl.read(path)
    assert (len(points), len(tris)) == (8, 12)
    (ids,) = _stl.parts(points, tris)
    assert _stl.bounds(points, tris, ids) == ((0, 0, 0), (2, 3, 4))


def test_two_boxes_are_two_parts_in_order(tmp_path):
    """Parts come sorted by their lowest corner, not by file order."""
    path = tmp_path / "two.stl"
    write_stl(path, box(5, 0, 0, 6, 1, 1) + box(0, 0, 0, 1, 1, 1))
    points, tris = _stl.read(path)
    first, second = _stl.parts(points, tris)
    assert _stl.bounds(points, tris, first)[0] == (0, 0, 0)
    assert _stl.bounds(points, tris, second)[0] == (5, 0, 0)


def test_open_mesh_is_refused(tmp_path):
    """A box with one triangle missing is not a closed part."""
    path = tmp_path / "open.stl"
    write_stl(path, box(0, 0, 0, 1, 1, 1)[:-1])
    points, tris = _stl.read(path)
    with pytest.raises(ValueError, match="open"):
        _stl.parts(points, tris)


def test_truncated_file_is_refused(tmp_path):
    """A file one byte short of its triangle count does not read."""
    path = tmp_path / "short.stl"
    write_stl(path, box(0, 0, 0, 1, 1, 1))
    path.write_bytes(path.read_bytes()[:-1])
    with pytest.raises(ValueError, match="binary STL"):
        _stl.read(path)


def test_box_is_six_planes_and_twelve_lines():
    """A box keeps its 8 corners, 12 straight edges and 6 planes."""
    points, edges, faces, report = _refit.solid(*indexed(box(0, 0, 0, 2, 3,
                                                             4)))
    assert len(points) == 8
    assert [e[2] for e in edges] == [("line",)] * 12
    assert report == {"plane": (6, pytest.approx(52.0))}
    assert all(len(loops) == 1 and len(loops[0]) == 4
               for _s, _same, loops in faces)


def test_cylinder_is_one_cylinder_between_two_circles():
    """24 flat strips become one cylinder of the strips' radius."""
    _points, edges, faces, _report = _refit.solid(
        *turned([(0, 0), (5, 0), (5, 8), (0, 8)], 24))
    found = kinds(faces)
    assert sorted(found) == ["cylinder", "plane"]
    assert len(found["plane"]) == 2
    (_kind, origin, axis, radius), = found["cylinder"]
    assert radius == pytest.approx(5.0, abs=1e-9)
    assert origin[:2] == (0.0, 0.0) and axis == (0.0, 0.0, 1.0)
    assert sorted(e[2][0] for e in edges) == ["circle", "circle"]
    assert {round(e[2][3], 9) for e in edges} == {5.0}


def test_cylinder_survives_noise_on_its_vertices():
    """Vertices 0.2um off the cylinder still give it, radius within 0.5um."""
    _points, _edges, faces, _report = _refit.solid(
        *turned([(0, 0), (5, 0), (5, 8), (0, 8)], 24, wobble=2e-4))
    (_kind, _origin, _axis, radius), = kinds(faces)["cylinder"]
    assert radius == pytest.approx(5.0, abs=5e-4)


def test_cone_has_its_half_angle():
    """A frustum from radius 6 to 4 over 8 gives a cone of atan(0.25)."""
    _points, _edges, faces, _report = _refit.solid(
        *turned([(0, 0), (6, 0), (4, 8), (0, 8)], 24))
    (_kind, origin, axis, radius, angle), = kinds(faces)["cone"]
    assert angle == pytest.approx(math.atan(0.25), abs=1e-9)
    # The radius is given where the cone's own axis starts; it grows
    # along that axis, which here points down.
    assert axis == (0.0, 0.0, -1.0)
    assert radius == pytest.approx(6.0 - 0.25 * origin[2], abs=1e-9)


def test_rounded_edge_is_a_torus():
    """A quarter round of radius 1 in 6 steps gives a torus 4 and 1."""
    arc = [(4 + math.sin(a), 1 - math.cos(a))
           for a in (math.radians(15 * i) for i in range(7))]
    _points, _edges, faces, _report = _refit.solid(
        *turned([(0, 0)] + arc + [(5, 6), (0, 6)], 48))
    found = kinds(faces)
    (_kind, origin, _axis, major, minor), = found["torus"]
    assert (major, minor) == (pytest.approx(4.0, abs=1e-6),
                              pytest.approx(1.0, abs=1e-6))
    assert origin[2] == pytest.approx(1.0, abs=1e-6)
    assert len(found["cylinder"]) == 1


def test_prism_with_eight_sides_stays_flat():
    """8 flat sides 45 degrees apart are planes, not a cylinder."""
    _points, _edges, _faces, report = _refit.solid(
        *turned([(0, 0), (5, 0), (5, 8), (0, 8)], 8))
    assert sorted(report) == ["plane"]
    assert report["plane"][0] == 10


def test_helix_pieces_follow_the_helix():
    """Cubic pieces of 15 degrees stay within 0.02um of the helix."""
    frame = _surface.Frame((0.0, 0.0, 0.0), (0.0, 0.0, 1.0),
                           (1.0, 0.0, 0.0), False)
    points, pieces = _refit._helix_points(frame, 22.0, 1.0, 2.6, 0.3,
                                          0.3 + math.radians(40))
    assert pieces == 3 and len(points) == 10
    worst = 0.0
    for k in range(pieces):
        c = points[3 * k:3 * k + 4]
        for i in range(21):
            u = i / 20
            x, y, z = (
                (1 - u) ** 3 * c[0][j] + 3 * (1 - u) ** 2 * u * c[1][j]
                + 3 * (1 - u) * u * u * c[2][j] + u ** 3 * c[3][j]
                for j in range(3))
            worst = max(worst, abs(math.hypot(x, y) - 22.0), abs(
                z - 1.0 - 2.6 * math.atan2(y, x) / (2 * math.pi)))
    assert worst < 2e-5
    control, knots, mults = _refit._smooth(points)
    assert (len(control), knots, mults) == (8, [0.0, 1.0, 2.0, 3.0],
                                            [4, 2, 2, 4])


def test_step_text_names_the_surfaces():
    """The STEP text has the cylinder, its circles and is repeatable."""
    points, edges, faces, _report = _refit.solid(
        *turned([(0, 0), (5, 0), (5, 8), (0, 8)], 24))
    item = ("brep", points, edges, faces, (1.0, 0.5, 0.0))
    text = step_gen.write("cylinder", [item])
    assert text.count("CYLINDRICAL_SURFACE") == 1
    assert text.count("CIRCLE(") == 2
    assert text.count("ADVANCED_FACE") == 3
    assert text.count("MANIFOLD_SOLID_BREP") == 1
    assert text == step_gen.write("cylinder", [item])


def test_dial_parts_have_their_surfaces():
    """The 9 parts of the dial: few faces, a thread on body and nut."""
    parts = m5dial.mesh_parts()
    fitted = m5dial.refit()
    assert sorted(fitted) == sorted(parts) == sorted(
        name for name, _colour in m5dial.MESH_PARTS.values())
    for name, (_points, _edges, faces, report) in fitted.items():
        assert len(faces) < 0.25 * len(parts[name][1]), name
        assert ("helicoid" in report) == (name in ("body", "nut")), name
    assert fitted["seat_ring"][3].keys() == {"cylinder", "plane"}
    assert len(fitted["seat_ring"][2]) == 4
    assert _surface._pitches(_surface._Part(*parts["body"]), _surface.Frame(
        (0.0, 0.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0), False)) == [2.6]
