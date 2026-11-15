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

// The Dial alone: a group that stands on y = 0, with what moves on it.
// bases: the glTF files of the base's two parts, or null for none.
export function makeDial(gltf, bases = null) {
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
  const base = new THREE.Group();
  for (const part of bases || []) {
    layMeshes(part, box, mesh => base.add(mesh),
      c => new THREE.MeshStandardMaterial(
        {color: c.clone(), roughness: .75, metalness: 0}));
  }
  const rest = -box.min.y;
  body.position.y = knob.position.y = base.position.y = rest;
  root.add(body, knob, base);

  // Two faces on the glass, the second over the first.
  const faces = [0, 1].map(k => {
    const m = new THREE.MeshBasicMaterial(
      {toneMapped: false, transparent: true, opacity: k ? 0 : 1});
    const disc = new THREE.Mesh(new THREE.CircleGeometry(SCREEN, 96), m);
    disc.rotation.x = -Math.PI / 2;
    disc.position.y = rest + GLASS + .06 + k * .02;
    disc.visible = false;
    root.add(disc);
    return {m, disc, image: null};
  });
  const show = (f, image, opacity) => {
    if (image && f.image !== image) {
      const tex = new THREE.Texture(image);
      tex.colorSpace = THREE.SRGBColorSpace;
      tex.anisotropy = 8;
      tex.needsUpdate = true;
      f.m.map = tex;
      f.m.needsUpdate = true;
      f.image = image;
    }
    f.disc.visible = !!image && opacity > 0;
    f.m.opacity = opacity;
  };

  // A point of the Dial in the scene, wherever the group stands.
  const at = (x, y, z) => () => root.localToWorld(
    new THREE.Vector3(x, rest + y, z)).toArray();
  const inBase = base.children.length > 0;
  return {
    root,
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
      body: inBase ? at(-30, -24, 16) : at(-21, -6, 10),
    },
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
export function buildDial(gltf, bases = null) {
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

  const dial = makeDial(gltf, bases);
  dial.root.position.y = -dial.lift;
  ground.position.y -= dial.lift;
  scene.add(dial.root);
  return {
    scene, cam, ...dial,
    setLid: () => {},
    setServoPort: () => {},
    setServo: () => {},
  };
}
