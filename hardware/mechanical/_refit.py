"""Refit a closed triangle mesh with the surfaces it was cut from.

The mesh is a tessellated CAD model: its vertices lie on the model's
surfaces within a small noise, its triangles are chords. ``solid`` finds
the surfaces again and returns the part as faces on planes, cylinders,
cones, tori and a thread's helical surfaces, as
hardware/kicad/tools/step_gen.py's ``("brep", ...)`` solid takes it.

Surfaces. A triangle proposes surfaces: its own plane, and about every
axis it may belong to a cone or cylinder (a straight profile) and a
torus (a circular profile). An axis is the z axis of the frame, or the
centre line of circular arcs on the mesh's edges that lie in planes at
right angles to x, y or z; about x and y only cylinders are looked for.
Where the mesh's edges run along helices of one pitch about z, the
triangle also proposes a thread's flank (a line turned and lifted along
the helix) and a thread's rounded corner (a circle turned and lifted).

Faces. A proposed surface grows from its triangle over the neighbours
whose vertices lie on it within ``EPS`` and that are chords of it, is
fitted again to all it holds, and grows again. A curved surface needs
evidence that it is curved: a cone, cylinder or flank must hold
triangles turned against each other, a torus or rounded corner 4
separate places along its profile. A first pass over the bare mesh
finds the area each triangle's best surface would take; the faces are
then made in the order of that area, the largest first, each over the
triangles still free. Triangles left alone join a face beside them
whose surface holds them. What no surface takes with a neighbour stays
a flat face of one triangle. A face that winds about its axis more than
once, as a thread's do, is cut by planes through the axis into sectors.

Edges. The faces keep the mesh's topology: a face's bounds are the mesh
edges between it and its neighbours, joined into edges that run from
one vertex where 3 faces meet to the next. A vertex is moved to where
its faces' surfaces meet. An edge is a line, a circle about an axis of
one of its faces or fitted to its points, a helix, or a polyline
through the mesh's vertices.

No dependency outside the standard library; the result is the same on
every run.
"""

import math

from _segment import _Refit
from _surface import (
    _angles, _chains, CHORD_DEG, _circle2, _cross, _dot, EPS, HELIX_DEG,
    HELIX_NEAR, _least_normal, _meet, MOVE_MAX, _normal, _Part, _res, SAG,
    _solve, _sub, UNIFORM, _unit, _unwind,
)


def _circle_about(frame, pts, surfaces):
    """The circle about the frame's axis through all pts, or None.

    Where the two surfaces meet in a circle about that axis, the circle
    is their intersection; else it is the mean of the points.
    """
    polar = [frame.polar(p) for p in pts]
    rho = sum(q[0] for q in polar) / len(polar)
    z = sum(q[1] for q in polar) / len(polar)
    if rho < 1e-3 or any(abs(q[0] - rho) > EPS or abs(q[1] - z) > EPS
                         for q in polar):
        return None
    rad = polar[0][2]
    side = _cross(frame.d, rad)

    def at(x, out):
        return tuple(frame.o[k] + x[0] * out[k] + x[1] * frame.d[k]
                     for k in range(3))

    x = [rho, z]
    for _it in range(12):
        r = [_res(s, at(x, rad)) for s in surfaces]
        jac = []
        for k in range(2):
            y = list(x)
            y[k] += 1e-6
            jac.append([(_res(s, at(y, rad)) - r[i]) / 1e-6
                        for i, s in enumerate(surfaces)])
        mat = [[sum(u * v for u, v in zip(jac[i], jac[j], strict=True))
                + (1e-9 if i == j else 0.0) for j in range(2)]
               for i in range(2)]
        step = _solve(mat, [-sum(u * v for u, v in zip(jac[i], r, strict=True))
                            for i in range(2)])
        if step is None:
            break
        x = [x[0] + step[0], x[1] + step[1]]
        if max(abs(v) for v in step) < 1e-11:
            break
    exact = (abs(x[0] - rho) < MOVE_MAX and abs(x[1] - z) < MOVE_MAX
             and x[0] > 1e-3
             and all(abs(_res(s, at(x, out))) < 1e-6
                     for s in surfaces for out in (rad, side)))
    if not exact:
        x = [rho, z]
    centre = tuple(frame.o[k] + x[1] * frame.d[k] for k in range(3))
    return centre, frame.d, x[0]


