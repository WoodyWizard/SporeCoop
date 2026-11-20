---
id: BUG-1328
title: Lab lane 5: the guest's own City Hall editor at its Civ arrival offers no random-name dice: the driver cannot name it
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
Lab, fix/w58-linestages on rel/0.7.16: the guest's own City Hall editor at its Civ arrival (WP-X1, BUG-1276) never
ended on **lane 5**: line_stages_outfit 20261119-165858, 20261119-171852, 20261119-173854, 20261119-180816,
20261119-182732 (left with the red cross) and line_stages_editors 20261119-184827 (no outfit). The campaign's driver
dragged three parts, pressed "done" and the name panel's dice (614, 826), as in every other editor: "You must give your
creation a name before you can save" stayed for 22-25 tries (`coop.civarrival` phase=open, results=0). The guest's
screen (out/lane5/shots/arrival_editor_120.png, stuck_outfit_cityhall*.png, full2.png): the expanded name panel ("Name
your City Hall", Description, Tags) has **no dice** left of the name field; the kit's `text` (WM_CHAR) and key taps do
not reach the name field (the key taps went to the editor's hotkeys: its help page). On **lane 6** the same tests name
the same editor with the dice and end at the 4th "done": line_stages_editors 20261119-164538, 20261119-171852,
20261119-180816, line_stages 20261119-165858 / 20261119-184827, line_stages_nodesign_off 20261119-173854 and
line_stages_outfit 20261119-190903 (with the outfit: so the guest's own Tribal Outfitter is not the cause, as first
thought); lane 2 (fix/w57-own-editors 20261119-162318) too.

## Root cause
Not found. Observed: on lane 5 only, 6 of 6 runs; no warning in either game's log; lane 5 and lane 6 have the same
game files (sizes), the same Preferences.prop and login.prop in the guest profile (inst10 on lane 5, inst12 on lane 6);
lane 5's coop.ini is an older template (name=Alice, no dna_pace / log_detail / [report]). Inferred (unverified): something
of lane 5's bottle or guest profile (inst10) hides the random-name button (a name generator resource or a setting the
panel reads). Whether a player can type a name there is not verified (the kit's text input does not reach Spore's name
field in any editor). Next: compare the bottles' and inst10's files with lane 6, or recreate lane 5
(`tools/spore.sh lane create 5`) and rerun line_stages_editors there.

## Fix
Not fixed (a lab lane's environment, not seen on other lanes). Test side (fix/w58-linestages, BUG-1277): the
campaign's driver leaves a guest's own arrival editor with the red cross after 14 "done"
(`Campaign.arrival_cancel_after`; the player's way to keep the civilization's designs, the note names this ticket), so
line_stages / line_stages_outfit reach their Civ checks on such a lane; line_stages --civ-editors keeps no red cross
(its designs are checked: it FAILs on lane 5).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-19: opened (fix/w58-linestages): the lab runs above, the screenshots; first taken for an effect of the guest's own
  outfitter, then seen on lane 5 only (line_stages_outfit passes the editors on lane 6, 20261119-190903); open.
