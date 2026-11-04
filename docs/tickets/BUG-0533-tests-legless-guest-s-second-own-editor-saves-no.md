---
id: BUG-0533
title: tests: legless_guest's second own editor saves no new creature (the guest has 15 DNA after the first editor's charge)
status: open
kind: test
stage: Creature
reported: 2026-11-04
source: fix/w41-cell-left (BUG-0345 follow-up)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0278, BUG-0466, BUG-0345
---

## Report
`legless_guest` (coop_legless_guest.py --guest new) fails at "the guest changed its creature and the host shows the
new one": in the guest's second own editor (`coop.vote editor` on the guest, the game's own entry) the test drags a
mouth onto the creature, presses done and Save New, but My Creations gets no new creation ("back from the editor, no
new own creature (the avatar keeps ...)"). Seen on rel/0.7.5 lane 6 (out/lab/20261104-043804, in the rel worktree)
and on lane 4 with creations.guest_world_species on (out/lab/20261104-052627) AND off
(out/lab/20261104-053218): not the BUG-0345 rule. The last passing run is out/lab/20261102-163032 (main checkout),
before BUG-0466 (the own editor charges its parts).

## Root cause
Observed: after the first own editor the guest has 15 DNA ("own editor result: ... remaining DNA 65", then the
host's total); the screenshot of the second editor (out/lane4/shots/legless_guest_next_editor_new.png) shows no new
mouth on the body and 15 DNA in the editor. Inferred, not checked: since BUG-0466 the part the test adds costs more
DNA than the guest has, the edit does not happen, done leaves without a save. Also a sync-check gate
"n: 1 missing on the guest" in both lane 4 runs (not looked at).
Tried (not kept): `coop.dna add 200` on the host before the second editor: the host had 215, the guest still 15 when
its editor opened (the total had not reached it), and then the editor did not close at all ("the guest saves its
edit" failed too; out/lab/20261104-054104, out/lab/20261104-055343). To do: give the guest DNA its own game has
(wait until `coop.dna` on the guest shows it) before the editor, or a free part.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
