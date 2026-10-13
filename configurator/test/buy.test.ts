// The parts list of a setup: the search address, the rows to buy and to
// bring, the text form and the CSV form.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Lang, Selection } from '../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, resolve } from '../src/core.ts';
import { t } from '../src/i18n.ts';
import { optionsOf } from '../src/names.ts';
import { SEARCH, buyNote, csvField, listCsv, listText, partsList, searchQuery, searchUrl } from '../src/view/buy.ts';
import type { View } from '../src/view/chooser.ts';
import { result } from '../src/view/result.ts';

function view(part: Partial<Selection>, lang: Lang = 'en'): View {
  const selection: Selection = { functions: [], owned: [], options: { ...DEFAULT_OPTIONS }, ...part };
  const setup = resolve(CATALOGUE, selection);
  return { lang, catalogue: CATALOGUE, setup, options: optionsOf(setup), selection };
}

const URL = 'https://example.invalid/#v=1';
const ids = (lines: { part: { id: string } }[]): string[] => lines.map((l) => l.part.id);

test('searchQuery: maker and ordering name each in quotes', () => {
  assert.equal(searchQuery('Waveshare', 'ESP32-S3-Touch-LCD-7'), '"Waveshare" "ESP32-S3-Touch-LCD-7"');
});

test('searchQuery: a spec without a maker is split at ", ", each term in quotes; a double quote leaves a term', () => {
  assert.equal(searchQuery(null, 'Resistor 2.2 kΩ, 0.25 W'), '"Resistor 2.2 kΩ" "0.25 W"');
  assert.equal(searchQuery(null, 'Plug 5.5 × 2.1 mm'), '"Plug 5.5 × 2.1 mm"');
  assert.equal(searchQuery('A "B"', 'C'), '"A B" "C"');
  assert.equal(searchQuery(null, 'a, , b'), '"a" "b"');
});

test('searchUrl: Ω, ×, quotes, & and parentheses are percent-encoded', () => {
  assert.equal(searchUrl(null, 'Resistor 2.2 kΩ, 0.25 W'), `${SEARCH}%22Resistor%202.2%20k%CE%A9%22%20%220.25%20W%22`);
  assert.equal(searchUrl(null, '5.5 × 2.1 mm'), `${SEARCH}%225.5%20%C3%97%202.1%20mm%22`);
  assert.equal(searchUrl('A & B', 'x(1)!*\''), `${SEARCH}%22A%20%26%20B%22%20%22x%281%29%21%2A%27%22`);
  const url = new globalThis.URL(searchUrl('A & B', 'x'));
  assert.equal(url.origin, 'https://www.google.com');
  assert.equal(url.pathname, '/search');
  assert.deepEqual([...url.searchParams.keys()], ['q']);
  assert.equal(url.searchParams.get('q'), '"A & B" "x"');
  assert.equal(url.hash, '');
});

test('csvField: every field in double quotes, a double quote doubled', () => {
  assert.equal(csvField('a'), '"a"');
  assert.equal(csvField(''), '""');
  assert.equal(csvField('say "hi", then\nleave'), '"say ""hi"", then\nleave"');
});

test('the list of the bare setup holds the bench parts, no test object', () => {
  const list = partsList(view({}));
  assert.ok(ids(list.buy).includes('panel'));
  assert.ok(ids(list.buy).includes('copro'));
  assert.deepEqual(list.bring, []);
  for (const l of list.buy) assert.ok(l.search?.startsWith(SEARCH), l.part.id);
});

test('a part ticked under "Parts you own" leaves the list', () => {
  const all = partsList(view({ functions: ['escpow'] }));
  const owned = partsList(view({ functions: ['escpow'], owned: ['ina228'] }));
  assert.ok(ids(all.buy).includes('ina228'));
  assert.ok(!ids(owned.buy).includes('ina228'));
  assert.deepEqual(ids(owned.buy), ids(all.buy).filter((id) => id !== 'ina228'));
});

test('a test object and its supply stand in the list to bring, without page and search', () => {
  const list = partsList(view({ functions: ['servo'] }));
  assert.ok(ids(list.bring).includes('servo'));
  assert.ok(!ids(list.buy).includes('servo'));
  for (const l of list.bring) {
    assert.equal(l.maker, null);
    assert.equal(l.page, null);
    assert.equal(l.search, null);
  }
});

