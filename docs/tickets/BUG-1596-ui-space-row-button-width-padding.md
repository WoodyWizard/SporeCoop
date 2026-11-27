---
id: BUG-1596
title: UI: F9 Space row action buttons clip localized captions due to narrow width padding
status: fixed
kind: bug
stage: UI
reported: 2026-11-26
source: AI playtest (v0.8.2.3)
role: both
versions: 0.8.2.3
fixed_in: 0.8.2.4
branch: main
commits: 225ccb98
tests: make test
signature: Space row action button label clipped inside native button border
switch: none (UI button layout padding calculation in coop_ui.cpp)
docs:
related:
---

## Report
Longer Space tab action button labels clipped inside native UTFWin button borders in F9 -> Space.

## Root cause
`coop_ui.cpp` did not apply full native border padding when widening Space row action buttons.

## Fix
Widened dynamic Space row button sizing in `mods/coop/coop_ui.cpp` with full native border padding (`+58 px`, up to `280 px`).

## Verification
- no-fail-run: UI layout padding fix verified by `make test` and live F9 layout checks.
