// The M5Stack Dial from its STEP model: the file tools/video_cad.py
// writes with --solids, one mesh a solid in the solid's colour.  Units are
// mm.  The model's z axis, the knob's, stands up as y; the model's -y,
// where the USB-C socket is, faces +z.  The Dial rests on its rear parts,
// y = 0 at their lowest face.
//
// The knob ring is one solid and turns alone.  The display shows a face of
// the Dial, a 240 x 240 image, on the round glass; two faces cross-fade.
//
// The hand-held base (hardware/mechanical/_gen_dial_base.py), when given,
// is two printed parts in the Dial's own frame: z = 0 where the Dial's
// flange seats.  The Dial then stands on the base's foot.
//
// With the base and its extra parts the whole can stand apart: the Dial
// with its ring nut and the base's top part form the upper half, the
// bottom part with the cell and the foam pad the lower one.  setParts()
// takes a number per part, 0 seated and 1 fully out; setFlip() rolls the
// whole over, so that the foot and its 4 screws show.  The cell, the pad
// and the screws are drawn here as plain solids from the numbers of
// hardware/mechanical/README.md.
//
// SPDX-License-Identifier: MIT
import * as THREE from 'three';

const KNOB = 'solid-0';     // the ring with the 20 grooves
const GLASS = 13.8;         // the top of the glass above the panel seat
const SCREEN = 16.2;        // the radius of the lit display, 1.28in across
const BASE_SIDE = 35.2;     // the base's radius 12mm under the seat, at
                            // the USB opening: the arc of 49.5 from 60
                            // across at the top to 72 across 21mm down

// The meshes of a file tools/video_cad.py writes with --solids, laid into
// the scene's frame: each handed to put(mesh, name), all in box.
function layMeshes(gltf, box, put, look) {
  // The writer turns mm into m; a quarter turn about x stands z up.
  const src = gltf.scene;
  src.updateMatrixWorld(true);
  const lay = new THREE.Matrix4().makeRotationX(-Math.PI / 2)
    .multiply(new THREE.Matrix4().makeScale(1000, 1000, 1000));
  src.traverse(o => {
    if (!o.isMesh) return;
    const g = o.geometry.clone();
    g.applyMatrix4(new THREE.Matrix4().multiplyMatrices(lay, o.matrixWorld));
    g.computeVertexNormals();
    g.computeBoundingBox();
    box.union(g.boundingBox);
    const mesh = new THREE.Mesh(g, look(o.material.color));
    mesh.castShadow = mesh.receiveShadow = true;
    put(mesh, o.name);
  });
}

// How far out a part stands at 1, in mm: [name, its group's way].
const AWAY = {
  upper: [0, 58, 0], dial: [0, 46, 0], nut: [0, -26, 0],
  cell: [0, 20, 0], pad: [0, 34, 0], screws: [0, -34, 0],
};
const HEIGHT = [-32.5, 14.6];   // the base's foot and the bezel's top

