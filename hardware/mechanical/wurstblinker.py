"""Wurstblinker node and kinetic Wurstblinker node: two joke models.

Neither is hardware of the bench, and neither is built. The models are
the solids of the cartoon the video ``wurstblinker`` shows
(tools/video/player/wurst3d.js), in the same sizes: a stand, a node board
with antenna and lamp, and

- ``wurstblinker_node``: a servo on a post, its shaft towards -y, an arm
  and a sausage clipped to the arm, drawn upright;
- ``wurstblinker_kinetic``: a tank on 2 posts, a pipe, a valve block and
  a barrel along +x with the sausage loaded.

Every size is chosen for the picture; none is measured. The sausage is a
cylinder 180 long and 28 across with flat ends; the video draws its ends
round. The video's balls, the antenna's tip and the lamp, are short
cylinders here, and its 2 clips are flat rings.

Frame: z up, z = 0 under the stand; x to the right as the video's first
view shows the node; +y away from that view. The stand's middle is at
x = 0, y = 0. All sizes mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _solid as s

BLUE, GREY, GREEN = (0.23, 0.29, 0.45), (0.54, 0.58, 0.65), (0.18, 0.64, 0.42)
PALE, DARK, RED = (0.79, 0.82, 0.88), (0.14, 0.16, 0.20), (1.00, 0.23, 0.31)
AMBER, SERVO, GOLD = (1.00, 0.69, 0.00), (0.18, 0.44, 0.84), (0.95, 0.78, 0.29)
WHITE, WURST, TANK = (0.96, 0.95, 0.90), (0.78, 0.41, 0.23), (0.89, 0.23, 0.23)

SAUSAGE_LEN, SAUSAGE_D = 180.0, 28.0
# The shaft's and the barrel's height over the stand's underside.
SHAFT_Z, BARREL_Z = 128.0, 134.0


def _rod(x, y, r, z0, z1, rgb):
    """An upright cylinder."""
    return s.ext(s.circle(x, y, r), z0, z1, rgb)


def stand():
    """The stand, the node board on its post, the antenna and the lamp.

    Returns:
        The shapes both models share.
    """
    return [
        s.box(-115, -55, 115, 55, 0, 14, BLUE),
        _rod(-70, 10, 7, 14, 74, GREY),
        s.box(-118, -21, -22, 41, 74, 80, GREEN),
        s.box(-103, -7, -73, 19, 80, 88, PALE),
        s.box(-59, 11, -45, 25, 80, 86, DARK),
        _rod(-108, 30, 1.8, 80, 150, DARK),
        _rod(-108, 30, 6, 146, 158, RED),
        _rod(-40, -8, 7, 80, 88, AMBER),
    ]


def node():
    """The Wurstblinker node, its sausage upright.

    Returns:
        The shapes.
    """
    z0 = SHAFT_Z + 14
    return stand() + [
        _rod(40, 10, 9, 14, 96, GREY),
        s.box(17, -3, 63, 23, 96, 140, SERVO),
        s.box(7, -3, 73, 23, 127.5, 132.5, SERVO),
        # The shaft's hub, in front of the servo.
        s.ext(s.circle(40, SHAFT_Z, 7), 3, 13, GOLD, frame=s.Y),
        s.box(34, -18, 46, -14, 124, 220, WHITE),
        _rod(40, -28, SAUSAGE_D / 2, z0, z0 + SAUSAGE_LEN, WURST),
    ] + [s.ext(s.circle(40, -28, 16.5), z - 2.2, z + 2.2, WHITE,
               holes=[s.circle(40, -28, SAUSAGE_D / 2 + 0.2)])
         for z in (184.0, 220.0)]


def kinetic():
    """The kinetic Wurstblinker node, its sausage in the barrel.

    Returns:
        The shapes.
    """
    bore = SAUSAGE_D / 2 + 2.5
    return stand() + [
        _rod(20, 10, 6, 14, 58, GREY),
        _rod(90, 10, 6, 14, 58, GREY),
        s.ext(s.circle(10, 78, 26), -11, 121, TANK, frame=s.X),
        # The gauge, on the tank's front.
        s.ext(s.circle(55, 80, 11), 16, 20, WHITE, frame=s.Y),
        _rod(10, 10, 5, 99, 123, GREY),
        s.box(-3, -3, 23, 23, 123, 145, GOLD),
        s.ext(s.circle(10, BARREL_Z, 21), 23, 223, GREY,
              holes=[s.circle(10, BARREL_Z, bore)], frame=s.X),
        s.ext(s.circle(10, BARREL_Z, SAUSAGE_D / 2), 30, 30 + SAUSAGE_LEN,
              WURST, frame=s.X),
    ] + [s.ext(s.circle(10, BARREL_Z, 25), x - 3, x + 3, DARK,
               holes=[s.circle(10, BARREL_Z, 21.2)], frame=s.X)
         for x in (30.0, 220.0)]


def parts(_colors):
    """The two models.

    Returns:
        A dict of file stem to solids.
    """
    return {"wurstblinker_node": s.items(node()),
            "wurstblinker_kinetic": s.items(kinetic())}
