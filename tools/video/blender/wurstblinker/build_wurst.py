r"""Build the Wurstblinker video's 3D scenes in Blender, cel shaded.

The player draws the cartoon with tools/video/player/wurst3d.js.  This
script builds the same scene from the two STEP models of
hardware/mechanical/wurstblinker.py, converted to .glb by
tools/video_cad.py --solids, and keys it per video frame from a timeline
(wurst_timeline.py): camera, the arm's swing, the lamp, the three
strokes, the dog's jump, the shot, its hit on the traffic light, its
fall to the ground and the muzzle puffs.

The look, as wurst3d.js draws it:

- Every part is cel shaded in three tones: MeshToonMaterial with the ramp
  110/190/255, lit by AmbientLight 1.5 and a DirectionalLight 2.4 from
  (-160, 300, 260).  Radiance = albedo * (1.5 + 2.4 * ramp(n.l / 2 +
  .5)) / pi.  The parts do not receive shadows in the player, so a part's
  colour depends on n.l alone.  The player tone maps with
  ACESFilmicToneMapping at exposure 1.15; this script applies the same
  curve to each of the three colours in Python, and the material emits
  the result under the view transform Standard.  No specular.
- Every part carries an outline: an inverted hull, the mesh moved 1.8mm
  out along its vertex normals, faces reversed, back faces culled, colour
  0x10131c.
- A pale disc 0xfff1c2, radius 360mm, at (90, 150, -140) behind the
  model; the kinetic scene has a second one, radius 300mm, at
  (1150, 150, -160) behind the traffic light, the dog scene a third,
  radius 560mm, at (120, -330, -150).
- The ground at y = 0 shows the sun's shadow only, at the player's
  opacity .5, inside the player's shadow camera (+-300mm, depth 10mm to
  900mm).

Rounding: the STEP sausage is a flat-ended cylinder, the antenna's tip
and the lamp short cylinders, the clips and the barrel's rings flat
rings, the tank a flat-ended cylinder.  Each is replaced by its round
shape (capsule, ball, torus) of the solid's size.  The boxes get their
edges rounded with wurst3d.js's radii.  The sausage carries a knot at
each end, the gauge a needle and the barrel a dark bore (wurst3d.js).
The barrel's rings take the player's colour 0x5f6a80.  The traffic
light, the mast, the dog and the further discs have no STEP model: they
are built from wurst3d.js's sizes.

Usage (Blender 5.3, headless):

    blender -b --factory-startup --python-exit-code 1 \
        --python build_wurst.py -- CFG.json

CFG keys: timeline (wurst_timeline.py's output), glb_node, glb_kinetic,
out (the .blend); optional samples (default 32), sun_angle (degrees,
default 1.0).

Coordinates: every object hangs under the empty "root", turned +90
degrees about X, so local coordinates under it are three.js's (mm, y up,
z towards the first camera).  STEP (z up, +y away from the first view)
maps to three.js as (x, z, -y).

SPDX-License-Identifier: MIT
"""

import json
import math
import re
import struct
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

CFG = json.load(open(sys.argv[sys.argv.index("--") + 1]))
TL = json.load(open(CFG["timeline"]))
FR = TL["frames"]
N3D = max(f["i"] for f in FR if f["is3d"]) + 1  # Blender frames 1..N3D
F3 = FR[:N3D]

INK, EDGE = 0x10131C, 1.8
SUN = (-160.0, 300.0, 260.0)  # wurst3d.js sun.position, three.js frame
AMBIENT, DIRECT, RAMP = 1.5, 2.4, (110, 190, 255)
EXPOSURE = 1.15  # player.js toneMappingExposure
SUN_E = 3.0  # Blender sun strength; it casts the ground's shadow only
SHADOW_BOX = (300.0, 10.0, 900.0)  # half width, near, far (wurst3d.js)


def result(*parts):
    """Print one greppable RESULT line.

    Args:
        *parts: The words of the line.
    """
    print("RESULT", *parts, flush=True)


# ------------------------------------------------------------------ colour
def srgb_lin(h):
    """Convert an sRGB hex colour to linear RGB, as three.js Color does.

    Args:
        h: The colour, 0xRRGGBB.

    Returns:
        The linear RGB values, 0..1.
    """
    c = [((h >> s) & 255) / 255 for s in (16, 8, 0)]
    return np.array(
        [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4
         for x in c]
    )


ACES_IN = np.array([
    [0.59719, 0.35458, 0.04823],
    [0.07600, 0.90834, 0.01566],
    [0.02840, 0.13383, 0.83777],
])
ACES_OUT = np.array([
    [1.60475, -0.53108, -0.07367],
    [-0.10208, 1.10813, -0.00605],
    [-0.00327, -0.07276, 1.07602],
])


def aces(rgb):
    """Tone map as three.js ACESFilmicToneMapping does.

    Args:
        rgb: Linear radiance.

    Returns:
        The tone-mapped linear colour, clamped to 0..1.
    """
    v = ACES_IN @ (np.asarray(rgb, dtype=float) * EXPOSURE / 0.6)
    v = (v * (v + 0.0245786) - 0.000090537) / (
        v * (0.983729 * v + 0.4329510) + 0.238081
    )
    return np.clip(ACES_OUT @ v, 0, 1)


def toon_tones(albedo, emissive=(0, 0, 0)):
    """Give the three output colours of a MeshToonMaterial.

    Args:
        albedo: The linear base colour.
        emissive: The linear emissive colour added before tone mapping.

    Returns:
        Shade, body and light colours, linear, tone mapped.
    """
    return [
        aces(np.asarray(albedo) * (AMBIENT + DIRECT * g / 255) / math.pi
             + np.asarray(emissive))
        for g in RAMP
    ]


