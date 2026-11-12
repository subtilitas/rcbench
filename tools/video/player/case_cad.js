// The bench from its CAD (computer-aided design) assembly: the model
// tools/video_cad.py writes from the case's STEP file.  Units are mm.  The
// assembly is laid so that the display faces up, x runs along the 172mm
// side, z towards the connector wall and y = 0 is the power bank's
// underside.
//
// The STEP file carries no colours: each part gets its colour from the
// names of the assembly above it (PARTS, first match).  The meshes are
// merged per colour, in two groups, the display with its board and the
// rest, so that a frame is two dozen draw calls and not 2458.  The case
// is closed at the back: the display lifts out of its front.
//
// SPDX-License-Identifier: MIT
import * as THREE from 'three';
import {GLTFLoader} from 'three/addons/loaders/GLTFLoader.js';
import {mergeGeometries} from 'three/addons/utils/BufferGeometryUtils.js';

// The assembly's origin in mm: its x and y centre and its lowest face.
const CX = 82, CY = 47, CZ = 46;
const TOP = 55;        // the display's glass above the ground

// [pattern on "parent/child/..." names, colour, roughness, metalness].
const PARTS = [
  [/powerbank/, 0x2b3038, .45, .55],
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
    new THREE.ShadowMaterial({opacity: .45}));
  ground.rotation.x = -Math.PI / 2;
  ground.position.y = -.2;
  ground.receiveShadow = true;
  scene.add(ground);

  // The writer keeps the assembly's axes and turns its mm into m; a
  // quarter turn about x puts the display, which faces -z there, on top.  to() maps a point of the
  // assembly's own coordinates the same way.
  const lay = new THREE.Matrix4().makeTranslation(-CX, CZ, -CY)
    .multiply(new THREE.Matrix4().makeRotationX(Math.PI / 2))
    .multiply(new THREE.Matrix4().makeScale(1000, 1000, 1000));
  const src = gltf.scene;
  src.updateMatrixWorld(true);
  const groups = {body: {}, lid: {}};
  const boxes = {};
  const stop = [];
  src.traverse(o => {
    if (!o.isMesh) return;
    let path = '';
    for (let p = o; p; p = p.parent) path = (p.name || '') + '/' + path;
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
    const part = /ESP32-P4-WIFI6/.test(path) ? 'lid' : 'body';
    if (/estop_m16/.test(path)) {
      stop.push(g);
    } else {
      (groups[part][look.join()] ||= {look, list: []}).list.push(g);
    }
    // The box of each named assembly, for the anchors.
    for (const key of ['rbmod_1', 'XT60EF', 'usbc-Female', 'powerbank',
                       'estop_m16_assembly', 'PinHeader_3x04',
                       '/PCB/', 'PCB_ASM']) {
      if (!path.includes(key)) continue;
      let k = key;
      if (key === 'PinHeader_3x04') {
        // Two headers: J2 nearer the USB-C socket, J1 nearer the corner.
        k = g.boundingBox.getCenter(new THREE.Vector3()).x < 44 ? 'J2' : 'J1';
      }
      (boxes[k] ||= new THREE.Box3()).union(g.boundingBox);
    }
  });
  // The emergency stop's parts carry numbers, not names: the two that
  // stand furthest out of the wall are the red head.
  const out = g => g.boundingBox.getCenter(new THREE.Vector3()).z;
  stop.sort((a, b) => out(a) - out(b)).forEach((g, n) => {
    const look = n < 2 ? [0xd0122a, .35, .2] : [0x8d939d, .4, .7];
    (groups.body[look.join()] ||= {look, list: []}).list.push(g);
  });
  const body = new THREE.Group(), lid = new THREE.Group();
  scene.add(body, lid);
  for (const [part, into] of [['body', body], ['lid', lid]]) {
    for (const {look, list} of Object.values(groups[part])) {
      const mesh = new THREE.Mesh(mergeGeometries(list, false),
        new THREE.MeshStandardMaterial(
          {color: look[0], roughness: look[1], metalness: look[2]}));
      mesh.castShadow = mesh.receiveShadow = true;
      into.add(mesh);
    }
  }
  if (displayImage) {
    const tex = new THREE.Texture(displayImage);
    tex.colorSpace = THREE.SRGBColorSpace;
    tex.anisotropy = 8;
    tex.needsUpdate = true;
    const scr = new THREE.Mesh(new THREE.PlaneGeometry(154.2, 90.4),
      new THREE.MeshBasicMaterial({map: tex, toneMapped: false}));
    scr.rotation.x = -Math.PI / 2;
    scr.position.set(0, TOP + .08, .5);
    lid.add(scr);
  }

  // A point of a named part, in the scene: its box's centre, moved to one
  // face of the box where f says so (1 the high face, -1 the low one).
  const at = (key, fx = 0, fy = 0, fz = 0, from = body) => () => {
    const b = boxes[key];
    if (!b) return [0, 0, 0];
    const c = b.getCenter(new THREE.Vector3());
    const h = b.getSize(new THREE.Vector3()).multiplyScalar(.5);
    c.add(new THREE.Vector3(fx * h.x, fy * h.y, fz * h.z));
    return from.localToWorld(c).toArray();
  };
  const anchors = {
    display: () => lid.localToWorld(new THREE.Vector3(-30, TOP, -20))
      .toArray(),
    rbmod: at('rbmod_1'),
    pdmini: at('/PCB/'),
    panel: at('PCB_ASM', .3, 0, -.4, lid),
    powerbank: at('powerbank', 0, 0, 1),
    J1: at('J1', 0, 0, 1),
    J2: at('J2', 0, 0, 1),
    xt60: at('XT60EF', 0, 0, 1),
    usbc: at('usbc-Female', 0, 0, 1),
    estop: at('estop_m16_assembly', 0, 0, -1),
    side: () => body.localToWorld(new THREE.Vector3(86, 30, 20)).toArray(),
    plug: at('J1', 0, 0, 1),
  };
  return {
    scene, cam, anchors,
    setServoPort: () => {},
    setServo: () => {},
    // The storyboard's "lid" in mm: the display and its board rise out of
    // the case's front by 1.4 times that, the case being 37mm deep.
    setLid: mm => { lid.position.y = 1.4 * mm; },
  };
}
