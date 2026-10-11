// Entry point of the core: the data, the resolver, the default options, the
// groups of functions and the effects of taking a function or a part out.

import type { Core } from '../contract.ts';
import { CATALOGUE } from './catalogue.ts';
import {
  alwaysOn, baseParts, deselectEffect, groups, plainDrive, removalEffect, removalIsPlain, testObjects,
} from './groups.ts';
import { DEFAULT_OPTIONS, resolve } from './resolve.ts';

export {
  CATALOGUE, DEFAULT_OPTIONS, resolve, alwaysOn, baseParts, deselectEffect, groups, plainDrive, removalEffect,
  removalIsPlain, testObjects,
};
export type { Deselect, Group, GroupVariant, OptionChange, Refusal, Removal } from './groups.ts';

const core: Core = Object.freeze({ CATALOGUE, resolve, DEFAULT_OPTIONS });
export default core;