# ------------------------------------------------------------------ glb
def load_glb(path):
    """Read a tools/video_cad.py --solids file.

    Args:
        path: The .glb file.

    Returns:
        Per solid, in the STEP's order: positions (mm, STEP frame),
        triangles and linear colour.
    """
    raw = open(path, "rb").read()
    jlen = struct.unpack("<I", raw[12:16])[0]
    doc = json.loads(raw[20 : 20 + jlen])
    off = 20 + jlen
    blen = struct.unpack("<I", raw[off : off + 4])[0]
    binary = raw[off + 8 : off + 8 + blen]
    types = {5125: np.uint32, 5126: np.float32, 5123: np.uint16}
    ncomp = {"SCALAR": 1, "VEC3": 3}

    def acc(i):
        a = doc["accessors"][i]
        v = doc["bufferViews"][a["bufferView"]]
        n = ncomp[a["type"]]
        start = v.get("byteOffset", 0) + a.get("byteOffset", 0)
        out = np.frombuffer(
            binary, types[a["componentType"]], a["count"] * n, start
        )
        return out.reshape(-1, n) if n > 1 else out

    solids = []
    for nd in doc["nodes"]:
        if "mesh" not in nd:
            continue
        k = int(re.sub(r"\D", "", nd["name"]))
        prim = doc["meshes"][nd["mesh"]]["primitives"][0]
        pos = acc(prim["attributes"]["POSITION"]).astype(float) * 1000.0
        tri = acc(prim["indices"]).astype(int).reshape(-1, 3)
        pbr = doc["materials"][prim["material"]]["pbrMetallicRoughness"]
        solids.append((k, pos, tri, pbr["baseColorFactor"][:3]))
    solids.sort(key=lambda s: s[0])
    return [s[1:] for s in solids]


def step_to_three(p):
    """Map STEP (x, y, z), z up, to three.js (x, z, -y).

    Args:
        p: Points, last axis xyz.

    Returns:
        The points in the three.js frame.
    """
    p = np.asarray(p, dtype=float)
    return np.stack([p[..., 0], p[..., 2], -p[..., 1]], axis=-1)


# ------------------------------------------------------------ scene helpers
def clear_scene():
    """Remove every data block the factory scene holds."""
    for coll in (
        bpy.data.objects, bpy.data.meshes, bpy.data.materials,
        bpy.data.images, bpy.data.cameras, bpy.data.lights,
        bpy.data.node_groups, bpy.data.actions, bpy.data.worlds,
        bpy.data.curves,
    ):
        for item in list(coll):
            coll.remove(item)


def set_enum(owner, prop, wanted):
    """Assign the first value Blender accepts.

    Headless Blender under-reports the render engine and view transform
    enums, so the values are tried inside try/except.

    Args:
        owner: The data block.
        prop: The property's name.
        wanted: The values to try, in order.

    Returns:
        The value assigned.

    Raises:
        TypeError: Blender accepts none of them.
    """
    err = None
    for name in wanted:
        try:
            setattr(owner, prop, name)
            return name
        except TypeError as e:
            err = e
    raise err


def empty(name, parent=None, loc=(0, 0, 0)):
    """Add an empty.

    Args:
        name: Its name.
        parent: Its parent, or None.
        loc: Its location in the parent's frame.

    Returns:
        The empty.
    """
    ob = bpy.data.objects.new(name, None)
    ob.empty_display_size = 5
    bpy.context.scene.collection.objects.link(ob)
    ob.parent = parent
    ob.location = loc
    return ob


def emission_material(name, rgb_lin):
    """Make an unlit material, as MeshBasicMaterial after tone mapping.

    Args:
        name: The material's name.
        rgb_lin: The linear colour it emits.

    Returns:
        The material.
    """
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = (*rgb_lin, 1)
    em.inputs["Strength"].default_value = 1.0
    nt.links.new(em.outputs[0], out.inputs["Surface"])
    return m


def sockets(group):
    """Index sockets by identifier (stable across languages).

    Args:
        group: A node's inputs or outputs.

    Returns:
        A dict of identifier to socket.
    """
    return {s.identifier: s for s in group}


SUN_DIR_WORLD = None  # set once the root exists


def toon_material(name, albedo, emissive=None):
    """Make a three-tone material picked by n.l at the ramp's steps.

    The steps lie at u = n.l / 2 + .5 = 1/3 and 2/3.  With @p emissive, a
    second set of tones has the emissive colour added; the Mix node
    "glow" blends the two sets, keyed 0..1 (wurst3d.js
    emissiveIntensity).

    Args:
        name: The material's name.
        albedo: The linear base colour.
        emissive: The linear emissive colour, or None.

    Returns:
        The material.
    """
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    dot = nt.nodes.new("ShaderNodeVectorMath")
    dot.operation = "DOT_PRODUCT"
    dot.inputs[1].default_value = SUN_DIR_WORLD
    nt.links.new(geo.outputs["Normal"], dot.inputs[0])
    steps = []
    for edge in (-1 / 3, 1 / 3):
        gt = nt.nodes.new("ShaderNodeMath")
        gt.operation = "GREATER_THAN"
        gt.inputs[1].default_value = edge
        nt.links.new(dot.outputs["Value"], gt.inputs[0])
        steps.append(gt)

    def tones_node(tones):
        m1 = nt.nodes.new("ShaderNodeMix")
        m1.data_type = "RGBA"
        ins = sockets(m1.inputs)
        ins["A_Color"].default_value = (*tones[0], 1)
        ins["B_Color"].default_value = (*tones[1], 1)
        nt.links.new(steps[0].outputs[0], ins["Factor_Float"])
        m2 = nt.nodes.new("ShaderNodeMix")
        m2.data_type = "RGBA"
        ins2 = sockets(m2.inputs)
        nt.links.new(sockets(m1.outputs)["Result_Color"], ins2["A_Color"])
        ins2["B_Color"].default_value = (*tones[2], 1)
        nt.links.new(steps[1].outputs[0], ins2["Factor_Float"])
        return sockets(m2.outputs)["Result_Color"]

    col = tones_node(toon_tones(albedo))
    if emissive is not None:
        hot = tones_node(toon_tones(albedo, emissive))
        glow = nt.nodes.new("ShaderNodeMix")
        glow.name = "glow"
        glow.data_type = "RGBA"
        gi = sockets(glow.inputs)
        gi["Factor_Float"].default_value = 0.0
        nt.links.new(col, gi["A_Color"])
        nt.links.new(hot, gi["B_Color"])
        col = sockets(glow.outputs)["Result_Color"]
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Strength"].default_value = 1.0
    nt.links.new(col, em.inputs["Color"])
    nt.links.new(em.outputs[0], out.inputs["Surface"])
    return m


