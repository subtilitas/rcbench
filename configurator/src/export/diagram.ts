// The wiring drawing of the export, behind one interface: a resolved setup in,
// SVG markup out. The export paints whatever SVG this module returns, within
// the subset `svg.ts` documents.

import type { Catalogue, Lang, Setup, SignalClass } from '../../contract.ts';
import { drawing, sample } from '../draw/svg.ts';

export type Diagram = (setup: Setup, catalogue: Catalogue, lang: Lang) => string;

/** The drawing the PDF shows. Another drawing module replaces `drawing` here. */
export const wiringDiagram: Diagram = drawing;

/** Line sample of a signal class as SVG markup, for the legend and the table. */
export const lineSample: (cls: SignalClass) => string = sample;
