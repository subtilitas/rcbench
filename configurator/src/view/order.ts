// The build order as data: the stages of the catalogue with the wires that
// belong to each and, in the last stage, the drawn parts no wire ends on.
// The page and the PDF each set it in their own markup.

import type { Text } from '../../contract.ts';
import { tx } from '../i18n.ts';
import type { View } from './chooser.ts';

export type OrderStage = {
  title: Text;
  /** Wire numbers of the stage, ascending. */
  numbers: number[];
  /** Short names of the parts that stand in this stage without a wire. */
  names: string[];
};

/** The stages the setup lists. A stage after the first with neither wires nor parts is left out. */
export function orderStages(view: View): OrderStage[] {
  const { lang, catalogue, setup } = view;
  const last = Math.max(...catalogue.stages.map((s) => s.n));
  // A drawn part that no wire of the setup ends on has no stage from a wire. It belongs to the last stage.
  const wired = new Set(setup.wires.flatMap((w) => [w.a, w.b ?? '']).map((ref) => ref.split('.')[0]));
  const loose = setup.parts
    .map((r) => r.part)
    .filter((p) => p.slot.column !== 'none' && p.id !== 'copro' && !wired.has(p.id))
    .map((p) => tx(lang, p.short));
  const out: OrderStage[] = [];
  for (const stage of catalogue.stages) {
    const numbers = setup.wires.filter((w) => w.stage === stage.n).map((w) => w.n).sort((a, b) => a - b);
    const names = stage.n === last ? loose : [];
    if (numbers.length === 0 && names.length === 0 && stage.n > 1) continue;
    out.push({ title: stage.title, numbers, names });
  }
  return out;
}
