---
id: BUG-9042
title: Founder archive rejects native landfall creatures retaining Cell paint
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Native converted Stabbella founder archive
role: guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_founding.py
switch: creations.archive_cell_paints
signature: Saved guest creature needs missing paint 2F363540
docs: docs/re/creations.md
related: BUG-9040 BUG-9041
---

After correcting compressed header decoding, m2-auto-found-b.log stops
before creating a village because native Stabbella retains base layer
2F363540 with zero coat/detail. This is the guest's actual native landfall
creation, saved with legs in the early Creature editor. Existing RE notes
already identify stock Cell paint IDs absent from the Creature paint list.
Verify the finite set against shipped Cell models before accepting them.

tools/cell_paints.py reads44 shipped Cell BEMs and derives19 nonzero base
paints, including2F363540. Archive validation accepts only these known IDs in
the base layer with zero coat/detail. Other paints and all parts retain their
existing availability checks. No paint substitution is changed.

FAIL m2-auto-found-b.log: this native inherited paint is refused. PASS
m2-auto-found-d.log and m2-auto-found-cold-a.log: original species7CF26CCB
loads, persists and walks on both games after automatic founding and cold
restart. Both runs exit with zero native faults.
