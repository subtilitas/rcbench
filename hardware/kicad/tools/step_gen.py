"""Write STEP 3D models of the modules from the ``MODEL`` of a spec.

The output is ISO 10303-21 text, schema AP214 (automotive design), one
product with one solid per part and a colour on each solid. Every solid is
a prism: a closed polygon, optionally with holes, extruded along z. Faces
are planar; circles are polygons of 16 to 32 sides. Units are millimetres.

Model frame: the footprint origin (pad 1) at x = y = 0, x right, y up
(the footprint's y down, mirrored), z up from the carrier PCB's top side.
KiCad places a model with this frame on the footprint with no offset.

``MODEL`` keys, coordinates in the spec's board frame (y down), z from the
module board's top side::

    "mount": "header"     the module stands on 2.54mm male headers; their
                          2.5mm plastic sits on the carrier, the board's
                          underside 2.5mm above it
             "angled"     right-angle 2.54mm headers under the board: the
                          2.54mm plastic sits on the carrier and each pin
                          leaves horizontally, out over the nearest board
                          edge
             "flat"       the board's underside rests on the carrier
    "thickness": 1.6      board thickness
    "color": "green"      board colour, a key of COLORS (default green)
    "pins": ["1", ...]    pad numbers that get a header pin ("header" and
                          "angled" mounts): each drilled pad with a listed
                          number and a drill of 1.1mm or less
    "holes": [(x, y, d)]  holes besides the drilled pads
    "pad_holes": False    (optional) the drilled pads are carrier pads
                          off the module board, not holes in it
    "outline": [(x, y)]   (optional) the board outline, when it differs
                          from the footprint's
    "bodies": [...]       parts on the board:
        ("box", x0, y0, x1, y1, h, color)
        ("cyl", cx, cy, r, h, color)
        ("poly", [(x, y), ...], h, color)
      each with an optional last value z0, the body's underside (default
      0, the board's top side; negative values go below the board's
      underside at -thickness).

Unless ``pad_holes`` is False, every drilled pad of the footprint is a hole
through the board. A hole must lie inside the outline and clear of every
other hole.

The output is the same on every run: entity numbers follow the build
order and the file's time stamp is fixed.
"""

import math

# Pin header, 2.54mm pitch: plastic height, pin length below the plastic
# (into the carrier) and above it, square pin side.
HDR_PLASTIC = 2.5
HDR_BELOW = 6.0
HDR_ABOVE = 3.0
HDR_PIN = 0.64
# Right-angle header under the board: plastic height, the plastic's span
# along the pin from the hole, the pin's length past the plastic, the pin
# tip above the board's top side.
RA_PLASTIC = 2.54
RA_BODY = (1.0, 3.5)
RA_OUT = 6.0
RA_ABOVE = 1.4

COLORS = {
    "green": (0.12, 0.52, 0.24),
    "blue": (0.05, 0.22, 0.55),
    "black": (0.06, 0.06, 0.07),
    "white": (0.92, 0.92, 0.90),
    "red": (0.62, 0.08, 0.06),
    "chip": (0.12, 0.12, 0.13),
    "metal": (0.78, 0.78, 0.80),
    "gold": (0.83, 0.68, 0.22),
    "plastic": (0.10, 0.10, 0.10),
    "beige": (0.90, 0.86, 0.74),
    "yellow": (0.95, 0.75, 0.10),
    "termblue": (0.15, 0.40, 0.75),
    "termgreen": (0.15, 0.55, 0.25),
    "ceramic": (0.66, 0.55, 0.40),
    "glass": (0.10, 0.12, 0.18),
}


def _r(v):
    """A real as STEP writes it: always a decimal point, no -0."""
    s = f"{round(v, 6):.6f}".rstrip("0")
    return "0." if s in ("-0.", "0.") else s


def _q(s):
    return "'" + s.replace("'", "''") + "'"


class _Step:
    """Entity list; ``add`` returns the new entity's reference."""

    def __init__(self):
        self.lines = []
        self._pts = {}
        self._dirs = {}

    def add(self, text):
        self.lines.append(text)
        return f"#{len(self.lines)}"

    def point(self, p):
        key = tuple(round(c, 6) for c in p)
        if key not in self._pts:
            self._pts[key] = self.add(
                "CARTESIAN_POINT('',(" + ",".join(_r(c) for c in key) + "))")
        return self._pts[key]

    def direction(self, d):
        key = tuple(round(c, 9) for c in d)
        if key not in self._dirs:
            self._dirs[key] = self.add(
                "DIRECTION('',(" + ",".join(_r(c) for c in key) + "))")
        return self._dirs[key]

    def placement(self, p, axis, ref):
        return self.add(f"AXIS2_PLACEMENT_3D('',{self.point(p)},"
                        f"{self.direction(axis)},{self.direction(ref)})")


