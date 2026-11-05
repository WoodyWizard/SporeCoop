---
id: BUG-0577
title: After a rejoin the game shows the Cell and Creature stages as not done yet (stage progress lost)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in: 0.7.7
branch: fix/w43-world-state
commits: a69ee62e
tests: wsync_stages (coop_wsync_tribe.py stages), wsync_transitions
signature:
docs:
related:
---

## Report
The guest: after rejoining it shows that they need to do the cell stage and the creature stage, but they already did that.
Reports: SporeCoop-report-2026-11-04 (Discord text) (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Discord text only, no log of the rejoin. Inferred: the guest's stage progress (the History bar of the stages
  done) is its own save's; nothing sends the host's (W1: stage progress).

* Observed (lab): the stages done are cPlayer +0x10FC..+0x110C (docs/re/creations.md#creations-cplayer-fields);
  nothing sent them: a trait the host's game has is not in the guest's (wsync_stages 20261104-205424, the fix off).
* Observed (lab 20261104-205424 and 20261104-205648, the fix off): a rejoin loads the host's current world and the
  guest had the host's traits after it: the reported rejoin case is not reproduced (? the player's rejoin may have
  come from its own save, or the History bar reads another record).

## Fix
wsync row stages: the five consequence traits of the host's cPlayer, written into the guest's (no setter) whenever
they differ, after every transition. Switch `wsync.stages`.

## Verification
Not reproduced in the lab. Package W1.

Lab: wsync_stages FAIL with `SPORE_FIX_OFF=wsync.stages` out/lab/20261104-205424 (the guest lacks the host's traits
for 10 s), PASS out/lab/20261104-205427 (within 1 s, and after the rejoin). The rejoin part also passed with the fix
off (not reproduced).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W1), no evidence beyond the text.
- 2026-11-04 (fix/w43-world-state, W1): fixed; see Fix and Verification.
- 2026-11-04 (fix/w43-detector2, D0 detector): the detector compares the stages done (`All:state:stage_done`, cPlayer +0x10FC..+0x110C); not seen in the D0 runs: the creature and tribe fixtures start at their stage (no trait set on either game).
- 2026-11-05: released in 0.7.7.
