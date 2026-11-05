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
branch: fix/w44-guest-player2
commits: 265ec57c
tests:
signature:
docs: docs/re/creature.md#creature-ufo-puppet, docs/re/creature.md#creature-ufo-pick, docs/known-issues.md#issue-ufo-puppet
related: BUG-0486
switch: creature.ufo_puppet
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

* Observed (Ghidra, fix/w44-guest-player2): the ship picks its creature in its BT's pick node 0x0519E372 (tick
  0xDC26B0): the last creature within 50 m of the avatar that is adult, not the avatar, of another species profile,
  not epic, 5 m or more away; the follow node hovers 20 m over it with the abduct tool and has no timeout. Nothing
  excludes a partner's puppet (docs/re/creature.md#creature-ufo-pick). The start posts stimulus 2 to the ship's own
  BT (CORRECTION of the line above: not to the creatures).
* Not observed at runtime: in the lab the ship's BT runs ~7 s (206 ticks) after `coop.drama start 2` and stops; the
  pick never reached a candidate (out/lab/20261105-110417, -110601, -110850).

## Fix
Partial, not verified: `player_rules.cpp` (`creature.ufo_puppet`): cCreatureAnimal_IsEpic answers "epic" for a
partner's puppet when called from the pick node (return address 0xDC2B5F, `SporeCoop::UFOPick_IsEpicReturn`), so the
host's ship never picks the guest's creature, as it never picks the host's own. Not done: the host's ship shown on the
guest (a mirror ship). `coop.rules ufo` lists the ships, the puppets and the pick's answers.

## Verification
no-fail-run: the drama UFO does not reach its pick in the lab (its BT stops after ~7 s, seen=0 in `coop.rules ufo`
with the puppet the only creature within 70 m: out/lab/20261105-110417 with the switch off). The probe test
(coop_creature_ufo.py, the ship must never hover over the puppet) is not in the suite: it cannot fail here.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (T1), root cause observed in logs and code.
- 2026-11-04 (fix/w43-transitions, T1): the ship's start and beam found (docs), not fixed; known issue.
- 2026-11-05 (fix/w44-guest-player2, P2 of 0.7.8): the ship's target pick found (Ghidra), a guard behind creature.ufo_puppet; not verified at runtime (the lab's ship does not pick), still open.
