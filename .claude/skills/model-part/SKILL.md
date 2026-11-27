---
name: model-part
description: Build a STEP model of a purchased part for hardware/mechanical from the maker's STEP file, the maker's drawing, or owner photos on millimetre paper (top, underside, edge-on). Use when the owner sends photos of a part on scaled paper, asks for a 3D model of a board or device, or gives a datasheet or STEP link for one.
---

# Model a part

The process is written in `hardware/mechanical/README.md`, section
"Modelling a part". Read it first; this skill is the working checklist.

## 1. Find the best source

- Search for the maker's STEP file and drawing before using photos.
  Ask the owner for a link or PDF when a search finds none.
- A maker's STEP file is measured, never copied into the repository:
  read it with OpenCASCADE in a `_measure_<part>.py` that writes a data
  file, as `_measure_esp32_s3_lcd_185.py` does.
- A drawing's numbers go into the spec with its name and revision.

## 2. Photos on millimetre paper

- Ask for 3 views if missing: top side, underside, edge-on. Name what
  is missing; do not guess a height that an edge-on photo would give.
- The paper has lines every 1mm, 5mm and 10mm. Check the scale against
  all three. A 5mm line read as 10mm halves every size; this happened
  once (87px per 5mm, not per 10mm).
- Fit px/mm in a crop round the part from 20 or more 1mm lines.
- Correct heights: a face h over the paper is larger by D / (D - h).
  Read D from the photo's lens data (76mm equivalent: about 265mm).
- Measure each value in the view that shows it square. Record the
  error: pixel size, edge blur of 3 to 5px, the board's lean.
- Crop and zoom before reading a print, a part marking or a pad count.

## 3. Write the spec

- `hardware/mechanical/<part>.py` with `parts(colors)`, helpers from
  `_parts.py` (`usb_c`, chip packages) where they fit within the error.
- Docstring: product name and maker, every source (photos described,
  not in the repository), scale and correction, each measured value
  with its view, every estimate listed as an estimate, the result's
  error. Follow `pd_trigger_pd31.py`.
- Colours are sRGB values; the STEP writer converts them.
- Register the stem in `_models.py` (`pcb`, `casing` or `parts`) and
  update the counts in `test/tools/test_models_folders.py` in the same
  commit as the spec, never before it.

## 4. Check against the photos

- Render the model from the photos' 3 directions with step_render
  (OpenCASCADE); put each render beside its photo.
- Look for a mirrored side, a part off by a pitch, a wrong pin count,
  a print on the wrong face.
- Send the owner the renders before committing to a merge.

## 5. Finish

```sh
python3 hardware/mechanical/gen_models.py
python3 hardware/mechanical/gen_models.py --check
xvfb-run -a python3 hardware/kicad/tools/step_render.py \
    hardware/mechanical/renders --size 640 480 --folders \
    --step hardware/mechanical/models/<folder>/<part>.step
python3 -m pytest -q test/tools/test_models_folders.py
python3 tools/check_prose.py && python3 tools/check_units.py
python3 tools/check_docs.py && python3 tools/check_sizes.py
ruff check hardware/mechanical/ test/tools/
```

- Add the part to the tables in `hardware/mechanical/README.md` (the
  file table at the top and the part's section) and to `CHANGELOG.md`.
- Writing rules: present tense, numbers with units, unknowns stated as
  unknown, no marketing words.
- Commit as the owner; merge only after the owner has seen the renders.
