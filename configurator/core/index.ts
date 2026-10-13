// Entry point of the core: the data, the resolver, the default options, the
// groups of functions, the effects of taking a function or a part out, the
// help text of each function with a tick, the rows of the parts list and the
// heat per resistor of the motor simulator.

import type { Core } from '../contract.ts';
import { brought, buyRows } from './buy.ts';
import { CATALOGUE } from './catalogue.ts';
import {
  alwaysOn, baseParts, deselectEffect, groups, plainDrive, removalEffect, removalIsPlain, testObjects,
} from './groups.ts';
import { FUNCTION_HELP } from './help.ts';
import { DEFAULT_OPTIONS, PICTURES_MAX, resolve } from './resolve.ts';
import {
  CELL_VOLTS, DEFAULT_SIM, HINT_SHARE, SIM_CELLS, SIM_OHMS, SIM_PART, SIM_WATTS, SIM_WIRINGS, ohmsText, sameSim, sig, simHeat,
  simName, simPart, validSim, voltsText, wattsText, withSim,
} from './sim.ts';

export {
  CATALOGUE, DEFAULT_OPTIONS, FUNCTION_HELP, PICTURES_MAX, resolve, alwaysOn, baseParts, brought, buyRows, deselectEffect, groups,
  plainDrive, removalEffect, removalIsPlain, testObjects,
  CELL_VOLTS, DEFAULT_SIM, HINT_SHARE, SIM_CELLS, SIM_OHMS, SIM_PART, SIM_WATTS, SIM_WIRINGS, ohmsText, sameSim, sig, simHeat,
  simName, simPart, validSim, voltsText, wattsText, withSim,
};
export type { Load, SimHeat } from './sim.ts';
export type { Deselect, Group, GroupVariant, OptionChange, Refusal, Removal } from './groups.ts';

const core: Core = Object.freeze({ CATALOGUE, resolve, DEFAULT_OPTIONS });
export default core;
