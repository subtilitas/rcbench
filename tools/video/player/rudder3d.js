// A servo setting up a flap, in one scene: a slice of wing with its flap,
// the servo on its frame and the linkage (flap_drive_assembly of
// hardware/mechanical/_gen_flap_drive.py), an inertial sensor taped onto
// the flap (wit_imu), the bench and the M5Stack Dial.  Units are mm; x
// runs right, y up, z towards the viewer.
//
// The flap turns about its hinge and the linkage follows: the double arm
// turns about the servo's shaft, and the rod stays between the arm's pin
// and the horn's eye.  The joints are the generator's, in the wing's frame
// (x along the chord, y along the span, z up).  setDeflect() takes the
// flap's angle in degrees, positive with the trailing edge up, as the
// bench shows it.
//
// The bench is the case of case_cad.js where its model is given, and the
// 7 inch panel alone, leaning back, where it is not.  Either shows the
// panel's screen.
//
// SPDX-License-Identifier: MIT
import * as THREE from 'three';
import {buildCadCase} from './case_cad.js';
import {makeDial} from './dial3d.js';

// The linkage's points at neutral, (x, z) in the wing's frame.
const SHAFT = [87.259, 5.696], PIN = [87.062, -1.301];
const HINGE = [172.5, -1.350], EYE = [173.5, 11.568];
// The assembly's solids in the file's order: what turns with what.
const FLAP = [1, 5, 26, 27, 28], ARM = [21, 22], ROD = [23, 24, 25];
// The sensor on the flap's upper skin: its middle along the chord and the
// span, the skin's height there and its slope.
const IMU_AT = [203, 16, 4.36], IMU_SLOPE = -.141;
const WING_AT = [-150, 14, 215];    // the wing's nose in the scene
const DIAL_AT = [140, 0, 150];

const turned = (p, c, deg) => {
  const a = deg * Math.PI / 180, dx = p[0] - c[0], dz = p[1] - c[1];
  return [c[0] + dx * Math.cos(a) + dz * Math.sin(a),
          c[1] - dx * Math.sin(a) + dz * Math.cos(a)];
};
const dist = (p, q) => Math.hypot(p[0] - q[0], p[1] - q[1]);
const ROD_LEN = dist(PIN, EYE);

// The arm's angle for a flap's angle: the pin stays the rod's length from
// the eye.
function armFor(flap) {
  const eye = turned(EYE, HINGE, flap);
  const miss = a => dist(turned(PIN, SHAFT, -a), eye) - ROD_LEN;
  let lo = -75, hi = 75;
  if (miss(lo) * miss(hi) > 0) return 0;
  for (let i = 0; i < 40; i++) {
    const mid = (lo + hi) / 2;
    if (miss(lo) * miss(mid) > 0) lo = mid; else hi = mid;
  }
  return (lo + hi) / 2;
}

function meshesOf(gltf, lay, each) {
  gltf.scene.updateMatrixWorld(true);
  let n = 0;
  gltf.scene.traverse(o => {
    if (!o.isMesh) return;
    const g = o.geometry.clone();
    g.applyMatrix4(new THREE.Matrix4().multiplyMatrices(lay, o.matrixWorld));
    g.computeVertexNormals();
    g.computeBoundingBox();
    const c = o.material.color, dark = c.r + c.g + c.b < .12;
    const mesh = new THREE.Mesh(g, new THREE.MeshStandardMaterial({
      color: dark ? new THREE.Color(.05, .055, .07) : c.clone(),
      roughness: .55, metalness: .12}));
    mesh.castShadow = mesh.receiveShadow = true;
    each(mesh, n++);
  });
}

