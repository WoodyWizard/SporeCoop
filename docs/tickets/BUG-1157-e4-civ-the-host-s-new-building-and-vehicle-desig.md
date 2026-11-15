---
id: BUG-1157
title: E4: Civ: the host's new building and vehicle designs never reach a guest kept in place
status: fixed
kind: desync
stage: Civ
reported: 2026-11-15
source: plan
role: guest
versions: 0.7.14
fixed_in: unreleased
branch: fix/w51-editors
commits: cebe2441, ea6cf28b
tests: civ_designs
signature:
docs: docs/re/creations.md#creations-stage-designs, docs/re/civ.md#civ-culture-sync
related: BUG-0508, BUG-0647, BUG-0670, BUG-1158
---

## Report
Playability audit of wave E (out/audit-0.7.15/playability/tribe_civ.md, item 2), confirmed in the lab: the host picks
or makes a new building or vehicle design in Civ. A guest kept in place after the host's editor (BUG-0508) kept the
old designs: its cities and its mirrors of the host's vehicles looked different from the host's until the next world
from the host, while the notice said "everybody gets the new designs".

## Root cause
Observed (lane 4, the FAIL twin): the designs are the player civilization's culture set (cCivilization +0x6C, an EASTL
map design id -> key) set by Civ_SetDesign 0xBF4C50 (planner pick, editors' return), which re-models the civ's
buildings and vehicles of that design. The host's City Hall and military land vehicle took the new designs, the
guest's culture set, City Hall (07EDB5B0) and mirror (D9F4E98F) did not: no culture set sync existed, and mirrors are
built from the guest's own entry (civ.cpp mirror_design).

## Fix
designs.cpp, wsync row `civ_design` (id 17; older versions skip it): the host's culture set entries; a guest whose
entry differs calls the same setter on its player civilization, then the host's own My Creations come by the
BUG-0647 / BUG-0670 paths (civ::redesign_vehicles re-keys the civilization's vehicles, civ.host_buildings the
buildings). Switch: `coop.fix civ.design_sync off`.

## Verification
civ_designs (both profiles with other creations under the test ids; the host's City Hall and vehicle designs set with
the game's setter; checks the guest's culture set and the bem hashes of its City Hall and of its mirror = the host's):
FAIL with SPORE_FIX_OFF=civ.design_sync,editors.guest_designs: out/lab/20261115-131006;
PASS: out/lab/20261115-131752 (two runs each; civ_host_building, civ_host_design, tribe_editor_world 2x PASS in the same run). Earlier PASS runs: out/lab/20261115-130639 (tribe_outfit), out/lab/20261115-130858 (civ_designs).
Not verified: the host's real editor route end to end (vote, building editor, save); a design id the host has none of
yet is not sent (a guest's own pick there stays its own until the host makes one).

## History
- 2026-11-15: opened.
- 2026-11-15: reproduced and fixed in fix/w51-editors.
