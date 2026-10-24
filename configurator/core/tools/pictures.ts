// Checks the pictures of the parts against the files of the picture folder.
//
// A photo is two WebP files: `<file>.webp` and `<file>-thumb.webp`, the thumb
// 480px wide. A drawing is one SVG file: `<file>.svg`. The folder holds no
// file that no part names.

import { readFileSync, readdirSync } from 'node:fs';
import { join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

import type { Catalogue, OptionKey, Options } from '../../contract.ts';
import { OPTION_KEYS, OPTION_VALUES, PICTURES_MAX } from '../resolve.ts';

type Finding = { level: 'error' | 'warn'; id: string; msg: string };

/** The picture folder of the page. `read` returns null for a name the folder does not hold. */
export type PictureFolder = {
  names: ReadonlyArray<string>;
  read: (name: string) => Uint8Array | null;
};

/** Width of the thumb of a photo, in px. */
export const THUMB_WIDTH = 480;

/** The picture folder of the page, next to the core. */
export const PICTURE_DIR = fileURLToPath(new URL('../../public/pictures', import.meta.url));

const PICTURE_FILE = /^[a-z0-9]+(-[a-z0-9]+)*$/;
const SVG_NAMESPACE = 'http://www.w3.org/2000/svg';

function err(id: string, msg: string): Finding {
  return { level: 'error', id, msg };
}

/** Pixel size of a WebP file and whether it carries metadata; null when the bytes are no WebP file. */
export function webpInfo(b: Uint8Array): { width: number; height: number; metadata: boolean } | null {
  if (b.length < 30) return null;
  const at = (o: number): number => b[o] ?? 0;
  const tag = (o: number): string => String.fromCharCode(at(o), at(o + 1), at(o + 2), at(o + 3));
  if (tag(0) !== 'RIFF' || tag(8) !== 'WEBP') return null;
  const chunk = tag(12);
  if (chunk === 'VP8 ') {
    if (at(23) !== 0x9d || at(24) !== 0x01 || at(25) !== 0x2a) return null;
    return {
      width: (at(26) | (at(27) << 8)) & 0x3fff,
      height: (at(28) | (at(29) << 8)) & 0x3fff,
      metadata: false,
    };
  }
  if (chunk === 'VP8L') {
    if (at(20) !== 0x2f) return null;
    const bits = (at(21) | (at(22) << 8) | (at(23) << 16) | (at(24) << 24)) >>> 0;
    return { width: (bits & 0x3fff) + 1, height: ((bits >>> 14) & 0x3fff) + 1, metadata: false };
  }
  if (chunk === 'VP8X') {
    return {
      width: 1 + (at(24) | (at(25) << 8) | (at(26) << 16)),
      height: 1 + (at(27) | (at(28) << 8) | (at(29) << 16)),
      // Bit 3 of the flags: Exif chunk. Bit 2: XMP chunk.
      metadata: (at(20) & 0x0c) !== 0,
    };
  }
  return null;
}

/** What a drawing must not hold, and the pixel size its root element states. */
export function svgInfo(text: string): { problems: string[]; width: number | null; height: number | null } {
  const problems: string[] = [];
  const root = /<svg\b[^>]*>/.exec(text)?.[0] ?? null;
  if (root === null || !/^\s*(<\?xml[^>]*\?>\s*)?<svg\b/.test(text)) problems.push('is no SVG file');
  if (/<script\b/i.test(text)) problems.push('holds a script');
  if (/\son[a-z]+\s*=/i.test(text)) problems.push('holds an event attribute');
  if (/<(image|foreignObject|iframe|object|embed|style|link|a)\b/i.test(text)) {
    problems.push('holds an element that loads or embeds content');
  }
  if (/data:/i.test(text)) problems.push('holds a data URL');
  if (/@import|<!ENTITY|<!DOCTYPE|<\?xml-stylesheet/i.test(text)) problems.push('holds an import, an entity or a document type');
  for (const m of text.matchAll(/\bhref\s*=\s*(["'])(.*?)\1/gi)) {
    if (!(m[2] ?? '').startsWith('#')) problems.push(`refers to "${m[2] ?? ''}"`);
  }
  for (const m of text.matchAll(/url\(\s*["']?([^)"']*)/gi)) {
    if (!(m[1] ?? '').startsWith('#')) problems.push(`refers to "${m[1] ?? ''}"`);
  }
  for (const m of text.matchAll(/(?:[a-z][a-z0-9+.-]*:)?\/\/[^\s"'<>)]+/gi)) {
    if (m[0] !== SVG_NAMESPACE) problems.push(`names the address "${m[0]}"`);
  }
  const size = (name: string): number | null => {
    const m = root === null ? null : new RegExp(`\\s${name}="(\\d+)"`).exec(root);
    return m ? Number(m[1]) : null;
  };
  return { problems, width: size('width'), height: size('height') };
}

function applies(only: Partial<Options> | undefined, options: Options): boolean {
  return Object.entries(only ?? {}).every(([key, value]) => options[key as OptionKey] === value);
}

function optionSets(): Options[] {
  let sets: Array<Record<string, string>> = [{}];
  for (const key of OPTION_KEYS) {
    sets = sets.flatMap((set) => OPTION_VALUES[key].map((value) => ({ ...set, [key]: value })));
  }
  return sets as unknown as Options[];
}

export function checkPictures(c: Catalogue, folder: PictureFolder): Finding[] {
  const out: Finding[] = [];
  const named = new Set<string>();
  const decoder = new TextDecoder('utf-8', { fatal: true });

  for (const part of c.parts) {
    const pictures = part.pictures ?? [];
    const id = `part.${part.id}`;
    for (const options of optionSets()) {
      const shown = pictures.filter((p) => applies(p.only, options)).length;
      if (shown > PICTURES_MAX) {
        out.push(err(id, `${shown} pictures apply to one set of options; the limit is ${PICTURES_MAX}`));
        break;
      }
    }
    for (const picture of pictures) {
      const pid = `${id}.picture.${String(picture.file)}`;
      if (picture.kind !== 'photo' && picture.kind !== 'drawing') {
        out.push(err(pid, `kind ${String(picture.kind)} is not photo or drawing`));
        continue;
      }
      if (typeof picture.file !== 'string' || !PICTURE_FILE.test(picture.file)) {
        out.push(err(pid, 'the file name is not lowercase letters, digits and hyphens without an extension'));
        continue;
      }
      for (const lang of ['en', 'de'] as const) {
        const alt: unknown = picture.alt?.[lang];
        if (typeof alt !== 'string' || alt.trim() === '') out.push(err(pid, `no alt text in ${lang}`));
      }
      if (!Number.isInteger(picture.width) || !Number.isInteger(picture.height)
        || picture.width <= 0 || picture.height <= 0) {
        out.push(err(pid, 'width and height are not positive whole numbers'));
      }

      if (picture.kind === 'drawing') {
        const name = `${picture.file}.svg`;
        named.add(name);
        const bytes = folder.read(name);
        if (bytes === null) {
          out.push(err(pid, `the file ${name} is missing`));
          continue;
        }
        let text: string;
        try {
          text = decoder.decode(bytes);
        } catch {
          out.push(err(pid, `${name} is not UTF-8 text`));
          continue;
        }
        const svg = svgInfo(text);
        for (const problem of svg.problems) out.push(err(pid, `${name} ${problem}`));
        if (svg.width !== picture.width || svg.height !== picture.height) {
          out.push(err(pid, `${name} is ${svg.width ?? '?'} x ${svg.height ?? '?'} px; the record says ${picture.width} x ${picture.height}`));
        }
        continue;
      }

      for (const thumb of [false, true]) {
        const name = `${picture.file}${thumb ? '-thumb' : ''}.webp`;
        named.add(name);
        const bytes = folder.read(name);
        if (bytes === null) {
          out.push(err(pid, `the file ${name} is missing`));
          continue;
        }
        const info = webpInfo(bytes);
        if (info === null) {
          out.push(err(pid, `${name} is not a WebP file`));
          continue;
        }
        if (info.metadata) out.push(err(pid, `${name} carries Exif or XMP metadata`));
        if (!thumb) continue;
        if (info.width !== THUMB_WIDTH) {
          out.push(err(pid, `${name} is ${info.width} px wide; a thumb is ${THUMB_WIDTH} px wide`));
        }
        if (info.width !== picture.width || info.height !== picture.height) {
          out.push(err(pid, `${name} is ${info.width} x ${info.height} px; the record says ${picture.width} x ${picture.height}`));
        }
      }
    }
  }

  for (const name of folder.names) {
    if (!named.has(name)) out.push(err(`picture.${name}`, 'no part names this file of the picture folder'));
  }
  return out;
}

/** The picture folder on disk. A folder that does not exist holds no file. */
export function pictureFolder(dir: string): PictureFolder {
  let names: string[] = [];
  try {
    names = readdirSync(dir, { recursive: true, withFileTypes: true })
      .filter((entry) => !entry.isDirectory())
      .map((entry) => relative(dir, join(entry.parentPath, entry.name)).replaceAll('\\', '/'))
      .sort();
  } catch {
    names = [];
  }
  return {
    names,
    read: (name) => {
      if (!names.includes(name)) return null;
      try {
        return readFileSync(join(dir, name));
      } catch {
        return null;
      }
    },
  };
}