// The Dial alone: a group that stands on y = 0, with what moves on it.
// bases: the glTF files of the base's two parts, or null for none.
// nut: the glTF file of the ring nut, or null.
export function makeDial(gltf, bases = null, nut = null) {
  const root = new THREE.Group();
  const body = new THREE.Group(), knob = new THREE.Group();
  const box = new THREE.Box3();
  layMeshes(gltf, box, (mesh, name) => (name === KNOB ? knob : body).add(mesh),
    c => {
      // A black part of the model would swallow the light: lifted a little.
      const dark = c.r + c.g + c.b < .12;
      return new THREE.MeshStandardMaterial({
        color: dark ? new THREE.Color(.05, .055, .07) : c.clone(),
        roughness: dark ? .45 : .6, metalness: dark ? .25 : .1});
    });
  // The rear parts stand on the ground; with the base, its foot does.
  const alone = -box.min.y;
  const g = {};
  for (const name of ['all', 'upper', 'dial', 'top', 'nut', 'lower',
                      'cell', 'pad', 'screws']) {
    g[name] = new THREE.Group();
  }
  const plain = c => new THREE.MeshStandardMaterial(
    {color: c.clone(), roughness: .75, metalness: 0});
  (bases || []).forEach((part, n) => {
    // The top part first, then the bottom part.
    layMeshes(part, box, mesh => g[n ? 'lower' : 'top'].add(mesh), plain);
  });
  const rest = -box.min.y;
  const solid = (geo, at, color, rough = .6, metal = .1) => {
    geo.translate(...at);
    const mesh = new THREE.Mesh(geo, new THREE.MeshStandardMaterial(
      {color, roughness: rough, metalness: metal}));
    mesh.castShadow = mesh.receiveShadow = true;
    return mesh;
  };
  if (bases && nut) {
    // The ring nut under the top plate, 2.0mm thick.
    const none = new THREE.Box3();
    layMeshes(nut, none, mesh => g.nut.add(mesh), c => {
      const dark = c.r + c.g + c.b < .12;
      return new THREE.MeshStandardMaterial({
        color: dark ? new THREE.Color(.07, .075, .09) : c.clone(),
        roughness: .5, metalness: .2});
    });
    for (const mesh of g.nut.children) mesh.position.y = -2;
    // The cell 102540, 40.5 x 25 x 10mm, its top 20.1mm under the seat,
    // its length across the ^ mark, its wires away from the USB socket.
    g.cell.add(
      solid(new THREE.BoxGeometry(40.5, 10, 25), [0, -25.1, 0], 0xb9bec7,
            .35, .6),
      solid(new THREE.BoxGeometry(3, 10.1, 25.1), [-18.8, -25.1, 0],
            0xc9a23a, .5, .2),
      solid(new THREE.BoxGeometry(1.1, 1.1, 9), [-17.5, -22, -16.5],
            0xd23b3b, .6, 0),
      solid(new THREE.BoxGeometry(1.1, 1.1, 9), [-15.5, -22, -16.5],
            0x23262d, .6, 0));
    // The foam pad, 2mm, about 20 x 30mm, on the cell.
    g.pad.add(solid(new THREE.BoxGeometry(30, 2, 20), [0, -19.1, 0],
                    0x2b2f37, .95, 0));
    // 4 screws M3 x 12 on a circle of 58mm at 45 degrees, their pan
    // heads sunk 8mm into the foot.
    for (const [sx, sz] of [[1, 1], [1, -1], [-1, 1], [-1, -1]]) {
      const x = sx * 29 * Math.SQRT1_2, z = sz * 29 * Math.SQRT1_2;
      g.screws.add(
        solid(new THREE.CylinderGeometry(2.8, 2.8, 2, 24),
              [x, -25.5, z], 0xa9afb9, .35, .8),
        solid(new THREE.CylinderGeometry(1.5, 1.5, 12, 16),
              [x, -18.5, z], 0xa9afb9, .35, .8));
    }
  }
  g.dial.add(body, knob);
  g.upper.add(g.dial, g.top, g.nut);
  g.lower.add(g.cell, g.pad);
  g.all.add(g.upper, g.lower, g.screws);
  g.all.position.y = rest;
  root.add(g.all);
  const state = {};
  let flip = 0;
  const place = () => {
    for (const [name, away] of Object.entries(AWAY)) {
      const k = state[name] || 0;
      g[name].position.set(away[0] * k, away[1] * k, away[2] * k);
    }
    // The screws leave through the ground: gone at their far end.
    g.screws.visible = (state.screws || 0) < .98;
    // Rolled over about its middle, lifted so that no edge dips.
    const turn = flip * Math.PI / 180, mid = (HEIGHT[0] + HEIGHT[1]) / 2;
    g.all.rotation.z = turn;
    g.all.position.set(
      mid * Math.sin(turn),
      rest + mid * (1 - Math.cos(turn)) + 36 * Math.abs(Math.sin(turn)),
      0);
  };
  place();

  // Two faces on the glass, the second over the first.
  const faces = [0, 1].map(k => {
    const m = new THREE.MeshBasicMaterial(
      {toneMapped: false, transparent: true, opacity: k ? 0 : 1});
    const disc = new THREE.Mesh(new THREE.CircleGeometry(SCREEN, 96), m);
    disc.rotation.x = -Math.PI / 2;
    disc.position.y = GLASS + .06 + k * .02;
    disc.visible = false;
    g.dial.add(disc);
    return {m, disc, image: null};
  });
  const show = (f, image, opacity) => {
    if (image && f.image !== image) {
      const tex = new THREE.Texture(image);
      tex.colorSpace = THREE.SRGBColorSpace;
      tex.anisotropy = 8;
      tex.needsUpdate = true;
      if (f.m.map) f.m.map.dispose();
      f.m.map = tex;
      f.m.needsUpdate = true;
      f.image = image;
    }
    f.disc.visible = !!image && opacity > 0;
    f.m.opacity = opacity;
  };

  // A point of the Dial in the scene, wherever the group stands.
  const on = (name, x, y, z) => () => g[name].localToWorld(
    new THREE.Vector3(x, y, z)).toArray();
  const at = (x, y, z) => on('dial', x, y, z);
  const inBase = g.top.children.length > 0;
  return {
    root,
    // A pixel of the Dial's 240 x 240 face, in the scene.
    pixel: (x, y) => {
      root.updateWorldMatrix(true, true);
      return g.dial.localToWorld(new THREE.Vector3(
        (x / 120 - 1) * SCREEN, GLASS + .1, (y / 120 - 1) * SCREEN))
        .toArray();
    },
    // How far the base raises the Dial over standing alone, mm.
    lift: rest - alone,
    // Named points: x right, y up from the Dial's seat, z towards the
    // USB-C socket.  In the base, the socket is reached through the
    // base's opening and the cell lies in the base's lower part.
    anchors: {
      display: at(0, GLASS, 0),
      knob: at(23.5, 8, 8),
      bezel: at(-15, 14.6, -14),
      usb: inBase ? at(0, -12, BASE_SIDE) : at(0, -12, 18.5),
      power: at(0, -9, -18.7),
      portA: at(-17, -9, 0),
      portB: at(17, -9, 0),
      body: inBase ? on('lower', -30, -24, 16) : at(-21, -6, 10),
      // The Stamp-S3 on the Dial's rear, the parts of the base.
      stamp: at(0, -16.5, 9),
      nut: on('nut', 24.5, -4.5, 6),
      top: on('top', -29, -8, 14),
      bottom: on('lower', -30, -26, 16),
      cell: on('cell', 10, -20.1, 4),
      pad: on('pad', -8, -18.1, 2),
      screws: on('screws', 29 * Math.SQRT1_2, -26.5, 29 * Math.SQRT1_2),
    },
    // How far out each part stands: {name: 0 to 1}.
    setParts: parts => { Object.assign(state, parts); place(); },
    // The whole rolled over, in degrees: 180 shows the foot.
    setFlip: deg => { flip = deg; place(); },
    // The face images a and b and how far b has come in over a, 0 to 1.
    setFace: (a, b, mix) => {
      show(faces[0], a, 1);
      show(faces[1], mix > 0 ? b : null, mix);
    },
    // The knob's angle in degrees, clockwise seen from the front.
    setTurn: deg => { knob.rotation.y = -deg * Math.PI / 180; },
  };
}