def link(ob, parent):
    """Link an object into the scene under a parent.

    Args:
        ob: The object.
        parent: Its parent.

    Returns:
        The object.
    """
    bpy.context.scene.collection.objects.link(ob)
    ob.parent = parent
    return ob


_n = [0]


def nm(base):
    """Give a numbered name.

    Args:
        base: The name's stem.

    Returns:
        The stem with a running number.
    """
    _n[0] += 1
    return f"{base}{_n[0]:03d}"


def smooth_shading(bm, flat_axis_faces):
    """Shade faces smooth, edges over 30 degrees sharp.

    Args:
        bm: The bmesh.
        flat_axis_faces: Keep faces whose normal lies on a local axis
            flat (the sides of a rounded box, as RoundedBoxGeometry
            shades them).
    """
    for f in bm.faces:
        n = f.normal
        on_axis = max(abs(n.x), abs(n.y), abs(n.z)) > 0.9995
        f.smooth = not (flat_axis_faces and on_axis)
    for e in bm.edges:
        if len(e.link_faces) == 2:
            a = e.link_faces[0].normal.angle(e.link_faces[1].normal, 0)
            e.smooth = a < math.radians(30)


def finish(bm, name, mat, parent, loc=(0, 0, 0), outline=True,
           shadow=True):
    """Turn a bmesh into an object, with its outline hull as a child.

    Args:
        bm: The bmesh, in the object's local coordinates; it is freed.
        name: The object's name.
        mat: Its material.
        parent: Its parent.
        loc: Its location in the parent's frame.
        outline: Add the inverted hull.
        shadow: The object casts a shadow.

    Returns:
        The object.
    """
    hull_bm = bm.copy() if outline else None
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(mat)
    ob = link(bpy.data.objects.new(name, me), parent)
    ob.location = loc
    ob.visible_shadow = shadow
    if outline:
        hull_bm.normal_update()
        for v in hull_bm.verts:
            v.co = v.co + v.normal * EDGE
        bmesh.ops.reverse_faces(hull_bm, faces=hull_bm.faces)
        for f in hull_bm.faces:
            f.smooth = True
        hm = bpy.data.meshes.new(name + "_ink")
        hull_bm.to_mesh(hm)
        hull_bm.free()
        hm.materials.append(MAT_INK)
        h = link(bpy.data.objects.new(name + "_ink", hm), ob)
        h.visible_shadow = False
    return ob


def bm_from_solid(pos, tri):
    """Make a bmesh of one STEP solid, its vertices welded.

    Vertices are welded within the solid only, never across solids.

    Args:
        pos: Positions, mm, STEP frame.
        tri: Triangles.

    Returns:
        The bmesh, three.js frame.
    """
    bm = bmesh.new()
    vs = [bm.verts.new(p) for p in step_to_three(pos)]
    for a, b, c in tri:
        try:
            bm.faces.new((vs[a], vs[b], vs[c]))
        except ValueError:
            pass
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-3)
    bm.normal_update()
    return bm


def bbox(pos):
    """Give a solid's bounding box in the three.js frame.

    Args:
        pos: Positions, STEP frame.

    Returns:
        The lower and upper corners.
    """
    p = step_to_three(pos)
    return p.min(0), p.max(0)


def rounded_box(pos, tri, r):
    """Round a STEP box's edges, as RoundedBoxGeometry draws them.

    Args:
        pos: Positions, STEP frame.
        tri: Triangles.
        r: The edge radius, mm.

    Returns:
        The bmesh about its centre, and the centre.
    """
    bm = bm_from_solid(pos, tri)
    lo, hi = bbox(pos)
    c = (lo + hi) / 2
    bmesh.ops.translate(bm, verts=bm.verts, vec=Vector(-c))
    sharp = [
        e for e in bm.edges
        if len(e.link_faces) == 2
        and e.link_faces[0].normal.angle(e.link_faces[1].normal, 0)
        > math.radians(45)
    ]
    r = min(r, *(hi - lo) / 2 - 1e-3)
    corners = list({v for e in sharp for v in e.verts})
    bmesh.ops.bevel(
        bm, geom=sharp + corners, offset=r, segments=4, profile=0.5,
        affect="EDGES", clamp_overlap=True,
    )
    bm.normal_update()
    smooth_shading(bm, True)
    return bm, c


def solid_mesh(pos, tri):
    """Keep a STEP solid as it is: smooth sides, sharp rims.

    Args:
        pos: Positions, STEP frame.
        tri: Triangles.

    Returns:
        The bmesh about its centre, and the centre.
    """
    bm = bm_from_solid(pos, tri)
    lo, hi = bbox(pos)
    c = (lo + hi) / 2
    bmesh.ops.translate(bm, verts=bm.verts, vec=Vector(-c))
    smooth_shading(bm, False)
    return bm, c


def axis_matrix(axis):
    """Give the rotation that takes local +y to a three.js axis.

    Args:
        axis: 0, 1 or 2 for x, y or z.

    Returns:
        The 4x4 rotation.
    """
    return {
        0: Matrix.Rotation(-math.pi / 2, 4, "Z"),
        1: Matrix.Identity(4),
        2: Matrix.Rotation(math.pi / 2, 4, "X"),
    }[axis]


