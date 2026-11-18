---
id: BUG-9060
title: Military and religious Tribal graduation editors miss the native pause exception
status: fixed
kind: bug
stage: Tribe Civilization
reported: 2026-11-05
source: Fresh five-conquest Vantiegan graduation
role: host
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_graduation_cityhall.py
switch: tribe.graduation_vehicle_types
signature: editor7D433FAD held60 seconds at input_lock,paused after ready
docs: docs/re/tribe.md
related: BUG-9048 BUG-9050
---

tribal-offline-final.log fails at the first military vehicle editor. The test
also incorrectly accepts only the economic editor. Native log4 shows military
request7D433FAD at s168.445, both ready at169.397, then a hold at input_lock,
paused until the generic60-second fallback opens it at229.423. It is a long
avoidable stall, not a permanent deadlock. No faults were recorded.

Live request28C0B9E0 has +0C=7D433FAD, blank model at+10, return Tribe at+1C,
and community0116DD1B at+90, matching the already verified economic context.
The scoped graduation pause exception hardcodes economic F670AA43.
The community SDK CommonIDs.h identifies the other native land vehicle types:
military7D433FAD and cultural/religious9AD7D4AA. Preserve all context, blank-model,
active-graduation, actual pause-dialog and cinematic guards when accepting them.

Controlled native regression, tribal-roles-offline-final.log: with the new
switch off, the military request remains held beyond12 seconds after both
players ready (expected failure). Enabling the fix releases that same request
in0.62 seconds, without disabling the general cinematic/pause-dialog guards.
The guest then leaves during the vehicle editor. Host graduation finishes;
the guest rejoins Civilization with both original founders, current outfit
species2916C9C5/7CFFC56B, separate capitals and matching custom designs.
tribal-roles-offline-quit.log records zero faults and clean exits. Economic
graduation passed independently; the religious ID is SDK-verified but has not
yet had its own complete native-editor replay.