// The Dial in a scene of its own.  With the base the ground drops by the
// base's lift, so the Dial stays where the camera paths look for it.
export function buildDial(gltf, bases = null, nut = null) {
  const scene = new THREE.Scene();
  const cam = new THREE.PerspectiveCamera(30, 16 / 9, 2, 2000);
  scene.add(new THREE.HemisphereLight(0xcfe3ff, 0x10151f, 1.5));
  const sun = new THREE.DirectionalLight(0xffffff, 2.6);
  sun.position.set(-70, 150, 120);
  sun.castShadow = true;
  sun.shadow.mapSize.set(2048, 2048);
  Object.assign(sun.shadow.camera,
    {left: -90, right: 90, top: 90, bottom: -90, near: 10, far: 500});
  sun.shadow.radius = 5;
  sun.shadow.bias = -.0008;
  scene.add(sun);
  const rim = new THREE.DirectionalLight(0x38d6ff, 1.2);
  rim.position.set(120, 50, -120);
  scene.add(rim);
  const fill = new THREE.DirectionalLight(0xffffff, 1.0);
  fill.position.set(40, 30, 160);
  scene.add(fill);
  const ground = new THREE.Mesh(new THREE.PlaneGeometry(2000, 2000),
    new THREE.ShadowMaterial({opacity: .3}));
  ground.rotation.x = -Math.PI / 2;
  ground.position.y = -.1;
  ground.receiveShadow = true;
  scene.add(ground);

  const dial = makeDial(gltf, bases, nut);
  dial.root.position.y = -dial.lift;
  ground.position.y -= dial.lift;
  scene.add(dial.root);
  return {
    // A touch on the Dial's face finds its pixels under this name on
    // every rig that holds a Dial.
    scene, cam, ...dial, dialPixel: dial.pixel,
    setLid: () => {},
    setServoPort: () => {},
    setServo: () => {},
  };
}
