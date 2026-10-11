// Contract between the core (data, resolver, checks) and the UI (page, drawing, workflow).
// Types only. Changing this file is a change to the specification.

export type Lang = 'en' | 'de';
export type Text = { en: string; de: string };

export type Src =
  | { kind: 'repo'; path: string; line: number; tag: 'v0.16.0' }
  | { kind: 'guide'; section: string }
  | { kind: 'photo'; file: string }
  | { kind: 'page' };

export type Release = 'released' | 'main' | 'planned';
export type Hardware = 'run' | 'not-run';

export type PartId = string;
export type FunctionId = string;
export type PinId = string;

export type Options = {
  connection: 'pads' | 'osoyoo';
  ina228Board: 'matek' | 'xt90';
  escPower: 'battery' | 'pd';
};
export type OptionKey = keyof Options;

export type Pin = {
  id: PinId;
  label: Text;
  confirmed: boolean;
  only?: Partial<Options>;
  src: Src;
};

export type Part = {
  id: PartId;
  name: Text;
  short: Text;
  role: Text | null;
  locked: boolean;
  selectable: boolean;
  release: Release;
  hardware: Hardware;
  slot: { column: 'L' | 'R' | 'none'; order: number; dashed?: boolean; sub?: Text };
  pins: Pin[];
  variantName?: { option: OptionKey; names: Record<string, Text> };
  photo?: { file: string; alt: Text };
  src: Src;
};

export type Pad = {
  n: number;
  name: string;
  gpio: number | null;
  breakoutLabel: string;
  reserved?: Text;
  src: Src;
};

export type SignalClass =
  | 'canh' | 'canl' | 'gnd' | 'hb' | 'out' | 'uart' | 'sda' | 'scl' | 'logic' | 'power' | 'phase' | 'nc';

/** "panel.CANH", "pad.21", "can.H", "can.L" */
export type EndRef = string;

export type Cond =
  | { has: PartId }
  | { opt: OptionKey; is: string }
  | { not: Cond }
  | { all: Cond[] }
  | { any: Cond[] };

export type Route =
  | { kind: 'L' | 'R' | 'O'; lane: number }
  | { kind: 'X'; left: number; right: number; channel: number }
  | { kind: 'Z' }
  | { kind: 'S' };

export type PinChoice = 'fixed' | 'default' | 'example' | 'proposal';

export type Wire = {
  n: number;
  kind: 'wire' | 'component' | 'stub';
  a: EndRef;
  b: EndRef | null;
  to?: Text;
  net: string;
  netB?: string;
  cls: SignalClass;
  signal: Text;
  note: Text | null;
  when: Cond | null;
  stage: 1 | 2 | 3 | 4 | 5;
  route: Route;
  pin?: PinChoice;
  src: Src;
};

export type Fn = {
  id: FunctionId;
  group: 'servo' | 'esc' | 'bench';
  name: Text;
  needs: PartId[];
  forces?: Partial<Options>;
  hardware: Hardware;
  src: Src;
};

export type Rule = { id: string; when: Cond; adds: PartId; because: Text; src: Src };
export type Check = { id: string; kind: 'measure' | 'unknown' | 'note'; text: Text; when: Cond | null; src: Src };
export type Stage = { n: 1 | 2 | 3 | 4 | 5; title: Text; src: Src };

export type PinDefault = {
  signal: 'heartbeat' | 'sda' | 'scl' | 'tap' | 'esc' | 'servo' | 'pd_tx' | 'pd_rx';
  gpio: number;
  pad: number;
  choice: PinChoice;
  role: 'input' | 'output' | 'bus' | 'uart';
  allowed: number[] | 'catalogue';
  src: Src;
};

export type Catalogue = {
  release: string;
  hashVersion: 1;
  parts: Part[];
  pads: Pad[];
  wires: Wire[];
  functions: Fn[];
  rules: Rule[];
  checks: Check[];
  stages: Stage[];
  safety: Array<{ text: Text; src: Src }>;
  notInRelease: Array<{ id: string; text: Text; release: 'main' | 'planned'; src: Src }>;
  baseFunction: Text;
  pinDefaults: PinDefault[];
};

export type Selection = {
  functions: FunctionId[];
  owned: PartId[];
  options: Options;
};

export type Reason =
  | { kind: 'always' }
  | { kind: 'function'; id: FunctionId }
  | { kind: 'owned' }
  | { kind: 'rule'; id: string; because: Text }
  | { kind: 'option'; key: OptionKey; value: string };

export type ResolvedPart = { part: Part; addedBy: Reason[]; derived: boolean; ownedTick: boolean };
export type ResolvedFunction = { fn: Fn; chosen: boolean; enabled: boolean; missing: PartId[] };
export type ResolvedOption = { key: OptionKey; value: string; visible: boolean; forcedBy: FunctionId | null };
export type Unknown = { id: string; text: Text };

export type Setup = {
  parts: ResolvedPart[];
  functions: ResolvedFunction[];
  options: ResolvedOption[];
  wires: Wire[];
  checks: Check[];
  unknown: Unknown[];
  conflicts: never[];
  ignored: string[];
};

/** What `configurator/core/index.ts` and the stub both export. */
export type Core = {
  CATALOGUE: Catalogue;
  /** Pure and total. Never throws. Output arrays are in catalogue order. */
  resolve: (catalogue: Catalogue, selection: Selection) => Setup;
  /** Default options: pads, matek, battery. */
  DEFAULT_OPTIONS: Options;
};