def capsule(radius, length, axis=1, segs=32, rings=16):
    """Make a capsule: a cylinder with half balls (CapsuleGeometry).

    Args:
        radius: The radius, mm.
        length: The length overall, mm.
        axis: The three.js axis it lies along.
        segs: Segments around.
        rings: Rings from end to end.

    Returns:
        The bmesh about its centre.
    """
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(
        bm, u_segments=segs, v_segments=rings, radius=radius
    )
    half = length / 2 - radius
    for v in bm.verts:
        z = v.co.z
        v.co.z = z + (half if z > 1e-6 else -half if z < -1e-6 else 0)
    # create_uvsphere runs along z: turn it to y, then to the axis.
    turn = axis_matrix(axis) @ Matrix.Rotation(-math.pi / 2, 4, "X")
    bmesh.ops.transform(bm, verts=bm.verts, matrix=turn)
    bm.normal_update()
    for f in bm.faces:
        f.smooth = True
    for e in bm.edges:
        e.smooth = True
    return bm


def ball(radius, segs=24, rings=16):
    """Make a smooth ball.

    Args:
        radius: The radius, mm.
        segs: Segments around.
        rings: Rings from pole to pole.

    Returns:
        The bmesh about its centre.
    """
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(
        bm, u_segments=segs, v_segments=rings, radius=radius
    )
    bm.normal_update()
    for f in bm.faces:
        f.smooth = True
    return bm


def torus(major, minor, axis, segs=40, tube=12):
    """Make a wire ring.

    Args:
        major: The ring's radius, mm.
        minor: The wire's radius, mm.
        axis: The three.js axis it turns about.
        segs: Segments around the ring.
        tube: Segments around the wire.

    Returns:
        The bmesh about its centre.
    """
    bm = bmesh.new()
    grid = []
    for i in range(segs):
        a = 2 * math.pi * i / segs
        row = []
        for j in range(tube):
            b = 2 * math.pi * j / tube
            rr = major + minor * math.cos(b)
            row.append(bm.verts.new(
                (rr * math.cos(a), minor * math.sin(b), rr * math.sin(a))
            ))
        grid.append(row)
    for i in range(segs):
        i2 = (i + 1) % segs
        for j in range(tube):
            j2 = (j + 1) % tube
            bm.faces.new(
                (grid[i][j], grid[i][j2], grid[i2][j2], grid[i2][j])
            )
    bmesh.ops.transform(bm, verts=bm.verts, matrix=axis_matrix(axis))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.normal_update()
    for f in bm.faces:
        f.smooth = True
    return bm


def plain_box(w, h, d, r):
    """Make a box with rounded edges.

    Args:
        w: Width (x), mm.
        h: Height (y), mm.
        d: Depth (z), mm.
        r: The edge radius, mm; 0 for sharp edges.

    Returns:
        The bmesh about its centre.
    """
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((v.co.x * w, v.co.y * h, v.co.z * d))
    if r:
        r = min(r, w / 2 - 1e-3, h / 2 - 1e-3, d / 2 - 1e-3)
        bmesh.ops.bevel(
            bm, geom=list(bm.verts) + list(bm.edges), offset=r, segments=3,
            profile=0.5, affect="EDGES", clamp_overlap=True,
        )
    bm.normal_update()
    smooth_shading(bm, True)
    return bm


def cylinder(radius, height, segs=32):
    """Make an upright cylinder (CylinderGeometry): smooth side, flat caps.

    Args:
        radius: The radius, mm.
        height: The height (y), mm.
        segs: Segments around.

    Returns:
        The bmesh about its centre.
    """
    bm = bmesh.new()
    bmesh.ops.create_cone(
        bm, cap_ends=True, segments=segs, radius1=radius, radius2=radius,
        depth=height,
    )
    bmesh.ops.rotate(
        bm, verts=bm.verts, cent=(0, 0, 0),
        matrix=Matrix.Rotation(math.radians(-90), 3, "X"),
    )
    bm.normal_update()
    smooth_shading(bm, False)
    return bm


def disc_bm(radius, segs=96):
    """Make a flat disc in the local xy plane.

    Args:
        radius: The radius, mm.
        segs: Segments around.

    Returns:
        The bmesh.
    """
    bm = bmesh.new()
    vs = [
        bm.verts.new((radius * math.cos(2 * math.pi * k / segs),
                      radius * math.sin(2 * math.pi * k / segs), 0))
        for k in range(segs)
    ]
    bm.faces.new(vs)
    bm.normal_update()
    return bm


# -------------------------------------------------------------------- scene
bpy.context.preferences.filepaths.save_version = 0
clear_scene()
sc = bpy.context.scene
sc.unit_settings.system = "METRIC"
sc.unit_settings.scale_length = 0.001
sc.unit_settings.length_unit = "MILLIMETERS"
result("engine", set_enum(
    sc.render, "engine", ("BLENDER_EEVEE", "BLENDER_EEVEE_NEXT")
))
sc.render.resolution_x, sc.render.resolution_y = 1920, 1080
sc.render.resolution_percentage = 100
sc.render.fps = 30
sc.frame_start, sc.frame_end = 1, N3D
sc.render.film_transparent = True
sc.render.image_settings.file_format = "PNG"
sc.render.image_settings.color_mode = "RGBA"
sc.render.image_settings.color_depth = "8"
result("view_transform", set_enum(
    sc.view_settings, "view_transform", ("Standard",)
))
sc.view_settings.look = "None"
sc.view_settings.exposure = 0.0
sc.view_settings.gamma = 1.0
sc.eevee.taa_render_samples = CFG.get("samples", 32)
sc.eevee.use_shadows = True
sc.eevee.shadow_resolution_scale = 1.0

ROOT_M = Matrix.Rotation(math.radians(90), 4, "X")
R3 = ROOT_M.to_3x3()
root = empty("root")
root.matrix_basis = ROOT_M
sun_three = Vector(SUN).normalized()
SUN_DIR_WORLD = tuple(R3 @ sun_three)

