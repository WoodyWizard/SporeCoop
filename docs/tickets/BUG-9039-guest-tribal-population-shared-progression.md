---
id: BUG-9039
title: Guest Tribal population capacity must follow shared campaign progression
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Shared campaign capacity audit
role: guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_capacity.py --control
switch: tribe.shared_capacity
signature: Shared upgrade gives host capacity9 but guest6 on host and12 on client
docs: docs/re/tribe.md
related: BUG-9020 BUG-9038
---

The guest founder uses NPC archetype15. Native C8EC70 selects the archetype
capacity for that tribe, whereas the native player branch uses community+38.
The earlier birth test qualified the six-member cap on its initial fixture.
Compare actual native capacities after an earned campaign upgrade before
changing behavior. Shared progression must include population unlocks.

CORRECTION (2026-11-04): m2-capacity-control-a.log shows the rebuilt guest
already has archetype0, level0. Its host-side IsPlayerOwned override selects
capacity6 from that level. The client does not apply that override and selects
the default non-player capacity12. The home founder is level1/capacity9 on
both. Thus the mismatch is not solely the original archetype15 path.

CORRECTION (2026-11-04): the earlier archetype15 interpretation was wrong.
The verified SpawnNpcTribe arguments are (position, archetype, count, food,
flag, species). Existing founding passes archetype0 and food15. The native
player-ownership/level branches above fully explain the observed mismatch.

The capacity hook recognizes the registered guest founder on the host and
the host-assigned own village on the guest. It calls the original getter on
the native campaign tribe, following earned upgrades and native tuning. It
does not change species, faction, unrelated NPCs or bypass the native cap.

PASS m2-capacity-fixed-a.log on fresh CapacityFixedA profiles: disabled fix
reproduces9/6/12; enabled fix gives9 to both founders on both peers. Actual F9
births produce members7/8/9; birth10 is rejected without food charge. Unrelated
NPC limits remain unchanged. Both games exit with zero native faults.
