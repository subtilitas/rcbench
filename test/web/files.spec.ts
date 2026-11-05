import { expect, test, type APIRequestContext, type Locator, type Page } from '@playwright/test';
import { crc32 } from 'node:zlib';

// The Wi-Fi page's file manager in a browser, against the mock of the panel
// (test/host/wa_mock_server.py).  The page is the one wa_page_main() builds,
// with the panel's highlighter; the API answers as wa_api.c does.  Every
// test runs in English and in German.  The texts a test expects are read
// from ui_text.def through the mock, so a button is looked up by the
// words the panel shows.

type Lang = 'en' | 'de';
const LANGS: Lang[] = ['en', 'de'];
const LANG_NAME: Record<Lang, string> = { en: 'English', de: 'German' };

type Entry = { en: string; de: string };

const IDS = [
  'LOG_RESCAN', 'LOG_OPEN', 'LOG_NEXT', 'LOG_PREV', 'LOG_DELETE',
  'LOG_DELETE_TITLE', 'LOG_DELETED', 'FM_ENTRIES', 'FM_ENTRIES_OF',
  'FM_INFO', 'FM_RENAME', 'FM_RENAMED', 'FM_MOVE', 'FM_MOVED', 'FM_NEW',
  'FM_MADE', 'FM_TK_FOLDER', 'FM_TK_RENAME', 'FM_CLOSE', 'ED_EDIT',
  'ED_SAVED', 'ED_Q_CHANGED', 'ED_READ_AGAIN', 'ED_OVERWRITE', 'SET_SAVE',
  'SET_LEAVE_TITLE', 'SET_STAY', 'P_FM_UPLOAD', 'P_FM_REPLACE',
  'P_FM_REPLACE_Q', 'P_FM_DOWNLOAD', 'P_API_CHANGED', 'P_API_NOT_EMPTY',
  'P_API_ARMED', 'P_API_WRITES_OFF', 'P_API_WRITES_HOLD',
];

const REPORT = 'ATEST001.TXT';
const REPORT_STAMP = 1790000000;

let mock: APIRequestContext;
let texts: Record<string, Entry> = {};
let script_errors: string[] = [];

test.beforeAll(async ({ playwright }) => {
  mock = await playwright.request.newContext({ baseURL: `http://127.0.0.1:${process.env.E2E_PORT ?? 4180}` });
  for (const id of IDS) {
    const r = await mock.get(`/__mock/text?id=${id}`);
    expect(r.status(), `ui_text.def has ${id}`).toBe(200);
    texts[id] = await r.json();
  }
});

test.afterAll(async () => {
  await mock.dispose();
});

test.afterEach(() => {
  expect(script_errors, 'the page raised no uncaught error').toEqual([]);
});

/** The page's printf: each %s, %d and %u replaced by one argument in order. */
function fmt(s: string, ...args: (string | number)[]): string {
  let k = 0;
  return s.replace(/%[sdu]/g, () => String(args[k++]));
}

type Opts = { writes?: 'OPEN' | 'HOLD' | 'OFF'; window?: boolean; armed?: boolean; logging?: boolean };

/** One browser session of the page in a language, with the card as the tests start from. */
class Fm {
  readonly page: Page;
  readonly lang: Lang;

  constructor(page: Page, lang: Lang) {
    this.page = page;
    this.lang = lang;
  }

  /** The text of a ui_text.def entry in the session's language. */
  t(id: string, ...args: (string | number)[]): string {
    return fmt(texts[id][this.lang], ...args);
  }

  pane(i: number): Locator {
    return this.page.locator('#fm .pane').nth(i);
  }

  rows(i: number): Locator {
    return this.pane(i).locator('tbody tr:not(.none)');
  }

  /** The row named @p name in pane @p i. */
  row(i: number, name: string): Locator {
    return this.rows(i).filter({ has: this.page.locator('.n', { hasText: new RegExp(`^${name.replace('.', '\\.')}$`) }) });
  }

