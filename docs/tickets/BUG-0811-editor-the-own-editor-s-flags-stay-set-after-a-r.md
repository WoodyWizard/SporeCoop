---
id: BUG-0811
title: Editor: the own editor's flags stay set after a refused Submit (part-list waiver and own-editor price on the next editor)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-creature
commits: a6704bd5
tests: own_editor_cancel, make test
signature:
docs: docs/re/creature.md#creature-c2-audit-0713
related: BUG-0466 BUG-0720
---

## Report
No player report. After an own editor request the game refused (or a mode switch that kept it from opening), the next native editor accepts parts the stage list forbids and a later editor result can be charged as an own-editor price (DNA lost, also sent to the host as a spend).

## Root cause
Observed in the code; the trigger is not reachable in play. open_own_editor cleared g_own_editor on a refused Submit but left g_own_editor_charge_due, and end_session reset neither. But the game's `Editors::EditorRequest::Submit` 0x5A92C0 always returns 1 (Ghidra, dc04: it posts the request 0xB03BC30C and the mode switch message 0xE11332, sets the cursor, returns 1); with g_vote_bypass set (the own editor) the mod's h_Submit refuses only for the BUG-0720 test capture (`coop.editreq`). A submitted request opens its editor through the posted switch, whose end clears g_own_editor; its result clears the price. So neither the waiver nor the price outlives a real request.

## Fix
Hardening, as the ticket asked: a refused Submit also clears the price due, and the session end clears both flags unless the player is in an editor (its result still comes). Switch `coop.fix creations.own_editor_flags off`.

## Fix idea
Clear both flags when Submit fails, at the session end, and when no editor mode follows within ~10 s.

## How to test
Guest: `coop.vote hold` with a mode switch forced as the own editor opens (Submit refused or no editor), then open a native editor: g_part_list_waivers must not rise (fix) vs rises (main).

## Verification
no-fail-run: the refused Submit is not reachable in play (the game's Submit always returns 1, docs/re/creature.md#creature-c2-audit-0713); only the mod's test capture refuses. The change is two flag resets on paths that never ran in a session; covered by make test and the Creature runs of this branch (own_editor_cancel, smoke).
* `own_editor_cancel` (the guest's own Creature editor, its red cross) passes with the change: out/lab/20261112-080250 (lane 7).

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12 (fix/w48-creature, C2 of 0.7.13): the trigger is not reachable (EditorRequest::Submit returns 1 always); hardening behind creations.own_editor_flags.
- 2026-11-12: released in 0.7.13.
