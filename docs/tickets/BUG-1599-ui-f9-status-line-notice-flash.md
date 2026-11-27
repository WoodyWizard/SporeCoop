---
id: BUG-1599
title: UI: transient Rivals notices and errors are not reflected on the F9 status line
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
signature: F9 status line does not surface recent Rivals action notice or refusal reason
switch: none (F9 status line mirror in coop_ui.cpp and coop.cpp)
docs:
related:
---

## Report
When a Rivals button action failed or posted a local notice while F9 was open, the F9 status line did not reflect the message.

## Root cause
Local notices only went to the corner feed overlay and were not echoed onto the F9 window status line.

## Fix
Added a transient notice/error flash on the F9 status line in `mods/coop/coop_ui.h`, `mods/coop/coop_ui.cpp`, and `mods/coop/coop.cpp` so feedback is immediately visible both inside F9 and in `coop.uistate`.

## Verification
- no-fail-run: UI status-line mirror verified by `make test` and live 4-player playtest (`out/ai-playtest/20261127-075046`).
