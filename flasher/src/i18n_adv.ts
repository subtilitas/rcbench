// Strings of Advanced mode in English and German. They load with the
// Advanced code (advanced.ts) on the first switch to Advanced, not with the
// page.

import type { Lang } from './i18n.ts';

const A = {
  srcRelease: ['Release', 'Release'],
  srcFile: ['Local file', 'Lokale Datei'],
  srcLoading: ['The list of releases loads.', 'Die Liste der Releases lädt.'],
  fileHint: [
    'A merged image for 0x0 (.bin) for an ESP32 board; a UF2 file or a .bin for the flash start for an RP2350.',
    'Ein zusammengeführtes Image für 0x0 (.bin) für ein ESP32-Board; eine UF2-Datei oder eine .bin für den Flash-Anfang für einen RP2350.',
  ],
  sigFile: ['Signature file of the image (.sig, optional)', 'Signaturdatei des Images (.sig, optional)'],
  imgLocal: ['local file, no manifest to compare', 'lokale Datei, kein Manifest zum Vergleich'],
  binWarn: [
    'A .bin goes to 0x10000000, the start of the flash, over the partition table of an IOmcu. A UF2 file keeps the layout.',
    'Eine .bin landet bei 0x10000000, am Anfang des Flashs, über der Partitionstabelle einer IOmcu. Eine UF2-Datei behält die Aufteilung.',
  ],
  sigLocal: [
    'Signature: local file, signature not checked. Choose its .sig file as well to check it. WRITE stays possible.',
    'Signatur: lokale Datei, Signatur nicht geprüft. Wähle auch ihre .sig-Datei, um sie zu prüfen. SCHREIBEN bleibt möglich.',
  ],
  sigChecking: [
    'Signature: checking {file}. WRITE waits for the result.',
    'Signatur: {file} wird geprüft. SCHREIBEN wartet auf das Ergebnis.',
  ],
  sigMismatch: [
    'Signature: the signature does not match this image ({file}). The rcbench release key did not sign it. Advanced mode allows WRITE: write it only when you know where the image comes from.',
    'Signatur: Die Signatur passt nicht zu diesem Image ({file}). Der rcbench-Release-Schlüssel hat es nicht signiert. Der erweiterte Modus erlaubt SCHREIBEN: Schreibe es nur, wenn du weißt, woher das Image kommt.',
  ],
  keep: ['Keep settings', 'Einstellungen behalten'],
  keepSub: [
    'the NVS (non-volatile storage) partition with the settings and the bindings is not written',
    'die NVS-Partition (non-volatile storage) mit den Einstellungen und den Kopplungen wird nicht geschrieben',
  ],
  keepOff: [
    'The whole image is written from 0x0, over the NVS partition: the settings and the bindings on the board are erased.',
    'Das ganze Image wird ab 0x0 geschrieben, über die NVS-Partition: Die Einstellungen und die Kopplungen auf dem Board werden gelöscht.',
  ],
  keepRefused: [
    'With "Keep settings" on, nothing is written. Untick it to write the whole image; that erases the settings.',
    'Mit „Einstellungen behalten“ wird nichts geschrieben. Nimm den Haken weg, um das ganze Image zu schreiben; das löscht die Einstellungen.',
  ],
  planned: ['not in a release yet: local file only', 'noch in keinem Release: nur lokale Datei'],
  noReleaseFile: ['no release carries it: local file', 'kein Release enthält es: lokale Datei'],
} as const satisfies Record<string, readonly [string, string]>;

export type AdvKey = keyof typeof A;

/** The Advanced string of a key in a language, with each {name} replaced. */
export function tAdv(lang: Lang, key: AdvKey, vars: Record<string, string | number> = {}): string {
  const s: string = A[key][lang === 'de' ? 1 : 0];
  return s.replace(/\{(\w+)\}/g, (m, name: string) => (name in vars ? String(vars[name]) : m));
}

/** Every Advanced key with both strings, for the tests. */
export const ADV_KEYS = Object.keys(A) as AdvKey[];
export const advStrings = (key: AdvKey): readonly [string, string] => A[key];
