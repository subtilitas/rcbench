#!/usr/bin/env python3
"""Draw a module spec as PNG: footprint (silkscreen, fab, pads) and symbol.

With --photo, the footprint is drawn over a photo of the board's component
side, for checking positions against it. The photo stays outside the
repository; only the spec is committed.

Usage:
    render.py <spec.py> <out.png>
    render.py <spec.py> <out.png> --photo <img> --at X0 Y0 --scale SX SY

--at is the pixel of the board frame's (0, 0); --scale is px per mm along x
and y. Needs Pillow.
"""

import argparse
import importlib.util

from PIL import Image, ImageDraw


def load(path):
    """Import a spec file.

    Args:
        path: The spec's file path.

    Returns:
        The imported module.
    """
    spec = importlib.util.spec_from_file_location("spec", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def draw_footprint(d, fp, px, sx, sy, colours):
    """Draw a footprint spec's outline, items and pads.

    Args:
        d: The Pillow drawing context.
        fp: The FOOTPRINT dict.
        px: Function mapping board millimetres to pixels.
        sx: Pixels per millimetre along x.
        sy: Pixels per millimetre along y.
        colours: Colours for "fab", "silk", "pad", "drill" and "num".
    """
    board = (("poly", fp["outline"]) if fp.get("outline")
             else ("rect", 0, 0, fp["board"][0], fp["board"][1]))
    for layer, items in (("fab", [board] + fp.get("fab", [])),
                         ("silk", fp.get("silk", []))):
        col = colours[layer]
        for it in items:
            if it[0] == "rect":
                (x0, y0), (x1, y1) = px(it[1], it[2]), px(it[3], it[4])
                box = [min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)]
                if len(it) > 5 and it[5] == "fill":
                    d.rectangle(box, fill=col)
                else:
                    d.rectangle(box, outline=col, width=2)
            elif it[0] == "line":
                d.line([px(it[1], it[2]), px(it[3], it[4])], fill=col, width=2)
            elif it[0] == "poly":
                pts = [px(x, y) for x, y in it[1]]
                d.line(pts + pts[:1], fill=col, width=2)
            elif it[0] == "circle":
                cx, cy = px(it[1], it[2])
                d.ellipse([cx - it[3] * sx, cy - it[3] * sy,
                           cx + it[3] * sx, cy + it[3] * sy], outline=col,
                          width=2)
            elif it[0] == "text":
                cx, cy = px(it[2], it[3])
                d.text((cx - d.textlength(it[1]) / 2, cy - 6), it[1], fill=col)
    for pd in fp["pads"]:
        cx, cy = px(pd["x"], pd["y"])
        w, h = pd["size"][0] / 2 * sx, pd["size"][1] / 2 * sy
        box = [cx - w, cy - h, cx + w, cy + h]
        if pd["shape"] == "rect":
            d.rectangle(box, outline=colours["pad"], width=2)
        else:
            d.ellipse(box, outline=colours["pad"], width=2)
        if pd.get("drill"):
            r = pd["drill"] / 2
            d.ellipse([cx - r * sx, cy - r * sy, cx + r * sx, cy + r * sy],
                      outline=colours["drill"], width=2)
        d.text((cx + w + 2, cy - 6), pd["num"], fill=colours["num"])


def draw_symbol(d, sym, ox, oy, s):
    """Draw a symbol spec's body, graphics and pins.

    Args:
        d: The Pillow drawing context.
        sym: The SYMBOL dict.
        ox: Pixel x of the symbol origin.
        oy: Pixel y of the symbol origin.
        s: Pixels per millimetre.
    """
    def px(x, y):
        return ox + x * s, oy - y * s
    left, top, right, bottom = sym["body"]
    d.rectangle([px(left, top), px(right, bottom)], outline=(120, 0, 0),
                fill=(255, 255, 210), width=2)
    for it in sym.get("graphics", []):
        if it[0] == "rect":
            (x0, y0), (x1, y1) = px(it[1], it[2]), px(it[3], it[4])
            d.rectangle([min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)],
                        outline=(120, 0, 0), width=2,
                        fill=(120, 0, 0) if len(it) > 5 else None)
        elif it[0] == "poly":
            d.line([px(x, y) for x, y in it[1]], fill=(120, 0, 0), width=2)
        elif it[0] == "text":
            cx, cy = px(it[2], it[3])
            d.text((cx - d.textlength(it[1]) / 2, cy - 6), it[1],
                   fill=(0, 0, 160))
    for _kind, name, num, x, y, rot in sym["pins"]:
        dx = {0: 2.54, 180: -2.54, 90: 0, 270: 0}[rot]
        dy = {90: 2.54, 270: -2.54, 0: 0, 180: 0}[rot]
        d.line([px(x, y), px(x + dx, y + dy)], fill=(160, 0, 0), width=3)
        tx, ty = px(x + dx, y + dy)
        if rot == 0:
            d.text((tx + 4, ty - 6), name, fill=(0, 100, 0))
        elif rot == 180:
            d.text((tx - 4 - d.textlength(name), ty - 6), name,
                   fill=(0, 100, 0))
        else:
            dy_text = 4 if rot == 90 else -16
            d.text((tx - d.textlength(name) / 2, ty + dy_text), name,
                   fill=(0, 100, 0))
        d.text((px(x, y)[0], px(x, y)[1] - 14), num, fill=(90, 90, 90))


def main():
    """Render the spec named on the command line."""
    ap = argparse.ArgumentParser()
    ap.add_argument("spec")
    ap.add_argument("out")
    ap.add_argument("--photo")
    ap.add_argument("--at", nargs=2, type=float)
    ap.add_argument("--scale", nargs=2, type=float)
    a = ap.parse_args()
    mod = load(a.spec)
    fp, sym = mod.FOOTPRINT, mod.SYMBOL
    if a.photo:
        img = Image.open(a.photo).convert("RGB")
        (x0, y0), (sx, sy) = a.at, a.scale
        d = ImageDraw.Draw(img)
        draw_footprint(d, fp, lambda x, y: (x0 + x * sx, y0 + y * sy), sx, sy,
                       {"fab": (255, 200, 0), "silk": (0, 255, 0),
                        "pad": (255, 0, 0), "drill": (0, 200, 255),
                        "num": (255, 255, 0)})
        img.save(a.out)
        return
    s = 24
    bw, bh = fp["board"]
    left, top, right, bottom = sym["body"]
    sym_w = (right - left + 16) * s
    w = int(max((bw + 12) * s, 400) + sym_w)
    h = int(max((bh + 12) * s, (top - bottom + 16) * s))
    img = Image.new("RGB", (w, h), "white")
    d = ImageDraw.Draw(img)
    draw_footprint(d, fp, lambda x, y: ((x + 6) * s, (y + 6) * s), s, s,
                   {"fab": (150, 150, 150), "silk": (0, 140, 0),
                    "pad": (200, 0, 0), "drill": (0, 120, 255),
                    "num": (0, 0, 0)})
    draw_symbol(d, sym, (bw + 12) * s + (8 - left) * s, h / 2, s)
    img.save(a.out)


if __name__ == "__main__":
    main()
