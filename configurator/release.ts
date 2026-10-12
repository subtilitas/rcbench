// The release the configurator describes. package.json holds it once; the
// data, the page, the checks and the tests read it from here.
import pkg from './package.json' with { type: 'json' };

/** The release number, "major.minor.patch", from the `version` field of package.json. */
export const VERSION: string = pkg.version;

/** The git tag of the release. Source links of the data point at this tag. */
export const TAG = `v${VERSION}`;