  async select(i: number, name: string): Promise<void> {
    await this.row(i, name).click();
    await expect(this.row(i, name)).toHaveClass(/sel/);
  }

  /** A button of the two rows under the panes. */
  bar(id: string): Locator {
    return this.page.locator('#fm .bar').getByRole('button', { name: this.t(id), exact: true });
  }

  /** A button of the box a write opens. */
  dialog(id: string): Locator {
    return this.page.locator('.wbox .card').getByRole('button', { name: this.t(id), exact: true });
  }

  /** A button of the viewer's head. */
  head(id: string): Locator {
    return this.page.locator('.viewer .vh').getByRole('button', { name: this.t(id), exact: true });
  }

  get msg(): Locator {
    return this.page.locator('#fm .bar .msg').first();
  }

  get viewerText(): Locator {
    return this.page.locator('.viewer .scroll > pre.text');
  }

  get note(): Locator {
    return this.page.locator('.viewer .vh .vn');
  }

  /** Opens text file @p name of pane @p i in the viewer. */
  async view(i: number, name: string): Promise<void> {
    await this.select(i, name);
    await this.bar('LOG_OPEN').click();
    await expect(this.page.locator('.viewer')).toBeVisible();
  }

  /** The lines the viewer draws, each as its spans [class, text]. */
  async viewerLines(): Promise<string[][][]> {
    return this.viewerText.evaluate((pre) => {
      const lines: string[][][] = [[]];
      for (const n of Array.from(pre.childNodes)) {
        if (n.nodeType === Node.TEXT_NODE) {
          for (const c of n.textContent ?? '') {
            if (c === '\n') lines.push([]);
          }
        } else {
          const el = n as HTMLElement;
          lines[lines.length - 1].push([el.className.slice(2), el.textContent ?? '']);
        }
      }
      lines.pop();
      return lines;
    });
  }

  async tree(): Promise<{ dirs: string[]; files: Record<string, number> }> {
    return (await mock.get('/__mock/tree')).json();
  }

  async read(path: string): Promise<string> {
    return (await mock.get(`/__mock/read?path=${encodeURIComponent(path)}`)).text();
  }

  async log(): Promise<{ method: string; url: string; status: number; if_match: string | null; if_none_match: string | null }[]> {
    return (await mock.get('/__mock/log')).json();
  }

  async puts() {
    return (await this.log()).filter((r) => r.method === 'PUT');
  }
}

/** Resets the mock to a state and language and opens the page. */
async function open(page: Page, lang: Lang, opts: Opts = {}): Promise<Fm> {
  script_errors = [];
  page.on('pageerror', (e) => script_errors.push(e.message));
  const r = await mock.post('/__mock/reset', { data: { lang, ...opts } });
  expect(r.ok()).toBe(true);
  const fm = new Fm(page, lang);
  await page.goto('/');
  await expect(fm.rows(0).first()).toBeVisible();
  await expect(fm.rows(1).first()).toBeVisible();
  return fm;
}

