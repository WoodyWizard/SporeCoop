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
branch: fix/w44-guest-player2
commits: 2ce39e6a
tests:
signature:
docs: docs/re/creature.md#creature-eat-body
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

* Observed (Ghidra, fix/w44-guest-player2): the eat cursor 0xD2FFD0 needs a dead creature (+0xB5E) of another
  species whose meat +0xB84 is above 0. Runtime (out/lab/20261105-112339): the guest's dead proxy has the host body's
  meat (100 / 100) and dead flag: the guest's game may eat it. Eating by clicks in the lab was unreliable for both
  players (the host's control too), so not judged. Inferred: the reported bodies were missing from the guest's game.

## Fix
Not fixed: not reproduced. `coop.rules meat <id>` prints a body's meat and dead flag on either game.

## Verification
no-fail-run: not reproduced (out/lab/20261105-112339; corpse_positions PASS out/lab/20261104-202315).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (P1), not reproduced: corpse_positions PASS out/lab/20261104-202315.
- 2026-11-04 (fix/w43-guest-player, P1 of 0.7.7): not fixed, not reproduced: no time left in P1 for the pack-kill case; no eat predicate of the player was found on the way.
- 2026-11-05 (fix/w44-guest-player2, P2 of 0.7.8): the eat rule found (meat +0xB84), the guest's bodies have it; not reproduced, not fixed.
