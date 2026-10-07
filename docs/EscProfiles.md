# ESC programming profiles

[Deutsch](EscProfiles-de.md)

An ESC (electronic speed controller) profile describes how to program one
family of ESCs through its throttle-stick menu. It covers how the menu is
entered, how the ESC sounds a number, and which items and values the menu
holds. It is the data [stick programming](StickProgramming.md) runs: the
operator picks an ESC and the values to change, and the bench drives the
throttle and counts the beeps in the supply current. 24 of the 72 profiles
are of a kind it runs.

## What is in the set

| | Count |
| --- | --- |
| Profiles (families) | 72 |
| Models | 451 |
| Brands | 20 |
| Menu items | 360 |

| Who can run a profile | Profiles | Models |
| --- | --- | --- |
| The bench alone: throttle signal and power | 44 | 340 |
| A person sets a jumper, presses a button or reads an LED | 26 | 108 |
| of those, with the steps listed in `manual` | 24 | 88 |
| Nobody: the manual gives no usable procedure | 2 | 3 |

Every profile is `"verified": false`. The profiles come from 153 manuals and
were written without an ESC on a bench. No manual states a beep length, a
gap or a loop time in milliseconds, so every time field is null until a
bench recording measures it.

## Where profiles live

| Store | Path | Read |
| --- | --- | --- |
| Built in | `shared/esc/profiles/*.json`, compiled into the panel image | always |
| SD card | `/ESC/*.json` | once, at start-up |

A card file is named after its id: `/ESC/hobbywing-flyfun-8item.json` holds
`"id": "hobbywing-flyfun-8item"`. Upper and lower case may differ; anything
else is refused. A card file with the id of a built-in profile replaces that
profile. A card file with a new id adds a profile after the built-in ones.
File names of 64 characters or more are not read. A card file that would
take its maker past 512 models (`ESC_MAKER_MODELS_MAX`), counted across
the maker's profiles built in and from the card, is refused. The card holds at most 32
profiles; the panel reads the names of the first 64 `.json` files in
`/ESC/`. A refused file is named on the console with its reason, for example:

```
ESC profiles: MYESC.JSON refused: items[2].values: not 1-255 entries
```

A file is refused when it:

- is larger than 64 KiB, is not JSON, or is not UTF-8;
- nests deeper than 16 levels below the top object, or has an object of
  more than 64 members;
- has a key twice in one object, compared as it decodes (`"sch\u0065ma"` is
  `"schema"`);
- holds `\u0000` or half a surrogate pair in any string;
- is named other than its id;
- breaks a rule below.

The generator applies the same rules to the files in the repository. The panel runs without a card; the built-in profiles are then
the whole set.

## The file

One JSON object per file. `id` is the file name without `.json`, 1 to 48
characters of `a-z 0-9 -`.

```json
{
  "schema": 1,
  "id": "hobbywing-flyfun-8item",
  "brand": "Hobbywing",
  "family": "FlyFun 6A-100A sensorless, 8-item menu",
  "verified": false,
  "automatable": "full",
  "automatable_note": "",
  "sources": [{"file": "...pdf", "pages": "3-4", "url": "https://..."}],
  "models": [...],
  "scheme": {...},
  "items": [...],
  "unknowns": ["Beep duration in ms is not stated."],
  "notes": []
}
```

`sources`, `unknowns`, `notes` and every `description` field stay in the
JSON. The panel does not load them.

### Fields the panel reads

| Field | Values |
| --- | --- |
| `automatable` | `full`, `assisted`, `none`; anything but `full` needs `automatable_note` |
| `manual` | an `assisted` profile's steps a person does at the ESC; see [Manual steps](#manual-steps) |
| `scheme.type` | `count`, `short_long`, `melody_groups`, `yes_no`, `stick_position`, `other` |
| `scheme.entry.throttle` | `min`, `mid`, `max`: the stick position that opens the menu |
| `scheme.entry.when` | `before_power_on`, `after_power_on` |
| `scheme.entry.hold_ms` | 0 to 600000, or null when not stated |
| `scheme.entry.steps` | 1 to 255 operator actions, in order |
| `scheme.announce.what` | `item`, `value`, `item_then_value` |
| `scheme.announce.encoding` | `count`, `short_long`, `melody`, `yes_no` |
| `scheme.announce.long_equals_short` | short beeps per long beep; required for `short_long` |
| `scheme.announce.beep_ms`, `gap_ms`, `group_gap_ms` | 0 to 60000, or null |
| `scheme.announce.repeat` | 0 to 255: 0 repeats until a choice is made, null not known |
| `scheme.select.throttle`, `scheme.skip.throttle` | `min`, `mid`, `max`, `none` |
| `scheme.listen` | where the stick rests while the menu sounds: `{"throttle": "min"}`, `mid` or `max`. Absent or null: where the entry left it. Set in the 9 YGE profiles, whose menu sounds with the stick at minimum after a maximum entry; stick programming runs such a profile only when `hold_ms` gives the entry's length |
| `scheme.store` | the move that stores a selection once the ESC has answered it: `{"throttle": "min"}`, `mid` or `max`. Absent or null: the selection stores. Set in the 8 YGE mode setups, where the stick back to minimum stores the mode |
| `scheme.select.within_ms` | 0 to 60000: the time after the tone in which the move counts, or null when not stated |
| `scheme.value_select` | two-stage menus only: `select` picks the item, then `value_select.throttle` (`min`, `mid`, `max`, `none`) stores the value sounded; `within_ms` as for `select`. Absent or null: the `select` move stores the value |
| `scheme.changes_per_entry` | `one`, `many` |

