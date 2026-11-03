#!/usr/bin/env python3
"""Load every STEP model of the library with OpenCASCADE and check it.

For each rcbench_modules.3dshapes/*.step: the file reads without error,
holds as many solids as its spec builds, every solid passes the B-rep
(boundary representation) check and is closed with a positive volume,
every solid carries a colour, and the bounding box matches the spec's
within 0.01mm. The models of hardware/mechanical/models/ are checked the
same way, bar the bounding box.

Usage:
    python3 hardware/kicad/tools/step_check.py [--png DIR]

--png writes a shaded view of each model to DIR (needs matplotlib).
Needs OCP, the OpenCASCADE binding: pip install cadquery-ocp.
"""

import argparse
import sys
from pathlib import Path

from OCP.Bnd import Bnd_Box
from OCP.BRepBndLib import BRepBndLib
from OCP.BRepCheck import BRepCheck_Analyzer
from OCP.BRepGProp import BRepGProp
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.BRep import BRep_Tool
from OCP.GProp import GProp_GProps
from OCP.IFSelect import IFSelect_RetDone
from OCP.Quantity import Quantity_Color
from OCP.STEPCAFControl import STEPCAFControl_Reader
from OCP.TCollection import TCollection_ExtendedString
from OCP.TDocStd import TDocStd_Document
from OCP.TopAbs import TopAbs_FACE, TopAbs_SOLID
from OCP.TopExp import TopExp_Explorer
from OCP.TopLoc import TopLoc_Location
from OCP.TopoDS import TopoDS
from OCP.XCAFDoc import XCAFDoc_ColorSurf, XCAFDoc_DocumentTool

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gen_all  # noqa: E402
import step_gen  # noqa: E402

sys.path.insert(0, str(HERE.parent.parent / "mechanical"))
import gen_models  # noqa: E402


def read(path):
    """Read one STEP file into an XCAF document.

    Args:
        path: The file.

    Returns:
        (shape, colour of each solid or None, in file order).

    Raises:
        RuntimeError: When the file does not read.
    """
    doc = TDocStd_Document(TCollection_ExtendedString("rcbench"))
    rd = STEPCAFControl_Reader()
    rd.SetColorMode(True)
    if rd.ReadFile(str(path)) != IFSelect_RetDone or not rd.Transfer(doc):
        raise RuntimeError(f"{path}: does not read")
    colors = XCAFDoc_DocumentTool.ColorTool_s(doc.Main())
    shape = rd.Reader().OneShape()
    found = []
    ex = TopExp_Explorer(shape, TopAbs_SOLID)
    while ex.More():
        col = Quantity_Color()
        ok = colors.GetColor(ex.Current(), XCAFDoc_ColorSurf, col)
        found.append((ex.Current(), (col.Red(), col.Green(), col.Blue())
                      if ok else None))
        ex.Next()
    return shape, found


def check(spec):
    """Check the model of one spec.

    Args:
        spec: The spec module.

    Returns:
        A list of error texts, empty when the model passes.
    """
    fp = spec.FOOTPRINT
    path = HERE.parent / f"{gen_all.LIB}.3dshapes" / f"{fp['name']}.step"
    want = step_gen.solids(fp, spec.MODEL)
    errs = []
    shape, found = read(path)
    if len(found) != len(want):
        errs.append(f"{len(found)} solids, spec builds {len(want)}")
    for i, (solid, col) in enumerate(found):
        if not BRepCheck_Analyzer(solid).IsValid():
            errs.append(f"solid {i}: B-rep check fails")
        props = GProp_GProps()
        BRepGProp.VolumeProperties_s(solid, props)
        if props.Mass() <= 0:
            errs.append(f"solid {i}: volume {props.Mass():.4f}")
        if col is None:
            errs.append(f"solid {i}: no colour")
    xs = [x for o, _h, _a, _b, _c in want for x, _y in o]
    ys = [y for o, _h, _a, _b, _c in want for _x, y in o]
    zs = [z for _o, _h, a, b, _c in want for z in (a, b)]
    box = Bnd_Box()
    box.SetGap(0.0)
    BRepBndLib.Add_s(shape, box)
    lo, hi = box.CornerMin(), box.CornerMax()
    got = (lo.X(), lo.Y(), lo.Z(), hi.X(), hi.Y(), hi.Z())
    exp = (min(xs), min(ys), min(zs), max(xs), max(ys), max(zs))
    if any(abs(g - e) > 0.01 for g, e in zip(got, exp, strict=True)):
        errs.append("bounding box " + " ".join(f"{v:.2f}" for v in got)
                    + ", spec " + " ".join(f"{v:.2f}" for v in exp))
    return errs