# The sun casts the ground's shadow; the parts' tones come from n.l in
# their materials.
ld = bpy.data.lights.new("sun", "SUN")
ld.energy = SUN_E
ld.use_shadow = True
ld.angle = math.radians(CFG.get("sun_angle", 1.0))
sun = bpy.data.objects.new("sun", ld)
sc.collection.objects.link(sun)
sun.parent = root
sun.matrix_basis = (
    (-sun_three).to_track_quat("-Z", "Y").to_matrix().to_4x4()
)

# A black world: the ground's shader sees the sun alone.
world = bpy.data.worlds.new("black")
sc.world = world
world.use_nodes = True
bgn = next(n for n in world.node_tree.nodes if n.type == "BACKGROUND")
bgn.inputs["Color"].default_value = (0, 0, 0, 1)
bgn.inputs["Strength"].default_value = 0.0

MAT_INK = emission_material("ink", aces(srgb_lin(INK)))
MAT_INK.use_backface_culling = True

# Ground at y = -0.2: ShadowMaterial({opacity: .5}).  The lit fraction is
# the diffuse value against E n.l / pi.  EEVEE leaves some light in the
# shadow (about .46 of lit), so the step runs from .55 to .9 of lit.
# three.js blends the .5 on the sRGB-encoded canvas and Blender in linear
# light: 1 - .5 ** 2.2 = .78 in linear darkens as much (231 to 116 of 255
# over the disc).
gm = bpy.data.materials.new("ground_shadow")
gm.use_nodes = True
gm.surface_render_method = "BLENDED"
nt = gm.node_tree
for n in list(nt.nodes):
    nt.nodes.remove(n)
outn = nt.nodes.new("ShaderNodeOutputMaterial")
dif = nt.nodes.new("ShaderNodeBsdfDiffuse")
dif.inputs["Color"].default_value = (1, 1, 1, 1)
s2r = nt.nodes.new("ShaderNodeShaderToRGB")
bw = nt.nodes.new("ShaderNodeRGBToBW")
mr = nt.nodes.new("ShaderNodeMapRange")
mr.clamp = True
lit = SUN_E * sun_three.y / math.pi
mr.inputs["From Min"].default_value = 0.55 * lit
mr.inputs["From Max"].default_value = 0.9 * lit
mr.inputs["To Min"].default_value = 1 - 0.5**2.2
mr.inputs["To Max"].default_value = 0.0
tr = nt.nodes.new("ShaderNodeBsdfTransparent")
emi = nt.nodes.new("ShaderNodeEmission")
emi.inputs["Color"].default_value = (0, 0, 0, 1)
mixs = nt.nodes.new("ShaderNodeMixShader")
nt.links.new(dif.outputs[0], s2r.inputs[0])
nt.links.new(s2r.outputs["Color"], bw.inputs[0])
nt.links.new(bw.outputs[0], mr.inputs["Value"])
# The player's shadow camera: orthographic, +-300mm, depth 10mm to 900mm,
# at the sun, looking at the origin.  A ground point outside it takes no
# shadow, so an occluder outside it (the sausage far out in flight) casts
# none.
zl = sun_three
xl = Vector((0, 1, 0)).cross(zl).normalized()
yl = zl.cross(xl)
geo_g = nt.nodes.new("ShaderNodeNewGeometry")
rel = nt.nodes.new("ShaderNodeVectorMath")
rel.operation = "SUBTRACT"
rel.inputs[1].default_value = tuple(R3 @ Vector(SUN))
nt.links.new(geo_g.outputs["Position"], rel.inputs[0])
half, near, far = SHADOW_BOX
inside = None
for axis, lo, hi in ((xl, -half, half), (yl, -half, half), (-zl, near, far)):
    d = nt.nodes.new("ShaderNodeVectorMath")
    d.operation = "DOT_PRODUCT"
    d.inputs[1].default_value = tuple(R3 @ axis)
    nt.links.new(rel.outputs[0], d.inputs[0])
    for op, edge in (("GREATER_THAN", lo), ("LESS_THAN", hi)):
        c = nt.nodes.new("ShaderNodeMath")
        c.operation = op
        c.inputs[1].default_value = edge
        nt.links.new(d.outputs["Value"], c.inputs[0])
        if inside is None:
            inside = c
        else:
            m = nt.nodes.new("ShaderNodeMath")
            m.operation = "MULTIPLY"
            nt.links.new(inside.outputs[0], m.inputs[0])
            nt.links.new(c.outputs[0], m.inputs[1])
            inside = m
clip = nt.nodes.new("ShaderNodeMath")
clip.operation = "MULTIPLY"
nt.links.new(mr.outputs["Result"], clip.inputs[0])
nt.links.new(inside.outputs[0], clip.inputs[1])
nt.links.new(clip.outputs[0], mixs.inputs[0])
nt.links.new(tr.outputs[0], mixs.inputs[1])
nt.links.new(emi.outputs[0], mixs.inputs[2])
nt.links.new(mixs.outputs[0], outn.inputs["Surface"])
gbm = bmesh.new()
for x, z in ((-1500, -1500), (1500, -1500), (1500, 1500), (-1500, 1500)):
    gbm.verts.new((x, 0, z))
gbm.faces.new(gbm.verts)
gbm.normal_update()
gbm.faces.ensure_lookup_table()
if gbm.faces[0].normal.y < 0:
    bmesh.ops.reverse_faces(gbm, faces=gbm.faces)
ground = finish(gbm, "ground", gm, root, (0, -0.2, 0), outline=False,
                shadow=False)

# The pale disc behind the model (MeshBasicMaterial, tone mapped).
disc = finish(
    disc_bm(360), "disc",
    emission_material("disc", aces(srgb_lin(0xFFF1C2))), root,
    (90, 150, -140), outline=False, shadow=False,
)

MATS = {}


