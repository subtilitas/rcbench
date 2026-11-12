#!/usr/bin/env python3
"""Convert the case's STEP assembly into the model the video player loads.

The player's case scene draws tools/video/cad/case.glb when a storyboard
asks for the model "cad".  This tool writes that file from a STEP
(ISO 10303-21) assembly: it reads the assembly with its product names,
meshes every solid and writes one binary glTF (GL Transmission Format)
file.  Neither file is in the tree: the assembly is 65MB and the model
35MB.

    pip install cadquery-ocp==8.0.1.1.0
    python3 tools/video_cad.py rcbench_case.stp

The STEP file carries no colours.  The player gives each part its colour
from its product name (tools/video/player/case_cad.js), so the names of
the assembly's parts are part of this interface: case_front, powerbank,
XT60EF, estop_m16_assembly, rbmod 1 and the others listed there.

Attributes:
    MODEL: The file the player loads.
    DEFLECTION_MM: The largest distance between a surface and its mesh.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import pathlib
import sys

MODEL = (pathlib.Path(__file__).resolve().parent / "video" / "cad"
         / "case.glb")
DEFLECTION_MM = 0.15


def convert(step: pathlib.Path, model: pathlib.Path) -> None:
    """Mesh a STEP assembly and write it as binary glTF.

    Args:
        step: The assembly.
        model: The .glb file to write.
    """
    try:
        from OCP.BRepMesh import BRepMesh_IncrementalMesh
        from OCP.collections import (
            IndexedDataMap_TCollection_AsciiString_TCollection_AsciiString
            as FileInfo)
        from OCP.collections import Sequence_TDF_Label
        from OCP.IFSelect import IFSelect_RetDone
        from OCP.Message import Message_ProgressRange
        from OCP.RWGltf import RWGltf_CafWriter
        from OCP.RWMesh import RWMesh_NameFormat_ProductOrInstance
        from OCP.STEPCAFControl import STEPCAFControl_Reader
        from OCP.TCollection import (TCollection_AsciiString,
                                     TCollection_ExtendedString)
        from OCP.TDocStd import TDocStd_Document
        from OCP.XCAFDoc import XCAFDoc_DocumentTool
    except ImportError:
        sys.exit("the conversion needs cadquery-ocp: "
                 "pip install cadquery-ocp==8.0.1.1.0")

    doc = TDocStd_Document(TCollection_ExtendedString("XmlXCAF"))
    reader = STEPCAFControl_Reader()
    reader.SetNameMode(True)
    if reader.ReadFile(str(step)) != IFSelect_RetDone:
        sys.exit(f"{step} is not a STEP file this reader takes")
    if not reader.Transfer(doc):
        sys.exit(f"{step}: the assembly could not be transferred")
    tool = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
    free = Sequence_TDF_Label()
    tool.GetFreeShapes(free)
    for n in range(1, free.Length() + 1):
        BRepMesh_IncrementalMesh(tool.GetShape_s(free.Value(n)),
                                 DEFLECTION_MM, False, 0.5, True)
    model.parent.mkdir(parents=True, exist_ok=True)
    writer = RWGltf_CafWriter(TCollection_AsciiString(str(model)), True)
    writer.SetNodeNameFormat(RWMesh_NameFormat_ProductOrInstance)
    writer.SetMeshNameFormat(RWMesh_NameFormat_ProductOrInstance)
    if not writer.Perform(doc, FileInfo(), Message_ProgressRange()):
        sys.exit(f"{model} could not be written")


def main() -> int:
    """Convert one assembly.

    Returns:
        The exit status.
    """
    ap = argparse.ArgumentParser()
    ap.add_argument("step", type=pathlib.Path, help="the STEP assembly")
    ap.add_argument("-o", "--output", type=pathlib.Path, default=MODEL)
    args = ap.parse_args()
    convert(args.step, args.output)
    print(args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
