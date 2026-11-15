// The MiniRcBench from its CAD (computer-aided design) assembly: the file
// tools/video_cad.py writes with --parts, one node a component in its
// solids' colours.  Units are mm.  The bench lies on its back: the
// assembly's z axis, out of the glass, stands up as y, its y axis, up the
// portrait display, runs to -z, and y = 0 is the case's back.  The wall
// with the USB-C opening and the two lead openings faces +z.
//
// The case and its 3 buttons have no colour in the assembly and take one
// here.  The two male servo connectors in the case's pockets and the 4 screws are
// not in the assembly: they are drawn here as plain solids.
//
// Each component can stand off its seat: setParts() takes a number per
// component, 0 seated and 1 fully out along its own way in.  setFlip()
// rolls the whole bench over, so that its back shows.
//
// SPDX-License-Identifier: MIT
import * as THREE from 'three';

const BACK = 24;            // the case's back under the glass's front
const GLASS = .75;          // the glass's front over the assembly's z = 0
const WINDOW = [43.8, 58.2];
const CASE = [0x4a566e, .8, .05];
const BUTTON = [0xf2a31d, .6, 0];

// [name, pattern on a node's name, the way out in mm, the turn in rad
// about x, y and z at the far end].  First match.
const UNITS = [
  ['shell', /^case_shell/, [0, 0, -140], [0, 0, 0]],
  ['buttons', /^button/, [-26, 46, 0], [0, 0, -.5]],
  ['ina', /^ina219/, [10, 74, 46], [.35, 0, 0]],
  ['board', /^rp2350/, [0, 130, 0], [-.18, 0, 0]],
  ['cell', /^lipo/, [-30, 96, -30], [0, .4, 0]],
  ['boost', /^boost/, [-50, 84, 20], [0, -.5, 0]],
];
const EXTRA = {
  plugs: [[0, 62, 40], [.3, 0, 0]],
  screws: [[0, -52, 0], [0, 0, 0]],
};

// A point of the assembly in the scene's axes.
const P = (x, y, z) => new THREE.Vector3(x, z + BACK, -y);

