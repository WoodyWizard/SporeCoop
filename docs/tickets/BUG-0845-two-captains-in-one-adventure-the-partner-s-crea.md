---
id: BUG-0845
title: Two captains in one adventure: the partner's creature puppet in the scenario cast crashes the host at the act change (suspected)
status: not-reproduced
kind: crash
stage: Space
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9003
role: host
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in:
branch: fix/w48-small
commits: 3a3c5d3b 3156086b
tests: adventure_duo, adventure_duo_host_first
signature: fork: SporeApp.exe+0x80714E (Steam 2024) read of address 1 in cScenarioPlayMode::SetCurrentAct from Initialize
docs: docs/re/adventure.md#adventure-two-captains, docs/06-testing.md#testing-p1-g1
related: BUG-0426
---

## Report
Two players who host/join at the galaxy menu and then start the same Galactic Adventures mission (the tutorial)
together: the fork's host crashed at SporeApp+0x80714E inside cScenarioPlayMode::SetCurrentAct / Initialize; the
guest stayed in the mission without its host. Inferred to happen also at later act changes.

## Root cause
* Observed (code): modes.cpp:1598 the Adventure adapter (kScenario) has creature_spawn / creature_drive /
  creature_despawn, so a remote captain is a Creature-stage animal puppet inside the scenario; coop.cpp:441
  creature_puppet() includes the Scenario adapter.
* Observed (tests): only coop_mode.py --adventure covers adventures; it joins after both games are already in the
  adventure and checks puppets, not act changes, and it is not in tools/tests/suite.py.
* Inferred (fork's analysis): the scenario (re)builds its cast on initialization and act changes and faults on the
  foreign animal. Confidence medium: suspected for our code until reproduced.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages PR #196, BUG-9003 and docs/re/space.md#space-duo-adventure); NOT FINAL: taken from the fork, verify on our code and against our rules before using.

* modes.cpp: the Adventure adapter keeps place/local/fill but its spawn, drive, despawn and tag entries are nullptr
  ("a position label with no game object"). coop.cpp drive_puppet returns early when the adapter has no spawn;
  creature_puppet() no longer includes Scenario; puppet_why reports "captain marker" in Scenario.
* coop.cpp on_overlay: in Scenario the partner's name is drawn at the network position from its state buffer (sample
  at now, age < 5 s, same place, PF_IN_WORLD and not PF_DEAD), not at a puppet.
* Extra in the fork (a feature, not needed for the crash): shared goal credit via the play controller
  (cScenarioMode+0x78) and the goal-credit thiscall F1ACF0, see the docs/re/space.md section added with this ticket.
* Their test: tools/tests/duo_adventure.py --host 4 --guest 5 --gate host (and guest-first): both captains reach the
  playable mission and the second act with zero faults.

## How to test
New lab test (adventure, 2 games): host and join at the galaxy menu, both start the tutorial adventure (the
adventure_start.py clicks), play to the next act (walk to the first goal; a test command or the gate). Assert: no
fault or crash report on either game, both in act 1 or later. FAIL on main if the suspicion holds (host crash at
SetCurrentAct), PASS with the marker-only adapter. Also add the existing coop_mode.py --adventure to suite.py.

## Fix
Not reproduced after a serious attempt; no fix (the Adventure adapter keeps its creature puppets).
* Found: in our build two players never have each other's captain in an adventure at all. Each game plays its own copy of the adventure: the scenario resource key (cScenarioMode +0x74 -> +0x10: instance / group) differs per play (6FCBCE96, E6F78525, 288BE48A seen for the same tutorial, group 408A0000), so the Adventure adapter's place ids differ and both games say "partner in another world": no puppet, nothing foreign in the cast (lab 20261112-122759, -123202).
* The fork's case forced: `coop.adventure place <hex>` (tests) gives both games one place id; then each game has the partner's captain as a creature puppet in its scenario. Act changes with the puppet there: the host's game's own change 0 -> 1 and a JumpToAct on the guest (host first, out/lab/20261112-124436); JumpToAct on the host and the guest's own change (guest first: the host's Initialize ran while the guest's captain was already in the same place, the fork's order, out/lab/20261112-125719). No fault, no crash report, both games in act 1 with the puppets alive after it.
* Tools added: `coop.adventure [act <n> | place <hex>]` (play state +0x90, act +0xB8, the resource key, the place; JumpToAct 0xF1F3C0 = SporeCoop::ScenarioPlay_JumpToAct), test adventure_duo (+ _host_first), the intro card (play state 1 until Enter) documented.
* Not tried: the fork's own adventure and Steam build; later acts of longer adventures; the guest's captain dying in an act. If adventure co-op (BUG-0793) makes the place ids equal, run adventure_duo without `place` again.

## Verification
no-fail-run: not reproduced. With the partner's captain as a puppet in the scenario (forced one place): out/lab/20261112-124436 (host first), out/lab/20261112-125719 (guest first, the fork's order), act 0 -> 1 on both games each time, faults 0, no crash report. Without forcing (the default): no puppet in an adventure (out/lab/20261112-122759, -123202). Two guest-first runs with the forced place did not get one game into the adventure (its galaxy menu clicks were lost: out/lab/20261112-123647, -124902; the test now takes a picture and starts once more).

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-12 (fix/w48-small): not reproduced (no puppets in adventures by default; with forced places act changes passed in both orders).
