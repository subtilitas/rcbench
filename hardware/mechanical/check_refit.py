#!/usr/bin/env python3
"""Compare the refit M5Stack Dial solids with the mesh they come from.

m5dial.py fits surfaces to the 9 parts of vendor/m5stack_dial/Dial.stl.
This reads the written STEP files back with OpenCASCADE, meshes each of
the 9 solids to 0.02mm and holds it against its mesh part:

- every node and triangle centre of the solid's mesh lies within
  REFIT_TO_MESH of the part: no face stands off or reaches past it;
- every vertex and triangle centre of the part lies within
  MESH_TO_REFIT of the solid's mesh: no face is missing;
- the two volumes agree within VOLUME.

The part's triangles are chords of the surfaces, up to 0.085mm off them
and 0.13mm in the thread, so the two distances are not zero.

Usage:
    python3 hardware/mechanical/check_refit.py

Exit status 1 when a part fails. Needs OCP (pip install cadquery-ocp)
and numpy.
"""

import sys
from pathlib import Path

import numpy as np
from OCP.BRep import BRep_Tool
from OCP.BRepAdaptor import BRepAdaptor_Surface
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.TopAbs import TopAbs_FACE, TopAbs_REVERSED
from OCP.TopExp import TopExp_Explorer
from OCP.TopLoc import TopLoc_Location
from OCP.TopoDS import TopoDS

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "kicad" / "tools"))
import m5dial  # noqa: E402
import step_check  # noqa: E402

REFIT_TO_MESH = 0.15    # mm
MESH_TO_REFIT = 0.15    # mm
VOLUME = 0.01           # share of the part's volume
DEFLECTION = 0.02       # mm, of the solid's mesh
CELL = 0.5              # mm, side of the search grid's cells


def tessellate(solid):
    """Mesh a solid with OpenCASCADE.

    Args:
        solid: The solid.

    Returns:
        (nodes, triangles, kinds): the nodes as an (n, 3) array, the
        triangles as rows of 3 node indices turned to face outwards,
        and a dict of surface kind to its number of faces.
    """
    BRepMesh_IncrementalMesh(solid, DEFLECTION, False, 0.2, True)
    nodes, tris, kinds = [], [], {}
    walk = TopExp_Explorer(solid, TopAbs_FACE)
    while walk.More():
        face = TopoDS.Face(walk.Current())
        kind = str(BRepAdaptor_Surface(face).GetType()).rsplit("_", 1)[-1]
        kinds[kind] = kinds.get(kind, 0) + 1
        place = TopLoc_Location()
        mesh = BRep_Tool.Triangulation_s(face, place)
        if mesh is not None:
            move, base = place.Transformation(), len(nodes)
            for i in range(1, mesh.NbNodes() + 1):
                q = mesh.Node(i).Transformed(move)
                nodes.append((q.X(), q.Y(), q.Z()))
            for i in range(1, mesh.NbTriangles() + 1):
                a, b, c = mesh.Triangle(i).Get()
                if face.Orientation() == TopAbs_REVERSED:
                    a, c = c, a
                tris.append((base + a - 1, base + b - 1, base + c - 1))
        walk.Next()
    return np.array(nodes, float), np.array(tris, int), kinds


def _to_triangles(p, a, b, c):
    """Distance from each point p to its triangle (a, b, c)."""
    ab, ac, ap = b - a, c - a, p - a
    d1, d2 = (ab * ap).sum(1), (ac * ap).sum(1)
    bp = p - b
    d3, d4 = (ab * bp).sum(1), (ac * bp).sum(1)
    cp = p - c
    d5, d6 = (ab * cp).sum(1), (ac * cp).sum(1)
    vc, vb, va = d1 * d4 - d3 * d2, d5 * d2 - d1 * d6, d3 * d6 - d5 * d4
    total = va + vb + vc
    total = np.where(np.abs(total) < 1e-300, 1e-300, total)
    near = a + ab * (vb / total)[:, None] + ac * (vc / total)[:, None]
    with np.errstate(divide="ignore", invalid="ignore"):
        t = np.clip((d4 - d3) / ((d4 - d3) + (d5 - d6)), 0, 1)
        m = (va <= 0) & ((d4 - d3) >= 0) & ((d5 - d6) >= 0)
        near[m] = (b + (c - b) * t[:, None])[m]
        t = np.clip(d2 / (d2 - d6), 0, 1)
        m = (vb <= 0) & (d2 >= 0) & (d6 <= 0)
        near[m] = (a + ac * t[:, None])[m]
        t = np.clip(d1 / (d1 - d3), 0, 1)
        m = (vc <= 0) & (d1 >= 0) & (d3 <= 0)
        near[m] = (a + ab * t[:, None])[m]
    m = (d6 >= 0) & (d5 <= d6)
    near[m] = c[m]
    m = (d3 >= 0) & (d4 <= d3)
    near[m] = b[m]
    m = (d1 <= 0) & (d2 <= 0)
    near[m] = a[m]
    return np.linalg.norm(p - near, axis=1)


