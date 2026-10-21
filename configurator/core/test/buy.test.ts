// The rows of the parts list: every part has them for every set of options,
// in both languages, and a page is an https address of the maker.

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

import type { Catalogue, Options } from '../../contract.ts';
import { ROW_ID, brought, buyRows, rowToken } from '../buy.ts';
import { CATALOGUE } from '../catalogue.ts';
import { DEFAULT_OPTIONS, resolve } from '../resolve.ts';
import { allOptions, checkBuy, checkString, pageProblem } from '../tools/check.ts';
import type { Finding } from '../tools/check.ts';

const SETS = allOptions();

function copy(): Catalogue {
  return structuredClone(CATALOGUE);
}

function errors(findings: Finding[]): string[] {
  return findings.filter((f) => f.level === 'error').map((f) => `${f.id}: ${f.msg}`);
}

test('the option sets are every combination of the option values', () => {
  assert.equal(SETS.length, 32);
  assert.equal(new Set(SETS.map((o) => JSON.stringify(o))).size, 32);
});

test('every part has a row for every set of options, with order and check in English and German', () => {
  for (const part of CATALOGUE.parts) {
    for (const options of SETS) {
      const rows = buyRows(part, options);
      assert.ok(rows.length > 0, `${part.id} has no row for ${JSON.stringify(options)}`);
      for (const row of rows) {
        for (const lang of ['en', 'de'] as const) {
          assert.deepEqual(checkString(`${part.id}.order.${lang}`, row.order[lang], false), []);
          assert.deepEqual(checkString(`${part.id}.check.${lang}`, row.check[lang], false), []);
        }
      }
    }
  }
});

test('every selectable part has a row with a maker or a spec', () => {
  const selectable = CATALOGUE.parts.filter((p) => p.selectable);
  assert.ok(selectable.length > 0);
  for (const part of selectable) {
    assert.ok(part.buy.length > 0, part.id);
    for (const row of part.buy) assert.ok(row.maker === null || row.maker.trim() !== '', part.id);
  }
});

test('every part that a setup holds has rows under the options of that setup', () => {
  const ids = CATALOGUE.parts.map((p) => p.id);
  const everything = CATALOGUE.functions.map((f) => f.id);
  const seen = new Set<string>();
  for (const options of SETS) {
    for (const functions of [everything, ...everything.map((f) => [f])]) {
      const setup = resolve(CATALOGUE, { functions, owned: [], options });
      for (const r of setup.parts) {
        seen.add(r.part.id);
        assert.ok(buyRows(r.part, options).length > 0, `${r.part.id} under ${JSON.stringify(options)}`);
      }
    }
  }
  // Every part can end in a list.
  assert.deepEqual([...seen].sort(), [...ids].sort());
});

test('a part with a variant name lists the row of the variant the setup selected', () => {
  const variants = CATALOGUE.parts.filter((p) => p.variantName);
  assert.deepEqual(variants.map((p) => p.id).sort(), ['copro', 'ina228']);
  for (const part of variants) {
    const { option, names } = part.variantName!;
    for (const value of Object.keys(names)) {
      const options = { ...DEFAULT_OPTIONS, [option]: value } as Options;
      const rows = buyRows(part, options);
      assert.ok(rows.some((r) => r.only?.[option] === value), `${part.id} ${option}=${value}`);
    }
  }
  const matek = buyRows(CATALOGUE.parts.find((p) => p.id === 'ina228')!, DEFAULT_OPTIONS);
  const xt90 = buyRows(CATALOGUE.parts.find((p) => p.id === 'ina228')!, { ...DEFAULT_OPTIONS, ina228Board: 'xt90' });
  assert.deepEqual(matek.map((r) => r.order.en), ['I2C-INA-BM']);
  assert.deepEqual(xt90.map((r) => r.order.en), ['INA228 module, XT90']);
  const copro = CATALOGUE.parts.find((p) => p.id === 'copro')!;
  assert.deepEqual(buyRows(copro, DEFAULT_OPTIONS).map((r) => r.maker), ['Waveshare']);
  assert.deepEqual(buyRows(copro, { ...DEFAULT_OPTIONS, connection: 'osoyoo' }).map((r) => r.maker), ['Waveshare', 'Osoyoo']);
});

test('every page is https and parses as a URL; a page has a maker', () => {
  const pages = CATALOGUE.parts.flatMap((p) => p.buy).filter((r) => r.page !== undefined);
  assert.ok(pages.length >= 5);
  for (const row of pages) {
    const url = new URL(row.page!);
    assert.equal(url.protocol, 'https:', row.page);
    assert.equal(pageProblem(row.page!), null, row.page);
    assert.notEqual(row.maker, null, row.page);
  }
});

test('the parts the visitor brings are the test objects and their supplies, and name no maker', () => {
  assert.deepEqual(CATALOGUE.parts.filter(brought).map((p) => p.id), ['servo', 'ssup', 'esc', 'batt']);
  for (const part of CATALOGUE.parts.filter(brought)) {
    for (const row of part.buy) {
      assert.equal(row.maker, null, part.id);
      assert.equal(row.page, undefined, part.id);
    }
  }
});

test('a modelled part gives a spec, not a product', () => {
  const modelled = CATALOGUE.parts.filter((p) => p.modelled);
  assert.ok(modelled.length > 0);
  for (const part of modelled) {
    for (const row of part.buy) {
      assert.equal(row.maker, null, part.id);
      assert.equal(row.page, undefined, part.id);
    }
  }
});

