---
id: BUG-0670
title: Civ: the host's own building designs resolve to the guest's own creations of the same id on the guest
status: fixed
kind: bug
stage: Civ
reported: 2026-11-07
source: found in fix/w46-tribe-civ (BUG-0647 analysis)
role: guest
versions: 0.7.9
fixed_in: 0.7.10
branch: fix/w46-misc
commits: cfd3464e
tests: civ_host_building
signature:
docs: docs/re/civ.md#civ-host-buildings
related: BUG-0647, BUG-0469, BUG-0424
---

## Report
Code review in fix/w46-tribe-civ (BUG-0647): a building of a city on a guest is built by cCity::SetBuildingModel 0xBDB940, which reads the civilization's design key itself (docs/re/civ.md#civ-content-all-kinds). A design of the host's own My Creations (its city hall, house, factory made in the Civ editors) has a key the guest's game resolves to the guest's own creation of the same id, or to nothing: the guest sees other buildings than the host. Not reproduced in the lab yet (the fixture's designs are premades).

## Root cause
Observed (lab lane 9, out/lab/20261107-120600): with another building model under the same id in the guest's My
Creations, the guest's copy of the host's house (built in session, and loaded with the host's world after a rejoin)
had the guest's model (bem 9A907DC4, the host's 0B25F69B). cCity::SetBuildingModel 0xBDB940 reads the civ's design
key and calls the building's model object SetModelKey (vt 0x94 of +0x34); the key is at building +0xC4, as for
vehicles (docs/re/civ.md#civ-host-buildings).

## Fix
civ.cpp host_buildings_tick: once a second a guest checks every building of every city; a design key the host
answers as its own (creations::host_design, the BUG-0647 messages) gets the host's records under a key of ours and
the building SetModelKey with it. The building's own key goes back into +0xC4 while the game saves (save hooks) and
with SetModelKey when the session ends, so no key of ours reaches a save. Switch: `coop.fix civ.host_buildings off`.

## Verification
civ_host_building (house of the test design X: model hash on the guest = the host's, new and after a rejoin):
FAIL with SPORE_FIX_OFF=civ.host_buildings: out/lab/20261107-120600;
PASS: out/lab/20261107-120446. Not verified: a guest's own save while re-keyed buildings exist (the save hook path).

## History
- 2026-11-07: opened.
- 2026-11-07: reproduced and fixed in fix/w46-misc (civ.host_buildings).
- 2026-11-07: released in 0.7.10.
