"""Write KiCad 10 footprints and symbols from module specs.

A spec is a Python module in hardware/kicad/modules/ that defines FOOTPRINT
and SYMBOL dictionaries. Footprint coordinates are millimetres in the
module's board frame: origin at the top-left corner of the component side,
x right, y down. ``origin`` moves the footprint origin to a point of that
frame, normally pad 1. Symbol coordinates are millimetres, y up, pin ends on
the 2.54mm grid.

Footprint items (``silk`` and ``fab`` lists)::

    ("rect", x0, y0, x1, y1)            outline
    ("rect", x0, y0, x1, y1, "fill")    filled
    ("line", x0, y0, x1, y1)
    ("circle", cx, cy, r)
    ("text", s, x, y, size)
    ("text", s, x, y, size, angle)      angle in degrees, counter-clockwise
    ("poly", [(x, y), ...])             closed polygon outline

``outline`` (optional) replaces the board rectangle by a closed polygon in
the board frame; ``value_at`` (optional) places the Value text; ``model``
(optional) is the path of the footprint's 3D model.

Pads::

    {"num": "1", "x": 2.0, "y": 2.0, "shape": "rect" | "circle" | "oval",
     "size": (w, h), "drill": d}          through hole
    {"num": "1", ..., "drill": None}       surface mount on F.Cu
    {"num": "", "x": .., "y": .., "shape": "circle", "size": (d, d),
     "drill": d, "type": "np"}              non-plated hole
    {"num": "1", ..., "drill": 1.0, "offset": (dx, dy)}  drill off centre

Symbol graphics::

    ("rect", x0, y0, x1, y1)  ("rect", ..., "outline")  filled with the line
    ("poly", [(x, y), ...])
    ("text", s, x, y, size)
    ("text", s, x, y, size, angle)

Symbol pins: (type, name, number, x, y, rotation), length 2.54mm.

File formats: footprint 20260206, symbol library 20251024 (KiCad 10).
"""

import uuid

FP_VERSION = "20260206"
SYM_VERSION = "20251024"


def _u():
    return str(uuid.uuid4())


def _num(v):
    """A coordinate as KiCad writes it: no trailing zeros."""
    s = f"{round(v, 4):.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def _q(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


class _Frame:
    def __init__(self, origin):
        self.ox, self.oy = origin

    def p(self, x, y):
        return f"{_num(x - self.ox)} {_num(y - self.oy)}"


def _stroke(w, ind):
    return (f"{ind}(stroke\n{ind}\t(width {_num(w)})\n{ind}\t(type solid)\n"
            f"{ind})\n")


def _fp_item(fr, item, layer, w, uid):
    kind = item[0]
    if kind == "rect":
        fill = "yes" if len(item) > 5 and item[5] == "fill" else "no"
        return (f"\t(fp_rect\n\t\t(start {fr.p(item[1], item[2])})\n"
                f"\t\t(end {fr.p(item[3], item[4])})\n{_stroke(w, chr(9) * 2)}"
                f"\t\t(fill {fill})\n\t\t(layer {_q(layer)})\n"
                f"\t\t(uuid {_q(uid())})\n\t)\n")
    if kind == "line":
        return (f"\t(fp_line\n\t\t(start {fr.p(item[1], item[2])})\n"
                f"\t\t(end {fr.p(item[3], item[4])})\n{_stroke(w, chr(9) * 2)}"
                f"\t\t(layer {_q(layer)})\n\t\t(uuid {_q(uid())})\n\t)\n")
    if kind == "circle":
        cx, cy, r = item[1], item[2], item[3]
        return (f"\t(fp_circle\n\t\t(center {fr.p(cx, cy)})\n"
                f"\t\t(end {fr.p(cx + r, cy)})\n{_stroke(w, chr(9) * 2)}"
                f"\t\t(fill no)\n\t\t(layer {_q(layer)})\n"
                f"\t\t(uuid {_q(uid())})\n\t)\n")
    if kind == "poly":
        pts = " ".join(f"(xy {fr.p(x, y)})" for x, y in item[1])
        return (f"\t(fp_poly\n\t\t(pts {pts})\n{_stroke(w, chr(9) * 2)}"
                f"\t\t(fill no)\n\t\t(layer {_q(layer)})\n"
                f"\t\t(uuid {_q(uid())})\n\t)\n")
    if kind == "text":
        s, x, y, size = item[1], item[2], item[3], item[4]
        ang = _num(item[5]) if len(item) > 5 else "0"
        th = max(0.08, round(size * 0.15, 2))
        return (f"\t(fp_text user {_q(s)}\n\t\t(at {fr.p(x, y)} {ang})\n"
                f"\t\t(unlocked yes)\n\t\t(layer {_q(layer)})\n"
                f"\t\t(uuid {_q(uid())})\n\t\t(effects\n\t\t\t(font\n"
                f"\t\t\t\t(size {_num(size)} {_num(size)})\n"
                f"\t\t\t\t(thickness {_num(th)})\n\t\t\t)\n\t\t)\n\t)\n")
    raise ValueError(f"unknown footprint item {kind}")


