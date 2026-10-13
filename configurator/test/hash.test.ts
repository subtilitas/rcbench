import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import type { Lang, Selection } from '../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, resolve, alwaysOn } from '../src/core.ts';
import { HASH_VERSION, MAX_HASH, MAX_IDS, MAX_REPORTED, formatHash, parseHash, reportIds, shareAddress, shareHash, testObjectTicks, withoutFormerTicks } from '../src/hash.ts';
import { describe, sweep } from './geometry.ts';

const base = (): Selection => ({ functions: [], owned: [], options: { ...DEFAULT_OPTIONS } });
const sorted = (s: Selection): Selection => ({
  functions: [...s.functions].sort(), owned: [...s.owned].sort(), options: { ...s.options },
});

test('an empty hash is the base setup without a notice', () => {
  for (const hash of ['', '#']) {
    assert.deepEqual(parseHash(hash, DEFAULT_OPTIONS), { selection: base(), lang: null, badVersion: false, unknown: [], have: [] });
  }
});

test('the base setup is written as the version alone', () => {
  assert.equal(formatHash(base(), 'en', 'en', CATALOGUE), `v=${HASH_VERSION}`);
  assert.equal(formatHash(base(), 'de', 'de', CATALOGUE), `v=${HASH_VERSION}`);
});

test('every selection of the sweep survives format and parse', () => {
  const samples = sweep({ CATALOGUE, resolve, DEFAULT_OPTIONS });
  assert.ok(samples.length > 100);
  for (const { selection } of samples) {
    for (const lang of ['en', 'de'] as Lang[]) {
      const hash = formatHash(selection, lang, 'en', CATALOGUE);
      assert.ok(hash.length <= MAX_HASH, describe(selection));
      const parsed = parseHash(`#${hash}`, DEFAULT_OPTIONS);
      assert.equal(parsed.badVersion, false, describe(selection));
      assert.deepEqual(parsed.unknown, [], describe(selection));
      assert.deepEqual(sorted(parsed.selection), sorted(selection), describe(selection));
      assert.equal(parsed.lang ?? 'en', lang);
      assert.equal(formatHash(parsed.selection, lang, 'en', CATALOGUE), hash);
    }
  }
});

test('the full selection survives format and parse', () => {
  // Every tick of the page: the functions that need a part.
  const full: Selection = {
    functions: CATALOGUE.functions.filter((f) => !alwaysOn(CATALOGUE, f)).map((f) => f.id),
    owned: CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id),
    options: { connection: 'osoyoo', ina228Board: 'xt90', escPower: 'pd', servoPower: 'bench', servoPort: 'outputs' },
  };
  const hash = formatHash(full, 'de', 'en', CATALOGUE);
  assert.ok(hash.length <= MAX_HASH);
  assert.deepEqual(parseHash(hash, DEFAULT_OPTIONS).selection, full);
  assert.ok(hash.endsWith('&o=osoyoo,xt90,pd,servo5v&l=de'));
});

test('ids stand in catalogue order whatever the tick order', () => {
  const ids = CATALOGUE.functions.map((f) => f.id);
  const a = formatHash({ ...base(), functions: ids }, 'en', 'en', CATALOGUE);
  const b = formatHash({ ...base(), functions: [...ids].reverse() }, 'en', 'en', CATALOGUE);
  assert.equal(a, b);
});

test('the language key is written only when it differs from the start language', () => {
  assert.equal(formatHash(base(), 'de', 'en', CATALOGUE), 'v=1&l=de');
  assert.equal(formatHash(base(), 'en', 'de', CATALOGUE), 'v=1&l=en');
  assert.equal(parseHash('#v=1&l=de', DEFAULT_OPTIONS).lang, 'de');
  assert.equal(parseHash('#v=1&l=en', DEFAULT_OPTIONS).lang, 'en');
  assert.equal(parseHash('#v=1&l=fr', DEFAULT_OPTIONS).lang, null);
  assert.equal(parseHash('#l=de', DEFAULT_OPTIONS).lang, 'de');
  assert.equal(parseHash('#l=de', DEFAULT_OPTIONS).badVersion, false);
});

