---
id: BUG-0554
title: Creature: the guest's creature is not stunned by an NPC's charge
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
The guest: "I don't get stunned by other creatures' charge". A charge from an NPC hits the guest but does not stun it as it does in the host's own game.
Reports: SporeCoop-report-2026-11-04 175959 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (logs): an NPC's hit on the guest reaches the guest only as MSG_DAMAGE ("damage: 1 hit by proxy
  owner=npc posse=? amount=N from the host", 244 lines in the guest's log), applied by npc.cpp with
  game::apply_damage (the game's TakeDamage on the guest's avatar). No mod line about a charge or a stun.
* Inferred: the charge ability's stun is its effect on the victim in the host's game, i.e. on the guest's puppet
  there; the guest's own avatar never gets it (P1: the guest as a player in the game's rules).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced in the lab (no test makes an NPC charge the guest yet). Package P1 of docs/plan-0.7.7.md.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (P1), cause inferred from the logs and npc.cpp, not reproduced in the lab.
