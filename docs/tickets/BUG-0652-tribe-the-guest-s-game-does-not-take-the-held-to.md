---
id: BUG-0652
title: Tribe: the guest's game does not take the held tool of a member (fields: tribe_member_tool applied 6, the game reads 11)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-06
source: lab log sweep 2026-11-06 (703 runs, 2026-11-05/06)
role: 
versions: 0.7.8, rel/0.7.9
fixed_in: unreleased
branch: fix/w46-s1-sweep
commits: 915c0e22
tests: tribe_leader_sync (coop_tribe_leader_sync.py)
signature:
docs: docs/re/tribe.md#tribe-leader-sync
related: BUG-0514, BUG-0583
---

## Report
Seen in 55 runs on 10 branches, also on rel/0.7.9 runs 20261106-153514 (3 tests) and 20261106-200316 (4 tests): `fields: tribe_member_tool N tool: applied 6.000, the game reads 11.000`. The held-tool replication (tribe.tool_hold) does not take on the guest. Example: out/lab/20261106-013035/tribe_coop.guest.game.log:406 (rel/0.7.9 worktree). Related: BUG-0514 (fixed 0.7.5) names the field but not this warning.

## Root cause
* Observed (lab): the warning is the leader branch of apply_held_tool (tribe.leader_tool, BUG-0583): the mirror led its tribe in the
  guest's game while the host's member did not (`tribe: this game's tribe leader keeps its staff (the host's member holds tool 6)`
  right before every warning; probe 20261107 on lane 4: NPC tribe 1's members #5 / #6 hold tool 6, the host's leader #4).
  The leader (cCommunity +0x11C) was each game's own; in tribe_walk_player#2 (fix/w45-lead-player 20261106-212040) the guest's
  mirror #5 led NPC tribe 1. Visible: that mirror kept the chief's staff (no instrument), the mirror of the host's chief had a
  staff too: two chiefs.
* Observed (lab 20261107-114312): right after a guest's load an NPC tribe of its game had no leader at all ("the leader was none").
* Inferred: a mirror made again (a load, a respawn of the leader's mirror) leaves the game's own SetChieftain(0) choice, the
  first adult.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
`tribe.leader_sync` (tribe.cpp): the members' field kind (tribe_member) has a `leader` field (F_LEADER = 3: 2 leads, 1 not); the
guest's game calls cTribe::SetChieftain (GOG 2024 0xC8FEE0, `SporeCoop::cTribe_SetChieftain`, verified in Ghidra) on the mirror of
the host's leader. Applied before the tool kind in the same pass: the old leader takes the host member's tool at once. Test command
`coop.tribe chief <id>` (guest). Off: each game's own leaders (0.7.9).
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
tribe_leader_sync: FAIL with SPORE_FIX_OFF=tribe.leader_sync out/lab/20261107-114534 (the guest's #5 still led tribe 1 after 15 s,
held tool 7 vs the host's 6; also 20261107-114145 with the first version of the test); PASS out/lab/20261107-114426 (the host's
leader #4 led in the guest's game 1.1 s after the setup, #5 held the host's tool 7).
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened from the orchestrator's lab log sweep; planned for 0.7.10.
- 2026-11-07 (fix/w46-s1-sweep): fixed; see Root cause / Verification.
