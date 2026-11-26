// The Wurstblinker node as a cartoon: a remote node on a stand, a servo and
// a sausage on the servo's arm.  The scene is a joke; no part of it is a
// model of hardware.  Units are mm, y up, z towards the camera.  Every
// surface is cel shaded in three tones and carries a dark outline, an
// inverted hull 1.8mm outside it, before a pale disc that sets them off.
// setSwing(k) turns the arm from k = 0, the sausage upright, to k = 1, the
// sausage out to the right by 90 degrees.  buildWurst(true) is the kinetic
// buildWurst(false, true) sets the node on a mast 620mm high and a dog on
// the ground under the sausage, which jumps 260mm with k.  buildWurst(true) is the kinetic
// node: a tank of compressed air and a barrel in the servo's place.
// setFire(k) sends the sausage out of the barrel, from k = 0, loaded, to
// k = 0.6, where its end meets a traffic light 987mm from the muzzle and
// the amber lamp takes over from the red one, and on to k = 1, where it
// lies on the ground 260mm back after 1 turn in the air.
//
// SPDX-License-Identifier: MIT
import * as THREE from 'three';
import {RoundedBoxGeometry} from 'three/addons/geometries/RoundedBoxGeometry.js';

const INK = 0x10131c, EDGE = 1.8, HIT = .6;

