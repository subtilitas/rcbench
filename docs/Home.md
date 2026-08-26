# rcbench

<sub>These pages are generated from `docs/` in the repository and are
overwritten on every push to `main` — edit the files, not the wiki.</sub>

A **motor, ESC and servo test bench** in two halves: an ESP32-S3 panel that
decides, draws and stores, and an RP2350 coprocessor that measures, drives and
talks to everything with a deadline.

> The rule that settles arguments: **the coprocessor owns anything with a
> deadline, the panel owns anything with an opinion.**

The repository's [README](https://github.com/subtilitas/rcbench) is the running
record — what is built, what is not, and what has not been settled. These pages
are the reference behind it, and they are written by the commit that lands the
code they describe rather than ahead of it. A page that is missing is a
subsystem that is not here yet.

## Pages

| Page | What it covers |
| --- | --- |
| [What this is for](Manifest.md) | The pitch, what each line of it means, and where it stands |
| [Screens](Screens.md) | The shell: the status band, the splash, the menu, and adding a screen |
| [The link](Link.md) | The frame, the page and register model, the decoder, and what the wire costs |
| [Safety](Safety.md) | The heartbeat, and the three independent ways this bench stops |
| [Performance](Performance.md) | Why the frame rate is what it is, and the two findings that shape every screen |
| [Building](Building.md) | The tree, the three toolchains, and what CI checks |
