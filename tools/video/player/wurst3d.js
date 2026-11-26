// The Wurstblinker node as a cartoon: a remote node on a stand, a servo and
// a sausage on the servo's arm.  The scene is a joke; no part of it is a
// model of hardware.  Units are mm, y up, z towards the camera.  Every
// surface is cel shaded in three tones and carries a dark outline, an
// inverted hull 1.8mm outside it, before a pale disc that sets them off.
// setSwing(k) turns the arm from k = 0, the sausage upright, to k = 1, the
// sausage out to the right by 90 degrees.
//
// SPDX-License-Identifier: MIT
import * as THREE from 'three';
import {RoundedBoxGeometry} from 'three/addons/geometries/RoundedBoxGeometry.js';

const INK = 0x10131c, EDGE = 1.8;

export function buildWurst() {
  const scene = new THREE.Scene();
  const cam = new THREE.PerspectiveCamera(30, 16 / 9, 5, 3000);
  scene.add(new THREE.AmbientLight(0xffffff, 1.5));
  const sun = new THREE.DirectionalLight(0xffffff, 2.4);
  sun.position.set(-160, 300, 260);
  sun.castShadow = true;
  sun.shadow.mapSize.set(2048, 2048);
  Object.assign(sun.shadow.camera,
    {left: -300, right: 300, top: 300, bottom: -300, near: 10, far: 900});
  scene.add(sun);
  const ground = new THREE.Mesh(new THREE.PlaneGeometry(3000, 3000),
    new THREE.ShadowMaterial({opacity: .5}));
  ground.rotation.x = -Math.PI / 2;
  ground.receiveShadow = true;
  scene.add(ground);

  // A pale disc behind the scene: the outlines read against it.
  const disc = new THREE.Mesh(new THREE.CircleGeometry(360, 96),
    new THREE.MeshBasicMaterial({color: 0xfff1c2}));
  disc.position.set(90, 150, -140);
  scene.add(disc);

  // Three tones: shade, body, light.
  const ramp = new THREE.DataTexture(new Uint8Array([110, 190, 255]), 3, 1,
                                     THREE.RedFormat);
  ramp.minFilter = ramp.magFilter = THREE.NearestFilter;
  ramp.needsUpdate = true;
  const toon = c => new THREE.MeshToonMaterial({color: c, gradientMap: ramp});
  const ink = new THREE.MeshBasicMaterial({color: INK, side: THREE.BackSide});
  // A mesh and its outline: the same shape, each vertex moved out along
  // its normal, drawn from the inside.
  const add = (geo, color, par, x = 0, y = 0, z = 0) => {
    const mesh = new THREE.Mesh(geo, toon(color));
    mesh.position.set(x, y, z);
    mesh.castShadow = true;
    const hull = geo.clone();
    const p = hull.attributes.position, n = hull.attributes.normal;
    for (let i = 0; i < p.count; i++) {
      p.setXYZ(i, p.getX(i) + n.getX(i) * EDGE, p.getY(i) + n.getY(i) * EDGE,
               p.getZ(i) + n.getZ(i) * EDGE);
    }
    mesh.add(new THREE.Mesh(hull, ink));
    par.add(mesh);
    return mesh;
  };
  const box = (w, h, d, r) => new RoundedBoxGeometry(w, h, d, 4, r);
  const rod = (r, len) => new THREE.CylinderGeometry(r, r, len, 32);

  // Stand, node board, radio module, antenna and lamp.
  add(box(230, 14, 110, 5), 0x3a4a72, scene, 0, 7, 0);
  add(rod(7, 60), 0x8a93a6, scene, -70, 44, -10);
  add(box(96, 6, 62, 2), 0x2fa36b, scene, -70, 77, -10);
  add(box(30, 8, 26, 2), 0xc9d2e0, scene, -88, 84, -6);
  add(box(14, 6, 14, 1.5), 0x232833, scene, -52, 83, -18);
  add(rod(1.8, 70), 0x232833, scene, -108, 115, -30);
  add(new THREE.SphereGeometry(6, 24, 16), 0xff3b4e, scene, -108, 152, -30);
  const lamp = add(new THREE.SphereGeometry(7, 24, 16), 0xffb000, scene,
                   -40, 86, 8);
  lamp.material.emissive = new THREE.Color(0xffb000);

  // Servo on a post, its shaft towards the camera.
  add(rod(9, 86), 0x8a93a6, scene, 40, 57, -10);
  add(box(46, 44, 26, 3), 0x2f6fd6, scene, 40, 118, -10);
  add(box(66, 5, 26, 2), 0x2f6fd6, scene, 40, 130, -10);
  const hub = add(rod(7, 10), 0xf2c84b, scene, 40, 128, 8);
  hub.rotation.x = Math.PI / 2;

  // The arm turns about the shaft: a strip, two clips and the sausage.
  const pivot = new THREE.Group();
  pivot.position.set(40, 128, 16);
  scene.add(pivot);
  add(box(12, 96, 4, 1.5), 0xf4f1e6, pivot, 0, 44, 0);
  const wurst = add(new THREE.CapsuleGeometry(14, 152, 12, 32), 0xc8693a,
                    pivot, 0, 104, 12);
  for (const y of [-90, 90]) {
    add(new THREE.SphereGeometry(4.5, 16, 12), 0x7a3a1e, wurst, 0, y, 0);
  }
  for (const y of [56, 92]) {
    const clip = add(new THREE.TorusGeometry(15.5, 2.2, 12, 32), 0xf4f1e6,
                     pivot, 0, y, 12);
    clip.rotation.x = Math.PI / 2;
  }
  // Three strokes beside the sausage's end while it is out.
  const strokes = new THREE.Group();
  for (const [a, len] of [[-.5, 26], [0, 34], [.5, 26]]) {
    const s = new THREE.Mesh(box(len, 5, 3, 1.2),
                             new THREE.MeshBasicMaterial({color: 0xffb000}));
    s.position.set(40 + 222 + Math.cos(a) * 20, 128 + Math.sin(a) * 44, 28);
    s.rotation.z = a;
    strokes.add(s);
  }
  scene.add(strokes);

  const world = (obj, x, y, z) => () => {
    scene.updateMatrixWorld(true);
    return obj.localToWorld(new THREE.Vector3(x, y, z)).toArray();
  };
  const setSwing = k => {
    pivot.rotation.z = -Math.PI / 2 * k;
    lamp.material.emissiveIntensity = k;
    strokes.visible = k > .85;
  };
  setSwing(0);
  return {
    scene, cam, setSwing,
    anchors: {
      node: () => [-70, 82, 21],
      antenna: () => [-108, 152, -30],
      servo: () => [40, 118, 3],
      sausage: world(wurst, 0, 40, 14),
    },
    setLid: () => {}, setServoPort: () => {}, setServo: () => {},
  };
}
