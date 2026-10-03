The merge holds a function open, with no part, when P3 overturns a P2 drop and the re-rank does not handle the part under that function.

- P3's `exclusions_not_holding` entries carry `function`: the function P2 dropped the part under. The merge holds only that function open. Where P2 dropped the part under no function of that name, or P3 names none (recorded outputs), the drop counts as overturned under every function that dropped the part, as before.
- A part number with a `#` after 4 characters or more, with a letter and a digit, also matches the number before the `#`. This is Analog Devices' packing and RoHS option: `LTC4020EUHF#TRPBF` handles a find of `LTC4020EUHF#PBF`. Other makers' packing suffixes (TI's reel letter, Maxim's `+T`) remain other part numbers. The README states this as a current limitation.

Effect on FU-2B1's R7 record: P3 overturned P2's drop of `BQ25887RGER` under Cell balancing only, and its drop of `LTC4020EUHF#PBF` (C2858365). The re-rank dropped BQ25887 under Cell balancing and ranked `LTC4020EUHF#TRPBF` (C462630) fifth. The merge held Pack charger and Power path open for both parts, so their first-ranked BQ25723RSNR went unverified.

A replay of all 50 recorded P3 outputs through `main` and this branch differs in one item: the `LTC4020EUHF#PBF` follow-up of FU-2B1 R7. The BQ25887 item stays, because recorded P3 outputs name no function.

Dry run: 7 new cases. Two of them fail on `main`.
