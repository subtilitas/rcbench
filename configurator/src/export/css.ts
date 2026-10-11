// Reads the style sheet of the page for the PDF export: the colour tokens of
// the print set and the rules that style SVG elements.
//
// Supported selectors: compounds of a tag name and classes, joined by the
// descendant combinator. A rule with another selector form is skipped. Rules
// inside an at-rule are skipped, except the `:root` tokens of `@media print`.

export type Decls = Record<string, string>;
type Compound = { tag: string | null; classes: string[] };
type Rule = { chain: Compound[]; decls: Decls; weight: number };
/** `skipped` lists the selectors outside an at-rule that the reader does not support. */
export type Css = { tokens: Record<string, string>; rules: Rule[]; skipped: string[] };

/** An element as selectors see it. */
export type Node = { tag: string; classes: string[] };

type Block = { head: string; body: string };

/** Splits a sheet into its top-level blocks. */
function blocks(text: string): Block[] {
  const out: Block[] = [];
  let depth = 0;
  let start = 0;
  let head = '';
  let open = 0;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (c === '{') {
      if (depth === 0) {
        head = text.slice(start, i).trim();
        open = i + 1;
      }
      depth++;
    } else if (c === '}') {
      depth--;
      if (depth === 0) {
        out.push({ head, body: text.slice(open, i) });
        start = i + 1;
      }
    } else if (c === ';' && depth === 0) start = i + 1;
  }
  return out;
}

function decls(body: string): Decls {
  const out: Decls = {};
  for (const part of body.split(';')) {
    const colon = part.indexOf(':');
    if (colon < 0) continue;
    out[part.slice(0, colon).trim().toLowerCase()] = part.slice(colon + 1).trim();
  }
  return out;
}

function compound(text: string): Compound | null {
  const m = /^([a-z][a-z0-9]*)?((?:\.[A-Za-z_][\w-]*)*)$/.exec(text);
  if (!m || text === '') return null;
  return { tag: m[1] ?? null, classes: (m[2] ?? '').split('.').filter((c) => c !== '') };
}

export function parseCss(source: string): Css {
  const text = source.replace(/\/\*[\s\S]*?\*\//g, '');
  const tokens: Record<string, string> = {};
  const rules: Rule[] = [];
  const skipped: string[] = [];
  const takeTokens = (body: string): void => {
    for (const [name, value] of Object.entries(decls(body))) if (name.startsWith('--')) tokens[name] = value;
  };
  const top = blocks(text);
  for (const block of top) if (block.head === ':root') takeTokens(block.body);
  for (const block of top) {
    if (!/^@media\s+print$/.test(block.head)) continue;
    for (const inner of blocks(block.body)) if (inner.head === ':root') takeTokens(inner.body);
  }
  let order = 0;
  for (const block of top) {
    if (block.head.startsWith('@') || block.head === ':root') continue;
    const d = decls(block.body);
    for (const selector of block.head.split(',')) {
      const chain = selector.trim().split(/\s+/).map(compound);
      order++;
      if (chain.some((c) => c === null)) {
        skipped.push(selector.trim());
        continue;
      }
      const done = chain as Compound[];
      const classes = done.reduce((sum, c) => sum + c.classes.length, 0);
      const tags = done.filter((c) => c.tag !== null).length;
      rules.push({ chain: done, decls: d, weight: classes * 1e8 + tags * 1e6 + order });
    }
  }
  rules.sort((a, b) => a.weight - b.weight);
  return { tokens, rules, skipped };
}

const fits = (c: Compound, node: Node): boolean =>
  (c.tag === null || c.tag === node.tag) && c.classes.every((name) => node.classes.includes(name));

/** True when the chain matches the last node of `path`, which runs from the root to the element. */
function matches(chain: Compound[], path: Node[]): boolean {
  let at = path.length - 1;
  if (!fits(chain[chain.length - 1] as Compound, path[at] as Node)) return false;
  for (let i = chain.length - 2; i >= 0; i--) {
    at--;
    while (at >= 0 && !fits(chain[i] as Compound, path[at] as Node)) at--;
    if (at < 0) return false;
  }
  return true;
}

/** Replaces `var(--name)` by the token. An unknown token is an error. */
function resolve(value: string, tokens: Record<string, string>): string {
  return value.replace(/var\(\s*(--[\w-]+)\s*\)/g, (_, name: string) => {
    const token = tokens[name];
    if (token === undefined) throw new Error(`The style sheet has no token ${name}.`);
    return token;
  });
}

/** The declarations that apply to the last node of `path`, later and more specific rules last. */
export function styleOf(css: Css, path: Node[]): Decls {
  let known = memo.get(css);
  if (!known) {
    known = new Map();
    memo.set(css, known);
  }
  const key = path.map((n) => `${n.tag}.${n.classes.join('.')}`).join(' ');
  let out = known.get(key);
  if (!out) {
    out = compute(css, path);
    known.set(key, out);
  }
  return out;
}

/** Results of `styleOf` per style sheet and path. A caller does not change a result. */
const memo = new WeakMap<Css, Map<string, Decls>>();

function compute(css: Css, path: Node[]): Decls {
  const out: Decls = {};
  for (const rule of css.rules) {
    if (!matches(rule.chain, path)) continue;
    for (const [name, value] of Object.entries(rule.decls)) {
      const plain = resolve(value.replace(/\s*!important$/, ''), css.tokens);
      if (name === 'font') {
        // The shorthand sets the size and resets the weight.
        const size = /(\d+(?:\.\d+)?)px/.exec(plain);
        if (size) out['font-size'] = `${size[1]}px`;
        const weight = /^(?:\s*(?:normal|italic)\s+)*(bold|[1-9]00)\b/.exec(plain);
        out['font-weight'] = weight ? (weight[1] as string) : '400';
      } else out[name] = plain;
    }
  }
  return out;
}

/** A colour of the sheet as red, green and blue from 0 to 1, or null for `none`. */
export function colour(value: string): [number, number, number] | null {
  const v = value.trim().toLowerCase();
  if (v === 'none' || v === 'transparent') return null;
  const named: Record<string, string> = { black: '#000000', white: '#ffffff' };
  const hex = named[v] ?? v;
  const m = /^#([0-9a-f]{3}|[0-9a-f]{6})$/.exec(hex);
  if (!m) throw new Error(`The export reads no colour from "${value}".`);
  const digits = m[1] as string;
  const full = digits.length === 3 ? [...digits].map((c) => c + c).join('') : digits;
  return [0, 2, 4].map((i) => parseInt(full.slice(i, i + 2), 16) / 255) as [number, number, number];
}
