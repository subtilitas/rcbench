#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""Measure a board from a photo taken on squared paper.

The paper's grid, PITCH apart, gives the scale. ``lattice`` finds the
crossings of the grid lines, numbers them from one crossing outward and
fits the plane projection from photo pixels to mm on the paper.
``camera`` gives the place of the camera over the paper from that
projection and the lens' focal length. A thing h over the paper looks
larger than it is by height / (height - h) about the point under the
camera, which is how a size read off the paper becomes a true size.

``flat`` lays a photo of the INA228 module onto the board frame of
hardware/kicad/modules/ina228_bifrc_xt90.py (mm, y down, seen from the
top side) by the 4 screw holes, whose pixels PHOTOS lists.

The owner's three photos are not in the repository; the tool takes the
directory that holds them. Two of them were taken at 64mm focal length
(35mm equivalent), which their metadata gave; the top photo is a
cut-out with no known focal length.

    ina228_underside.jpg   the board's underside, the board lying on its
                           two connectors. Camera 196mm over the paper.
                           The board's underside is 11.65mm over the
                           paper: the connectors' 9.9 to 10.2mm and the
                           board's 1.6mm. That gives the board's size
                           and the screw holes' spacing: 85.15 x 51.70
                           and 74.70 x 41.35 on the paper, divided by
                           196.2 / 184.55.
    ina228_top.jpg         the top side. The header's pins, 8.6mm long,
                           lean 0.046mm per mm from the point (54, 24.7)
                           of the board: the camera is 196mm over the
                           board there.
    ina228_case.jpg        the empty case. Camera 195mm over the paper.

Usage:
    python3 hardware/mechanical/_photo_grid.py PHOTOS DIR

