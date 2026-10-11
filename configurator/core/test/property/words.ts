// The words the writing rules of the project exclude from every text of the
// page, English and German.

export const BANNED: ReadonlyArray<string> = [
  'powerful', 'seamless', 'seamlessly', 'robust', 'blazing', 'comprehensive', 'simply', 'just', 'easy', 'easily',
  'mächtig', 'nahtlos', 'einfach',
];

const PATTERNS = BANNED.map((word) => [word, new RegExp(`(?<![\\p{L}\\p{N}])${word}(?![\\p{L}\\p{N}])`, 'iu')] as const);

/** The banned words in `text`, as whole words, in any letter case. */
export function bannedIn(text: string): string[] {
  return PATTERNS.filter(([, pattern]) => pattern.test(text)).map(([word]) => word);
}
