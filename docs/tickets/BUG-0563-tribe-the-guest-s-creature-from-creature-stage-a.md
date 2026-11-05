---
id: BUG-0563
title: Tribe: the guest's creature from Creature stage appears as an enemy monster at the start of Tribe; killed, it leaves meat that never runs out
status: open
kind: desync
stage: Tribe
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
related: BUG-0345
---

## Report
The host: after starting Tribe the guest's creature appeared as an enemy monster; they killed it and its meat is infinite. On Discord a video: the creature in the cutscene was the guest's previous creature from Creature stage.
Reports: SporeCoop-report-2026-11-04 190929 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (logs): at the host's Creature -> Tribe change (18:57:47) the guest's game was still in Creature
  ("partner in another mode"), the host's game had the guest's puppet (species 7C6E99B3, the guest's own creation
  29158399) until then and saved for the share ("save: 4 objects of ours are kept out of it").
* Inferred: the game's Creature -> Tribe conversion took the guest's puppet (or its herd) into the Tribe world
  as an animal (the cutscene creature, the "monster" with endless meat: a herd of ours without the game's
  tuning). Our objects must not cross the stage change (T1/W2).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced in the lab (campaign_tribe / tribe_snapshot do the live change; not rerun). Package W2 (plan table).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W2), cause inferred from the logs.
