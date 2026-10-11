// Entry point of the core: the data, the resolver and the default options.

import type { Core } from '../contract.ts';
import { CATALOGUE } from './catalogue.ts';
import { DEFAULT_OPTIONS, resolve } from './resolve.ts';

export { CATALOGUE, DEFAULT_OPTIONS, resolve };

const core: Core = Object.freeze({ CATALOGUE, resolve, DEFAULT_OPTIONS });
export default core;
