---
id: BUG-9030
title: Refounding the host village loses its campaign totem and upgrade
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Tribal conquest investigation
role: host and guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_capture_rebuild.py --victim host --campaign
signature: rebuilt host tribe has null native totem pointer at 19D0
docs: docs/re/tribe.md
related: BUG-9017 BUG-9025
---

Existing rebuilt-host saves have no native totem, unlike the original founder.
Native reward processing skips rewards when that pointer is absent. The
replacement path creates an NPC-style tribe and retires the old tribe without
preserving totem history. Reproduce using an earned-conquest checkpoint before
choosing the native restoration path. Preserve the existing food/population
setback; do not reset the shared campaign's earned conquest history.

Fixed by reconstructing the earned segment/color values in a fresh native
totem, restoring the verified community level, and refreshing the hut model
after replacing the native player reference. No intrusive pointers are copied.

FAIL: m2-totem-preserve-control.log with tribe.refound_campaign disabled.
PASS: m2-totem-preserve.log, automatic two-game save/reload and native movement.
PASS: m2-totem-cold.log, fresh processes loading cloned saved profiles.
PASS: m2-totem-next-conquest.log, subsequent guest conquest advances the restored
totem from one to two segments and native unlocks from (1,3,4,5,7) to
(1,2,3,4,5,6,7) on both games. No native faults. The short conquest fixture
supplies food and leaves the final lethal strike to the native citizen AI.
