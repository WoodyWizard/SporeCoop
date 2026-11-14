---
id: BUG-1096
title: tests: the campaign driver loops on editor done for 30 min when its parts drag is lost at low fps
status: fixed
kind: test
stage: tribe
reported: 2026-11-14
source: lab 20261113-231423-2 campaign_tribe_end
role: host
versions: rel/0.7.14
fixed_in: unreleased
branch: fix/w49-civedit
commits: 8ab36fd1, bb09b2b3
tests: tribe_civ_editor_slow, campaign_tribe_end
signature:
docs: docs/06-testing.md#testing-w49-civedit
related: BUG-1091, BUG-0576
---

## Report
rel/0.7.14 (HEAD "Ticket index after fix/w49-npcwalk"), campaign_tribe_end lab 20261113-231423-2 (lane 5): the Tribe
goal bar 5/5, the evolve vote 4/4 (host 23:35:47, editor=99E92F05), the host Tribe -> Editor at 23:35:50, then the
campaign driver logged "Alice: editor done" every ~8 s for 30 min and the stage never became Civ ("[stuck] tribe/advance:
the stage change tribe -> civ did not finish in 30 min"). fix/w49-camp2 passed the same point in 20261113-224609.
Reproduce: `SPORE_FPS=host:3` and the driver's one drag (suite `tribe_civ_editor_slow` with `CAMPAIGN_OLD_EDITOR_DRAG=1`).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
A test-driver problem under machine load, not a mod bug (and not one of the merges after fix/w49-camp2).
Observed:
- The host's stuck screenshot (out/lane5/campaign/20261113-231425/campaign_02_stuck_Alice.png): the City Hall editor
  with an empty plot and the red "You need at least 3 parts to Save". Every "done" did what the game does then: nothing.
- The driver (coop_campaign.editor) drags the three parts once, at the third "done" try (23:36:15 "three parts dragged
  into an empty editor"), and never again. At that time the machine was loaded (load average 25-31, an unrelated
  process at ~780% CPU): the host ran 11.8-14 fps with hitches of 0.3-2.7 s, the three other games of the lane 3-7 fps
  (the same in lane 6's run). The drag (0.3 s press, 10 moves in 0.8 s, release) fits in a few frames: lost.
- The passing run 20261113-224609 made the same clicks at 65 fps: both editors left in about 35 s.
- The mod side is the same in both runs: vote end, "the editor does not wait for the game's own input_lock,paused
  (BUG-0576)", Tribe -> Editor in 2.7 s, then `busy=input_lock,paused,editor` (as in the passing run); no refused
  request, no vote, no fault. Nothing for a FIXOFF bisection of the later merges.
- Reproduced in 5 min: the host at 3 fps (SPORE_FPS) loses the same drag: one part on the plot, the same red line, 14
  "done" tries in the editor (20261114-000115).
- campaign_tribe_noclick 20261113-231423 (lane 6) is a different case: its City Hall editor ended in 37 s; the planted
  lost clicks plus the slow raids under load brought the goal bar to 5/5 only at 23:50:53 and the 30 min ran out.
- `coop.ui find "You need at least 3 parts to Save"` does not see the red line (always False), so the retry counts tries.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
tools/tests/coop_campaign.py: `editor_parts_due(n)`: the first drag at the third try as before; while the editor stays
up the parts are dragged again every 4 tries (n = 6, 10, 14, ...), each slower (`drag_slow(..., pace)`: the waits 2x, 3x,
at most 4x), noted `the editor still up after N "done": three parts dragged again (pace P, BUG-1096)`.
`CAMPAIGN_OLD_EDITOR_DRAG=1`: one drag only (before). New test option `coop_tribe_civ.py --player-editor` (the editors
left the driver's way), suite `tribe_civ_editor_slow` (`SPORE_FPS=host:3`).
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
- FAIL: 20261114-000115 (lane 5, `CAMPAIGN_OLD_EDITOR_DRAG=1 lab.py run tribe_civ_editor_slow`): the host stays in the
  City Hall editor after 14 "done" tries ("You need at least 3 parts to Save", one part on the plot).
- PASS: 20261114-000120 (lane 6, the fix): the City Hall editor left after the drags of tries 2, 6 and 10 (14 tries), the
  vehicle editor after 8, both games in Civ, no faults. (The try cap was raised from 14 to 20 after this run: the
  City Hall needed all 14.)
- Load only, confirmed: campaign_tribe_end 20261114-001918 (lane 5, rel/0.7.14's mod code and its old driver,
  `CAMPAIGN_OLD_EDITOR_DRAG=1`, after the foreign load ended at 00:15): PASS in 1614 s; the host at 60-65 fps left the
  City Hall and vehicle editors after the one drag each (4 "done" tries each), Civ after 936 s of the Tribe advance.
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-14: opened.
- 2026-11-14: fixed on fix/w49-civedit (test driver); FAIL 20261114-000115, PASS 20261114-000120.
