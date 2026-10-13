// Contract between the core (data, resolver, checks) and the UI (page, drawing, workflow).
// Types only. Changing this file is a change to the specification.

export type Lang = 'en' | 'de';
export type Text = { en: string; de: string };

export type Src =
  | { kind: 'repo'; path: string; line: number; tag: string }
  | { kind: 'guide'; section: string }
  | { kind: 'photo'; file: string }
  | { kind: 'owner'; date: string }
  | { kind: 'doc'; title: string; url: string; ref: string }
  | { kind: 'page' };

export type Release = 'released' | 'main' | 'planned';

export type PartId = string;
export type FunctionId = string;
export type PinId = string;

export type Options = {
  connection: 'pads' | 'osoyoo';
  ina228Board: 'matek' | 'xt90';
  escPower: 'battery' | 'pd';
  /** Plus of the servo: a supply of the visitor, or the bench itself. */
  servoPower: 'external' | 'bench';
  /**
   * The pin of the servo signal: a pad bound under OUTPUTS, or the
   * programming port of the coprocessor. No visitor choice and no hash
   * token: the function that programs a servo forces it.
   */
  servoPort: 'outputs' | 'prog';
};
export type OptionKey = keyof Options;

export type Pin = {
  id: PinId;
  label: Text;
  confirmed: boolean;
  only?: Partial<Options>;
  src: Src;
};

/**
 * A picture of a part in the picture folder of the page.
 *
 * - photo: `<file>.webp` and `<file>-thumb.webp`, the thumb 480 px wide.
 * - drawing: `<file>.svg`.
 *
 * `width` and `height` are the pixel size of the file a part card shows: the
 * thumb of a photo, the file of a drawing. `only` limits the picture to the
 * options it names.
 */
export type Picture = {
  kind: 'photo' | 'drawing';
  file: string;
  alt: Text;
  width: number;
  height: number;
  only?: Partial<Options>;
};

/**
 * One row of the parts list: what to order, or what the visitor brings.
 *
 * - `maker`: the maker's name, as the trademark notice lists it. `null` for a
 *   generic part and for a modelled part: `order` then states the spec.
 * - `order`: the ordering name or model as the maker writes it, else the spec.
 * - `check`: what to check when buying, the variant traps. One short line.
 * - `page`: the maker's product page, https. Absent when no stable maker page
 *   exists. No shop, no tracking parameter.
 * - `qty`: pieces, 1 when absent.
 * - `only`: the options the row applies to.
 */
export type Buy = {
  maker: string | null;
  order: Text;
  check: Text;
  page?: string;
  qty?: number;
  only?: Partial<Options>;
};

/**
 * A part of the bench, or a test object.
 *
 * A test object (`testObject: true`) is the device a visitor connects and
 * tests: the bench needs none. It is in a setup when a chosen function
 * drives it, never as a part a function needs, never as an owned part. It
 * keeps a slot and pins, because its wires end on it.
 */
export type Part = {
  id: PartId;
  name: Text;
  short: Text;
  role: Text | null;
  locked: boolean;
  selectable: boolean;
  testObject?: true;
  /**
   * A modelled part: the catalogue states what the part has to do, not which
   * product does it. Its name gives the required range and its role the
   * values a source states; no maker, model or picture is chosen.
   */
  modelled?: true;
  /** The part sits in the path between these 2 parts: a wire joins it to each of them. */
  between?: [PartId, PartId];
  release: Release;
  slot: { column: 'L' | 'R' | 'none'; order: number; dashed?: boolean; sub?: Text };
  pins: Pin[];
  variantName?: { option: OptionKey; names: Record<string, Text> };
  /** Pictures of the part, the main one first. At most 2 apply to one set of options. */
  pictures?: Picture[];
  /**
   * The visitor brings the part: its spec follows the test object, so the
   * page names no product. A test object is always brought.
   */
  yours?: true;
  /** The rows of the parts list. Every part has at least 1 for each set of options. */
  buy: Buy[];
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

/**
 * A function of the bench.
 *
 * - `needs`: the bench hardware it needs. No test object.
 * - `drives`: the test object it acts on. A chosen function brings that test
 *   object and its wires into the setup.
 */
export type Fn = {
  id: FunctionId;
  group: 'servo' | 'esc' | 'bench';
  name: Text;
  needs: PartId[];
  drives?: PartId;
  forces?: Partial<Options>;
  src: Src;
};

export type Rule = { id: string; when: Cond; adds: PartId; because: Text; src: Src };
export type Check = { id: string; kind: 'measure' | 'unknown' | 'note'; text: Text; when: Cond | null; src: Src };
export type Stage = { n: 1 | 2 | 3 | 4 | 5; title: Text; src: Src };

/**
 * A source of power for a test object or for the logic of the bench.
 *
 * - `label`: the name of the source, a few words.
 * - `text`: what the source is and how it connects. It stands on its own, so
 *   it moves into a help text without a change of wording.
 * - `volts`, `amps`: numbers a source states, else `null`. `steps` lists the
 *   values of a source that has no range. `basis` states where they come from.
 * - `pros`, `cons`, `unknowns`: one short sentence each.
 * - `when`: the setups that offer the source. `null`: every setup.
 * - `used`: the setups whose wires use the source.
 * - `choice`: sources with the same value stand for one part of the wiring.
 *   The setup uses one of them and does not state which.
 */
export type PowerSource = {
  id: string;
  feeds: 'servo' | 'esc' | 'logic';
  label: Text;
  text: Text;
  volts: { min: number; max: number; steps?: number[] } | null;
  amps: { min: number | null; max: number } | null;
  basis: Text | null;
  pros: Text[];
  cons: Text[];
  unknowns: Text[];
  when: Cond | null;
  used: Cond;
  choice?: string;
  src: Src;
};

export type PinDefault = {
  signal: 'heartbeat' | 'sda' | 'scl' | 'tap' | 'esc' | 'servo' | 'prog' | 'pd_tx' | 'pd_rx';
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
  powerSources: PowerSource[];
};

export type Selection = {
  functions: FunctionId[];
  owned: PartId[];
  options: Options;
};

/**
 * The values the visitor chooses on the motor simulator: the LiPo cell count
 * of the ESC pack (4.2 V per cell), the resistance in Ω and the power rating
 * in W of each of the 3 resistors, and how they are joined. They name the
 * part and its row of the parts list and give the heat per resistor. They
 * change no wire and no function.
 */
export type SimSettings = {
  cells: number;
  ohms: number;
  watts: number;
  wiring: 'delta' | 'star';
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
/** A power source the setup offers. `used`: the wires of the setup use it. */
export type ResolvedPower = { source: PowerSource; used: boolean };

export type Setup = {
  parts: ResolvedPart[];
  functions: ResolvedFunction[];
  options: ResolvedOption[];
  wires: Wire[];
  checks: Check[];
  unknown: Unknown[];
  /** The power sources of the logic and of each test object in the setup, in catalogue order. */
  power: ResolvedPower[];
  ignored: string[];
};

/** What `configurator/core/index.ts` and the stub both export. */
export type Core = {
  CATALOGUE: Catalogue;
  /** Pure and total. Never throws. Output arrays are in catalogue order. */
  resolve: (catalogue: Catalogue, selection: Selection) => Setup;
  /** Default options: pads, matek, battery, external, outputs. */
  DEFAULT_OPTIONS: Options;
};
