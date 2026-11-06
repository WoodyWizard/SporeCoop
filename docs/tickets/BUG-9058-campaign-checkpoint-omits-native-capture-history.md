---
id: BUG-9058
title: Campaign checkpoint omits native capture history on the guest
status: not-reproduced
kind: bug
stage: Civilization
reported: 2026-11-05
source: Native history integration regression
role: host guest
versions: duo10-dev
fixed_in:
branch: duo-species
commits:
tests:
switch:
signature: host history stage3 has captures but guest stage3 is absent after reload
docs: docs/re/civ.md
related: BUG-9056
---

m2-civ-history-fixed-b.log: capture totals/technology/species/ownership pass,
host history has [(3,3),(3,4)], guest history is empty after the checkpoint.
Both native processes report zero faults. The host's raw native stage map has
stage3; the guest's map does not. The serialized campaign history is not
qualified merely by mirrored ownership or nation capture totals.

Native pause Save at E03097 calls History_Get then thiscall7EBDB0(manager,-1),
ret4+151, before posting save message01CD20F0. The mod posts that save message
directly. Test the missing native pre-save history step before adding any new
history wire format or mutating the native event list.

Native control m2-civ-history-native-flush.log FAIL: calling the exact pre-save
step then requesting another save/share did not restore stage3 on the guest.
Do not implement that call as a claimed fix. Investigate native loading and
the serialized history resource next. Host/guest nation counters still agree.

CORRECTION (2026-11-05, duo-species): the premise is disproved. Getter67DE50
returns the telemetry manager, whose vtable is adjacent to telemetry.dat and
App/Telemetry. Function7EB6D0 writes telemetry.dat; serializer7EB210 saves only
categories7 onward. AE3740 emits telemetry, not the player-visible timeline.
The missing guest telemetry is not evidence of missing campaign history.
The experimental telemetry hook and misleading regression have been withdrawn.
Actual player-visible history remains unqualified; inspect UITimeline instead.
