---
id: BUG-1524
title: Creature: after the switch to the host's Creature world the HUD portrait still shows the Cell creature, and herbivores have no herbivore mouth
status: duplicate
kind: bug
stage: Creature
reported: 2026-11-23
source: discord
role: guest
versions: 0.7.15.5
fixed_in:
branch: fix/w82-creature
commits:
tests: hud_portrait (BUG-1426's regression)
signature:
docs: docs/re/creature.md#creature-meter-hold
related: BUG-1426, BUG-0937, BUG-1434
---

## Report
Player report on Discord 2026-11-22 (coop 0.7.15.5, Steam; out/research/dc-report-20261122-world-switch.md section 5, the guest's own marks after the world switch): mark 2: the bottom-right portrait still the Cell creature; "neither of us has a herbivore mouth but we are herbivores". No matching ticket found with `tickets.py find`. Next: fix-player-report (reproduce with a test that fails first).

## Root cause
* Portrait: the report's module list has `SporeApp.exe` at 0x00CF0000, not 0x00400000: Windows relocated the exe, and
  0.7.15.5's portrait fix (BUG-0937) compared the drawable's vtable with a raw link-time number that never matched a
  relocated exe, so the HUD kept the game's first picture (the Cell creature). That is BUG-1426 (UIFX-3), fixed in
  0.7.16.1 (the vtable from the symbol `UTFWin::ImageDrawable::vftable`).
* Herbivore mouth: not a mod fault found. Both creatures were the players' own edits in their editors (the guest's
  `own=... from choices.ini`, the host's 4-record creation) of the creature line of an old planet of the other
  player's galaxy played from the co-op copy (BUG-1434); the mod never adds or removes parts. Which mouth the Cell's
  diet implies is the game's own rule; the shots do not show the creatures.

## Fix
None here (the portrait: BUG-1426 in 0.7.16.1).

## Verification
* no-fail-run: the portrait is BUG-1426 (fixed in 0.7.16.1; Wine never relocates the exe, its regression is
  `hud_portrait`); the mouth half is not reproducible from the logs.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 12 (WP-C1), branch fix/w82-creature.
- 2026-11-23: analysed on fix/w82-creature: the portrait is BUG-1426 (relocated exe in the report), closed as its duplicate.
