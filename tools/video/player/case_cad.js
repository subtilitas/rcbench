// The bench from its CAD (computer-aided design) assembly: the model
// tools/video_cad.py writes from the case's STEP file.  Units are mm.  The
// assembly is laid so that the display faces up, x runs along the 172mm
// side, z towards the connector wall and y = 0 is the case's
// underside.
//
// The STEP file carries no colours: each part gets its colour from the
// names of the assembly above it (PARTS, first match).  The meshes are
// merged per colour within each component (UNITS), so that a frame is a
// few dozen draw calls and not 2458.
//
// Each component can stand off its seat: setParts() takes a number per
// component, 0 seated and 1 fully out along the component's own way in
// (AWAY), turned a little about its centre and faded at the far end.  A
// storyboard assembles the bench with it.  The case is closed at the back:
// the display goes into its front last.
//
// SPDX-License-Identifier: MIT
import * as THREE from 'three';
import {GLTFLoader} from 'three/addons/loaders/GLTFLoader.js';
import {mergeGeometries} from 'three/addons/utils/BufferGeometryUtils.js';

// The assembly's origin in mm: its x and y centre and its lowest face.
const CX = 82, CY = 47, CZ = 28;
const TOP = 37;        // the display's glass above the ground

// [pattern on "parent/child/..." names, colour, roughness, metalness].
const PARTS = [
  [/case_front/, 0x1b1e24, .85, 0],
  [/XT60EF_base/, 0xf2b21d, .55, 0],
  [/XT60EF_female/, 0xd9a93f, .3, .9],
  [/usbc-Female/, 0x9ba1ab, .35, .7],
  [/estop_m16/, 0x8d939d, .4, .7],
  [/7INCH-1024X600-LCD/, 0x07090c, .25, .1],
  [/rbmod_PCB/, 0x1f5446, .55, 0],
  [/rbmod_copper/, 0x2c6a58, .5, .2],
  [/rbmod_pad|Terminal|PinHeader/, 0xd9b04a, .3, .9],
  [/RP2350-CAN/, 0x1d4fa8, .5, .1],
  [/INA3221_Module/, 0x5a2d82, .5, .1],
  [/DCDC_Buck|USBC_PD_Trigger/, 0x15803a, .5, .1],
  [/JST_PH/, 0xe9e4d6, .6, 0],
  [/R_Axial|D_DO-34/, 0xb9a27a, .6, 0],
];
// The components: [name, pattern on the path, the way out in mm, the turn
// in rad about x, y and z at the far end].  First match; the last takes
// what is left.
const UNITS = [
  ['lid', /ESP32-P4-WIFI6/, [0, 300, 0], [.25, 0, 0]],
  ['estop', /estop_m16/, [0, 30, -230], [0, 0, 1.2]],
  ['xt60', /XT60EF/, [-60, 20, 250], [0, .6, 0]],
  ['usbc', /usbc-Female/, [20, 40, 250], [0, -.5, 0]],
  ['pdmini', /rcbench_case\/PCB\//, [-130, 230, 30], [.3, .5, 0]],
  ['mcu', /RP2350-CAN/, [-40, 150, -70], [.4, 0, .3]],
  ['ina', /INA3221_Module/, [90, 140, 70], [0, .5, -.3]],
  ['buck', /DCDC_Buck/, [30, 200, 10], [-.3, .4, 0]],
  ['trigger', /USBC_PD_Trigger/, [130, 120, -60], [.3, -.4, 0]],
  ['rbmod', /rbmod_1/, [150, 230, 0], [.2, -.3, 0]],
  ['case', /./, [0, 190, -260], [-.35, .5, 0]],
];
const PLATE = [0x12703a, .55, 0];      // a board without a name
const OTHER = [0x23262c, .5, .15];     // a component without a name

export function loadCad(url) {
  return new Promise((ok, bad) => new GLTFLoader().load(url, ok, undefined,
    () => bad(new Error('missing ' + url))));
}

export function buildCadCase(displayImage, gltf) {
  const scene = new THREE.Scene();
  const cam = new THREE.PerspectiveCamera(30, 16 / 9, 5, 3000);
  scene.add(new THREE.HemisphereLight(0xcfe3ff, 0x10151f, 1.35));
  const sun = new THREE.DirectionalLight(0xffffff, 2.6);
  sun.position.set(-120, 260, 220);
  sun.castShadow = true;
  sun.shadow.mapSize.set(2048, 2048);
  Object.assign(sun.shadow.camera,
    {left: -240, right: 240, top: 240, bottom: -240, near: 10, far: 900});
  sun.shadow.radius = 6;
  sun.shadow.bias = -.0008;
  scene.add(sun);
  const rim = new THREE.DirectionalLight(0x38d6ff, 1.1);
  rim.position.set(200, 80, -200);
  scene.add(rim);
  const fill = new THREE.DirectionalLight(0xffffff, 1.0);
  fill.position.set(60, 40, 300);
  scene.add(fill);
  const ground = new THREE.Mesh(new THREE.PlaneGeometry(3000, 3000),
    new THREE.ShadowMaterial({opacity: .26}));
  ground.rotation.x = -Math.PI / 2;
  ground.position.y = -.2;
  ground.receiveShadow = true;
  scene.add(ground);

  // The writer keeps the assembly's axes and turns its mm into m; a
  // quarter turn about x puts the display, which faces -z there, on top.
  const lay = new THREE.Matrix4().makeTranslation(-CX, CZ, -CY)
    .multiply(new THREE.Matrix4().makeRotationX(Math.PI / 2))
    .multiply(new THREE.Matrix4().makeScale(1000, 1000, 1000));
  const src = gltf.scene;
  src.updateMatrixWorld(true);
  const units = {};
  for (const [name, , away, spin] of UNITS) {
    units[name] = {away, spin, looks: {}, box: new THREE.Box3(),
                   group: new THREE.Group(), mats: []};
  }
  const boxes = {};
  const stop = [];
  src.traverse(o => {
    if (!o.isMesh) return;
    let path = '';
    for (let p = o; p; p = p.parent) path = (p.name || '') + '/' + path;
    // The power bank of the assembly is not part of the bench shown.
    if (/powerbank/.test(path)) return;
    const g = o.geometry.clone();
    for (const name of Object.keys(g.attributes)) {
      if (name !== 'position' && name !== 'normal') g.deleteAttribute(name);
    }
    if (!g.attributes.normal) g.computeVertexNormals();
    g.applyMatrix4(new THREE.Matrix4().multiplyMatrices(lay, o.matrixWorld));
    g.computeBoundingBox();
    const size = g.boundingBox.getSize(new THREE.Vector3());
    const dims = [size.x, size.y, size.z].sort((a, b) => a - b);
    let look = PARTS.find(([re]) => re.test(path));
    look = look ? look.slice(1)
      : dims[0] < 2.6 && dims[1] > 30 ? PLATE : OTHER;
    const unit = units[UNITS.find(([, re]) => re.test(path))[0]];
    unit.box.union(g.boundingBox);
    if (/estop_m16/.test(path)) {
      stop.push(g);
    } else {
      (unit.looks[look.join()] ||= {look, list: []}).list.push(g);
    }
    if (path.includes('PinHeader_3x04')) {
      // Two headers: J2 nearer the USB-C socket, J1 nearer the corner.
      const k = g.boundingBox.getCenter(new THREE.Vector3()).x < 44 ? 'J2'
                                                                    : 'J1';
      (boxes[k] ||= new THREE.Box3()).union(g.boundingBox);
    }
  });
  // The emergency stop's parts carry numbers, not names: the two that
  // stand furthest out of the wall are the red head.
  const out = g => g.boundingBox.getCenter(new THREE.Vector3()).z;
  stop.sort((a, b) => out(a) - out(b)).forEach((g, n) => {
    const look = n < 2 ? [0xd0122a, .35, .2] : [0x8d939d, .4, .7];
    (units.estop.looks[look.join()] ||= {look, list: []}).list.push(g);
  });
  // Each component is a group at its box's centre, so that it turns about
  // itself; its meshes are moved back by that centre.
  for (const u of Object.values(units)) {
    u.home = u.box.getCenter(new THREE.Vector3());
    u.group.position.copy(u.home);
    scene.add(u.group);
    for (const {look, list} of Object.values(u.looks)) {
      const geo = mergeGeometries(list, false);
      geo.translate(-u.home.x, -u.home.y, -u.home.z);
      const m = new THREE.MeshStandardMaterial(
        {color: look[0], roughness: look[1], metalness: look[2],
         transparent: true, emissive: 0x00e5ff, emissiveIntensity: 0});
      const mesh = new THREE.Mesh(geo, m);
      mesh.castShadow = mesh.receiveShadow = true;
      u.mats.push(m);
      u.group.add(mesh);
    }
  }
  if (displayImage) {
    const tex = new THREE.Texture(displayImage);
    tex.colorSpace = THREE.SRGBColorSpace;
    tex.anisotropy = 8;
    tex.needsUpdate = true;
    const m = new THREE.MeshBasicMaterial(
      {map: tex, toneMapped: false, transparent: true});
    const scr = new THREE.Mesh(new THREE.PlaneGeometry(154.2, 90.4), m);
    scr.rotation.x = -Math.PI / 2;
    scr.position.set(0, TOP + .08, .5).sub(units.lid.home);
    units.lid.group.add(scr);
    units.lid.screen = m;
  }

  // A point of a component, in the scene: its box's centre, moved to one
  // face of the box where f says so (1 the high face, -1 the low one).
  // With a box of its own, a header, the point lies in that box.
  const at = (name, fx = 0, fy = 0, fz = 0, box = null) => () => {
    const u = units[name], b = box ? boxes[box] : u.box;
    if (!b || b.isEmpty()) return [0, 0, 0];
    const c = b.getCenter(new THREE.Vector3());
    const h = b.getSize(new THREE.Vector3()).multiplyScalar(.5);
    c.add(new THREE.Vector3(fx * h.x, fy * h.y, fz * h.z)).sub(u.home);
    return u.group.localToWorld(c).toArray();
  };
  const anchors = {
    display: at('lid', -.35, 1, -.4),
    panel: at('lid', .3, 0, -.4),
    case: at('case', -.9, .2, 1),
    rbmod: at('rbmod', .1, 0, .5),
    mcu: at('mcu'),
    ina: at('ina'),
    buck: at('buck'),
    trigger: at('trigger'),
    pdmini: at('pdmini'),
    J1: at('rbmod', 0, 0, 1, 'J1'),
    J2: at('rbmod', 0, 0, 1, 'J2'),
    xt60: at('xt60', 0, 0, 1),
    usbc: at('usbc', 0, 0, 1),
    estop: at('estop', 0, 0, -1),
    side: at('case', 1, 0, .4),
    plug: at('rbmod', 0, 0, 1, 'J1'),
  };
  let lift = 0;
  const state = {};
  const place = () => {
    for (const [name, u] of Object.entries(units)) {
      // The modules of the carrier ride on it: they are out by its way
      // as well as their own.
      const k = state[name] || 0;
      const ride = ['mcu', 'ina', 'buck', 'trigger'].includes(name)
        ? state.rbmod || 0 : 0;
      u.group.position.copy(u.home)
        .addScaledVector(new THREE.Vector3(...u.away), k)
        .addScaledVector(new THREE.Vector3(...units.rbmod.away), ride);
      if (name === 'lid') u.group.position.y += lift;
      u.group.rotation.set(u.spin[0] * k, u.spin[1] * k, u.spin[2] * k);
      const seen = 1 - Math.min(1, Math.max(0, (k - .8) / .2));
      // Opaque while seated or near: a transparent material is sorted,
      // not depth-tested against the others, and faces can drop out.
      for (const m of u.mats) {
        m.opacity = seen;
        if (m.transparent !== seen < 1) {
          m.transparent = seen < 1;
          m.needsUpdate = true;
        }
      }
      if (u.screen) u.screen.opacity = seen;
      u.group.visible = seen > 0;
    }
  };
  return {
    scene, cam, anchors,
    setServoPort: () => {},
    setServo: () => {},
    // The storyboard's "lid" in mm: the display and its board rise out of
    // the case's front by 1.4 times that, the case being 37mm deep.
    setLid: mm => { lift = 1.4 * mm; place(); },
    // How far out each component stands: {name: 0 to 1}.
    setParts: parts => { Object.assign(state, parts); place(); },
    // A component lit from inside, 0 to 1: the one a label names.
    setGlow: (name, amount) => {
      for (const m of (units[name] || {mats: []}).mats) {
        m.emissiveIntensity = Math.max(m.emissiveIntensity, .08 * amount);
      }
    },
    clearGlow: () => {
      for (const u of Object.values(units)) {
        for (const m of u.mats) m.emissiveIntensity = 0;
      }
    },
  };
}
