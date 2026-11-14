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

# A vertex is on a surface within EPS mm. A triangle is a chord of the
# surface: the surface's normal turns by at most CHORD_DEG degrees
# between its vertices, and its own normal is within NORMAL_DEG plus
# NORMAL_SHARE of that turn of the surface's normal at its centre. A
# flat face whose corners lie on a curved surface fails this.
EPS = 6e-4
NORMAL_DEG, NORMAL_SHARE = 10.0, 0.6
# A triangle's centre is within SAG mm of the surface it is a chord of.
SAG = 0.1
# A face of 1 or 2 triangles joins a face beside it whose surface holds
# its vertices within GATHER times EPS.
GATHER = 2.5
# A triangle on two surfaces at once goes to the later of these.
RANK = ("plane", "cone", "screw", "torus", "coil")
# A triangle on a thread's surface covers FLAT_SHARE of its area when
# laid out along the helix and the profile.
FLAT_SHARE = 0.3
# The steps of an arc on the mesh's edges differ by at most UNIFORM.
UNIFORM = 3.0
# A cone or cylinder holds two triangles turned SPREAD_DEG against each
# other; a torus holds PROFILE_MIN places on its profile circle
# that are PROFILE_GAP mm and PROFILE_SHARE of its radius apart.
SPREAD_DEG = 3.0
PROFILE_MIN, PROFILE_GAP, PROFILE_SHARE = 4, 5e-3, 0.08
# A thread: PITCH_EDGES mesh edges run along helices of one pitch
# between PITCH_MIN and PITCH_MAX mm, equal within PITCH_TOL of it. A
# flank's profile line leans at least SCREW_MIN (sine) from a cylinder.
# A helix is written as cubic pieces of at most HELIX_DEG degrees; the
# mesh's border between two surfaces about the axis is within
# HELIX_NEAR mm of the helix they meet in.
PITCH_EDGES, PITCH_MIN, PITCH_MAX, PITCH_TOL = 30, 0.2, 10.0, 0.02
SCREW_MIN = 0.05
HELIX_DEG = 15.0
HELIX_NEAR = 0.02
# A curved face that winds further about its axis than a turn plus
# WOUND_DEG degrees is cut into sectors of SECTOR_DEG by planes through
# the axis. A cut that is not straight is a polyline within EPS / 2 of
# the surface.
WOUND_DEG, SECTOR_DEG = 20.0, 120.0
# A fitted axis stays within AXIS_STAY mm of the arcs' centre, and a
# surface about it holds SUPPORT vertices more than it has parameters.
AXIS_STAY, SUPPORT = 0.03, 4
# A triangle lower than THIN mm over its longest edge has no usable
# normal; it goes where its vertices are.
THIN = 2e-3
# A vertex or a circle moves at most MOVE_MAX mm from the mesh to where
# its faces' surfaces meet.
MOVE_MAX = 3e-3
# One mesh edge is an arc of at most CHORD_DEG degrees.
CHORD_DEG = 61.0
# Radii, centres and offsets within SNAP_TOL of a multiple of SNAP mm
# take that value when the surface still holds its vertices.
SNAP, SNAP_TOL = 1e-3, 4e-4
# Arcs on the mesh's edges that give an axis: at least ARC_MIN vertices
# over ARC_SPAN degrees, within ARC_TOL mm of their circle. Centres
# within AXIS_MERGE mm are one axis.
ARC_MIN, ARC_SPAN, ARC_TOL, AXIS_MERGE = 4, 30.0, 4e-4, 1e-2
LEVEL_TOL = 1e-4
FIT_MAX = 400           # vertices a fit uses at most
PLANE_KEEP = 0.999      # a plane wins when it covers this share of the best

_CHORD_COS = math.cos(math.radians(CHORD_DEG))


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def _unit(a):
    m = math.sqrt(_dot(a, a))
    return (a[0] / m, a[1] / m, a[2] / m)


def _solve(mat, rhs):
    """Solve a small dense system by elimination with pivoting.

    Returns:
        The solution, or None when the matrix is singular.
    """
    n = len(rhs)
    a = [list(mat[i]) + [rhs[i]] for i in range(n)]
    for i in range(n):
        p = max(range(i, n), key=lambda r: abs(a[r][i]))
        if abs(a[p][i]) < 1e-14:
            return None
        a[i], a[p] = a[p], a[i]
        for r in range(i + 1, n):
            f = a[r][i] / a[i][i]
            for c in range(i, n + 1):
                a[r][c] -= f * a[i][c]
    out = [0.0] * n
    for i in range(n - 1, -1, -1):
        out[i] = (a[i][n] - sum(a[i][c] * out[c]
                                for c in range(i + 1, n))) / a[i][i]
    return out


def _least_normal(points):
    """Centroid and direction of least spread of points in 3 or 2 D."""
    n, dim = len(points), len(points[0])
    c = [sum(p[k] for p in points) / n for k in range(dim)]
    m = [[sum((p[i] - c[i]) * (p[j] - c[j]) for p in points)
          for j in range(dim)] for i in range(dim)]
    if dim == 2:
        # Eigenvector of the smaller eigenvalue of a symmetric 2 x 2.
        ang = 0.5 * math.atan2(2 * m[0][1], m[0][0] - m[1][1])
        return c, (-math.sin(ang), math.cos(ang))
    vec = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]
    for _sweep in range(30):            # Jacobi rotations
        off = abs(m[0][1]) + abs(m[0][2]) + abs(m[1][2])
        if off < 1e-18:
            break
        for i, j in ((0, 1), (0, 2), (1, 2)):
            if abs(m[i][j]) < 1e-300:
                continue
            th = 0.5 * math.atan2(2 * m[i][j], m[j][j] - m[i][i])
            cs, sn = math.cos(th), math.sin(th)
            for k in range(3):
                m[k][i], m[k][j] = (cs * m[k][i] - sn * m[k][j],
                                    sn * m[k][i] + cs * m[k][j])
            for k in range(3):
                m[i][k], m[j][k] = (cs * m[i][k] - sn * m[j][k],
                                    sn * m[i][k] + cs * m[j][k])
            for k in range(3):
                vec[k][i], vec[k][j] = (cs * vec[k][i] - sn * vec[k][j],
                                        sn * vec[k][i] + cs * vec[k][j])
    k = min(range(3), key=lambda i: m[i][i])
    return c, _unit((vec[0][k], vec[1][k], vec[2][k]))


