---
id: BUG-9015
title: The fresh replacement-village prototype crashes when its saved world loads
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-04
source: Duo replacement-settlement cold-load control
role: host
versions: 0.7.3.6-duo9 native prototype
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: build/local/m2-fresh-check.py
signature: C99B00 reads null +34 after fresh tribe replacement and cold load
docs: docs/re/tribe.md#tribe-refounding-population-control
related: BUG-9013, BUG-9014
---

## Report

A new native SpawnNpcTribe retains three citizens and a chief for more than
90 seconds, unlike reuse of the defeated cTribe. The conqueror owns the old
hut and food mat. After native save and clean exit, loading that world on the
host crashes at unrebased C99B00, reading address 34.

## Evidence

PASS live: build/local/m2-fresh-tribe.log. FAIL cold:
m2-fresh-cold-ownership.log. Crash preserved in m2-fresh-cold-crash.txt
(crash-20261104-002424-i4.txt). The defeated old cTribe was left in the world
with references to its former primary hut and food mat. Whether that is the
crash's direct cause is still being checked. This is not a released gameplay
path; the verified Duo 9 package contains no rebuilding implementation.

## Follow-up (2026-11-04)

C99B00 dereferences the result of GetToolByType(10), which was null. The crash
dump does not directly identify the affected tribe. A fresh control transfers
the old hut's remaining owner reference, detaches the old tribe's primary
buildings through IntrusivePtr_Assign, then uses the noun manager's normal
DestroyInstance path. After save and clean exit, both cold-loaded peers retain
the conqueror's old buildings and the new original-species village. Evidence:
m2-retired-cold-ownership.log and m2-retired-cold-population.log. With the
separate BUG-9016 mirror fix, duo_tribe_rebuild_checkpoint.py passes the full
native ownership/population/chief checks on both peers
(m2-retired-native-checkpoint-fixed.log). This remains a prototype, so the
capture/rebuild integration and this ticket remain open.

Integrated resolution: BUG-9017/9018 now perform the verified retirement
and checkpoint refresh in the actual capture/rebuild action. Both founder
directions pass native ownership, population and walking after cold load;
BUG-9030 additionally preserves the rebuilt host's history and hut level.
Fresh Vantiegan capture/rebuild and subsequent five-victory progression pass
in tribal-final-rebuild-c.log and tribal-final-campaign-h.log. The unsafe
tool-less old tribe from this prototype is not retained by the integrated path.
