---
id: BUG-1438
title: evo_screens_switch: the lab's sync-check gate caught the difference the test made on purpose (its BUG-1396 move during the host's caption stand, n21 12.9 m), not marked sync-force
status: open
kind: test
stage: Creature
reported: 2026-11-22
source: lab
role: 
versions: 
fixed_in:
branch: fix/w65-night
commits: 5101d015
tests: tools/tests/coop_evo_screens.py (evo_screens_switch and its variants)
signature:
docs:
related: BUG-1396, BUG-1437
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, rel/0.7.16 5376d388): evo_screens_switch printed RESULT: OK
but the lab's sync-check gate failed it: host `22:28:06.264 [s 195.309] sync-check: MISMATCH with player 1 'Bob'
(Creature 9652F8AD, 2 checks 3 s apart, 30 objects compared): n: 1 apart (n21 12.9)`, the same on the guest.

## Root cause
Test: n21 is the animal the test itself moved (BUG-1396): "BUG-1396: while the host's game stands: npc 21 put at (137.27
-466.88 -129.82)"; the host's sync-check line has n21 at exactly that place (pos=(137.3 -466.9 -129.8), spd 0), the
guest's copy at its old place (want=(136.9 -470.3 -117.4), age=46.51 s: no NPC state goes out while the host's game
stands). The host's caption stand lasted 22:27:20.8 - 22:28:30.1; the mod's 30 s sync-check slot at 22:28:06 fell into
it. The difference is the one the test makes on purpose; 8 s after the host's game went on the copy stood 0.1 m from
the host's (the test's own check, which is what BUG-1376 claims). The test did not tell the lab (`LAB-MARK
sync-force`, docs/06-testing.md#testing-log-gates), so the gate counted it. Not the mod, not a flake: any run whose
sync-check slot falls between the move and the host's game going on fails this way.

## Fix
coop_evo_screens.py: `twoinst.lab_mark('sync-force', 'on')` right before the move and `off` right after the 3 reads 8 s
after the host's game went on (the gate leaves out MISMATCH lines of that window only; the test's own 5 m check covers
that window's end). The gate itself is unchanged.

## Verification
Open, waiting for the lab run: `SPORE_DETECTOR=20 python3 tools/lab.py run evo_screens_switch --lane N --retry 0
--no-build` PASS with no sync-check gate finding (its log has `LAB-MARK sync-force on` / `off` around the move).
no-fail-run: the FAIL before is 20261122-194210 evo_screens_switch (the gate); a twin is not possible (a test marking).

## History
- 2026-11-22: opened.
- 2026-11-22: fix/w65-night: the test marks its forced difference for the gate.