test('the variant the setup selects names the row', () => {
  const sensor = (o: Partial<typeof DEFAULT_OPTIONS>) =>
    partsList(view({ functions: ['escpow'], options: { ...DEFAULT_OPTIONS, ...o } })).buy.filter((l) => l.part.id === 'ina228');
  const matek = sensor({});
  const xt90 = sensor({ ina228Board: 'xt90' });
  assert.ok(matek.length > 0, 'the function escpow needs the INA228');
  assert.deepEqual(matek.map((l) => [l.maker, l.order]), [['Matek', 'I2C-INA-BM']]);
  assert.deepEqual(xt90.map((l) => l.maker), [null]);
  assert.match(xt90[0]!.order, /XT90/);
  const pads = partsList(view({})).buy.filter((l) => l.part.id === 'copro').map((l) => l.maker);
  const osoyoo = partsList(view({ options: { ...DEFAULT_OPTIONS, connection: 'osoyoo' } })).buy.filter((l) => l.part.id === 'copro').map((l) => l.maker);
  assert.deepEqual(pads, ['Waveshare']);
  assert.deepEqual(osoyoo, ['Waveshare', 'Osoyoo']);
});

for (const lang of ['en', 'de'] as const) {
  test(`${lang}: the text form starts with the setup link and the disclaimer`, () => {
    const v = view({ functions: ['servo'] }, lang);
    const text = listText(v, URL);
    const lines = text.split('\n');
    assert.equal(lines[0], t(lang, 'buyTextHead', { r: CATALOGUE.release }));
    assert.equal(lines[1], URL);
    assert.equal(lines[3], buyNote(v));
    assert.ok(buyNote(v).includes(`v${CATALOGUE.release}`));
    assert.ok(text.endsWith('\n'));
    assert.ok(!text.includes('\r'));
    const list = partsList(v);
    for (const l of list.buy) {
      assert.ok(text.includes(l.check), l.part.id);
      assert.ok(text.includes(l.search!), l.part.id);
      if (l.page !== null) assert.ok(text.includes(l.page), l.part.id);
    }
    assert.ok(text.includes(`\n${t(lang, 'bring')}\n`));
    for (const l of list.bring) assert.ok(text.includes(`${l.qty} × ${l.order}`), l.part.id);
  });

  test(`${lang}: the CSV holds a header and 1 record per row to buy, quoted, in CR LF`, () => {
    const v = view({ functions: ['servo'] }, lang);
    const csv = listCsv(v);
    const list = partsList(v);
    assert.ok(csv.endsWith('\r\n'));
    const records = csv.slice(0, -2).split('\r\n');
    assert.equal(records.length, 1 + list.buy.length);
    assert.equal(records[0], ['csvQty', 'csvMaker', 'csvOrder', 'csvCheck', 'csvPage', 'csvSearch'].map((k) => csvField(t(lang, k as 'csvQty'))).join(','));
    list.buy.forEach((l, i) => {
      assert.equal(records[i + 1], [String(l.qty), l.maker ?? '', l.order, l.check, l.page ?? '', l.search!].map(csvField).join(','));
    });
  });

  test(`${lang}: the page shows every row, the disclaimer and links that open a new tab`, () => {
    const v = view({ functions: ['servo'] }, lang);
    const html = result(v, URL, []);
    const section = html.slice(html.indexOf('id="h-buy"'), html.indexOf('id="h-power"'));
    const list = partsList(v);
    assert.equal(section.split('<li data-part=').length - 1, list.buy.length + list.bring.length);
    assert.ok(section.includes(t(lang, 'buyNote', { r: CATALOGUE.release })));
    const links = [...section.matchAll(/<a [^>]*>/g)].map((m) => m[0]);
    assert.equal(links.length, list.buy.length + list.buy.filter((l) => l.page !== null).length);
    for (const a of links) assert.match(a, /target="_blank" rel="noopener noreferrer"/);
  });
}

test('a list without rows to buy says so', () => {
  // The locked parts are never owned, so only a setup made up for the test has no row to buy.
  const v = view({});
  const none: View = { ...v, setup: { ...v.setup, parts: v.setup.parts.map((r) => ({ ...r, ownedTick: true })) } };
  assert.deepEqual(partsList(none), { buy: [], bring: [] });
  assert.ok(listText(none, URL).includes(t('en', 'buyNone')));
  assert.equal(listCsv(none).split('\r\n').length, 2);
  assert.ok(result(none, URL, []).includes('id="buy-none"'));
});