Reads the photos from PHOTOS. Writes DIR/ina228_top.png and
DIR/ina228_underside.png, both in the board frame at SCALE pixels per
mm with MARGIN mm round the board, and DIR/ina228_case.png on the
paper's frame, and prints each fit. Needs
numpy and OpenCV (pip install opencv-python-headless).
"""

import sys
from collections import deque
from pathlib import Path

import cv2
import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "kicad" / "modules"))
import ina228_bifrc_xt90 as kicad  # noqa: E402

PITCH = 5.0             # mm between grid lines
SCALE = 40              # pixels per mm of a flattened photo
MARGIN = 12.0           # mm round the board in a flattened photo
FRAME = 34.6            # mm, long side of the 35mm frame at 4:3
# Photo: file, grid pitch in pixels (a first guess), focal length in mm
# (35mm equivalent, None when unknown), and the pixels of the screw
# holes at the board's (x, y) low-low, high-low, low-high, high-high.
PHOTOS = {
    "top": ("ina228_top.jpg", 95, None, (
        (1656.3, 997.7), (330.3, 1013.5), (1655.0, 268.0), (319.4, 274.2))),
    "underside": ("ina228_underside.jpg", 192, 64.0, (
        (2226.8, 3592.9), (2326.0, 692.0), (631.3, 3519.7),
        (746.4, 660.2))),
    "case": ("ina228_case.jpg", 195, 64.0, None),
}


def crossings(image, pitch):
    """Find where the paper's grid lines cross.

    Args:
        image: The photo as OpenCV reads it.
        pitch: The grid's pitch in pixels, to within a fifth.

    Returns:
        An array of (x, y) pixels.
    """
    grey = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    hsv = cv2.cvtColor(image, cv2.COLOR_BGR2HSV)
    paper = (hsv[..., 1] < 60) & (hsv[..., 2] > 110)
    size = min(int(pitch * 0.35) | 1, 255)
    ground = cv2.medianBlur(grey, size).astype(int)
    lines = ((ground - grey > 7) & paper).astype(np.uint8)
    lines = cv2.dilate(lines, np.ones((3, 3), np.uint8))
    run = int(pitch * 0.45)
    along = cv2.morphologyEx(lines, cv2.MORPH_OPEN,
                             np.ones((1, run), np.uint8))
    across = cv2.morphologyEx(lines, cv2.MORPH_OPEN,
                              np.ones((run, 1), np.uint8))
    wide = np.ones((5, 5), np.uint8)
    both = cv2.dilate(along, wide) & cv2.dilate(across, wide)
    count, _labels, stats, centres = cv2.connectedComponentsWithStats(both)
    return np.array([centres[k] for k in range(1, count)
                     if 9 <= stats[k, 4] <= (pitch * 0.3) ** 2])


def lattice(image, pitch):
    """Fit the projection from photo pixels to mm on the paper.

    Args:
        image: The photo as OpenCV reads it.
        pitch: The grid's pitch in pixels, to within a fifth.

    Returns:
        (matrix, count, rms): the 3 x 3 matrix, the number of crossings
        it rests on and their root mean square distance in mm from the
        grid.

    Raises:
        ValueError: When no crossing has 4 neighbours a pitch away.
    """
    points = crossings(image, pitch)

    def nearest(place):
        gaps = np.linalg.norm(points - place, axis=1)
        k = int(gaps.argmin())
        return float(gaps[k]), k

    steps = ((pitch, 0.0), (-pitch, 0.0), (0.0, pitch), (0.0, -pitch))
    start = np.array([image.shape[1] * 0.15, image.shape[0] * 0.5])
    seed = None
    for k in np.argsort(np.linalg.norm(points - start, axis=1)):
        if all(nearest(points[k] + step)[0] < 0.15 * pitch
               for step in steps):
            seed = int(k)
            break
    if seed is None:
        raise ValueError("no grid found")
    number = {seed: (0, 0)}
    basis = {seed: (np.array(steps[0]), np.array(steps[2]))}
    queue = deque([seed])
    while queue:
        a = queue.popleft()
        col, row = number[a]
        for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            step = basis[a][0] * dc + basis[a][1] * dr
            gap, b = nearest(points[a] + step)
            if gap >= 0.22 * pitch or b in number:
                continue
            number[b] = (col + dc, row + dr)
            seen = (points[b] - points[a]) * (dc + dr)
            new = list(basis[a])
            new[0 if dc else 1] = 0.5 * new[0 if dc else 1] + 0.5 * seen
            basis[b] = tuple(new)
            queue.append(b)
    pixels = np.float32([points[k] for k in number])
    places = np.float32([[PITCH * c, PITCH * r] for c, r in number.values()])
    matrix, used = cv2.findHomography(pixels, places, cv2.RANSAC, 0.4)
    got = cv2.perspectiveTransform(pixels[None], matrix)[0]
    gaps = np.linalg.norm(got - places, axis=1)[used.ravel() > 0]
    return matrix, int(used.sum()), float(np.sqrt((gaps ** 2).mean()))


def camera(matrix, size, focal):
    """The camera's place over the paper.

    Args:
        matrix: The projection from photo pixels to mm on the paper.
        size: The photo's (width, height) in pixels.
        focal: The focal length in mm, 35mm equivalent.

    Returns:
        (x, y, height) in mm: the point of the paper under the camera
        and the camera's height over it.
    """
    width, height = size
    f = focal / FRAME * max(size)
    lens = np.array([[f, 0.0, width / 2], [0.0, f, height / 2],
                     [0.0, 0.0, 1.0]])
    pose = np.linalg.inv(lens) @ np.linalg.inv(matrix)
    pose /= np.mean([np.linalg.norm(pose[:, 0]), np.linalg.norm(pose[:, 1])])
    if pose[2, 2] < 0:
        pose = -pose
    turn = np.column_stack([pose[:, 0], pose[:, 1],
                            np.cross(pose[:, 0], pose[:, 1])])
    x, y, z = -turn.T @ pose[:, 2]
    return float(x), float(y), float(abs(z))


def read(name, photos):
    """Read one of PHOTOS and fit its grid.

    Args:
        name: A key of PHOTOS.
        photos: The directory that holds the photos.

    Returns:
        (image, matrix, count, rms) as ``lattice`` gives them.

    Raises:
        FileNotFoundError: When the photo is not in the directory.
    """
    file, pitch, _focal, _holes = PHOTOS[name]
    image = cv2.imread(str(Path(photos) / file))
    if image is None:
        raise FileNotFoundError(Path(photos) / file)
    return (image, *lattice(image, pitch))


def flat(name, photos):
    """Lay a photo of the board onto the board frame.

    Args:
        name: "top" or "underside".
        photos: The directory that holds the photos.

    Returns:
        (picture, matrix): the photo at SCALE pixels per mm with MARGIN
        round the board, x to the right and y down as seen from the top
        side, and the matrix from photo pixels to board mm.
    """
    image, paper, _count, _rms = read(name, photos)
    seen = cv2.perspectiveTransform(
        np.float32([PHOTOS[name][3]]), paper)[0]
    (x0, y0), (x1, _), (_, y1), _ = kicad.HOLES
    board = np.float32([(x0, y0), (x1, y0), (x0, y1), (x1, y1)])
    matrix = cv2.getPerspectiveTransform(seen, board) @ paper
    draw = np.array([[SCALE, 0.0, MARGIN * SCALE],
                     [0.0, SCALE, MARGIN * SCALE], [0.0, 0.0, 1.0]])
    size = (int((kicad.BW + 2 * MARGIN) * SCALE),
            int((kicad.BH + 2 * MARGIN) * SCALE))
    return cv2.warpPerspective(image, draw @ matrix, size,
                               flags=cv2.INTER_CUBIC), matrix


def on_paper(name, photos, scale=20):
    """Lay a photo onto its paper.

    Args:
        name: A key of PHOTOS.
        photos: The directory that holds the photos.
        scale: Pixels per mm.

    Returns:
        (picture, origin): the photo with the grid square, and the
        paper's (x, y) in mm at the picture's first pixel.
    """
    image, paper, _count, _rms = read(name, photos)
    height, width = image.shape[:2]
    corners = cv2.perspectiveTransform(np.float32(
        [[[0, 0], [width, 0], [width, height], [0, height]]]), paper)[0]
    low, high = np.floor(corners.min(0)), np.ceil(corners.max(0))
    draw = np.array([[scale, 0.0, -scale * low[0]],
                     [0.0, scale, -scale * low[1]], [0.0, 0.0, 1.0]])
    size = (int((high[0] - low[0]) * scale), int((high[1] - low[1]) * scale))
    return cv2.warpPerspective(image, draw @ paper, size,
                               flags=cv2.INTER_AREA), low


def main(argv):
    """Run from the command line.

    Args:
        argv: The directory of the photos and the one to write to.

    Returns:
        0, or 2 for a wrong command line.
    """
    if len(argv) != 2:
        print(__doc__)
        return 2
    photos, out = argv[0], Path(argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for name, (file, _pitch, focal, holes) in PHOTOS.items():
        image, paper, count, rms = read(name, photos)
        line = f"{file}: {count} crossings within {rms:.2f}mm"
        if focal:
            x, y, height = camera(paper, image.shape[1::-1], focal)
            line += f", camera {height:.1f}mm over ({x:.1f}, {y:.1f})"
        print(line)
        picture = (flat(name, photos) if holes
                   else on_paper(name, photos))[0]
        cv2.imwrite(str(out / f"ina228_{name}.png"), picture)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
