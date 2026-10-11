import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Lang, Selection } from '../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, deselectEffect, groups, resolve } from '../src/core.ts';
import { STRINGS, t } from '../src/i18n.ts';
import { optionsOf } from '../src/names.ts';
import { chooser, deselectText, groupId, removalWarning, removeControl, statusText } from '../src/view/chooser.ts';
import type { Pending, View } from '../src/view/chooser.ts';
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

const LANGS: Lang[] = ['en', 'de'];
const URL = 'https://example.invalid/';
const col = (lang: Lang): 0 | 1 => (lang === 'en' ? 0 : 1);
const short = (id: string, lang: Lang): string => CATALOGUE.parts.find((p) => p.id === id)!.short[lang];
const fnName = (id: string, lang: Lang): string => CATALOGUE.functions.find((f) => f.id === id)!.name[lang];
const text = (html: string): string => html.replace(/<[^>]+>/g, ' ').replace(/&quot;/g, '"').replace(/\s+/g, ' ').trim();
const count = (html: string, piece: string): number => html.split(piece).length - 1;
const status = (v: View, id: string): string => statusText(v, v.setup.functions.find((f) => f.fn.id === id)!);

/** The markup of one group of the chooser. */
function fieldset(html: string, id: string): string {
  const start = html.indexOf(`<fieldset class="grp" id="${id}">`);
  assert.ok(start >= 0, `no group ${id}`);
  return html.slice(start, html.indexOf('</fieldset>', start));
}

/** The markup of one row: its label with the input of this id. */
function row(html: string, id: string): string {
  const input = html.indexOf(`id="${id}"`);
  assert.ok(input >= 0, `no row ${id}`);
  const start = html.lastIndexOf('<label', input);
  return html.slice(start, html.indexOf('</label>', start));
}

test('a drawn part with pins and no wire is named in the build order', () => {
  for (const lang of LANGS) {
    const v = view({ owned: ['usbtap'] }, lang);
    assert.ok(!v.setup.wires.some((w) => w.a.startsWith('usbtap.') || w.b?.startsWith('usbtap.')));
    assert.ok(order(v).includes(short('usbtap', lang)), order(v));
  }
});

test('a part that a wire ends on is not listed as loose, a part without pins is', () => {
  const v = view({ owned: ['pdmini', 'sd'] });
  const said = order(v);
  const loose = said.split('\n').find((line) => line.startsWith(STRINGS.stageParts[0])) ?? '';
  assert.ok(loose.includes(short('sd', 'en')), said);
  assert.ok(!loose.includes(short('usbtap', 'en')), said);
  assert.ok(!loose.includes(short('pdmini', 'en')), said);
});

test('every drawn part of the full selection is in the build order by a wire or by name', () => {
  const v = view({
    functions: CATALOGUE.functions.map((f) => f.id),
    owned: CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id),
  });
  const said = order(v);
  const staged = new Set(v.setup.wires.flatMap((w) => [w.a, w.b ?? '']).map((ref) => ref.split('.')[0]));
  for (const r of v.setup.parts) {
    const p = r.part;
    if (p.slot.column === 'none' || p.id === 'copro' || staged.has(p.id)) continue;
    assert.ok(said.includes(p.short.en), `${p.id} is in no stage`);
  }
});

