---
id: BUG-1160
title: C7: Tribe member animations get their own message type (MSG_TRIBE_ANIM) instead of MSG_NPC_ANIM's whitelist
status: fixed
kind: tooling
stage: Tribe
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-tribe
commits: f0181eb5, e57d8f2d, a2c1ef8a
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
- Unit: tests/tribe_tests.cpp `anim_type` (the type per peer: none / tribe 1.0 / 1.1 / later minor / rev 2 / switch
  off; the kinds equal 0.7.14's whitelist), tests/wire_tests.cpp `test_tribe_anim` (golden bytes of K_GAMEOVER,
  K_HOST_PAUSE, K_AIR, K_HOLD_ITEM, a hold end, each against tribe.cpp's old writer), tests/validate_tests.cpp
  `test_manifest` (71 TRIBE_ANIM the one type added after 0.7.14: Module, route Tribe on a guest, host-only).
- Lab with a 0.7.14 peer (v0.7.14 built from `git archive`): `mixed_version_tribe` 20261115-181422 (this build hosts:
  "the host sends MSG_TRIBE_ANIM to this build and MSG_NPC_ANIM to the old release (sent new / old (562, 321))",
  Bob takes MSG_TRIBE_ANIM rx 562, Bob plays the relayed animations rx 337, the old Carol rx 203) and
  `mixed_version_tribe_host` 20261115-181759 (a v0.7.14 host: this build's Carol gets no MSG_TRIBE_ANIM, plays them
  from MSG_NPC_ANIM rx 236, the old Bob rx 331); both with the B6 warning and no fault.
- The Tribe tests on this build: 20261115-162402 (22/22, f0181eb5) and the final run (below).
- no-fail-run: a protocol move without a bug behind it; the old path is the one a 0.7.14 peer gets, both mixed runs
  prove it.
- The whole Tribe set on the split tree (b7e3706c + suite entry, a2c1ef8a): lab 20261115-182010, 136 of 142 (tribe_*,
  play_tribe, campaign_ct 782 s, tribe_outfit); the 6 others: tribe_mirror_hold, tribe_village_cadian,
  tribe_conquer_orders, tribe_bounce_mirrors OK in the rerun 20261115-211527 (known flakes of the host's own game:
  the chief's staff, the terrain, the conquest cinematic, the game-over caption); tribe_save_strip OK in 20261115-215636
  (its third game on lane 5: in 182010 the lab had given it lane 9, which another run took meanwhile); tribe_attack_spot
  failed on rel/0.7.15 too: BUG-1184, fixed, OK in 20261115-212912.
- After the merge of rel/0.7.15 b7f9ae9a (C2 / C3 of feat/w51-core2, d724de2c / 2a483b88): 20261115-212912 15 of 15
  (play_tribe --quick, tribe_host_pause, tribe_gameover_wait, tribe_away, tribe_cycle_restart, tribe_mirror_lifetime,
  tribe_owners_session, tribe_editor_world, tribe_bounce_world, tribe_host_reload_reset, tribe_other_world,
  tribe_pause_leak, tribe_outfit, tribe_cmd_listed, tribe_attack_spot), 20261115-215251 (mixed_version_tribe,
  mixed_version_tribe_host), 20261115-215636 (play_tribe --quick again, tribe_save_strip); make test passes.

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-tribe): MSG_TRIBE_ANIM, tribe family 1.1, wire/tribe_anim.h, per-peer type; mixed runs both
  directions.
