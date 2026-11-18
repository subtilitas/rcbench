// One part from its STEP model: the file tools/video_cad.py writes with
// --solids, one mesh a solid in the solid's colour.  Units are mm.  The
// model's z axis stands up as y; the part floats over the origin, its
// middle half its diagonal above the floor, so that it rolls over freely.  setSpin() turns it about the upright axis and setFlip()
// rolls it over about its long axis, each in degrees, so that a board can
// show its back.  An anchor [x, y, z] of a label is a point of the part
// and moves with it.
//
// SPDX-License-Identifier: MIT
import * as THREE from 'three';

export function buildPart(gltf) {
  const scene = new THREE.Scene();
  const cam = new THREE.PerspectiveCamera(30, 16 / 9, 2, 3000);
  scene.add(new THREE.HemisphereLight(0xcfe3ff, 0x10151f, 1.45));
  const sun = new THREE.DirectionalLight(0xffffff, 2.6);
  sun.position.set(-140, 320, 190);
  sun.castShadow = true;
  sun.shadow.mapSize.set(2048, 2048);
  Object.assign(sun.shadow.camera, {left: -220, right: 220, top: 220,
                                    bottom: -220, near: 10, far: 900});
  sun.shadow.bias = -.0004;
  scene.add(sun);
  const rim = new THREE.DirectionalLight(0x38d6ff, 1.1);
  rim.position.set(200, 120, -220);
  scene.add(rim);
  const fill = new THREE.DirectionalLight(0xffffff, 1.0);
  fill.position.set(160, 90, 240);
  scene.add(fill);
  const floor = new THREE.Mesh(new THREE.PlaneGeometry(3000, 3000),
                               new THREE.ShadowMaterial({opacity: .28}));
  floor.rotation.x = -Math.PI / 2;
  floor.receiveShadow = true;
  scene.add(floor);

  // The writer turns mm into m; a quarter turn about x stands z up.
  const src = gltf.scene, box = new THREE.Box3(), body = new THREE.Group();
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
    const mesh = new THREE.Mesh(g, new THREE.MeshStandardMaterial(
      {color: o.material.color, roughness: .55, metalness: .15}));
    mesh.castShadow = mesh.receiveShadow = true;
    body.add(mesh);
  });
  const mid = box.getCenter(new THREE.Vector3());
  body.position.set(-mid.x, -mid.y, -mid.z);
  const lift = Math.hypot(box.max.z - box.min.z, box.max.y - box.min.y) / 2;
  const roll = new THREE.Group(), turn = new THREE.Group();
  roll.add(body);
  turn.add(roll);
  scene.add(turn);
  turn.position.y = lift;
  return {
    scene, cam, anchors: {},
    size: box.getSize(new THREE.Vector3()).toArray(),
    // A point of the part, given from its middle, in the scene.
    point: p => {
      turn.updateMatrixWorld(true);
      return new THREE.Vector3(...p).applyMatrix4(roll.matrixWorld).toArray();
    },
    setSpin: deg => { turn.rotation.y = deg * Math.PI / 180; },
    setFlip: deg => { roll.rotation.x = deg * Math.PI / 180; },
    setLid: () => {}, setServoPort: () => {}, setServo: () => {},
  };
}
