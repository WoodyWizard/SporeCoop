---
id: BUG-9003
title: The tutorial adventure crashes when two captains enter together
status: fixed
kind: bug
stage: Space
reported: 2026-11-03
source: Duo fork Windows lab
role: host
versions: 0.7.3.6-duo5
fixed_in: 0.7.3.6-duo6
branch: duo-species
commits:
tests: tools/tests/duo_adventure.py
signature: SporeApp.exe+0x80714E
docs: docs/re/space.md#space-duo-adventure
related:
---

## Reproduce

Connect two Steam 2024 Galactic Adventures instances at the galaxy menu. Open the
tutorial from Galactic Adventures on each, roll a captain on each and play.
Both entered mission 6666E0D1. The host registered the guest puppet before native
scenario initialization finished. The host then read address 1 at game RVA 80714E.
The stack included cScenarioPlayMode::SetCurrentAct and Initialize. The guest
remained in the mission and lost its host.

Duo 6 uses a network-position marker without a native scenario actor. Multiple
two-captain runs reached the playable mission and its second act with zero faults.
