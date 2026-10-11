// The one seam between the page and the core. No other file of src/ imports
// core code.
export {
  CATALOGUE, resolve, DEFAULT_OPTIONS, baseParts, deselectEffect, groups, removalEffect, removalIsPlain,
} from '../core/index.ts';
export type { Deselect, Group, Removal } from '../core/index.ts';