for (const lang of LANGS) {
  test.describe(`the file manager in ${LANG_NAME[lang]}`, () => {
    test('the page opens on the card root and the User log folder in the panel language', async ({ page }) => {
      const origins = new Set<string>();
      page.on('request', (q) => origins.add(new URL(q.url()).origin));
      const fm = await open(page, lang);
      await expect(page.locator('html')).toHaveAttribute('lang', lang);
      await expect(fm.rows(0)).toHaveText([/^BENCH/, /^USER/, /^HELLO\.TXT/]);
      await expect(fm.pane(0).locator('.pc')).toHaveText(fm.t('FM_ENTRIES', 3));
      await expect(fm.pane(0).locator('.up')).toHaveText('RCBENCH');
      await expect(fm.pane(1).locator('.up')).toHaveText('‹ USER/LOGS');
      await expect(fm.bar('LOG_RESCAN')).toBeVisible();
      await expect(fm.bar('LOG_OPEN')).toBeVisible();
      await expect(fm.bar('FM_INFO')).toBeVisible();
      expect([...origins], 'the page loads nothing from elsewhere').toEqual([page.url().replace(/\/$/, '')]);
    });

    test('a folder of 49 entries is read as a page of 48 and a page of 1', async ({ page }) => {
      const fm = await open(page, lang);
      const right = fm.pane(1);
      await expect(fm.rows(1)).toHaveCount(48);
      await expect(right.locator('.pc')).toHaveText(fm.t('FM_ENTRIES_OF', 1, 48, 49));
      await expect(fm.rows(1).first()).toContainText('OLD');
      await expect(right.getByRole('button', { name: fm.t('LOG_PREV'), exact: true })).toBeDisabled();
      await right.getByRole('button', { name: fm.t('LOG_NEXT'), exact: true }).click();
      await expect(fm.rows(1)).toHaveCount(1);
      await expect(fm.rows(1).first()).toContainText('NOTES.TXT');
      await expect(right.locator('.pc')).toHaveText(fm.t('FM_ENTRIES_OF', 49, 49, 49));
      await expect(right.getByRole('button', { name: fm.t('LOG_NEXT'), exact: true })).toBeDisabled();
      await right.getByRole('button', { name: fm.t('LOG_PREV'), exact: true }).click();
      await expect(fm.rows(1)).toHaveCount(48);
      const lists = (await fm.log()).filter((r) => r.url.startsWith('/api/list') && r.url.includes('USER/LOGS'));
      expect(lists.map((r) => r.url.replace(/.*&go=/, 'go=')).filter((u) => u !== 'go=first')).toEqual([
        'go=next&key=LOG047.CSV&key_dir=0',
        'go=prev&key=NOTES.TXT&key_dir=0',
      ]);
    });

    test('a report is viewed with the highlight classes the panel gives its lines', async ({ page }) => {
      const fm = await open(page, lang);
      await fm.view(1, REPORT);
      await expect(page.locator('.viewer .vp')).toHaveText(`USER/LOGS/${REPORT}`);
      await expect(fm.viewerText.locator('span').first()).toBeVisible();
      const want: string[][][] = await (await mock.get('/__mock/spans?how=pass')).json();
      const got = await fm.viewerLines();
      expect(got.length).toBe(want.length);
      // Line 0 is the title, line 1 the result: label, blanks, OK word.
      expect(got[0].map((s) => s[0])).toEqual(['hd']);
      expect(got[1].map((s) => s[0])).toEqual(['lb', 'tx', 'ok']);
      // The first limit line holds a current, in the current's class, and ends in its verdict.
      const lim = got.findIndex((l) => l.some((s) => s[0] === 'cu') && l.some((s) => s[0] === 'ok'));
      expect(lim).toBeGreaterThan(1);
      expect(got[lim].map((s) => s[0])).toEqual(['tx', 'cu', 'tx', 'cu', 'tx', 'ok']);
      // Every line equals text_hl_line()'s spans, class and text.
      expect(got).toEqual(want);
      await fm.head('FM_CLOSE').click();
      await expect(page.locator('.viewer')).toBeHidden();
    });

    test('an edited file is saved with If-Match and the card holds the new bytes', async ({ page }) => {
      const fm = await open(page, lang);
      await fm.view(0, 'HELLO.TXT');
      await expect(fm.viewerText).toContainText('Hello from the card.');
      await fm.head('ED_EDIT').click();
      const ta = page.locator('.viewer .ed textarea');
      await expect(ta).toBeVisible();
      await ta.fill('Hello from the browser.\nSecond line.\n');
      await fm.head('SET_SAVE').click();
      await expect(fm.note).toHaveText(fm.t('ED_SAVED', 'HELLO.TXT'));
      expect(await fm.read('HELLO.TXT')).toBe('Hello from the browser.\nSecond line.\n');
      const puts = await fm.puts();
      expect(puts).toHaveLength(1);
      expect(puts[0].status).toBe(200);
      expect(puts[0].if_match).toBe(`"${crc32('Hello from the card.\n').toString(16).padStart(8, '0')}"`);
    });

    test('a save over a file the panel changed is refused with 412 and asks before anything is lost', async ({ page }) => {
      const fm = await open(page, lang);
      await fm.view(0, 'HELLO.TXT');
      await fm.head('ED_EDIT').click();
      const ta = page.locator('.viewer .ed textarea');
      await ta.fill('my edit\n');
      const r = await mock.post('/__mock/put', { data: { path: 'HELLO.TXT', text: 'changed on the panel\n' } });
      expect(r.ok()).toBe(true);
      await fm.head('SET_SAVE').click();
      const ask = page.locator('.viewer .box');
      await expect(ask).toBeVisible();
      await expect(ask.locator('h3')).toHaveText(fm.t('SET_LEAVE_TITLE'));
      await expect(ask.locator('p').first()).toHaveText(fm.t('ED_Q_CHANGED', 'HELLO.TXT'));
      await expect(fm.note).toHaveText(fm.t('P_API_CHANGED'));
      const puts = await fm.puts();
      expect(puts.map((p) => p.status)).toEqual([412]);
      expect(puts[0].if_match).toBe(`"${crc32('Hello from the card.\n').toString(16).padStart(8, '0')}"`);
      expect(await fm.read('HELLO.TXT')).toBe('changed on the panel\n');
      // STAY keeps the edit.
      await ask.getByRole('button', { name: fm.t('SET_STAY'), exact: true }).click();
      await expect(ask).toBeHidden();
      await expect(ta).toHaveValue('my edit\n');
      // READ AGAIN drops it and shows the panel's file.
      await fm.head('SET_SAVE').click();
      await expect(ask).toBeVisible();
      await ask.getByRole('button', { name: fm.t('ED_READ_AGAIN'), exact: true }).click();
      await expect(fm.viewerText).toContainText('changed on the panel');
      await expect(ta).toBeHidden();
      expect(await fm.read('HELLO.TXT')).toBe('changed on the panel\n');
    });

    test('holding OVERWRITE for 2 seconds saves over the changed file with the ETag the refusal carried', async ({ page }) => {
      const fm = await open(page, lang);
      await fm.view(0, 'HELLO.TXT');
      await fm.head('ED_EDIT').click();
      await page.locator('.viewer .ed textarea').fill('my edit\n');
      await mock.post('/__mock/put', { data: { path: 'HELLO.TXT', text: 'changed on the panel\n' } });
      await fm.head('SET_SAVE').click();
      const over = page.locator('.viewer .box').getByRole('button', { name: fm.t('ED_OVERWRITE'), exact: true });
      await expect(over).toBeEnabled();
      await over.hover();
      await page.mouse.down();
      await page.waitForTimeout(2300);
      await page.mouse.up();
      await expect(fm.note).toHaveText(fm.t('ED_SAVED', 'HELLO.TXT'));
      expect(await fm.read('HELLO.TXT')).toBe('my edit\n');
      const puts = await fm.puts();
      expect(puts.map((p) => p.status)).toEqual([412, 200]);
      expect(puts[1].if_match).toBe(`"${crc32('changed on the panel\n').toString(16).padStart(8, '0')}"`);
    });

    test('a file is uploaded into the active folder where no entry has its name', async ({ page }) => {
      const fm = await open(page, lang);
      const [chooser] = await Promise.all([page.waitForEvent('filechooser'), fm.bar('P_FM_UPLOAD').click()]);
      await chooser.setFiles({ name: 'UPLOAD1.TXT', mimeType: 'text/plain', buffer: Buffer.from('uploaded\n') });
      await expect(fm.msg).toHaveText(fm.t('ED_SAVED', 'UPLOAD1.TXT'));
      await expect(fm.row(0, 'UPLOAD1.TXT')).toBeVisible();
      expect(await fm.read('UPLOAD1.TXT')).toBe('uploaded\n');
      const puts = await fm.puts();
      expect(puts).toHaveLength(1);
      expect(puts[0].status).toBe(201);
      expect(puts[0].if_none_match).toBe('*');
    });

    test('uploading a name that exists asks before it replaces and then sends If-Match', async ({ page }) => {
      const fm = await open(page, lang);
      const [chooser] = await Promise.all([page.waitForEvent('filechooser'), fm.bar('P_FM_UPLOAD').click()]);
      await chooser.setFiles({ name: 'HELLO.TXT', mimeType: 'text/plain', buffer: Buffer.from('replaced\n') });
      const box = page.locator('.wbox .card');
      await expect(box).toContainText(fm.t('P_FM_REPLACE_Q', 'HELLO.TXT'));
      expect(await fm.read('HELLO.TXT')).toBe('Hello from the card.\n');
      await fm.dialog('P_FM_REPLACE').click();
      await expect(fm.msg).toHaveText(fm.t('ED_SAVED', 'HELLO.TXT'));
      expect(await fm.read('HELLO.TXT')).toBe('replaced\n');
      const puts = await fm.puts();
      expect(puts.map((p) => p.status)).toEqual([409, 200]);
      expect(puts[0].if_none_match).toBe('*');
      expect(puts[1].if_match).toBe(`"${crc32('Hello from the card.\n').toString(16).padStart(8, '0')}"`);
    });

    test('a new folder is made in the active folder', async ({ page }) => {
      const fm = await open(page, lang);
      await fm.bar('FM_TK_FOLDER').click();
      await expect(page.locator('.wbox .card h3')).toHaveText(fm.t('FM_TK_FOLDER'));
      await page.locator('.wbox input.name').fill('TRIPS');
      await fm.dialog('FM_NEW').click();
      await expect(fm.msg).toHaveText(fm.t('FM_MADE', 'TRIPS'));
      await expect(fm.row(0, 'TRIPS')).toBeVisible();
      expect((await fm.tree()).dirs).toContain('TRIPS');
    });

    test('an entry is renamed in its folder', async ({ page }) => {
      const fm = await open(page, lang);
      await fm.select(0, 'HELLO.TXT');
      await fm.bar('FM_RENAME').click();
      await expect(page.locator('.wbox .card h3')).toHaveText(fm.t('FM_TK_RENAME'));
      const input = page.locator('.wbox input.name');
      await expect(input).toHaveValue('HELLO.TXT');
      await input.fill('HI.TXT');
      await fm.dialog('FM_RENAME').click();
      await expect(fm.msg).toHaveText(fm.t('FM_RENAMED', 'HELLO.TXT', 'HI.TXT'));
      await expect(fm.row(0, 'HI.TXT')).toBeVisible();
      await expect(fm.row(0, 'HELLO.TXT')).toHaveCount(0);
      expect(Object.keys((await fm.tree()).files)).toContain('HI.TXT');
    });

    test('an entry is moved into the folder of the other pane under the same name', async ({ page }) => {
      const fm = await open(page, lang);
      await fm.select(0, 'HELLO.TXT');
      await fm.bar('FM_MOVE').click();
      await expect(fm.msg).toHaveText(fm.t('FM_MOVED', 'HELLO.TXT'));
      await expect(fm.row(0, 'HELLO.TXT')).toHaveCount(0);
      await expect(fm.pane(1).locator('.pc')).toHaveText(fm.t('FM_ENTRIES_OF', 1, 48, 50));
      const files = Object.keys((await fm.tree()).files);
      expect(files).toContain('USER/LOGS/HELLO.TXT');
      expect(files).not.toContain('HELLO.TXT');
    });

    test('a file is deleted after the question and stays when the question is cancelled', async ({ page }) => {
      const fm = await open(page, lang);
      await fm.select(0, 'HELLO.TXT');
      await fm.bar('LOG_DELETE').click();
      await expect(page.locator('.wbox .card h3')).toHaveText(fm.t('LOG_DELETE_TITLE'));
      await page.locator('.wbox .card').getByRole('button').last().click();
      await expect(page.locator('.wbox')).toBeHidden();
      expect(Object.keys((await fm.tree()).files)).toContain('HELLO.TXT');
      await fm.bar('LOG_DELETE').click();
      await fm.dialog('LOG_DELETE').click();
      await expect(fm.msg).toHaveText(fm.t('LOG_DELETED', 'HELLO.TXT'));
      await expect(fm.row(0, 'HELLO.TXT')).toHaveCount(0);
      expect(Object.keys((await fm.tree()).files)).not.toContain('HELLO.TXT');
    });

    test('a folder that holds a file is not deleted and the box says why', async ({ page }) => {
      const fm = await open(page, lang);
      await fm.select(1, 'OLD');
      await fm.bar('LOG_DELETE').click();
      await fm.dialog('LOG_DELETE').click();
      await expect(page.locator('.wbox .warn')).toHaveText(fm.t('P_API_NOT_EMPTY'));
      expect((await fm.tree()).dirs).toContain('USER/LOGS/OLD');
    });

    test('DOWNLOAD names the selected file and the card sends its bytes', async ({ page }) => {
      const fm = await open(page, lang);
      await fm.select(0, 'HELLO.TXT');
      const link = page.locator('#fm .bar a.btn');
      await expect(link).toHaveText(fm.t('P_FM_DOWNLOAD'));
      await expect(link).toHaveAttribute('href', '/card/HELLO.TXT');
      const got = await page.request.get('/card/HELLO.TXT');
      expect(got.status()).toBe(200);
      expect(await got.text()).toBe('Hello from the card.\n');
    });

    test('INFO shows the size and the time of the selected file', async ({ page }) => {
      const fm = await open(page, lang);
      await fm.select(1, REPORT);
      await fm.bar('FM_INFO').click();
      const box = page.locator('#fm > .box:not(.wbox)');
      await expect(box).toBeVisible();
      const when = new Date(REPORT_STAMP * 1000).toISOString().slice(0, 16).replace('T', ' ');
      await expect(box).toContainText(when);
      await expect(box).toContainText(`${Buffer.byteLength(await fm.read('USER/LOGS/' + REPORT))}B`);
    });

    test('while the bench is ARMED every write button is dimmed and a tap names the reason', async ({ page }) => {
      const fm = await open(page, lang, { armed: true });
      await fm.select(0, 'HELLO.TXT');
      await expect(page.locator('#fm .wr .btn.dim')).toHaveCount(6);
      await expect(fm.bar('LOG_OPEN')).not.toHaveClass(/dim/);
      await fm.bar('FM_RENAME').click();
      await expect(fm.msg).toHaveText(fm.t('P_API_ARMED'));
      await expect(page.locator('.wbox')).toBeHidden();
    });

    test('with browser writes off every write button is dimmed and EDIT names the reason', async ({ page }) => {
      const fm = await open(page, lang, { writes: 'OFF' });
      await expect(page.locator('#fm .wr .btn.dim')).toHaveCount(6);
      await fm.bar('P_FM_UPLOAD').click();
      await expect(fm.msg).toHaveText(fm.t('P_API_WRITES_OFF'));
      await fm.view(0, 'HELLO.TXT');
      await expect(fm.viewerText).toContainText('Hello from the card.');
      await fm.head('ED_EDIT').click();
      await expect(fm.note).toHaveText(fm.t('P_API_WRITES_OFF'));
      await expect(page.locator('.viewer .ed')).toBeHidden();
    });

    test('a write waits for the hold on the panel and opens when the hold is made', async ({ page }) => {
      const fm = await open(page, lang, { writes: 'HOLD' });
      await expect(page.locator('#fm .wr .btn.dim')).toHaveCount(6);
      await fm.select(0, 'HELLO.TXT');
      await fm.bar('FM_RENAME').click();
      await expect(fm.msg).toHaveText(fm.t('P_API_WRITES_HOLD'));
      await mock.post('/__mock/reset', { data: { lang, writes: 'HOLD', window: true } });
      await fm.bar('LOG_RESCAN').click();
      await expect(page.locator('#fm .wr .btn.dim')).toHaveCount(0);
    });
  });
}
