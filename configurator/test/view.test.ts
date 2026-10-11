import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Lang, Selection } from '../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, resolve } from '../src/core.ts';
import { STRINGS } from '../src/i18n.ts';
import { optionsOf } from '../src/names.ts';
import { chooser, lockHint } from '../src/view/chooser.ts';
import type { View } from '../src/view/chooser.ts';
import { result } from '../src/view/result.ts';

function view(part: Partial<Selection>, lang: Lang = 'en'): View {
  const selection: Selection = { functions: [], owned: [], options: { ...DEFAULT_OPTIONS }, ...part };
  const setup = resolve(CATALOGUE, selection);
  return { lang, catalogue: CATALOGUE, setup, options: optionsOf(setup), selection };
}

/** Text of the build order: everything after its heading, tags removed. */
function order(v: View): string {
  const html = result(v, 'https://example.invalid/', []);
  const at = html.indexOf(`<h3>${STRINGS.order[v.lang === 'en' ? 0 : 1]}</h3>`);
  assert.ok(at > 0, 'the build order has a heading');
  return html.slice(at).replace(/<footer>.*$/s, '').replace(/<[^>]+>/g, '\n');
}

const short = (id: string, lang: Lang): string => CATALOGUE.parts.find((p) => p.id === id)!.short[lang];
const hint = (v: View, id: string): string => lockHint(v.setup.parts.find((p) => p.part.id === id)!, v.lang, CATALOGUE);

test('a drawn part with pins and no wire is named in the build order', () => {
  for (const lang of ['en', 'de'] as Lang[]) {
    const v = view({ owned: ['usbtap'] }, lang);
    assert.ok(!v.setup.wires.some((w) => w.a.startsWith('usbtap.') || w.b?.startsWith('usbtap.')));
    assert.ok(order(v).includes(short('usbtap', lang)), order(v));
  }
});

test('a part that a wire ends on is not listed as loose, a part without pins is', () => {
  const v = view({ owned: ['pdmini', 'sd'] });
  const text = order(v);
  const loose = text.split('\n').find((line) => line.startsWith(STRINGS.stageParts[0])) ?? '';
  assert.ok(loose.includes(short('sd', 'en')), text);
  assert.ok(!loose.includes(short('usbtap', 'en')), text);
  assert.ok(!loose.includes(short('pdmini', 'en')), text);
});

test('every drawn part of the full selection is in the build order by a wire or by name', () => {
  const v = view({
    functions: CATALOGUE.functions.map((f) => f.id),
    owned: CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id),
  });
  const text = order(v);
  const staged = new Set(v.setup.wires.flatMap((w) => [w.a, w.b ?? '']).map((ref) => ref.split('.')[0]));
  for (const r of v.setup.parts) {
    const p = r.part;
    if (p.slot.column === 'none' || p.id === 'copro' || staged.has(p.id)) continue;
    assert.ok(text.includes(p.short.en), `${p.id} is in no stage`);
  }
});

test('the hint on a locked part names what holds it', () => {
  const [fn, option, rule] = [STRINGS.lockFn[0], STRINGS.lockOpt[0], STRINGS.lockRule[0]];
  // Held by a function.
  const byFn = view({ functions: ['servotest'] });
  assert.equal(hint(byFn, 'pdmini'), fn);
  // Held by the ESC power variant alone: no function is ticked.
  const byOption = view({ functions: ['esc'], options: { ...DEFAULT_OPTIONS, escPower: 'pd' } });
  assert.equal(hint(byOption, 'pdmini'), option);
  // Held by a rule: the owned PD mini brings the T-plug.
  const byRule = view({ owned: ['pdmini'] });
  assert.equal(hint(byRule, 'usbtap'), rule);
  // Held by a function and by the variant.
  const both = view({ functions: ['esc', 'servotest'], options: { ...DEFAULT_OPTIONS, escPower: 'pd' } });
  assert.equal(hint(both, 'pdmini'), `${fn} ${option}`);
});

test('the chooser shows the hint on the locked row and none on a free row', () => {
  const v = view({ functions: ['esc'], options: { ...DEFAULT_OPTIONS, escPower: 'pd' } }, 'de');
  const html = chooser(v, true);
  const row = (id: string): string => {
    const at = html.indexOf(`id="p-${id}"`);
    assert.ok(at > 0, id);
    return html.slice(at, html.indexOf('</label>', at));
  };
  assert.ok(row('pdmini').includes(STRINGS.lockOpt[1]));
  assert.ok(!row('pdmini').includes(STRINGS.lockFn[1]));
  assert.ok(row('usbtap').includes(STRINGS.lockRule[1]));
  for (const key of ['lockFn', 'lockOpt', 'lockRule'] as const) assert.ok(!row('sd').includes(STRINGS[key][1]));
});

test('the base setup lists the functions that need no part, without a tick', () => {
  const base = CATALOGUE.functions.filter((f) => f.needs.length === 0);
  assert.equal(base.length, 10);
  for (const lang of ['en', 'de'] as Lang[]) {
    const v = view({}, lang);
    const picker = chooser(v, false);
    const list = picker.slice(picker.indexOf('<section class="base"'), picker.indexOf('</section>'));
    assert.ok(list.includes(`<h3 id="g-base">${CATALOGUE.baseFunction[lang]}</h3>`), list);
    assert.ok(list.includes(STRINGS.baseHint[lang === 'en' ? 0 : 1]));
    assert.ok(!list.includes('<input'));
    const html = result(v, 'https://example.invalid/', []);
    assert.ok(html.includes(lang === 'en' ? '4 parts, 4 wires, 10 functions.' : '4 Teile, 4 Drähte, 10 Funktionen.'));
    assert.ok(html.includes(`<h4>${CATALOGUE.baseFunction[lang]}</h4>`));
    assert.ok(!html.includes(STRINGS.withParts[lang === 'en' ? 0 : 1]));
    for (const fn of base) {
      assert.ok(list.includes(`<li>${fn.name[lang]}`), fn.id);
      assert.ok(html.includes(`<li>${fn.name[lang]}`), fn.id);
      assert.ok(!picker.includes(`id="f-${fn.id}"`), fn.id);
    }
  }
});

test('a setup with parts keeps the functions that need no part and lists the others under their own heading', () => {
  const v = view({ functions: ['servo'], owned: ['sd'] });
  const html = result(v, 'https://example.invalid/', []);
  const at = html.indexOf(`<h4>${STRINGS.withParts[0]}</h4>`);
  assert.ok(at > html.indexOf(`<h4>${CATALOGUE.baseFunction.en}</h4>`));
  const rest = html.slice(at, html.indexOf('</ul>', at));
  assert.equal(rest.split('<li>').length - 1, 2);
  for (const fn of CATALOGUE.functions) {
    if (fn.needs.length === 0) assert.ok(html.slice(0, at).includes(`<li>${fn.name.en}`), fn.id);
  }
  assert.ok(chooser(v, false).includes('id="f-servo"'));
});
