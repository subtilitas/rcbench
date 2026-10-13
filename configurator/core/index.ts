// Entry point of the core: the data, the resolver, the default options, the
// groups of functions, the effects of taking a function or a part out, the
// help text of each function with a tick and the rows of the parts list.

import type { Core } from '../contract.ts';
import { ROW_ID, brought, buyRows, rowToken } from './buy.ts';
import { CATALOGUE } from './catalogue.ts';
import {
  alwaysOn, baseParts, deselectEffect, groups, plainDrive, removalEffect, removalIsPlain, testObjects,
} from './groups.ts';
import { FUNCTION_HELP } from './help.ts';
import { DEFAULT_OPTIONS, PICTURES_MAX, resolve } from './resolve.ts';

export {
  CATALOGUE, DEFAULT_OPTIONS, FUNCTION_HELP, PICTURES_MAX, ROW_ID, resolve, alwaysOn, baseParts, brought, buyRows, deselectEffect,
  groups, plainDrive, removalEffect, removalIsPlain, rowToken, testObjects,
};
export type { Deselect, Group, GroupVariant, OptionChange, Refusal, Removal } from './groups.ts';

const core: Core = Object.freeze({ CATALOGUE, resolve, DEFAULT_OPTIONS });
export default core;
