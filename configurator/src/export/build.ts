// The export pipeline: setup to document model to page layout to PDF bytes.
// The style sheet and the 2 font files are parameters, so the pipeline runs in
// the browser and under Node.js. The output depends on the setup, the
// language, the link, the date and the deflate implementation of the platform.

import { t } from '../i18n.ts';
import type { View } from '../view/chooser.ts';
import { parseCss } from './css.ts';
import type { Diagram } from './diagram.ts';
import { parseFont, use } from './font.ts';
import { layoutDoc, PAGE } from './layout.ts';
import type { Laid } from './layout.ts';
import { documentModel } from './model.ts';
import type { Doc } from './model.ts';
import { writePdf } from './pdf.ts';

export type ExportInput = {
  view: View;
  /** Share link of the setup. */
  url: string;
  /** Date of the export. The document shows its local calendar date. */
  now: Date;
  /** Text of `src/style.css`. */
  css: string;
  fonts: { regular: Uint8Array; bold: Uint8Array };
  diagram?: Diagram;
  /** False stores the streams without deflate. */
  compress?: boolean;
};

export type Exported = { bytes: Uint8Array; fileName: string; doc: Doc; laid: Laid };

export async function buildPdf(input: ExportInput): Promise<Exported> {
  const doc = documentModel(input.view, input.url, input.now, input.diagram);
  const fonts = { regular: use(parseFont(input.fonts.regular)), bold: use(parseFont(input.fonts.bold)) };
  const laid = layoutDoc(doc, parseCss(input.css), fonts);
  const bytes = await writePdf(
    laid.pages, fonts,
    { title: doc.title, lang: doc.lang, creator: t(doc.lang, 'title'), date: input.now },
    PAGE, input.compress !== false,
  );
  return { bytes, fileName: doc.fileName, doc, laid };
}
