---
id: BUG-1161
title: D3: the Tribe rules (owner dealing, member hold, snapshot apply) in tribe_logic.h with unit tests
status: fixed
kind: test
stage: Tribe
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-tribe
commits: f0181eb5
tests: tests/tribe_tests.cpp
signature:
docs: docs/01-architecture.md#arch-tribe-wave-c
related: BUG-1140, BUG-1154, BUG-0620, BUG-0857, BUG-0665, BUG-0173, BUG-0671
---

## Report
Plan item D3 of docs/plan-0.7.15.md (audit STG-8, TST-9): the Tribe rules had no unit test; tribe.cpp (8k lines) was
tested only in the lab.

## Root cause
Structure: the rules were inline in the game glue.

## Fix
mods/coop/tribe_logic.h (portable): owner dealing (`new_owner`: E1's claim, the players in turns, BUG-0671's purchase;
`resplit_owner`; `balance`: a newcomer's even share, plain members first, nobody below the share), the member hold
(`body_hold`: BUG-0620's bodies kept at most 60 s while dead in the list and their tribe is one of the snapshot's), the
snapshot apply decisions (`grow_up` BUG-0857, `make_mirror` BUG-0665, `pick_bind`: the nearest unbound member within
6 m, `ko` BUG-0173). tribe.cpp's host pass, guest snapshot and guest_tick call them (C1 moved these places anyway).
tests/tribe_tests.cpp (`make test`, unit-cpp under ASan/UBSan): each rule with its switch off showing the old
behaviour where it has one.

## Verification
- `make test` (unit-cpp: tests/tribe_tests.cpp under ASan / UBSan: 5 groups, all pass). The rules carry their old
  behaviour where a switch has one (tribe.mirror_grow, tribe.join_body, tribe.ko off: the old answer is checked).
- The lab runs of C1 that drive these functions: 20261115-162402 (22/22) and the final run (below).
- no-fail-run: the rules were extracted unchanged (no bug behind them); a mutation (e.g. `<=` in pick_bind's 6 m,
  the turn counted for a claim) fails tribe_tests.
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
- 2026-11-15 (feat/w51-tribe): tribe_logic.h and tests/tribe_tests.cpp.