def toon(albedo_lin, emissive=None, key=None):
    """Give the toon material of a colour, made once.

    Args:
        albedo_lin: The linear base colour.
        emissive: The linear emissive colour, or None.
        key: The cache key; default the colour.

    Returns:
        The material.
    """
    k = key or tuple(np.round(albedo_lin, 4))
    if k not in MATS:
        MATS[k] = toon_material(f"toon{len(MATS):02d}", albedo_lin, emissive)
    return MATS[k]


KNOT = srgb_lin(0x7A3A1E)
AMBER = srgb_lin(0xFFB000)
# Colours the player draws in place of the STEP's: the barrel's rings
# (STEP: DARK).
COLOR = {"ring1": srgb_lin(0x5F6A80), "ring2": srgb_lin(0x5F6A80)}
STAND_ROLES = {
    0: ("stand", "box", 5), 1: ("post", "rod"), 2: ("board", "box", 2),
    3: ("radio", "box", 2), 4: ("chip", "box", 1.5), 5: ("antenna", "rod"),
    6: ("tip", "ball"), 7: ("lamp", "ball"),
}
NODE_ROLES = {
    **STAND_ROLES,
    8: ("servo_post", "rod"), 9: ("servo", "box", 3),
    10: ("flange", "box", 2), 11: ("hub", "rod"), 12: ("arm", "box", 1.5),
    13: ("sausage", "capsule"), 14: ("clip1", "torus"),
    15: ("clip2", "torus"),
}
KINETIC_ROLES = {
    **STAND_ROLES,
    8: ("tank_post1", "rod"), 9: ("tank_post2", "rod"),
    10: ("tank", "capsule"), 11: ("gauge", "rod"), 12: ("pipe", "rod"),
    13: ("valve", "box", 3), 14: ("barrel", "rod"),
    15: ("sausage", "capsule"), 16: ("ring1", "torus"),
    17: ("ring2", "torus"),
}


def knots(parent, axis, half):
    """Add a ball of 4.5mm at each end of the sausage (wurst3d.js knots).

    Args:
        parent: The sausage.
        axis: The sausage's three.js axis.
        half: Half its length, mm.
    """
    for s in (-1, 1):
        p = [0, 0, 0]
        p[axis] = s * half
        finish(ball(4.5, 16, 12), nm("knot"), toon(KNOT), parent, tuple(p))


def build(model, glb, kinetic, dog=False):
    """Build one model with its moving parts and effects.

    Args:
        model: "wurst", "wurst-kinetic" or "wurst-dog".
        glb: The model's .glb file.
        kinetic: Build the kinetic node.
        dog: Set the node on a mast over a dog (wurst3d.js
            buildWurst(false, true)).

    Returns:
        A dict of the objects and materials the keys move.
    """
    grp = empty(model, root)
    solids = load_glb(glb)
    roles = KINETIC_ROLES if kinetic else NODE_ROLES
    lamp_mat = None

    def put(k, parent, rel=None):
        pos, tri, col = solids[k]
        lo, hi = bbox(pos)
        c = (lo + hi) / 2
        size = hi - lo
        role = roles[k]
        if role[1] == "box":
            bm, c = rounded_box(pos, tri, role[2])
        elif role[1] == "ball":
            r = max(size) / 2 if role[0] == "lamp" else min(size) / 2
            bm = ball(r)
        elif role[1] == "capsule":
            ax = int(np.argmax(size))
            r = min(s for i, s in enumerate(size) if i != ax) / 2
            bm = capsule(r, size[ax], ax)
        elif role[1] == "torus":
            ax = int(np.argmin(size))
            bm = torus(max(size) / 2 - 1.0, size[ax] / 2, ax)
        else:
            bm, c = solid_mesh(pos, tri)
        if role[0] == "lamp":
            mat = toon(np.array(col), AMBER, key="lamp_" + model)
        elif role[0] in COLOR:
            mat = toon(COLOR[role[0]])
        else:
            mat = toon(np.array(col))
        loc = c - (rel if rel is not None else 0)
        return finish(bm, f"{model}_{role[0]}", mat, parent, tuple(loc)), mat

    if not kinetic:
        # The arm turns about the servo's shaft: STEP x = 40, z = 128,
        # axis along y.
        pivot_at = np.array([40.0, 128.0, 16.0])
        pivot = empty(model + "_pivot", grp, tuple(pivot_at))
        for k in range(16):
            if k in (12, 13, 14, 15):
                ob, _ = put(k, pivot, pivot_at)
                if k == 13:
                    knots(ob, 1, 90.0)
            else:
                ob, mat = put(k, grp)
                if k == 7:
                    lamp_mat = mat
        # Three strokes beside the sausage's end while it is out
        # (MeshBasicMaterial, no outline).
        strokes = empty(model + "_strokes", grp)
        mat_s = emission_material("stroke", aces(AMBER))
        for a, ln in ((-0.5, 26), (0, 34), (0.5, 26)):
            at = (40 + 222 + math.cos(a) * 20, 128 + math.sin(a) * 44, 28)
            s = finish(plain_box(ln, 5, 3, 1.2), nm("stroke"), mat_s,
                       strokes, at, outline=False, shadow=False)
            s.rotation_euler = (0, 0, a)
        hound = None
        if dog:
            hound = build_dog(model, grp)
        return {"grp": grp, "pivot": pivot, "strokes": strokes,
                "lamp": lamp_mat, "hound": hound}

    shot_at = np.array([120.0, 134.0, -10.0])
    shot = empty(model + "_shot", grp, tuple(shot_at))
    for k in range(18):
        if k == 15:
            ob, _ = put(k, shot, shot_at)
            knots(ob, 0, 90.0)
        else:
            ob, mat = put(k, grp)
            if k == 7:
                lamp_mat = mat
    # The gauge's needle and the bore's dark mouth (wurst3d.js).
    needle = finish(plain_box(3, 9, 2, 0.8), nm("needle"),
                    toon(srgb_lin(0x232833)), grp, (57, 83, 21))
    needle.rotation_euler = (0, 0, -0.6)
    bore = finish(disc_bm(16.5, 40), "bore",
                  emission_material("bore", aces(srgb_lin(INK))), grp,
                  (223.4, 134, -10), outline=False, shadow=False)
    bore.rotation_euler = (0, math.pi / 2, 0)
    # A traffic light 987mm from the muzzle, before a second pale disc;
    # it turns about its foot when the sausage hits it (wurst3d.js).
    finish(disc_bm(300), "far_disc",
           emission_material("far_disc", aces(srgb_lin(0xFFF1C2))), grp,
           (1150, 150, -160), outline=False, shadow=False)
    light = empty(model + "_light", grp, (1240, 0, -10))
    finish(cylinder(34, 8), "light_foot", toon(srgb_lin(0x232833)), light,
           (0, 4, 0))
    finish(cylinder(6, 64), "light_pole", toon(srgb_lin(0x8A93A6)), light,
           (0, 40, 0))
    finish(plain_box(60, 170, 50, 8), "light_housing",
           toon(srgb_lin(0x2B303B)), light, (0, 150, 0))
    lamps = []
    for y, c in ((205, 0xFF3B4E), (150, 0xFFB000), (95, 0x2FD06B)):
        mat = toon(srgb_lin(c), srgb_lin(c) * 0.9, key=f"light_{c:06x}")
        finish(ball(18), nm("light_lamp"), mat, light, (0, y, 20))
        lamps.append(mat)
    # Four puffs at the muzzle.
    puffs = []
    for x, y, z, r in ((0, 0, 0, 30), (22, 20, 8, 22), (26, -16, -6, 20),
                       (48, 4, 4, 16)):
        puffs.append(finish(ball(r, 20, 14), nm("puff"),
                            toon(srgb_lin(0xFFFFFF)), grp,
                            (250 + x, 134 + y, -10 + z)))
    return {"grp": grp, "shot": shot, "puffs": puffs, "lamp": lamp_mat,
            "light": light, "lamps": lamps}