def _shade(tri, rgb):
    """Colour of a triangle lit from above left, by its normal."""
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = tri
    ux, uy, uz = bx - ax, by - ay, bz - az
    vx, vy, vz = cx - ax, cy - ay, cz - az
    n = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
    m = sum(c * c for c in n) ** 0.5 or 1.0
    light = (-0.4, -0.3, 0.87)
    k = 0.45 + 0.55 * abs(sum(n[i] * light[i] for i in range(3)) / m)
    return tuple(min(1.0, c * k) for c in rgb)


def check_file(path, n_solids):
    """Check one STEP file that is not a module of the library.

    Args:
        path: The file.
        n_solids: The number of solids its spec builds.

    Returns:
        A list of error texts, empty when the model passes.
    """
    errs = []
    _shape, found = read(path)
    if len(found) != n_solids:
        errs.append(f"{len(found)} solids, spec builds {n_solids}")
    for i, (solid, col) in enumerate(found):
        if not BRepCheck_Analyzer(solid).IsValid():
            errs.append(f"solid {i}: B-rep check fails")
        props = GProp_GProps()
        BRepGProp.VolumeProperties_s(solid, props)
        if props.Mass() <= 0:
            errs.append(f"solid {i}: volume {props.Mass():.4f}")
        if col is None:
            errs.append(f"solid {i}: no colour")
    return errs


def png(spec, out):
    """Write a shaded view of one model.

    Args:
        spec: The spec module.
        out: The PNG path.
    """
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    fp = spec.FOOTPRINT
    path = HERE.parent / f"{gen_all.LIB}.3dshapes" / f"{fp['name']}.step"
    _shape, found = read(path)
    fig = plt.figure(figsize=(8, 6), dpi=110)
    ax = fig.add_subplot(projection="3d")
    lo, hi = [1e9] * 3, [-1e9] * 3
    tris, cols = [], []
    for solid, col in found:
        BRepMesh_IncrementalMesh(solid, 0.05)
        ex = TopExp_Explorer(solid, TopAbs_FACE)
        while ex.More():
            face = TopoDS.Face(ex.Current())
            loc = TopLoc_Location()
            tri = BRep_Tool.Triangulation_s(face, loc)
            if tri is not None:
                tf = loc.Transformation()
                pts = [tri.Node(i + 1).Transformed(tf)
                       for i in range(tri.NbNodes())]
                for k in range(1, tri.NbTriangles() + 1):
                    t = [(p.X(), p.Y(), p.Z())
                         for p in (pts[i - 1] for i in tri.Triangle(k).Get())]
                    tris.append(t)
                    cols.append(_shade(t, col or (1, 0, 1)))
                    for p in t:
                        lo = [min(lo[i], p[i]) for i in range(3)]
                        hi = [max(hi[i], p[i]) for i in range(3)]
            ex.Next()
    # One collection, so the depth sort runs across all solids.
    ax.add_collection3d(Poly3DCollection(tris, facecolors=cols,
                                         edgecolors="none"))
    span = max(h - lo_ for h, lo_ in zip(hi, lo, strict=True)) / 2
    for i, setter in enumerate((ax.set_xlim, ax.set_ylim, ax.set_zlim)):
        c = (hi[i] + lo[i]) / 2
        setter(c - span, c + span)
    ax.set_box_aspect((1, 1, 1))
    ax.view_init(elev=35, azim=-60)
    ax.set_title(fp["name"])
    fig.savefig(out)
    plt.close(fig)


def main(argv):
    """Check every model; optionally draw each.

    Args:
        argv: Command-line arguments without the program name.

    Returns:
        0 when every model passes, 1 otherwise.
    """
    ap = argparse.ArgumentParser()
    ap.add_argument("--png", type=Path)
    args = ap.parse_args(argv)
    bad = 0
    for spec in gen_all.load_specs():
        if not getattr(spec, "MODEL", None):
            print(f"NO MODEL {spec.FOOTPRINT['name']}")
            bad += 1
            continue
        errs = check(spec)
        name = spec.FOOTPRINT["name"]
        for e in errs:
            print(f"FAIL {name}: {e}")
        if not errs:
            print(f"ok   {name}")
        bad += bool(errs)
        if args.png:
            args.png.mkdir(parents=True, exist_ok=True)
            png(spec, args.png / f"{name}.png")
    for path, (_text, items) in gen_models.outputs().items():
        errs = check_file(path, len(items))
        for e in errs:
            print(f"FAIL {path.name}: {e}")
        if not errs:
            print(f"ok   {path.name}")
        bad += bool(errs)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
