import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Lang, ResolvedPart, Selection } from '../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, alwaysOn, deselectEffect, groups, removalEffect, resolve } from '../src/core.ts';
import { STRINGS, t, tx, esc } from '../src/i18n.ts';
import { benchParts, optionsOf, testObjectsOf } from '../src/names.ts';
import { describe, sweep } from './geometry.ts';
import { chooser, deselectText, groupId, reasonText, removalWarning, removeControl, statusText } from '../src/view/chooser.ts';
import type { Pending, View } from '../src/view/chooser.ts';
import { HELP_URL, NOTICE_URL, result, resultError } from '../src/view/result.ts';

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
    const v = view({ owned: ['sim'] }, lang);
    assert.ok(!v.setup.wires.some((w) => w.a.startsWith('sim.') || w.b?.startsWith('sim.')));
    assert.ok(order(v).includes(short('sim', lang)), order(v));
  }
});

test('the card of the DC supply of the Osoyoo breakout board carries the mark of a modelled part, no other card does', () => {
  for (const lang of LANGS) {
    const tag = `<span class="tag">${t(lang, 'modelled')}</span>`;
    assert.equal(t(lang, 'modelled'), lang === 'en' ? 'Modelled part: no product chosen' : 'Modelliertes Teil: kein Produkt gewählt');
    const page = result(view({ functions: ['servo'], options: { ...DEFAULT_OPTIONS, servoPower: 'bench', connection: 'osoyoo' } }, lang), URL, []);
    // The card under "Parts" and the row of the parts list carry the mark, once each.
    const strip = page.slice(page.indexOf('id="h-parts"'), page.indexOf('id="h-buy"'));
    const list = page.slice(page.indexOf('id="h-buy"'), page.indexOf('id="h-power"'));
    assert.equal(count(strip, tag), 1);
    assert.equal(count(list, tag), 1);
    assert.ok(list.includes(`<li data-part="dcsup">`), list);
    const card = strip.slice(strip.indexOf(tag) - 200, strip.indexOf(tag));
    assert.ok(card.includes(`<b>${esc(CATALOGUE.parts.find((p) => p.id === 'dcsup')!.name[lang])}</b>`), card);
    for (const options of [{ servoPower: 'bench', connection: 'pads' }, { servoPower: 'external', connection: 'osoyoo' }] as const) {
      const other = result(view({ functions: ['servo'], options: { ...DEFAULT_OPTIONS, ...options } }, lang), URL, []);
      assert.equal(count(other, tag), 0, JSON.stringify(options));
      assert.ok(!other.includes('data-part="dcsup"'), JSON.stringify(options));
    }
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
      // No heading names a test object as a part the bench needs.
      for (const part of CATALOGUE.parts.filter((x) => x.testObject === true)) assert.ok(!legend.includes(short(part.id, lang)), `${group.key}: ${legend}`);
      // A function every setup runs is a row without a control; every other function has one tick.
      const always = group.functions.filter((id) => alwaysOn(CATALOGUE, CATALOGUE.functions.find((f) => f.id === id)!));
      const ticks = [...body.matchAll(/ data-f="([^"]+)"/g)].map((m) => m[1]);
      const rows = [...body.matchAll(/<li id="b-([^"]+)"/g)].map((m) => m[1]);
      assert.deepEqual([rows, ticks], [always, group.functions.filter((id) => !always.includes(id))], group.key);
      if (group.parts.length > 0) {
        assert.deepEqual(always, [], group.key);
        for (const id of [...group.also, ...group.oneOf.flat()]) assert.ok(text(body).includes(short(id, lang)), `${group.key}: ${id}`);
      } else {
        // The 3 functions that drive a test object on the base setup: a tick each, under their own sentence.
        assert.deepEqual(ticks, ['servo', 'servoprog', 'esc']);
        assert.ok(body.indexOf('id="basetick"') > body.indexOf('</ul>') && body.indexOf('id="basetick"') < body.indexOf('id="f-servo"'));
        assert.ok(body.includes(STRINGS.baseTick[col(lang)]));
        assert.ok(!body.slice(0, body.indexOf('</ul>')).includes('<input') && !body.includes('<button'), 'the list of the base setup holds a control');
      }
    }
    assert.deepEqual(list.map(groupId), ['g-base', 'g-pdmini', 'g-ina3221', 'g-as5600', 'g-ina228', 'g-sd', 'g-estop', 'g-sim-tap']);
    for (const f of CATALOGUE.functions) {
      assert.equal(count(html, `id="f-${f.id}"`) + count(html, `id="b-${f.id}"`), 1, f.id);
      assert.equal(count(html, `id="f-${f.id}"`), !alwaysOn(CATALOGUE, f) ? 1 : 0, f.id);
    }
    assert.ok(html.includes(STRINGS.chooseHint[col(lang)]));
  }
});

