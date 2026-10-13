// The one seam between the page and the core. No other file of src/ imports
// core code.
export {
  CATALOGUE, resolve, DEFAULT_OPTIONS, FUNCTION_HELP, PICTURES_MAX, alwaysOn, baseParts, deselectEffect, groups, plainDrive, removalEffect,
  removalIsPlain, testObjects,
} from '../core/index.ts';
export type { Deselect, Group, Removal } from '../core/index.ts';
