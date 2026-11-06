---
id: BUG-9054
title: Second founder nation remains NPC controlled with a separate budget
status: open
kind: bug
stage: Civilization
reported: 2026-11-05
source: Automatic founder handoff native test
role: host guest
versions: duo10-dev
fixed_in:
branch: duo-species
commits:
tests: tools/tests/duo_civ_founder_control.py
switch: civ.founder_control
signature: founder2231 city owner-1 vehicle owner254 NPC order9
docs: docs/re/civ.md
related: BUG-9046
---

DuoQACivFoundersA correctly retains guest species0680BF38 in native nation2231,
but existing co-op ownership only assigns cities/vehicles in the host nation.
The new nation independently constructs buildings, spawns a vehicle and issues
NPC order9. Its native wealth is zero while the host holds4000. Native city and
vehicle selection also use the single-player ownership checks. Implement player
control, shared economy and shared progression before calling this playable.

Initial native control FAIL/PASS and verified functions are recorded in
docs/re/civ.md#civ-automatic-founders-control. The fixed-a regression passes
ownership, idle AI suppression, own-nation purchase and native guest move order.
Further checks cover actual UI selection and shared budget availability.

2026-11-05: shared available budget passes in ControlB and CivInputA. Native
cursor binding was a second cause: it still used the home country's +D4 vector.
The getter wrapper now supplies the guest country's native list. The unpatched
m2-civ-native-input-control.log fails actual drag selection; patched
m2-civ-native-input-fixed-a.log passes both players' drag/mouse movement, host
city-menu purchase and rejection of partner selection, with zero native faults.
Tests: tools/tests/duo_civ_native_controls.py. Keep open for shared progression,
city planner and absent-partner control qualification.
