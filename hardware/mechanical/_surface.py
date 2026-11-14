"""Surfaces a mesh part is refitted with, and the fits to its points.

The first of three modules: this one has the tolerances, the surfaces
(plane, cone, torus, and a thread's flank and rounded corner) with a
point's distance from each, the fits, the mesh part with its triangle
neighbours, and what is found on the bare mesh: the axes of its arcs
and the pitch of its helices. _segment.py grows the faces, _refit.py
gives them edges. _refit.py describes the method.
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
