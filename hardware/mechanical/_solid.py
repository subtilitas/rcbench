"""Extruded solids with flat and cylindrical faces, placed in space.

A shape is a profile in a plane, with holes, pushed along the plane's
normal. A profile is a closed list of corners; a corner is (u, v) for a
straight side to the next corner or (u, v, (cu, cv, turn)) for an arc
about (cu, cv) to the next corner, turn +1 counter-clockwise and -1
clockwise. An arc spans less than 180 degrees.

The plane is a frame (origin, u, v, w) of the origin and three unit
vectors with w = u x v; the profile's coordinates run along u and v and
the shape reaches from d0 to d1 along w. ``place`` turns and moves
shapes by changing their frames only. ``items`` writes the shapes as
("brep", points, edges, faces, rgb) for
lib/rcbench/hardware/kicad/tools/step_gen.py's ``write``: arcs become
circle edges and cylinder faces, not polygons.
"""

import math

# The value a rod has in the place of a shape's holes.
PIPE = "pipe"

Z = ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
# Profile (y, z), pushed along x.
X = ((0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0))
# Profile (x, z), pushed along -y: d runs from -y1 to -y0.
Y = ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, -1.0, 0.0))


def rect(x0, y0, x1, y1):
    """A rectangle, counter-clockwise."""
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def rounded(x0, y0, x1, y1, r):
    """A rectangle with corners of radius r, counter-clockwise."""
    return [
        (x0 + r, y0), (x1 - r, y0, (x1 - r, y0 + r, 1)),
        (x1, y0 + r), (x1, y1 - r, (x1 - r, y1 - r, 1)),
        (x1 - r, y1), (x0 + r, y1, (x0 + r, y1 - r, 1)),
        (x0, y1 - r), (x0, y0 + r, (x0 + r, y0 + r, 1)),
    ]


def circle(cx, cy, r):
    """A circle of 4 quarter arcs, counter-clockwise."""
    return [(cx + r, cy, (cx, cy, 1)), (cx, cy + r, (cx, cy, 1)),
            (cx - r, cy, (cx, cy, 1)), (cx, cy - r, (cx, cy, 1))]


def stadium(x0, y0, x1, y1):
    """A rectangle whose two shorter sides are half circles."""
    if x1 - x0 >= y1 - y0:
        r, c = (y1 - y0) / 2, (y0 + y1) / 2
        return [(x0 + r, y0), (x1 - r, y0, (x1 - r, c, 1)),
                (x1, c, (x1 - r, c, 1)), (x1 - r, y1),
                (x0 + r, y1, (x0 + r, c, 1)), (x0, c, (x0 + r, c, 1))]
    r, c = (x1 - x0) / 2, (x0 + x1) / 2
    return [(x1, y0 + r), (x1, y1 - r, (c, y1 - r, 1)),
            (c, y1, (c, y1 - r, 1)), (x0, y1 - r),
            (x0, y0 + r, (c, y0 + r, 1)), (c, y0, (c, y0 + r, 1))]


def strip(path, t):
    """The outline of a band of thickness t along a polyline.

    Args:
        path: The corners of the band's centre line, (u, v).
        t: The band's thickness.

    Returns:
        A profile with mitred corners.
    """
    def offset(side):
        out = []
        for i, (x, y) in enumerate(path):
            dirs = []
            for a, b in ((i - 1, i), (i, i + 1)):
                if a >= 0 and b < len(path):
                    dx, dy = path[b][0] - path[a][0], path[b][1] - path[a][1]
                    n = math.hypot(dx, dy)
                    dirs.append((dx / n, dy / n))
            nx = sum(-d[1] for d in dirs) / len(dirs)
            ny = sum(d[0] for d in dirs) / len(dirs)
            n = math.hypot(nx, ny)
            nx, ny = nx / n, ny / n
            # Mitre: the offset along the bisector that keeps t / 2.
            k = (t / 2) / (nx * -dirs[0][1] + ny * dirs[0][0])
            out.append((x + side * k * nx, y + side * k * ny))
        return out
    return offset(-1) + offset(1)[::-1]


def turned(profile, cx, cy, angle):
    """A profile turned about (cx, cy), degrees counter-clockwise."""
    c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))

    def turn(u, v):
        return (cx + c * (u - cx) - s * (v - cy),
                cy + s * (u - cx) + c * (v - cy))

    return [turn(*q[:2]) if len(q) == 2
            else turn(*q[:2]) + ((*turn(*q[2][:2]), q[2][2]),)
            for q in profile]


