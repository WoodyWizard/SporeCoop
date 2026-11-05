---
id: BUG-0555
title: Creature: NPCs bite and charge the guest's creature while it flies (glides)
status: open
kind: bug
stage: Creature
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0418
---

## Report
The guest: other creatures can attack it while it is flying (bite, charge, maybe strike). In the game's own rules a flying creature cannot be hit by those.
Reports: SporeCoop-report-2026-11-04 180055 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (logs): the guest's hits come from the host's game (MSG_DAMAGE, see BUG-0554) at 18:00:54 too; the
  host's game decides the hit on the guest's puppet, which walks on the ground in the host's game.
* Inferred: the game's flight immunity (a gliding creature cannot be bitten / charged) checks the victim's own
  locomotion; the puppet on the host is not flying, so the host's NPCs hit it (P1).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced in the lab. Package P1.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (P1), inferred, not reproduced in the lab.
