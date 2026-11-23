// Wiring of the flasher page: events, the render of the current step, and
// the browser history. Every forward step pushes a history entry with the
// wizard state in the URL hash; BACK and the browser's back are the same
// move. A reload lands at most on Connect.

import './style.css';
import {
  chooseImage, connectSerial, connectUsb, dropConn, dropImage, enterBootsel, leaveAdvanced, loadAdvanced, loadReleaseImage, loadReleases,
  releasesIn, resetConnect, write,
} from './actions.ts';
import { browserLang } from './i18n.ts';
import { $, board, errText, facts, hasSerial, hasUsb, log, readStoredLang, st, storeLang, tr, ui } from './state.ts';
import type { TaskId } from './tasks.ts';
import { renderFooter, renderStep, stepTitle } from './views.ts';
import {
  back, cancel, chooseBoard, chooseTask, clamp, connectAgain, fromHash, next, setAdvanced, startOver, stepsOf, toHash, writeAnother,
} from './wizard.ts';
import type { Drop, Move, Wiz } from './wizard.ts';

let shown = '';

function render(): void {
  const focus = document.activeElement?.id;
  document.documentElement.lang = st.lang;
  document.title = tr('title');
  $('title').textContent = tr('title');
  $('sub').textContent = tr('sub');
  for (const b of document.querySelectorAll<HTMLButtonElement>('.lang button')) {
    b.setAttribute('aria-pressed', String(b.dataset.lang === st.lang));
  }
  const none = !hasSerial && !hasUsb;
  $('notice').innerHTML = none ? `<p>${tr('noBrowser')}</p>` : '';
  $('notice').hidden = !none;
  $('step').dataset.step = st.wiz.step;
  $('step-title').textContent = stepTitle();
  $('step-body').innerHTML = renderStep();
  $('log-sum').textContent = tr('log');
  $('log').textContent = st.log.join('\n');
  $('foot').innerHTML = renderFooter();
  // Focus moves to the heading on each step change, else stays where it was.
  const key = `${st.wiz.task}/${st.wiz.step}`;
  if (key !== shown && shown) $('step-title').focus();
  else if (focus) document.getElementById(focus)?.focus();
  shown = key;
}

const urlOf = (w: Wiz): string => {
  const h = toHash(w);
  return h ? `#${h}` : location.pathname + location.search;
};
const depth = (): number => (history.state as { depth?: number } | null)?.depth ?? 0;

/** Moves to a state: a new history entry, or the current one replaced. */
function go(w: Wiz, how: 'push' | 'replace'): void {
  st.wiz = w;
  if (how === 'push') history.pushState({ depth: depth() + 1 }, '', urlOf(w));
  else history.replaceState({ depth: depth() }, '', urlOf(w));
  render();
}

function applyDrop(d: Drop): void {
  if (d.conn) resetConnect();
  if (d.image) dropImage();
}

function move(m: Move): void {
  if (m.wiz === st.wiz) return;
  applyDrop(m.drop);
  go(m.wiz, 'push');
}

ui.render = render;
ui.connected = (): void => {
  if (st.wiz.step === 'connect') go(next(st.wiz, facts()), 'push');
  else { void dropConn(); render(); }
};
ui.written = (): void => {
  if (st.writing) go(next(st.wiz, facts()), 'push');
  // Done replaces the Write entry; the browser's back from Done leads to Task (onPop).
  else if (st.outcome === 'ok') go(next(st.wiz, facts()), 'replace');
  else render();
};

/** BACK: the browser's back while the page holds an entry of its own, else the model's back in place. */
function onBack(): void {
  if (st.busy) return;
  if (depth() > 0) { history.back(); return; }
  const m = back(st.wiz, facts());
  applyDrop(m.drop);
  go(m.wiz, 'replace');
}

const ORDER = ['task', 'board', 'prepare', 'connect', 'check', 'write', 'done'];

