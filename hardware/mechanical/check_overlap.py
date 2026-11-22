#!/usr/bin/env python3
"""Check that no two solids of a mechanical model share volume.

Each named model under hardware/mechanical/models/ is read back with
OpenCASCADE. Every pair of solids whose boxes overlap is intersected;
the check fails on a pair that shares more than SHARED. Stems joined
by "+" are checked as one model: the parts as they sit together.

Usage:
    python3 hardware/mechanical/check_overlap.py [STEM[+STEM ...] ...]

Without a stem it checks MODELS, the models drawn part by part. Exit
status 1 when a pair fails. Needs OCP (pip install
cadquery-ocp==8.0.1.1.0).

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import sys
from pathlib import Path

from OCP.Bnd import Bnd_Box
from OCP.BRepAlgoAPI import BRepAlgoAPI_Common
from OCP.BRepBndLib import BRepBndLib
from OCP.BRepGProp import BRepGProp
from OCP.GProp import GProp_GProps

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "kicad" / "tools"))
import _models  # noqa: E402
import step_check  # noqa: E402

MODELS = ("panel_7b", "ina228_xt90+ina228_xt90_case", "ina219_cjmcu219",
          "lipo_charger_boost_5v", "boost_5v_en", "esp32_c6_zero",
          "core2350b", "core2350b_usb_adapter", "es3c28p",
          "lipo_702030_380mah",
          "lipo_102540_1000mah", "servo_standard", "hitec_d485hw",
          "flycolor_flydragon_lite_40a", "tmotor_at2814", "esc_motor_set",
          "kst_x10", "wit_imu",
          "rbmod_v2_smd", "rbmod_v2", "rp2350_can", "ina3221_module",
          "dcdc_buck_mini", "usbc_pd_trigger", "matek_mbec12s",
          "ina228_node_layout")
SHARED = 1e-6           # mm3


def shared(*paths):
    """Find the pairs of solids of one model that share volume.

    Args:
        *paths: The model's STEP files.

    Returns:
        (pairs, count): a list of (index, index, shared volume in mm3),
        and the number of solids.
    """
    solids = [solid for path in paths
              for solid, _rgb in step_check.read(path)[1]]
    boxes = []
    for solid in solids:
        box = Bnd_Box()
        box.SetGap(0.0)
        BRepBndLib.AddOptimal_s(solid, box, False, False)
        lo, hi = box.CornerMin(), box.CornerMax()
        boxes.append((lo.X(), lo.Y(), lo.Z(), hi.X(), hi.Y(), hi.Z()))
    pairs = []
    for i, a in enumerate(boxes):
        for j in range(i):
            b = boxes[j]
            if all(a[k] < b[k + 3] - 1e-4 and b[k] < a[k + 3] - 1e-4
                   for k in range(3)):
                both = BRepAlgoAPI_Common(solids[i], solids[j]).Shape()
                props = GProp_GProps()
                BRepGProp.VolumeProperties_s(both, props)
                if props.Mass() > SHARED:
                    pairs.append((j, i, props.Mass()))
    return pairs, len(solids)


def main(argv):
    """Run from the command line.

    Args:
        argv: The stems of the models to check; MODELS when empty.

    Returns:
        0 when no pair shares volume, else 1.
    """
    bad = 0
    for stem in argv or MODELS:
        pairs, count = shared(*(_models.path(one)
                                for one in stem.split("+")))
        print(f"{'FAIL' if pairs else 'ok  '} {stem}: {count} solids, "
              f"{len(pairs)} pairs share volume")
        for j, i, volume in pairs:
            print(f"     solids {j} and {i} share {volume:.4f}mm3")
        bad += bool(pairs)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