export function buildWurst(kinetic = false, dog = false) {
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

  const world = (obj, x, y, z) => () => {
    scene.updateMatrixWorld(true);
    return obj.localToWorld(new THREE.Vector3(x, y, z)).toArray();
  };
  const idle = {setLid: () => {}, setServoPort: () => {}, setServo: () => {}};
  const fixed = {node: () => [-70, 82, 21], antenna: () => [-108, 152, -30]};
  const knots = w => {
    for (const y of [-90, 90]) {
      add(new THREE.SphereGeometry(4.5, 16, 12), 0x7a3a1e, w, 0, y, 0);
    }
  };

  if (kinetic) {
    // Tank on two posts, a pipe up to the breech, and the barrel along x.
    for (const x of [20, 90]) add(rod(6, 44), 0x8a93a6, scene, x, 36, -10);
    const tank = add(new THREE.CapsuleGeometry(26, 80, 12, 32), 0xe23b3b,
                     scene, 55, 78, -10);
    tank.rotation.z = Math.PI / 2;
    const dial = add(rod(11, 4), 0xf4f1e6, scene, 55, 80, 18);
    dial.rotation.x = Math.PI / 2;
    add(box(3, 9, 2, .8), 0x232833, scene, 57, 83, 21).rotation.z = -.6;
    add(rod(5, 34), 0x8a93a6, scene, 10, 116, -10);
    add(box(26, 22, 26, 3), 0xf2c84b, scene, 10, 134, -10);
    const barrel = add(rod(21, 210), 0x8a93a6, scene, 118, 134, -10);
    barrel.rotation.z = Math.PI / 2;
    for (const x of [30, 222]) {
      const ring = add(new THREE.TorusGeometry(22, 3.2, 12, 40), 0x5f6a80,
                       scene, x, 134, -10);
      ring.rotation.y = Math.PI / 2;
    }
    const bore = new THREE.Mesh(new THREE.CircleGeometry(16.5, 40),
      new THREE.MeshBasicMaterial({color: INK}));
    bore.rotation.y = Math.PI / 2;
    bore.position.set(223.4, 134, -10);
    scene.add(bore);
    // A traffic light 987mm from the muzzle, before a second pale disc.
    // It turns about its foot when the sausage hits it.
    const far = new THREE.Mesh(new THREE.CircleGeometry(300, 96),
      new THREE.MeshBasicMaterial({color: 0xfff1c2}));
    far.position.set(1150, 150, -160);
    scene.add(far);
    const light = new THREE.Group();
    light.position.set(1240, 0, -10);
    scene.add(light);
    add(rod(34, 8), 0x232833, light, 0, 4, 0);
    add(rod(6, 64), 0x8a93a6, light, 0, 40, 0);
    add(box(60, 170, 50, 8), 0x2b303b, light, 0, 150, 0);
    const lamps = [[205, 0xff3b4e], [150, 0xffb000], [95, 0x2fd06b]]
      .map(([y, c]) => {
        const m = add(new THREE.SphereGeometry(18, 24, 16), c, light,
                      0, y, 20);
        m.material.emissive = new THREE.Color(c);
        return m;
      });
    const shot = new THREE.Group();
    scene.add(shot);
    const wurst = add(new THREE.CapsuleGeometry(14, 152, 12, 32), 0xc8693a,
                      shot);
    knots(wurst);
    // Four puffs at the muzzle, there for the first fifth of the way.
    const puffs = [[0, 0, 0, 30], [22, 20, 8, 22], [26, -16, -6, 20],
                   [48, 4, 4, 16]].map(([x, y, z, r]) => {
      const m = add(new THREE.SphereGeometry(r, 20, 14), 0xffffff, scene,
                    250 + x, 134 + y, -10 + z);
      return m;
    });
    const setFire = k => {
      // To HIT the sausage flies; from HIT on it bounces back and falls.
      const u = Math.min(k / HIT, 1), v = Math.max((k - HIT) / (1 - HIT), 0);
      shot.position.set(120 + 1000 * u - 260 * v,
                        134 - 14 * u * u + 160 * v - 266 * v * v, -10);
      shot.rotation.z = -Math.PI / 2 + 2 * Math.PI * v;
      shot.visible = k > 0;
      lamp.material.emissiveIntensity = k > 0 ? 1 : 0;
      light.rotation.z = -.12 * Math.exp(-3 * v) * Math.sin(14 * v);
      lamps.forEach((m, i) => {
        m.material.emissiveIntensity = (v > 0 ? i === 1 : i === 0) ? .9 : 0;
      });
      const puff = k > 0 && k < .2 ? Math.sin(Math.PI * k / .2) : 0;
      for (const m of puffs) {
        m.visible = puff > .25;
        m.scale.setScalar(Math.max(puff, .25));
      }
    };
    setFire(0);
    return {
      scene, cam, setFire, ...idle,
      anchors: {...fixed, tank: () => [55, 78, 16],
                barrel: () => [150, 134, 11],
                light: () => [1240, 205, 28],
                sausage: world(wurst, 0, 0, 14)},
    };
  }

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
  knots(wurst);
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

  // The dog faces the node and jumps as the sausage comes down.
  // With the dog the node stands on a mast, DEEP above the ground.
  const DEEP = 620;
  const hound = new THREE.Group();
  hound.position.set(250, -DEEP, 10);
  hound.visible = dog;
  scene.add(hound);
  if (dog) {
    add(rod(12, DEEP), 0x8a93a6, scene, 0, -DEEP / 2, 0);
    add(rod(60, 10), 0x3a4a72, scene, 0, 5 - DEEP, 0);
    ground.position.y = -DEEP;
    const low = new THREE.Mesh(new THREE.CircleGeometry(560, 96),
      new THREE.MeshBasicMaterial({color: 0xfff1c2}));
    low.position.set(120, -330, -150);
    scene.add(low);
  }
  const FUR = 0xb9793f, EAR = 0x6b3f1e;
  add(new THREE.CapsuleGeometry(26, 70, 12, 32), FUR, hound, 0, 62, 0)
    .rotation.z = Math.PI / 2;
  add(new THREE.SphereGeometry(24, 24, 16), FUR, hound, -62, 94, 0);
  add(box(32, 16, 18, 5), FUR, hound, -90, 88, 0);
  add(new THREE.SphereGeometry(5, 16, 12), INK, hound, -107, 91, 0);
  add(new THREE.SphereGeometry(3.5, 12, 8), INK, hound, -74, 102, 20);
  for (const z of [-17, 17]) {
    add(box(11, 28, 6, 2.5), EAR, hound, -50, 104, z).rotation.z = .5;
    for (const x of [-34, 36]) add(rod(6, 40), FUR, hound, x, 20, z * .8);
  }
  add(rod(4, 38), FUR, hound, 64, 84, 0).rotation.z = -.8;

  const setSwing = k => {
    hound.position.y = 260 * k - (dog ? DEEP : 0);
    hound.rotation.z = -.35 - .45 * k;
    pivot.rotation.z = -Math.PI / 2 * k;
    lamp.material.emissiveIntensity = k;
    strokes.visible = k > .85;
  };
  setSwing(0);
  return {
    scene, cam, setSwing, ...idle,
    anchors: {...fixed, servo: () => [40, 118, 3],
              dog: world(hound, -62, 94, 24),
              sausage: world(wurst, 0, 40, 14)},
  };
}