def _circle2(points):
    """Least-squares circle through 2-D points (Kasa).

    Returns:
        (x, y, radius), or None for points on a line.
    """
    n = len(points)
    sx = sum(p[0] for p in points) / n
    sy = sum(p[1] for p in points) / n
    suu = suv = svv = suuu = svvv = suvv = svuu = 0.0
    for x, y in points:
        u, v = x - sx, y - sy
        suu += u * u
        suv += u * v
        svv += v * v
        suuu += u * u * u
        svvv += v * v * v
        suvv += u * v * v
        svuu += v * u * u
    det = suu * svv - suv * suv
    if abs(det) < 1e-12 * max(1e-12, suu * svv):
        return None
    uc = (svv * (suuu + suvv) - suv * (svvv + svuu)) / (2 * det)
    vc = (suu * (svvv + svuu) - suv * (suuu + suvv)) / (2 * det)
    return sx + uc, sy + vc, math.sqrt(uc * uc + vc * vc + (suu + svv) / n)


class Frame:
    """An axis: a point, a direction and two directions across it.

    ``free`` is True when a fit may move the point across the axis.
    """

    def __init__(self, origin, axis, across, free, home=None):
        self.o, self.d, self.u = origin, axis, across
        self.v = _cross(axis, across)
        self.free = free
        self.home = origin if home is None else home

    def moved(self, du, dv):
        """The frame with its point moved by (du, dv) across the axis."""
        o = tuple(self.o[k] + du * self.u[k] + dv * self.v[k]
                  for k in range(3))
        return Frame(o, self.d, self.u, self.free, self.home)

    def placed(self, origin):
        """The frame with its point at origin."""
        return Frame(origin, self.d, self.u, self.free, self.home)

    def polar(self, p):
        """Distance from the axis, height along it, radial direction."""
        q = _sub(p, self.o)
        x, y, z = _dot(q, self.u), _dot(q, self.v), _dot(q, self.d)
        rho = math.hypot(x, y)
        if rho < 1e-12:
            return 0.0, z, self.u
        cx, cy = x / rho, y / rho
        return rho, z, tuple(cx * self.u[k] + cy * self.v[k]
                             for k in range(3))


# A surface is ("plane", normal, offset), ("cone", frame, a, b, c) with
# the profile line a * rho + b * z = c, a >= 0 (a cylinder has b = 0),
# ("torus", frame, rc, zc, rm) with the profile circle of radius rm
# about (rc, zc), ("screw", frame, pitch, a, b, c): the line
# a * rho + b * h = c turned about the axis while it climbs one pitch
# per turn, h = z - pitch * angle / 2 pi taken to the nearest turn (a
# thread's flank), or ("coil", frame, pitch, rc, hc, rm): the circle of
# radius rm about (rc, hc) turned and climbing the same way (a
# thread's rounded corner). ``_res`` is a point's signed distance from
# the surface, ``_normal`` the surface's own normal there: away from
# the axis or the profile circle's centre.

def _lift(s, p):
    """A point on the screw or coil s.

    Returns:
        Its distance from the axis, its h, and the directions out from
        the axis and about it.
    """
    frame, pitch = s[1], s[2]
    rho, z, rad = frame.polar(p)
    ang = math.atan2(_dot(rad, frame.v), _dot(rad, frame.u))
    h = z - pitch * ang / (2 * math.pi)
    if s[0] == "screw":
        h -= pitch * round((s[3] * rho + s[4] * h - s[5]) / (s[4] * pitch))
    else:
        h -= pitch * round((h - s[4]) / pitch)
    return rho, h, rad, _cross(frame.d, rad)


def _res(s, p):
    if s[0] == "plane":
        return _dot(s[1], p) - s[2]
    if s[0] == "screw":
        rho, h, _rad, _about = _lift(s, p)
        lead = s[4] * s[2] / (2 * math.pi * max(rho, 1e-9))
        return (s[3] * rho + s[4] * h - s[5]) / math.sqrt(
            s[3] ** 2 + s[4] ** 2 + lead ** 2)
    if s[0] == "coil":
        rho, h, _rad, _about = _lift(s, p)
        return math.hypot(rho - s[3], h - s[4]) - s[5]
    rho, z, _rad = s[1].polar(p)
    if s[0] == "cone":
        return s[2] * rho + s[3] * z - s[4]
    return math.hypot(rho - s[2], z - s[3]) - s[4]


def _normal(s, p):
    if s[0] == "plane":
        return s[1]
    if s[0] in ("screw", "coil"):
        rho, h, rad, about = _lift(s, p)
        a, b = (s[3], s[4]) if s[0] == "screw" else (rho - s[3], h - s[4])
        lead = b * s[2] / (2 * math.pi * max(rho, 1e-9))
        d = s[1].d
        return _unit(tuple(a * rad[k] + b * d[k] - lead * about[k]
                           for k in range(3)))
    rho, z, rad = s[1].polar(p)
    if s[0] == "cone":
        a, b = s[2], s[3]
    else:
        a, b = rho - s[2], z - s[3]
        m = math.hypot(a, b) or 1.0
        a, b = a / m, b / m
    d = s[1].d
    return (a * rad[0] + b * d[0], a * rad[1] + b * d[1],
            a * rad[2] + b * d[2])