def _circle_fit(pts):
    """A circle through 3 and more points in space, or None."""
    c, n = _least_normal(pts)
    if any(abs(_dot(_sub(p, c), n)) > EPS for p in pts):
        return None
    u = _unit(_cross(n, (1.0, 0.0, 0.0) if abs(n[0]) < 0.9
                     else (0.0, 1.0, 0.0)))
    v = _cross(n, u)
    flat = [(_dot(_sub(p, c), u), _dot(_sub(p, c), v)) for p in pts]
    fit = _circle2(flat)
    if fit is None or fit[2] > 500.0 or any(
            abs(math.hypot(x - fit[0], y - fit[1]) - fit[2]) > EPS
            for x, y in flat):
        return None
    centre = tuple(c[k] + fit[0] * u[k] + fit[1] * v[k] for k in range(3))
    return centre, n, fit[2]


def _turn(centre, axis, pts):
    """The signed angle the points turn about the axis, radians."""
    total = 0.0
    for p, q in zip(pts, pts[1:], strict=False):
        a, b = _sub(p, centre), _sub(q, centre)
        total += math.atan2(_dot(_cross(a, b), axis), _dot(a, b))
    return total


def _helix_points(frame, rho, z0, pitch, t0, t1, end=None):
    """Control points of cubic pieces along a helix.

    The helix has radius rho about the frame's axis and the height
    z0 + pitch * t / 2 pi at the angle t; the pieces run from t0 to t1.
    With ``end`` = (rho, z0) at t1 the radius and z0 change evenly from
    their values at t0, so that an edge reaches both its vertices.

    Returns:
        (points, pieces): 3 * pieces + 1 control points.
    """
    pieces = max(1, math.ceil(abs(t1 - t0) / math.radians(HELIX_DEG)))
    step = (t1 - t0) / pieces
    arm = 4.0 / 3.0 * math.tan(step / 4)
    rho1, z1 = end if end else (rho, z0)
    out_rate = (rho1 - rho) / (t1 - t0) * step / 3
    up_rate = ((z1 - z0) / (t1 - t0) + pitch / (2 * math.pi)) * step / 3

    def at(t):
        part = (t - t0) / (t1 - t0)
        r = rho + (rho1 - rho) * part
        cs, sn = math.cos(t), math.sin(t)
        z = z0 + (z1 - z0) * part + pitch * t / (2 * math.pi)
        point = tuple(frame.o[k] + r * (cs * frame.u[k] + sn * frame.v[k])
                      + z * frame.d[k] for k in range(3))
        along = tuple(r * (cs * frame.v[k] - sn * frame.u[k]) * arm
                      + out_rate * (cs * frame.u[k] + sn * frame.v[k])
                      + up_rate * frame.d[k] for k in range(3))
        return point, along

    out = [at(t0)[0]]
    for i in range(pieces):
        pa, ta = at(t0 + i * step)
        pb, tb = at(t0 + (i + 1) * step)
        out += [tuple(pa[k] + ta[k] for k in range(3)),
                tuple(pb[k] - tb[k] for k in range(3)), pb]
    return out, pieces


def _smooth(points):
    """Cubic pieces as one curve with a continuous tangent.

    Where two of the pieces of ``_helix_points`` join, the tangent is
    continuous and the joint is midway between its neighbours; without
    the joints the same curve is a B-spline with double inner knots.

    Returns:
        (control points, knots, multiplicities).
    """
    pieces = (len(points) - 1) // 3
    keep = [p for i, p in enumerate(points)
            if i % 3 or i in (0, len(points) - 1)]
    return (keep, [float(i) for i in range(pieces + 1)],
            [4] + [2] * (pieces - 1) + [4])


