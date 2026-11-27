---
id: BUG-1597
title: UI: F9 Space tab pager buttons stay enabled at first and last page
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
signature: Space tab < / > pager buttons remain clickable on single-page or boundary pages
switch: none (UI button enabled-state update in coop_ui.cpp)
docs:
related:
---

## Report
Unlike the Rivals and Settings tabs, the Space tab `<` and `>` pager buttons remained enabled on the first and last page.

## Root cause
`coop_ui.cpp` did not update the Space pager button enabled states against the current page bounds.

## Fix
Disabled `<` at page 0 and `>` on the final page of the Space tab in `mods/coop/coop_ui.cpp` to match Rivals and Settings pagination.

## Verification
- no-fail-run: UI button enabled-state fix verified by `make test`.