// The panel alone as the bench: leaning back, its display showing a shot.
function panelBench(gltf) {
  const scene = new THREE.Scene();
  const cam = new THREE.PerspectiveCamera(30, 16 / 9, 5, 3000);
  scene.add(new THREE.HemisphereLight(0xcfe3ff, 0x10151f, 1.35));
  const sun = new THREE.DirectionalLight(0xffffff, 2.6);
  sun.position.set(-120, 260, 220);
  sun.castShadow = true;
  sun.shadow.mapSize.set(2048, 2048);
  Object.assign(sun.shadow.camera,
    {left: -340, right: 340, top: 340, bottom: -340, near: 10, far: 1100});
  sun.shadow.bias = -.0008;
  scene.add(sun);
  const rim = new THREE.DirectionalLight(0x38d6ff, 1.0);
  rim.position.set(220, 120, -200);
  scene.add(rim);
  const floor = new THREE.Mesh(new THREE.PlaneGeometry(4000, 4000),
                               new THREE.ShadowMaterial({opacity: .28}));
  floor.rotation.x = -Math.PI / 2;
  floor.receiveShadow = true;
  scene.add(floor);
  const lay = new THREE.Matrix4().makeRotationX(-Math.PI / 2)
    .multiply(new THREE.Matrix4().makeScale(1000, 1000, 1000));
  const body = new THREE.Group(), box = new THREE.Box3();
  meshesOf(gltf, lay, mesh => {
    box.union(mesh.geometry.boundingBox);
    body.add(mesh);
  });
  const mid = box.getCenter(new THREE.Vector3());
  const mat = new THREE.MeshBasicMaterial({color: 0x05070b});
  const glass = new THREE.Mesh(new THREE.PlaneGeometry(154.2, 85.9), mat);
  glass.rotation.x = -Math.PI / 2;
  glass.position.set(mid.x + .9, box.max.y + .08, mid.z + 1.4);
  body.add(glass);
  body.position.set(-mid.x, -mid.y, -mid.z);
  const lean = new THREE.Group();
  lean.add(body);
  lean.rotation.x = 62 * Math.PI / 180;
  lean.position.set(0, 46, 0);
  scene.add(lean);
  let shown = null;
  const on = (x, y, z) => () => lean.localToWorld(
    new THREE.Vector3(x, y, z)).toArray();
  return {
    scene, cam,
    anchors: {display: on(0, 8, 0), panel: on(60, 8, -30)},
    setScreen: image => {
      if (!image || image === shown) return;
      const tex = new THREE.Texture(image);
      tex.colorSpace = THREE.SRGBColorSpace;
      tex.anisotropy = 8;
      tex.needsUpdate = true;
      if (mat.map) mat.map.dispose();
      mat.map = tex;
      mat.color.set(0xffffff);
      mat.needsUpdate = true;
      shown = image;
    },
    setLid: () => {}, setServoPort: () => {}, setServo: () => {},
  };
}

