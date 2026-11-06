---
id: BUG-9050
title: Intermediate Tribal graduation designs trigger an unsafe evolution save
status: fixed
kind: crash
stage: Tribe Civilization
reported: 2026-11-05
source: Native graduation continuation after absent pen guard
role: host guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_graduation_cityhall.py
switch: tribe.graduation_save
signature: native serializer BCAB07 while sharing between City Hall and vehicle design
docs: docs/re/tribe.md
related: BUG-9046 BUG-9048 BUG-9049
---

m2-graduation-pen-fixed-a.log passed the previous null-pen point, but then
crashed during an automatic world save. Returning from City Hall to Tribe
is treated as species evolution and schedules a share. After the cinematic,
Spore asks for vehicle editorF670AA43 at s111.341. The pending share saves
at s111.377 while the intermediate village is dismantled for graduation.
Serializer crash runtimeEBAB07/canonicalBCAB07 follows at s112.649.
Report crash-20261105-024857-i4.txt. Do not save the intermediate graduation
scene; send the actual Civilization stage when it is ready. Preserve native
editor cancellation, normal Tribal outfit sharing and join/leave recovery.

Qualification update: m2-graduation-full-a.log reached Civilization on both
clients, zero faults, original species and roster intact. The final busy:-
assertion timed out at Spore's first-use camera tutorial, tracked in BUG-9052.
This is not a complete pass. Native city hall and vehicle designs used real
UI input; the second editor now has the same verified request context as the
first (+1C Tribe,+90 community), scoped to the active graduation world.

Final qualification: tribal-graduation-lifetime-final.log completes economic
graduation with a90-second vehicle-editor hold and guest-local member removal;
both games reach Civilization with matching custom designs and no faults.
tribal-roles-offline-final.log completes military graduation when the guest
leaves during the vehicle editor, then rejoins after host advancement. Both
native capitals, original founders and current outfits survive. Both runs
end with fault-free clean exits (corresponding *-quit.log files). Native first
City Hall entry has no cancel button in this graduation context; ordinary
Tribal outfitter cancellation remains covered by BUG-9043/9044.