def _area(pts):
    return sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1)
               in zip(pts, pts[1:] + pts[:1], strict=True)) / 2


def _dedupe(pts):
    """Drop repeated neighbours, the closing point among them."""
    out = []
    for p in pts:
        p = (round(p[0], 6), round(p[1], 6))
        if not out or p != out[-1]:
            out.append(p)
    if len(out) > 1 and out[0] == out[-1]:
        out.pop()
    return out


def circle(cx, cy, r):
    """Closed polygon of a circle, 16 to 32 sides by radius.

    Args:
        cx: Centre x.
        cy: Centre y.
        r: Radius.

    Returns:
        The corner points.
    """
    n = 16 if r < 1.0 else 24 if r < 3.0 else 32
    return [(cx + r * math.cos(2 * math.pi * i / n),
             cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def _inside(pts, x, y):
    """Whether (x, y) lies inside the closed polygon ``pts``."""
    hit = False
    for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1], strict=True):
        if (y0 > y) != (y1 > y) and x < x0 + (y - y0) * (x1 - x0) / (y1 - y0):
            hit = not hit
    return hit


def _check_holes(name, outline, drills):
    """Raise ValueError for a hole off the board or touching another."""
    for i, (x, y, d) in enumerate(drills):
        if not all(_inside(outline, px, py) for px, py in circle(x, y, d / 2)):
            raise ValueError(f"{name}: hole at ({x}, {y}) leaves the board")
        for x2, y2, d2 in drills[i + 1:]:
            if math.hypot(x2 - x, y2 - y) <= (d + d2) / 2:
                raise ValueError(f"{name}: holes at ({x}, {y}) and "
                                 f"({x2}, {y2}) touch")


def _prism(st, outer, holes, z0, z1):
    """A closed shell of planar faces; returns the MANIFOLD_SOLID_BREP."""
    rings = [_dedupe(outer)] + [_dedupe(h) for h in holes]
    # Outer ring counter-clockwise seen from +z, holes clockwise: the top
    # face then runs every ring forwards and each side face faces out.
    rings = [r if (_area(r) > 0) == (i == 0) else r[::-1]
             for i, r in enumerate(rings)]
    faces = []
    top_loops, bot_loops = [], []
    for ring in rings:
        n = len(ring)
        vb = [st.add(f"VERTEX_POINT('',{st.point((x, y, z0))})")
              for x, y in ring]
        vt = [st.add(f"VERTEX_POINT('',{st.point((x, y, z1))})")
              for x, y in ring]

        def edge(a, b, pa, pb):
            d = [pb[k] - pa[k] for k in range(3)]
            m = math.sqrt(sum(c * c for c in d))
            vec = st.add(f"VECTOR('',{st.direction([c / m for c in d])},"
                         f"{_r(m)})")
            line = st.add(f"LINE('',{st.point(pa)},{vec})")
            return st.add(f"EDGE_CURVE('',{a},{b},{line},.T.)")

        eb = [edge(vb[i], vb[(i + 1) % n], (*ring[i], z0),
                   (*ring[(i + 1) % n], z0)) for i in range(n)]
        et = [edge(vt[i], vt[(i + 1) % n], (*ring[i], z1),
                   (*ring[(i + 1) % n], z1)) for i in range(n)]
        ev = [edge(vb[i], vt[i], (*ring[i], z0), (*ring[i], z1))
              for i in range(n)]

        def oe(e, fwd):
            sense = ".T." if fwd else ".F."
            return st.add(f"ORIENTED_EDGE('',*,*,{e},{sense})")

        top_loops.append([oe(e, True) for e in et])
        bot_loops.append([oe(e, False) for e in reversed(eb)])
        for i in range(n):
            j = (i + 1) % n
            (x0, y0), (x1, y1) = ring[i], ring[j]
            dx, dy = x1 - x0, y1 - y0
            m = math.hypot(dx, dy)
            loop = st.add("EDGE_LOOP('',(" + ",".join(
                (oe(eb[i], True), oe(ev[j], True), oe(et[i], False),
                 oe(ev[i], False))) + "))")
            bound = st.add(f"FACE_OUTER_BOUND('',{loop},.T.)")
            plane = st.add("PLANE('',{})".format(st.placement(
                (x0, y0, z0), (dy / m, -dx / m, 0.0), (dx / m, dy / m, 0.0))))
            faces.append(st.add(f"ADVANCED_FACE('',({bound}),{plane},.T.)"))
    for loops, z, axis in ((top_loops, z1, (0.0, 0.0, 1.0)),
                           (bot_loops, z0, (0.0, 0.0, -1.0))):
        bounds = []
        for k, oes in enumerate(loops):
            loop = st.add("EDGE_LOOP('',(" + ",".join(oes) + "))")
            kind = "FACE_OUTER_BOUND" if k == 0 else "FACE_BOUND"
            bounds.append(st.add(f"{kind}('',{loop},.T.)"))
        plane = st.add("PLANE('',{})".format(st.placement(
            (*rings[0][0], z), axis, (1.0, 0.0, 0.0))))
        faces.insert(0, st.add("ADVANCED_FACE('',(" + ",".join(bounds)
                               + f"),{plane},.T.)"))
    shell = st.add("CLOSED_SHELL('',(" + ",".join(faces) + "))")
    return st.add(f"MANIFOLD_SOLID_BREP('',{shell})")


