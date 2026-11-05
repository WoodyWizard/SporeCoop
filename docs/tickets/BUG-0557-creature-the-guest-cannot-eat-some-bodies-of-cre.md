---
id: BUG-0557
title: Creature: the guest cannot eat some bodies of creatures the players killed
status: open
kind: desync
stage: Creature
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: both
versions: 0.7.5
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0383
---

## Report
Both players: the guest cannot eat some of the bodies of creatures they killed together.
Reports: SporeCoop-report-2026-11-04 181848, 181852 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s3016 / s3024: the host sees two bodies of killed creatures by a nest; the guest's screen at the same time shows that ground without them (a darker view).
Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (logs, s 3016 / 3024): no "eat:" line from the guest for those bodies; at s 3016 the guest's
  screen shows the ground by the nest without the two bodies the host sees.
* Lab: corpse_positions (BUG-0383, bodies of NPCs killed standing, running, thrown, on a slope, by an NPC, while
  the guest was away, eaten) on 0.7.6: see Verification. Inferred: the bodies the guest misses are kills whose
  proxies were gone on the guest (out of its 110 m, or removed with their herd) or kills by the host's pack (P1).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
corpse_positions PASS on 0.7.6 code (out/lab/20261104-202315): every kind of body (standing, running, thrown, slope,
by an NPC, while the guest was away, eaten) where the host has it; the reported case (bodies by a nest after a
pack fight) is not among them. no-fail-run: not reproduced. Package P1.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (P1), not reproduced: corpse_positions PASS out/lab/20261104-202315.
- 2026-11-04 (fix/w43-guest-player, P1 of 0.7.7): not fixed, not reproduced: no time left in P1 for the pack-kill case; no eat predicate of the player was found on the way.
