---
id: BUG-1592
title: Tribe & Civ Rivals: guest camera starts over host village/capital instead of own assigned tribe/capital
status: fixed
kind: bug
stage: All
reported: 2026-11-26
source: AI playtest (v0.8.2.3)
role: guest
versions: 0.8.2.3
fixed_in: 0.8.2.4
branch: main
commits: 20fa22f2
tests: rivals_civ_spec_sw, make test
signature: guest enters Tribe or Civ Rivals looking at the host village/capital
switch: rivals.tribe_guest_camera, rivals.civ_guest_camera
docs:
related:
---

## Report
When entering Tribe Rivals or Civilization Rivals as a guest, the camera stayed centered on the host's village/capital rather than panning to the guest's own assigned tribe hut or capital city.

## Root cause
After perspective swap in `tribe_rivals.cpp` and `civ_rivals.cpp`, neither module centered the local camera once on the guest's assigned home coordinates.

## Fix
Added `COOP_FIX("rivals.tribe_guest_camera", "BUG-1592")` in `mods/coop/tribe_rivals.cpp` and `COOP_FIX("rivals.civ_guest_camera", "BUG-1592")` in `mods/coop/civ_rivals.cpp` to center the guest camera once on their own assigned tribe hut or capital city upon entering the stage.

## Verification
- PASS run: `out/lab/20261127-001053` (`rivals_civ_spec_sw`, PASS) and `make test`.
- no-fail-run: camera focus quality-of-life fix with twin switches `rivals.tribe_guest_camera` and `rivals.civ_guest_camera`.
