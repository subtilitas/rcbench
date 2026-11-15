// The bench in its case as a three.js model: tub, display lid, rbmod
// carrier, PD mini, connectors and a servo on a lead.  Units are mm; x runs
// along the 190mm side, y up, z towards the connector wall.  The model is a
// mockup: outer size, connector order and J1's pin order are from the
// case's CAD (computer-aided design) views, every other dimension is
// estimated.
//
// SPDX-License-Identifier: MIT
import * as THREE from 'three';
import {RoundedBoxGeometry} from 'three/addons/geometries/RoundedBoxGeometry.js';

const W = 190, D = 120, H = 30, TH = 2.6;
const OW = 12.6, OH = 10.4, OY = 12, PITCH = 2.54;

// The headers behind the wall's two openings, seen from outside with the
// display up: positions run left to right, rows top to bottom.  J1's order
// is confirmed on the case; J2's rows are assumed to match J1's.
export const CONNECTORS = {
  J1: {x: 58, title: 'PROG ports', rows: ['GND', '+ 5 V', 'signal'],
       positions: [['PROGP04', 0], ['PROGP01', 13], ['PROGP02', 14],
                   ['PROGP03', 15]]},
  J2: {x: 34, title: 'servo outputs', rows: ['GND', '+', 'signal'],
       positions: [['PWM03', 5], ['PWM02', 4], ['PWM01', 2], ['PWM04', 1]]},
};
export const WIRE_COLORS = ['#7a4a22', '#e0362c', '#ff9a1f'];

const mat = (c, o = {}) => new THREE.MeshStandardMaterial(
  {color: c, roughness: .7, metalness: 0, ...o});

