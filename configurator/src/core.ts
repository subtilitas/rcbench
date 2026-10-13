// The one seam between the page and the core. No other file of src/ imports
// core code.
export {
  CATALOGUE, resolve, DEFAULT_OPTIONS, FUNCTION_HELP, PICTURES_MAX, alwaysOn, baseParts, brought, buyRows, deselectEffect, groups,
  plainDrive, removalEffect, removalIsPlain, testObjects,
  CELL_VOLTS, DEFAULT_SIM, HINT_SHARE, SIM_CELLS, SIM_OHMS, SIM_PART, SIM_WATTS, SIM_WIRINGS, ohmsText, sameSim, sig, simHeat, validSim, voltsText,
  wattsText, withSim,
} from '../core/index.ts';
export type { Deselect, Group, Removal, SimHeat } from '../core/index.ts';
