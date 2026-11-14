"""Read a binary STL mesh and split it into its parts.

A part is a set of triangles connected through shared edges. Each part
must be a closed 2-manifold: every directed edge has exactly one reverse.
No dependency outside the standard library.
"""

import struct


def read(path):
    """Read a binary STL file.

    Args:
        path: The file.

    Returns:
        (points, triangles): points as (x, y, z) tuples, equal vertices
        merged; triangles as index triples, counter-clockwise from outside.

    Raises:
        ValueError: When the file's length does not fit its triangle count.
    """
    raw = path.read_bytes()
    (count,) = struct.unpack_from("<I", raw, 80)
    if len(raw) != 84 + 50 * count:
        raise ValueError(f"{path}: not a binary STL of {count} triangles")
    index, points, tris = {}, [], []
    for i in range(count):
        v = struct.unpack_from("<9f", raw, 84 + 50 * i + 12)
        tri = []
        for k in (0, 3, 6):
            p = v[k:k + 3]
            if p not in index:
                index[p] = len(points)
                points.append(p)
            tri.append(index[p])
        tris.append(tuple(tri))
    return points, tris


def _find(parent, a):
    while parent[a] != a:
        parent[a] = parent[parent[a]]
        a = parent[a]
    return a


def _half_edges(tris, ids):
    """Directed edge to triangle for the triangles ``ids``.

    Raises:
        ValueError: For a directed edge used by two triangles.
    """
    owner = {}
    for t in ids:
        a, b, c = tris[t]
        for e in ((a, b), (b, c), (c, a)):
            if e in owner:
                raise ValueError(f"edge {e} is used twice in one direction")
            owner[e] = t
    return owner


def parts(points, tris):
    """Split a mesh into its edge-connected parts.

    Args:
        points: The mesh's points.
        tris: Its triangles.

    Returns:
        A list of parts, each a sorted list of triangle indices, ordered
        by each part's lowest (x, y, z) corner of its bounding box.

    Raises:
        ValueError: When an edge has no reverse, so a part is not closed.
    """
    owner = _half_edges(tris, range(len(tris)))
    parent = list(range(len(tris)))
    for (a, b), t in owner.items():
        if (b, a) not in owner:
            raise ValueError(f"edge {(a, b)} is open")
        parent[_find(parent, owner[(b, a)])] = _find(parent, t)
    groups = {}
    for t in range(len(tris)):
        groups.setdefault(_find(parent, t), []).append(t)

    def corner(ids):
        return tuple(min(points[v][k] for t in ids for v in tris[t])
                     for k in range(3))

    return sorted(groups.values(), key=corner)


def bounds(points, tris, ids):
    """The bounding box of a part.

    Args:
        points: The mesh's points.
        tris: Its triangles.
        ids: The part's triangle indices.

    Returns:
        ((x0, y0, z0), (x1, y1, z1)).
    """
    vs = [points[v] for t in ids for v in tris[t]]
    return (tuple(min(p[k] for p in vs) for k in range(3)),
            tuple(max(p[k] for p in vs) for k in range(3)))
