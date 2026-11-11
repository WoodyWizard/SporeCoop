---
id: BUG-0845
title: Two captains in one adventure: the partner's creature puppet in the scenario cast crashes the host at the act change (suspected)
status: open
kind: crash
stage: Space
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9003
role: host
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in:
branch:
commits:
tests:
signature: fork: SporeApp.exe+0x80714E (Steam 2024) read of address 1 in cScenarioPlayMode::SetCurrentAct from Initialize
docs: docs/re/space.md#space-fork-adventure-notes
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
