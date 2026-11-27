---
id: BUG-1593
title: UI: F9 Rivals row action buttons clip localized captions due to narrow width padding
status: fixed
kind: bug
stage: UI
reported: 2026-11-26
source: AI playtest (v0.8.2.3)
role: both
versions: 0.8.2.3
fixed_in: 0.8.2.4
branch: main
commits: 20fa22f2
tests: make test
signature: Rivals row action button label clipped inside native button border
switch: none (UI button layout padding calculation in coop_ui.cpp)
docs:
related:
---

## Report
Longer Rivals action button labels clipped against native UTFWin button borders in F9 -> Rivals.

## Root cause
`coop_ui.cpp` sized Rivals row buttons without accounting for the full native border padding.

## Fix
Widened dynamic Rivals row button sizing in `mods/coop/coop_ui.cpp` with full native border padding (`+58 px`, up to `280 px`).

## Verification
- no-fail-run: UI layout padding calculation verified in `make test` and live F9 screenshots (`out/ai-playtest/20261127-075046`).
