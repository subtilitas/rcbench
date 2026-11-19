// A servo setting up a flap, in one scene: a slice of wing with its flap,
// the servo on its frame and the linkage (flap_drive_assembly of
// hardware/mechanical/_gen_flap_drive.py), an inertial sensor on a pad of
// double-sided gel tape on the flap (wit_imu), the bench and the M5Stack
// Dial.  Units are mm; x
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
const PAD = 2;                      // the gel tape's thickness
const WING_AT = [-150, 14, 215];    // the wing's nose in the scene
const DIAL_AT = [140, 0, 150];
// The bench standing: its lean and its turn in degrees, and where its
// middle goes.  The Dial's tip towards the viewer, degrees.
const STAND = [72, 14, -10, 54, 40], TIP = 48;

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
    // A pixel of the display's 1024 x 600 screen, in the scene.
    pixel: (x, y) => {
      scene.updateMatrixWorld(true);
      return glass.localToWorld(new THREE.Vector3(
        (x / 1024 - .5) * 154.2, (.5 - y / 600) * 85.9, 0)).toArray();
    },
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
  // The bench as one body: it lies flat at the origin, or stands on its
  // long edge behind the wing, leaning back and turned to the viewer, so
  // that its display is read while the flap moves in front of it.
  const bench = new THREE.Group(), lean = new THREE.Group();
  const cant = new THREE.Group();
  for (const o of [...scene.children]) {
    if (o.isLight || (o.isMesh && o.material.isShadowMaterial)) continue;
    bench.add(o);
  }
  lean.add(bench);
  cant.add(lean);
  scene.add(cant);

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
    mesh.userData.skin = n === 0 || n === 1 || n === 2;
    mesh.userData.wire = n >= 51;
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
  // Double-sided gel tape between the sensor and the skin: a clear pad
  // 2mm thick under the sensor's whole base.
  const tapeMat = new THREE.MeshPhysicalMaterial(
    {color: 0xbfe6ff, roughness: .15, transmission: .6, thickness: 2,
     transparent: true, opacity: 0});
  const tape = new THREE.Group();
  const pad = new THREE.Mesh(
    new THREE.BoxGeometry(34, 49, PAD), tapeMat);
  pad.position.set(0, 0, PAD / 2);
  tape.add(pad);
  seat.add(tape);

  // The servo's lead: from the servo's three wires through the wing to
  // the slice's end towards the bench, over the table and into the
  // bench's connector wall.
  const wires = new THREE.Box3();
  frame.updateMatrixWorld(true);
  frame.traverse(o => {
    if (o.isMesh && o.userData.wire) {
      wires.union(new THREE.Box3().setFromObject(o));
    }
  });
  const from = wires.getCenter(new THREE.Vector3());
  // The header the plug goes into, in the bench's own frame.
  // J1, the header of the programming ports, is the one nearer the
  // case's corner (docs/Carrier.md).  PROGP01 is its position 2 of 4,
  // counted from the left with the display up.  In the case's assembly,
  // laid as case_cad.js lays it, that column's pins are at x 57.50, its
  // middle row at y 11.13, and the header's body ends at z 47.38, 6mm
  // behind the pins' tips and 6.71mm behind the wall's outer face.  The
  // plug's face stands at the body, over the pins.
  const socket = [57.5, 11.13, 47.38];
  const WIRES = [0x7a4a1e, 0xd0122a, 0xe8731a];
  const leads = WIRES.map(color => {
    const m = new THREE.Mesh(new THREE.BufferGeometry(),
      new THREE.MeshStandardMaterial({color, roughness: .6}));
    m.castShadow = true;
    scene.add(m);
    return m;
  });
  // The lead's plug: a 3-pin servo connector, 2.6 x 7.6 x 14mm, black,
  // standing on its narrow side: a header holds 4 plugs side by side,
  // each with its 3 pins one over the other.
  const plug = new THREE.Group();
  const shell = new THREE.Mesh(new THREE.BoxGeometry(2.6, 7.8, 14),
    new THREE.MeshStandardMaterial({color: 0x3a3f4a, roughness: .45}));
  shell.castShadow = true;
  plug.add(shell);
  WIRES.forEach((color, i) => {
    const eye = new THREE.Mesh(new THREE.BoxGeometry(1.5, 1.5, .4),
      new THREE.MeshStandardMaterial({color: 0xd4ad38, metalness: .6,
                                      roughness: .35}));
    // J1's rows from the top: GND, +5V, signal.
    eye.position.set(0, (1 - i) * 2.54, 7.1);
    plug.add(eye);
    const tab = new THREE.Mesh(new THREE.BoxGeometry(.5, 1.7, 5),
      new THREE.MeshStandardMaterial({color, roughness: .6}));
    tab.position.set(1.4, (1 - i) * 2.54, -3.5);
    plug.add(tab);
  });
  scene.add(plug);
  let way = null, wayKey = '';
  // The lead from the servo to the plug, laid anew when the bench moves.
  const lay = () => {
    scene.updateMatrixWorld(true);
    const port = bench.localToWorld(new THREE.Vector3(...socket));
    // The way out of the header: the bench's connector wall faces +z.
    const out = new THREE.Vector3(0, 0, 1).transformDirection(
      bench.matrixWorld);
    const key = port.toArray().concat(out.toArray()).map(
      v => v.toFixed(2)).join();
    if (key === wayKey) return;
    wayKey = key;
    const back = port.clone().addScaledVector(out, 24);
    const clear = port.clone().addScaledVector(out, 40);
    clear.y = Math.max(4, clear.y * .5);
    way = new THREE.CatmullRomCurve3([
      from, new THREE.Vector3(from.x, from.y - 2, 168),
      new THREE.Vector3(from.x * .5 + clear.x * .5, 3,
                        (168 + clear.z) / 2 + 10),
      clear, back, port.clone().addScaledVector(out, 13)]);
    leads.forEach((m, i) => {
      m.geometry.dispose();
      m.geometry = new THREE.TubeGeometry(way, 160, .55, 8);
      // The three wires one over the other, as they enter the plug.
      m.position.set(0, (1 - i) * 1.15, 0);
    });
    plug.position.copy(port).addScaledVector(out, 7);
    plug.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, -1), out);
  };

  const knob = makeDial(dial, bases);
  // The Dial tips towards the viewer about its foot's front edge.
  const tip = new THREE.Group();
  tip.position.set(...DIAL_AT);
  tip.add(knob.root);
  scene.add(tip);

  // The skins that hide the servo: the wing's shell and its cover.
  const skins = [];
  frame.traverse(o => { if (o.isMesh && o.userData.skin) skins.push(o); });
  const state = {imu: 0, tape: 0, skin: 1, lead: 0, stand: 0, tip: 0};
  const place = () => {
    sensor.position.z = PAD + state.imu * 70;
    sensor.visible = state.imu < 1;
    tape.position.z = state.tape * 45;
    tapeMat.opacity = .75 * (1 - state.tape);
    tape.visible = state.tape < 1;
    // The bench from flat at the origin to standing behind the wing.
    const k = state.stand;
    lean.rotation.x = k * STAND[0] * Math.PI / 180;
    cant.rotation.y = k * STAND[1] * Math.PI / 180;
    cant.position.set(k * STAND[2], k * STAND[3], k * STAND[4]);
    tip.rotation.x = state.tip * TIP * Math.PI / 180;
    tip.position.y = state.tip * 26;
    lay();
    // The lead grows from the servo to the bench as it is plugged in.
    for (const m of leads) {
      m.visible = state.lead < 1;
      m.geometry.setDrawRange(
        0, Math.round(160 * (1 - state.lead)) * 8 * 6);
    }
    plug.visible = state.lead < .04;
    for (const m of skins) {
      m.material.transparent = state.skin < 1;
      m.material.opacity = .18 + .82 * state.skin;
      m.material.depthWrite = state.skin > .5;
      m.castShadow = state.skin > .5;
    }
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
      imu: onFlap(IMU_AT[0], IMU_AT[1], IMU_AT[2] + PAD + 15),
      tape: onFlap(IMU_AT[0], IMU_AT[1] - 25, IMU_AT[2] + 1),
      horn: inWing(EYE[0], -5, EYE[1]),
      lead: () => way.getPoint(.62).toArray(),
      port: () => plug.position.toArray(),
    },
    setFace: knob.setFace, setTurn: knob.setTurn,
    dialPixel: knob.pixel,
    // How far out the sensor stands, 1 to 0, its tape, 1 off to 0 on,
    // the skins, 1 solid to 0 clear, the servo's lead, 1 away to 0
    // plugged in, the bench, 0 flat to 1 standing, and the Dial, 0 level
    // to 1 tipped towards the viewer.
    setParts: parts => { Object.assign(state, parts); place(); },
    setDeflect,
  };
}
