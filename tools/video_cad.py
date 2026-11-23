#!/usr/bin/env python3
r"""Convert the case's STEP assembly into the model the video player loads.

The player's case scene draws tools/video/cad/case.glb when a storyboard
asks for the model "cad".  This tool writes that file from a STEP
(ISO 10303-21) assembly: it reads the assembly with its product names,
meshes every solid and writes one binary glTF (GL Transmission Format)
file.  Neither file is in the tree: the assembly is 65MB and the model
35MB.

    pip install cadquery-ocp==8.0.1.1.0
    python3 tools/video_cad.py rcbench_case.stp

    python3 tools/video_cad.py --solids \
        hardware/mechanical/models/parts/m5dial.step \
        -o tools/video/cad/m5dial.glb

With --solids the model holds one mesh a solid, named solid-0, solid-1 and
on in the file's order, each in its solid's colour: the player's dial
scene turns the knob, one solid, alone.  That needs numpy as well.

The case's STEP file carries no colours.  The player gives each part its colour
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


def convert_solids(step: pathlib.Path, model: pathlib.Path) -> int:
    """Write a STEP part as binary glTF with one mesh a solid.

    Args:
        step: The part.
        model: The .glb file to write.

    Returns:
        The count of solids written.
    """
    import json
    import struct

    try:
        import numpy as np
        from OCP.BRep import BRep_Tool
        from OCP.BRepMesh import BRepMesh_IncrementalMesh
        from OCP.collections import Sequence_TDF_Label
        from OCP.IFSelect import IFSelect_RetDone
        from OCP.Quantity import Quantity_Color
        from OCP.STEPCAFControl import STEPCAFControl_Reader
        from OCP.TCollection import TCollection_ExtendedString
        from OCP.TDocStd import TDocStd_Document
        from OCP.TopAbs import TopAbs_FACE, TopAbs_REVERSED, TopAbs_SOLID
        from OCP.TopExp import TopExp_Explorer
        from OCP.TopLoc import TopLoc_Location
        from OCP.TopoDS import TopoDS
        from OCP.XCAFDoc import (XCAFDoc_ColorGen, XCAFDoc_ColorSurf,
                                 XCAFDoc_DocumentTool)
    except ImportError:
        sys.exit("the conversion needs numpy and cadquery-ocp: "
                 "pip install numpy cadquery-ocp==8.0.1.1.0")

    doc = TDocStd_Document(TCollection_ExtendedString("XmlXCAF"))
    reader = STEPCAFControl_Reader()
    reader.SetColorMode(True)
    if reader.ReadFile(str(step)) != IFSelect_RetDone:
        sys.exit(f"{step} is not a STEP file this reader takes")
    if not reader.Transfer(doc):
        sys.exit(f"{step}: the part could not be transferred")
    shapes = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
    colours = XCAFDoc_DocumentTool.ColorTool_s(doc.Main())
    free = Sequence_TDF_Label()
    shapes.GetFreeShapes(free)
    blob = bytearray()
    views, accessors, meshes, materials, nodes = [], [], [], [], []
    for n in range(1, free.Length() + 1):
        shape = shapes.GetShape_s(free.Value(n))
        BRepMesh_IncrementalMesh(shape, DEFLECTION_MM, False, 0.5, True)
        solids = TopExp_Explorer(shape, TopAbs_SOLID)
        while solids.More():
            solid = solids.Current()
            # An assembly holds a colour on the solid as its part defines
            # it, without the place of this instance, or on its faces.
            colour = Quantity_Color()
            bare = solid.Located(TopLoc_Location())
            first = TopExp_Explorer(solid, TopAbs_FACE)
            tries = [solid, bare]
            if first.More():
                tries += [first.Current(),
                          first.Current().Located(TopLoc_Location())]
            for item in tries:
                if (colours.GetColor(item, XCAFDoc_ColorSurf, colour)
                        or colours.GetColor(item, XCAFDoc_ColorGen, colour)):
                    break
            else:
                colour = Quantity_Color()
            points, corners = [], []
            faces = TopExp_Explorer(solid, TopAbs_FACE)
            while faces.More():
                face = TopoDS.Face(faces.Current())
                place = TopLoc_Location()
                tri = BRep_Tool.Triangulation_s(face, place)
                if tri is not None:
                    move = place.Transformation()
                    base = len(points)
                    for k in range(1, tri.NbNodes() + 1):
                        at = tri.Node(k).Transformed(move)
                        points.append((at.X(), at.Y(), at.Z()))
                    for k in range(1, tri.NbTriangles() + 1):
                        a, b, c = tri.Triangle(k).Get()
                        if face.Orientation() == TopAbs_REVERSED:
                            b, c = c, b
                        corners.append((base + a - 1, base + b - 1,
                                        base + c - 1))
                faces.Next()
            solids.Next()
            if not corners:
                continue
            # glTF is in metres; the player turns them back into mm.
            pos = (np.asarray(points, dtype="<f4") / 1000.0)
            idx = np.asarray(corners, dtype="<u4")
            for data, target in ((pos, 34962), (idx, 34963)):
                views.append({"buffer": 0, "byteOffset": len(blob),
                              "byteLength": data.nbytes, "target": target})
                blob += data.tobytes()
            k = len(meshes)
            accessors.append({
                "bufferView": 2 * k, "componentType": 5126, "type": "VEC3",
                "count": len(pos), "min": pos.min(axis=0).tolist(),
                "max": pos.max(axis=0).tolist()})
            accessors.append({
                "bufferView": 2 * k + 1, "componentType": 5125,
                "type": "SCALAR", "count": idx.size})
            materials.append({"pbrMetallicRoughness": {
                "baseColorFactor": [colour.Red(), colour.Green(),
                                    colour.Blue(), 1.0],
                "metallicFactor": 0.1, "roughnessFactor": 0.6}})
            meshes.append({"primitives": [{
                "attributes": {"POSITION": 2 * k}, "indices": 2 * k + 1,
                "material": k}]})
            nodes.append({"name": f"solid-{k}", "mesh": k})
    head = json.dumps({
        "asset": {"version": "2.0"}, "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes)))}], "nodes": nodes,
        "meshes": meshes, "materials": materials, "accessors": accessors,
        "bufferViews": views, "buffers": [{"byteLength": len(blob)}],
    }).encode()
    head += b" " * (-len(head) % 4)
    blob += b"\0" * (-len(blob) % 4)
    model.parent.mkdir(parents=True, exist_ok=True)
    with model.open("wb") as out:
        out.write(struct.pack("<4sII", b"glTF", 2,
                              28 + len(head) + len(blob)))
        out.write(struct.pack("<I4s", len(head), b"JSON") + head)
        out.write(struct.pack("<I4s", len(blob), b"BIN\0") + bytes(blob))
    return len(nodes)


def _colours_of(step: pathlib.Path) -> list:
    """List the colour of each solid of a STEP part, in the file's order.

    Args:
        step: The part.

    Returns:
        (red, green, blue) from 0 to 1 per solid, None for a solid
        without a colour.
    """
    from OCP.collections import Sequence_TDF_Label
    from OCP.IFSelect import IFSelect_RetDone
    from OCP.Quantity import Quantity_Color
    from OCP.STEPCAFControl import STEPCAFControl_Reader
    from OCP.TCollection import TCollection_ExtendedString
    from OCP.TDocStd import TDocStd_Document
    from OCP.TopAbs import TopAbs_SOLID
    from OCP.TopExp import TopExp_Explorer
    from OCP.XCAFDoc import (XCAFDoc_ColorGen, XCAFDoc_ColorSurf,
                             XCAFDoc_DocumentTool)

    doc = TDocStd_Document(TCollection_ExtendedString("XmlXCAF"))
    reader = STEPCAFControl_Reader()
    reader.SetColorMode(True)
    if reader.ReadFile(str(step)) != IFSelect_RetDone \
            or not reader.Transfer(doc):
        return []
    shapes = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
    colours = XCAFDoc_DocumentTool.ColorTool_s(doc.Main())
    free = Sequence_TDF_Label()
    shapes.GetFreeShapes(free)
    found = []
    for n in range(1, free.Length() + 1):
        solids = TopExp_Explorer(shapes.GetShape_s(free.Value(n)),
                                 TopAbs_SOLID)
        while solids.More():
            colour = Quantity_Color()
            solid = solids.Current()
            if (colours.GetColor(solid, XCAFDoc_ColorSurf, colour)
                    or colours.GetColor(solid, XCAFDoc_ColorGen, colour)):
                found.append((round(colour.Red(), 3),
                              round(colour.Green(), 3),
                              round(colour.Blue(), 3)))
            else:
                found.append(None)
            solids.Next()
    return found


def convert_parts(step: pathlib.Path, model: pathlib.Path) -> list:
    """Write a STEP assembly as binary glTF with one node a component.

    Args:
        step: The assembly.
        model: The .glb file to write.

    Returns:
        The names of the nodes written: each top component's product
        name, with -2, -3 and on for a product placed more than once.
    """
    import json
    import struct

    try:
        import numpy as np
        from OCP.BRep import BRep_Tool
        from OCP.BRepMesh import BRepMesh_IncrementalMesh
        from OCP.collections import Sequence_TDF_Label
        from OCP.IFSelect import IFSelect_RetDone
        from OCP.Quantity import Quantity_Color
        from OCP.STEPCAFControl import STEPCAFControl_Reader
        from OCP.TCollection import TCollection_ExtendedString
        from OCP.TDataStd import TDataStd_Name
        from OCP.TDF import TDF_Label
        from OCP.TDocStd import TDocStd_Document
        from OCP.TopAbs import TopAbs_FACE, TopAbs_REVERSED, TopAbs_SOLID
        from OCP.TopExp import TopExp_Explorer
        from OCP.TopLoc import TopLoc_Location
        from OCP.TopoDS import TopoDS
        from OCP.XCAFDoc import (XCAFDoc_ColorGen, XCAFDoc_ColorSurf,
                                 XCAFDoc_DocumentTool)
    except ImportError:
        sys.exit("the conversion needs numpy and cadquery-ocp: "
                 "pip install numpy cadquery-ocp==8.0.1.1.0")

    doc = TDocStd_Document(TCollection_ExtendedString("XmlXCAF"))
    reader = STEPCAFControl_Reader()
    reader.SetColorMode(True)
    reader.SetNameMode(True)
    if reader.ReadFile(str(step)) != IFSelect_RetDone:
        sys.exit(f"{step} is not a STEP file this reader takes")
    if not reader.Transfer(doc):
        sys.exit(f"{step}: the assembly could not be transferred")
    shapes = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
    colours = XCAFDoc_DocumentTool.ColorTool_s(doc.Main())

    def name_of(label):
        name = TDataStd_Name()
        if label.FindAttribute(TDataStd_Name.GetID_s(), name):
            return name.Get().ToExtString()
        return "part"

    free = Sequence_TDF_Label()
    shapes.GetFreeShapes(free)
    tops = Sequence_TDF_Label()
    shapes.GetComponents_s(free.Value(1), tops)
    blob = bytearray()
    views, accessors, meshes, materials, nodes, names = [], [], [], [], [], []
    seen = {}
    for n in range(1, tops.Length() + 1):
        label = tops.Value(n)
        product = label
        if shapes.IsReference_s(label):
            product = TDF_Label()
            shapes.GetReferredShape_s(label, product)
        name = name_of(product)
        seen_name = name
        seen[name] = seen.get(name, 0) + 1
        if seen[name] > 1:
            name = f"{name}-{seen[name]}"
        shape = shapes.GetShape_s(label)
        BRepMesh_IncrementalMesh(shape, DEFLECTION_MM, False, 0.5, True)
        # An assembly written without colours: the part's own file beside
        # it, named as the product, gives them solid by solid.
        own = []
        for suffix in (".step", ".stp"):
            beside = step.with_name(seen_name + suffix)
            if beside.is_file() and beside != step:
                own = _colours_of(beside)
                break
        # The triangles of the component, gathered by colour.
        by_colour = {}
        count = 0
        solids = TopExp_Explorer(shape, TopAbs_SOLID)
        while solids.More():
            count += 1
            solids.Next()
        if len(own) != count:
            own = []
        index = -1
        solids = TopExp_Explorer(shape, TopAbs_SOLID)
        while solids.More():
            solid = solids.Current()
            index += 1
            colour = Quantity_Color()
            found = (colours.GetColor(solid, XCAFDoc_ColorSurf, colour)
                     or colours.GetColor(solid, XCAFDoc_ColorGen, colour))
            if not found:
                # A colour set on the part, not on its place.
                bare = solid.Located(TopLoc_Location())
                found = (colours.GetColor(bare, XCAFDoc_ColorSurf, colour)
                         or colours.GetColor(bare, XCAFDoc_ColorGen, colour))
            key = ((round(colour.Red(), 3), round(colour.Green(), 3),
                    round(colour.Blue(), 3)) if found else None)
            if key is None and own:
                key = own[index]
            points, corners = by_colour.setdefault(key, ([], []))
            faces = TopExp_Explorer(solid, TopAbs_FACE)
            while faces.More():
                face = TopoDS.Face(faces.Current())
                place = TopLoc_Location()
                tri = BRep_Tool.Triangulation_s(face, place)
                if tri is not None:
                    move = place.Transformation()
                    base = len(points)
                    for k in range(1, tri.NbNodes() + 1):
                        at = tri.Node(k).Transformed(move)
                        points.append((at.X(), at.Y(), at.Z()))
                    for k in range(1, tri.NbTriangles() + 1):
                        a, b, c = tri.Triangle(k).Get()
                        if face.Orientation() == TopAbs_REVERSED:
                            b, c = c, b
                        corners.append((base + a - 1, base + b - 1,
                                        base + c - 1))
                faces.Next()
            solids.Next()
        primitives = []
        for key, (points, corners) in by_colour.items():
            if not corners:
                continue
            pos = (np.asarray(points, dtype="<f4") / 1000.0)
            idx = np.asarray(corners, dtype="<u4")
            for data, target in ((pos, 34962), (idx, 34963)):
                views.append({"buffer": 0, "byteOffset": len(blob),
                              "byteLength": data.nbytes, "target": target})
                blob += data.tobytes()
            k = len(accessors)
            accessors.append({
                "bufferView": k, "componentType": 5126, "type": "VEC3",
                "count": len(pos), "min": pos.min(axis=0).tolist(),
                "max": pos.max(axis=0).tolist()})
            accessors.append({
                "bufferView": k + 1, "componentType": 5125,
                "type": "SCALAR", "count": idx.size})
            material = {"pbrMetallicRoughness": {
                "baseColorFactor": list(key or (0.5, 0.5, 0.5)) + [1.0],
                "metallicFactor": 0.1, "roughnessFactor": 0.6}}
            if key is None:
                material["name"] = "none"
            materials.append(material)
            primitives.append({"attributes": {"POSITION": k},
                               "indices": k + 1,
                               "material": len(materials) - 1})
        if not primitives:
            continue
        meshes.append({"primitives": primitives})
        nodes.append({"name": name, "mesh": len(meshes) - 1})
        names.append(name)
    head = json.dumps({
        "asset": {"version": "2.0"}, "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes)))}], "nodes": nodes,
        "meshes": meshes, "materials": materials, "accessors": accessors,
        "bufferViews": views, "buffers": [{"byteLength": len(blob)}],
    }).encode()
    head += b" " * (-len(head) % 4)
    blob += b"\0" * (-len(blob) % 4)
    model.parent.mkdir(parents=True, exist_ok=True)
    with model.open("wb") as out:
        out.write(struct.pack("<4sII", b"glTF", 2,
                              28 + len(head) + len(blob)))
        out.write(struct.pack("<I4s", len(head), b"JSON") + head)
        out.write(struct.pack("<I4s", len(blob), b"BIN\0") + bytes(blob))
    return names


def main() -> int:
    """Convert one assembly.

    Returns:
        The exit status.
    """
    ap = argparse.ArgumentParser()
    ap.add_argument("step", type=pathlib.Path, help="the STEP assembly")
    ap.add_argument("-o", "--output", type=pathlib.Path, default=MODEL)
    ap.add_argument("--solids", action="store_true",
                    help="one mesh a solid, in the solid's colour")
    ap.add_argument("--parts", action="store_true",
                    help="one node a top component, its solids' colours")
    args = ap.parse_args()
    if args.parts:
        print(", ".join(convert_parts(args.step, args.output)))
    elif args.solids:
        print(f"{convert_solids(args.step, args.output)} solids")
    else:
        convert(args.step, args.output)
    print(args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
