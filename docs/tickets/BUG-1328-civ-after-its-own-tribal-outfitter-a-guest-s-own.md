---
id: BUG-1328
title: Civ: after its own Tribal Outfitter, a guest's own City Hall editor at its Civ arrival offers no random-name dice: the driver cannot name it
status: open
kind: bug
stage: Civ
reported: 2026-11-19
source: lab
role: 
versions: 
fixed_in:
branch: fix/w58-linestages
commits:
tests: line_stages_outfit
signature:
docs:
related: BUG-1272, BUG-1276, BUG-1277
---

## Report
Lab, line_stages_outfit on rel/0.7.16 (fix/w58-linestages, lane 5): runs 20261119-165858, 20261119-171852,
20261119-173854, 20261119-180816 (4 of 4 since the w57 merges; 20261119-162318 on fix/w57-own-editors passed the same
step once). The guest made its tribe outfit in its own Tribal Outfitter (WP-T2, BUG-1272), followed the host into Civ
and got its own City Hall editor at the arrival (WP-X1, BUG-1276). The campaign's driver dragged three parts, pressed
"done" and the name panel's dice (614, 826), as it does in every other editor: "You must give your creation a name
before you can save" stayed for 25 tries (`coop.civarrival` phase=open, results=0). The guest's screen
(out/lane5/shots/arrival_editor_120.png, stuck_outfit_cityhall*.png, full2.png): the expanded name panel ("Name your
City Hall", Description, Tags) has **no dice** left of the name field; WM_CHAR text (`text`) and key taps do not reach
the name field (key taps went to the editor's hotkeys: its help page). Without the outfit (line_stages,
line_stages_editors, line_stages_nodesign_off: 6 runs) the same editor is named by the dice and ends at the 4th
"done".

## Root cause
Not found. Observed: no dice in the name panel of the guest's own City Hall editor only after the guest's own Tribal
Outfitter; no warning in either game's log. Inferred (unverified): the random-name button depends on something the own
outfitter changed on the guest's game (its player species is then the outfit, a line creation 0x7B...... without a
name of its own?). Whether a player can type a name there is not verified (the kit's text input does not reach Spore's
name field in any editor, so a driver cannot tell). Needs the name panel's dice condition (Ghidra) and a hand test.

## Fix
Not fixed. Test side (fix/w58-linestages, BUG-1277): the campaign's driver leaves a guest's own arrival editor with
the red cross after 14 "done" (`Campaign.arrival_cancel_after`; the player's way to keep the civilization's designs,
the note names this ticket), so line_stages_outfit reaches its Civ checks; line_stages --civ-editors keeps no red
cross (its designs are checked).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-19: opened (fix/w58-linestages): the lab runs above, the screenshots; open, not in that branch's task.
