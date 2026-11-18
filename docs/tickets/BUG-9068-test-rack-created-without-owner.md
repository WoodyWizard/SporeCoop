---
id: BUG-9068
title: Native rack probe crashed before completing ownership assignment
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-06
source: Disposable sixth-rival rack diagnosis
role: host
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_sixth_site.py tools/tests/duo_tribe_six_rivals.py
signature: SporeApp.exe+A93A8C null+556 after isolated rack creation
docs: docs/re/tribe.md
related: BUG-9024 BUG-9069
---

The diagnostic split native CreateTool and SetTribe across console calls.
The host crashed with a read at 0x556. Its normal player save was never
loaded. The precise consumer is unverified. Do not repeat this separated
creation sequence. Choosing a normal NPC archetype by itself still left the
village without an instrument. The trial now stocks a free native building
slot and completes rack ownership and layout in the same callback.
Evidence: six-credit-rack-crash.txt, six-rack-control.log. This reproduces
the previously documented E93A8C owner+556 failure in the Tribal tool research.
The existing atomic tool_fixture command initializes ownership in the same
native callback. six-atomic-rack.log confirms the social rack count rose from
zero to one. The audience then requested music and both peers recorded its
native opinion at 3, where the preceding no-rack runs had remained at zero.
six-native-site.log and six-native-five.log verify the integrated rival
factory, an actual native alliance, twelve surviving performers and zero
faults. The split-call crash probe must never be run.