def distance(points, nodes, tris, reach):
    """Distance from each point to a triangle mesh.

    Args:
        points: The points, (n, 3).
        nodes: The mesh's nodes, (m, 3).
        tris: The mesh's triangles, rows of 3 node indices.
        reach: The largest distance looked for.

    Returns:
        The distances; ``reach`` for a point with no triangle that near.
    """
    low = np.minimum(nodes.min(0), points.min(0)) - reach - CELL
    corner = nodes[tris]
    first = np.floor((corner.min(1) - reach - low) / CELL).astype(int)
    last = np.floor((corner.max(1) + reach - low) / CELL).astype(int)
    cells = {}
    for i in range(len(tris)):
        for x in range(first[i, 0], last[i, 0] + 1):
            for y in range(first[i, 1], last[i, 1] + 1):
                for z in range(first[i, 2], last[i, 2] + 1):
                    cells.setdefault((x, y, z), []).append(i)
    cell = np.floor((points - low) / CELL).astype(int)
    order = np.lexsort((cell[:, 2], cell[:, 1], cell[:, 0]))
    sorted_cell = cell[order]
    step = np.flatnonzero(np.any(np.diff(sorted_cell, axis=0) != 0,
                                 axis=1)) + 1
    out = np.full(len(points), float(reach))
    pairs = [[], []]

    def flush():
        if pairs[0]:
            pi, ti = np.concatenate(pairs[0]), np.concatenate(pairs[1])
            found = _to_triangles(points[pi], corner[ti, 0], corner[ti, 1],
                                  corner[ti, 2])
            found[np.isnan(found)] = reach      # a triangle with no area
            np.minimum.at(out, pi, found)
            pairs[0].clear()
            pairs[1].clear()

    for begin, end in zip(np.concatenate(([0], step)),
                          np.concatenate((step, [len(points)])),
                          strict=True):
        near = cells.get(tuple(sorted_cell[begin]))
        if near:
            who = order[begin:end]
            pairs[0].append(np.repeat(who, len(near)))
            pairs[1].append(np.tile(np.array(near), len(who)))
            if sum(len(x) for x in pairs[0]) > 2_000_000:
                flush()
    flush()
    return out


def volume(nodes, tris):
    """The volume a closed triangle mesh encloses."""
    a, b, c = nodes[tris[:, 0]], nodes[tris[:, 1]], nodes[tris[:, 2]]
    return float((a * np.cross(b, c)).sum() / 6)


def check(name, solid, part):
    """Hold one solid against its mesh part.

    Args:
        name: The part's name.
        solid: The solid read from the STEP file.
        part: (points, triangles) of the mesh part.

    Returns:
        (line, ok): one line of the report, and whether the part passes.
    """
    points = np.array(part[0], float)
    tris = np.array(part[1], int)
    nodes, faces, kinds = tessellate(solid)
    reach = 4 * max(REFIT_TO_MESH, MESH_TO_REFIT)
    out = distance(np.vstack([nodes, nodes[faces].mean(1)]), points, tris,
                   reach).max()
    back = distance(np.vstack([points, points[tris].mean(1)]), nodes, faces,
                    reach).max()
    mesh_volume, refit_volume = volume(points, tris), volume(nodes, faces)
    ok = (out <= REFIT_TO_MESH and back <= MESH_TO_REFIT
          and abs(refit_volume - mesh_volume) <= VOLUME * mesh_volume)
    made = ", ".join(f"{n} {kind}" for kind, n in sorted(kinds.items()))
    return (f"{'ok  ' if ok else 'FAIL'} {name:10s} {len(tris):5d} triangles"
            f" -> {sum(kinds.values()):4d} faces ({made}); solid to mesh "
            f"{out:.3f}mm, mesh to solid {back:.3f}mm, volume "
            f"{mesh_volume:.1f} -> {refit_volume:.1f}mm3"), ok


def main():
    """Check the 9 parts.

    Returns:
        0 when every part passes, else 1.
    """
    parts = m5dial.mesh_parts()
    found = {}
    _shape, solids = step_check.read(HERE / "models" / "m5dial.step")
    for name, (solid, _colour) in zip(m5dial.DIAL, solids, strict=False):
        found[name] = solid
    _shape, solids = step_check.read(HERE / "models" / "m5dial_nut.step")
    found["nut"] = solids[0][0]
    bad = 0
    for name in sorted(parts):
        line, ok = check(name, found[name], parts[name])
        print(line, flush=True)
        bad += not ok
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