test('unknown but well-formed ids pass to the resolver, which reports them', () => {
  const parsed = parseHash('#v=1&f=servo,warp&p=ina3221,flux', DEFAULT_OPTIONS);
  assert.deepEqual(parsed.selection.functions, ['servo', 'warp']);
  assert.deepEqual(parsed.selection.owned, ['ina3221', 'flux']);
  assert.deepEqual(parsed.unknown, []);
  const setup = resolve(CATALOGUE, parsed.selection);
  assert.deepEqual([...setup.ignored].sort(), ['flux', 'warp']);
  assert.equal(formatHash(parsed.selection, 'en', 'en', CATALOGUE), 'v=1&f=servo&p=ina3221');
});

test('malformed ids and unknown option tokens are dropped and reported', () => {
  const parsed = parseHash('#v=1&f=servo,<img>,SERVO,a%20b&p=x_y&o=pd,turbo', DEFAULT_OPTIONS);
  assert.deepEqual(parsed.selection.functions, ['servo']);
  assert.deepEqual(parsed.selection.owned, []);
  assert.equal(parsed.selection.options.escPower, 'pd');
  assert.deepEqual(parsed.unknown, ['<img>', 'SERVO', 'a b', 'x_y', 'turbo']);
  assert.equal(parsed.badVersion, false);
});

test('a repeated id counts once', () => {
  assert.deepEqual(parseHash('#v=1&f=servo,servo,,servo', DEFAULT_OPTIONS).selection.functions, ['servo']);
});

test('another version gives the base setup and the version notice', () => {
  for (const hash of ['#v=2&f=servo', '#v=0', '#v=&f=servo', '#v=1x', '#v=01&f=servo', '#f=servo', '#p=ina3221', '#o=pd']) {
    const parsed = parseHash(hash, DEFAULT_OPTIONS);
    assert.equal(parsed.badVersion, true, hash);
    assert.deepEqual(parsed.selection, base(), hash);
  }
  const parsed = parseHash('#v=2&f=servo&l=de', DEFAULT_OPTIONS);
  assert.equal(parsed.lang, 'de');
});

test('a hash without a known key is the base setup without a notice', () => {
  for (const hash of ['#wiring', '#choose', '#x=1']) {
    const parsed = parseHash(hash, DEFAULT_OPTIONS);
    assert.equal(parsed.badVersion, false, hash);
    assert.deepEqual(parsed.selection, base(), hash);
  }
});

test('an oversized hash gives the base setup and the version notice', () => {
  const at = `v=1&f=${'a,'.repeat((MAX_HASH - 6) / 2)}`;
  assert.equal(at.length, MAX_HASH);
  assert.equal(parseHash(`#${at}`, DEFAULT_OPTIONS).badVersion, false);
  const parsed = parseHash(`#${at}a`, DEFAULT_OPTIONS);
  assert.equal(parsed.badVersion, true);
  assert.deepEqual(parsed.selection, base());
  assert.deepEqual(parseHash(`#v=1&f=${'servo,'.repeat(100000)}`, DEFAULT_OPTIONS).selection, base());
});

test('at most MAX_IDS ids are kept per key', () => {
  const ids = Array.from({ length: MAX_IDS + 20 }, (_, i) => `id${i}`);
  const parsed = parseHash(`#v=1&f=${ids.join(',')}`, DEFAULT_OPTIONS);
  assert.equal(parsed.selection.functions.length, MAX_IDS);
  assert.equal(parsed.badVersion, false);
});

test('an id longer than 32 characters is malformed', () => {
  const parsed = parseHash(`#v=1&f=${'a'.repeat(32)},${'b'.repeat(33)}`, DEFAULT_OPTIONS);
  assert.deepEqual(parsed.selection.functions, ['a'.repeat(32)]);
  assert.deepEqual(parsed.unknown, ['b'.repeat(33)]);
});

test('parsing never throws and never changes the defaults', () => {
  const defaults = { ...DEFAULT_OPTIONS };
  const inputs = ['#%', '#%E0%A4%A', '#v=1&f=%', '#&&&', '#=', '#v', '#v=1&v=2', '#v=1&o=,,,', '#\u0000', '#v=1&f=__proto__,constructor', '#v=1&o=__proto__'];
  for (const hash of inputs) {
    const parsed = parseHash(hash, defaults);
    assert.ok(Array.isArray(parsed.selection.functions), hash);
    assert.doesNotThrow(() => resolve(CATALOGUE, parsed.selection), hash);
  }
  assert.deepEqual(defaults, DEFAULT_OPTIONS);
  // @ts-expect-error a caller without types
  assert.deepEqual(parseHash(undefined, DEFAULT_OPTIONS).selection, base());
});

