// The parts list of a setup: the rows to buy and the rows the visitor brings,
// as data, as text and as CSV (comma-separated values). The page and the PDF
// each set it in their own markup.
//
// The search link is built from the data when the list is made; the data
// stores no search address. Nothing of the search engine is loaded: the page
// renders a plain link.

import type { Buy, Lang, Part } from '../../contract.ts';
import { brought, buyRows } from '../core.ts';
import { t, tx } from '../i18n.ts';
import type { View } from './chooser.ts';

export type BuyLine = {
  part: Part;
  qty: number;
  /** Null: a generic part or a modelled part; `order` is its spec. */
  maker: string | null;
  order: string;
  check: string;
  /** The product page of the maker, or null. */
  page: string | null;
  /** The web search for the row, or null for a row the visitor brings. */
  search: string | null;
};

export type PartsList = { buy: BuyLine[]; bring: BuyLine[] };

/** The address of the web search. The query follows it, percent-encoded. */
export const SEARCH = 'https://www.google.com/search?q=';

/** A search term in double quotes. A double quote inside the term leaves it. */
const quoted = (term: string): string => `"${term.replace(/"/g, '').trim()}"`;

/**
 * The search query of a row: maker and ordering name each in quotes. Without a
 * maker the spec stands alone, each of its terms between ", " in quotes.
 */
export function searchQuery(maker: string | null, order: string): string {
  const terms = maker === null ? order.split(', ') : [maker, order];
  return terms.map(quoted).filter((term) => term !== '""').join(' ');
}

/**
 * The search address of a row. Beyond `encodeURIComponent`, the characters
 * ! ' ( ) * are percent-encoded too: the address then holds no parenthesis,
 * which a PDF string would have to escape.
 */
export function searchUrl(maker: string | null, order: string): string {
  const query = encodeURIComponent(searchQuery(maker, order)).replace(/[!'()*]/g, (c) => `%${c.charCodeAt(0).toString(16).toUpperCase()}`);
  return SEARCH + query;
}

function line(lang: Lang, part: Part, row: Buy, bring: boolean): BuyLine {
  const order = tx(lang, row.order);
  return {
    part,
    qty: row.qty ?? 1,
    maker: row.maker,
    order,
    check: tx(lang, row.check),
    page: row.page ?? null,
    search: bring ? null : searchUrl(row.maker, order),
  };
}

/**
 * The parts list of a setup, in catalogue order. A part ticked under "Parts
 * you own" is left out. A part the visitor brings stands in `bring`.
 */
export function partsList(view: View): PartsList {
  const out: PartsList = { buy: [], bring: [] };
  for (const r of view.setup.parts) {
    if (r.ownedTick) continue;
    const bring = brought(r.part);
    for (const row of buyRows(r.part, view.options)) (bring ? out.bring : out.buy).push(line(view.lang, r.part, row, bring));
  }
  return out;
}

/** "Waveshare ESP32-S3-Touch-LCD-7", or the spec alone. */
export function lineName(l: BuyLine): string {
  return l.maker === null ? l.order : `${l.maker} ${l.order}`;
}

/** "2 ×". */
export const qtyText = (l: BuyLine): string => `${l.qty} ×`;

/** The disclaimer above the list: the release of the catalogue, no warranty. */
export function buyNote(view: View): string {
  return t(view.lang, 'buyNote', { r: view.catalogue.release });
}

/** The list as plain text: heading, setup link, disclaimer, then each row. Lines end in LF. */
export function listText(view: View, url: string): string {
  const { lang } = view;
  const list = partsList(view);
  const out: string[] = [t(lang, 'buyTextHead', { r: view.catalogue.release }), url, '', buyNote(view), '', t(lang, 'buy')];
  if (list.buy.length === 0) out.push(t(lang, 'buyNone'));
  for (const l of list.buy) {
    out.push(`${qtyText(l)} ${lineName(l)}`, `  ${t(lang, 'buyCheck')}${l.check}`);
    if (l.page !== null) out.push(`  ${t(lang, 'buyPage')}: ${l.page}`);
    if (l.search !== null) out.push(`  ${t(lang, 'buySearch')}: ${l.search}`);
  }
  if (list.bring.length > 0) {
    out.push('', t(lang, 'bring'));
    for (const l of list.bring) out.push(`${qtyText(l)} ${l.order}`, `  ${t(lang, 'buyCheck')}${l.check}`);
  }
  return `${out.join('\n')}\n`;
}

/** One CSV field, in double quotes, a double quote doubled (RFC 4180). */
export function csvField(value: string): string {
  return `"${value.replace(/"/g, '""')}"`;
}

/**
 * The rows to buy as CSV: a header row, then 1 record per row. Columns:
 * quantity, maker, ordering name, check, maker page, search link. Every field
 * is quoted; records end in CR LF (RFC 4180). The rows the visitor brings are
 * not in it.
 */
export function listCsv(view: View): string {
  const { lang } = view;
  const head = (['csvQty', 'csvMaker', 'csvOrder', 'csvCheck', 'csvPage', 'csvSearch'] as const).map((k) => t(lang, k));
  const records = [head, ...partsList(view).buy.map((l) => [String(l.qty), l.maker ?? '', l.order, l.check, l.page ?? '', l.search ?? ''])];
  return records.map((r) => r.map(csvField).join(',')).join('\r\n') + '\r\n';
}
