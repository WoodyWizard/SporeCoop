---
id: BUG-1574
title: Raise Cell knockout opacity from 0.35 to 0.65 so knocked-out cells remain clearly visible
status: fixed
kind: bug
stage: Cell
reported: 2026-11-26
source: YouTube 0.8.0 stream audit
role: both
versions: 0.8.0
fixed_in: unreleased
branch: main
commits: 1e808fcb
tests: cell_ko_gap
signature:
docs: docs/re/cell.md
related: BUG-0238, BUG-1575
---

## Report
In Cell stage co-op, when a player's cell was knocked out, players on the 0.8.0 stream thought their cell had vanished / disappeared ("куда я пропал?") because `0.35` opacity made the cell nearly invisible against the blue/green tidepool background.

## Root cause
- `mods/coop/cellsync.cpp` set `kKoOpacity = 0.35f` on knocked-out avatars and remote partner puppets.

## Fix
Implemented under switch `cell.ko_visibility` (`coop.fix cell.ko_visibility off` restores `0.35f` for testing):
- Raised `kKoOpacity` from `0.35f` to `0.65f` via `ko_opacity()` in `mods/coop/cellsync.cpp`, keeping knocked-out cells visibly distinct from living cells (`1.0f`) while remaining clearly visible on screen.
- Localized Cell KO, revive, timeout, and small-respawn notices through `i18n::tr()`.
- Updated Cell KO opacity expectations in `tools/tests/coop_cell_ko_gap.py`, `tools/tests/coop_cell.py`, and `tools/tests/coop_party_cell.py`, and registered `cell_ko_visibility_off` twin in `tools/tests/suite.py`.

## Verification
- Wine lab ON/OFF twin (`out/lab/20261126-132450`):
  - `cell_ko_gap` (ON): **PASS** (37 s, 0 faults; knocked-out partner puppet drawn at `0.65` opacity throughout the gap and after).
  - `cell_ko_visibility_off` (`SPORE_FIX_OFF=cell.ko_visibility`): **FAIL** (37 s; opacity reverted to `0.35`, failing all 28/28 and 20/20 opacity samples).

## History
- 2026-11-26: opened from 0.8.0 YouTube stream audit.
- 2026-11-26: fixed on `main` (`1e808fcb`), verified with ON/OFF Wine lab twin (`out/lab/20261126-132450`).
