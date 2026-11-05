---
id: BUG-0558
title: Creature: the UFO that picks up the players at the end of Creature cannot pick up the guest (the guest does not see the ship)
status: open
kind: bug
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
docs: docs/re/creature.md#creature-ufo-puppet, docs/known-issues.md#issue-ufo-puppet
related: BUG-0486
---

## Report
Both players: the spaceship tries to pick up the guest but cannot; the guest does not see the ship.
Reports: SporeCoop-report-2026-11-04 182712, 182720 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s3524 / s3528: the guest stands on grass under the moon; the host's screen at the same second shows another place (blue, plants like the sea floor): the two games are not at the same spot when the ship comes.
Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (logs): the host's game started the UFO event at 18:26:20 ("drama: ufo event starts (host or
  alone)"); the guest's game blocked its own UFO events 5 times (creature.guest_events, BUG-0486) and nothing sends
  the host's UFO to the guest (drama.cpp only blocks; no message). The host's UFO tries to take the guest's puppet
  in the host's game; the guest's game has no ship. (Not the end of Creature: the drama UFO abduction mid-stage.)

* Observed (Ghidra, GOG 2024, fix/w43-transitions): the event's start 0xD4F8A0 spawns the ship over the own avatar
  and posts BT stimulus 2 (`SporeCoop::BT_PostStimulus`); the ship takes a creature with the abduct tool
  (cAbductToolStrategy, hit 0x10323E0: animations 0x3CC7EAB / 0x3CC7EB4 and a cAbductionBeamLocomotion).
* Inferred: the guest's puppet is a creature next to the host's avatar in the host's game, so the host's ship takes
  it; the mod places the puppet at the guest's position every frame, so the beam never lifts it ("trying but
  cannot"); the guest's game has no ship (its own visit is refused by creature.guest_events, the host's not sent).

## Fix
Not done in T1 (0.7.7): the ship's target pick is not found; see docs/re/creature.md#creature-ufo-puppet for the two
ways (keep puppets out of the host ship's pick, or relay the visit and play a partner's abduction on its own avatar).

## Verification
Not reproduced in the lab. Package T1 (plan table); the mechanism is a host event the guest lacks (W1/W2 style relay).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (T1), root cause observed in logs and code.
- 2026-11-04 (fix/w43-transitions, T1): the ship's start and beam found (docs), not fixed; known issue.
