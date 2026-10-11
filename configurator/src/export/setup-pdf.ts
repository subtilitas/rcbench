// Browser entry of the PDF export. The page loads this module and everything
// it imports as one separate chunk on the first use of the PDF button. The
// chunk holds the style sheet text and both font files, so the export makes no
// further request.

import css from '../style.css?raw';
import type { View } from '../view/chooser.ts';
import { buildPdf } from './build.ts';
import bold from './font/Inter-Bold-subset.ttf?inline';
import regular from './font/Inter-Regular-subset.ttf?inline';

/** The bytes of a base64 `data:` URL. */
function bytesOf(dataUrl: string): Uint8Array {
  const text = atob(dataUrl.slice(dataUrl.indexOf(',') + 1));
  const out = new Uint8Array(text.length);
  for (let i = 0; i < text.length; i++) out[i] = text.charCodeAt(i);
  return out;
}

export type SetupPdf = { blob: Blob; fileName: string };

export async function setupPdf(view: View, url: string, now: Date): Promise<SetupPdf> {
  const made = await buildPdf({ view, url, now, css, fonts: { regular: bytesOf(regular), bold: bytesOf(bold) } });
  return { blob: new Blob([made.bytes as Uint8Array<ArrayBuffer>], { type: 'application/pdf' }), fileName: made.fileName };
}
