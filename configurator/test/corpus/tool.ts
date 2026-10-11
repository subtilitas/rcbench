// Maintains the hash corpus.
//
//   node test/corpus/tool.ts add <name> <why> <hash>   append one entry
//   node test/corpus/tool.ts wires                      rewrite wires.json from the catalogue
//
// `add` refuses a name or a hash the corpus holds. It never changes an entry.

import { writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import {
  CORPUS_FILE, WIRES_FILE, expectationOf, hashOf, outcome, readCorpus,
} from './corpus.ts';
import type { Built, Corpus, Entry, Wires } from './corpus.ts';

/** One entry per line, so a change to the file shows as added lines. */
export function corpusText(corpus: Corpus): string {
  const lines = corpus.entries.map((e) => `    ${JSON.stringify(e)}`);
  return `{\n  "ids": ${JSON.stringify(corpus.ids)},\n  "entries": [\n${lines.join(',\n')}\n  ]\n}\n`;
}

export function wiresText(wires: Wires): string {
  const lines = Object.entries(wires).map(([name, list]) => `  ${JSON.stringify(name)}: ${JSON.stringify(list)}`);
  return `{\n${lines.join(',\n')}\n}\n`;
}

export function wiresOf(corpus: Corpus): Wires {
  return Object.fromEntries(corpus.entries.map((e) => [e.name, outcome(hashOf(e)).wires]));
}

/** `corpus` with one more entry. Throws when the name or the hash is taken. */
export function added(corpus: Corpus, name: string, why: string, hash: string | Built): Corpus {
  if (!/^[a-z0-9]+(-[a-z0-9]+)*$/.test(name)) throw new Error(`"${name}" is no entry name: lower case, digits and hyphens`);
  if (why.trim() === '') throw new Error('an entry states what it is in the corpus for');
  if (corpus.entries.some((e) => e.name === name)) throw new Error(`the corpus holds an entry named ${name}`);
  const text = hashOf({ hash });
  const twin = corpus.entries.find((e) => hashOf(e) === text);
  if (twin) throw new Error(`the corpus holds this hash as ${twin.name}`);
  const entry: Entry = { name, hash, why, expect: expectationOf(text) };
  return { ids: corpus.ids, entries: [...corpus.entries, entry] };
}

function main(argv: string[]): number {
  const [command, ...rest] = argv;
  if (command === 'add' && rest.length === 3) {
    const [name, why, hash] = rest as [string, string, string];
    const corpus = added(readCorpus(), name, why, hash);
    writeFileSync(CORPUS_FILE, corpusText(corpus));
    writeFileSync(WIRES_FILE, wiresText(wiresOf(corpus)));
    console.log(`added ${name}: ${corpus.entries.length} entries`);
    return 0;
  }
  if (command === 'wires' && rest.length === 0) {
    const corpus = readCorpus();
    writeFileSync(WIRES_FILE, wiresText(wiresOf(corpus)));
    console.log(`wires.json: ${corpus.entries.length} entries`);
    return 0;
  }
  console.error('usage: tool.ts add <name> <why> <hash> | tool.ts wires');
  return 2;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  process.exitCode = main(process.argv.slice(2));
}