### Models

| Field | Values |
| --- | --- |
| `name` | unique within the profile |
| `cells_min`, `cells_max` | 0 to 255, or null |
| `cell_type` | `lipo`, `nimh`: what `cells_*` count |
| `v_max_mv` | maximum input in mV, or null |
| `current_a` | continuous current in A, 0 to 65535, or null |

### Items

| Field | Values |
| --- | --- |
| `number` | 1 to 255, as the ESC sounds it |
| `name` | as the manual names it |
| `key` | 1 to 32 of `a-z 0-9 _`; one meaning across brands: `brake`, `timing`, `cutoff_voltage`, `cutoff_type`, `battery_type`, `cell_count`, `startup`, `governor`, `direction`, `throttle_range`, `pwm_freq`, `aircraft_type`, `mode`, `reset` |
| `values` | 1 to 255 of `{"number": 0-255, "name": "...", "default": true}`; numbers unique, at most one default |
| `values[].entry_throttle` | `min`, `mid`, `max`: the stick position the manual programs this value from, where it is not the entry's; absent or null, the entry's. Stick programming powers the ESC up there for that value |
| `values[].entry_hold_ms` | 0 to 600000: power-on to the menu when this value is programmed, where the manual gives a wait other than `scheme.entry.hold_ms`; absent or null, the entry's. Kontronik SUN PLUS modes 4 to 6 wait 5000 ms |
| `values[].after_select` | 1 to 4 of `min`, `mid`, `max`: the moves the manual asks for after this value's select move, in order, each once the ESC has answered the one before; absent or null, none. The Kontronik car modes and PIX mode 2 go to `min`, the brake. Stick programming makes each STORE after the one before, after the profile's `scheme.store` move; a two-stage profile takes none |
| `applies_to` | model names of this profile, or null for all |
| `applies_when` | a condition in words, e.g. `"model type heli"` |

Two items may share a number only when both carry `applies_to` or
`applies_when`.

### Manual steps

`manual` lists what a person does at the ESC besides the throttle and the
power, in the order a run meets it. Absent or null: none.

```json
"manual": [
  {"when": "before_power", "action": "Fit the jumper on any 2 of the 3 programming contacts.",
   "source": "Kontronik_Beat.pdf p.5"},
  {"when": "before_menu", "action": "Pull the jumper off after 2 s or the tone sequence."}
]
```

| Field | Values |
| --- | --- |
| `manual` | 1 to 4 steps; only on an `automatable: "assisted"` profile |
| `when` | `before_power`, `at_power_up`, `before_menu`, `during_menu`, `after_programming`; each step no earlier than the one above it |
| `action` | 1 to 120 bytes of UTF-8, in English: two lines of the screen's pop-up |
| `action_de` | the same step in German, 1 to 120 bytes of UTF-8, umlauts included; absent or null when none, and the English shows in German too |
| `starts_menu` | `true` where the action itself starts the menu's series, as a Kontronik jumper pulled or button pressed does; absent, null or `false` otherwise. Only on a `before_menu` step, at most one a profile, and no `before_menu` step after it. Stick programming listens from the moment it asks for that step ([Stick programming](StickProgramming.md#manual-steps)) |
| `hold_ms` | `at_power_up` only: 0 to 60000, how long the step is held after the supply comes on; absent or null when not stated |

`source` and any other member of a step stay in the JSON. The action is the
profile's own text: in German where the profile gives `action_de` and the
interface is German, else in English. Every step of record has its German.
The screen translates when it is due. What a run does with
each step is in [Stick programming](StickProgramming.md#manual-steps).

24 profiles hold steps: the 22 Kontronik families, `turnigy-aquastar` (its
switch at full throttle) and `greatplanes-electrifly-c-series` (its on/off
button). `graupner-brushless-control-t`, whose menu is read from LEDs, and
`hacker-master-senstrol`, which needs its motor's identification chip, a
second channel and a JetiBox, are `assisted` with none: no step a person can
be asked for at a moment.

## Adding or correcting a profile

On one bench: copy the file to `/ESC/` on the card and restart the panel.

In the repository: edit or add the file under `shared/esc/profiles/`, then
regenerate the C file and check it:

```sh
python3 tools/gen_esc_profiles.py
python3 tools/gen_esc_profiles.py --check
```

CI (continuous integration) runs `--check`. The host suite parses every file
with the panel's reader and compares the result with the generated table,
field by field, so the generator and the card reader accept the same files.

## Current limitations

- No profile has run against an ESC. Item and value numbers, defaults and
  entry gestures are as the manuals state them, and some manuals disagree
  with each other; each profile's `notes` names the conflicts.
- The beep fields (`beep_ms`, `gap_ms`, `group_gap_ms`) are null in every
  profile and stick programming does not read them: its timing comes from
  its settings.
- The bench supply is the PD mini, at most 20 V. ESCs whose minimum input is
  above that, such as 6S-and-up YGE Opto and Navy models, need an external
  supply.
- Where a profile's menu is told apart by pitch rather than by count (Hitec,
  Mystery, Readytosky), the supply current may not separate the items. This
  is not measured.
- A card changed while the panel runs is read at the next start.