DEEP = 620.0  # wurst3d.js: the mast's height in the dog scene
FUR, EAR = srgb_lin(0xB9793F), srgb_lin(0x6B3F1E)


def build_dog(model, grp):
    """Add the mast, its foot, the third disc and the dog (wurst3d.js).

    Args:
        model: The model's name.
        grp: The model's group.

    Returns:
        The dog's group; its keys set the jump and the tilt.
    """
    finish(cylinder(12, DEEP), "mast", toon(srgb_lin(0x8A93A6)), grp,
           (0, -DEEP / 2, 0))
    finish(cylinder(60, 10), "mast_foot", toon(srgb_lin(0x3A4A72)), grp,
           (0, 5 - DEEP, 0))
    finish(disc_bm(560), "low_disc",
           emission_material("low_disc", aces(srgb_lin(0xFFF1C2))), grp,
           (120, -330, -150), outline=False, shadow=False)
    hound = empty(model + "_hound", grp, (250, -DEEP, 10))
    fur, ink = toon(FUR), toon(srgb_lin(INK))
    finish(capsule(26, 70 + 2 * 26, 0), "dog_body", fur, hound, (0, 62, 0))
    finish(ball(24), "dog_head", fur, hound, (-62, 94, 0))
    finish(plain_box(32, 16, 18, 5), "dog_snout", fur, hound, (-90, 88, 0))
    finish(ball(5, 16, 12), "dog_nose", ink, hound, (-107, 91, 0))
    finish(ball(3.5, 12, 8), "dog_eye", ink, hound, (-74, 102, 20))
    for z in (-17, 17):
        ear = finish(plain_box(11, 28, 6, 2.5), nm("dog_ear"), toon(EAR),
                     hound, (-50, 104, z))
        ear.rotation_euler = (0, 0, 0.5)
        for x in (-34, 36):
            finish(cylinder(6, 40), nm("dog_leg"), fur, hound,
                   (x, 20, z * 0.8))
    tail = finish(cylinder(4, 38), "dog_tail", fur, hound, (64, 84, 0))
    tail.rotation_euler = (0, 0, -0.8)
    return hound


NODE = build("wurst", CFG["glb_node"], False)
DOG = build("wurst-dog", CFG["glb_node"], False, dog=True)
KIN = build("wurst-kinetic", CFG["glb_kinetic"], True)

# ------------------------------------------------------------------- camera
cam_d = bpy.data.cameras.new("cam")
cam_d.sensor_fit = "VERTICAL"
cam_d.angle_y = math.radians(30)
cam_d.clip_start, cam_d.clip_end = 5, 3000
cam = bpy.data.objects.new("cam", cam_d)
sc.collection.objects.link(cam)
cam.parent = root
sc.camera = cam
look_at = empty("cam_target", root)
tt = cam.constraints.new("TRACK_TO")
tt.target = look_at
tt.track_axis, tt.up_axis = "TRACK_NEGATIVE_Z", "UP_Y"


# --------------------------------------------------------------------- keys
def channelbag(idd):
    """Give the channel bag of a data block's action, made when missing.

    Args:
        idd: The data block.

    Returns:
        The channel bag.
    """
    from bpy_extras import anim_utils

    ad = idd.animation_data or idd.animation_data_create()
    if ad.action is None:
        ad.action = bpy.data.actions.new(f"{idd.name}_act")
    if ad.action_slot is None:
        ad.action_slot = ad.action.slots.new(
            id_type=idd.id_type, name=idd.name
        )
    return anim_utils.action_ensure_channelbag_for_slot(
        ad.action, ad.action_slot
    )


