// The anchor table of the part drawings, as the page reads it.

import type { Anchors } from '../../drawings/generate.ts';
import data from '../../drawings/anchors.json' with { type: 'json' };

export const ANCHORS = data as unknown as Anchors;