test('a notice names at most MAX_REPORTED ids, each cut to 32 characters', () => {
  assert.equal(reportIds(['a', 'b', 'a']), 'a, b');
  const many = Array.from({ length: MAX_REPORTED + 1 }, (_, i) => `x${i}`);
  assert.equal(reportIds(many), `${many.slice(0, MAX_REPORTED).join(', ')}, …`);
  assert.equal(reportIds(['z'.repeat(40)]), `${'z'.repeat(32)}…`);
});

test('a link written without the functions that need no part resolves to the same parts and wires', () => {
  const hash = 'v=1&f=servo,esc&p=sd';
  const parsed = parseHash(`#${hash}`, DEFAULT_OPTIONS);
  assert.equal(parsed.badVersion, false);
  assert.deepEqual(parsed.unknown, []);
  assert.deepEqual(parsed.selection, { functions: ['servo', 'esc'], owned: ['sd'], options: { ...DEFAULT_OPTIONS } });
  const setup = resolve(CATALOGUE, parsed.selection);
  assert.deepEqual(setup.ignored, []);
  assert.deepEqual(setup.parts.map((p) => p.part.id), ['panel', 'copro', 'usb', 'wire', 'servo', 'ssup', 'esc', 'batt', 'sd']);
  assert.deepEqual(setup.wires.map((w) => w.n), [1, 2, 3, 4, 7, 8, 9, 10, 11, 12, 13, 14, 15]);
  assert.deepEqual(setup.functions.filter((f) => f.chosen).map((f) => f.fn.id), ['servo', 'esc']);
  // The functions that need no part are enabled and are not part of the link.
  const base = CATALOGUE.functions.filter((f) => alwaysOn(CATALOGUE, f)).map((f) => f.id);
  assert.equal(base.length, 10);
  for (const id of base) assert.ok(setup.functions.find((f) => f.fn.id === id)?.enabled, id);
  const written = formatHash(parsed.selection, 'en', 'en', CATALOGUE);
  assert.equal(written, hash);
  assert.ok(base.every((id) => !written.includes(id)));
  assert.equal(HASH_VERSION, 1);
});

test('a function that needs no part is never written into the hash', () => {
  const ids = CATALOGUE.functions.filter((f) => alwaysOn(CATALOGUE, f)).map((f) => f.id);
  // A link that names such ids parses without a notice.
  const parsed = parseHash(`#v=1&f=link,servo,${ids.join(',')}`, DEFAULT_OPTIONS);
  assert.deepEqual(parsed.unknown, []);
  assert.ok(parsed.selection.functions.includes('link'));
  // It resolves to the parts and wires of the ticks alone.
  const withIds = resolve(CATALOGUE, parsed.selection);
  const ticks = resolve(CATALOGUE, { ...base(), functions: ['servo'] });
  assert.deepEqual(withIds.ignored, []);
  assert.deepEqual(withIds.parts.map((p) => p.part.id), ticks.parts.map((p) => p.part.id));
  assert.deepEqual(withIds.wires.map((w) => w.n), ticks.wires.map((w) => w.n));
  // The hash written back holds the tick alone.
  assert.equal(formatHash(parsed.selection, 'en', 'en', CATALOGUE), 'v=1&f=servo');
  assert.equal(formatHash({ ...base(), functions: ids }, 'en', 'en', CATALOGUE), 'v=1');
  for (const id of ids) {
    assert.equal(formatHash({ ...base(), functions: [id, 'esc'] }, 'en', 'en', CATALOGUE), 'v=1&f=esc', id);
  }
});