test('a group names a part that is one of several boards and the parts the rules add', () => {
  const en = chooser(view({}), false);
  const escpow = text(fieldset(en, 'g-ina228'));
  assert.ok(escpow.startsWith(`Needs: ${short('ina228', 'en')}`), escpow);
  assert.ok(!escpow.slice(0, escpow.indexOf(':', 7)).includes(short('esc', 'en')), escpow);
  assert.ok(new RegExp(`${short('ina228', 'en')}: one of [^.]*MATEK[^.]*, or [^.]*XT90`).test(escpow), escpow);
  assert.ok(escpow.includes(`Also needs: ${short('pull', 'en')}.`), escpow);
  const pd = ['pdmini', 'charger', 'usbtap'].map((id) => short(id, 'en')).join(' + ');
  assert.ok(escpow.includes(`Also needs, by variant: ${short('batt', 'en')}, or ${pd}.`), escpow);
  // A servo takes its power from the servo supply or from a pin of the bench: the 5 V pin adds no part,
  // the V pin of the Osoyoo breakout board adds its DC supply.
  const servocur = text(fieldset(en, 'g-ina3221'));
  assert.ok(servocur.startsWith(`Needs: ${short('ina3221', 'en')}`), servocur);
  assert.ok(servocur.includes(`Also needs, by variant: ${short('ssup', 'en')}, or no further part, or ${short('dcsup', 'en')}.`), servocur);
  assert.ok(text(fieldset(en, 'g-pdmini')).startsWith(`Needs: ${short('pdmini', 'en')} Also needs: ${short('charger', 'en')}, ${short('usbtap', 'en')}.`));
  const de = text(fieldset(chooser(view({}, 'de'), false), 'g-ina228'));
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
    // The servo is connected and the PD mini is in the setup: 2 more functions run without a tick.
    for (const id of ['servo', 'supply']) {
      assert.ok(!row(html, `f-${id}`).includes(' checked'), id);
      assert.ok(row(html, `f-${id}`).includes('data-state="inc"'), id);
    }
    assert.ok(row(html, 'f-supply').includes(STRINGS.stInc[col(lang)]));
    assert.ok(row(html, 'f-servo').includes(t(lang, 'stConn', { x: short('servo', lang) })));
    // Not connected: the row of the function that drives the other test object names what a tick connects.
    assert.ok(row(html, 'f-esc').includes('data-state="off"'));
    assert.equal(status(v, 'esc'), status(view({}, lang), 'esc'));
    assert.ok(status(v, 'esc').includes(short('esc', lang)));
    // Off: the row names the part that is missing.
    assert.ok(row(html, 'f-angle').includes('data-state="off"'));
    assert.equal(status(v, 'angle'), STRINGS.adds[col(lang)] + short('as5600', lang));
    // The heading of a group whose parts the setup holds says so.
    assert.ok(fieldset(html, 'g-pdmini').includes(STRINGS.gIn[col(lang)]));
    for (const id of ['g-base', 'g-ina228', 'g-ina3221']) assert.ok(!fieldset(html, id).includes(STRINGS.gIn[col(lang)]), id);
    // The result names the same states.
    const res = result(v, URL, []);
    assert.equal(count(res, `<small>${STRINGS.stOwn[col(lang)]}</small>`), 1);
    assert.equal(count(res, `<small>${STRINGS.stInc[col(lang)]}</small>`), 1);
    assert.equal(count(res, `<small>${t(lang, 'stConn', { x: short('servo', lang) })}</small>`), 1);
  }
  assert.equal([...chooser(view({}), true).matchAll(/type="checkbox"[^>]* checked/g)].length, 0);
});

