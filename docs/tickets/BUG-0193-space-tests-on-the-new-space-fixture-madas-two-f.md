---
id: BUG-0193
title: Space tests on the new `space` fixture (Madas): two flaky runs seen (lane 6, 2026-10-23)
status: open
kind: test
stage: Space
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: 
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* `coop_space_rescue.py` round 1 failed once: the host had flown ~550 m off with 2.5 s of W and came
  back too late (repair 1.0 of 2.0 s when the 20 s ran out); round 2 then started 1330 m apart. Passed
  in the next run and on the base branch. The ship of the new fixture is faster: a shorter flight off
  would make it stable.
* `coop_space_death.py` failed once in the forced-jump step: after the OK the ship stayed dead for 47 s
  and the guard "respawn caption without a planet" did not fire (0), so the respawn caption had ended
  some other way; passed in the next run and on the base branch.

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w4-tests (wave 4): not reached (budget went to the creature tests and BUG-0149). Not measured with --repeat. Left open.
- 2026-11-04 (fix/w42-space-play): coop_space_rescue.py: the host flies off in 0.3 s pulses until the repair
  distance is 120 m (at most 2.5 s) instead of 2.5 s of W (the ~550 m flight of the report). --repeat 3 (lab
  20261104-114611): the test's checks passed 3 of 3 (rounds start 24-319 m apart, the rescue in time each time);
  run #3 failed the lab's sync-check gate instead (p0 46 m during the rescue flight: new ticket BUG-0547).
  coop_space_death.py: 3 of 3 (lab 20261104-114611 #1-#3); the 47 s dead ship of the report was not seen again,
  cause not found. Left open for BUG-0547 and the death case.
- 2026-11-04 (fix/w42-space-play): space_rescue --repeat 3 again: 3/3 (lab 20261104-123228).
- 2026-11-04 (fix/w43-tests): G1 looked at the labs since 2026-11-03: space_death 4 of 4 and space_rescue 6 of 7 ok (the one failure is BUG-0547's gate); the 47 s dead ship not seen again. Left open, not changed.
