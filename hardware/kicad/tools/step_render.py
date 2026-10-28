#!/usr/bin/env python3
"""Render every STEP model of the library to PNG with OpenCASCADE.

Each model is read back from rcbench_modules.3dshapes/ as KiCad reads it,
with its colours, and drawn shaded with face edges from above the front
left, on a white background. The PNGs go to DIR, one per footprint, plus
a contact sheet ``all.png`` of all of them.

Usage:
    xvfb-run -a python3 hardware/kicad/tools/step_render.py DIR [--size W H]

Needs OCP (pip install cadquery-ocp), Pillow for the contact sheet, and
an OpenGL context: an X display, or xvfb-run with Mesa.
"""

import argparse
import sys
from pathlib import Path

from OCP.AIS import AIS_InteractiveContext, AIS_Shape
from OCP.Aspect import (Aspect_DisplayConnection, Aspect_GradientFillMethod,
                         Aspect_TOTP_LEFT_LOWER, Aspect_TypeOfLine)
from OCP.Graphic3d import (Graphic3d_BufferType, Graphic3d_MaterialAspect,
                           Graphic3d_NameOfMaterial,
                           Graphic3d_RenderingMode)
from OCP.Image import Image_AlienPixMap
from OCP.OpenGl import OpenGl_GraphicDriver
from OCP.Prs3d import Prs3d_LineAspect
from OCP.Quantity import Quantity_Color, Quantity_TOC_RGB
from OCP.TCollection import TCollection_AsciiString
from OCP.V3d import V3d_TypeOfOrientation, V3d_Viewer
from OCP.Xw import Xw_Window

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gen_all  # noqa: E402
import step_check  # noqa: E402


def _rgb(r, g, b):
    return Quantity_Color(r, g, b, Quantity_TOC_RGB)


class Renderer:
    """One offscreen OpenGL view, reused for every model."""

    def __init__(self, w, h):
        """Open the view.

        Args:
            w: Image width in pixels.
            h: Image height in pixels.
        """
        self.w, self.h = w, h
        disp = Aspect_DisplayConnection()
        driver = OpenGl_GraphicDriver(disp, True)
        driver.ChangeOptions().swapInterval = 0
        self.viewer = V3d_Viewer(driver)
        self.viewer.SetDefaultLights()
        self.viewer.SetLightOn()
        self.view = self.viewer.CreateView()
        win = Xw_Window(disp, "step_render", 0, 0, w, h)
        win.SetVirtual(True)
        self.view.SetWindow(win)
        self.view.SetBgGradientColors(_rgb(1.0, 1.0, 1.0),
                                      _rgb(0.86, 0.88, 0.91),
                                      Aspect_GradientFillMethod.Aspect_GradientFillMethod_Vertical)
        params = self.view.ChangeRenderingParams()
        params.Method = Graphic3d_RenderingMode.Graphic3d_RM_RASTERIZATION
        params.NbMsaaSamples = 8
        params.IsAntialiasingEnabled = True
        self.view.TriedronDisplay(Aspect_TOTP_LEFT_LOWER, _rgb(0.3, 0.3, 0.3),
                                  0.08)
        self.ctx = AIS_InteractiveContext(self.viewer)

    def render(self, path, out):
        """Draw one STEP file to a PNG.

        Args:
            path: The STEP file.
            out: The PNG path.

        Raises:
            RuntimeError: When the image cannot be written.
        """
        self.ctx.RemoveAll(False)
        _shape, found = step_check.read(path)
        edge = Prs3d_LineAspect(_rgb(0.15, 0.15, 0.15),
                                Aspect_TypeOfLine.Aspect_TOL_SOLID, 1.0)
        for solid, col in found:
            ais = AIS_Shape(solid)
            ais.SetColor(_rgb(*(col or (1.0, 0.0, 1.0))))
            ais.SetMaterial(Graphic3d_MaterialAspect(
                Graphic3d_NameOfMaterial.Graphic3d_NOM_PLASTIC))
            drawer = ais.Attributes()
            drawer.SetFaceBoundaryDraw(True)
            drawer.SetFaceBoundaryAspect(edge)
            self.ctx.Display(ais, 1, -1, False)
        self.view.SetProj(V3d_TypeOfOrientation.V3d_XnegYnegZpos)
        self.view.FitAll(0.06, False)
        self.view.Redraw()
        img = Image_AlienPixMap()
        if not self.view.ToPixMap(img, self.w, self.h,
                                  Graphic3d_BufferType.Graphic3d_BT_RGB):
            raise RuntimeError(f"{path}: no image")
        if not img.Save(TCollection_AsciiString(str(out))):
            raise RuntimeError(f"{out}: not written")


def sheet(pngs, out, cols=4):
    """Paste the PNGs into one contact sheet with their names.

    Args:
        pngs: The image paths.
        out: The sheet's path.
        cols: Images per row.
    """
    from PIL import Image, ImageDraw
    ims = [Image.open(p).convert("RGB") for p in pngs]
    w, h = ims[0].size
    rows = (len(ims) + cols - 1) // cols
    sh = Image.new("RGB", (w * cols, h * rows), "white")
    draw = ImageDraw.Draw(sh)
    for i, (im, p) in enumerate(zip(ims, pngs, strict=True)):
        x, y = (i % cols) * w, (i // cols) * h
        sh.paste(im, (x, y))
        draw.text((x + 10, y + 8), Path(p).stem, fill=(0, 0, 0))
    sh.save(out)


def main(argv):
    """Render every model of the library.

    Args:
        argv: Command-line arguments without the program name.

    Returns:
        0 when every model renders.
    """
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=Path)
    ap.add_argument("--size", type=int, nargs=2, default=(800, 600))
    args = ap.parse_args(argv)
    args.out.mkdir(parents=True, exist_ok=True)
    r = Renderer(*args.size)
    pngs = []
    for spec in gen_all.load_specs():
        name = spec.FOOTPRINT["name"]
        path = HERE.parent / f"{gen_all.LIB}.3dshapes" / f"{name}.step"
        png = args.out / f"{name}.png"
        r.render(path, png)
        pngs.append(png)
        print(f"wrote {png}")
    sheet(pngs, args.out / "all.png")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