export function buildCase(displayImage) {
  const scene = new THREE.Scene();
  const cam = new THREE.PerspectiveCamera(30, 16 / 9, 5, 3000);
  scene.add(new THREE.HemisphereLight(0xcfe3ff, 0x10151f, 1.25));
  const sun = new THREE.DirectionalLight(0xffffff, 2.6);
  sun.position.set(-120, 260, 220);
  sun.castShadow = true;
  sun.shadow.mapSize.set(2048, 2048);
  Object.assign(sun.shadow.camera,
    {left: -220, right: 220, top: 220, bottom: -220, near: 10, far: 800});
  sun.shadow.radius = 6;
  sun.shadow.bias = -.0006;
  scene.add(sun);
  const rim = new THREE.DirectionalLight(0x38d6ff, 1.1);
  rim.position.set(200, 80, -200);
  scene.add(rim);
  const fill = new THREE.DirectionalLight(0xffffff, .9);
  fill.position.set(60, 40, 300);
  scene.add(fill);
  const ground = new THREE.Mesh(new THREE.PlaneGeometry(3000, 3000),
    new THREE.ShadowMaterial({opacity: .45}));
  ground.rotation.x = -Math.PI / 2;
  ground.position.y = -.2;
  ground.receiveShadow = true;
  scene.add(ground);

  const mCase = mat(0x2a2f39, {roughness: .82});
  const mGold = mat(0xe2b64a, {metalness: .9, roughness: .3});
  const mBlk = mat(0x0c0d10, {roughness: .5});
  const mWh = mat(0xe9e4d6);
  const mPcb = mat(0x2f6253, {roughness: .55});
  const mPcb2 = mat(0x16843a, {roughness: .55});
  const box = (w, h, d, m, x, y, z, r, par) => {
    const g = r ? new RoundedBoxGeometry(w, h, d, 3, r)
                : new THREE.BoxGeometry(w, h, d);
    const o = new THREE.Mesh(g, m);
    o.position.set(x, y, z);
    o.castShadow = o.receiveShadow = true;
    par.add(o);
    return o;
  };
  const cyl = (r1, r2, len, m, x, y, z, par) => {
    const o = new THREE.Mesh(new THREE.CylinderGeometry(r1, r2, len, 40), m);
    o.rotation.x = Math.PI / 2;
    o.position.set(x, y, z);
    o.castShadow = true;
    par.add(o);
    return o;
  };

  // Tub: floor, three plain walls, and the connector wall with two holes.
  const tub = new THREE.Group();
  scene.add(tub);
  box(W, TH, D, mCase, 0, TH / 2, 0, 1, tub);
  box(W, H, TH, mCase, 0, H / 2, -D / 2 + TH / 2, 1, tub);
  box(TH, H, D, mCase, -W / 2 + TH / 2, H / 2, 0, 1, tub);
  box(TH, H, D, mCase, W / 2 - TH / 2, H / 2, 0, 1, tub);
  const sh = new THREE.Shape();
  sh.moveTo(-W / 2, 0);
  sh.lineTo(W / 2, 0);
  sh.lineTo(W / 2, H);
  sh.lineTo(-W / 2, H);
  sh.closePath();
  for (const c of Object.values(CONNECTORS)) {
    const h = new THREE.Path();
    h.moveTo(c.x - OW / 2, OY - OH / 2);
    h.lineTo(c.x + OW / 2, OY - OH / 2);
    h.lineTo(c.x + OW / 2, OY + OH / 2);
    h.lineTo(c.x - OW / 2, OY + OH / 2);
    h.closePath();
    sh.holes.push(h);
  }
  const fw = new THREE.Mesh(
    new THREE.ExtrudeGeometry(sh, {depth: TH, bevelEnabled: false}), mCase);
  fw.position.z = D / 2 - TH;
  fw.castShadow = fw.receiveShadow = true;
  tub.add(fw);
  for (const c of Object.values(CONNECTORS)) {
    box(11, 8.4, 2.6, mBlk, c.x, OY, D / 2 - 11.5, 0, tub);
    for (let p = 0; p < 4; p++) {
      for (let r = 0; r < 3; r++) {
        box(.7, .7, 9, mGold, c.x + (p - 1.5) * PITCH,
            OY + (1 - r) * PITCH, D / 2 - 6, 0, tub);
      }
    }
  }
  // XT60, the USB-C jack, the side opening and the emergency stop.
  const mY = mat(0xf2b21d, {roughness: .55});
  box(27, 10, 3, mY, -72, 12, D / 2 + .4, 1.2, tub);
  box(15, 7.4, 3.4, mat(0xd89a10), -72, 12, D / 2 + .9, 1, tub);
  cyl(1.7, 1.7, 1, mBlk, -75.4, 12, D / 2 + 2.3, tub);
  cyl(1.7, 1.7, 1, mBlk, -68.6, 12, D / 2 + 2.3, tub);
  box(16, 8, 16, mY, -72, 12, D / 2 - 9, 1, tub);
  cyl(6.2, 6.2, 2.4, mat(0x8d939d, {metalness: .5, roughness: .4}),
      -42, 11, D / 2 + .2, tub).scale.set(1.75, 1, 1);
  box(9, 2.8, 2.6, mBlk, -42, 11, D / 2 + .5, 1.2, tub);
  box(22, 10, 30, mat(0x9a9fa8), -42, 11, D / 2 - 17, 2, tub);
  box(.6, 5, 11, mBlk, W / 2 + .05, 9, 36, 0, tub);
  cyl(9.8, 9.8, 5, mat(0x15171b), -58, 16, -D / 2 - 2.5, tub);
  cyl(12.4, 11, 12, mat(0xd0122a, {roughness: .35, metalness: .2}),
      -58, 16, -D / 2 - 11, tub);
  // rbmod carrier with the RP2350-CAN module.
  box(82, 1.6, 74, mPcb, 46, 8, 20, 0, tub);
  box(52, 1.2, 21, mat(0x1b1e24), 40, 11.4, 8, 0, tub);
  box(9, 1.4, 9, mBlk, 36, 12.6, 8, 0, tub);
  box(9, 3.2, 7, mat(0xb9bec7, {metalness: .7, roughness: .35}),
      61, 12.6, 8, 0, tub);
  for (let i = 0; i < 2; i++) {
    for (let j = 0; j < 20; j++) {
      box(.6, 2.4, .6, mGold, 15.9 + j * PITCH, 9.6, -1.4 + i * 18.8, 0, tub);
    }
  }
  for (const [z, len] of [[-2, 12], [9, 8], [21, 12], [32, 8], [44, 8]]) {
    box(5.5, 5, len, mWh, 82.5, 11.3, z, 0, tub);
  }
  // PD mini.
  box(40, 1.6, 61, mPcb2, -22, 9, -8, 1, tub);
  box(13, 2.2, 26, mat(0x05070a, {roughness: .2}), -28, 10.9, -14, 0, tub);
  box(13, 2.2, 5, mat(0x2540c8), -28, 10.95, 1.5, 0, tub);
  for (let i = 0; i < 4; i++) {
    box(4.4, 2.6, 4.4, mat(0xcfd3da, {metalness: .6}),
        -36 + i * 7.4, 11.1, -34, 0, tub);
  }
  box(10, 6, 9, mat(0x7bbf4e), -10, 12.8, 17, 0, tub);
  box(9, 4, 9, mat(0x7f858e, {metalness: .5}), -13, 11.8, -2, 0, tub);

  // Lid: glass, the screen's image and the panel board under it.
  const lid = new THREE.Group();
  scene.add(lid);
  box(W, 4, D, mat(0x0a0c10, {roughness: .3, metalness: .1}),
      0, H + 2, 0, 1.6, lid);
  if (displayImage) {
    const tex = new THREE.Texture(displayImage);
    tex.colorSpace = THREE.SRGBColorSpace;
    tex.anisotropy = 8;
    tex.needsUpdate = true;
    const scr = new THREE.Mesh(new THREE.PlaneGeometry(156, 91.4),
      new THREE.MeshBasicMaterial({map: tex, toneMapped: false}));
    scr.rotation.x = -Math.PI / 2;
    scr.position.set(0, H + 4.06, 0);
    lid.add(scr);
  }
  box(172, 1.6, 104, mPcb2, 0, H - 3, 0, 0, lid);
  box(60, 5, 22, mat(0x1f2a28), -20, H - 6.4, -10, 0, lid);

  // Servo: plug, three wires, body and horn; rebuilt per plug position.
  const servo = new THREE.Group();
  scene.add(servo);
  let plugX = 0;
  const setServoPort = (connector, position) => {
    servo.clear();
    plugX = CONNECTORS[connector].x + (position - 2.5) * PITCH;
    box(2.5, 7.8, 13, mBlk, plugX, OY, D / 2 + 7.6, .4, servo);
    WIRE_COLORS.forEach((c, r) => {
      const y = OY + (1 - r) * PITCH, z = D / 2 + r * 1.6;
      const cv = new THREE.CatmullRomCurve3([
        new THREE.Vector3(plugX, y, D / 2 + 13),
        new THREE.Vector3(plugX, y, D / 2 + 30),
        new THREE.Vector3(plugX + 18, 1.2, z + 58),
        new THREE.Vector3(plugX + 52, 1.2, z + 70),
        new THREE.Vector3(plugX + 86, 6, z + 62)]);
      const m = new THREE.Mesh(new THREE.TubeGeometry(cv, 60, .75, 8),
                               mat(c));
      m.castShadow = true;
      servo.add(m);
    });
    const mS = mat(0x1d2733, {roughness: .4, metalness: .5});
    box(40, 36, 20, mS, plugX + 106, 18, D / 2 + 62, 2, servo);
    box(54, 2.4, 20, mS, plugX + 106, 28, D / 2 + 62, .8, servo);
    const hub = new THREE.Mesh(new THREE.CylinderGeometry(3, 3, 5, 24), mGold);
    hub.position.set(plugX + 96, 38, D / 2 + 62);
    servo.add(hub);
    box(7, 2, 30, mWh, plugX + 96, 41, D / 2 + 62, .9, servo);
    servo.traverse(o => { if (o.material) o.material.transparent = true; });
  };
  servo.visible = false;

  // Named points a storyboard label can point at.
  const anchors = {
    display: () => [-30, H + 4 + lid.position.y, -30],
    rbmod: () => [46, 9, 22],
    pdmini: () => [-24, 11, -8],
    J1: () => [CONNECTORS.J1.x, OY, D / 2],
    J2: () => [CONNECTORS.J2.x, OY, D / 2],
    xt60: () => [-72, 12, D / 2 + 2],
    usbc: () => [-42, 11, D / 2 + 1.5],
    estop: () => [-58, 16, -D / 2 - 17],
    side: () => [W / 2, 9, 36],
    plug: () => [plugX, OY, D / 2 + 10 + servo.position.z],
  };
  return {
    scene, cam, anchors, setServoPort,
    setLid: mm => { lid.position.y = mm; },
    // alpha fades the servo in; out is how far the plug is pulled, in mm.
    setServo: (alpha, out) => {
      servo.visible = alpha > 0;
      servo.position.z = out;
      servo.traverse(o => { if (o.material) o.material.opacity = alpha; });
    },
  };
}
