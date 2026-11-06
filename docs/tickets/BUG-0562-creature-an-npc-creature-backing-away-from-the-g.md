---
id: BUG-0562
title: Creature: an NPC creature backing away from the guest gets stuck inside its nest (video)
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
related:
---

## Report
The guest posted a video: a creature got stuck into the nest after backing away from it.
Reports: SporeCoop-report-2026-11-04 (Discord video) (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Discord video only, no log line. Inferred: the host's game walks the NPC back (its AI), the guest's proxy follows
  the host's path through the nest model; not analysed.

* fix/w45-creature-reports (P3 of 0.7.9): no log or frame of the video in the reports (only a Discord video, not
  available to the agent). The 0.7.5 guest log of that session has 22 lines "npc: proxy N ... 1.4 m under this game's
  ground ... the host's NPC 0.0 m from it (driven)": proxies drawn inside the guest's ground, the class fixed in 0.7.8
  (`drive::at_sender_agl` / npc.proxy_ground, BUG-0425 / V1). Inferred: the NPC "inside its nest" is a proxy drawn in the
  nest mound on the guest (the mound is collision, not terrain: BUG-0434 / BUG-0435 for puppets), or a proxy walked
  against the mound by the guest's own locomotion while the host's NPC backed off through it (the backing away is the
  BT node 0x02D852E0, docs/re/creature.md#creature-roar-relay). Regression check in the lab: npc_walk (the host's NPCs
  wander, walk, run and flee around the guest at the fixture's home nest; the guest's proxies sampled every frame).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced in the lab. Package W2/P1 (not in the plan table).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open, not analysed (video only).
- 2026-11-05 (fix/w44-guest-player2, P2 of 0.7.8): not started (the time went to BUG-0501, BUG-0558, BUG-0496, BUG-0557).
- 2026-11-05 (fix/w44-guest-player3, P2b of 0.7.8): not reached (the time went to BUG-0600 and BUG-0465).
- 2026-11-06 (fix/w45-creature-reports, P3 of 0.7.9): not reproduced (video only); likely the 0.7.8 height class or BUG-0434; npc_walk run as the regression check; left open.
