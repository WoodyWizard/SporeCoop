---
id: BUG-9014
title: Members of a rebuilt native tribe disappear during subsequent simulation
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Duo native refounding stability control
role: host
versions: 0.7.3.6-duo9 native prototype
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: build/local/m2-refound-observe.py
signature: rebuilt population falls from three after native SpawnTribe
docs: docs/re/tribe.md#tribe-refounding-population-control
related: BUG-9013
---

## Report

Correcting the stale selectable entry allows three actual citizens to spawn.
Fresh controls then lose members after about 20 or 35 seconds. Native
mbDefeated remains false, the species cache is retained and food remains.
It also happens with separate village orders enabled. No native faults occur.

## Evidence

build/local/m2-refound-after-observe.log and m2-owned-observe.log fail the
90-second population check. Native probes see RemoveMember(c, false) from
DA7A2F in a behavior-tree death routine, rather than tribe unload. This is
different from BUG-9013's stale selection reference. The exact trigger of
the death behavior is still unknown. The prototype is not a playable feature.

## Next check

Compare a normal save/reload of the reconstructed tribe against the same live
sequence, before committing to a native lifecycle reset or a new tribe object.

## Follow-up (2026-11-04)

Native save/reload and explicitly setting a chief each still lose citizens
(m2-reload-observe.log, m2-refound-chief.log). A fresh SpawnNpcTribe survives
the 90-second population observation (m2-fresh-tribe.log). Retiring the old
tribe also passes the host's post-cold-load population observation
(m2-retired-cold-population.log). Use the full native factory for the future
rebuild action; the exact stale behavior state in the reused tribe is unknown.
The gameplay integration remains open. See
docs/re/tribe.md#tribe-refounding-retirement.

Integrated resolution: the BUG-9017/9018 player-facing path uses the fresh
native factory and retires the defeated object. Cold population and actual
walking pass for both founders. tribal-final-rebuild-c.log reconstructs a
host lost to a real NPC conquest; subsequent campaign-h and five-readback
checks retain both populations through the remaining victories. The exact
stale behavior in the abandoned reuse prototype is still unknown, but that
prototype is not used by the integrated rebuild action.