test('a function whose parts are there under the other variant names the variant it needs', () => {
  const v = view({ functions: ['esc'], owned: ['pdmini'] });
  assert.equal(v.setup.functions.find((f) => f.fn.id === 'stick')?.enabled, false);
  assert.equal(status(v, 'stick'), 'Not active: needs ESC power B.');
  assert.equal(status(view({ functions: ['esc'], owned: ['pdmini'] }, 'de'), 'stick'), 'Nicht aktiv: braucht ESC-Versorgung B.');
});

test('every part beyond the base setup has a remove control with a name, a base part and a test object have none', () => {
  for (const lang of LANGS) {
    const v = view({ functions: CATALOGUE.functions.map((f) => f.id) }, lang);
    const html = result(v, URL, []);
    assert.ok(v.setup.parts.some((r) => r.part.locked) && v.setup.parts.some((r) => !r.part.locked));
    assert.deepEqual(v.setup.parts.filter((r) => r.part.testObject === true).map((r) => r.part.id), ['servo', 'esc']);
    for (const r of v.setup.parts) {
      const control = removeControl(v, r.part);
      const fixed = r.part.locked || r.part.testObject === true;
      assert.equal(count(html, `id="rm-${r.part.id}"`), fixed ? 0 : 1, r.part.id);
      if (fixed) assert.equal(control, '', r.part.id);
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
    // The servo is no part of the bench: the question lists it neither as leaving nor as kept, and it stays connected.
    assert.ok(!said.includes(t(lang, 'rmKept', { x: short('servo', lang) })), said);
    assert.deepEqual(removalEffect(CATALOGUE, v.selection, 'pdmini').objectsOut, []);
    assert.deepEqual(removalEffect(CATALOGUE, v.selection, 'pdmini').selection.functions, ['servo']);
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
      const v: View = { ...view({ owned: ['sim', 'sd'] }, lang), pending: { part: 'sim', at } };
      const warning = removalWarning(v);
      const said = text(warning);
      assert.ok(said.startsWith(t(lang, 'rmAsk', { p: short('sim', lang) })), said);
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
      if (r.part.locked || r.part.testObject === true) {
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
  assert.ok(asked >= selections.length - CATALOGUE.functions.filter((f) => alwaysOn(CATALOGUE, f)).length, `${asked} questions`);
});

test('the question under "Parts you own" stands under its row, and no question shows for an absent part', () => {
  const v: View = { ...view({ functions: ['servotest'] }, 'de'), pending: { part: 'pdmini', at: 'own' } };
  const html = chooser(v, true);
  assert.equal(count(html, 'id="rmwarn"'), 1);
  const rows = [...html.matchAll(/id="p-([^"]+)"/g)].map((m) => m[1] as string);
  const next = rows[rows.indexOf('pdmini') + 1] as string;
  assert.ok(html.indexOf('id="rmwarn"') > html.indexOf('id="p-pdmini"'));
  assert.ok(html.indexOf('id="rmwarn"') < html.indexOf(`id="p-${next}"`));
  assert.ok(row(html, 'p-pdmini').includes(' checked'));
  assert.ok(!result(v, URL, []).includes('id="rmwarn"'));
  // A test object is no part to own: no row, and no question.
  assert.deepEqual(rows, CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id));
  assert.ok(!rows.includes('servo') && !rows.includes('esc'));
  for (const part of ['servo', 'esc', 'sd', 'panel', 'warp']) {
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
  assert.ok(ticked >= 2);
  // The T-plug has no row: the PD mini brings it.
  assert.ok(!html.includes('id="p-usbtap"'));
  assert.ok(html.includes(`<span class="tag">${t('en', 'ownN', { n: ticked })}</span>`));
  assert.ok(html.includes(STRINGS.ownHint[0]));
});

test('the note after a function leaves the selection states what stays and what leaves', () => {
  const selection: Selection = { functions: ['servotest', 'servo'], owned: [], options: { ...DEFAULT_OPTIONS } };
  // Still active: the servo stays connected for the other function.
  const stays = deselectEffect(CATALOGUE, selection, 'servo');
  const after: View = { ...view(stays.selection), note: stays };
  assert.equal(deselectText(after, stays), `${t('en', 'dsOn', { f: fnName('servo', 'en') })} ${t('en', 'dsConn', { x: short('servo', 'en') })}`);
  const html = chooser(after, false);
  assert.equal(count(html, 'id="note"'), 1);
  assert.ok(html.indexOf('id="note"') > html.indexOf('id="f-servo"'));
  assert.ok(html.includes(`<p class="msg" id="note" role="status">${deselectText(after, stays)}</p>`));
  // Off: the PD mini leaves with its companions, the servo supply comes in.
  const leaves = deselectEffect(CATALOGUE, selection, 'servotest');
  const de: View = { ...view(leaves.selection, 'de'), note: leaves };
  const said = deselectText(de, leaves);
  assert.ok(said.startsWith(t('de', 'dsOff', { f: fnName('servotest', 'de') })), said);
  // The servo is no part of the bench: the note lists it neither as staying nor as leaving.
  assert.ok(!said.includes(t('de', 'dsConn', { x: short('servo', 'de') })) && !said.includes(STRINGS.outObjects[1].split('{')[0] as string), said);
  assert.deepEqual(leaves.objectsOut, []);
  // The last function that drives the servo leaves: the note says the servo is no longer connected.
  const alone: Selection = { functions: ['servo'], owned: [], options: { ...DEFAULT_OPTIONS } };
  const gone = deselectEffect(CATALOGUE, alone, 'servo');
  assert.deepEqual(gone.objectsOut, ['servo']);
  for (const lang of LANGS) {
    assert.ok(deselectText({ ...view(gone.selection, lang), note: gone }, gone).includes(t(lang, 'outObjects', { x: short('servo', lang) })));
  }
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

test('the base setup lists the functions that need no part, without a tick', () => {
  const base = CATALOGUE.functions.filter((f) => alwaysOn(CATALOGUE, f));
  assert.ok(base.length > 0);
  assert.deepEqual(groups(CATALOGUE)[0]?.functions.slice(0, base.length), base.map((f) => f.id));
  for (const lang of ['en', 'de'] as Lang[]) {
    const v = view({}, lang);
    const picker = chooser(v, false);
    assert.equal(picker.indexOf('<fieldset'), picker.indexOf('<fieldset class="grp" id="g-base">'), 'the base group is not the first group');
    const group = fieldset(picker, 'g-base');
    assert.ok(group.includes(`<legend>${STRINGS.gBase[col(lang)]}</legend>`), group);
    const list = group.slice(0, group.indexOf('</ul>'));
    assert.ok(list.includes(STRINGS.baseHint[col(lang)]));
    assert.ok(!list.includes('<input') && !list.includes('<button') && !list.includes('tabindex'));
    const html = result(v, 'https://example.invalid/', []);
    assert.deepEqual(benchParts(v.setup), v.setup.parts);
    assert.ok(html.includes(t(lang, 'sum', { p: benchParts(v.setup).length, w: v.setup.wires.length, f: base.length })));
    assert.ok(html.includes(`<h4>${CATALOGUE.baseFunction[lang]}</h4>`));
    assert.ok(!html.includes(STRINGS.withParts[col(lang)]));
    for (const fn of base) {
      // Each row: the name and its state in words. No row carries a mark.
      const at = list.indexOf(`<li id="b-${fn.id}">`);
      assert.ok(at >= 0, fn.id);
      const item = list.slice(at, list.indexOf('</li>', at));
      assert.ok(item.includes(`<b>${fn.name[lang]}</b>`), fn.id);
      assert.ok(!item.includes('class="tag"'), fn.id);
      assert.ok(item.includes(`<small>${STRINGS.stBase[col(lang)]}</small>`), fn.id);
      assert.equal(status(v, fn.id), STRINGS.stBase[col(lang)]);
      assert.ok(html.includes(`<li>${fn.name[lang]}`), fn.id);
      assert.ok(!picker.includes(`id="f-${fn.id}"`), fn.id);
    }
  }
});

test('a selection that names a function of the base setup shows it as included, never as selected by the visitor', () => {
  const base = CATALOGUE.functions.filter((f) => alwaysOn(CATALOGUE, f)).map((f) => f.id);
  for (const lang of LANGS) {
    const v = view({ functions: [...base, 'servotest'] }, lang);
    const picker = chooser(v, false);
    const list = fieldset(picker, 'g-base');
    assert.ok(!list.includes(STRINGS.stOwn[col(lang)]) && !list.includes(' checked'), list);
    for (const id of base) assert.equal(status(v, id), STRINGS.stBase[col(lang)], id);
    assert.equal(status(v, 'servotest'), STRINGS.stOwn[col(lang)]);
    assert.equal(count(picker, ' checked'), count(chooser(view({ functions: ['servotest'] }, lang), false), ' checked'));
    // The result lists them under the heading of the base setup, without a state of their own.
    const html = result(v, URL, []);
    const [first, second] = [html.indexOf(`<h4>${CATALOGUE.baseFunction[lang]}</h4>`), html.indexOf(`<h4>${STRINGS.withParts[col(lang)]}</h4>`)];
    assert.ok(first > 0 && second > first);
    assert.ok(!html.slice(first, second).includes('<small>'));
    for (const id of base) assert.ok(html.slice(first, second).includes(`<li>${fnName(id, lang)}`), id);
    // The question before a part leaves names no function of the base setup.
    for (const r of v.setup.parts) {
      if (r.part.locked) continue;
      const question = removalWarning({ ...v, pending: { part: r.part.id, at: 'result' } });
      for (const id of base) assert.ok(!text(question).includes(fnName(id, lang)), `${r.part.id}: ${id}`);
    }
    // Without its tick such a function stays on, and the note says the base setup runs it.
    const said = deselectText(v, deselectEffect(CATALOGUE, v.selection, base[0] as string));
    assert.equal(said, `${t(lang, 'dsOn', { f: fnName(base[0] as string, lang) })} ${STRINGS.dsBase[col(lang)]}`);
  }
});

test('a setup with parts keeps the functions that need no part and lists the others under their own heading', () => {
  const v = view({ functions: ['servo'], owned: ['sd'] });
  const html = result(v, 'https://example.invalid/', []);
  const at = html.indexOf(`<h4>${STRINGS.withParts[0]}</h4>`);
  assert.ok(at > html.indexOf(`<h4>${CATALOGUE.baseFunction.en}</h4>`));
  const rest = html.slice(at, html.indexOf('</ul>', at));
  assert.ok(rest.includes('<ul class="plain" id="does-parts">'));
  const withParts = v.setup.functions.filter((f) => f.enabled && !alwaysOn(CATALOGUE, f.fn));
  assert.ok(withParts.length > 0);
  assert.equal(rest.split('<li>').length - 1, withParts.length);
  // Each of them states how it is in the setup.
  assert.equal(rest.split('<small>').length - 1, withParts.length);
  for (const fn of CATALOGUE.functions) {
    if (alwaysOn(CATALOGUE, fn)) assert.ok(html.slice(0, at).includes(`<li>${fn.name.en}`), fn.id);
  }
  assert.ok(chooser(v, false).includes('id="f-servo"'));
});

/** The markup of a list of the result, by id. */
function listOf(html: string, id: string): string {
  const at = html.indexOf(` id="${id}">`);
  if (at < 0) return '';
  const end = html.indexOf('<h3', at);
  return html.slice(at, end < 0 ? html.length : end);
}

test('a test object stands under its own heading with the functions that use it, never in the parts list', () => {
  for (const lang of LANGS) {
    // Edges: 1 function with a test object alone, with 1 further part, 2 test objects, and all of it.
    const cases: Array<[string[], string[]]> = [
      [['servo'], ['servo']], [['esc'], ['esc']], [['angle'], ['servo']], [['servo', 'esc'], ['servo', 'esc']],
      [['stick', 'servotest', 'tap'], ['servo', 'esc']], [CATALOGUE.functions.map((f) => f.id), ['servo', 'esc']],
    ];
    for (const [functions, objects] of cases) {
      const v = view({ functions }, lang);
      const html = result(v, URL, []);
      assert.deepEqual(testObjectsOf(v.setup).map((r) => r.part.id), objects, functions.join());
      assert.equal(count(html, `<h3 id="h-tested">${STRINGS.tested[col(lang)]}</h3>`), 1);
      assert.ok(html.indexOf('id="h-tested"') > html.indexOf('<ul class="parts">'));
      assert.ok(html.indexOf('id="h-tested"') < html.indexOf('id="h-power"'));
      const parts = html.slice(html.indexOf('<ul class="parts">'), html.indexOf('id="h-tested"'));
      const tested = listOf(html, 'tested');
      assert.equal(count(tested, '<li>'), objects.length);
      assert.ok(!tested.includes('<button') && !tested.includes('<input'), 'a test object carries a control');
      for (const id of objects) {
        const part = CATALOGUE.parts.find((x) => x.id === id)!;
        assert.ok(tested.includes(`<b>${part.name[lang]}</b>`), id);
        assert.ok(!parts.includes(`<b>${part.name[lang]}</b>`), `${id} is in the parts list`);
        assert.ok(!html.includes(`id="rm-${id}"`), id);
        // Every enabled function that drives it is named, and no other.
        const users = v.setup.functions.filter((f) => f.fn.drives === id);
        assert.ok(users.some((f) => f.enabled), id);
        const item = tested.slice(tested.indexOf(`<b>${part.name[lang]}</b>`));
        const line = text(item.slice(0, item.indexOf('</li>')));
        assert.ok(line.includes(STRINGS.usedBy[col(lang)].trim()), line);
        for (const f of users) assert.equal(line.includes(f.fn.name[lang]), f.enabled, `${id}: ${f.fn.id}`);
      }
      // The parts list and the count of the summary hold the bench hardware only.
      assert.equal(count(parts, '<b>'), benchParts(v.setup).length);
      assert.ok(html.includes(t(lang, 'sum', { p: benchParts(v.setup).length, w: v.setup.wires.length, f: v.setup.functions.filter((f) => f.enabled).length })));
      assert.ok(!text(parts).includes(STRINGS.neededBy[col(lang)].trim() + ' ' + fnName('servo', lang) + '.'), 'a part is needed by "drive a servo"');
    }
    // No test object: the heading stays and a sentence says so.
    const none = result(view({ owned: ['sd'] }, lang), URL, []);
    assert.equal(count(none, 'id="h-tested"'), 1);
    assert.ok(!none.includes('id="tested"'));
    assert.ok(none.includes(STRINGS.testedNone[col(lang)]));
  }
});

test('the result lists the power sources that fit the connected test objects, each with every line of the data', () => {
  for (const lang of LANGS) {
    for (const functions of [[], ['servo'], ['esc'], ['servo', 'esc'], ['stick'], ['servotest', 'escpow']]) {
      for (const servoPower of ['external', 'bench'] as const) {
        const v = view({ functions, options: { ...DEFAULT_OPTIONS, servoPower } }, lang);
        const html = result(v, URL, []);
        const what = `${functions.join()} ${servoPower}`;
        assert.equal(count(html, `<h3 id="h-power">${STRINGS.power[col(lang)]}</h3>`), 1, what);
        const feeds = [...new Set(v.setup.power.map((e) => e.source.feeds))];
        assert.deepEqual(feeds, ['logic', ...testObjectsOf(v.setup).map((r) => r.part.id)], what);
        assert.deepEqual([...html.matchAll(/id="power-([a-z]+)"/g)].map((m) => m[1]), feeds, what);
        assert.deepEqual([...html.matchAll(/<li id="pw-([^"]+)">/g)].map((m) => m[1]), v.setup.power.map((e) => e.source.id), what);
        for (const entry of v.setup.power) {
          const at = html.indexOf(`<li id="pw-${entry.source.id}">`);
          const next = html.indexOf('<li id="pw-', at + 1);
          const item = html.slice(at, next < 0 ? html.indexOf('<h3', at) : next);
          assert.ok(item.includes(`<b>${esc(entry.source.label[lang])}</b>`), entry.source.id);
          assert.ok(item.includes(esc(entry.source.text[lang])), entry.source.id);
          assert.equal(item.includes(`<span class="tag">${STRINGS[entry.source.choice === undefined ? 'gIn' : 'gInOne'][col(lang)]}</span>`), entry.used, `${what}: ${entry.source.id}`);
          for (const line of [...entry.source.pros, ...entry.source.cons, ...entry.source.unknowns]) assert.ok(item.includes(esc(line[lang])), entry.source.id);
          assert.equal(count(item, '<span class="kind">'), entry.source.pros.length + entry.source.cons.length + entry.source.unknowns.length, entry.source.id);
          // No source repeats the state of the project.
          assert.ok(!/not run|nicht gelaufen|has met no/i.test(item), entry.source.id);
        }
        // A connected test object has at least 1 source, and the wires of the setup use at least 1 of them.
        for (const r of testObjectsOf(v.setup)) {
          const own = v.setup.power.filter((e) => e.source.feeds === r.part.id);
          assert.ok(own.length > 0, `${what}: ${r.part.id}`);
          assert.ok(own.some((e) => e.used), `${what}: ${r.part.id}`);
        }
      }
    }
  }
});

test('every setup that wires the plus of the servo to a pin of the bench carries the warning, and no other setup does', () => {
  const note = CATALOGUE.checks.find((c) => c.id === 'servo-bench-supply')!;
  assert.equal(note.kind, 'note');
  let fed = 0;
  let other = 0;
  for (const { selection } of sweep({ CATALOGUE, resolve, DEFAULT_OPTIONS })) {
    const v = view(selection);
    // The plus of the servo on a pin of the bench: a wire or a stub from the 5 V pad or a V pin of the strip.
    const bench = v.setup.wires.filter((w) => w.cls === 'power' && w.a === 'servo.POS' && (w.b === null || w.b.startsWith('pad.')));
    const numbered = v.setup.wires.some((w) => [59, 60, 61, 62].includes(w.n));
    assert.equal(bench.length > 0 || numbered, numbered, describe(selection));
    const html = result(v, URL, []);
    assert.equal(html.includes(note.text.en), numbered, describe(selection));
    assert.equal(v.setup.checks.some((c) => c.id === note.id), numbered, describe(selection));
    if (numbered) {
      fed += 1;
      assert.equal(v.setup.power.filter((e) => e.used && e.source.feeds === 'servo').every((e) => ['copro-5v', 'strip-v'].includes(e.source.id)), true, describe(selection));
    } else other += 1;
  }
  assert.ok(fed > 20 && other > 20, `${fed} setups on a pin of the bench, ${other} without`);
  // The German text stands in the German result.
  const de = view({ functions: ['servo'], options: { ...DEFAULT_OPTIONS, servoPower: 'bench' } }, 'de');
  assert.ok(result(de, URL, []).includes(note.text.de));
  assert.ok(!result(view({ functions: ['servo'] }, 'de'), URL, []).includes(note.text.de));
});

test('the state of the project stands once, in the footer, and no row repeats it', () => {
  for (const lang of LANGS) {
    for (const functions of [[], ['servo', 'esc'], CATALOGUE.functions.map((f) => f.id)]) {
      const v = view({ functions }, lang);
      const html = result(v, URL, []);
      assert.equal(count(html, `<p id="dev-state">${STRINGS.devState[col(lang)]}</p>`), 1);
      assert.equal(count(html, STRINGS.devState[col(lang)]), 1);
      assert.ok(html.indexOf('id="dev-state"') > html.indexOf('<footer>'));
      const pages = html + chooser(v, false) + chooser(v, true);
      assert.ok(!/not run on hardware|nicht auf Hardware gelaufen|has not run|nicht gelaufen/i.test(pages), functions.join());
    }
  }
  assert.ok(!('nrh' in STRINGS));
});

test('the footer links to the trademark notice of the repository in both languages', () => {
  assert.equal(NOTICE_URL, 'https://github.com/subtilitas/rcbench/blob/main/NOTICE');
  const name = { en: 'Trademark notice', de: 'Markenhinweis' };
  for (const lang of LANGS) {
    const link = `<a id="noticelink" href="${NOTICE_URL}" rel="noopener noreferrer">${name[lang]}</a></p></footer>`;
    for (const html of [result(view({ functions: ['servo'] }, lang), URL, []), resultError(lang, CATALOGUE, URL)]) {
      assert.equal(count(html, '<footer>'), 1);
      // Besides the notice, only the maker pages and web searches of the parts list leave the page, each in a new tab.
      const away = count(html, 'href="http') - 1;
      assert.equal(away, count(html, 'target="_blank" rel="noopener noreferrer"'), 'a link of the parts list opens in a new tab');
      assert.equal(count(html.slice(html.indexOf('<footer>')), 'href="http'), 1, 'one link in the footer');
      assert.ok(html.endsWith(link), html.slice(-200));
      // The sentence on the names stands before the link, and the link has no tabindex that takes it out of the Tab order.
      assert.ok(html.includes(`${STRINGS.foot[col(lang)]} <a id="noticelink"`));
      assert.ok(!/<a id="noticelink"[^>]*tabindex/.test(html));
    }
  }
});

test('the footer links to the panel help of its language, on the same site', () => {
  assert.deepEqual(HELP_URL, { en: 'help/index.html', de: 'help/de/index.html' });
  const name = { en: 'Panel help', de: 'Panel-Hilfe' };
  for (const lang of LANGS) {
    for (const html of [result(view({ functions: ['servo'] }, lang), URL, []), resultError(lang, CATALOGUE, URL)]) {
      const foot = html.slice(html.indexOf('<footer>'));
      assert.equal(count(foot, 'id="helplink"'), 1);
      assert.ok(foot.includes(`<p><a id="helplink" href="${HELP_URL[lang]}">${name[lang]}</a></p>`), foot);
    }
  }
});

test('the reason line of a rule part: "Included with these parts" after a bench part, "Needed by" after any other cause', () => {
  const follows = CATALOGUE.rules.filter((r) => 'has' in (r.when ?? {}) && !CATALOGUE.parts.find((p) => p.id === (r.when as { has: string }).has)?.testObject);
  // The data decides: the rules whose condition is one bench part.
  assert.deepEqual(follows.map((r) => r.id), ['pd-charger', 'pd-tplug']);
  for (const lang of LANGS) {
    for (const rule of CATALOGUE.rules) {
      const reason: ResolvedPart['addedBy'][number] = { kind: 'rule', id: rule.id, because: rule.because };
      const part = { part: CATALOGUE.parts.find((p) => p.id === rule.adds)!, addedBy: [reason] } as ResolvedPart;
      const line = reasonText(part, view({}, lang));
      const label = follows.includes(rule) ? STRINGS.stIncBy[lang === 'en' ? 0 : 1] : STRINGS.neededBy[lang === 'en' ? 0 : 1];
      assert.equal(line, `${label}${tx(lang, rule.because)}.`, `${rule.id} ${lang}`);
    }
  }
  // In a resolved setup: ESC power B is a cause, not a part.
  const v = view({ functions: ['esc'], options: { ...DEFAULT_OPTIONS, escPower: 'pd' } }, 'en');
  assert.equal(reasonText(v.setup.parts.find((r) => r.part.id === 'pdmini')!, v), 'Needed by: ESC power B.');
});

test('a part a rule adds reads "Included with these parts" in English and "Enthalten bei diesen Teilen" in German', () => {
  assert.equal(STRINGS.stInc[0], 'Included with these parts.');
  assert.equal(STRINGS.stInc[1], 'Enthalten bei diesen Teilen.');
  const reasons = { en: 'Included with these parts: PD mini.', de: 'Enthalten bei diesen Teilen: PD mini.' };
  for (const lang of LANGS) {
    const v = view({ owned: ['pdmini'] }, lang);
    const tap = v.setup.parts.find((r) => r.part.id === 'usbtap')!;
    assert.deepEqual(tap.addedBy.map((a) => a.kind), ['rule']);
    assert.equal(reasonText(tap, v), reasons[lang]);
    assert.ok(text(result(v, URL, [])).includes(reasons[lang]), lang);
    // The T-plug has no tick in the chooser.
    assert.ok(!chooser(v, true).includes('id="p-usbtap"'));
  }
});