def _fp_prop(name, value, at, layer, uid, hide=False):
    h = "\t\t(hide yes)\n" if hide else ""
    return (f"\t(property {_q(name)} {_q(value)}\n\t\t(at {at} 0)\n"
            f"\t\t(unlocked yes)\n\t\t(layer {_q(layer)})\n{h}"
            f"\t\t(uuid {_q(uid())})\n\t\t(effects\n\t\t\t(font\n"
            f"\t\t\t\t(size 1 1)\n\t\t\t\t(thickness 0.15)\n"
            f"\t\t\t)\n\t\t)\n\t)\n")


def _pad(fr, pd, uid):
    w, h = pd["size"]
    if pd.get("drill") is None:
        kind, layers, drill = "smd", '"F.Cu" "F.Mask" "F.Paste"', ""
    else:
        np = pd.get("type") == "np"
        kind = "np_thru_hole" if np else "thru_hole"
        layers = '"*.Mask"' if np else '"*.Cu" "*.Mask"'
        off = ""
        if pd.get("offset"):
            off = f" (offset {_num(pd['offset'][0])} {_num(pd['offset'][1])})"
        drill = f"\t\t(drill {_num(pd['drill'])}{off})\n"
    rem = "\t\t(remove_unused_layers no)\n" if kind == "thru_hole" else ""
    return (f"\t(pad {_q(pd['num'])} {kind} {pd['shape']}\n"
            f"\t\t(at {fr.p(pd['x'], pd['y'])})\n"
            f"\t\t(size {_num(w)} {_num(h)})\n{drill}\t\t(layers {layers})\n"
            f"{rem}\t\t(uuid {_q(uid())})\n\t)\n")


def _extent(fp):
    """Bounding box of the board, the drawings and the pads."""
    xs, ys = [0.0, fp["board"][0]], [0.0, fp["board"][1]]
    for x, y in fp.get("outline", []):
        xs.append(x)
        ys.append(y)
    for item in fp.get("silk", []) + fp.get("fab", []):
        if item[0] == "poly":
            xs += [x for x, _ in item[1]]
            ys += [y for _, y in item[1]]
        elif item[0] in ("rect", "line"):
            xs += [item[1], item[3]]
            ys += [item[2], item[4]]
        elif item[0] == "circle":
            xs += [item[1] - item[3], item[1] + item[3]]
            ys += [item[2] - item[3], item[2] + item[3]]
    for pd in fp["pads"]:
        xs += [pd["x"] - pd["size"][0] / 2, pd["x"] + pd["size"][0] / 2]
        ys += [pd["y"] - pd["size"][1] / 2, pd["y"] + pd["size"][1] / 2]
    return min(xs), min(ys), max(xs), max(ys)


def footprint(fp, uid=_u):
    """The .kicad_mod text of footprint spec ``fp``."""
    fr = _Frame(fp["origin"])
    bw, bh = fp["board"]
    tht = any(pd.get("drill") is not None and pd.get("type") != "np"
              for pd in fp["pads"])
    s = (f"(footprint {_q(fp['name'])}\n\t(version {FP_VERSION})\n"
         f"\t(generator \"pcbnew\")\n\t(generator_version \"10.0\")\n"
         f"\t(layer \"F.Cu\")\n")
    ex = _extent(fp)
    s += _fp_prop("Reference", "REF**", fr.p(bw / 2, ex[1] - 1.5), "F.SilkS",
                  uid)
    vx, vy = fp.get("value_at", (bw / 2, ex[3] + 1.5))
    s += _fp_prop("Value", fp["name"], fr.p(vx, vy), "F.Fab", uid)
    s += _fp_prop("Datasheet", fp.get("datasheet", ""), "0 0", "F.Fab", uid,
                  hide=True)
    s += _fp_prop("Description", fp["desc"], "0 0", "F.Fab", uid, hide=True)
    s += f"\t(attr {'through_hole' if tht else 'smd'})\n"
    s += "\t(duplicate_pad_numbers_are_jumpers no)\n"
    board = (("poly", fp["outline"]) if fp.get("outline")
             else ("rect", 0, 0, bw, bh))
    for layer, w, items in (("F.SilkS", 0.12, [board] + fp.get("silk", [])),
                            ("F.Fab", 0.1, [board] + fp.get("fab", []))):
        for item in items:
            s += _fp_item(fr, item, layer, w, uid)
    x0, y0, x1, y1 = fp.get("courtyard") or _extent(fp)
    c = 0.25
    s += _fp_item(fr, ("rect", x0 - c, y0 - c, x1 + c, y1 + c), "F.CrtYd",
                  0.05, uid)
    s += _fp_item(fr, ("text", "${REFERENCE}", bw / 2, bh / 2, 1.0), "F.Fab",
                  0.1, uid)
    for pd in fp["pads"]:
        s += _pad(fr, pd, uid)
    s += "\t(embedded_fonts no)\n"
    if fp.get("model"):
        s += (f"\t(model {_q(fp['model'])}\n"
              "\t\t(offset\n\t\t\t(xyz 0 0 0)\n\t\t)\n"
              "\t\t(scale\n\t\t\t(xyz 1 1 1)\n\t\t)\n"
              "\t\t(rotate\n\t\t\t(xyz 0 0 0)\n\t\t)\n\t)\n")
    return s + ")\n"