def _style(st, solid, rgb, ctx_items):
    col = st.add("COLOUR_RGB(''," + ",".join(_r(c) for c in rgb) + ")")
    fsc = st.add(f"FILL_AREA_STYLE_COLOUR('',{col})")
    fas = st.add(f"FILL_AREA_STYLE('',({fsc}))")
    ssfa = st.add(f"SURFACE_STYLE_FILL_AREA({fas})")
    sss = st.add(f"SURFACE_SIDE_STYLE('',({ssfa}))")
    ssu = st.add(f"SURFACE_STYLE_USAGE(.BOTH.,{sss})")
    psa = st.add(f"PRESENTATION_STYLE_ASSIGNMENT(({ssu}))")
    ctx_items.append(st.add(f"STYLED_ITEM('color',({psa}),{solid})"))


def solids(fp, model):
    """The prisms of a module in the model frame.

    Args:
        fp: The spec's FOOTPRINT.
        model: The spec's MODEL.

    Returns:
        A list of (outer, holes, z0, z1, rgb), points in the model frame.

    Raises:
        ValueError: For an unknown mount, body or colour.
    """
    ox, oy = fp["origin"]
    t = model.get("thickness", 1.6)
    mount = model["mount"]
    if mount not in ("header", "angled", "flat"):
        raise ValueError(f"unknown mount {mount}")
    base = {"header": HDR_PLASTIC, "angled": RA_PLASTIC, "flat": 0.0}[mount]
    top = base + t

    def tr(pts):
        return [(x - ox, oy - y) for x, y in pts]

    bw, bh = fp["board"]
    outline = (model.get("outline") or fp.get("outline")
               or [(0, 0), (bw, 0), (bw, bh), (0, bh)])
    drills = [(pd["x"], pd["y"], pd["drill"]) for pd in fp["pads"]
              if pd.get("drill") and model.get("pad_holes", True)]
    drills += list(model.get("holes", []))
    _check_holes(fp["name"], _dedupe(outline), drills)
    holes = [circle(x, y, d / 2) for x, y, d in drills]
    out = [(tr(outline), [tr(h) for h in holes], base, top,
            COLORS[model.get("color", "green")])]
    for body in model.get("bodies", []):
        kind = body[0]
        if kind == "box":
            x0, y0, x1, y1, h, col = body[1:7]
            pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
            rest = body[7:]
        elif kind == "cyl":
            cx, cy, r, h, col = body[1:6]
            pts = circle(cx, cy, r)
            rest = body[6:]
        elif kind == "poly":
            pts, h, col = body[1:4]
            rest = body[4:]
        else:
            raise ValueError(f"unknown body {kind}")
        z0 = top + (rest[0] if rest else 0.0)
        out.append((tr(pts), [], z0, z0 + h, COLORS[col]))
    if mount == "flat":
        return out
    s, p = 2.54 / 2, HDR_PIN / 2
    xs, ys = [x for x, _ in outline], [y for _, y in outline]
    for pd in fp["pads"]:
        if (pd["num"] not in model["pins"] or not pd.get("drill")
                or pd["drill"] > 1.1):
            continue
        x, y = pd["x"], pd["y"]
        if mount == "angled":
            out += _angled_pin(tr, x, y, (min(xs), min(ys), max(xs),
                                          max(ys)), top)
        else:
            out.append((tr([(x - s, y - s), (x + s, y - s), (x + s, y + s),
                            (x - s, y + s)]), [], 0.0, HDR_PLASTIC,
                        COLORS["plastic"]))
            out.append((tr([(x - p, y - p), (x + p, y - p), (x + p, y + p),
                            (x - p, y + p)]), [], -HDR_BELOW,
                        HDR_PLASTIC + HDR_ABOVE, COLORS["gold"]))
    return out