test('the hash of a document holds no id of a function that needs no part, in every spelling of the link', () => {
  const ids = CATALOGUE.functions.filter((f) => alwaysOn(CATALOGUE, f)).map((f) => f.id);
  const share = (hash: string): string => shareHash(hash, DEFAULT_OPTIONS, CATALOGUE);
  // A hash without such an id is returned as it is, also in a spelling the page does not write.
  for (const hash of ['', '#', '#v=1', '#v=1&f=servo,esc&p=sd', '#v=1&l=en', '#l=de&v=1&f=esc', '#v=1&f=servo%2Cesc', '#v=2&f=link', '#v=1&f=nothere', `#v=1&f=${'x'.repeat(2100)}`]) {
    assert.equal(share(hash), hash, hash);
  }
  for (const id of ids) {
    // Alone, beside ticks on both sides, encoded, with a key before it, and with parts, options and a language.
    assert.equal(share(`#v=1&f=${id}`), '#v=1', id);
    assert.equal(share(`#v=1&f=servo,${id},esc`), '#v=1&f=servo,esc', id);
    assert.equal(share(`#v=1&f=${id}%2Cservo`), '#v=1&f=servo', id);
    assert.equal(share(`#v=1&f=servo%2C${id}`), '#v=1&f=servo', id);
    assert.equal(share(`#v=1&f=%73ervo,${id.replace(/^./, (c) => `%${c.charCodeAt(0).toString(16)}`)}`), '#v=1&f=servo', id);
    assert.equal(share(`#l=de&f=${id},esc&v=1`), '#v=1&f=esc&l=de', id);
    assert.equal(share(`#v=1&f=${id},esc&p=sd&o=osoyoo,pd&l=en`), '#v=1&f=esc&p=sd&o=osoyoo,pd&l=en', id);
    assert.equal(share(`#v=1&f=${id},esc&p=sd&o=osoyoo,pd`), '#v=1&f=esc&p=sd&o=osoyoo,pd', id);
  }
  assert.equal(share(`#v=1&f=${CATALOGUE.functions.map((f) => f.id).join(',')}`), `#v=1&f=${CATALOGUE.functions.filter((f) => !alwaysOn(CATALOGUE, f)).map((f) => f.id).join(',')}`);
  // The result names the same parts and wires as the link.
  const before = resolve(CATALOGUE, parseHash(`#v=1&f=${ids.join('%2C')},servo&o=osoyoo`, DEFAULT_OPTIONS).selection);
  const after = resolve(CATALOGUE, parseHash(share(`#v=1&f=${ids.join('%2C')},servo&o=osoyoo`), DEFAULT_OPTIONS).selection);
  assert.deepEqual(after.parts.map((p) => p.part.id), before.parts.map((p) => p.part.id));
  assert.deepEqual(after.wires.map((w) => w.n), before.wires.map((w) => w.n));
});

test('a test object under the owned parts of a link becomes the tick of the function that drives it', () => {
  const ticks = (hash: string): Selection => testObjectTicks(parseHash(hash, DEFAULT_OPTIONS).selection, CATALOGUE);
  // p=servo is f=servo, p=esc is f=esc; every other id and every option stays.
  assert.deepEqual(ticks('#v=1&p=servo'), { ...base(), functions: ['servo'] });
  assert.deepEqual(ticks('#v=1&p=esc'), { ...base(), functions: ['esc'] });
  assert.deepEqual(ticks('#v=1&p=esc,servo'), { ...base(), functions: ['esc', 'servo'] });
  assert.deepEqual(ticks('#v=1&f=angle&p=servo,sd,esc&o=pd'), { functions: ['angle', 'servo', 'esc'], owned: ['sd'], options: { ...DEFAULT_OPTIONS, escPower: 'pd' } });
  // The function is ticked already: no second tick.
  assert.deepEqual(ticks('#v=1&f=servo&p=servo'), { ...base(), functions: ['servo'] });
  // No test object under the owned parts: the selection is returned as it is.
  const plain = parseHash('#v=1&f=servotest&p=sd', DEFAULT_OPTIONS).selection;
  assert.equal(testObjectTicks(plain, CATALOGUE), plain);
  // The input is not changed.
  const given = parseHash('#v=1&p=servo', DEFAULT_OPTIONS).selection;
  testObjectTicks(given, CATALOGUE);
  assert.deepEqual(given, { ...base(), owned: ['servo'] });
  // A test object that no function drives without a further part is dropped from the owned parts.
  const bare = { ...CATALOGUE, functions: CATALOGUE.functions.filter((f) => f.id !== 'servo') };
  assert.deepEqual(testObjectTicks(given, bare), base());
  // No row of "Parts you own" is a test object, and the page writes none into a link.
  for (const part of CATALOGUE.parts.filter((x) => x.testObject === true)) assert.equal(part.selectable, false, part.id);
  assert.equal(formatHash({ ...base(), owned: ['servo', 'esc', 'sd'] }, 'en', 'en', CATALOGUE), 'v=1&p=sd');
});

