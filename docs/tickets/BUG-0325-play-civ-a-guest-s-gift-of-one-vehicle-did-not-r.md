---
id: BUG-0325
title: play_civ: a guest's gift of one vehicle did not reach the host, and one guest command was rejected (seed 11)
status: open
kind: desync
stage: Civ
reported: 2026-10-30
source: play_civ.py --seed 11, fix/w14-tribe-civ
role: guest
versions: fix/w14-tribe-civ (main b3101d2 + branch)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0206
---

## Report
`SPORE_LANE=7 python3 tools/tests/play_civ.py --minutes 10 --seed 11` (fix/w14-tribe-civ, 2026-10-30,
log out/w14/play_civ_fix_11.log + both game logs of the worktree, not in git): RESULT FAIL with two
problems: "16:06:32 did not happen: guest sees [9] owned by the partner and no longer selected"
(the guest's bot gave vehicle 9 to the host in the co-op window; 4 s later the guest still had it
as its own) and "guest commands: sent 66, the host executed 65 and rejected 1". The vehicle sync of
the run was good (p90 0.22 m, max 1.52 m). Seen once (the same seed's run before the branch had
neither).

## Root cause
Not analysed. Code reading: the host counts a rejection for a malformed gift (bad `to`) or a
command for a vehicle / city the sender does not own; a gift of a vehicle the sender does not own
on the host is skipped silently (not counted). Candidates (inferred): the vehicle changed owner or
died on the host between the guest's selection and the gift, or a command raced a capture.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