def _helix_edge(frame, pitch, pts, ends, surfaces):
    """The helix through the mesh points pts, or None.

    Mesh points on one helix within EPS give it. Else, between two
    surfaces about the axis, a thread's or a cylinder's or cone's, the
    helix is where they meet, and the mesh points may be HELIX_NEAR mm
    from it: where a flank runs into a rounded corner the two surfaces
    are one within EPS over a band, and the mesh's border wanders in
    it.
    """
    ang = _angles(frame, pts)
    if abs(ang[-1] - ang[0]) < 1e-3:
        return None
    rho = [frame.polar(p)[0] for p in pts]
    lift = [frame.polar(p)[1] - pitch * a / (2 * math.pi)
            for p, a in zip(pts, ang, strict=True)]
    x = [sum(rho) / len(rho), sum(lift) / len(lift)]
    near = EPS
    clean = all(abs(r - x[0]) <= EPS for r in rho) and all(
        abs(h - x[1]) <= 2 * EPS for h in lift)
    if not clean and all(
            s[0] in ("screw", "coil") or (s[0] == "cone" and s[1] is frame)
            for s in surfaces):
        rad = frame.polar(pts[0])[2]

        def at(y):
            z = y[1] + pitch * ang[0] / (2 * math.pi)
            return tuple(frame.o[k] + y[0] * rad[k] + z * frame.d[k]
                         for k in range(3))

        for _it in range(12):
            r = [_res(s, at(x)) for s in surfaces]
            jac = []
            for k in range(2):
                y = list(x)
                y[k] += 1e-6
                jac.append([(_res(s, at(y)) - r[i]) / 1e-6
                            for i, s in enumerate(surfaces)])
            mat = [[sum(u * v for u, v in zip(jac[i], jac[j], strict=True))
                    + (1e-9 if i == j else 0.0) for j in range(2)]
                   for i in range(2)]
            step = _solve(mat, [-sum(
                u * v for u, v in zip(jac[i], r, strict=True))
                                for i in range(2)])
            if step is None:
                return None
            x = [x[0] + step[0], x[1] + step[1]]
            if max(abs(v) for v in step) < 1e-11:
                break
        if any(abs(_res(s, at(x))) > EPS for s in surfaces):
            return None
        near = HELIX_NEAR
    if any(abs(r - x[0]) > near for r in rho) or any(
            abs(h - x[1]) > 2 * near for h in lift):
        return None
    turn = []
    for end, about in zip(ends, (ang[0], ang[-1]), strict=True):
        a = _angles(frame, [end])[0]
        turn.append(about + (a - about + math.pi) % (2 * math.pi) - math.pi)
    # Through the two vertices: they are within the same tolerance of
    # the helix, and an edge that ends beside its vertex leaves a gap.
    at = [frame.polar(e) for e in ends]
    low, high = ((q[0], q[1] - pitch * a / (2 * math.pi))
                 for q, a in zip(at, turn, strict=True))
    points, _pieces = _helix_points(frame, low[0], low[1], pitch, turn[0],
                                    turn[1], high)
    return ("bspline", 3) + _smooth(points)


def _cut_curve(s, pts, ends):
    """A polyline on the surface s along the mesh edges through pts.

    Each mesh edge is a chord of the surface. Points are put on it and
    moved onto the surface, twice as many each time, until the pieces
    between them stay within EPS / 2 of the surface.
    """
    path = [ends[0]] + list(pts[1:-1]) + [ends[1]]
    out = [path[0]]
    for a, b in zip(path, path[1:], strict=False):
        count = 1
        while True:
            on = [_meet([s], tuple(a[k] + (b[k] - a[k]) * i / count
                                   for k in range(3)), 2 * SAG)
                  for i in range(1, count)]
            piece = [a] + on + [b]
            if count >= 64 or all(
                    abs(_res(s, tuple((p[k] + q[k]) / 2 for k in range(3))))
                    < EPS / 2 for p, q in zip(piece, piece[1:], strict=False)):
                break
            count *= 2
        out += piece[1:]
    chord = _sub(out[-1], out[0])
    length = math.sqrt(_dot(chord, chord))
    if length > 0 and all(
            math.sqrt(max(0.0, _dot(q, q) - (_dot(q, chord) / length) ** 2))
            < EPS / 2 for q in (_sub(p, out[0]) for p in out[1:-1])):
        return ("line",)
    return ("bspline", 1, out, [float(i) for i in range(len(out))],
            [2] + [1] * (len(out) - 2) + [2])


