#!/usr/bin/env python3
"""Measure the bench enclosure's STEP file with OpenCASCADE.

models/casing/rcbench_case.step is Onshape's export of the owner's
enclosure design, kept as delivered. This tool reads it and prints, for
the assembly and for each product, the solids, faces, volume, box and
placement; the box is in the frame the entry stands in. Lengths are mm;
the file's metre is converted on reading.

Usage:
    python3 hardware/mechanical/_measure_rcbench_case.py [FILE.step]

Needs OCP (pip install cadquery-ocp==8.0.1.1.0).

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import sys
from pathlib import Path

from OCP.Bnd import Bnd_Box
from OCP.BRepBndLib import BRepBndLib
from OCP.BRepCheck import BRepCheck_Analyzer
from OCP.BRepGProp import BRepGProp
from OCP.GProp import GProp_GProps
from OCP.STEPCAFControl import STEPCAFControl_Reader
from OCP.TCollection import TCollection_ExtendedString
from OCP.TDataStd import TDataStd_Name
from OCP.TDF import TDF_ChildIterator
from OCP.TDocStd import TDocStd_Document
from OCP.TopAbs import TopAbs_FACE, TopAbs_SOLID
from OCP.TopExp import TopExp_Explorer
from OCP.XCAFDoc import XCAFDoc_DocumentTool

HERE = Path(__file__).resolve().parent
DEFAULT = HERE / "models" / "casing" / "rcbench_case.step"


def _name(label):
    """Return the product name of a label, or "?"."""
    attr = TDataStd_Name()
    if label.FindAttribute(TDataStd_Name.GetID_s(), attr):
        return attr.Get().ToExtString()
    return "?"


def _count(shape, kind):
    """Return how many sub-shapes of a kind the shape holds."""
    found = TopExp_Explorer(shape, kind)
    n = 0
    while found.More():
        n += 1
        found.Next()
    return n


def _box(shape):
    """Return the tight box of a shape as (xmin, ymin, zmin, xmax, ...)."""
    box = Bnd_Box()
    BRepBndLib.AddOptimal_s(shape, box, False, False)
    low, high = box.CornerMin(), box.CornerMax()
    return (low.X(), low.Y(), low.Z(), high.X(), high.Y(), high.Z())


def _volume(shape):
    """Return the volume of a shape in mm3."""
    props = GProp_GProps()
    BRepGProp.VolumeProperties_s(shape, props)
    return props.Mass()


def _walk(tool, label, depth, valid):
    """Print one label and its children, indented by depth."""
    shape = tool.GetShape_s(label)
    if shape.IsNull():
        return
    trsf = shape.Location().Transformation()
    place = [[round(trsf.Value(r, c), 4) for c in range(1, 5)]
             for r in range(1, 4)]
    box = " ".join(f"{v:.3f}" for v in _box(shape))
    print(f"{'  ' * depth}{_name(label)}: solids "
          f"{_count(shape, TopAbs_SOLID)}, faces "
          f"{_count(shape, TopAbs_FACE)}, {_volume(shape) / 1000:.2f}cm3, "
          f"box {box}, placement {place}")
    valid.append(BRepCheck_Analyzer(shape).IsValid())
    child = TDF_ChildIterator(label)
    while child.More():
        _walk(tool, child.Value(), depth + 1, valid)
        child.Next()


def main(argv):
    """Print the measurements.

    Args:
        argv: Command-line arguments without the program name: the file.

    Returns:
        0 when every shape is valid, 1 otherwise.
    """
    path = Path(argv[0]) if argv else DEFAULT
    doc = TDocStd_Document(TCollection_ExtendedString("rcbench"))
    reader = STEPCAFControl_Reader()
    reader.SetNameMode(True)
    reader.ReadFile(str(path))
    reader.Transfer(doc)
    tool = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
    valid = []
    child = TDF_ChildIterator(doc.Main().FindChild(1))
    while child.More():
        _walk(tool, child.Value(), 0, valid)
        child.Next()
    print("valid" if all(valid) else "INVALID")
    return 0 if all(valid) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