test('every maker of the list is named in the trademark notice', () => {
  const notice = readFileSync(new URL('../../../NOTICE', import.meta.url), 'utf8');
  const readme = readFileSync(new URL('../../../README.md', import.meta.url), 'utf8');
  const makers = new Set(CATALOGUE.parts.flatMap((p) => p.buy).map((r) => r.maker).filter((m): m is string => m !== null));
  assert.ok(makers.size >= 5);
  for (const maker of makers) {
    // The notice lists a maker by its first word: "WeAct" for WeAct Studio.
    const name = maker.split(' ')[0] as string;
    assert.match(notice, new RegExp(`\\b${name}\\b`), `NOTICE lacks ${name}`);
    assert.match(readme, new RegExp(`\\b${name}\\b`), `README.md lacks ${name}`);
  }
});

test('pageProblem: http, a fragment, a tracking parameter and no URL are problems', () => {
  assert.equal(pageProblem('https://www.example.org/p.htm'), null);
  assert.equal(pageProblem('https://www.example.org/?portfolio=x'), null);
  assert.equal(pageProblem('http://www.example.org/'), 'is not https');
  assert.equal(pageProblem('https://www.example.org/#a'), 'has a fragment');
  assert.equal(pageProblem('https://www.example.org/?utm_source=x'), 'has a tracking parameter');
  assert.equal(pageProblem('https://www.example.org/?tag=x-21'), 'has a tracking parameter');
  assert.equal(pageProblem('not a url'), 'does not parse as a URL');
});

test('the data has no finding of the parts list check', () => {
  assert.deepEqual(errors(checkBuy(CATALOGUE)), []);
});

test('the parts list check reports each broken copy', () => {
  const at = (c: Catalogue, id: string) => c.parts.find((p) => p.id === id)!;
  const has = (c: Catalogue, pattern: RegExp): void => {
    const found = errors(checkBuy(c));
    assert.ok(found.some((f) => pattern.test(f)), `no finding ${pattern}; got ${found.join(' | ') || 'none'}`);
  };

  let c = copy();
  at(c, 'panel').buy = [];
  has(c, /part\.panel: a set of options gives no row/);

  c = copy();
  at(c, 'ina228').buy = at(c, 'ina228').buy.filter((r) => r.only?.ina228Board !== 'xt90');
  has(c, /part\.ina228: (a set of options gives no row|the variant ina228Board=xt90 has no row)/);

  c = copy();
  at(c, 'copro').buy = at(c, 'copro').buy.map((r) => ({ ...r, only: undefined }));
  has(c, /part\.copro: the variant connection=osoyoo has no row of its own/);

  c = copy();
  at(c, 'panel').buy[0]!.page = 'http://www.waveshare.com/';
  has(c, /part\.panel\.buy\.0: the page .* is not https/);

  c = copy();
  at(c, 'sd').buy[0]!.page = 'https://www.example.org/';
  has(c, /part\.sd\.buy\.0: a page without a maker/);

  c = copy();
  at(c, 'servo').buy[0]!.maker = 'Futaba';
  has(c, /part\.servo\.buy\.0: a part the visitor brings names no maker/);

  c = copy();
  at(c, 'pull').buy[0]!.qty = 0;
  has(c, /part\.pull\.buy\.0: quantity 0/);

  c = copy();
  at(c, 'pull').buy[0]!.only = { connection: 'usb' as never };
  has(c, /part\.pull\.buy\.0: connection=usb is not a value/);

  c = copy();
  at(c, 'pdmini').yours = true;
  has(c, /part\.pdmini: a part the visitor brings is neither locked nor selectable/);

  c = copy();
  at(c, 'panel').buy[0]!.maker = 'Waveshare!';
  has(c, /part\.panel\.buy\.0\.maker: string holds an exclamation mark/);
  c = copy();
  delete at(c, 'wire').buy[1]!.id;
  delete at(c, 'wire').buy[2]!.id;
  has(c, /part\.wire: more than 1 row of the parts list has no id/);

  c = copy();
  at(c, 'wire').buy[1]!.id = 'pair';
  has(c, /part\.wire: the row id pair occurs twice/);

  c = copy();
  at(c, 'wire').buy[2]!.id = 'Multi-meter';
  has(c, /part\.wire\.buy\.2: the row id Multi-meter is not 1 to 12/);
});

test('each row has a token of its own: the part id, or the part id and the row id', () => {
  const tokens = CATALOGUE.parts.flatMap((p) => p.buy.map((row) => rowToken(p, row)));
  assert.equal(new Set(tokens).size, tokens.length);
  for (const part of CATALOGUE.parts) {
    assert.ok(part.buy.filter((row) => row.id === undefined).length <= 1, part.id);
    for (const row of part.buy) if (row.id !== undefined) assert.match(row.id, ROW_ID, part.id);
  }
  const of = (id: string): string[] => CATALOGUE.parts.find((p) => p.id === id)!.buy.map((row) => rowToken(CATALOGUE.parts.find((p) => p.id === id)!, row));
  assert.deepEqual(of('panel'), ['panel.p47b']);
  assert.deepEqual(of('copro'), ['copro', 'copro.osoyoo']);
  assert.deepEqual(of('wire'), ['wire.pair', 'wire.jumper', 'wire.dmm']);
  assert.deepEqual(of('usbtap'), ['usbtap.board', 'usbtap.cable']);
  assert.deepEqual(of('ina228'), ['ina228.matek', 'ina228.xt90']);
});