def _angled_pin(tr, x, y, box, top):
    """Plastic, vertical leg and horizontal leg of one right-angle pin.

    The pin leaves towards the edge of ``box`` nearest to (x, y).
    """
    x0, y0, x1, y1 = box
    dist = {(-1, 0): x - x0, (1, 0): x1 - x, (0, -1): y - y0, (0, 1): y1 - y}
    dx, dy = min(dist, key=dist.get)
    s, p = 2.54 / 2, HDR_PIN / 2
    zc = RA_PLASTIC / 2

    def strip(a, b, half):
        """Rectangle from a to b along the pin, 2 * half across it."""
        if dx:
            return [(x + dx * a, y - half), (x + dx * b, y - half),
                    (x + dx * b, y + half), (x + dx * a, y + half)]
        return [(x - half, y + dy * a), (x + half, y + dy * a),
                (x + half, y + dy * b), (x - half, y + dy * b)]

    return [
        (tr(strip(*RA_BODY, s)), [], 0.0, RA_PLASTIC, COLORS["plastic"]),
        (tr([(x - p, y - p), (x + p, y - p), (x + p, y + p),
             (x - p, y + p)]), [], zc + p, top + RA_ABOVE,
         COLORS["gold"]),
        (tr(strip(-p, RA_BODY[1] + RA_OUT, p)), [], zc - p,
         zc + p, COLORS["gold"]),
    ]


def step(fp, model):
    """The STEP text of a module.

    Args:
        fp: The spec's FOOTPRINT.
        model: The spec's MODEL.

    Returns:
        The file's text.
    """
    name = fp["name"]
    st = _Step()
    app = st.add("APPLICATION_CONTEXT('automotive design')")
    st.add("APPLICATION_PROTOCOL_DEFINITION('international standard',"
           f"'automotive_design',2000,{app})")
    pctx = st.add(f"PRODUCT_CONTEXT('',{app},'mechanical')")
    prod = st.add(f"PRODUCT({_q(name)},{_q(name)},'',({pctx}))")
    st.add(f"PRODUCT_RELATED_PRODUCT_CATEGORY('part',$,({prod}))")
    pdf = st.add(f"PRODUCT_DEFINITION_FORMATION('','',{prod})")
    pdc = st.add(f"PRODUCT_DEFINITION_CONTEXT('part definition',{app},"
                 "'design')")
    pd = st.add(f"PRODUCT_DEFINITION('design','',{pdf},{pdc})")
    pds = st.add(f"PRODUCT_DEFINITION_SHAPE('','',{pd})")
    mm = st.add("( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) )")
    rad = st.add("( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) )")
    sr = st.add("( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() )")
    unc = st.add(f"UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-07),{mm},"
                 "'distance_accuracy_value','confusion accuracy')")
    ctx = st.add("( GEOMETRIC_REPRESENTATION_CONTEXT(3) "
                 f"GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT(({unc})) "
                 f"GLOBAL_UNIT_ASSIGNED_CONTEXT(({mm},{rad},{sr})) "
                 "REPRESENTATION_CONTEXT('Context #1','3D Context with "
                 "UNIT and UNCERTAINTY') )")
    items = [st.placement((0.0, 0.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0))]
    styled = []
    for outer, holes, z0, z1, rgb in solids(fp, model):
        brep = _prism(st, outer, holes, z0, z1)
        items.append(brep)
        _style(st, brep, rgb, styled)
    rep = st.add(f"ADVANCED_BREP_SHAPE_REPRESENTATION({_q(name)},("
                 + ",".join(items) + f"),{ctx})")
    st.add(f"SHAPE_DEFINITION_REPRESENTATION({pds},{rep})")
    st.add("MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('',("
           + ",".join(styled) + f"),{ctx})")
    head = ("ISO-10303-21;\nHEADER;\n"
            f"FILE_DESCRIPTION(({_q(name + ' module model')}),'2;1');\n"
            f"FILE_NAME({_q(name + '.step')},'2026-01-01T00:00:00',(''),"
            "(''),'rcbench step_gen.py','rcbench step_gen.py','');\n"
            "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));\n"
            "ENDSEC;\nDATA;\n")
    body = "".join(f"#{i + 1}={line};\n" for i, line in enumerate(st.lines))
    return head + body + "ENDSEC;\nEND-ISO-10303-21;\n"