test('the link of a document names a test object as the tick of its function', () => {
  const share = (hash: string): string => shareHash(hash, DEFAULT_OPTIONS, CATALOGUE);
  assert.equal(share('#v=1&p=servo'), '#v=1&f=servo');
  assert.equal(share('#v=1&p=esc&o=pd&l=de'), '#v=1&f=esc&o=pd&l=de');
  assert.equal(share('#v=1&f=angle&p=servo,sd'), '#v=1&f=servo,angle&p=sd');
  assert.equal(share('#v=1&f=servo&p=servo%2Cesc'), '#v=1&f=servo,esc');
});

test('the servo supply from the bench is one option token, and a link without it keeps the external supply', () => {
  assert.equal(DEFAULT_OPTIONS.servoPower, 'external');
  assert.equal(parseHash('#v=1&f=servo&o=pd', DEFAULT_OPTIONS).selection.options.servoPower, 'external');
  assert.equal(parseHash('#v=1&f=servo&o=servo5v', DEFAULT_OPTIONS).selection.options.servoPower, 'bench');
  assert.equal(formatHash({ ...base(), functions: ['servo'], options: { ...DEFAULT_OPTIONS, servoPower: 'bench' } }, 'en', 'en', CATALOGUE), 'v=1&f=servo&o=servo5v');
  assert.equal(formatHash({ ...base(), functions: ['servo'] }, 'en', 'en', CATALOGUE), 'v=1&f=servo');
});

test('the servo programmer is one function id; its link holds no option token for the signal pin', () => {
  const parsed = parseHash('#v=1&f=servoprog', DEFAULT_OPTIONS);
  assert.deepEqual([parsed.badVersion, parsed.unknown], [false, []]);
  assert.deepEqual(parsed.selection, { ...base(), functions: ['servoprog'] });
  assert.equal(formatHash(parsed.selection, 'en', 'en', CATALOGUE), 'v=1&f=servoprog');
  // The resolver sets the pin from the function, so the setup, not the link, holds it.
  const setup = resolve(CATALOGUE, parsed.selection);
  assert.deepEqual(setup.options.find((o) => o.key === 'servoPort'), { key: 'servoPort', value: 'prog', visible: false, forcedBy: 'servoprog' });
  assert.equal(formatHash({ ...base(), functions: ['servo'], options: { ...DEFAULT_OPTIONS, servoPort: 'prog' } }, 'en', 'en', CATALOGUE), 'v=1&f=servo');
  // With the supply from the bench and the PD mini as an owned part.
  assert.equal(formatHash({ functions: ['servoprog'], owned: ['pdmini'], options: { ...DEFAULT_OPTIONS, servoPower: 'bench' } }, 'de', 'en', CATALOGUE), 'v=1&f=servoprog&p=pdmini&o=servo5v&l=de');
  // "kst" is no id of this release: the links of the corpus that name it keep their meaning.
  assert.deepEqual(resolve(CATALOGUE, parseHash('#v=1&f=servo,kst&p=mt6701', DEFAULT_OPTIONS).selection).ignored, ['kst', 'mt6701']);
});

