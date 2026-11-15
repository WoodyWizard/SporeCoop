---
id: BUG-1160
title: C7: Tribe member animations get their own message type (MSG_TRIBE_ANIM) instead of MSG_NPC_ANIM's whitelist
status: open
kind: tooling
stage: Tribe
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in:
branch: feat/w51-tribe
commits:
tests: tests/tribe_tests.cpp, tests/wire_tests.cpp, tests/validate_tests.cpp, mixed_version_tribe, tribe_mirror_anims, tribe_party_music, tribe_host_pause, tribe_gameover_wait
signature:
docs: docs/04-netcode.md#netcode-tribe-anim, docs/01-architecture.md#arch-tribe-wave-c
related: BUG-1137, BUG-1142, BUG-0890, BUG-1086, BUG-0960
---

## Report
Plan item C7 of docs/plan-0.7.15.md (audit STG-5): about 15 Tribe sub-kinds (the members' animations, holds, phases,
tracks, air height, the theme and party music, the game-over and pause waits) went as MSG_NPC_ANIM kinds through a
whitelist in npc.cpp (`kind 2..12, 14, 15, 16, 20, 21`) to tribe::on_member_anims; every new kind needed an edit there,
and an old guest ignored a new kind silently.

## Root cause
Structure: one message type shared by two modules.

## Fix
MSG_TRIBE_ANIM (id 71; 70 is HOST_BUSY of feat/w51-core2), a row in net/messages.def, the tribe family's minor bumped
1.0 -> 1.1 (older versions ignore an unknown type: no conflict, the B6 warning only); route Tribe, class Module (tribe.cpp
decides per kind what applies outside Tribe: `wire::tribe_anim::any_mode`). The payload is the same bytes as after
MSG_NPC_ANIM's type: wire/tribe_anim.h (the kind list, `is_tribe_kind`, `any_mode`, `type_for`, the structs Wait, Air,
Hold that tribe.cpp now writes; golden bytes in tests/wire_tests.cpp `test_tribe_anim`). The host picks the type per
guest (`anim_send`, through the new `net::Session::peer_families`): MSG_TRIBE_ANIM for a guest that listed tribe 1.1 or
later, MSG_NPC_ANIM for every other one. A guest takes both: npc.cpp still passes the tribe kinds of MSG_NPC_ANIM on
(`is_tribe_kind`, the old whitelist), for a 0.7.14 host. With a 0.7.14 peer: a 0.7.15 host sends that guest
MSG_NPC_ANIM exactly as 0.7.14 did; a 0.7.14 host's MSG_NPC_ANIM is played by a 0.7.15 guest as before; nobody is
refused. Status: `tribe anim msg: fix= sent_new= sent_old= rx_new= rx_bad=` in coop.status. Switch
`coop.fix tribe.anim_msg off`: MSG_NPC_ANIM to everyone (0.7.14), kept: the lab's path for an old peer.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