def pipe(path, radius, rgb, frame=Z):
    """A shape: a round rod along a path in the frame's (u, v) plane.

    Args:
        path: The corners of the rod's centre line, open; a corner is
            (u, v) for a straight piece to the next corner or
            (u, v, (cu, cv, turn)) for an arc to it. Each piece starts
            in the direction the one before it ends in.
        radius: The rod's radius, less than every arc's.
        rgb: The colour.
        frame: The frame of the path's plane.
    """
    return (path, PIPE, radius, 0.0, rgb, frame)


def ext(outer, d0, d1, rgb, holes=(), frame=Z):
    """A shape: ``outer`` less ``holes`` from d0 to d1 along the frame."""
    return (outer, list(holes), d0, d1, rgb, frame)


def box(x0, y0, x1, y1, z0, z1, rgb):
    """A box with its sides along the axes."""
    return ext(rect(x0, y0, x1, y1), z0, z1, rgb)


def _turn(v, angle):
    c, s = math.cos(angle), math.sin(angle)
    return (c * v[0] - s * v[1], s * v[0] + c * v[1], v[2])


def place(shapes, x=0.0, y=0.0, z=0.0, turn=0.0, flip=False):
    """Turn shapes about z, turn them over, then move them.

    Args:
        shapes: The shapes.
        x: The move along x.
        y: The move along y.
        z: The move along z.
        turn: Degrees counter-clockwise about z, seen from +z.
        flip: Whether to turn the shapes over about the y axis after
            the turn: (x, y, z) becomes (-x, y, -z).

    Returns:
        The new shapes.
    """
    angle = math.radians(turn)

    def vec(v):
        v = _turn(v, angle)
        return (-v[0], v[1], -v[2]) if flip else v

    out = []
    for outer, holes, d0, d1, rgb, (o, u, v, w) in shapes:
        o = vec(o)
        out.append((outer, holes, d0, d1, rgb,
                    ((o[0] + x, o[1] + y, o[2] + z), vec(u), vec(v),
                     vec(w))))
    return out


def _segments(profile, want_ccw):
    """A profile as (start, end, arc) sides in the wanted direction."""
    n = len(profile)
    segs = [(profile[i][:2], profile[(i + 1) % n][:2],
             profile[i][2] if len(profile[i]) > 2 else None)
            for i in range(n)]
    area = sum(a[0] * b[1] - b[0] * a[1] for a, b, _arc in segs)
    if (area > 0) != want_ccw:
        segs = [(b, a, None if arc is None else (arc[0], arc[1], -arc[2]))
                for a, b, arc in reversed(segs)]
    return segs


def _brep(outer, holes, d0, d1, frame):
    """Points, edges and faces of one shape, as step_gen takes them."""
    o, u, v, w = frame

    def at(p, d):
        return tuple(o[k] + p[0] * u[k] + p[1] * v[k] + d * w[k]
                     for k in range(3))

    def scale(a, s):
        return tuple(s * c for c in a)

    points, edges, faces = [], [], []
    bottom_loops, top_loops = [], []
    loops = [_segments(outer, True)] + [_segments(h, False) for h in holes]
    for segs in loops:
        n, base, first = len(segs), len(points), len(edges)
        for a, _b, _arc in segs:
            points.append(at(a, d0))
        for a, _b, _arc in segs:
            points.append(at(a, d1))
        for level, d in ((0, d0), (1, d1)):
            for i, (_a, _b, arc) in enumerate(segs):
                curve = ("line",)
                if arc is not None:
                    radius = math.hypot(segs[i][0][0] - arc[0],
                                        segs[i][0][1] - arc[1])
                    curve = ("circle", at(arc[:2], d), scale(w, arc[2]),
                             radius)
                edges.append((base + level * n + i,
                              base + level * n + (i + 1) % n, curve))
        for i in range(n):
            edges.append((base + i, base + n + i, ("line",)))
        for i, (a, b, arc) in enumerate(segs):
            loop = [(first + i, True), (first + 2 * n + (i + 1) % n, True),
                    (first + n + i, False), (first + 2 * n + i, False)]
            if arc is None:
                dx, dy = b[0] - a[0], b[1] - a[1]
                length = math.hypot(dx, dy)
                normal = tuple((dy * u[k] - dx * v[k]) / length
                               for k in range(3))
                faces.append((("plane", at(a, d0), normal), True, [loop]))
            else:
                radius = math.hypot(a[0] - arc[0], a[1] - arc[1])
                faces.append((("cylinder", at(arc[:2], d0), w, radius),
                              arc[2] > 0, [loop]))
        bottom_loops.append([(first + i, False) for i in reversed(range(n))])
        top_loops.append([(first + n + i, True) for i in range(n)])
    start = loops[0][0][0]
    faces.append((("plane", at(start, d0), scale(w, -1.0)), True,
                  bottom_loops))
    faces.append((("plane", at(start, d1), w), True, top_loops))
    return points, edges, faces