test('every link of the fixture resolves to the parts, wires, functions and variants it names', () => {
  // The fixture holds links without the keys of the servo supply and the servo signal pin: every function and every owned part alone,
  // every pair of them, a servo or an ESC as an owned part under each of the 8 sets of variants, and the full sets.
  type Entry = { setup: string; links: string[] };
  const entries = JSON.parse(readFileSync(new URL('./fixtures/links-v1.json', import.meta.url), 'utf8')) as Entry[];
  let links = 0;
  let objects = 0;
  let retired = 0;
  for (const entry of entries) {
    for (const link of entry.links) {
      links += 1;
      const parsed = parseHash(`#${link}`, DEFAULT_OPTIONS);
      assert.deepEqual([parsed.badVersion, parsed.unknown], [false, []], link);
      if (parsed.selection.owned.some((id) => id === 'servo' || id === 'esc')) objects += 1;
      const setup = resolve(CATALOGUE, testObjectTicks(parsed.selection, CATALOGUE));
      // The T-plug has no tick any more. A stored link that names it gets the T-plug from the PD mini when the
      // setup holds the PD mini, as stored, and none otherwise: there the resolver lists it as ignored.
      if (parsed.selection.owned.includes('usbtap')) {
        assert.deepEqual(setup.ignored, ['usbtap'], link);
        if (!setup.parts.some((r) => r.part.id === 'pdmini')) {
          retired += 1;
          assert.ok(!setup.parts.some((r) => r.part.id === 'usbtap'), link);
          continue;
        }
      }
      const got = [
        setup.parts.map((r) => r.part.id).join(','),
        setup.wires.map((w) => w.n).filter((n) => n !== 63).join(','),
        setup.functions.filter((f) => f.enabled).map((f) => f.fn.id).join(','),
        setup.options.filter((x) => x.key !== 'servoPower' && x.key !== 'servoPort').map((x) => `${x.key}=${x.value}${x.visible ? '' : '?'}`).join(','),
      ].join('|');
      assert.equal(got, entry.setup, link);
      // The fixture lists the power path of the PD mini as wire 19 alone. Wire 63 is its second half
      // and stands in a setup exactly when wire 19 does.
      const has = (n: number): boolean => setup.wires.some((w) => w.n === n);
      assert.equal(has(63), has(19), link);
      // The servo supply of such a link is the external one: no wire runs from a supply pin of the bench to the servo.
      assert.ok(!setup.wires.some((w) => [59, 60, 61, 62].includes(w.n)), link);
    }
  }
  assert.equal(links, 1192);
  assert.ok(retired > 0, 'no link names the T-plug without the PD mini');
  assert.ok(objects > 300, `${objects} links with a test object as an owned part`);
});

test('an old link that holds the T-plug loads: the part is dropped without a notice, the PD mini still brings it', () => {
  // Before the T-plug lost its tick, p=usbtap was a valid owned part.
  const alone = parseHash('#v=1&f=supply&p=usbtap', DEFAULT_OPTIONS);
  assert.deepEqual([alone.badVersion, alone.unknown], [false, []]);
  assert.deepEqual(alone.selection.owned, ['usbtap']);
  const setup = resolve(CATALOGUE, alone.selection);
  assert.deepEqual(setup.ignored, ['usbtap']);
  // No notice: the former tick leaves the owned parts before the setup is resolved.
  const given = withoutFormerTicks(alone.selection);
  assert.deepEqual(given.owned, []);
  assert.deepEqual(resolve(CATALOGUE, given).ignored, []);
  // Only the owned parts hold it: f=usbtap and o=usbtap keep their notice, and so does the id of a part that was never a tick.
  for (const hash of ['#v=1&f=usbtap', '#v=1&o=usbtap']) {
    const p = parseHash(hash, DEFAULT_OPTIONS);
    const given = withoutFormerTicks(p.selection);
    assert.deepEqual(given, p.selection, hash);
    // The id stays in the selection the page resolves, where the resolver or the page's own filter reports it.
    assert.ok([...given.functions, ...given.owned, ...Object.values(given.options)].includes('usbtap') || p.unknown.includes('usbtap'), hash);
  }
  assert.deepEqual(withoutFormerTicks({ functions: ['usbtap'], owned: ['usbtap', 'sd'], options: { ...DEFAULT_OPTIONS } }), { functions: ['usbtap'], owned: ['sd'], options: { ...DEFAULT_OPTIONS } });
  for (const id of ['kst', 'charger', 'panel']) {
    const p = parseHash(`#v=1&p=${id}`, DEFAULT_OPTIONS);
    assert.equal([...p.unknown, ...resolve(CATALOGUE, withoutFormerTicks(p.selection)).ignored].length > 0, true, id);
  }
  // The function supply needs the PD mini, which brings the T-plug.
  assert.ok(setup.parts.some((r) => r.part.id === 'usbtap' && r.addedBy.every((a) => a.kind === 'rule' && a.id === 'pd-tplug')));
  // The page draws the selection reduced to the parts it offers a tick for: the T-plug is no part of the written hash.
  const ticks = alone.selection.owned.filter((id) => CATALOGUE.parts.some((p) => p.id === id && p.selectable));
  assert.deepEqual(ticks, []);
  assert.equal(formatHash({ ...alone.selection, owned: ticks }, 'en', 'en', CATALOGUE), 'v=1&f=supply');
  assert.equal(formatHash(alone.selection, 'en', 'en', CATALOGUE), 'v=1&f=supply');
  // Without the PD mini the link gives the base setup and no T-plug.
  const bare = resolve(CATALOGUE, parseHash('#v=1&p=usbtap', DEFAULT_OPTIONS).selection);
  assert.deepEqual(bare.ignored, ['usbtap']);
  assert.ok(!bare.parts.some((r) => r.part.id === 'usbtap'));
});