def _eff(size, ind):
    return (f"{ind}(effects\n{ind}\t(font\n{ind}\t\t(size {_num(size)} "
            f"{_num(size)})\n{ind}\t)\n{ind})")


def _sym_prop(name, value, x, y, hide=False):
    h = "\t\t\t(hide yes)\n" if hide else ""
    return (f"\t\t(property {_q(name)} {_q(value)}\n"
            f"\t\t\t(at {_num(x)} {_num(y)} 0)\n\t\t\t(show_name no)\n"
            f"\t\t\t(do_not_autoplace no)\n{h}{_eff(1.27, chr(9) * 3)}\n"
            f"\t\t)\n")


def _sym_item(item):
    i = "\t\t\t"
    kind = item[0]
    if kind == "rect":
        fill = item[5] if len(item) > 5 else "none"
        return (f"{i}(rectangle\n{i}\t(start {_num(item[1])} {_num(item[2])})"
                f"\n{i}\t(end {_num(item[3])} {_num(item[4])})\n"
                f"{_stroke(0.1524, i + chr(9))}{i}\t(fill\n"
                f"{i}\t\t(type {fill})"
                f"\n{i}\t)\n{i})\n")
    if kind == "poly":
        xy = " ".join(f"(xy {_num(x)} {_num(y)})" for x, y in item[1])
        return (f"{i}(polyline\n{i}\t(pts {xy})\n"
                f"{_stroke(0.1524, i + chr(9))}{i}\t(fill\n{i}\t\t(type none)"
                f"\n{i}\t)\n{i})\n")
    if kind == "text":
        ang = _num(item[5] * 10) if len(item) > 5 else "0"
        return (f"{i}(text {_q(item[1])}\n{i}\t(at {_num(item[2])} "
                f"{_num(item[3])} {ang})\n{_eff(item[4], i + chr(9))}\n{i})\n")
    raise ValueError(f"unknown symbol item {kind}")


def _pin(kind, name, num, x, y, rot):
    i = "\t\t\t"
    return (f"{i}(pin {kind} line\n{i}\t(at {_num(x)} {_num(y)} {rot})\n"
            f"{i}\t(length 2.54)\n{i}\t(name {_q(name)}\n"
            f"{_eff(1.27, i + chr(9) * 2)}\n{i}\t)\n{i}\t(number {_q(num)}\n"
            f"{_eff(1.27, i + chr(9) * 2)}\n{i}\t)\n{i})\n")


def symbol(sym, lib):
    """Write one (symbol ...) block.

    Args:
        sym: The SYMBOL dict.
        lib: The footprint library nickname for the Footprint field.

    Returns:
        The block's text.
    """
    name = sym["name"]
    left, top, right, bottom = sym["body"]
    s = (f"\t(symbol {_q(name)}\n\t\t(exclude_from_sim no)\n"
         f"\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(in_pos_files yes)\n"
         f"\t\t(duplicate_pin_numbers_are_jumpers no)\n")
    # Reference above and Value below the body and every pin.
    hi = max([top] + [p[4] for p in sym["pins"]])
    lo = min([bottom] + [p[4] for p in sym["pins"]])
    s += _sym_prop("Reference", sym.get("ref", "M"), 0, hi + 2.54)
    s += _sym_prop("Value", name, 0, lo - 2.54)
    s += _sym_prop("Footprint", f"{lib}:{sym['footprint']}", 0,
                   lo - 5.08, hide=True)
    s += _sym_prop("Datasheet", sym.get("datasheet", ""), 0, 0, hide=True)
    s += _sym_prop("Description", sym["desc"], 0, 0, hide=True)
    s += f"\t\t(symbol {_q(name + '_0_1')}\n"
    s += _sym_item(("rect", left, top, right, bottom, "background"))
    for item in sym.get("graphics", []):
        s += _sym_item(item)
    s += "\t\t)\n"
    s += f"\t\t(symbol {_q(name + '_1_1')}\n"
    for p in sym["pins"]:
        s += _pin(*p)
    s += "\t\t)\n\t\t(embedded_fonts no)\n\t)\n"
    return s


def symbol_library(symbols, lib):
    """The .kicad_sym text holding every symbol spec in ``symbols``."""
    s = (f"(kicad_symbol_lib\n\t(version {SYM_VERSION})\n"
         f"\t(generator \"kicad_symbol_editor\")\n"
         f"\t(generator_version \"10.0\")\n")
    for sym in symbols:
        s += symbol(sym, lib)
    return s + ")\n"
