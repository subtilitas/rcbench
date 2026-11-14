"""Give every triangle of a mesh part a face on a fitted surface.

The second of three modules; _refit.py describes the method.
"""

import math

from _surface import (
    AXIS_STAY, _CHORD_COS, _circle2, _cross, _dot, EPS, _fit, FLAT_SHARE,
    Frame, GATHER, _least_normal, _lift, _local_frames, _meet, _normal,
    NORMAL_DEG, NORMAL_SHARE, _Part, _pitches, PLANE_KEEP, PROFILE_GAP,
    PROFILE_MIN, PROFILE_SHARE, RANK, _res, SAG, SCREW_MIN, SECTOR_DEG,
    SPREAD_DEG, SUPPORT, _tidy, _unwind, WOUND_DEG,
)



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
