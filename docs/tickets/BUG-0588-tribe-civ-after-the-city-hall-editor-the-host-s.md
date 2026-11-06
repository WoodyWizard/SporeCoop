---
id: BUG-0588
title: Tribe -> Civ: after the City Hall editor the host's own NPCs read at (0, 0, 0) and the sync check reports MISMATCH; the Creature evolve-in-place path runs for the Tribe editors
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-04
source: lab (fix/w43-transitions, coop_tribe_civ.py)
role: host
versions: 0.7.6
fixed_in: 0.7.8
branch: fix/w44-world-state2
commits: 724c27f7 d21a18ff 040d4314 94022d49 cd2f986f 06f729dd 9f7a1a8e
tests: tribe_civ (coop_tribe_civ.py)
signature:
docs: docs/re/tribe.md#tribe-stage-end
related:
switch: diag.stage_end
---

## Report
Seen in the runs of the BUG-0576 test (tribe_civ, lane 4, a scratch run at 22:26 on fix/w43-transitions):
* when the game asked for the vehicle editor (the second Tribe -> Civ editor) the host logged "evolved in place: Bob
  keeps its world (host species 00000000 -> 00000000, age -1 -> -1, migration -1 -> -1, cinematic 1)" and "Bob gets
  your new body in place (no reload)", and after each editor "Alice evolved the species: everybody gets the new
  body": the Creature / Cell evolve-in-place path (BUG-0508) runs for the Tribe -> Civ building and vehicle editors;
* 30 s after the City Hall editor the host's sync check reported `MISMATCH ... n: 46 apart`, the host's own NPC
  rows "here" at pos (0.0 0.0 0.0) with hp set (n51, n71, n73 ...).
No harm seen in the test (both games reached Civ); not analysed.
Reproduce: `tools/lab.py run tribe_civ --lane N`, read the host's log between the two editors.

## Root cause
Observed: back from the City Hall editor the host's game takes its Tribe world down for the Civ stage: the player
tribe's food mat (+0x260 = 0) and totem pole are gone (lab 20261105-123325, 20261105-123819) and, in lab
20261104-224338, its 46 animals read at (0, 0, 0) while the guest's proxies stayed: the host's sync-check compared
them and reported "n: 46 apart". A guest's game runs no such phase. The notice and the evolve-in-place path took the
City Hall and vehicle editors for an evolution ("Alice evolved the species: everybody gets the new body", lab
20261105-123819 before the last commit). Inferred: the animals' move to (0 0 0) belongs to the City Hall cinematic;
it did not happen in the 3 runs of 2026-11-05 (no MISMATCH with the fix off either).

## Fix
`tribe::stage_end()` (the player tribe without its food mat in Tribe: cTribeUI +0x28 / +0x29 read 0 by then): the
host's sync-check compares nothing while it is true (switch `diag.stage_end`; off: compared, 0.7.7); coop.wstate
prints `st phase stage_end=1` and the detector skips the sample as `apart`. The notice after the Tribe -> Civ
editors (the last EditorRequest's editor 99E92F05 / F670AA43 / 9AD7D4AA / 7CB21BAD, or stage_end) says "made a
design for the Civilization stage: it comes next". The in-place path itself stays (Civ sends the world).

## Verification
tribe_civ, new step after the City Hall editor: the host's game is in the end of Tribe (stage_end) and 6 sync-checks
asked in 30 s give no MISMATCH. PASS out/lab/20261105-123819; with `SPORE_FIX_OFF=diag.stage_end`
out/lab/20261105-124141 also passed: the animals did not go to (0 0 0) in these runs, so the MISMATCH itself has no
FAIL run.
no-fail-run: the animals at (0 0 0) after the City Hall were seen once (lab 20261104-224338) and not in the 3 runs of
2026-11-05; the stage_end sign is verified (true in both runs, the detector twin of BUG-0594).

## History
- 2026-11-04: opened.
- 2026-11-05: the sync-check and the detector skip the host's end of Tribe, the notice fixed (fix/w44-world-state2).
- 2026-11-05: released in 0.7.8.