def _gauss_newton(make, x, points):
    """Improve the parameters x of the surface make(x) on points."""
    for _it in range(8):
        s = make(x)
        if s is None:
            return None
        r = [_res(s, p) for p in points]
        cols = []
        for k in range(len(x)):
            y = list(x)
            y[k] += 1e-6
            sk = make(y)
            if sk is None:
                return None
            cols.append([(_res(sk, p) - r[i]) / 1e-6
                         for i, p in enumerate(points)])
        n = len(x)
        jtj = [[sum(a * b for a, b in zip(cols[i], cols[j], strict=True))
                for j in range(n)] for i in range(n)]
        lam = 1e-9 * (sum(jtj[i][i] for i in range(n)) / n + 1e-30)
        for i in range(n):
            jtj[i][i] += lam
        step = _solve(jtj, [-sum(a * b
                                 for a, b in zip(cols[i], r, strict=True))
                            for i in range(n)])
        if step is None:
            return None
        x = [x[i] + step[i] for i in range(n)]
        if max(abs(v) for v in step) < 1e-9:
            break
    return make(x)


def _sample(points):
    if len(points) <= FIT_MAX:
        return points
    step = len(points) / FIT_MAX
    return [points[int(i * step)] for i in range(FIT_MAX)]


def _fit(s, points):
    """The surface of the kind of s that fits points best, or None."""
    points = _sample(points)
    if s[0] == "plane":
        if len(points) < 3:
            return s
        c, n = _least_normal(points)
        if _dot(n, s[1]) < 0:
            n = (-n[0], -n[1], -n[2])
        return ("plane", n, _dot(n, c))
    frame = s[1]
    if s[0] == "screw":
        prof = [_lift(s, q)[:2] for q in points]
        c, (a, b) = _least_normal(prof)
        if a * s[3] + b * s[4] < 0:
            a, b = -a, -b
        if abs(b) < SCREW_MIN:
            return None
        return ("screw", frame, s[2], a, b, a * c[0] + b * c[1])
    if s[0] == "coil":
        fit = _circle2([_lift(s, q)[:2] for q in points])
        if fit is None:
            return None
        return ("coil", frame, s[2], fit[0], fit[1], fit[2])
    if s[0] == "cone":
        if frame.d[2] != 1.0:           # a cylinder along x or y
            def make(x):
                return ("cone", frame.moved(x[0], x[1]), 1.0, 0.0, x[2])
            return _gauss_newton(make, [0.0, 0.0, s[4]], points)
        if frame.free:
            def make(x):
                return ("cone", frame.moved(x[0], x[1]), math.cos(x[2]),
                        math.sin(x[2]), x[3])
            out = _gauss_newton(make, [0.0, 0.0, math.atan2(s[3], s[2]),
                                       s[4]], points)
            if out is None or out[2] < 0:
                return None
            return out
        prof = [frame.polar(p)[:2] for p in points]
        c, (a, b) = _least_normal(prof)
        if a < 0:
            a, b = -a, -b
        return ("cone", frame, a, b, a * c[0] + b * c[1])
    if frame.free:
        def make(x):
            if x[4] <= 0:
                return None
            return ("torus", frame.moved(x[0], x[1]), x[2], x[3], x[4])
        return _gauss_newton(make, [0.0, 0.0, s[2], s[3], s[4]], points)

    def make(x):
        if x[2] <= 0:
            return None
        return ("torus", frame, x[0], x[1], x[2])
    return _gauss_newton(make, [s[2], s[3], s[4]], points)


def _snap(x):
    y = round(x / SNAP) * SNAP
    return y if abs(x - y) < SNAP_TOL else x


def _tidy(s, points):
    """The surface with round numbers where it still holds points.

    A cone that is a cylinder within EPS becomes one; an axis, a radius,
    a plane along x, y or z and a torus take values on the SNAP grid.
    """
    def holds(c):
        return all(abs(_res(c, p)) < EPS for p in points)

    if s[0] == "plane":
        n = s[1]
        k = max(range(3), key=lambda i: abs(n[i]))
        if abs(n[k]) > 1 - 1e-7:
            flat = tuple((1.0 if n[k] > 0 else -1.0) if i == k else 0.0
                         for i in range(3))
            for d in (_snap(sum(_dot(flat, p) for p in points)
                            / len(points)), s[2]):
                if holds(("plane", flat, d)):
                    return ("plane", flat, d)
        return s
    if s[0] in ("screw", "coil"):
        return s
    frame = s[1]
    if frame.free:
        o = tuple(_snap(c) for c in frame.o)
        c = (s[0], frame.placed(o)) + s[2:]
        if holds(c):
            s, frame = c, c[1]
    if s[0] == "cone":
        if abs(s[3]) < 0.02:
            rho = sum(frame.polar(p)[0] for p in points) / len(points)
            for r in (_snap(rho), max(frame.polar(p)[0] for p in points)
                      - EPS / 2, rho):
                c = ("cone", frame, 1.0, 0.0, r)
                if holds(c):
                    return c
        return s
    c = ("torus", frame, _snap(s[2]), _snap(s[3]), _snap(s[4]))
    return c if holds(c) else s