test('the hash of a document holds no former tick', () => {
  const share = (hash: string): string => shareHash(hash, DEFAULT_OPTIONS, CATALOGUE);
  assert.equal(share('#v=1&f=supply&p=usbtap'), '#v=1&f=supply');
  assert.equal(share('#v=1&p=usbtap'), '#v=1');
  assert.equal(share('#v=1&p=sd,usbtap&o=osoyoo,pd&l=de'), '#v=1&p=sd&o=osoyoo,pd&l=de');
  // An id of that spelling under f= or o= is no former tick: the hash stays as it is.
  assert.equal(share('#v=1&f=usbtap'), '#v=1&f=usbtap');
});

test('the address to copy, print and put in a document is the page address with the hash of a document', () => {
  const address = (href: string): string => shareAddress(href, DEFAULT_OPTIONS, CATALOGUE);
  const page = 'https://subtilitas.github.io/rcbench/configurator/';
  // The former tick, a function without a tick and a test object leave the hash; the part before '#' stays.
  assert.equal(address(`${page}#v=1&f=supply&p=usbtap`), `${page}#v=1&f=supply`);
  assert.equal(address(`${page}#v=1&f=supply&p=usbtap&l=de`), `${page}#v=1&f=supply&l=de`);
  assert.equal(address(`${page}#v=1&p=sd,usbtap&o=osoyoo,pd&l=en`), `${page}#v=1&p=sd&o=osoyoo,pd&l=en`);
  assert.equal(address(`${page}#v=1&p=servo`), `${page}#v=1&f=servo`);
  assert.equal(address(`http://localhost:4611/?p=usbtap#v=1&p=usbtap`), 'http://localhost:4611/?p=usbtap#v=1');
  // An address the page writes, one without a hash and one with an empty hash stay as they are.
  for (const href of [`${page}#v=1&f=servo,esc&p=pdmini`, `${page}#v=1&f=usbtap`, page, `${page}#`, `${page}#v=2&p=usbtap`]) {
    assert.equal(address(href), href, href);
  }
});

test('"have it" ticks: the key h survives format and parse, in catalogue order, each once', () => {
  const sel: Selection = { ...base(), functions: ['escpow'] };
  const hash = formatHash(sel, 'en', 'en', CATALOGUE, ['ina228.matek', 'wire.dmm', 'panel', 'wire.dmm']);
  assert.equal(hash, 'v=1&f=escpow&h=panel,wire.dmm,ina228.matek');
  const parsed = parseHash(`#${hash}`, DEFAULT_OPTIONS);
  assert.deepEqual(parsed.have, ['panel', 'wire.dmm', 'ina228.matek']);
  assert.deepEqual(parsed.unknown, []);
  assert.deepEqual(parsed.selection, sel);
  assert.equal(formatHash(parsed.selection, 'en', 'en', CATALOGUE, parsed.have), hash);
  // The key stands after the option tokens and before the language.
  assert.equal(formatHash({ ...sel, options: { ...DEFAULT_OPTIONS, ina228Board: 'xt90' } }, 'de', 'en', CATALOGUE, ['ina228.xt90']), 'v=1&f=escpow&o=xt90&h=ina228.xt90&l=de');
  // A token that names no row of the catalogue is not written.
  assert.equal(formatHash(base(), 'en', 'en', CATALOGUE, ['wire.nope', 'warp', 'panel']), 'v=1&h=panel');
});

