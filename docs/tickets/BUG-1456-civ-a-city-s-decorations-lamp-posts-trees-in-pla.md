---
id: BUG-1456
title: Civ: a city's decorations (lamp posts, trees in planters, fountains, braziers) differ between the games (the city_ornament sync is Space only)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-23
source: lab
role: guest
versions: 0.7.16
fixed_in: 0.7.99
branch: rel/0.7.99
commits: 9506ce24
tests: host_civ_ornament, host_civ_ornament_off
signature:
docs:
related: BUG-0541
switch: civ.city_ornament
---

## Report
Key frame review of the 0.7.16 final check, lab 20261122-194210 (nightly on 5376d388), same camera on both games
(`sameview`) in the guest's city: the city's decorations stand at other places or are other objects on the two games.
* `out/lab/20261122-194210/frames/sheets/030_line_stages_08_civ_guest_citizen1.jpg`: the host has a street lamp where the guest has a tree in a planter.
* `out/lab/20261122-194210/frames/sheets/031_line_stages_09_civ_guest_citizen2.jpg`: the host's lamp post in the middle of the plaza, the guest's tree
  planter and lamp elsewhere.
* `out/lab/20261122-194210/frames/sheets/035_line_stages_editors_08_civ_guest_citizen1.jpg` / `036_..._09_...`: the host's fountain and tree planters,
  the guest's crystal monument, other trees and braziers.
* The same in older runs: lab 20261120-133703 and 20261121-003202, line_stages / line_stages_editors frames 08 / 09
  (a fountain or a mushroom planter on one game only). Not new in 0.7.16.
Each game's picture is plausible on its own; a player sees it only when both players look at the same city and talk about
it. To reproduce: `tools/frames_review.py <run> --test line_stages` after any run of line_stages.

## Root cause
Inferred from docs/re/space.md#space-city-ornaments: a city's decorations (noun 0x18C88E4, City_MakeDecorations
0xBE6A50) are made per game and differ per load; the mod's `city_ornament` kind (worldobj.cpp, BUG-0541) syncs them in
Space only ("~ Civ cities run 0xBE6A50 too; the kind is Space only (no Civ detector row, not tested there)").
A fix would extend the kind to stage Civ (and add a Civ detector row).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
PASS: out/lab/20261124-152417 (host_civ_ornament, 0 faults).
FAIL: out/lab/20261124-161419 (host_civ_ornament_off: guest did not bind city_ornament snapshots in Civ).

## History
- 2026-11-23: opened (fix/w66-frames, key frame review of the 0.7.16 final check, lab 20261122-194210 on 5376d388).
- 2026-11-24: released in 0.7.99.
