---
id: BUG-1437
title: evo_screens_switch slow/noready/lowfps: the test's own Enter ended the host's caption stand before its BUG-1396 move (no animal moved)
status: open
kind: test
stage: Creature
reported: 2026-11-22
source: lab
role: 
versions: 
fixed_in:
branch: fix/w65-night
commits:
tests: tools/tests/coop_evo_screens.py (evo_screens_switch, evo_screens_switch_slow, evo_screens_switch_noready, evo_screens_switch_lowfps, evo_screens_switch_lead_gap_off)
signature:
docs:
related: BUG-1396, BUG-1376, BUG-1438
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, rel/0.7.16 5376d388): evo_screens_switch_slow,
evo_screens_switch_noready and evo_screens_switch_lowfps FAIL with "BUG-1396: the host's game did not stand: no animal
moved" and then "FAIL the test moved one of the host's animals while its game stood (None) and compared its copy after
(- m)". evo_screens_switch (plain) passed that check in the same run (n21 moved, 0.1 m after).

## Root cause
Test (the BUG-1396 fix): the stand did happen in all three, but the test's own Enter ended it before the move.
Host game logs (`host busy: none (why 0060)` = kWhyCaption|kWhyStill: the caption stand begins; trace
`busy=cinematic,input_lock,paused`) against the key frame 04 times (written right after `good()` returned):
- slow: stand 21:43:26.8 - 21:45:02.8 (96 s), key frame 04 at 21:45:09.
- lowfps: stand 22:18:31.7 - 22:19:16.9 (45 s), key frame 04 at 22:19:24.
- noready: the cinematic 22:18:50 - 22:19:12, the caption stand 22:19:12.1 - 22:19:13.7 (1.6 s), key frame 04 22:19:20.
- plain (passed): stand 22:27:20.8 - 22:28:30.1, key frame 04 at 22:27:44: `good()` had returned before the caption came.
The caption comes while the guest is still in (or just back from) its own editor. `good()` calls `confirm_captions()`,
which presses Enter on the host whenever its cinematic plays; its first call (30 s after both games are back in
Creature) confirmed the caption, and only then `move_host_animal()` waited 30 s for a stand that was over. The caption
does not end by itself (slow: 96 s until the test's Enter; plain: 70 s, ended by the Enter at the 45 s mark), so whether
the move happened was a race of the guest's editor time against the caption (the plain run won it by ~2 s).
Not the mod: the stand happened in all four runs; only the test's order of Enter and move differed.

## Fix
coop_evo_screens.py --part switch: `HostStand` replaces `move_host_animal`. It is ticked from every wait after both
editors (the sleeps, `confirm_captions()` inside `good()` and `host_runs()`, the 45 s wait). While the host's game
stands under its caption (game time paused and its cinematic playing) and no animal was moved yet, the test does not
press Enter on the host's caption (a player reading it); once a host animal 3-25 m away has a copy on the guest (the
guest may still be in its own editor), it is put 12 m farther out as before, the caption is held 10 s more (the gap
of the host's states stays far over kLateGap, 2 s), then confirmed; 8 s after the host's game goes on the copies are
compared (3 reads), as before. So the move happens in every run where the caption comes, whenever it comes.
Precondition: when no stand gives a host animal 3-25 m away at all (where the family stood: luck), the test prints
`RESULT: INCONCLUSIVE` (docs/06-testing.md#testing-lab-verdicts); a stand that never came or a guest without copies
of the host's near animals stays a FAIL (the mod can cause those). The checks themselves are unchanged.

Checked offline: the HostStand state machine against a stub game (stand at 3 s, the guest's copy at 6 s): no
Enter before the move, the move at the copy, Enter after the hold, the reads after the host went on, sync-force
on/off around them.

## Verification
Open, waiting for the lab run:
- PASS expected: `SPORE_DETECTOR=20 python3 tools/lab.py run evo_screens_switch evo_screens_switch_slow
  evo_screens_switch_noready evo_screens_switch_lowfps --lane N --lane M --retry 0 --no-build` (each prints
  "BUG-1396: while the host's game stands: npc ... put at ..." and "ok the test moved one of the host's animals").
- FAIL expected (the BUG-1376 twin): `SPORE_DETECTOR=20 python3 tools/lab.py run evo_screens_switch_lead_gap_off
  --lane N --retry 0 --no-build` ("the guest's copies of the host's animals stand where the host's game has them after
  its caption" FAIL, the moved animal ~10 m off).
- The FAILs before: 20261122-194210 evo_screens_switch_slow, _noready, _lowfps.

## History
- 2026-11-22: opened.
- 2026-11-22: fix/w65-night: cause from the host logs and key frame times; the test holds the host's caption until its move.