/** The browser's back or forward: the state of the entry, held to the facts. */
function onPop(): void {
  const cur = st.wiz;
  if (st.busy) {
    // A write or a connect runs: the entry goes back on, the screen stays.
    history.pushState({ depth: depth() + 1 }, '', urlOf(cur));
    return;
  }
  if (cur.advanced && !fromHash(location.hash).advanced) leaveAdvanced();
  if (cur.step === 'done') {
    applyDrop({ conn: true, image: true });
    go(startOver(cur).wiz, 'push');
    return;
  }
  const want = fromHash(location.hash);
  // An entry of Advanced holds only after its code is in.
  if (want.advanced && !st.adv) want.advanced = false;
  const sameBoard = want.board !== null && want.task === cur.task && want.board === cur.board;
  // Back past Connect drops the connection; back to Board or Task, or to another board, the image too.
  if (!sameBoard || ORDER.indexOf(want.step) < ORDER.indexOf('connect')) {
    applyDrop({ conn: true, image: !sameBoard || want.step === 'task' || want.step === 'board' });
  }
  const w = clamp(want, facts());
  st.wiz = w;
  if (w.board && !st.image && !st.loading && !st.imageNote) chooseImage();
  if (toHash(w) !== toHash(want)) history.replaceState({ depth: depth() }, '', urlOf(w));
  render();
}

/**
 * The Advanced switch on Task: on once its code is in, off with the source,
 * "Keep settings" and the picked release reset. The Details log opens with it.
 */
async function switchAdvanced(on: boolean): Promise<void> {
  if (on) {
    try { await loadAdvanced(); } catch (e) { log(errText(e)); render(); return; }
  } else leaveAdvanced();
  ($('log-box') as HTMLDetailsElement).open = on;
  go(setAdvanced(st.wiz, on), 'replace');
}

function onChange(ev: Event): void {
  const el = ev.target as HTMLInputElement;
  if (el.id === 'adv') void switchAdvanced(el.checked);
  else if (st.wiz.advanced) st.adv?.onChange(el);
}

function onClick(ev: Event): void {
  const el = (ev.target as HTMLElement).closest('button');
  if (!el || el.disabled) return;
  if (el.dataset.lang === 'en' || el.dataset.lang === 'de') {
    st.lang = el.dataset.lang;
    storeLang(st.lang);
    render();
    return;
  }
  const f = facts();
  const pick = (w: Wiz): void => {
    if (w === st.wiz) return;
    st.wiz = w;
    if (w.board) chooseImage();
    go(w, 'push');
  };
  if (el.dataset.task) { pick(chooseTask(st.wiz, el.dataset.task as TaskId)); return; }
  if (el.dataset.board) { pick(chooseBoard(st.wiz, el.dataset.board)); return; }
  switch (el.id) {
    case 'back': onBack(); break;
    case 'cancel': move(cancel(st.wiz, f)); break;
    case 'next': go(next(st.wiz, f), 'push'); break;
    case 'connect-esp': void connectSerial(); break;
    case 'bootsel': void enterBootsel(); break;
    case 'connect-pico': void connectUsb(); break;
    case 'disconnect': void dropConn().then(render); break;
    case 'retry': void loadReleaseImage(); break;
    case 'write': void write(); break;
    case 'connect-again': move(connectAgain(st.wiz, f)); break;
    case 'again': move(writeAnother(st.wiz)); break;
    case 'over': move(startOver(st.wiz)); break;
    case 'other-board':
      // A wrong chip: Board for a task of several boards, else Task.
      if (st.wiz.task && stepsOf(st.wiz.task).includes('board')) {
        applyDrop({ conn: true, image: true });
        go({ ...st.wiz, board: null, step: 'board' }, 'push');
      } else move(cancel(st.wiz, f));
      break;
    default:
  }
}

async function main(): Promise<void> {
  st.lang = readStoredLang() ?? browserLang(navigator.languages ?? [navigator.language]);
  document.addEventListener('click', onClick);
  document.addEventListener('change', onChange);
  window.addEventListener('popstate', onPop);
  window.addEventListener('beforeunload', (e) => { if (st.writing) e.preventDefault(); });
  // A reload or a pasted link: connection and image are not in the URL, so clamp lands at most on Connect.
  const want = fromHash(location.hash);
  if (want.advanced) {
    try { await loadAdvanced(); ($('log-box') as HTMLDetailsElement).open = true; } catch (e) { log(errText(e)); want.advanced = false; }
  }
  st.wiz = clamp(want, facts());
  history.replaceState({ depth: 0 }, '', urlOf(st.wiz));
  render();
  if (!hasSerial && !hasUsb) return;
  if (board()) chooseImage();
  await loadReleases();
  releasesIn();
}

void main();