class _Part:
    """One mesh part with its triangle neighbours."""

    def __init__(self, points, tris):
        self.pts, self.tris = points, tris
        self.twin = {}
        for t, (a, b, c) in enumerate(tris):
            for e in ((a, b), (b, c), (c, a)):
                self.twin[e] = t
        self.nrm, self.area, self.cen, self.thin = [], [], [], []
        self.fan = [[] for _p in points]
        for t, (a, b, c) in enumerate(tris):
            pa, pb, pc = points[a], points[b], points[c]
            n = _cross(_sub(pb, pa), _sub(pc, pa))
            m = math.sqrt(_dot(n, n))
            self.nrm.append((n[0] / m, n[1] / m, n[2] / m) if m > 0
                            else (0.0, 0.0, 0.0))
            self.area.append(m / 2)
            self.cen.append(tuple((pa[k] + pb[k] + pc[k]) / 3
                                  for k in range(3)))
            longest = max(math.dist(pa, pb), math.dist(pb, pc),
                          math.dist(pc, pa))
            self.thin.append(longest == 0 or m / longest < THIN)
            for v in (a, b, c):
                self.fan[v].append(t)

    def around(self, t):
        """The triangles across the three edges of triangle t."""
        a, b, c = self.tris[t]
        return (self.twin[(b, a)], self.twin[(c, b)], self.twin[(a, c)])


_AXES = (((0.0, 0.0, 1.0), (1.0, 0.0, 0.0)),
         ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0)),
         ((0.0, 1.0, 0.0), (0.0, 0.0, 1.0)))


def _chains(edges):
    """Join edges into chains that end where 1 or 3 and more edges meet."""
    adj = {}
    for a, b in edges:
        adj.setdefault(a, []).append(b)
        adj.setdefault(b, []).append(a)
    seen, out = set(), []

    def walk(a, b):
        chain = [a, b]
        seen.add((min(a, b), max(a, b)))
        while len(adj[chain[-1]]) == 2:
            nxt = [v for v in adj[chain[-1]] if v != chain[-2]]
            if not nxt:
                break
            key = (min(chain[-1], nxt[0]), max(chain[-1], nxt[0]))
            if key in seen:
                break
            seen.add(key)
            chain.append(nxt[0])
        return chain

    for ends in (True, False):
        for a in sorted(adj):
            if ends and len(adj[a]) == 2:
                continue
            for b in sorted(adj[a]):
                if (min(a, b), max(a, b)) not in seen:
                    out.append(walk(a, b))
    return out


def _arcs(chain, flat):
    """Runs of chain vertices on one circle, as (first, last, circle)."""
    out, i, n = [], 0, len(chain)
    while i + 2 < n:
        j, best = i + 2, _circle2([flat[v] for v in chain[i:i + 3]])
        if best is None or best[2] > 500.0:
            i += 1
            continue
        while j + 1 < n:
            xy = [flat[v] for v in chain[i:j + 2]]
            fit = _circle2(xy)
            if fit is None or max(
                    abs(math.hypot(x - fit[0], y - fit[1]) - fit[2])
                    for x, y in xy) > ARC_TOL:
                break
            best, j = fit, j + 1
        out.append((i, j, best))
        i = j
    return out