def key(idd, path, index, values, constant=False):
    """Key values[k] at frame 1 + k, only where the value changes.

    Args:
        idd: The data block.
        path: The property's data path.
        index: The property's array index.
        values: One value a frame.
        constant: Hold each key (no interpolation).
    """
    cb = channelbag(idd)
    fc = cb.fcurves.find(path, index=index) or cb.fcurves.new(
        path, index=index
    )
    vals = np.asarray(values, dtype=np.float32)
    keep = np.ones(len(vals), dtype=bool)
    if len(vals) > 2:
        same_prev = np.r_[False, vals[1:] == vals[:-1]]
        same_next = np.r_[vals[:-1] == vals[1:], False]
        keep = ~(same_prev & same_next)
    fr = np.nonzero(keep)[0]
    fc.keyframe_points.add(len(fr))
    co = np.empty(len(fr) * 2, dtype=np.float32)
    co[0::2] = fr + 1
    co[1::2] = vals[fr]
    fc.keyframe_points.foreach_set("co", co)
    fc.keyframe_points.foreach_set(
        "interpolation", np.full(len(fr), 0 if constant else 1, np.int32)
    )
    fc.update()


GLOW = 'nodes["glow"].inputs[0].default_value'


def hide_keys(obs, shown):
    """Key hide_render of objects and their children.

    Args:
        obs: The objects.
        shown: One bool a frame.
    """
    for top in obs:
        for ob in [top, *top.children_recursive]:
            key(ob, "hide_render", 0, [0.0 if s else 1.0 for s in shown],
                constant=True)


# Frames between 3D scenes keep the last 3D state (the assembly does not
# show them).
last = next(f for f in F3 if f["is3d"])
HELD = []
for f in F3:
    if f["is3d"]:
        last = f
    HELD.append(last)
for c in range(3):
    key(cam, "location", c, [h["cam"][c] for h in HELD])
    key(look_at, "location", c, [h["target"][c] for h in HELD])

# Objects with keys of their own (strokes, shot, puffs) carry the scene's
# visibility in them.
OWN = {NODE["strokes"], DOG["strokes"], KIN["shot"], *KIN["puffs"]}
OWN |= {c for o in list(OWN) for c in o.children_recursive}
for model, parts in (("wurst", NODE), ("wurst-dog", DOG),
                     ("wurst-kinetic", KIN)):
    on = [h["model"] == model for h in HELD]
    for ob in [parts["grp"], *parts["grp"].children_recursive]:
        if ob not in OWN:
            key(ob, "hide_render", 0, [0.0 if o else 1.0 for o in on],
                constant=True)

# The ground lies DEEP lower in the dog scene.
key(ground, "location", 1,
    [-0.2 - (DEEP if h["model"] == "wurst-dog" else 0) for h in HELD],
    constant=True)
for model, parts in (("wurst", NODE), ("wurst-dog", DOG)):
    sw = [h.get("swing", 0.0) if h["model"] == model else 0.0
          for h in HELD]
    key(parts["pivot"], "rotation_euler", 2, [-math.pi / 2 * k for k in sw])
    key(parts["lamp"].node_tree, GLOW, 0, sw)
    hide_keys([parts["strokes"]],
              [h["model"] == model and k > 0.85 for h, k in zip(
                  HELD, sw, strict=True)])
    if parts["hound"] is not None:
        # wurst3d.js setSwing: the dog jumps 260mm and tilts with k.
        key(parts["hound"], "location", 1, [260 * k - DEEP for k in sw])
        key(parts["hound"], "rotation_euler", 2,
            [-0.35 - 0.45 * k for k in sw])

kin_on = [h["model"] == "wurst-kinetic" for h in HELD]
fk = [h.get("fire", 0.0) if on else 0.0 for h, on in zip(HELD, kin_on,
                                                          strict=True)]
# wurst3d.js setFire: to HIT the sausage flies (u), from HIT on it bounces
# back from the traffic light, turns once and falls to the ground (v).
HIT = 0.6
uu = [min(k / HIT, 1.0) for k in fk]
vv = [max((k - HIT) / (1 - HIT), 0.0) for k in fk]
shot = KIN["shot"]
key(shot, "location", 0,
    [120 + 1000 * u - 260 * v for u, v in zip(uu, vv, strict=True)])
key(shot, "location", 1,
    [134 - 14 * u * u + 160 * v - 266 * v * v
     for u, v in zip(uu, vv, strict=True)])
key(shot, "rotation_euler", 2, [2 * math.pi * v for v in vv])
key(KIN["light"], "rotation_euler", 2,
    [-0.12 * math.exp(-3 * v) * math.sin(14 * v) for v in vv])
for i, mat in enumerate(KIN["lamps"]):
    # Red until the hit, amber from it; green stays dark.
    key(mat.node_tree, GLOW, 0,
        [1.0 if ((i == 1) if v > 0 else (i == 0)) else 0.0 for v in vv],
        constant=True)
hide_keys([shot], [on and k > 0 for on, k in zip(kin_on, fk, strict=True)])
key(KIN["lamp"].node_tree, GLOW, 0, [1.0 if k > 0 else 0.0 for k in fk],
    constant=True)
puff = [math.sin(math.pi * k / 0.2) if 0 < k < 0.2 else 0.0 for k in fk]
for p in KIN["puffs"]:
    for c in range(3):
        key(p, "scale", c, [max(v, 0.25) for v in puff])
hide_keys(KIN["puffs"],
          [on and v > 0.25 for on, v in zip(kin_on, puff, strict=True)])

bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=CFG["out"], compress=False)
segs, a = [], None
for f in FR:
    if f["is3d"] and a is None:
        a = f["i"] + 1
    if not f["is3d"] and a is not None:
        segs.append(f"{a}-{f['i']}")
        a = None
if a is not None:
    segs.append(f"{a}-{FR[-1]['i'] + 1}")
tris = sum(
    sum(len(p.vertices) - 2 for p in o.data.polygons)
    for o in bpy.data.objects if o.type == "MESH"
)
result("scene", CFG["out"], "frames", N3D, "3d_frames",
       sum(f["is3d"] for f in FR), "segments", ",".join(segs), "objects",
       len(bpy.data.objects), "materials", len(bpy.data.materials),
       "triangles", tris)
