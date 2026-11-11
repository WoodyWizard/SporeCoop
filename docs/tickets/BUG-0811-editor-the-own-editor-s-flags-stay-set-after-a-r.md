---
id: BUG-0811
title: Editor: the own editor's flags stay set after a refused Submit (part-list waiver and own-editor price on the next editor)
status: open
kind: bug
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: 
related: BUG-0466 BUG-0720
---

## Report
No player report. After an own editor request the game refused (or a mode switch that kept it from opening), the next native editor accepts parts the stage list forbids and a later editor result can be charged as an own-editor price (DNA lost, also sent to the host as a spend).

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): coop.cpp:1639-1645 never clears g_own_editor_charge_due when Submit returns false; g_own_editor is cleared only on leaving an editor mode (coop.cpp:2392); the session end (4031-4068) resets neither; h_BlockingErrors (1526) waives the part-list error while they are set. Confidence: high on the code, rare path.

## Fix
Not fixed yet.

## Fix idea
Clear both flags when Submit fails, at the session end, and when no editor mode follows within ~10 s.

## How to test
Guest: `coop.vote hold` with a mode switch forced as the own editor opens (Submit refused or no editor), then open a native editor: g_part_list_waivers must not rise (fix) vs rises (main).

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