def _local_frames(part):
    """Axes of the circular arcs on the mesh's level edges.

    Returns:
        (frames, of): the frames, and for each vertex the indices of the
        frames whose arcs it is on.
    """
    frames, of = [], {}
    cos_flat = math.cos(math.radians(0.5))
    for axis, across in _AXES:
        other = _cross(axis, across)
        flat = [(_dot(p, across), _dot(p, other)) for p in part.pts]
        level = [_dot(p, axis) for p in part.pts]
        edges = []
        for (a, b), t in part.twin.items():
            if a < b and abs(level[a] - level[b]) < LEVEL_TOL and _dot(
                    part.nrm[t], part.nrm[part.twin[(b, a)]]) < cos_flat:
                edges.append((a, b))
        found = []
        for chain in _chains(edges):
            for i, j, (cx, cy, r) in _arcs(chain, flat):
                seg = chain[i:j + 1]
                if seg[0] == seg[-1]:
                    seg = seg[:-1]
                ang = [math.atan2(flat[v][1] - cy, flat[v][0] - cx)
                       for v in chain[i:j + 1]]
                steps = [(ang[k + 1] - ang[k] + math.pi) % (2 * math.pi)
                         - math.pi for k in range(len(ang) - 1)]
                size = sorted(abs(s) for s in steps)
                if size[-1] > math.radians(CHORD_DEG) or size[-1] \
                        > UNIFORM * size[len(size) // 2] + 1e-3:
                    continue
                found.append((abs(sum(steps)), cx, cy, r, seg))
        groups = []
        for span, cx, cy, _r, seg in sorted(
                found, key=lambda f: (-f[0], f[1], f[2])):
            if len(seg) < ARC_MIN or span < math.radians(ARC_SPAN):
                continue
            for g in groups:
                if math.hypot(g[0] / g[2] - cx, g[1] / g[2] - cy) \
                        < AXIS_MERGE:
                    g[0] += cx * span
                    g[1] += cy * span
                    g[2] += span
                    break
            else:
                groups.append([cx * span, cy * span, span])
        for sx, sy, w in groups:
            cx, cy = sx / w, sy / w
            if axis[2] == 1.0 and math.hypot(cx, cy) < AXIS_MERGE:
                continue                # the frame's own z axis
            origin = tuple(cx * across[k] + cy * other[k] for k in range(3))
            index = len(frames)
            frames.append(Frame(origin, axis, across, True))
            for _span, _x, _y, _r, seg in found:
                rs = [math.hypot(flat[v][0] - cx, flat[v][1] - cy)
                      for v in seg]
                if max(rs) - min(rs) < 2 * EPS + 2 * ARC_TOL:
                    for v in seg:
                        of.setdefault(v, set()).add(index)
    return frames, of


def _pitches(part, frame):
    """The pitch of the helices the mesh's edges run along.

    The edges that climb about the axis at a constant distance from it
    vote for a pitch; the densest group of votes gives the helix edges,
    and a line through height over angle along their chains the pitch.

    Returns:
        A list with the pitch, mm per turn, positive for a right-hand
        thread; empty for a part with no thread.
    """
    polar = [frame.polar(p) for p in part.pts]
    votes = []
    for (a, b), _t in part.twin.items():
        if a > b:
            continue
        (ra, za, da), (rb, zb, db) = polar[a], polar[b]
        if ra < 1e-3 or abs(ra - rb) > EPS or abs(za - zb) < 1e-3:
            continue
        ang = math.atan2(_dot(_cross(da, db), frame.d), _dot(da, db))
        if abs(ang) < 1e-3:
            continue
        pitch = 2 * math.pi * (zb - za) / ang
        if PITCH_MIN <= abs(pitch) <= PITCH_MAX:
            votes.append((pitch, a, b))
    votes.sort()
    best, j = (0, 0), 0
    for i in range(len(votes)):
        j = max(j, i)
        while j + 1 < len(votes) and votes[j + 1][0] - votes[i][0] \
                <= PITCH_TOL * abs(votes[i][0]):
            j += 1
        if j - i + 1 > best[1] - best[0]:
            best = (i, j + 1)
    if best[1] - best[0] < PITCH_EDGES:
        return []
    num = den = 0.0
    for chain in _chains([(a, b) for _p, a, b in votes[best[0]:best[1]]]):
        if len(chain) < 6 or chain[0] == chain[-1]:
            continue
        ang = _angles(frame, [part.pts[v] for v in chain])
        z = [polar[v][1] for v in chain]
        ma, mz = sum(ang) / len(ang), sum(z) / len(z)
        num += sum((x - ma) * (y - mz) for x, y in zip(ang, z, strict=True))
        den += sum((x - ma) ** 2 for x in ang)
    return [_snap(2 * math.pi * num / den)] if den > 0 else []


class _Refit:
    """The segmentation of one part into faces on surfaces."""

    def __init__(self, part):
        self.part = part
        self.main = Frame((0.0, 0.0, 0.0), (0.0, 0.0, 1.0),
                          (1.0, 0.0, 0.0), False)
        self.frames, self.frames_of = _local_frames(part)
        self.pitches = _pitches(part, self.main)
        self.region = [-1] * len(part.tris)
        self.surfaces = []      # per region: (surface, same sense)
        self.members = []       # per region: triangles

    def _chord(self, s, t, at):
        """How triangle t lies on surface s, its vertices being on it.

        Args:
            s: The surface.
            t: The triangle.
            at: Cache of the surface's normal at each vertex.

        Returns:
            1.0 or -1.0 when the triangle is a chord of the surface with
            its normal along or against the surface's, else 0.0.
        """
        part = self.part
        turn = 0.0
        if s[0] != "plane":
            for v in part.tris[t]:
                if v not in at:
                    at[v] = _normal(s, part.pts[v])
            a, b, c = (at[v] for v in part.tris[t])
            low = min(_dot(a, b), _dot(b, c), _dot(c, a))
            if low < _CHORD_COS:
                return 0.0
            turn = math.acos(min(1.0, low))
        if abs(_res(s, part.cen[t])) > SAG:
            return 0.0
        if s[0] in ("screw", "coil"):
            return self._wound(s, t)
        dot = _dot(part.nrm[t], _normal(s, part.cen[t]))
        if abs(dot) < math.cos(math.radians(NORMAL_DEG)
                               + NORMAL_SHARE * turn):
            return 0.0
        return 1.0 if dot > 0 else -1.0

    def _wound(self, s, t):
        """How triangle t lies on the screw or coil s, by its area there.

        A band of a thread is narrow and climbs: a triangle across it is
        skewed, and its normal says little. Its vertices laid out along
        the helix and along the profile do: the triangle covers
        FLAT_SHARE of its area there when it is a chord of the surface,
        and nothing when it cuts through.

        Returns:
            1.0 or -1.0 for a chord with its turning sense along or
            against the surface's normal, else 0.0.
        """
        part = self.part
        frame = s[1]
        lifted = [_lift(s, part.pts[v]) for v in part.tris[t]]
        rho = sum(q[0] for q in lifted) / 3
        flat = []
        for q in lifted:
            first = lifted[0][2]
            u = rho * math.atan2(_dot(_cross(first, q[2]), frame.d),
                                 _dot(first, q[2]))
            if s[0] == "screw":
                v = (-s[4] * q[0] + s[3] * q[1]) / math.hypot(s[3], s[4])
            else:
                ang = math.atan2(q[1] - s[4], q[0] - s[3])
                start = math.atan2(lifted[0][1] - s[4], lifted[0][0] - s[3])
                v = s[5] * ((ang - start + math.pi) % (2 * math.pi)
                            - math.pi)
            flat.append((u, v))
        area = ((flat[1][0] - flat[0][0]) * (flat[2][1] - flat[0][1])
                - (flat[2][0] - flat[0][0]) * (flat[1][1] - flat[0][1])) / 2
        if abs(area) < FLAT_SHARE * part.area[t]:
            return 0.0
        return 1.0 if area > 0 else -1.0

    def _grow(self, s, seed, sense):
        """The triangles on surface s reachable from the seed."""
        part = self.part
        pts, tris = part.pts, part.tris
        seen, stack, out = {seed}, [seed], []
        ok_vertex, at = {}, {}
        while stack:
            t = stack.pop()
            out.append(t)
            for o in part.around(t):
                if o in seen or self.region[o] >= 0:
                    continue
                seen.add(o)
                good = True
                for v in tris[o]:
                    if v not in ok_vertex:
                        ok_vertex[v] = abs(_res(s, pts[v])) < EPS
                    if not ok_vertex[v]:
                        good = False
                        break
                if good and (part.thin[o]
                             or self._chord(s, o, at) == sense):
                    stack.append(o)
        return out

    def _settle(self, s, seed):
        """Grow, fit and grow again until the face stops changing.

        Returns:
            (surface, sense, triangles), or None when the seed is not
            on the surface.
        """
        part = self.part
        best = None
        for _round in range(5):
            if s is None or any(abs(_res(s, part.pts[v])) >= EPS
                                for v in part.tris[seed]):
                break
            sense = self._chord(s, seed, {})
            if sense == 0.0:
                break
            tris = self._grow(s, seed, sense)
            if best is not None and len(tris) <= len(best[2]):
                break
            best = (s, sense, tris)
            verts = sorted({v for t in tris for v in part.tris[t]})
            s = _fit(s, [part.pts[v] for v in verts])
        return best

    def _curved_enough(self, s, tris):
        part = self.part
        frame = s[1]
        count = len({v for t in tris for v in part.tris[t]})
        params = (3 if s[0] in ("torus", "coil") else 2) + (
            2 if frame.free else 0)
        if count < params + SUPPORT or math.dist(
                frame.o, frame.home) > AXIS_STAY:
            return False
        if all(part.thin[t] for t in tris):
            return False
        if s[0] in ("cone", "screw"):
            limit = math.cos(math.radians(SPREAD_DEG))
            firm = [t for t in tris if not part.thin[t]]
            return any(_dot(part.nrm[firm[0]], part.nrm[t]) < limit
                       for t in firm)
        verts = sorted({v for t in tris for v in part.tris[t]})
        if s[0] == "coil":
            rc, zc, rm = s[3], s[4], s[5]
            prof = [_lift(s, part.pts[v])[:2] for v in verts]
        else:
            rc, zc, rm = s[2], s[3], s[4]
            prof = [frame.polar(part.pts[v])[:2] for v in verts]
        if rm < 0.02 or not (rc > rm * 1.001):
            return False
        places = sorted({round(math.atan2(z - zc, rho - rc) * rm, 6)
                         for rho, z in prof})
        count, last = 0, None
        gap = max(PROFILE_GAP, PROFILE_SHARE * rm)
        for place in places:
            if last is None or place - last > gap:
                count += 1
                last = place
        return count >= PROFILE_MIN

    def _proposals(self, seed):
        """The curved surfaces the seed triangle may lie on."""
        part = self.part
        pts = [part.pts[v] for v in part.tris[seed]]
        frames = [self.main]
        local = set()
        for v in part.tris[seed]:
            local |= self.frames_of.get(v, set())
        frames += [self.frames[i] for i in sorted(local)]
        # A circular profile shows in the seed and one neighbour: their
        # 4 vertices are at 3 places along it.
        own = set(part.tris[seed])
        groups = [pts + [part.pts[v] for v in part.tris[o] if v not in own]
                  for o in part.around(seed)]

        def circles(coords, most):
            found = {}
            for group in groups:
                prof = [coords(p) for p in group]
                fit = _circle2(prof) if len(prof) > 3 else None
                if fit is None or not 0.02 < fit[2] < most or any(
                        abs(math.hypot(x - fit[0], y - fit[1]) - fit[2])
                        > EPS for x, y in prof):
                    continue
                found[tuple(round(c, 3) for c in fit)] = fit
            return [found[k] for k in sorted(found)]

        out = []
        for pitch in self.pitches:
            first = self.main.polar(pts[0])[2]
            start = math.atan2(_dot(first, self.main.v),
                               _dot(first, self.main.u))

            def lifted(p, pitch=pitch, start=start):
                rho, z, rad = self.main.polar(p)
                ang = math.atan2(_dot(rad, self.main.v),
                                 _dot(rad, self.main.u))
                ang = start + (ang - start + math.pi) % (2 * math.pi) \
                    - math.pi
                return rho, z - pitch * ang / (2 * math.pi)

            prof = [lifted(p) for p in pts]
            if min(q[0] for q in prof) < 1e-3:
                continue
            c, (a, b) = _least_normal(prof)
            if abs(b) >= SCREW_MIN:
                out.append(("screw", self.main, pitch, a, b,
                            a * c[0] + b * c[1]))
            for fit in circles(lifted, 5.0):
                out.append(("coil", self.main, pitch, fit[0], fit[1],
                            fit[2]))
        for frame in frames:
            prof = [frame.polar(p)[:2] for p in pts]
            if max(p[0] for p in prof) < 1e-6:
                continue
            c, (a, b) = _least_normal(prof)
            if a < 0:
                a, b = -a, -b
            if frame.d[2] != 1.0:
                out.append(("cone", frame, 1.0, 0.0,
                            sum(q[0] for q in prof) / 3))
                continue
            if a > 0.02:
                out.append(("cone", frame, a, b, a * c[0] + b * c[1]))
            for fit in circles(lambda p, frame=frame: frame.polar(p)[:2],
                               200.0):
                out.append(("torus", frame, fit[0], fit[1], fit[2]))
        return out

    def _best(self, seed):
        """The surface that takes the largest area from the seed on.

        Returns:
            (surface, sense, triangles, area), over the triangles that
            have no face yet.
        """
        part = self.part
        n = part.nrm[seed]
        best = self._settle(
            ("plane", n, _dot(n, part.pts[part.tris[seed][0]])), seed)
        best_area = plane_area = sum(part.area[t] for t in best[2])
        for s in self._proposals(seed):
            got = self._settle(s, seed)
            if got is None or not self._curved_enough(got[0], got[2]):
                continue
            area = sum(part.area[t] for t in got[2])
            if area > best_area and plane_area < PLANE_KEEP * area:
                best, best_area = got, area
        return best + (best_area,)

    def run(self):
        """Give every triangle a face.

        A first pass over the bare mesh finds what each seed's best
        surface would take. The faces are then made in the order of
        that area, the largest first, each over what is still free.
        """
        part = self.part
        order = [t for t in sorted(range(len(part.tris)),
                                   key=lambda t: (-part.area[t], t))
                 if not part.thin[t]]
        covered, first = set(), []
        for seed in order:
            if seed in covered:
                continue
            got = self._best(seed)
            covered.update(got[2])
            first.append((-got[3], seed))
        for seed in [s for _a, s in sorted(first)] + order:
            if self.region[seed] >= 0:
                continue
            best = self._best(seed)
            index = len(self.members)
            for t in best[2]:
                self.region[t] = index
            verts = sorted({v for t in best[2] for v in part.tris[t]})
            self.surfaces.append(
                (_tidy(best[0], [part.pts[v] for v in verts]), best[1]))
            self.members.append(sorted(best[2]))
        order = sorted(range(len(part.tris)))
        left = [t for t in order if self.region[t] < 0]
        while left:                     # thin triangles nobody took
            rest = []
            for t in left:
                a, b, c = part.tris[t]
                sides = sorted(
                    ((math.dist(part.pts[e[0]], part.pts[e[1]]), e)
                     for e in ((a, b), (b, c), (c, a))), reverse=True)
                for _length, e in sides:
                    r = self.region[part.twin[(e[1], e[0])]]
                    if r >= 0:
                        self.region[t] = r
                        self.members[r].append(t)
                        break
                else:
                    rest.append(t)
            if len(rest) == len(left):
                raise ValueError("thin triangles with no face beside them")
            left = rest
        self._gather()
        self._sectors()

    def _gather(self):
        """Give the faces of 1 or 2 triangles to a face beside them.

        A triangle a growing face passed by, because the face's surface
        was not yet fitted to all it holds, joins it now: its vertices
        within GATHER times EPS of the surface, and a chord of it.
        """
        part, region = self.part, self.region
        reach = GATHER * EPS
        again = True
        while again:
            again = False
            for r, (s, _sense) in enumerate(self.surfaces):
                if s[0] != "plane" or len(self.members[r]) > 2:
                    continue
                for t in list(self.members[r]):
                    for o in part.around(t):
                        q = region[o]
                        other, sense = self.surfaces[q]
                        if q == r or (other[0] == "plane"
                                      and len(self.members[q]) <= 2):
                            continue
                        if any(abs(_res(other, part.pts[v])) >= reach
                               for v in part.tris[t]) or not (
                                part.thin[t]
                                or self._chord(other, t, {}) == sense):
                            continue
                        region[t] = q
                        self.members[q].append(t)
                        self.members[r].remove(t)
                        again = True
                        break
        # Where a rounded corner runs into the face it is tangent to,
        # the triangles of a narrow band lie on both surfaces, and the
        # border between the two faces wanders in it. The band goes to
        # the surface that comes later in RANK, so the border is the
        # band's edge.
        again = True
        while again:
            again = False
            for t in range(len(part.tris)):
                r = region[t]
                mine = RANK.index(self.surfaces[r][0][0])
                for o in part.around(t):
                    q = region[o]
                    other, sense = self.surfaces[q]
                    if q == r or RANK.index(other[0]) <= mine:
                        continue
                    if any(abs(_res(other, part.pts[v])) >= EPS
                           for v in part.tris[t]) or not (
                            part.thin[t]
                            or self._chord(other, t, {}) == sense):
                        continue
                    region[t] = q
                    self.members[q].append(t)
                    self.members[r].remove(t)
                    again = True
                    break
        keep = [r for r, m in enumerate(self.members) if m]
        index = {r: i for i, r in enumerate(keep)}
        self.surfaces = [self.surfaces[r] for r in keep]
        self.members = [sorted(self.members[r]) for r in keep]
        for t, r in enumerate(region):
            region[t] = index[r]

    def _sectors(self):
        """Cut the faces that wind more than a turn about their axis.

        A face on a cylinder, a cone or a thread's surface that winds
        around more than once cannot be one face. Planes through the
        axis, SECTOR_DEG apart, cut it: every triangle of the face that
        crosses a plane is split along it, with the new vertices on the
        face's surface, and the pieces between two planes become faces
        of their own.
        """
        part = self.part
        wound = []
        for r, (s, _sense) in enumerate(self.surfaces):
            if s[0] == "plane":
                continue
            _angle, centre = _unwind(part, s[1], self.members[r])
            if max(centre.values()) - min(centre.values()) \
                    > 2 * math.pi + math.radians(WOUND_DEG):
                wound.append(r)
        if not wound:
            return
        pts, tris, region = list(part.pts), list(part.tris), self.region
        count = round(360.0 / SECTOR_DEG)
        width = 2 * math.pi / count

        def wrap(x):
            return (x + math.pi) % (2 * math.pi) - math.pi

        starts = {}
        for r in wound:
            s = self.surfaces[r][0]
            frame = s[1]

            def raw(p, frame=frame):
                rad = frame.polar(p)[2]
                return math.atan2(_dot(rad, frame.v), _dot(rad, frame.u))

            ang = {v: raw(pts[v]) for t, own in enumerate(region) if own == r
                   for v in tris[t]}
            start = next(x for x in (0.013 * i for i in range(200))
                         if all(abs(wrap(a - x - k * width)) > 1e-4
                                for a in ang.values() for k in range(count)))
            starts[r] = start
            for k in range(count):
                plane = wrap(start + k * width)
                owner = {}
                for t, (a, b, c) in enumerate(tris):
                    for e in ((a, b), (b, c), (c, a)):
                        owner[e] = t
                new = {}
                for t in [t for t, own in enumerate(region) if own == r]:
                    tri = tris[t]
                    for i in range(3):
                        u, v = tri[i], tri[(i + 1) % 3]
                        key = (min(u, v), max(u, v))
                        if key in new:
                            continue
                        d, e = wrap(ang[v] - ang[u]), wrap(plane - ang[u])
                        if not (1e-9 < e < d - 1e-9 or d + 1e-9 < e < -1e-9):
                            continue
                        # On the ring through u and v: their distance
                        # from the axis and their height, part way.
                        (ru, zu, _du), (rv, zv, _dv) = (
                            frame.polar(pts[u]), frame.polar(pts[v]))
                        rho = ru + (rv - ru) * e / d
                        z = zu + (zv - zu) * e / d
                        cs, sn = math.cos(plane), math.sin(plane)
                        at = tuple(
                            frame.o[j] + rho * (cs * frame.u[j]
                                                + sn * frame.v[j])
                            + z * frame.d[j] for j in range(3))
                        beside = self.surfaces[region[owner[(v, u)]]][0]
                        on = [s]
                        if beside is not s and (beside[0] == "plane"
                                                or beside[1] is not frame):
                            on.append(beside)
                        new[key] = len(pts)
                        ang[len(pts)] = plane
                        pts.append(_meet(on, at, 2 * SAG))
                for t in range(len(tris)):
                    a, b, c = tris[t]
                    cut = [new.get((min(u, v), max(u, v)))
                           for u, v in ((a, b), (b, c), (c, a))]
                    hit = [m is not None for m in cut]
                    if not any(hit):
                        continue
                    if sum(hit) == 3:
                        raise ValueError("a plane cuts a triangle 3 times")
                    while not (hit[0] and not hit[1]):   # turn the triangle
                        a, b, c = b, c, a
                        cut, hit = cut[1:] + cut[:1], hit[1:] + hit[:1]
                    if hit[2]:          # (a, b) and (c, a) are cut
                        m, n = cut[0], cut[2]
                        pieces = [(a, m, n), (m, b, c), (m, c, n)]
                    else:               # (a, b) alone
                        m = cut[0]
                        pieces = [(a, m, c), (m, b, c)]
                    tris[t] = pieces[0]
                    for piece in pieces[1:]:
                        tris.append(piece)
                        region.append(region[t])
        part = self.part = _Part(pts, tris)
        self.members = [[] for _s in self.surfaces]
        for t, r in enumerate(region):
            self.members[r].append(t)
        for r in wound:
            s, sense = self.surfaces[r]
            frame = s[1]
            sector = {}
            for t in self.members[r]:
                rad = frame.polar(part.cen[t])[2]
                a = math.atan2(_dot(rad, frame.v), _dot(rad, frame.u))
                sector[t] = int(((a - starts[r]) % (2 * math.pi)) / width)
            seen, first = set(), True
            for begin in list(self.members[r]):
                if begin in seen:
                    continue
                seen.add(begin)
                stack, piece = [begin], []
                while stack:
                    t = stack.pop()
                    piece.append(t)
                    for o in part.around(t):
                        if o not in seen and sector.get(o) == sector[begin]:
                            seen.add(o)
                            stack.append(o)
                if first:
                    self.members[r] = sorted(piece)
                    first = False
                    continue
                for t in piece:
                    region[t] = len(self.members)
                self.surfaces.append((s, sense))
                self.members.append(sorted(piece))


def _meet(surfaces, p, most=MOVE_MAX):
    """The point nearest p that is on all the surfaces.

    Returns:
        The point, or p when it would move more than ``most``.
    """
    start = p
    for _it in range(12):
        mat = [[1e-7, 0.0, 0.0], [0.0, 1e-7, 0.0], [0.0, 0.0, 1e-7]]
        rhs = [0.0, 0.0, 0.0]
        for s in surfaces:
            r, n = _res(s, p), _normal(s, p)
            for i in range(3):
                rhs[i] -= n[i] * r
                for j in range(3):
                    mat[i][j] += n[i] * n[j]
        step = _solve(mat, rhs)
        if step is None:
            return start
        p = (p[0] + step[0], p[1] + step[1], p[2] + step[2])
        if max(abs(x) for x in step) < 1e-11:
            break
    return p if math.dist(p, start) < most else start


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


def _angles(frame, pts):
    """The points' angles about the frame's axis, without jumps."""
    out = []
    for p in pts:
        rad = frame.polar(p)[2]
        ang = math.atan2(_dot(rad, frame.v), _dot(rad, frame.u))
        if out:
            ang = out[-1] + (ang - out[-1] + math.pi) % (2 * math.pi) \
                - math.pi
        out.append(ang)
    return out


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


def _unwind(part, frame, members):
    """The angle about the axis of each vertex of a face, without jumps.

    The angle is carried from triangle to triangle across the face, so
    a face that winds around the axis more than once has angles beyond
    one turn.

    Returns:
        (angle of each vertex, angle of each triangle's centre).
    """
    inside = set(members)
    first = part.tris[members[0]][0]
    rad = frame.polar(part.pts[first])[2]
    angle = {first: math.atan2(_dot(rad, frame.v), _dot(rad, frame.u))}
    centre = {}
    # Breadth first: the angle reaches each triangle by a short way, so
    # a closed band comes out one turn wide and no wider.
    seen, stack, at = {members[0]}, [members[0]], 0
    while at < len(stack):
        t = stack[at]
        at += 1
        known = next(v for v in part.tris[t] if v in angle)
        a = frame.polar(part.pts[known])[2]
        total = 0.0
        for v in part.tris[t]:
            b = frame.polar(part.pts[v])[2]
            step = math.atan2(_dot(_cross(a, b), frame.d), _dot(a, b))
            total += step
            if v not in angle:
                angle[v] = angle[known] + step
        centre[t] = angle[known] + total / 3
        for o in part.around(t):
            if o in inside and o not in seen:
                seen.add(o)
                stack.append(o)
    return angle, centre


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