def _pipe(path, radius, frame):
    """Points, edges and faces of a rod: cylinders and pieces of tori."""
    o, u, v, w = frame

    def at(p, d=0.0):
        return tuple(o[k] + p[0] * u[k] + p[1] * v[k] + d * w[k]
                     for k in range(3))

    def vec(a):
        return tuple(a[0] * u[k] + a[1] * v[k] for k in range(3))

    pieces = []
    for q, nxt in zip(path, path[1:], strict=False):
        a, b = q[:2], nxt[:2]
        arc = q[2] if len(q) > 2 else None
        if arc is None:
            length = math.hypot(b[0] - a[0], b[1] - a[1])
            t0 = t1 = ((b[0] - a[0]) / length, (b[1] - a[1]) / length)
        else:
            big = math.hypot(a[0] - arc[0], a[1] - arc[1])
            t0 = (-arc[2] * (a[1] - arc[1]) / big,
                  arc[2] * (a[0] - arc[0]) / big)
            t1 = (-arc[2] * (b[1] - arc[1]) / big,
                  arc[2] * (b[0] - arc[0]) / big)
        if pieces and math.hypot(t0[0] - pieces[-1][4][0],
                                 t0[1] - pieces[-1][4][1]) > 1e-9:
            raise ValueError("a rod's pieces must join without a corner")
        pieces.append((a, b, arc, t0, t1))
    nodes = [(piece[0], piece[3]) for piece in pieces]
    nodes.append((pieces[-1][1], pieces[-1][4]))
    points, edges, faces = [], [], []
    for p, t in nodes:
        for k in range(4):
            c, s = (1, 0, -1, 0)[k], (0, 1, 0, -1)[k]
            points.append(at((p[0] - radius * c * t[1],
                              p[1] + radius * c * t[0]), radius * s))
    for i, (p, t) in enumerate(nodes):
        for k in range(4):
            edges.append((4 * i + k, 4 * i + (k + 1) % 4,
                          ("circle", at(p), vec(t), radius)))
    first = len(edges)
    for i, (_a, _b, arc, _t0, _t1) in enumerate(pieces):
        for k in range(4):
            curve = ("line",)
            if arc is not None:
                c, s = (1, 0, -1, 0)[k], (0, 1, 0, -1)[k]
                big = math.hypot(_a[0] - arc[0], _a[1] - arc[1])
                curve = ("circle", at(arc[:2], radius * s),
                         tuple(arc[2] * x for x in w),
                         big - arc[2] * radius * c)
            edges.append((4 * i + k, 4 * (i + 1) + k, curve))
    for i, (a, _b, arc, t0, _t1) in enumerate(pieces):
        for k in range(4):
            loop = [(4 * i + k, True), (first + 4 * i + (k + 1) % 4, True),
                    (4 * (i + 1) + k, False), (first + 4 * i + k, False)]
            if arc is None:
                surface = ("cylinder", at(a), vec(t0), radius)
            else:
                surface = ("torus", at(arc[:2]), w,
                           math.hypot(a[0] - arc[0], a[1] - arc[1]), radius)
            faces.append((surface, True, [loop]))
    last = len(nodes) - 1
    faces.append((("plane", at(nodes[0][0]),
                   tuple(-x for x in vec(nodes[0][1]))), True,
                  [[(k, False) for k in reversed(range(4))]]))
    faces.append((("plane", at(nodes[last][0]), vec(nodes[last][1])), True,
                  [[(4 * last + k, True) for k in range(4)]]))
    return points, edges, faces


def items(shapes):
    """The shapes as step_gen.write takes them."""
    return [("brep", *(_pipe(outer, d0, frame) if holes == PIPE
                       else _brep(outer, holes, d0, d1, frame)), rgb)
            for outer, holes, d0, d1, rgb, frame in shapes]