test('the chooser holds one group per set of parts, each function in one group, in both languages', () => {
  const list = groups(CATALOGUE);
  for (const lang of LANGS) {
    const html = chooser(view({}, lang), false);
    assert.equal(count(html, '<fieldset class="grp"'), list.length);
    let last = 0;
    for (const group of list) {
      const body = fieldset(html, groupId(group));
      const position = html.indexOf(body);
      assert.ok(position > last, `${group.key} is out of order`);
      last = position;
      const legend = text(body.slice(0, body.indexOf('</legend>')));
      if (group.parts.length === 0) assert.equal(legend, STRINGS.gBase[col(lang)]);
      else assert.equal(legend, STRINGS.gNeeds[col(lang)] + group.parts.map((id) => short(id, lang)).join(' + '));
      assert.deepEqual([...body.matchAll(/ data-f="([^"]+)"/g)].map((m) => m[1]), group.functions);
      for (const id of [...group.also, ...group.oneOf.flat()]) assert.ok(text(body).includes(short(id, lang)), `${group.key}: ${id}`);
    }
    for (const f of CATALOGUE.functions) assert.equal(count(html, `id="f-${f.id}"`), 1, f.id);
    assert.ok(html.includes(STRINGS.chooseHint[col(lang)]));
  }
});

test('a group names a part that is one of several boards and the parts the rules add', () => {
  const en = chooser(view({}), false);
  const escpow = text(fieldset(en, 'g-esc-ina228'));
  assert.ok(escpow.startsWith(`Needs: ${short('esc', 'en')} + ${short('ina228', 'en')}`), escpow);
  assert.ok(new RegExp(`${short('ina228', 'en')}: one of [^.]*MATEK[^.]*, or [^.]*XT90`).test(escpow), escpow);
  assert.ok(escpow.includes(`Also needs: ${short('pull', 'en')}.`), escpow);
  const esc = text(fieldset(en, 'g-esc'));
  const pd = ['pdmini', 'charger', 'usbtap'].map((id) => short(id, 'en')).join(' + ');
  assert.ok(esc.includes(`Also needs, by variant: ${short('batt', 'en')}, or ${pd}.`), esc);
  const de = text(fieldset(chooser(view({}, 'de'), false), 'g-esc-ina228'));
  assert.ok(de.startsWith('Braucht: '), de);
  assert.ok(/: wahlweise [^.]*MATEK[^.]* oder [^.]*XT90/.test(de), de);
});

test('a function reads "selected by you", "included with these parts" or what it adds', () => {
  for (const lang of LANGS) {
    const v = view({ functions: ['servotest'] }, lang);
    const html = chooser(v, false);
    // Chosen: ticked, and the words say so.
    assert.ok(row(html, 'f-servotest').includes(' checked'));
    assert.ok(row(html, 'f-servotest').includes('data-state="own"'));
    assert.equal(status(v, 'servotest'), STRINGS.stOwn[col(lang)]);
    // The servo and the PD mini are in the setup: 2 more functions run without a tick.
    for (const id of ['servo', 'supply']) {
      assert.ok(!row(html, `f-${id}`).includes(' checked'), id);
      assert.ok(row(html, `f-${id}`).includes('data-state="inc"'), id);
      assert.ok(row(html, `f-${id}`).includes(STRINGS.stInc[col(lang)]), id);
    }
    // Off: the row names the part that is missing.
    assert.ok(row(html, 'f-angle').includes('data-state="off"'));
    assert.equal(status(v, 'angle'), STRINGS.adds[col(lang)] + short('as5600', lang));
    // The heading of a group whose parts the setup holds says so.
    for (const id of ['g-servo', 'g-pdmini', 'g-servo-pdmini']) assert.ok(fieldset(html, id).includes(STRINGS.gIn[col(lang)]), id);
    assert.ok(!fieldset(html, 'g-esc').includes(STRINGS.gIn[col(lang)]));
    // The result names the same states.
    const res = result(v, URL, []);
    assert.equal(count(res, `<small>${STRINGS.stOwn[col(lang)]}</small>`), 1);
    assert.equal(count(res, `<small>${STRINGS.stInc[col(lang)]}</small>`), 2);
  }
  assert.equal([...chooser(view({}), true).matchAll(/type="checkbox"[^>]* checked/g)].length, 0);
});

test('a function whose parts are there under the other variant names the variant it needs', () => {
  const v = view({ owned: ['esc', 'pdmini'] });
  assert.equal(v.setup.functions.find((f) => f.fn.id === 'stick')?.enabled, false);
  assert.equal(status(v, 'stick'), 'Not active: needs ESC power B.');
  assert.equal(status(view({ owned: ['esc', 'pdmini'] }, 'de'), 'stick'), 'Nicht aktiv: braucht ESC-Versorgung B.');
});

test('every part beyond the base setup has a remove control with a name, a base part has none', () => {
  for (const lang of LANGS) {
    const v = view({ functions: CATALOGUE.functions.map((f) => f.id) }, lang);
    const html = result(v, URL, []);
    assert.ok(v.setup.parts.some((r) => r.part.locked) && v.setup.parts.some((r) => !r.part.locked));
    for (const r of v.setup.parts) {
      const control = removeControl(v, r.part);
      assert.equal(count(html, `id="rm-${r.part.id}"`), r.part.locked ? 0 : 1, r.part.id);
      if (r.part.locked) assert.equal(control, '', r.part.id);
      else {
        assert.ok(control.includes(`aria-label="${t(lang, 'rmName', { p: r.part.short[lang] })}"`), control);
        assert.ok(control.includes('aria-expanded="false"'), control);
        assert.ok(control.endsWith(`>${STRINGS.rm[col(lang)]}</button>`), control);
      }
    }
    assert.ok(!html.includes('id="rmwarn"'));
  }
});

test('the question before a removal names the functions that go off and the parts that follow', () => {
  for (const lang of LANGS) {
    const pending: Pending = { part: 'pdmini', at: 'result' };
    const v: View = { ...view({ functions: ['servotest'] }, lang), pending };
    const html = result(v, URL, []);
    assert.equal(count(html, 'id="rmwarn"'), 1);
    assert.ok(html.includes('id="rm-pdmini" data-rm="pdmini" aria-expanded="true"'));
    // The chooser does not show the question of the parts list.
    assert.ok(!chooser(v, true).includes('id="rmwarn"'));
    const warning = removalWarning(v);
    assert.ok(warning.startsWith('<div class="warn" id="rmwarn" role="alertdialog" aria-labelledby="rmwarn-h" aria-describedby="rmwarn-d">'));
    const said = text(warning);
    assert.ok(said.startsWith(t(lang, 'rmAsk', { p: short('pdmini', lang) })), said);
    assert.ok(said.includes(`${fnName('servotest', lang)} ${STRINGS.stOwn[col(lang)]}`), said);
    assert.ok(said.includes(`${fnName('supply', lang)} ${STRINGS.stInc[col(lang)]}`), said);
    assert.ok(said.includes(t(lang, 'outParts', { x: `${short('charger', lang)}, ${short('usbtap', lang)}` })), said);
    assert.ok(said.includes(t(lang, 'inParts', { x: short('ssup', lang) })), said);
    assert.ok(said.includes(t(lang, 'rmKept', { x: short('servo', lang) })), said);
    assert.ok(said.endsWith(`${STRINGS.rmYes[col(lang)]} ${STRINGS.rmNo[col(lang)]}`), said);
    assert.ok(warning.indexOf('id="rm-yes"') < warning.indexOf('id="rm-no"'));
  }
});

test('the question names a variant that changes and a function that comes on', () => {
  const v: View = { ...view({ functions: ['esc', 'servotest'] }), pending: { part: 'batt', at: 'result' } };
  const said = text(removalWarning(v));
  assert.ok(said.includes('No function goes off.'), said);
  assert.ok(said.includes('Variant that changes: ESC power B.'), said);
  assert.ok(said.includes(`Functions that come on: ${fnName('stick', 'en')}.`), said);
  assert.ok(said.endsWith('Remove part Keep'), said);
});

test('a removal that changes nothing else asks as well, and says so in words', () => {
  for (const lang of LANGS) {
    for (const at of ['result', 'own'] as const) {
      const v: View = { ...view({ owned: ['usbtap', 'sd'] }, lang), pending: { part: 'usbtap', at } };
      const warning = removalWarning(v);
      const said = text(warning);
      assert.ok(said.startsWith(t(lang, 'rmAsk', { p: short('usbtap', lang) })), said);
      assert.ok(said.includes(STRINGS.rmPlain[col(lang)]), said);
      assert.ok(!said.includes(STRINGS.rmNone[col(lang)]), said);
      assert.ok(said.endsWith(`${STRINGS.rmPlain[col(lang)]} ${STRINGS.rmYesPlain[col(lang)]} ${STRINGS.rmNo[col(lang)]}`), said);
      assert.equal(count(warning, 'id="rm-yes"'), 1);
      assert.equal(count(warning, 'id="rm-no"'), 1);
    }
  }
  // A removal with another effect does not carry the sentence.
  const other = text(removalWarning({ ...view({ functions: ['esc'] }), pending: { part: 'batt', at: 'result' } }));
  assert.ok(!other.includes(STRINGS.rmPlain[0]), other);
});

test('every part beyond the base setup of every single function and single part gets a question with both answers', () => {
  const selections: Array<Partial<Selection>> = [
    ...CATALOGUE.functions.map((f) => ({ functions: [f.id] })),
    ...CATALOGUE.parts.filter((p) => p.selectable).map((p) => ({ owned: [p.id] })),
  ];
  let asked = 0;
  for (const selection of selections) {
    const v = view(selection);
    for (const r of v.setup.parts) {
      const warning = removalWarning({ ...v, pending: { part: r.part.id, at: 'result' } });
      const what = `${r.part.id} of ${JSON.stringify(selection)}`;
      if (r.part.locked) {
        assert.equal(warning, '', what);
        continue;
      }
      asked += 1;
      assert.equal(count(warning, 'id="rm-yes"'), 1, what);
      assert.equal(count(warning, 'id="rm-no"'), 1, what);
      // The question states an effect on the functions, or that there is none.
      assert.ok([STRINGS.rmPlain[0], STRINGS.rmNone[0], STRINGS.rmOff[0]].some((s) => warning.includes(s)), what);
    }
  }
  assert.ok(asked >= selections.length, `${asked} questions`);
});

test('the question under "Parts you own" stands under its row, and no question shows for an absent part', () => {
  const v: View = { ...view({ functions: ['servotest'] }, 'de'), pending: { part: 'servo', at: 'own' } };
  const html = chooser(v, true);
  assert.equal(count(html, 'id="rmwarn"'), 1);
  assert.ok(html.indexOf('id="rmwarn"') > html.indexOf('id="p-servo"'));
  assert.ok(html.indexOf('id="rmwarn"') < html.indexOf('id="p-esc"'));
  assert.ok(row(html, 'p-servo').includes(' checked'));
  assert.ok(!result(v, URL, []).includes('id="rmwarn"'));
  for (const part of ['esc', 'panel', 'warp']) {
    assert.equal(removalWarning({ ...v, pending: { part, at: 'own' } }), '', part);
  }
  assert.equal(removalWarning({ ...v, pending: null }), '');
  assert.equal(removalWarning(view({})), '');
});

test('"Parts you own" ticks every selectable part of the setup and states why a part without a tick of its own is there', () => {
  const v = view({ functions: ['servotest'], owned: ['sd'] });
  const html = chooser(v, true);
  let ticked = 0;
  for (const part of CATALOGUE.parts.filter((p) => p.selectable)) {
    const present = v.setup.parts.some((r) => r.part.id === part.id);
    if (present) ticked += 1;
    assert.equal(row(html, `p-${part.id}`).includes(' checked'), present, part.id);
    assert.ok(!row(html, `p-${part.id}`).includes('aria-disabled'), part.id);
  }
  assert.ok(row(html, 'p-pdmini').includes(STRINGS.neededBy[0]));
  assert.ok(!row(html, 'p-sd').includes(STRINGS.neededBy[0]));
  assert.ok(ticked >= 3);
  assert.ok(html.includes(`<span class="tag">${t('en', 'ownN', { n: ticked })}</span>`));
  assert.ok(html.includes(STRINGS.ownHint[0]));
});

test('the note after a function leaves the selection states what stays and what leaves', () => {
  const selection: Selection = { functions: ['servotest', 'servo'], owned: [], options: { ...DEFAULT_OPTIONS } };
  // Still active: the servo stays for the other function.
  const stays = deselectEffect(CATALOGUE, selection, 'servo');
  const after: View = { ...view(stays.selection), note: stays };
  assert.equal(deselectText(after, stays), `${t('en', 'dsOn', { f: fnName('servo', 'en') })} ${t('en', 'dsStay', { x: short('servo', 'en') })}`);
  const html = chooser(after, false);
  assert.equal(count(html, 'id="note"'), 1);
  assert.ok(html.indexOf('id="note"') > html.indexOf('id="f-servo"'));
  assert.ok(html.includes(`<p class="msg" id="note" role="status">${deselectText(after, stays)}</p>`));
  // Off: the PD mini leaves with its companions, the servo supply comes in.
  const leaves = deselectEffect(CATALOGUE, selection, 'servotest');
  const de: View = { ...view(leaves.selection, 'de'), note: leaves };
  const said = deselectText(de, leaves);
  assert.ok(said.startsWith(t('de', 'dsOff', { f: fnName('servotest', 'de') })), said);
  assert.ok(said.includes(t('de', 'dsStay', { x: short('servo', 'de') })), said);
  assert.ok(said.includes(t('de', 'outParts', { x: ['pdmini', 'charger', 'usbtap'].map((id) => short(id, 'de')).join(', ') })), said);
  assert.ok(said.includes(t('de', 'inParts', { x: short('ssup', 'de') })), said);
  assert.ok(said.includes(t('de', 'offFns', { x: fnName('supply', 'de') })), said);
  // No note once the function is ticked again, and none without a change.
  assert.ok(!chooser({ ...view(selection), note: stays }, false).includes('id="note"'));
  assert.ok(!chooser({ ...view(selection), note: deselectEffect(CATALOGUE, selection, 'esc') }, false).includes('id="note"'));
  assert.ok(!chooser(view(selection), false).includes('id="note"'));
});

test('data text of the remove control reaches the page escaped', () => {
  const v = view({ functions: ['servotest'] });
  const part = { ...CATALOGUE.parts.find((p) => p.id === 'pdmini')!, id: 'x"y', short: { en: '<i>&"', de: '<i>&"' } };
  const control = removeControl(v, part);
  assert.ok(control.includes('id="rm-x&quot;y"'), control);
  assert.ok(control.includes('aria-label="Remove &lt;i&gt;&amp;&quot;"'), control);
});