def _edge_curve(pts, closed, surfaces, ends):
    """The curve of an edge through the mesh points pts.

    Args:
        pts: The points along the edge, the first again at the end of a
            closed edge.
        closed: True for a closed edge.
        surfaces: The surfaces of the two faces the edge is between.
        ends: The edge's two vertices, where its faces' surfaces meet.

    Returns:
        ("line",), ("circle", centre, axis, radius) or ("bspline", ...):
        a helix as cubic pieces, any other curve as a polyline.
    """
    a, b = pts[0], pts[-1]
    if not closed:
        chord = _sub(b, a)
        length = math.sqrt(_dot(chord, chord))
        if length > 0:
            d = (chord[0] / length, chord[1] / length, chord[2] / length)
            straight = all(
                math.sqrt(max(0.0, _dot(q, q) - _dot(q, d) ** 2)) < EPS
                for q in (_sub(p, a) for p in pts[1:-1]))
            mids = [tuple((p[k] + q[k]) / 2 for k in range(3))
                    for p, q in zip(pts, pts[1:], strict=False)]
            if straight and all(abs(_res(s, m)) < EPS
                                for s in surfaces for m in mids):
                return ("line",)
    cut = surfaces[0] is surfaces[1] and not closed
    for s in surfaces:
        if s[0] in ("screw", "coil") and not closed and not cut:
            helix = _helix_edge(s[1], s[2], pts, ends, surfaces)
            if helix:
                return helix
    ring = pts[:-1] if closed else pts
    found = None
    for s in surfaces:
        if s[0] in ("cone", "torus"):
            found = _circle_about(s[1], ring, surfaces)
            if found:
                break
    if found is None and len(ring) >= 3:
        found = _circle_fit(ring)
    if found:
        centre, axis, radius = found
        turn = _turn(centre, axis, pts)
        size = sorted(abs(_turn(centre, axis, pair))
                      for pair in zip(pts, pts[1:], strict=False))
        if (closed and abs(abs(turn) - 2 * math.pi) > 0.5) \
                or size[-1] > math.radians(CHORD_DEG) \
                or size[-1] > UNIFORM * size[len(size) // 2] + 1e-3:
            found = None
        elif turn < 0:
            axis = (-axis[0], -axis[1], -axis[2])
    if found:
        return ("circle", centre, axis, radius)
    if cut:
        return _cut_curve(surfaces[0], pts, ends)
    if len(pts) == 2:
        return ("line",)
    knots = [float(i) for i in range(len(pts))]
    mults = [2] + [1] * (len(pts) - 2) + [2]
    return ("bspline", 1, [ends[0]] + list(pts[1:-1]) + [ends[1]], knots,
            mults)


def _screw_surface(s, sense, pts, angles):
    """A thread flank as a B-spline surface over its points.

    Args:
        s: The screw surface.
        sense: 1.0 when the mesh's normals are along the surface's own.
        pts: The face's points.
        angles: Their angles about the axis, without jumps across the
            face.

    Returns:
        (surface, same sense) as step_gen takes them.
    """
    frame, pitch = s[1], s[2]
    polar = [frame.polar(p) for p in pts]
    prof = [(q[0], q[1] - pitch * a / (2 * math.pi))
            for q, a in zip(polar, angles, strict=True)]
    c, (a, b) = _least_normal(prof)
    if a * s[3] + b * s[4] < 0:
        a, b = -a, -b
    offset = a * c[0] + b * c[1]
    rim = math.radians(2.0)
    t0, t1 = min(angles) - rim, max(angles) + rim
    r0 = max(0.01, min(q[0] for q in polar) - 0.05)
    r1 = max(q[0] for q in polar) + 0.05
    rows = []
    for rho in (r0, r1):
        row, _pieces = _helix_points(frame, rho, (offset - a * rho) / b,
                                     pitch, t0, t1)
        row, knots, mults = _smooth(row)
        rows.append(row)
    net = [list(pair) for pair in zip(*rows, strict=False)]
    # The surface's own normal: along the helix crossed with along the
    # profile line, at the first point.
    rho, _z, rad = polar[0]
    about = _cross(frame.d, rad)
    turn = tuple(rho * about[k] + pitch / (2 * math.pi) * frame.d[k]
                 for k in range(3))
    line = tuple((r1 - r0) * rad[k] - a / b * (r1 - r0) * frame.d[k]
                 for k in range(3))
    own = _cross(turn, line)
    out = _normal(s, pts[0])
    same = (_dot(own, out) > 0) == (sense > 0)
    return ("bspline", 3, 1, net, knots, mults, [0.0, 1.0], [2, 2]), same


def _coil_surface(s, sense, pts, angles):
    """A thread's rounded corner as a B-spline surface over its points.

    Args:
        s: The coil surface.
        sense: 1.0 when the mesh's normals are along the surface's own.
        pts: The face's points.
        angles: Their angles about the axis, without jumps across the
            face.

    Returns:
        (surface, same sense) as step_gen takes them.
    """
    frame, pitch, rc, rm = s[1], s[2], s[3], s[5]
    polar = [frame.polar(p) for p in pts]
    lift = [q[1] - pitch * a / (2 * math.pi)
            for q, a in zip(polar, angles, strict=True)]
    hc = s[4] + pitch * round((sum(lift) / len(lift) - s[4]) / pitch)
    mid = math.atan2(sum(lift) / len(lift) - hc,
                     sum(q[0] for q in polar) / len(polar) - rc)
    span = [(math.atan2(h - hc, q[0] - rc) - mid + math.pi) % (2 * math.pi)
            - math.pi for q, h in zip(polar, lift, strict=True)]
    rim = math.radians(3.0)
    f0, f1 = mid + min(span) - rim, mid + max(span) + rim
    arcs = max(1, math.ceil((f1 - f0) / math.radians(45.0)))
    step = (f1 - f0) / arcs
    arm = 4.0 / 3.0 * math.tan(step / 4) * rm
    prof = []
    for i in range(arcs + 1):
        f = f0 + i * step
        at = (rc + rm * math.cos(f), hc + rm * math.sin(f))
        along = (-arm * math.sin(f), arm * math.cos(f))
        if i:
            prof.append((at[0] - along[0], at[1] - along[1]))
        prof.append(at)
        if i < arcs:
            prof.append((at[0] + along[0], at[1] + along[1]))
    rim = math.radians(2.0)
    t0, t1 = min(angles) - rim, max(angles) + rim
    rows = []
    for i, (rho, h) in enumerate(prof):
        if i % 3 == 0 and 0 < i < len(prof) - 1:
            continue                    # a joint of the profile's arcs
        row, _pieces = _helix_points(frame, rho, h, pitch, t0, t1)
        row, knots, mults = _smooth(row)
        rows.append(row)
    net = [list(group) for group in zip(*rows, strict=False)]
    # The surface's own normal: along the helix crossed with along the
    # profile circle, at the first point.
    rho, _z, rad = polar[0]
    about = _cross(frame.d, rad)
    turn = tuple(rho * about[k] + pitch / (2 * math.pi) * frame.d[k]
                 for k in range(3))
    f = math.atan2(lift[0] - hc, rho - rc)
    arc = tuple(-math.sin(f) * rad[k] + math.cos(f) * frame.d[k]
                for k in range(3))
    same = (_dot(_cross(turn, arc), _normal(s, pts[0])) > 0) == (sense > 0)
    return ("bspline", 3, 3, net, knots, mults,
            [float(i) for i in range(arcs + 1)],
            [4] + [2] * (arcs - 1) + [4]), same


def _step_surface(s, sense, at):
    """The surface as step_gen takes it, and its same-sense flag."""
    if s[0] == "plane":
        n = s[1] if sense > 0 else (-s[1][0], -s[1][1], -s[1][2])
        d = _res(s, at)
        origin = tuple(at[k] - d * s[1][k] for k in range(3))
        return ("plane", origin, n), True
    frame = s[1]
    if s[0] == "torus":
        origin = tuple(frame.o[k] + s[3] * frame.d[k] for k in range(3))
        return ("torus", origin, frame.d, s[2], s[4]), sense > 0
    a, b, c = s[2], s[3], s[4]
    if abs(b) < 1e-7:
        return ("cylinder", frame.o, frame.d, c / a), sense > 0
    z = frame.polar(at)[1]
    origin = tuple(frame.o[k] + z * frame.d[k] for k in range(3))
    axis = frame.d if -b / a > 0 else (-frame.d[0], -frame.d[1], -frame.d[2])
    return ("cone", origin, axis, (c - b * z) / a,
            math.atan(abs(b / a))), sense > 0


def _unwound(part, frame, members):
    """A face's points and their angles about the axis, without jumps."""
    angle, _centre = _unwind(part, frame, members)
    verts = sorted(angle)
    return [part.pts[v] for v in verts], [angle[v] for v in verts]


def _topology(fit):
    """The faces' edges and loops from the triangle regions.

    Returns:
        (points, edges, faces) as step_gen's ("brep", ...) takes them.
    """
    part, region = fit.part, fit.region
    tris, twin = part.tris, part.twin
    border = {}                 # directed mesh edge -> its region
    for t, (a, b, c) in enumerate(tris):
        for e in ((a, b), (b, c), (c, a)):
            if region[twin[(e[1], e[0])]] != region[t]:
                border[e] = region[t]
    chains = _chains(sorted((a, b) for a, b in border if a < b))
    ends = set()
    for chain in chains:
        ends.update((chain[0], chain[-1]))
    index = {v: i for i, v in enumerate(sorted(ends))}
    points = [_meet([fit.surfaces[r][0] for r in sorted(
        {region[t] for t in part.fan[v]})], part.pts[v])
        for v in sorted(ends)]
    edges, edge_of, length, verts = [], {}, [], []
    for k, chain in enumerate(chains):
        left, right = border[(chain[0], chain[1])], border[(chain[1],
                                                           chain[0])]
        closed = chain[0] == chain[-1]
        along = [part.pts[v] for v in chain]
        pair = (index[chain[0]], index[chain[-1]])
        curve = _edge_curve(along, closed,
                            (fit.surfaces[left][0], fit.surfaces[right][0]),
                            (points[pair[0]], points[pair[1]]))
        edges.append(pair + (curve,))
        length.append(sum(math.dist(p, q) for p, q in zip(
            along, along[1:], strict=False)))
        verts.append(along)
        for a, b in zip(chain, chain[1:], strict=False):
            edge_of[(a, b)] = (k, True, a == chain[0])
            edge_of[(b, a)] = (k, False, b == chain[-1])

    def follow(e):
        """The next border edge of the same region after edge e."""
        t, b = twin[e], e[1]
        while True:
            tri = tris[t]
            nxt = (b, tri[(tri.index(b) + 1) % 3])
            if nxt in border:
                return nxt
            t = twin[(nxt[1], nxt[0])]

    def simple(loop):
        """The loop cut where it passes one vertex twice."""
        out, path, at = [], [], {}
        for k, fwd in loop:
            start = chains[k][0] if fwd else chains[k][-1]
            end = chains[k][-1] if fwd else chains[k][0]
            at.setdefault(start, len(path))
            path.append((k, fwd, start))
            if end in at:
                cut = at[end]
                out.append([(i, f) for i, f, _s in path[cut:]])
                for _i, _f, s in path[cut:]:
                    if at.get(s, -1) >= cut:
                        del at[s]
                del path[cut:]
        return out

    loops = {}
    done = set()
    for e in sorted(border):
        if e in done or not edge_of[e][2]:
            continue
        loop, cur = [], e
        while cur not in done:
            done.add(cur)
            k, fwd, first = edge_of[cur]
            if first:
                loop.append((k, fwd))
            cur = follow(cur)
        loops.setdefault(border[e], []).extend(simple(loop))
    faces = []
    for r, (s, sense) in enumerate(fit.surfaces):
        mine = loops.get(r, [])
        at = part.pts[tris[fit.members[r][0]][0]]
        if s[0] in ("screw", "coil"):
            make = _screw_surface if s[0] == "screw" else _coil_surface
            surface, same = make(
                s, sense, *_unwound(part, s[1], fit.members[r]))
        else:
            surface, same = _step_surface(s, sense, at)

        def size(loop, surface=surface):
            if surface[0] != "plane":
                return sum(length[k] for k, _f in loop)
            ring = []
            for k, fwd in loop:
                ring += verts[k][:-1] if fwd else verts[k][:0:-1]
            total = [0.0, 0.0, 0.0]
            for p, q in zip(ring, ring[1:] + ring[:1], strict=True):
                total[0] += (p[1] - q[1]) * (p[2] + q[2])
                total[1] += (p[2] - q[2]) * (p[0] + q[0])
                total[2] += (p[0] - q[0]) * (p[1] + q[1])
            return _dot(total, surface[2])
        mine.sort(key=lambda lp: -size(lp))
        faces.append((surface, same, mine))
    return points, edges, faces


def solid(points, tris):
    """One closed mesh part as faces on fitted surfaces.

    Args:
        points: The part's points, in the frame whose z axis is the
            part's main axis.
        tris: Its triangles as index triples, counter-clockwise from
            outside.

    Returns:
        (points, edges, faces, report): the first three as step_gen's
        ("brep", ...) solid takes them; ``report`` maps a surface kind
        ("plane", "cylinder", "cone", "torus", "helicoid") to (faces,
        area in mm2).
    """
    fit = _Refit(_Part(points, tris))
    fit.run()
    out_points, edges, faces = _topology(fit)
    report = {}
    for (surface, _same, _loops), members in zip(faces, fit.members,
                                                 strict=True):
        kind = "helicoid" if surface[0] == "bspline" else surface[0]
        n, area = report.get(kind, (0, 0.0))
        report[kind] = (n + 1, area + sum(fit.part.area[t]
                                          for t in members))
    return out_points, edges, faces, report
