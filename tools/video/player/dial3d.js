// The M5Stack Dial from its STEP model: the file tools/video_cad.py
// writes with --solids, one mesh a solid in the solid's colour.  Units are
// mm.  The model's z axis, the knob's, stands up as y; the model's -y,
// where the USB-C socket is, faces +z.  The Dial rests on its rear parts,
// y = 0 at their lowest face.
//
// The knob ring is one solid and turns alone.  The display shows a face of
// the Dial, a 240 x 240 image, on the round glass; two faces cross-fade.
//
// SPDX-License-Identifier: MIT
import * as THREE from 'three';

const KNOB = 'solid-0';     // the ring with the 20 grooves
const GLASS = 13.8;         // the top of the glass above the panel seat
const SCREEN = 16.2;        // the radius of the lit display, 1.28in across

export function buildDial(gltf) {
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

  // The writer turns mm into m; a quarter turn about x stands z up.
  const src = gltf.scene;
  src.updateMatrixWorld(true);
  const lay = new THREE.Matrix4().makeRotationX(-Math.PI / 2)
    .multiply(new THREE.Matrix4().makeScale(1000, 1000, 1000));
  const body = new THREE.Group(), knob = new THREE.Group();
  const box = new THREE.Box3();
  src.traverse(o => {
    if (!o.isMesh) return;
    const g = o.geometry.clone();
    g.applyMatrix4(new THREE.Matrix4().multiplyMatrices(lay, o.matrixWorld));
    g.computeVertexNormals();
    g.computeBoundingBox();
    box.union(g.boundingBox);
    const c = o.material.color;
    // A black part of the model would swallow the light: lifted a little.
    const dark = c.r + c.g + c.b < .12;
    const mesh = new THREE.Mesh(g, new THREE.MeshStandardMaterial({
      color: dark ? new THREE.Color(.05, .055, .07) : c.clone(),
      roughness: dark ? .45 : .6, metalness: dark ? .25 : .1}));
    mesh.castShadow = mesh.receiveShadow = true;
    (o.name === KNOB ? knob : body).add(mesh);
  });
  const rest = -box.min.y;        // the rear parts stand on the ground
  body.position.y = knob.position.y = rest;
  scene.add(body, knob);

  // Two faces on the glass, the second over the first.
  const faces = [0, 1].map(k => {
    const m = new THREE.MeshBasicMaterial(
      {toneMapped: false, transparent: true, opacity: k ? 0 : 1});
    const disc = new THREE.Mesh(new THREE.CircleGeometry(SCREEN, 96), m);
    disc.rotation.x = -Math.PI / 2;
    disc.position.y = rest + GLASS + .06 + k * .02;
    disc.visible = false;
    scene.add(disc);
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

  const at = (x, y, z) => () => [x, rest + y, z];
  return {
    scene, cam,
    // Named points, in the scene's mm: x right, y up from the ground,
    // z towards the USB-C socket.
    anchors: {
      display: at(0, GLASS, 0),
      knob: at(23.5, 8, 8),
      bezel: at(-15, 14.6, -14),
      usb: at(0, -12, 18.5),
      power: at(0, -9, -18.7),
      portA: at(-17, -9, 0),
      portB: at(17, -9, 0),
      body: at(-21, -6, 10),
    },
    setLid: () => {},
    setServoPort: () => {},
    setServo: () => {},
    // The face images a and b and how far b has come in over a, 0 to 1.
    setFace: (a, b, mix) => {
      show(faces[0], a, 1);
      show(faces[1], mix > 0 ? b : null, mix);
    },
    // The knob's angle in degrees, clockwise seen from the front.
    setTurn: deg => { knob.rotation.y = -deg * Math.PI / 180; },
  };
}
