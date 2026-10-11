// Runs axe-core on a page at the tags of the Web Content Accessibility
// Guidelines (WCAG) 2.2, level AA.

import { AxeBuilder } from '@axe-core/playwright';
import type { Page } from '@playwright/test';

export const TAGS = ['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa', 'wcag22aa'];

export type Axe = {
  /** One line per violated rule: its id, its help text and the elements. An empty list: no violation. */
  violations: string[];
  /** The count of rules that ran and passed. */
  passes: number;
};

/** The report of axe-core on the page as it is. */
export async function axeOf(page: Page): Promise<Axe> {
  const report = await new AxeBuilder({ page }).withTags(TAGS).analyze();
  return {
    violations: report.violations.map((v) => `${v.id}: ${v.help} (${v.nodes.map((n) => n.target.join(' ')).join('; ')})`),
    passes: report.passes.length,
  };
}