export function buildMini(gltf) {
  const scene = new THREE.Scene();
  const cam = new THREE.PerspectiveCamera(30, 16 / 9, 2, 3000);
  scene.add(new THREE.HemisphereLight(0xcfe3ff, 0x10151f, 1.45));
  const sun = new THREE.DirectionalLight(0xffffff, 2.6);
  sun.position.set(-90, 200, 150);
  sun.castShadow = true;
  sun.shadow.mapSize.set(2048, 2048);
  Object.assign(sun.shadow.camera,
    {left: -160, right: 160, top: 160, bottom: -160, near: 10, far: 700});
  sun.shadow.radius = 5;
  sun.shadow.bias = -.0008;
  scene.add(sun);
  const rim = new THREE.DirectionalLight(0x38d6ff, 1.1);
  rim.position.set(150, 60, -150);
  scene.add(rim);
  const fill = new THREE.DirectionalLight(0xffffff, 1.0);
  fill.position.set(50, 40, 200);
  scene.add(fill);
  const ground = new THREE.Mesh(new THREE.PlaneGeometry(3000, 3000),
    new THREE.ShadowMaterial({opacity: .28}));
  ground.rotation.x = -Math.PI / 2;
  ground.position.y = -.2;
  ground.receiveShadow = true;
  scene.add(ground);

  const root = new THREE.Group();
  scene.add(root);
  const lay = new THREE.Matrix4().makeTranslation(0, BACK, 0)
    .multiply(new THREE.Matrix4().makeRotationX(-Math.PI / 2))
    .multiply(new THREE.Matrix4().makeScale(1000, 1000, 1000));
  const units = {};
  const unit = (name, away, spin) => units[name] = {
    away, spin, box: new THREE.Box3(), group: new THREE.Group(), mats: [],
    meshes: []};
  for (const [name, , away, spin] of UNITS) unit(name, away, spin);
  for (const [name, [away, spin]] of Object.entries(EXTRA)) {
    unit(name, away, spin);
  }
  const add = (u, geo, look) => {
    geo.computeBoundingBox();
    u.box.union(geo.boundingBox);
    const dark = typeof look[0] !== 'number'
      && look[0].r + look[0].g + look[0].b < .2;
    const m = new THREE.MeshStandardMaterial({
      color: dark ? new THREE.Color(.05, .055, .07) : look[0],
      roughness: look[1], metalness: look[2],
      emissive: 0x00e5ff, emissiveIntensity: 0});
    u.mats.push(m);
    u.meshes.push(new THREE.Mesh(geo, m));
  };
  const src = gltf.scene;
  src.updateMatrixWorld(true);
  src.traverse(o => {
    if (!o.isMesh) return;
    // A component with several colours is a group of meshes: the
    // group carries the component's name.
    let found = null;
    for (let p = o; p && !found; p = p.parent) {
      found = UNITS.find(([, re]) => re.test(p.name || ''));
    }
    if (!found) return;
    const u = units[found[0]];
    let g = o.geometry.clone();
    g.applyMatrix4(new THREE.Matrix4().multiplyMatrices(lay, o.matrixWorld));
    if (found[0] === 'cell') {
      // The cell's lead is straight in its model and would leave the
      // case through its wall: only the triangles at the cell stay.
      g = g.toNonIndexed();
      const p = g.attributes.position.array, keep = [];
      for (let i = 0; i < p.length; i += 9) {
        if (Math.max(p[i + 2], p[i + 5], p[i + 8]) < 6) {
          keep.push(...p.slice(i, i + 9));
        }
      }
      g = new THREE.BufferGeometry();
      g.setAttribute('position',
                     new THREE.Float32BufferAttribute(keep, 3));
    }
    g.computeVertexNormals();
    const none = o.material.name === 'none';
    add(u, g, none ? (found[0] === 'buttons' ? BUTTON : CASE)
      : [o.material.color.clone(), .6, .1]);
  });
  // The two male connectors in the pockets under ESC and TMT: a shroud
  // open to the outside with 3 pins in it, the pin end of a servo
  // extension lead.  The shroud's mouth lies 1.2mm inside the wall's
  // face.
  for (const x of [-9.55, 9.55]) {
    const part = (w, h, len, dx, dz, y, look) => {
      const g = new THREE.BoxGeometry(w, h, len);
      g.translate(...P(x + dx, y, -19.95 + dz).toArray());
      add(units.plugs, g, look);
    };
    const BLACK = [0x1b1e24, .55, .1];
    part(8.4, 3.8, 8.2, 0, 0, -28.9, BLACK);         // the body
    part(8.4, .5, 6.8, 0, 1.65, -36.4, BLACK);       // the shroud's walls
    part(8.4, .5, 6.8, 0, -1.65, -36.4, BLACK);
    part(.5, 3.8, 6.8, -3.95, 0, -36.4, BLACK);
    part(.5, 3.8, 6.8, 3.95, 0, -36.4, BLACK);
    for (const dx of [-2.54, 0, 2.54]) {
      part(.64, .64, 6.0, dx, 0, -36.0, [0xd9b04a, .3, .9]);
    }
  }
  // 4 screws from the back into the board's standoffs.
  for (const [x, y] of [[-20.5, -32.9], [20.5, -32.9], [-20.5, 27.1],
                        [20.5, 27.1]]) {
    const head = new THREE.CylinderGeometry(2.4, 2.4, 1.6, 24);
    head.translate(...P(x, y, -12.6).toArray());
    add(units.screws, head, [0x9ba1ab, .35, .8]);
    const shaft = new THREE.CylinderGeometry(1.2, 1.2, 5, 16);
    shaft.translate(...P(x, y, -9.5).toArray());
    add(units.screws, shaft, [0x9ba1ab, .35, .8]);
  }
  for (const u of Object.values(units)) {
    u.home = u.box.getCenter(new THREE.Vector3());
    u.group.position.copy(u.home);
    root.add(u.group);
    for (const mesh of u.meshes) {
      mesh.geometry.translate(-u.home.x, -u.home.y, -u.home.z);
      mesh.castShadow = mesh.receiveShadow = true;
      u.group.add(mesh);
    }
  }
  // The display's picture, 240 x 320, on the glass's clear window.
  const face = new THREE.MeshBasicMaterial(
    {toneMapped: false, transparent: true, opacity: 0});
  const scr = new THREE.Mesh(new THREE.PlaneGeometry(...WINDOW), face);
  scr.rotation.x = -Math.PI / 2;
  scr.position.copy(P(0, 0, GLASS + .06)).sub(units.board.home);
  units.board.group.add(scr);
  let shown = null;
  const setScreen = image => {
    if (!image || image === shown) return;
    const tex = new THREE.Texture(image);
    tex.colorSpace = THREE.SRGBColorSpace;
    tex.magFilter = THREE.NearestFilter;
    tex.anisotropy = 8;
    tex.needsUpdate = true;
    if (face.map) face.map.dispose();
    face.map = tex;
    face.opacity = 1;
    face.needsUpdate = true;
    shown = image;
    scr.visible = units.board.group.visible;
  };

  // A point of the assembly that rides on a component.
  const at = (name, x, y, z) => () => units[name].group.localToWorld(
    P(x, y, z).sub(units[name].home)).toArray();
  const mid = name => () => units[name].group.localToWorld(
    new THREE.Vector3()).toArray();
  const anchors = {
    display: at('board', 0, 6, GLASS),
    glass: at('board', -18, 24, GLASS),
    board: at('board', 14, 30, -6),
    usbc: at('board', 0, -40, -7.5),
    card: at('shell', 27.8, -9, -6.5),
    keys: at('board', 23.6, 11.5, -5.2),
    buttons: at('buttons', 28.7, 11.5, -6.2),
    shell: at('shell', -27.8, 12, -12),
    case: at('shell', -27.8, 12, -12),
    back: at('shell', 8, 6, -24),
    ports: at('shell', 0, -41, -20),
    esc: at('shell', -9.55, -41, -20),
    tmt: at('shell', 9.55, -41, -20),
    plugs: mid('plugs'),
    screws: at('screws', 20.5, 27.1, -24),
    cell: mid('cell'),
    boost: mid('boost'),
    ina: mid('ina'),
  };
  const state = {};
  let flip = 0;
  const place = () => {
    const turn = flip * Math.PI / 180;
    root.rotation.z = turn;
    // Rolled over about its middle, lifted so that no corner dips.
    const half = BACK / 2;
    root.position.set(-half * Math.sin(turn),
                      half * (1 - Math.cos(turn))
                      + 30 * Math.abs(Math.sin(turn)), 0);
    for (const [name, u] of Object.entries(units)) {
      const k = state[name] || 0;
      u.group.position.copy(u.home)
        .addScaledVector(new THREE.Vector3(...u.away), k);
      u.group.rotation.set(u.spin[0] * k, u.spin[1] * k, u.spin[2] * k);
      const seen = 1 - Math.min(1, Math.max(0, (k - .8) / .2));
      for (const m of u.mats) {
        m.opacity = seen;
        if (m.transparent !== seen < 1) {
          m.transparent = seen < 1;
          m.needsUpdate = true;
        }
      }
      u.group.visible = seen > 0;
      if (name === 'board') scr.visible = seen >= 1 && !!face.map;
    }
  };
  place();
  return {
    scene, cam, anchors, setScreen,
    setServoPort: () => {},
    setServo: () => {},
    setLid: () => {},
    // How far out each component stands: {name: 0 to 1}.
    setParts: parts => { Object.assign(state, parts); place(); },
    // The bench rolled over about its long axis, in degrees: 180 shows
    // its back.
    setFlip: deg => { flip = deg; place(); },
    setGlow: (name, amount) => {
      for (const m of (units[name] || {mats: []}).mats) {
        m.emissiveIntensity = Math.max(m.emissiveIntensity, .1 * amount);
      }
    },
    clearGlow: () => {
      for (const u of Object.values(units)) {
        for (const m of u.mats) m.emissiveIntensity = 0;
      }
    },
  };
}