export function buildRudder({wing, imu, dial, bases, caseGltf, panel}) {
  const rig = caseGltf ? buildCadCase(null, caseGltf) : panelBench(panel);
  const {scene} = rig;

  // The wing in its own frame: z up, y along the span.
  const frame = new THREE.Group();
  frame.rotation.x = -Math.PI / 2;
  frame.position.set(...WING_AT);
  scene.add(frame);
  const mm = new THREE.Matrix4().makeScale(1000, 1000, 1000);
  // A group that turns about an axis along the span through (x, z).
  const pivot = ([x, z]) => {
    const at = new THREE.Group(), turn = new THREE.Group();
    const hold = new THREE.Group();
    at.position.set(x, 0, z);
    hold.position.set(-x, 0, -z);
    turn.add(hold);
    at.add(turn);
    frame.add(at);
    return {at, turn, hold};
  };
  const flap = pivot(HINGE), arm = pivot(SHAFT), rod = pivot(PIN);
  meshesOf(wing, mm, (mesh, n) => {
    (FLAP.includes(n) ? flap.hold : ARM.includes(n) ? arm.hold
      : ROD.includes(n) ? rod.hold : frame).add(mesh);
  });

  // The sensor and its tape ride on the flap.
  const seat = new THREE.Group();
  seat.position.set(IMU_AT[0], IMU_AT[1], IMU_AT[2]);
  seat.rotation.y = -Math.atan(IMU_SLOPE);
  flap.hold.add(seat);
  const sensor = new THREE.Group();
  meshesOf(imu, mm, mesh => sensor.add(mesh));
  seat.add(sensor);
  const tapeMat = new THREE.MeshStandardMaterial(
    {color: 0xe9d38a, roughness: .7, transparent: true, opacity: 0});
  const tape = new THREE.Group();
  const strip = (x0, y0, z0, x1, y1, z1) => {
    const m = new THREE.Mesh(
      new THREE.BoxGeometry(x1 - x0, y1 - y0, z1 - z0), tapeMat);
    m.position.set((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
    tape.add(m);
  };
  // Two strips across the sensor, along the span: over its top, down
  // both sides and 14mm onto the skin.
  for (const x of [-10, 10]) {
    strip(x - 6, -26.4, 15, x + 6, 26.4, 15.3);
    strip(x - 6, -26.4, 0, x + 6, -26.1, 15.3);
    strip(x - 6, 26.1, 0, x + 6, 26.4, 15.3);
    strip(x - 6, -40, 0, x + 6, -26.1, .3);
    strip(x - 6, 26.1, 0, x + 6, 40, .3);
  }
  seat.add(tape);

  const knob = makeDial(dial, bases);
  knob.root.position.set(...DIAL_AT);
  scene.add(knob.root);

  const state = {imu: 0, tape: 1};
  const place = () => {
    sensor.position.z = state.imu * 70;
    sensor.visible = state.imu < 1;
    tapeMat.opacity = .92 * (1 - state.tape);
    tape.visible = state.tape < 1;
  };
  place();
  const setDeflect = deg => {
    const f = -deg, a = armFor(f);
    flap.turn.rotation.y = f * Math.PI / 180;
    arm.turn.rotation.y = -a * Math.PI / 180;
    const pin = turned(PIN, SHAFT, -a), eye = turned(EYE, HINGE, f);
    const was = Math.atan2(EYE[1] - PIN[1], EYE[0] - PIN[0]);
    const now = Math.atan2(eye[1] - pin[1], eye[0] - pin[0]);
    rod.turn.rotation.y = was - now;
    rod.at.position.set(pin[0], 0, pin[1]);
  };
  setDeflect(0);
  scene.updateMatrixWorld(true);
  const inWing = (x, y, z) => () => {
    scene.updateMatrixWorld(true);
    return frame.localToWorld(new THREE.Vector3(x, y, z)).toArray();
  };
  const onFlap = (x, y, z) => () => {
    scene.updateMatrixWorld(true);
    return flap.hold.localToWorld(new THREE.Vector3(x, y, z)).toArray();
  };
  return {
    ...rig,
    anchors: {
      ...rig.anchors,
      dial: knob.anchors.display, knob: knob.anchors.knob,
      wing: inWing(60, 16, 12), servo: inWing(SHAFT[0] + 12, 2, 2),
      hinge: inWing(HINGE[0], -28, HINGE[1]), rod: inWing(130, -5, 6),
      flap: onFlap(222, -10, 1.6), edge: onFlap(229, 16, .3),
      imu: onFlap(IMU_AT[0], IMU_AT[1], IMU_AT[2] + 15),
      tape: onFlap(IMU_AT[0] + 10, IMU_AT[1] + 34, IMU_AT[2]),
      horn: inWing(EYE[0], -5, EYE[1]),
    },
    setFace: knob.setFace, setTurn: knob.setTurn,
    // How far out the sensor stands, 1 to 0, and its tape, 1 off to 0 on.
    setParts: parts => { Object.assign(state, parts); place(); },
    setDeflect,
  };
}
