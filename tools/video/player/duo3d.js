// The bench in its case and the M5Stack Dial in one scene: the case from
// its CAD assembly (case_cad.js), the Dial beside it (dial3d.js), 128mm
// right of the case's middle and 30mm towards the viewer.  The case's
// display takes shots of the panel's screen, the Dial's display its
// faces, and the Dial's knob turns.
//
// SPDX-License-Identifier: MIT
import {buildCadCase} from './case_cad.js';
import {makeDial} from './dial3d.js';

export function buildDuo(caseGltf, dialGltf, baseGltfs = null) {
  const rig = buildCadCase(null, caseGltf);
  // With its base the Dial stands on the base's foot.
  const dial = makeDial(dialGltf, baseGltfs);
  dial.root.position.set(128, 0, 30);
  rig.scene.add(dial.root);
  rig.scene.updateMatrixWorld(true);
  return {
    ...rig,
    // The case's points, and the Dial's: its display is "dial".
    anchors: {...rig.anchors, dial: dial.anchors.display,
              knob: dial.anchors.knob, bezel: dial.anchors.bezel},
    setFace: dial.setFace,
    setTurn: dial.setTurn,
  };
}