test('a link without the key h opens the same setup with no "have it" tick', () => {
  for (const hash of ['#v=1', '#v=1&f=servo,escpow&p=sd&o=xt90', '#v=1&f=supply&l=de']) {
    const parsed = parseHash(hash, DEFAULT_OPTIONS);
    assert.deepEqual(parsed.have, [], hash);
    assert.equal(parsed.badVersion, false, hash);
    assert.equal(`#${formatHash(parsed.selection, parsed.lang ?? 'en', 'en', CATALOGUE, parsed.have)}`, hash);
  }
  assert.equal(HASH_VERSION, 1);
});

test('a malformed row token is reported; a well-formed unknown one passes to the page', () => {
  const parsed = parseHash('#v=1&h=wire.dmm,Wire.DMM,wire.,wire..dmm,wire.dmm.x,.dmm,wire.abcdefghijklm,wire.nope', DEFAULT_OPTIONS);
  assert.deepEqual(parsed.have, ['wire.dmm', 'wire.nope']);
  assert.deepEqual(parsed.unknown, ['Wire.DMM', 'wire.', 'wire..dmm', 'wire.dmm.x', '.dmm', 'wire.abcdefghijklm']);
  // A row id of 12 characters is well-formed.
  assert.deepEqual(parseHash('#v=1&h=wire.abcdefghijkl', DEFAULT_OPTIONS).have, ['wire.abcdefghijkl']);
  // At most MAX_IDS tokens are kept.
  const many = Array.from({ length: MAX_IDS + 5 }, (_, i) => `p${i}`).join(',');
  assert.equal(parseHash(`#v=1&h=${many}`, DEFAULT_OPTIONS).have.length, MAX_IDS);
});

test('the key h without a version gives the base setup and the version notice', () => {
  const parsed = parseHash('#h=wire.dmm', DEFAULT_OPTIONS);
  assert.equal(parsed.badVersion, true);
  assert.deepEqual(parsed.have, []);
  assert.equal(parseHash('#v=2&h=wire.dmm', DEFAULT_OPTIONS).badVersion, true);
});

test('the hash of a document holds the "have it" ticks the page shows', () => {
  const share = (hash: string, have?: string[]): string => shareHash(hash, DEFAULT_OPTIONS, CATALOGUE, have);
  // The ticks of the page are the ticks of the hash: the hash stays as it is.
  assert.equal(share('#v=1&h=panel,wire.dmm', ['panel', 'wire.dmm']), '#v=1&h=panel,wire.dmm');
  assert.equal(share('#v=1&h=panel,wire.dmm'), '#v=1&h=panel,wire.dmm');
  // A stale tick of the link leaves; the rest and the language stay.
  assert.equal(share('#v=1&f=escpow&o=xt90&h=wire.dmm,ina228.matek&l=de', ['wire.dmm']), '#v=1&f=escpow&o=xt90&h=wire.dmm&l=de');
  assert.equal(share('#v=1&h=wire.nope', []), '#v=1');
  // A malformed token beside a kept one leaves as well: the parser rejects it, the page drops it.
  assert.equal(share('#v=1&h=Wire.DMM,panel', ['panel']), '#v=1&h=panel');
  assert.equal(share('#v=1&f=servo&h=panel,wire.dmm.x&l=de', ['panel']), '#v=1&f=servo&h=panel&l=de');
  // A repeated token is no dropped token: the hash stays as it is.
  assert.equal(share('#v=1&h=panel,panel', ['panel']), '#v=1&h=panel,panel');
  // A hash of another version stays as it is.
  assert.equal(share('#v=2&h=Wire.DMM', []), '#v=2&h=Wire.DMM');
  // A test object leaves as before and the ticks stay.
  assert.equal(share('#v=1&p=servo&h=panel', ['panel']), '#v=1&f=servo&h=panel');
  const page = 'https://subtilitas.github.io/rcbench/configurator/';
  assert.equal(shareAddress(`${page}#v=1&h=pdmini,panel`, DEFAULT_OPTIONS, CATALOGUE, ['panel']), `${page}#v=1&h=panel`);
});
